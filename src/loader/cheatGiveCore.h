#ifndef KYTY_LOADER_CHEAT_GIVE_CORE_H_
#define KYTY_LOADER_CHEAT_GIVE_CORE_H_
// The give items cheat's state, shared by the loader (cheatGive.h: Install), the window (F10) and the system overlay's
// panel (graphics/presentation/systemOverlay.cpp: search the item list, pick a count, give).
// The cheat file's cave (the characters' per-frame update, 0x325ff0 in 01.007.000) takes one request at a time from
// the request block: +0 sequence (ours), +4 done (the cave's, claimed with lock cmpxchg), +8 category, +12 id,
// +16 count, +20 result (the game's AddItem's; -2: no player data).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace Loader::CheatGive {

struct alignas(64) Request {
	std::atomic<uint32_t> sequence {0};
	std::atomic<uint32_t> done {0};
	std::atomic<uint32_t> category {0};
	std::atomic<uint32_t> id {0};
	std::atomic<uint32_t> count {0};
	std::atomic<int32_t>  result {0};
};
static_assert(sizeof(std::atomic<uint32_t>) == 4 && offsetof(Request, result) == 20);

struct Item {
	uint32_t    category = 0, id = 0;
	std::string name, lower;
};
inline constexpr uint32_t Weapons = 0, Armor = 0x10000000, Rings = 0x20000000, Goods = 0x40000000;
inline const char*        CategoryName(uint32_t category) {
	   return category == Weapons ? "Weapon" : category == Armor ? "Armor" : category == Rings ? "Ring" : "Item";
}

inline Request                           g_request;
inline std::atomic<bool>                 g_installed {false};
inline std::vector<Item>                 g_items; // (set once by Install, before the game runs)
inline std::mutex                        g_mutex;
inline std::function<void(std::string)>  g_notify;   // (window.cpp: the on-screen notice)
inline std::deque<std::string>           g_log;      // the last results, newest last (the panel shows them)
inline std::atomic<uint64_t>             g_log_revision {0};
inline std::atomic<int>                  g_pending {0};

inline std::string Lower(std::string text) {
	for (auto& c: text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}
inline std::string Trim(std::string text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
	return text.substr(start);
}

inline std::vector<Item> LoadItems(const char* path) {
	std::vector<Item> items;
	if (path == nullptr) return items;
	std::ifstream file(path);
	std::string   line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream words(line);
		std::string        category, id, name;
		if (!(words >> category >> id)) continue;
		std::getline(words >> std::ws, name);
		name = Trim(name);
		if (name.empty()) continue;
		items.push_back({static_cast<uint32_t>(std::strtoul(category.c_str(), nullptr, 16)),
		                 static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 10)), name, Lower(name)});
	}
	return items;
}
// "<name>", "<name> x5", "<name> *5" -> name and count (1..99); `count` keeps its value without a suffix.
inline void SplitCount(std::string line, std::string* name, uint32_t* count) {
	line             = Trim(line);
	const auto space = line.find_last_of(" \t");
	if (space != std::string::npos) {
		auto last = line.substr(space + 1);
		if (!last.empty() && (last[0] == 'x' || last[0] == 'X' || last[0] == '*')) last.erase(0, 1);
		if (!last.empty() && last.find_first_not_of("0123456789") == std::string::npos && last.size() <= 4) {
			*count = static_cast<uint32_t>(std::strtoul(last.c_str(), nullptr, 10));
			line   = Trim(line.substr(0, space));
		}
	}
	*count = std::clamp<uint32_t>(*count, 1, 99);
	*name  = line;
}
// Whether an item's name has all the words of `query` (lower case), in any order: "bone smash+4".
inline bool Matches(const Item& item, const std::vector<std::string>& words) {
	return std::all_of(words.begin(), words.end(),
	                   [&](const std::string& word) { return item.lower.find(word) != std::string::npos; });
}
inline std::vector<std::string> Words(const std::string& query) {
	std::istringstream       in(Lower(query));
	std::vector<std::string> words;
	for (std::string word; in >> word;) words.push_back(word);
	return words;
}
// The item a name means (give.txt), or nullptr with the reason in `why`.
inline const Item* Find(const std::string& name, std::string* why) {
	const auto wanted = Lower(name);
	for (const auto& item: g_items) {
		if (item.lower == wanted) return &item;
	}
	const auto  words   = Words(name);
	const Item* found   = nullptr;
	size_t      matches = 0;
	for (const auto& item: g_items) {
		if (Matches(item, words) && matches++ == 0) found = &item;
	}
	if (matches == 1) return found;
	*why = matches == 0 ? "\"" + name + "\" is no item"
	                    : "\"" + name + "\" is " + std::to_string(matches) + " items (" + found->name + ", ...)";
	return nullptr;
}

inline void Report(std::string line) {
	std::printf("Cheat give: %s\n", line.c_str());
	std::fflush(stdout);
	{
		std::scoped_lock lock {g_mutex};
		g_log.push_back(line);
		while (g_log.size() > 6) g_log.pop_front();
	}
	g_log_revision++;
	if (g_notify) g_notify(std::move(line));
}

// Gives one item through the cave (blocking, up to 15 s); the line to show.
inline std::string GiveOne(const Item& item, uint32_t count) {
	const uint32_t previous = g_request.done.load();
	g_request.category      = item.category;
	g_request.id            = item.id;
	g_request.count         = count;
	g_request.result        = 0x7fffffff;
	const uint32_t sequence = previous + 1;
	g_request.sequence      = sequence;
	const auto label        = item.name + (count > 1 ? " x" + std::to_string(count) : std::string {});
	for (int waited = 0; waited < 15000; waited += 10) {
		if (g_request.done.load() == sequence) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // (its result follows the claim)
			const int32_t result = g_request.result.load();
			std::printf("Cheat give: %s (category %X id %u) -> %d\n", label.c_str(), item.category, item.id, result);
			if (result == -2) return label + ": no character loaded yet";
			return "Gave " + label;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	// Not taken: withdrawn, unless the game takes it right now.
	uint32_t expected = previous;
	if (g_request.done.compare_exchange_strong(expected, sequence)) {
		return label + ": the game did not take it (load into the world first)";
	}
	return "Gave " + label;
}

// The worker giving what the panel and F10 queue, one at a time.
struct Job {
	Item     item;
	uint32_t count = 1;
	bool     file  = false; // give.txt's list instead
};
inline std::deque<Job>         g_jobs;
inline std::condition_variable g_jobs_ready;
inline bool                    g_worker_started = false;

inline void GiveFile() {
	const char*   path = std::getenv("KYTY_CHEAT_GIVE_FILE");
	std::ifstream file(path ? path : "");
	if (!file) {
		Report("Give items: no give.txt (KYTY_CHEAT_GIVE_FILE)");
		return;
	}
	std::string line;
	int         given = 0, lines = 0;
	while (std::getline(file, line)) {
		if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
		std::string name;
		uint32_t    count = 1;
		SplitCount(line, &name, &count);
		if (name.empty()) continue;
		lines++;
		std::string why;
		const Item* item = Find(name, &why);
		if (item == nullptr) {
			Report(why);
			continue;
		}
		auto result = GiveOne(*item, count);
		if (result.rfind("Gave ", 0) == 0) given++;
		Report(std::move(result));
	}
	if (lines == 0) Report("Give items: give.txt lists no items");
	else if (lines > 1) Report("Give items: " + std::to_string(given) + " of " + std::to_string(lines) + " given");
}

inline void Queue(Job job) {
	std::unique_lock lock {g_mutex};
	g_jobs.push_back(std::move(job));
	g_pending++;
	if (!g_worker_started) {
		g_worker_started = true;
		std::thread([] {
			for (;;) {
				Job job;
				{
					std::unique_lock lock {g_mutex};
					g_jobs_ready.wait(lock, [] { return !g_jobs.empty(); });
					job = std::move(g_jobs.front());
					g_jobs.pop_front();
				}
				if (job.file) {
					GiveFile();
				} else {
					Report(GiveOne(job.item, job.count));
				}
				g_pending--;
			}
		}).detach();
	}
	lock.unlock();
	g_jobs_ready.notify_one();
}

// The panel (F10 opens and closes it; Escape or the pad's circle closes it). The overlay calls back when it changes.
inline std::atomic<bool>     g_panel_open {false};
inline std::atomic<uint64_t> g_panel_generation {0};
inline void (*g_panel_changed)() = nullptr; // (systemOverlay.cpp: RefreshVisibility)
inline void SetPanel(bool open) {
	if (g_panel_open.exchange(open) == open) return;
	if (open) g_panel_generation++;
	if (auto* changed = g_panel_changed) changed();
}

// F10: true when the give cheat is installed (the key then does nothing else): the panel open or closed.
inline bool TogglePanel(std::function<void(std::string)> notify) {
	if (!g_installed) return false;
	{
		std::scoped_lock lock {g_mutex};
		if (!g_notify) g_notify = std::move(notify);
	}
	if (g_items.empty()) {
		Report("Give items: no item list (KYTY_CHEAT_ITEMS)");
		return true;
	}
	SetPanel(!g_panel_open);
	return true;
}

} // namespace Loader::CheatGive

#endif // KYTY_LOADER_CHEAT_GIVE_CORE_H_
