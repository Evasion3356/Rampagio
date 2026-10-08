#include "Descriptions.h"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
	struct Entry
	{
		const char* menu;
		const char* caption;
		const char* text;
	};

	constexpr Entry kRampage[] = {
#include "data\Descriptions.inc"
	};

	// Ours: rows Rampage doesn't have, or that work differently from its
	// row of the same name. Same keys as Descriptions.inc.
	constexpr Entry kOurs[] = {
		{ "Home", "Recovery", "Money, items, honor, bounty, unlocks and collectibles." },
		{ "Settings", "Language", "The menu's language.\nGame Language follows the game's own setting." },
		{ "Blip", "Add Blip", "Add a blip for it on the map." },
		{ "Blip", "Teleport to", "Teleports you to it." },
		{ "Blip", "Teleport to Me", "Teleports it to you." },
	};

	// Our menu titles that differ from the Rampage menu holding the same
	// rows: { ours, Rampage's }. Tried after the title itself.
	constexpr std::pair<const char*, const char*> kMenuAliases[] = {
		{ "Weapon", "Weapons" },
		{ "Weapon Visuals", "Visuals" },
		{ "Weapon Modifiers", "Modifiers" },
		{ "Abilities", "Player Abilities" },
		{ "Overlay Settings", "Overlay" },
		{ "Core", "Core Settings" },
		{ "Theme", "Extended UI" },
		{ "Ped Manager", "Local Peds" },
		{ "Vehicle Manager", "Local Vehicles" },
		{ "Object Manager", "Local Objects" },
		{ "Ped Editor", "Edit Ped" },
		{ "General", "Ped General" },
	};

	// Lowercase, without ~...~ codes, so "~COLOR_RED~Kill" matches "Kill".
	std::string Key(std::string_view s)
	{
		std::string key;
		bool code = false;
		for (char c : s)
		{
			if (c == '~')
				code = !code;
			else if (!code)
				key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		const size_t first = key.find_first_not_of(' ');
		const size_t last = key.find_last_not_of(' ');
		return first == std::string::npos ? std::string() : key.substr(first, last - first + 1);
	}

	std::string Join(std::string_view menu, std::string_view caption)
	{
		return Key(menu) + '\x1F' + Key(caption);
	}

	struct Tables
	{
		std::unordered_map<std::string, std::string_view> entries; // Join(menu, caption)
		std::unordered_map<std::string, std::string> aliases;     // Key(ours) -> Key(Rampage's)

		Tables()
		{
			for (const Entry& e : kOurs)
				entries.try_emplace(Join(e.menu, e.caption), e.text);
			for (const Entry& e : kRampage)
				entries.try_emplace(Join(e.menu, e.caption), e.text);
			for (const auto& [ours, rampage] : kMenuAliases)
				aliases.emplace(Key(ours), Key(rampage));
		}
	};

	const Tables& Get()
	{
		static const Tables tables;
		return tables;
	}
}

namespace Descriptions
{
	std::string_view Find(std::string_view menu, std::string_view caption)
	{
		const Tables& t = Get();
		if (auto it = t.entries.find(Join(menu, caption)); it != t.entries.end())
			return it->second;
		if (auto alias = t.aliases.find(Key(menu)); alias != t.aliases.end())
			if (auto it = t.entries.find(alias->second + '\x1F' + Key(caption)); it != t.entries.end())
				return it->second;
		return {};
	}
}
