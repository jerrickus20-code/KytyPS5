#ifndef EMULATOR_SRC_LOCAL_LATE_UPSCALE_H_
#define EMULATOR_SRC_LOCAL_LATE_UPSCALE_H_
// The late upscale (KYTY_UPSCALE_LATE=1 with KYTY_UPSCALER, loader/demonsSoulsSuperResolution.h): Demon's Souls' PS5 Pro
// path makes its whole frame at the render size - its super resolution dispatch passes the jittered 3D through
// (renderSuperResolution.cpp), and the bloom, depth of field, motion blur, the other effects and the interface after it
// are drawn at that size too -, then at the guest flip DLSS (or FSR 3) makes the finished frame the late output's size
// (2560x1440 from 1280x720) from it and the dispatch's depth, motion vectors and jitter, as a tone-mapped color.
#include "frame-gen.h"

namespace Libs::Graphics {
class CommandBuffer;
class Image;
namespace LateUpscale {

// The PS5 Pro path runs with the late upscale (DemonsSoulsSuperResolution::Late).
[[nodiscard]] bool Active();
// Renderer thread, in the super resolution dispatch (its command buffer, outside rendering): the dispatch's depth and
// motion vectors copied (the game's later passes may reuse their memory before the flip), its jitter, motion scale,
// projection and reset flag (`parameters`'s) kept for the flip.
void Capture(GraphicContext& graphics, CommandBuffer& command, Image& depth, Image& motion,
             const FrameGen::UpscaleInputs& parameters);
// The late output's size for a guest frame of `extent`: the late output's height, the width at the frame's aspect (a
// multiple of 2); `extent` itself when it is not smaller.
[[nodiscard]] vk::Extent2D OutputExtent(vk::Extent2D extent);
// Renderer thread, at the guest flip (Presenter::PrepareFrame, outside rendering): the guest's display image `source`
// upscaled to OutputExtent with what this guest frame's dispatch left (Capture), when it did and the sizes agree.
// Returns the upscaled image (transfer-source layout), which the presenter's frame copies, or null: a frame without the
// 3D (a movie, a menu, a loading screen), no upscaler on the device; the frame is stretched to that size instead.
[[nodiscard]] VulkanImage* Upscale(GraphicContext& graphics, CommandBuffer& command, Image& source);

} // namespace LateUpscale
} // namespace Libs::Graphics

#endif // EMULATOR_SRC_LOCAL_LATE_UPSCALE_H_
