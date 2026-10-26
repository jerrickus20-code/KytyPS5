#ifndef KYTY_LOADER_CHEAT_GIVE_H_
#define KYTY_LOADER_CHEAT_GIVE_H_
// F10: give items (Demon's Souls 01.007.000, with the cheat file's "Give Items (F10)"): a panel over the game to search
// the item list by name, pick a count and give (systemOverlay.cpp), or give.txt's list from it.
// KYTY_CHEAT_GIVE_SLOT=<eboot offset>: 8 bytes in the cheat file's code caves that the emulator points at its request
// block (cheatGiveCore.h); the cave at the start of the characters' per-frame update (0x325ff0) sees a new request
// there and makes the game add the item itself (its AddItem, as the debug menu's "Set Item" does).
// KYTY_CHEAT_ITEMS=<file>: lines "<category hex> <id> <name>" (cheats\PPSA01341_01.007.000.items.txt).
// KYTY_CHEAT_GIVE_FILE=<file>: the panel's "Give give.txt" list, one item a line: "<name>" or "<name> x<count>"
// ('#' starts a comment), read at every use.

#include "loader/cheatGiveCore.h"
#include "loader/runtimeLinker.h"

#include "common/virtualMemory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Loader::CheatGive {

inline void Install(const Program* program) {
	const char* slot_text = std::getenv("KYTY_CHEAT_GIVE_SLOT");
	if (slot_text == nullptr || *slot_text == 0 || program == nullptr || program->base_vaddr == 0) return;
	const uint64_t offset = std::strtoull(slot_text, nullptr, 16);
	if (offset == 0 || offset + 16 > program->mapped_size) {
		std::printf("Cheat give: slot %s is outside the game's code; F10 gives nothing\n", slot_text);
		std::fflush(stdout);
		return;
	}
	const uint64_t slot = program->base_vaddr + offset;
	static const uint8_t zero[16] {};
	if (std::memcmp(reinterpret_cast<const void*>(slot), zero, sizeof(zero)) != 0) {
		std::printf("Cheat give: the slot at +%llX is not the cheat file's (empty) one; F10 gives nothing\n",
		            static_cast<unsigned long long>(offset));
		std::fflush(stdout);
		return;
	}
	using Common::VirtualMemory::Mode;
	const uint64_t page     = slot & ~uint64_t {0xfff};
	Mode           old_mode = Mode::ExecuteRead;
	if (!Common::VirtualMemory::Protect(page, 0x1000, Mode::ExecuteReadWrite, &old_mode)) {
		std::printf("Cheat give: could not make the slot writable; F10 gives nothing\n");
		std::fflush(stdout);
		return;
	}
	const auto address = reinterpret_cast<uint64_t>(&g_request);
	std::memcpy(reinterpret_cast<void*>(slot), &address, sizeof(address));
	Common::VirtualMemory::Protect(page, 0x1000, old_mode);
	g_items     = LoadItems(std::getenv("KYTY_CHEAT_ITEMS"));
	g_installed = true;
	std::printf("Cheat give: F10 gives items: a panel to search %zu items (KYTY_CHEAT_ITEMS), give.txt %s (slot +%llX)\n",
	            g_items.size(),
	            std::getenv("KYTY_CHEAT_GIVE_FILE") ? std::getenv("KYTY_CHEAT_GIVE_FILE") : "(KYTY_CHEAT_GIVE_FILE unset)",
	            static_cast<unsigned long long>(offset));
	std::fflush(stdout);
}

} // namespace Loader::CheatGive

#endif // KYTY_LOADER_CHEAT_GIVE_H_
