#ifndef KYTY_LOADER_CHEAT_HOTKEY_H_
#define KYTY_LOADER_CHEAT_HOTKEY_H_
// In-game cheat switches. KYTY_CHEAT_KEYS=<file>: lines "<key> <eboot offset> <original bytes> <patched bytes> <label>"
// (hex; '#' starts a comment; key F1-F3, F5, F10 or F11). The lines of one key are one cheat: the key switches all its
// places of the game's code between their original and patched bytes - the cheat's hooks in and out; its code caves
// come with the --game-patch file - with a notice on screen. A cheat starts on when the cheat file already put its
// hooks in, else off. Checked at start: every place must hold one of its two byte strings, all in the same state (else
// that key is left alone and the log says why). Without a cheat on it, F11 stays the fullscreen switch.

#include "loader/runtimeLinker.h"

#include "common/virtualMemory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace Loader::CheatHotkey {

struct Place {
	uint64_t             address = 0;
	std::vector<uint8_t> original, patched;
};
struct Cheat {
	std::string        label;
	std::vector<Place> places;
	bool               on = false;
	bool               broken = false;
};
inline std::mutex                   g_mutex;
inline std::map<std::string, Cheat> g_cheats; // by key name ("F1")

inline bool ParseHex(const std::string& text, std::vector<uint8_t>* bytes) {
	if (text.empty() || text.size() % 2 != 0) return false;
	bytes->clear();
	for (size_t i = 0; i < text.size(); i += 2) {
		char*      end   = nullptr;
		const auto pair  = text.substr(i, 2);
		const auto value = std::strtoul(pair.c_str(), &end, 16);
		if (end == nullptr || *end != 0) return false;
		bytes->push_back(static_cast<uint8_t>(value));
	}
	return true;
}

inline bool Holds(const Place& place, const std::vector<uint8_t>& bytes) {
	return std::memcmp(reinterpret_cast<const void*>(place.address), bytes.data(), bytes.size()) == 0;
}

inline void Install(const Program* program) {
	const char* path = std::getenv("KYTY_CHEAT_KEYS");
	if (path == nullptr || *path == 0 || program == nullptr || program->base_vaddr == 0) return;
	std::ifstream file(path);
	if (!file) {
		std::printf("Cheat keys: %s not readable; no cheat keys\n", path);
		return;
	}
	std::map<std::string, Cheat> cheats;
	std::string                  line;
	int                          number = 0;
	while (std::getline(file, line)) {
		number++;
		if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
		std::istringstream words(line);
		std::string        key, offset_text, original_text, patched_text, label;
		if (!(words >> key)) continue;
		Place place;
		if (!(words >> offset_text >> original_text >> patched_text) || !ParseHex(original_text, &place.original) ||
		    !ParseHex(patched_text, &place.patched) || place.original.size() != place.patched.size()) {
			std::printf("Cheat keys: %s line %d is not <key> <offset> <original> <patched> <label>; skipped\n", path,
			            number);
			continue;
		}
		std::getline(words >> std::ws, label);
		while (!label.empty() && (label.back() == '\r' || label.back() == ' ')) label.pop_back();
		const uint64_t offset = std::strtoull(offset_text.c_str(), nullptr, 16);
		place.address         = program->base_vaddr + offset;
		auto& cheat           = cheats[key];
		if (cheat.label.empty()) cheat.label = label.empty() ? key : label;
		if (offset == 0 || offset + place.original.size() > program->mapped_size) {
			std::printf("Cheat keys: %s line %d is outside the game's code; %s left alone\n", path, number, key.c_str());
			cheat.broken = true;
			continue;
		}
		cheat.places.push_back(std::move(place));
	}
	std::scoped_lock lock {g_mutex};
	g_cheats.clear();
	for (auto& [key, cheat]: cheats) {
		if (cheat.broken || cheat.places.empty()) continue;
		size_t on = 0, off = 0;
		for (const auto& place: cheat.places) {
			on += Holds(place, place.patched) ? 1u : 0u;
			off += Holds(place, place.original) ? 1u : 0u;
		}
		if (on != cheat.places.size() && off != cheat.places.size()) {
			std::printf("Cheat keys: %s (%s): the game's code is not the expected one (%zu of %zu places on, %zu off); "
			            "left alone\n",
			            key.c_str(), cheat.label.c_str(), on, cheat.places.size(), off);
			continue;
		}
		cheat.on = on == cheat.places.size();
		std::printf("Cheat keys: %s switches %s (%zu places), %s at start\n", key.c_str(), cheat.label.c_str(),
		            cheat.places.size(), cheat.on ? "on" : "off");
		g_cheats.emplace(key, std::move(cheat));
	}
	std::fflush(stdout);
}

// A key (window.cpp, "F1"): its cheat on or off; an empty string when it has none (the key then does what it did),
// else the line to show on screen.
inline std::string Toggle(const std::string& key) {
	std::scoped_lock lock {g_mutex};
	const auto       found = g_cheats.find(key);
	if (found == g_cheats.end()) return {};
	auto& cheat = found->second;
	using Common::VirtualMemory::Mode;
	for (const auto& place: cheat.places) {
		const auto&    bytes    = cheat.on ? place.original : place.patched;
		const uint64_t page     = place.address & ~uint64_t {0xfff};
		const uint64_t size     = (place.address + bytes.size() - page + 0xfff) & ~uint64_t {0xfff};
		Mode           old_mode = Mode::ExecuteRead;
		if (!Common::VirtualMemory::Protect(page, size, Mode::ExecuteReadWrite, &old_mode)) {
			std::printf("Cheat keys: could not make the game's code writable; %s left as it was\n", cheat.label.c_str());
			std::fflush(stdout);
			return cheat.label + ": could not be switched  [" + key + "]";
		}
		std::memcpy(reinterpret_cast<void*>(place.address), bytes.data(), bytes.size());
		Common::VirtualMemory::FlushInstructionCache(place.address, bytes.size());
		Common::VirtualMemory::Protect(page, size, old_mode);
	}
	cheat.on = !cheat.on;
	std::printf("Cheat keys: %s %s\n", cheat.label.c_str(), cheat.on ? "ON" : "OFF");
	std::fflush(stdout);
	return cheat.label + (cheat.on ? ": ON  [" : ": OFF  [") + key + "]";
}

} // namespace Loader::CheatHotkey

#endif // KYTY_LOADER_CHEAT_HOTKEY_H_
