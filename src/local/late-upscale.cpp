// The late upscale (late-upscale.h): DLSS or FSR 3 at the guest flip, on the game's finished frame.
#include "late-upscale.h"

#include "graphics/host_gpu/renderer/image/image.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/vulkanCommon.h"
#include "loader/demonsSoulsSuperResolution.h"
#include "frame-gen.h"
#include "fsr-native.h"
#ifdef KYTY_LOCAL_VULKAN_RECORDING
#include "vulkan-recording.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace Libs::Graphics::LateUpscale {
namespace {

// An image of the late upscale's own (not the texture cache's), with its view.
struct Owned {
	VulkanImage   image;
	vk::ImageView view = nullptr;
};

// Renderer thread (Kyty.Gpu): Capture's copies and parameters, the finished frame's copy and the upscaled frame.
Owned                   g_depth, g_motion, g_color, g_output;
FrameGen::UpscaleInputs g_parameters;
bool                    g_captured = false; // the current guest frame's dispatch left its inputs
bool                    g_history  = false; // the last guest frame was upscaled (else the next one starts a new history)
uint64_t                g_flips    = 0;     // guest flips seen (Upscale)

// An image made again (another size or format) is destroyed some flips later: the GPU, or the recording worker's queue,
// may still hold commands using it (DeleteImage frees it, or pools it for another image, at once).
struct Retired {
	vk::Image     image      = nullptr;
	VmaAllocation allocation = nullptr;
	vk::ImageView view       = nullptr;
	uint64_t      flip       = 0;
};
constexpr uint64_t   RetireFlips = 8;
std::vector<Retired> g_retired;

bool IsDepthFormat(vk::Format format) {
	switch (format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32Sfloat:
		case vk::Format::eD32SfloatS8Uint:
		case vk::Format::eX8D24UnormPack32: return true;
		default: return false;
	}
}

// The finished frame read as it is stored: its sRGB bytes as UNORM (the upscaler's output is UNORM's, nothing is
// linearized on the way), as the presenter's frame takes them (Presenter::PrepareFrame).
vk::Format StoredFormat(vk::Format format) {
	switch (format) {
		case vk::Format::eR8G8B8A8Srgb: return vk::Format::eR8G8B8A8Unorm;
		case vk::Format::eB8G8R8A8Srgb: return vk::Format::eB8G8R8A8Unorm;
		case vk::Format::eA8B8G8R8SrgbPack32: return vk::Format::eA8B8G8R8UnormPack32;
		default: return format;
	}
}

void Report(const char* text) {
	static std::atomic<uint32_t> reports {0};
	if (reports.fetch_add(1, std::memory_order_relaxed) < 16) {
		std::printf("Late upscale: %s\n", text);
		std::fflush(stdout);
	}
}

// `owned` given up: destroyed RetireFlips flips from now (DestroyRetired).
void Retire(Owned& owned) {
	if (owned.image.image != nullptr || owned.view != nullptr)
		g_retired.push_back({owned.image.image, owned.image.allocation, owned.view, g_flips});
	owned.image.image      = nullptr;
	owned.image.allocation = nullptr;
	owned.image.state      = {};
	owned.image.subresource_states.clear();
	owned.view = nullptr;
}

void DestroyRetired(GraphicContext& graphics) {
	std::erase_if(g_retired, [&](const Retired& retired) {
		if (retired.flip + RetireFlips > g_flips) return false;
		if (retired.view != nullptr) graphics.device.destroyImageView(retired.view, nullptr);
		if (retired.image != nullptr) {
			VulkanImage image;
			image.image      = retired.image;
			image.allocation = retired.allocation;
			graphics.DeleteImage(image);
		}
		return true;
	});
}

// `owned` (re)made as `format` of `extent` with `usage` and a view of the whole image; false (none) when the GPU cannot
// make it.
bool Make(GraphicContext& graphics, Owned& owned, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage) {
	if (owned.image.image != nullptr && owned.image.format == format && owned.image.extent.width == extent.width &&
	    owned.image.extent.height == extent.height && owned.image.usage == usage)
		return true;
	Retire(owned);
	vk::ImageCreateInfo create {};
	create.imageType     = vk::ImageType::e2D;
	create.extent        = vk::Extent3D {extent.width, extent.height, 1};
	create.mipLevels     = 1;
	create.arrayLayers   = 1;
	create.format        = format;
	create.tiling        = vk::ImageTiling::eOptimal;
	create.initialLayout = vk::ImageLayout::eUndefined;
	create.usage         = usage;
	create.sharingMode   = vk::SharingMode::eExclusive;
	create.samples       = vk::SampleCountFlagBits::e1;
	if (!graphics.CreateImage(create, owned.image)) return false;
	vk::ImageViewCreateInfo view {};
	view.image            = owned.image.image;
	view.viewType         = vk::ImageViewType::e2D;
	view.format           = format;
	view.subresourceRange = {IsDepthFormat(format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	if (graphics.device.createImageView(&view, nullptr, &owned.view) != vk::Result::eSuccess) {
		owned.view = nullptr;
		graphics.DeleteImage(owned.image); // (never used)
		return false;
	}
	return true;
}

// `source`'s subresource (`level`, `layer`; transfer-source layout) copied into `target` (of its size there), left
// sampled.
void CopyInto(vk::CommandBuffer command, vk::Image source, uint32_t level, uint32_t layer, Owned& target) {
	const auto aspect = IsDepthFormat(target.image.format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
	FrameGen::Transition(command, target.image, aspect, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite,
	                     vk::PipelineStageFlagBits2::eTransfer);
	vk::ImageCopy copy {};
	copy.srcSubresource = {aspect, level, layer, 1};
	copy.dstSubresource = {aspect, 0, 0, 1};
	copy.extent         = target.image.extent;
	command.copyImage(source, vk::ImageLayout::eTransferSrcOptimal, target.image.image, vk::ImageLayout::eTransferDstOptimal, copy);
	FrameGen::Transition(command, target.image, aspect, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eShaderRead,
	                     vk::PipelineStageFlagBits2::eAllCommands);
}

FrameGen::UpscaleImage Described(const Owned& owned, vk::ImageLayout layout) {
	FrameGen::UpscaleImage out;
	out.image        = owned.image.image;
	out.view         = owned.view;
	out.layout       = layout;
	out.format       = owned.image.format;
	out.extent       = {owned.image.extent.width, owned.image.extent.height};
	out.usage        = owned.image.usage;
	out.image_format = owned.image.format;
	return out;
}

// DLSS records into the command buffer itself (Streamline, NGX), so it runs where the renderer's own commands are
// recorded, in their order: on the recording worker (KYTY_VULKAN_RECORDING) as a packet, else here (as
// renderSuperResolution.cpp's).
struct UpscaleRecord {
	vk::CommandBuffer       command;
	FrameGen::UpscaleInputs inputs;
	GraphicContext*         graphics = nullptr;
	bool                    fsr      = false;
};

// The upscaler refused (an input size outside its mode, NGX's errors): the frame stretched into the output, in the
// layouts the renderer left (the color sampled, the output general) and left so.
void Stretch(const UpscaleRecord& record, const vk::detail::DispatchLoaderDynamic& dispatch) {
	const auto& in      = record.inputs;
	const auto  barrier = [&](vk::Image image, vk::ImageLayout from, vk::ImageLayout to, vk::PipelineStageFlags2 source_stage,
                             vk::AccessFlags2 source_access, vk::PipelineStageFlags2 destination_stage,
                             vk::AccessFlags2 destination_access) {
		vk::ImageMemoryBarrier2 b {};
		b.srcStageMask        = source_stage;
		b.srcAccessMask       = source_access;
		b.dstStageMask        = destination_stage;
		b.dstAccessMask       = destination_access;
		b.oldLayout           = from;
		b.newLayout           = to;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image               = image;
		b.subresourceRange    = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
		vk::DependencyInfo dependency {};
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers    = &b;
		record.command.pipelineBarrier2(dependency, dispatch);
	};
	const auto all = vk::PipelineStageFlagBits2::eAllCommands;
	barrier(in.color.image, in.color.layout, vk::ImageLayout::eTransferSrcOptimal, all, vk::AccessFlagBits2::eNone,
	        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
	barrier(in.output.image, in.output.layout, in.output.layout, all, vk::AccessFlagBits2::eMemoryWrite,
	        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
	vk::ImageBlit blit {};
	blit.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
	blit.srcOffsets[1]  = vk::Offset3D {static_cast<int32_t>(in.color.extent.width), static_cast<int32_t>(in.color.extent.height), 1};
	blit.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
	blit.dstOffsets[1]  = vk::Offset3D {static_cast<int32_t>(in.output.extent.width), static_cast<int32_t>(in.output.extent.height), 1};
	record.command.blitImage(in.color.image, vk::ImageLayout::eTransferSrcOptimal, in.output.image, in.output.layout, 1, &blit,
	                         vk::Filter::eLinear, dispatch);
	barrier(in.color.image, vk::ImageLayout::eTransferSrcOptimal, in.color.layout, vk::PipelineStageFlagBits2::eTransfer,
	        vk::AccessFlagBits2::eNone, all, vk::AccessFlagBits2::eShaderRead);
	barrier(in.output.image, in.output.layout, in.output.layout, vk::PipelineStageFlagBits2::eTransfer,
	        vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
}

void RunUpscale(const UpscaleRecord& record, const vk::detail::DispatchLoaderDynamic& dispatch) {
	const bool upscaled = record.fsr ? FsrNative::Upscale(*record.graphics, record.command, record.inputs)
	                                 : FrameGen::Upscale(record.command, record.inputs, dispatch);
#ifdef KYTY_LOCAL_VULKAN_RECORDING
	LocalVulkanRecording::InvalidateReplayState(); // (what DLSS bound)
#endif
	if (!upscaled) Stretch(record, dispatch);
}

#ifdef KYTY_LOCAL_VULKAN_RECORDING
void ReplayUpscale(std::span<const LocalVulkanRecording::Segment> segments, const vk::detail::DispatchLoaderDynamic& dispatch) {
	UpscaleRecord record;
	std::memcpy(static_cast<void*>(&record), segments[0].data, sizeof(record));
	RunUpscale(record, dispatch);
}
#endif

} // namespace

bool Active() {
	return Loader::DemonsSoulsSuperResolution::Late();
}

void Capture(GraphicContext& graphics, CommandBuffer& command, Image& depth, Image& motion,
             const FrameGen::UpscaleInputs& parameters) {
	g_captured = false;
	command.EndRendering();
	const auto vk_command = command.Handle();
	const auto usage      = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	if (!Make(graphics, g_depth, parameters.depth.extent, depth.backing.format, usage) ||
	    !Make(graphics, g_motion, parameters.motion.extent, motion.backing.format, usage)) {
		return Report("its depth or motion copy could not be made: this frame stretched");
	}
	depth.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, {}, vk_command);
	CopyInto(vk_command, depth.backing.image, parameters.depth.base_level, parameters.depth.base_layer, g_depth);
	motion.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, {}, vk_command);
	CopyInto(vk_command, motion.backing.image, parameters.motion.base_level, parameters.motion.base_layer, g_motion);
	g_parameters = parameters;
	g_captured   = true;
}

vk::Extent2D OutputExtent(vk::Extent2D extent) {
	const uint32_t height = Loader::DemonsSoulsSuperResolution::LateOutputHeight();
	if (extent.height == 0 || extent.height >= height) return extent;
	const auto width = static_cast<uint32_t>(std::lround(static_cast<double>(extent.width) * height / extent.height / 2.0)) * 2u;
	return {width, height};
}

VulkanImage* Upscale(GraphicContext& graphics, CommandBuffer& command, Image& source) {
	++g_flips;
	DestroyRetired(graphics);
	const bool captured = std::exchange(g_captured, false);
	const bool history  = std::exchange(g_history, false);
	if (!captured) return nullptr; // (no 3D in this guest frame)
	const bool fsr  = FsrNative::UpscalerRequested();
	const bool dlss = FrameGen::UpscalerReady() && !fsr;
	if (!dlss && !fsr) {
		Report("no DLSS on this device: the frames stretched");
		return nullptr;
	}
	// The finished frame holds the 3D at its own pixels: the PS5 Pro path's output at the render size (the HD output's
	// places, loader/demonsSoulsHdOutput.h). Another size (a frame the game made otherwise) has nothing to match.
	const vk::Extent2D frame {source.backing.extent.width, source.backing.extent.height};
	const vk::Extent2D depth {g_depth.image.extent.width, g_depth.image.extent.height};
	const vk::Extent2D motion {g_motion.image.extent.width, g_motion.image.extent.height};
	if (frame != g_parameters.color.extent || frame != depth || frame != motion) {
		static std::atomic<uint32_t> reports {0};
		if (reports.fetch_add(1, std::memory_order_relaxed) < 4) {
			std::printf("Late upscale: a %ux%u frame over a %ux%u 3D (depth %ux%u, motion %ux%u): stretched\n", frame.width,
			            frame.height, g_parameters.color.extent.width, g_parameters.color.extent.height, depth.width,
			            depth.height, motion.width, motion.height);
			std::fflush(stdout);
		}
		return nullptr;
	}
	const auto output = OutputExtent(frame);
	command.EndRendering();
	const auto vk_command = command.Handle();
	if (!Make(graphics, g_color, frame, StoredFormat(source.backing.format),
	          vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled) ||
	    !Make(graphics, g_output, output, vk::Format::eR16G16B16A16Sfloat,
	          vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
	              vk::ImageUsageFlagBits::eTransferDst)) {
		Report("its frame images could not be made: the frames stretched");
		return nullptr;
	}
	source.Transit(vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, {}, vk_command);
	CopyInto(vk_command, source.backing.image, 0, 0, g_color);
	FrameGen::Transition(vk_command, g_output.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eGeneral,
	                     vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
	                     vk::PipelineStageFlagBits2::eAllCommands);
	UpscaleRecord record;
	record.command       = vk_command;
	record.graphics      = &graphics;
	record.fsr           = fsr;
	record.inputs        = g_parameters; // (the dispatch's jitter, motion scale, projection, planes and reset flag)
	record.inputs.color  = Described(g_color, vk::ImageLayout::eShaderReadOnlyOptimal);
	record.inputs.depth  = Described(g_depth, vk::ImageLayout::eShaderReadOnlyOptimal);
	record.inputs.motion = Described(g_motion, vk::ImageLayout::eShaderReadOnlyOptimal);
	record.inputs.output = Described(g_output, vk::ImageLayout::eGeneral);
	record.inputs.hdr    = false; // (tone-mapped, its effects and interface in it)
	// (A new history after frames without the 3D too: the last one upscaled is not this one's predecessor.)
	record.inputs.reset = g_parameters.reset || !history;
	if (static std::atomic<uint32_t> reports {0}; reports.fetch_add(1, std::memory_order_relaxed) < 2) {
		std::printf("Late upscale: %s, the finished %ux%u frame (%s) to %ux%u\n", fsr ? "FSR 3" : "DLSS", frame.width,
		            frame.height, vk::to_string(source.backing.format).c_str(), output.width, output.height);
		std::fflush(stdout);
	}
#ifdef KYTY_LOCAL_VULKAN_RECORDING
	const LocalVulkanRecording::Segment segment {&record, sizeof(record)};
	if (!LocalVulkanRecording::EnqueuePacket(ReplayUpscale, {&segment, 1})) LocalVulkanRecording::ReplayInline(ReplayUpscale, {&segment, 1});
#else
	RunUpscale(record, VULKAN_HPP_DEFAULT_DISPATCHER);
#endif
	command.InvalidateGraphicsState(); // (what DLSS bound)
	FrameGen::Transition(vk_command, g_output.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eTransferSrcOptimal,
	                     vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eTransfer);
	g_history = true;
	return &g_output.image;
}

} // namespace Libs::Graphics::LateUpscale
