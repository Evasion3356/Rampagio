/*
	Player > Wardrobe: ports Rampage's Submenus::SubSelfWardrobe (the rows
	besides Walk Styles and Damage Packs, which are in PlayerSubmenus.cpp),
	SubSelfWardrobeComponent, SubSelfWardrobeWearableState,
	SubSelfOutfitSaver, SubSelfFacialHair ("Hair and Weight"),
	SubSelfPedMetaTags, SubSelfPedMetaExpressions and SubSelfCustomizations
	("Overlay Textures"). The Model Changer is in ModelChanger.cpp.

	The clothing list is user-supplied, like the other lists:
	Rampagio_ClothingDb.xml next to Rampagio.json, in the same format as
	Rampage's Lists\ClothingDb.xml (<Component> entries with IsMP, PedType,
	Category and Hash). Without it, the Custom rows still take any hash.
	Category names are the game's metaped category names; the expression
	names come from alloc8or's MetaPedExpression list
	(tools/extract_expressions.py). Saved outfits go in Rampagio_Outfits.json
	(ours; Rampage writes one XML per outfit).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"
#include "..\Log.h"
#include "..\PatternScan.h"
#include "..\..\external\ScriptHookSDK\inc\main.h"
#include "..\..\external\minhook\include\MinHook.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <format>
#include <map>
#include <set>

namespace
{
	// The Player menu's ped, or the Ped Editor's / horse's when reached
	// through them (Menus::Target).
	Ped Me() { return Menus::Target::Get(); }

	constexpr Hash kArthur = 0x0D7114C9; // player_zero
	constexpr Hash kJohn = 0x00B69710;   // player_three

	// Shop item natives take an isMp flag: false for the story characters.
	bool IsMpModel(Ped ped)
	{
		const Hash model = ENTITY::GET_ENTITY_MODEL(ped);
		return model != kArthur && model != kJohn;
	}

	void Refresh(Ped ped, bool isMp)
	{
		PED::_SET_ACTIVE_META_PED_COMPONENTS_UPDATED(ped, isMp);
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
	}

	void WaitReady(Ped ped)
	{
		for (int i = 0; i < 200 && !PED::IS_PED_READY_TO_RENDER(ped); i++)
			WAIT(0);
	}

	void ApplyShopItem(Hash item)
	{
		const Ped ped = Me();
		const bool mp = IsMpModel(ped);
		PED::_APPLY_SHOP_ITEM_TO_PED(ped, item, FALSE, mp, FALSE);
		Refresh(ped, mp);
	}

	std::string Hex(Hash h) { return std::format("0x{:08X}", h); }

	// --- metaped categories ---------------------------------------------

	const char* const kCategories[] = {
		"ACCESSORIES", "AMMO_PISTOLS", "AMMO_RIFLES", "APRONS", "ARMOR", "BADGES", "BEARDS_CHIN",
		"BEARDS_CHOPS", "BEARDS_COMPLETE", "BEARDS_MUSTACHE", "BELTS", "BELT_BUCKLES", "BODIES_LOWER",
		"BODIES_UPPER", "BOOTS", "BOOT_ACCESSORIES", "CHAPS", "CLOAKS", "COATS", "COATS_CLOSED", "DRESSES",
		"EYES", "EYEWEAR", "GAUNTLETS", "GLOVES", "GUNBELTS", "GUNBELT_ACCS", "HAIR", "HAIR_ACCESSORIES",
		"HATS", "HEADS", "HOLSTERS_CROSSDRAW", "HOLSTERS_KNIFE", "HOLSTERS_LEFT", "HOLSTERS_RIGHT",
		"JEWELRY_BRACELETS", "JEWELRY_RINGS_LEFT", "JEWELRY_RINGS_RIGHT", "LOADOUTS", "MASKS",
		"MASKS_LARGE", "NECKTIES", "NECKWEAR", "PANTS", "PONCHOS", "SATCHELS", "SHIRTS_FULL", "SKIRTS",
		"SPATS", "SUSPENDERS", "TALISMAN_BELT", "TALISMAN_HOLSTER", "TALISMAN_SATCHEL", "TALISMAN_WRIST",
		"TEETH", "VESTS",
	};

	std::string CategoryName(Hash category)
	{
		for (const char* name : kCategories)
			if (GameUtil::Joaat(name) == category)
				return name;
		return Hex(category);
	}

	// The index of the ped's component in `category`, or -1.
	int ComponentIndexInCategory(Ped ped, Hash category)
	{
		const int count = PED::_GET_NUM_COMPONENTS_IN_PED(ped);
		for (int i = 0; i < count; i++)
			if (PED::_GET_CATEGORY_OF_COMPONENT_AT_INDEX(ped, i, 0) == category)
				return i;
		return -1;
	}

	// --- clothing database ----------------------------------------------

	struct Clothing
	{
		Hash hash;
		bool mp;
		int pedType; // 0 male, 1 female (MP only)
	};
	struct ClothingCategory
	{
		Hash category;
		std::string name;
		std::vector<Clothing> items;
	};
	std::vector<ClothingCategory> g_clothing;
	bool g_clothingLoaded = false;

	// A view into `line`.
	std::string_view TagValue(std::string_view line, const char* tag)
	{
		const std::string open = std::string("<") + tag + ">";
		const size_t a = line.find(open);
		if (a == std::string_view::npos)
			return {};
		const size_t start = a + open.size();
		const size_t end = line.find('<', start);
		return line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
	}

	void LoadClothing()
	{
		g_clothing.clear();
		std::map<Hash, size_t> byCategory;
		Clothing item{};
		Hash category = 0;
		for (const std::string& line : DataFile::LoadLines(L"Rampagio_ClothingDb.xml"))
		{
			if (line.starts_with("<Component>"))
			{
				item = {};
				category = 0;
			}
			else if (auto v = TagValue(line, "IsMP"); !v.empty())
				item.mp = v == "true" || v == "1";
			else if (auto v = TagValue(line, "PedType"); !v.empty())
				std::from_chars(v.data(), v.data() + v.size(), item.pedType);
			else if (auto v = TagValue(line, "Category"); !v.empty())
				category = GameUtil::ParseHash(v);
			else if (auto v = TagValue(line, "Hash"); !v.empty())
				item.hash = GameUtil::ParseHash(v);
			else if (line.starts_with("</Component>") && item.hash)
			{
				auto [it, added] = byCategory.try_emplace(category, g_clothing.size());
				if (added)
					g_clothing.push_back({ category, CategoryName(category), {} });
				g_clothing[it->second].items.push_back(item);
			}
		}
		std::sort(g_clothing.begin(), g_clothing.end(), [](auto& a, auto& b) { return a.name < b.name; });
		g_clothingLoaded = true;
	}

	// Filter: SP, MP (male), Both, Female, as Rampage's.
	int g_clothingFilter = 0;

	bool PassesFilter(const Clothing& c, int filter)
	{
		switch (filter)
		{
		case 0: return !c.mp;
		case 1: return c.mp && c.pedType == 0;
		case 3: return c.mp && c.pedType == 1;
		default: return true;
		}
	}

	// The filter matching the player's model: story characters SP, mp_female
	// Female, everything else MP.
	int FilterForPlayer()
	{
		const Hash model = ENTITY::GET_ENTITY_MODEL(Me());
		if (model == kArthur || model == kJohn)
			return 0;
		return model == GameUtil::Joaat("mp_female") ? 3 : 1;
	}

	// --- wearable states ------------------------------------------------

	Hash g_stateItem = 0;
	MenuBase* g_stateMenu = nullptr;

	void SetWearableState(Hash state)
	{
		const Ped ped = Me();
		const bool mp = IsMpModel(ped);
		PED::_UPDATE_SHOP_ITEM_WEARABLE_STATE(ped, g_stateItem, state, 0, mp, TRUE);
		Refresh(ped, mp);
	}

	void BuildStateMenu(MenuBase* m)
	{
		const Ped ped = Me();
		const bool female = ENTITY::GET_ENTITY_MODEL(ped) == GameUtil::Joaat("mp_female");
		const bool mp = IsMpModel(ped);
		const int count = PED::_GET_SHOP_ITEM_NUM_WEARABLE_STATES(g_stateItem, female, mp);
		for (int i = 0; i < count; i++)
		{
			const Hash state = PED::_GET_SHOP_ITEM_WEARABLE_STATE_BY_INDEX(g_stateItem, i, female, mp);
			Ui::Do(m, std::format("State {} ({})", i, Hex(state)), [state] { SetWearableState(state); });
		}
	}

	// Ours: picking an item applies it, then opens its wearable states when
	// it has any (Rampage opens the states instead of applying).
	void PickClothing(Hash item)
	{
		ApplyShopItem(item);
		const Ped ped = Me();
		const bool female = ENTITY::GET_ENTITY_MODEL(ped) == GameUtil::Joaat("mp_female");
		if (PED::_GET_SHOP_ITEM_NUM_WEARABLE_STATES(item, female, IsMpModel(ped)) > 0)
		{
			g_stateItem = item;
			Ui::Push(g_stateMenu);
		}
	}

	void DisableCategory(Hash category)
	{
		const Ped ped = Me();
		const bool mp = IsMpModel(ped);
		// Rampage also drops the matching cloth tag when removing a coat.
		if (category == 0x3C1A74CD) // COATS_CLOSED
			PED::REMOVE_TAG_FROM_META_PED(ped, 0x1D71FC86, 1);
		PED::REMOVE_TAG_FROM_META_PED(ped, category, 0);
		Refresh(ped, mp);
	}

	void BuildCategoryMenu(MenuBase* m, size_t index)
	{
		const ClothingCategory& cat = g_clothing[index];
		Ui::Choice(m, "Filter", { "SP", "MP", "Both", "Female" }, &g_clothingFilter, [](int) {
			Ui::Controller().ReopenActiveLater(); // rebuild with the new filter
		});
		const Hash category = cat.category;
		Ui::Do(m, "Disable", [category] { DisableCategory(category); });
		int n = 0;
		for (const Clothing& c : cat.items)
		{
			if (!PassesFilter(c, g_clothingFilter))
				continue;
			const Hash item = c.hash;
			Ui::Do(m, std::format("{} {}", ++n, Hex(item)), [item] { PickClothing(item); });
		}
	}

	MenuBase* g_categoryMenu = nullptr;
	size_t g_categoryIndex = 0;

	void BuildComponents(MenuBase* m)
	{
		if (!g_clothingLoaded)
			LoadClothing();
		Ui::Do(m, "Reload Clothes Database", [] {
			LoadClothing();
			Ui::Controller().ReopenActiveLater();
		});
		if (g_clothing.empty())
		{
			m->AddItem(new MenuItemLabel([] { return std::string("No Rampagio_ClothingDb.xml"); }));
			return;
		}
		for (size_t i = 0; i < g_clothing.size(); i++)
			m->AddItem(new MenuItemAction(std::format("{} ({})", g_clothing[i].name, g_clothing[i].items.size()), [i] {
				g_categoryIndex = i;
				g_clothingFilter = FilterForPlayer();
				Ui::Push(g_categoryMenu);
			}));
	}

	// --- wardrobe items (ours) -------------------------------------------
	// Every clothing item of the game's SP catalog by group, from
	// tools/extract_wardrobe_items.py. No XML needed.

	struct WardrobeItem { Hash key; const char* group; const char* sub; const char* name; };
	const WardrobeItem kWardrobeItems[] = {
#include "..\data\WardrobeItems.inc"
	};

	MenuBase* g_wardrobeGroupMenu = nullptr;
	std::string g_wardrobeGroup;

	// The game's own name for the item (the label is the item hash, as in
	// the tailor shop), else the catalog name with CLOTHING_ cut off, else the hash.
	std::string WardrobeItemLabel(const WardrobeItem& item)
	{
		std::string fallback = item.name ? item.name : Hex(item.key);
		if (fallback.size() > 9 && (fallback.starts_with("CLOTHING_") || fallback.starts_with("clothing_")))
			fallback.erase(0, 9);
		if (item.sub)
			fallback = std::format("{} {}", item.sub, fallback);
		return GameUtil::ItemName(item.key, fallback);
	}

	std::string g_wardrobeSearch;

	void BuildWardrobeGroups(MenuBase* m)
	{
		std::vector<std::pair<std::string, int>> groups; // kept in the table's order
		for (const WardrobeItem& item : kWardrobeItems)
		{
			auto it = std::find_if(groups.begin(), groups.end(), [&](auto& g) { return g.first == item.group; });
			if (it == groups.end())
				groups.emplace_back(item.group, 1);
			else
				it->second++;
		}
		// The common groups first, the odd ones after.
		std::stable_sort(groups.begin(), groups.end(), [](auto& a, auto& b) { return a.second > b.second; });
		for (auto& [group, count] : groups)
			m->AddItem(new MenuItemAction(std::format("{} ({})", group, count), [group = group] {
				g_wardrobeGroup = group;
				g_wardrobeSearch.clear();
				Ui::Push(g_wardrobeGroupMenu);
			}));
	}

	void BuildWardrobeGroup(MenuBase* m)
	{
		Ui::Action(m, g_wardrobeSearch.empty() ? "Search" : "Search: " + g_wardrobeSearch, [] {
			std::string text;
			if (!GameUtil::PromptText("Search:", text))
				return std::string("Cancelled");
			g_wardrobeSearch = text;
			Ui::Controller().ReopenActiveLater();
			return std::string();
		});
		std::string needle = g_wardrobeSearch;
		std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });

		std::vector<std::pair<std::string, Hash>> rows;
		int total = 0, invalid = 0;
		for (const WardrobeItem& item : kWardrobeItems)
		{
			if (g_wardrobeGroup != item.group)
				continue;
			total++;
			if (!ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(item.key, 0))
			{
				if (invalid++ < 5)
					Log::Write("Wardrobe: {} key {} ({}) is not valid", item.group, Hex(item.key), item.name ? item.name : "unnamed");
				continue;
			}
			std::string label = WardrobeItemLabel(item);
			if (!needle.empty())
			{
				std::string lower = label;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				if (lower.find(needle) == std::string::npos)
					continue;
			}
			rows.emplace_back(std::move(label), item.key);
		}
		std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
		Log::Write("Wardrobe: {}: {} items, {} invalid, {} listed (search \"{}\")", g_wardrobeGroup, total, invalid, rows.size(), g_wardrobeSearch);

		// Variants share a name (tints, honor versions): number them.
		std::map<std::string, int> seen;
		for (auto& [label, key] : rows)
			seen[label]++;
		std::map<std::string, int> index;
		for (auto& [label, key] : rows)
		{
			std::string text = seen[label] > 1 ? std::format("{} ({})", label, ++index[label]) : label;
			const Hash item = key;
			Ui::Do(m, text, [item] { PickClothing(item); });
		}
	}

	// Debug: what the game answers for every wardrobe item, to decide which
	// ones to leave out (the ones without a readable name).
	std::string DumpWardrobeNames()
	{
		nlohmann::json out = nlohmann::json::array();
		for (const WardrobeItem& item : kWardrobeItems)
		{
			const char* text = HUD::GET_STRING_FROM_HASH_KEY(item.key);
			out.push_back({
				{ "key", Hex(item.key) },
				{ "group", item.group },
				{ "name", item.name ? item.name : "" },
				{ "valid", ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(item.key, 0) != FALSE },
				{ "label", text ? text : "" },
			});
		}
		return DataFile::SaveJson(L"Rampagio_WardrobeNames.json", out) ? "Saved Rampagio_WardrobeNames.json" : "Couldn't save";
	}

	// --- main rows -------------------------------------------------------

	int g_outfitVariation = 0;
	bool g_keepFacialHair = false;

	// Rampage keeps facial hair while Shift is held; ours is a toggle row.
	void ApplyOutfitVariation()
	{
		PED::_EQUIP_META_PED_OUTFIT_PRESET(Me(), g_outfitVariation, g_keepFacialHair);
	}

	std::string RandomComponents()
	{
		if (!g_clothingLoaded)
			LoadClothing();
		if (g_clothing.empty())
			return "No Rampagio_ClothingDb.xml";
		const Ped ped = Me();
		const bool mp = IsMpModel(ped);
		const int filter = FilterForPlayer();
		for (const ClothingCategory& cat : g_clothing)
		{
			std::vector<Hash> pool;
			for (const Clothing& c : cat.items)
				if (PassesFilter(c, filter))
					pool.push_back(c.hash);
			if (!pool.empty())
				PED::_APPLY_SHOP_ITEM_TO_PED(ped, pool[MISC::GET_RANDOM_INT_IN_RANGE(0, static_cast<int>(pool.size()))], FALSE, mp, FALSE);
		}
		Refresh(ped, mp);
		return {};
	}

	// The naked body presets Rampage uses: 14 for Arthur, 28 for John.
	void LoadCleanBody()
	{
		const Ped ped = Me();
		const Hash model = ENTITY::GET_ENTITY_MODEL(ped);
		if (model == kArthur || model == kJohn)
		{
			PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, model == kArthur ? 14 : 28, TRUE);
			WaitReady(ped);
		}
	}

	void RemoveAllComponents()
	{
		const Ped ped = Me();
		LoadCleanBody();
		PED::_RESET_PED_COMPONENTS(ped);
		Refresh(ped, IsMpModel(ped));
	}

	// The story outfit the game keeps for the player: Global_1946054.f_1378,
	// an array of { component, ... } triples (size at f_1379).
	void LoadDefaultComponents()
	{
		UINT64* g = GameUtil::Global(1946054);
		if (!g)
			return;
		const Ped ped = Me();
		PED::_RESET_PED_COMPONENTS(ped);
		const int count = std::min<int>(static_cast<int>(g[1379]), 40);
		for (int i = 0; i < count; i++)
		{
			const Hash item = static_cast<Hash>(g[1380 + 3 * i]);
			if (!item)
				continue;
			PED::_APPLY_SHOP_ITEM_TO_PED(ped, item, FALSE, FALSE, FALSE);
			PED::_APPLY_SHOP_ITEM_TO_PED(ped, item, FALSE, TRUE, FALSE);
		}
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
	}

	// Metaped tags Rampage strips every frame while the toggles are on.
	constexpr Hash kOffHandHolster = 0xB6B6122D;
	constexpr Hash kSatchel = 0x94504D26;
	constexpr Hash kHat = 0x9925C067;

	void RemoveTagTick(Hash tag, bool noDualWield)
	{
		const Ped ped = Me();
		if (!PED::_IS_META_PED_USING_COMPONENT(ped, tag) || !PLAYER::IS_PLAYER_READY_FOR_CUTSCENE(PLAYER::PLAYER_ID()))
			return;
		if (noDualWield)
			WEAPON::_SET_ALLOW_DUAL_WIELD(ped, FALSE);
		PED::REMOVE_TAG_FROM_META_PED(ped, tag, 0);
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
	}

	// Never Lose Hat: the engine's remove-prop-from-ped function (the one
	// KNOCK_OFF_PED_PROP and a shot hat end in) is hooked, and a request to
	// remove the hat from the player that comes with an instigator ped (someone
	// shot it off) is dropped. Our own Drop Hat has no instigator, so it
	// still works. The old hook from the user's MP script hook; the container
	// is its ReClass of the first argument.
	struct RemoveObjectContainer
	{
		char pad_0000[8];
		void* pedToRemoveObjectFrom; // 0x08
		char pad_0010[8];
		void* instigatorPed; // 0x18
		char pad_0020[0x40];
	};
	static_assert(sizeof(RemoveObjectContainer) == 0x60);

	constexpr Hash kHatProp = 0x74A70540;

	using RemovePropFromPedFn = void* (*)(RemoveObjectContainer*, Hash&, bool);
	RemovePropFromPedFn g_removePropFromPed = nullptr;
	void* g_removePropTarget = nullptr;
	bool g_removePropTried = false;
	std::atomic<void*> g_playerPedPtr{ nullptr };

	void* RemovePropFromPedDetour(RemoveObjectContainer* container, Hash& prop, bool knockOff)
	{
		if (prop == kHatProp && container->instigatorPed
			&& container->pedToRemoveObjectFrom == g_playerPedPtr.load(std::memory_order_relaxed))
			return nullptr;
		return g_removePropFromPed(container, prop, knockOff);
	}

	void NeverLoseHatOn(bool on)
	{
		if (!on)
		{
			if (g_removePropFromPed)
				MH_DisableHook(g_removePropTarget);
			g_playerPedPtr = nullptr;
			return;
		}
		if (!g_removePropTried)
		{
			g_removePropTried = true;
			const auto match = PatternScan::FindInMainModule("E8 ? ? ? ? 48 8B 4C 24 ? 0F B7 F6");
			if (!match)
				Log::Write("Wardrobe: RemovePropFromPed pattern not found, Never Lose Hat does nothing");
			else
			{
				const MH_STATUS init = MH_Initialize();
				g_removePropTarget = reinterpret_cast<void*>(PatternScan::ResolveRip(*match, 1));
				if ((init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED)
					&& MH_CreateHook(g_removePropTarget, reinterpret_cast<void*>(&RemovePropFromPedDetour),
						reinterpret_cast<void**>(&g_removePropFromPed)) == MH_OK)
					Log::Write("Wardrobe: RemovePropFromPed hook created at {:#x}", reinterpret_cast<unsigned long long>(g_removePropTarget));
				else
				{
					Log::Write("Wardrobe: RemovePropFromPed hook failed");
					g_removePropFromPed = nullptr;
				}
			}
		}
		if (g_removePropFromPed)
			MH_EnableHook(g_removePropTarget);
	}

	// The player's ped pointer for the hook; the handle changes on a respawn.
	void NeverLoseHatTick()
	{
		g_playerPedPtr.store(getScriptHandleBaseAddress(PLAYER::PLAYER_PED_ID()), std::memory_order_relaxed);
	}

	// --- wardrobe cam ----------------------------------------------------

	Cam g_wardrobeCam = 0;

	void WardrobeCamTick()
	{
		for (Hash control : { 0x4D8FB4C1u, 0xFDA83190u, 0xD0842EDFu, 0xF78D7337u, 0xFD0F0C2Cu, 0xCC1075A7u })
			PAD::DISABLE_CONTROL_ACTION(0, control, TRUE);
		const Ped ped = Me();
		ENTITY::FREEZE_ENTITY_POSITION(ped, TRUE);
		if (!CAMERA::DOES_CAM_EXIST(g_wardrobeCam))
		{
			const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ped, 0.0f, 3.0f, 0.0f);
			g_wardrobeCam = CAMERA::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", FALSE);
			CAMERA::SET_CAM_COORD(g_wardrobeCam, p.x, p.y, p.z);
			CAMERA::SET_CAM_ACTIVE(g_wardrobeCam, TRUE);
			CAMERA::RENDER_SCRIPT_CAMS(TRUE, TRUE, 1000, TRUE, FALSE, 0);
			CAMERA::POINT_CAM_AT_ENTITY(g_wardrobeCam, ped, 0.0f, 0.0f, 0.0f, TRUE);
			CAMERA::SET_CAM_FOV(g_wardrobeCam, 40.0f);
		}
		// Zoom with the weapon wheel next/previous controls, as Rampage.
		float fov = CAMERA::GET_CAM_FOV(g_wardrobeCam);
		if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(2, 0x9DA42644) && fov < 200.0f)
			fov += 10.0f;
		if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(2, 0x81457A1A) && fov > 10.0f)
			fov -= 10.0f;
		CAMERA::SET_CAM_FOV(g_wardrobeCam, fov);
	}

	void WardrobeCamOff()
	{
		ENTITY::FREEZE_ENTITY_POSITION(Me(), FALSE);
		if (CAMERA::DOES_CAM_EXIST(g_wardrobeCam))
		{
			CAMERA::RENDER_SCRIPT_CAMS(FALSE, TRUE, 1000, TRUE, FALSE, 0);
			CAMERA::SET_CAM_ACTIVE(g_wardrobeCam, FALSE);
			CAMERA::DESTROY_CAM(g_wardrobeCam, TRUE);
		}
		g_wardrobeCam = 0;
	}

	// --- custom ----------------------------------------------------------

	std::string PromptHash(const char* title, Hash& out)
	{
		std::string text;
		if (!GameUtil::PromptText(title, text) || text.empty())
			return "Cancelled";
		out = GameUtil::ParseHash(text);
		return {};
	}

	std::string EnableComponent()
	{
		Hash h = 0;
		if (auto err = PromptHash("Enable a Ped Component by Hash", h); !err.empty())
			return err;
		if (!ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(h, 0))
			return "Unknown component";
		ApplyShopItem(h);
		return {};
	}

	std::string DisableComponent()
	{
		Hash h = 0;
		if (auto err = PromptHash("Disable a Ped Component by Hash", h); !err.empty())
			return err;
		PED::REMOVE_TAG_FROM_META_PED(Me(), h, 1);
		PED::_UPDATE_PED_VARIATION(Me(), FALSE, TRUE, TRUE, TRUE, FALSE);
		return {};
	}

	std::string SetBodyComponent()
	{
		Hash h = 0;
		if (auto err = PromptHash("Enable a Ped Body Component by Hash", h); !err.empty())
			return err;
		PED::_EQUIP_META_PED_OUTFIT(Me(), h);
		PED::_UPDATE_PED_VARIATION(Me(), FALSE, TRUE, TRUE, TRUE, FALSE);
		return {};
	}

	// --- outfits ---------------------------------------------------------

	const wchar_t* kOutfitsFile = L"Rampagio_Outfits.json";
	bool g_removeAllOnLoad = true;

	struct MetaTag
	{
		Hash drawable = 0, albedo = 0, normal = 0, material = 0, palette = 0;
		int tint0 = 0, tint1 = 0, tint2 = 0;
	};

	MetaTag ReadMetaTag(Ped ped, int index)
	{
		MetaTag t;
		PED::GET_META_PED_ASSET_GUIDS(ped, index, &t.drawable, &t.albedo, &t.normal, &t.material);
		PED::GET_META_PED_ASSET_TINT(ped, index, &t.palette, &t.tint0, &t.tint1, &t.tint2);
		return t;
	}

	void ApplyMetaTag(Ped ped, const MetaTag& t)
	{
		PED::_SET_META_PED_TAG(ped, t.drawable, t.albedo, t.normal, t.material, t.palette, t.tint0, t.tint1, t.tint2);
	}

	std::string SaveOutfit(bool completeModel)
	{
		std::string name;
		if (!GameUtil::PromptText("Outfit name", name, 40) || name.empty())
			return {};
		const Ped ped = Me();
		nlohmann::json outfit = { { "model", ENTITY::GET_ENTITY_MODEL(ped) }, { "removeAll", g_removeAllOnLoad } };
		nlohmann::json components = nlohmann::json::array();
		const int count = PED::_GET_NUM_COMPONENTS_IN_PED(ped);
		for (int i = 0; i < count; i++)
		{
			BOOL status = FALSE;
			Hash state = 0;
			const Hash item = PED::_GET_SHOP_ITEM_COMPONENT_AT_INDEX(ped, i, TRUE, &status, &state);
			if (item)
				components.push_back(item);
		}
		outfit["components"] = std::move(components);
		if (completeModel)
		{
			nlohmann::json tags = nlohmann::json::array();
			for (int i = 0; i < count; i++)
			{
				const MetaTag t = ReadMetaTag(ped, i);
				tags.push_back({ t.drawable, t.albedo, t.normal, t.material, t.palette, t.tint0, t.tint1, t.tint2 });
			}
			outfit["tags"] = std::move(tags);
		}
		nlohmann::json file = DataFile::LoadJson(kOutfitsFile);
		file[name] = std::move(outfit);
		return DataFile::SaveJson(kOutfitsFile, file) ? "Saved " + name : "Couldn't save";
	}

	std::string LoadOutfit(const std::string& name)
	{
		const nlohmann::json file = DataFile::LoadJson(kOutfitsFile);
		auto it = file.find(name);
		if (it == file.end() || !it->is_object())
			return TrFormat("No outfit named {}", name);
		const nlohmann::json& outfit = *it;
		try
		{
			const Ped ped = Me();
			const bool mp = IsMpModel(ped);
			if (outfit.value("removeAll", false))
				RemoveAllComponents();
			for (const nlohmann::json& item : outfit.value("components", nlohmann::json::array()))
				PED::_APPLY_SHOP_ITEM_TO_PED(ped, item.get<Hash>(), FALSE, mp, FALSE);
			Refresh(ped, mp);
			WaitReady(ped);
			for (const nlohmann::json& t : outfit.value("tags", nlohmann::json::array()))
			{
				if (!t.is_array() || t.size() != 8)
					continue;
				ApplyMetaTag(ped, { t[0].get<Hash>(), t[1].get<Hash>(), t[2].get<Hash>(), t[3].get<Hash>(), t[4].get<Hash>(),
					t[5].get<int>(), t[6].get<int>(), t[7].get<int>() });
			}
			PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
		}
		catch (const nlohmann::json::exception&)
		{
			return TrFormat("Outfit {} is damaged", name);
		}
		return {};
	}

	std::string LoadOutfitPrompt()
	{
		std::string name;
		if (!GameUtil::PromptText("Outfit name", name, 40) || name.empty())
			return {};
		return LoadOutfit(name);
	}

	void BuildOutfits(MenuBase* m)
	{
		Ui::Toggle(m, "Remove All On Load", [](bool on) { g_removeAllOnLoad = on; })->SetState(g_removeAllOnLoad);
		Ui::Action(m, "Save", [] { return SaveOutfit(false); });
		Ui::Action(m, "Save Complete Model", [] { return SaveOutfit(true); });
		const nlohmann::json saved = DataFile::LoadJson(kOutfitsFile);
		if (!saved.empty())
			Ui::Section(m, "Saved Outfits");
		for (const auto& [name, outfit] : saved.items())
		{
			const std::string n = name;
			Ui::Action(m, n, [n] { return LoadOutfit(n); });
		}
		if (!saved.empty())
		{
			Ui::Section(m, "Delete");
			for (const auto& [name, outfit] : saved.items())
			{
				const std::string n = name;
				Ui::Action(m, TrFormat("Delete {}", n), [n] {
					nlohmann::json file = DataFile::LoadJson(kOutfitsFile);
					file.erase(n);
					DataFile::SaveJson(kOutfitsFile, file);
					Ui::Controller().ReopenActiveLater();
					return TrFormat("Deleted {}", n);
				});
			}
		}
	}

	// --- hair and weight ---------------------------------------------------

	// Global_40.f_7731[3 /*5*/]: chin, chops and mustache length (0..10)
	// with a growth timestamp in .f_3; Global_40.f_7748: hair, length in
	// .f_1 (0..9) and timestamp in .f_5. Global_40.f_11095.f_11[13]: body
	// weight (-100..100). The game's growth (flow_controller func_1044 and
	// func_1046) counts hours from the timestamp to the current game time
	// (Global_1899515), capped at 168, and adds a length per elapsed interval,
	// so a zero timestamp means a week's growth at once. Stamping the current
	// time restarts the clock instead.
	int g_beard[3] = {};
	int g_hair = 0;

	UINT64* Global40() { return GameUtil::Global(40); }

	// The game time the growth timestamps are measured against.
	UINT64 GrowthNow()
	{
		const UINT64* now = GameUtil::Global(1899515);
		return now ? *now : 0;
	}

	void ReadHair()
	{
		if (UINT64* g = Global40())
		{
			for (int i = 0; i < 3; i++)
				g_beard[i] = static_cast<int>(g[7732 + 5 * i]);
			g_hair = static_cast<int>(g[7749]);
		}
	}

	void SetBeard(int i)
	{
		if (UINT64* g = Global40())
		{
			g[7732 + 5 * i] = g_beard[i];
			g[7735 + 5 * i] = GrowthNow();
		}
	}

	void SetHair()
	{
		if (UINT64* g = Global40())
		{
			g[7749] = g_hair;
			g[7753] = GrowthNow();
		}
	}

	float* WeightSlot()
	{
		UINT64* g = Global40();
		return g ? reinterpret_cast<float*>(&g[11120]) : nullptr;
	}

	float g_weight = 0.0f;
	bool g_lockWeight = false;
	bool g_lockHair = false;

	// The lengths Lock Hair holds, saved so they come back after a restart or
	// a save load (the sliders are re-read from the game, not saved). Index 0..2
	// are the beards, 3 the hair; -1 means nothing captured yet.
	Rampagio::IntCommand* g_lockTarget[4] = {};
	Rampagio::LoopedCommand* g_lockHairCommand = nullptr;

	void RegisterLockTargets()
	{
		const char* const kIds[] = { "wardrobe.lockhair.chin", "wardrobe.lockhair.chops", "wardrobe.lockhair.stache", "wardrobe.lockhair.hair" };
		for (int i = 0; i < 4; i++)
			g_lockTarget[i] = new Rampagio::IntCommand(kIds[i], "Lock Hair Target", "", -1, i == 3 ? 9 : 10, 1, -1);
	}

	bool HasLockTargets() { return g_lockTarget[0] && g_lockTarget[0]->GetState() >= 0; }

	void ClearLockTargets()
	{
		for (auto* target : g_lockTarget)
			if (target)
				target->SetState(-1);
	}

	// The sliders are the targets while locked; keep the saved copy in step.
	void SaveLockTargets()
	{
		for (int i = 0; i < 4; i++)
			if (g_lockTarget[i])
				g_lockTarget[i]->SetState(i == 3 ? g_hair : g_beard[i]);
	}

	// What the player does with the sliders or the open menu while the lock is
	// off is the new state to lock, so saved lengths from an earlier lock go.
	void ForgetStaleLockTargets()
	{
		if (!g_lockHair)
			ClearLockTargets();
	}

	// Lock Hair turned on: resume the saved lengths, else lock what the game has.
	void StartLockHair()
	{
		g_lockHair = true;
		if (HasLockTargets())
		{
			for (int i = 0; i < 3; i++)
				g_beard[i] = g_lockTarget[i]->GetState();
			g_hair = g_lockTarget[3]->GetState();
		}
		else
			ReadHair();
		SaveLockTargets();
	}

	void ReadWeight()
	{
		if (const float* w = WeightSlot())
			g_weight = *w;
	}

	// Makes the game re-apply the player's body after a weight write.
	void RefreshBody()
	{
		if (UINT64* refresh = GameUtil::Global(1347477))
		{
			refresh[201] = 1;
			refresh[198] = 0;
			refresh[200] = 0;
			// short_update only swaps the head (f_185 is the applied head hash) and
			// the face morph (f_183 is the applied stage) when they differ from what
			// the weight calls for, so forget both: a head the game thinks is right
			// but isn't (a weight change that skipped it) gets redone.
			refresh[185] = 0;
			refresh[183] = 99;
		}
	}

	void SetWeight()
	{
		if (float* slot = WeightSlot())
		{
			*slot = std::clamp(g_weight, -100.0f, 100.0f);
			RefreshBody();
		}
	}

	// Lock Weight / Lock Hair hold the values shown above: the game moves
	// weight over time and grows hair back. The sliders are the targets.
	void LockWeightTick()
	{
		const float* w = WeightSlot();
		if (w && *w != g_weight)
			SetWeight();
	}

	void LockHairTick()
	{
		UINT64* g = Global40();
		if (!g)
			return;
		SaveLockTargets();
		// Restart the growth clocks too, not only the lengths: the game
		// grows hair from the timestamp's age.
		for (int i = 0; i < 3; i++)
			SetBeard(i);
		SetHair();
	}

	// Same as the Weight slider, with the value typed in: the slider shows it
	// afterwards, and SetWeight raises Global_1347477.f_201 and clears f_198
	// and f_200 as Rampage does, which makes the game re-apply the body.
	std::string ChangeWeight()
	{
		std::string text;
		if (!GameUtil::PromptText("Enter Weight -100 to 100", text) || text.empty())
			return {};
		if (!WeightSlot() || !GameUtil::Global(1347477))
			return "Script globals not found";
		g_weight = std::clamp(static_cast<float>(std::atof(text.c_str())), -100.0f, 100.0f);
		SetWeight();
		return {};
	}

	// --- meta ped tags -----------------------------------------------------

	int g_tagCategory = 0;
	std::string g_tagFields[5]; // drawable, albedo, normal, material, palette
	int g_tagTints[3] = {};
	int g_tagIndex = 0;

	void ShowTag(const MetaTag& t)
	{
		const Hash values[5] = { t.drawable, t.albedo, t.normal, t.material, t.palette };
		for (int i = 0; i < 5; i++)
			g_tagFields[i] = Hex(values[i]);
		g_tagTints[0] = t.tint0;
		g_tagTints[1] = t.tint1;
		g_tagTints[2] = t.tint2;
	}

	void ReadTag()
	{
		const Ped ped = Me();
		const int index = ComponentIndexInCategory(ped, GameUtil::Joaat(kCategories[g_tagCategory]));
		ShowTag(index >= 0 ? ReadMetaTag(ped, index) : MetaTag{});
	}

	void ReadTagAt(int index)
	{
		const Ped ped = Me();
		ShowTag(index < PED::_GET_NUM_COMPONENTS_IN_PED(ped) ? ReadMetaTag(ped, index) : MetaTag{});
	}

	void ApplyTag()
	{
		const Ped ped = Me();
		ApplyMetaTag(ped, { GameUtil::ParseHash(g_tagFields[0]), GameUtil::ParseHash(g_tagFields[1]),
			GameUtil::ParseHash(g_tagFields[2]), GameUtil::ParseHash(g_tagFields[3]), GameUtil::ParseHash(g_tagFields[4]),
			g_tagTints[0], g_tagTints[1], g_tagTints[2] });
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
	}

	// --- meta ped expressions ------------------------------------------------

	struct Expression
	{
		int id;
		const char* name;
		bool horse;
	};
	const Expression kExpressions[] = {
#include "..\data\MetaPedExpressions.inc"
	};
	float g_expressionValues[std::size(kExpressions)] = {};

	// --- overlay textures ----------------------------------------------------

	// One layer per overlay type, composed over the HEADS albedo the way
	// Rampage's Apply does. TX Id and Palette Id pick from Rampage's tables
	// (data/OverlayTextures.inc) and fill in the hashes, which can also be
	// typed in.
	struct OverlayTexture { const char* overlay; Hash albedo, normal, material; };
	const OverlayTexture kOverlayTextures[] = {
#include "..\data\OverlayTextures.inc"
	};
	const char* const kOverlays[] = { "eyebrows", "scars", "eyeliners", "lipsticks", "acne", "shadows",
		"beardstabble", "paintedmasks", "ageing", "blush", "complex", "disc", "foundation", "freckles", "grime",
		"hair", "moles", "spots" };
	struct Overlay
	{
		bool visible = false;
		std::string albedo, normal, material;
		int variation = 0;
		float opacity = 1.0f;
		std::string palette;
		int tints[3] = {};
		int texture = 0, paletteId = 0;
	};
	Overlay g_overlays[std::size(kOverlays)];
	int g_overlay = 0;
	int g_headTexture = -1;

	std::string ApplyOverlays()
	{
		const Ped ped = Me();
		const int index = ComponentIndexInCategory(ped, GameUtil::Joaat("HEADS"));
		if (index < 0)
			return "No head component";
		const MetaTag head = ReadMetaTag(ped, index);
		if (g_headTexture != -1)
		{
			PED::_CLEAR_PED_TEXTURE(g_headTexture);
			PED::_RESET_PED_TEXTURE(g_headTexture);
			PED::_RELEASE_TEXTURE(g_headTexture);
		}
		const int tex = g_headTexture = PED::_REQUEST_TEXTURE(head.albedo, head.normal, head.material);
		for (const Overlay& o : g_overlays)
		{
			if (!o.visible || o.albedo.empty())
				continue;
			const Hash palette = GameUtil::ParseHash(o.palette);
			const int blend = palette ? 0 : 1;
			const int layer = PED::_ADD_TEXTURE_LAYER(tex, GameUtil::ParseHash(o.albedo), GameUtil::ParseHash(o.normal),
				GameUtil::ParseHash(o.material), blend, o.opacity, o.variation);
			if (blend == 0)
			{
				PED::_SET_TEXTURE_LAYER_PALLETE(tex, layer, palette);
				PED::_SET_TEXTURE_LAYER_TINT(tex, layer, o.tints[0], o.tints[1], o.tints[2]);
			}
			PED::_SET_TEXTURE_LAYER_SHEET_GRID_INDEX(tex, layer, o.variation);
			PED::_SET_TEXTURE_LAYER_ALPHA(tex, layer, o.opacity);
		}
		for (int i = 0; i < 200 && !PED::_IS_TEXTURE_VALID(tex); i++)
			WAIT(0);
		if (!PED::_IS_TEXTURE_VALID(tex))
			return "Error Loading Texture";
		PED::_UPDATE_PED_TEXTURE(tex);
		PED::_APPLY_TEXTURE_ON_PED(ped, GameUtil::Joaat("HEADS"), tex);
		WaitReady(ped);
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
		return {};
	}

	void BuildOverlay(MenuBase* m)
	{
		std::vector<std::string> names(std::begin(kOverlays), std::end(kOverlays));
		Ui::Choice(m, "Overlay", names, &g_overlay, [](int) { Ui::Controller().ReopenActiveLater(); });
		Overlay& o = g_overlays[g_overlay];
		Ui::Toggle(m, "Visibility", [&o](bool on) { o.visible = on; })->SetState(o.visible);
		std::vector<const OverlayTexture*> textures, palettes;
		for (const OverlayTexture& t : kOverlayTextures)
		{
			if (t.overlay == std::string_view(kOverlays[g_overlay]))
				textures.push_back(&t);
			else if (t.overlay == std::string_view("palette"))
				palettes.push_back(&t);
		}
		const auto hex = [](Hash h) { return h ? std::format("0x{:08X}", h) : std::string(); };
		if (!textures.empty())
			Ui::Number(m, "TX Id", &o.texture, 0, static_cast<int>(textures.size()) - 1, 1, [&o, textures, hex] {
				const OverlayTexture* t = textures[o.texture];
				o.albedo = hex(t->albedo);
				o.normal = hex(t->normal);
				o.material = hex(t->material);
				Ui::Controller().ReopenActiveLater();
			});
		Ui::Text(m, "Albedo", &o.albedo);
		Ui::Text(m, "Normal", &o.normal);
		Ui::Text(m, "Material", &o.material);
		Ui::Number(m, "Variation", &o.variation, 0, 64, 1);
		Ui::Number(m, "Opacity", &o.opacity, 0.0f, 1.0f, 0.1f);
		Ui::Number(m, "Palette Id", &o.paletteId, 0, static_cast<int>(palettes.size()) - 1, 1, [&o, palettes, hex] {
			o.palette = hex(palettes[o.paletteId]->albedo);
			Ui::Controller().ReopenActiveLater();
		});
		Ui::Text(m, "Palette", &o.palette);
		const char* const kTints[] = { "Primary Color", "Secondary Color", "Tertiary Color" };
		for (int i = 0; i < 3; i++)
			Ui::Number(m, kTints[i], &o.tints[i], 0, 255, 1);
		Ui::Action(m, "Apply", ApplyOverlays);
	}
}

namespace Menus
{
	void BuildWardrobeTop(MenuBase* wardrobe)
	{
		Shared().outfits = Ui::ListMenu(wardrobe, "Outfits", BuildOutfits);
		BuildModelChanger(wardrobe);
	}

	void BuildWardrobe(MenuBase* wardrobe)
	{
		// SubSelfFacialHair ("Hair and Weight").
		MenuBase* hair = Ui::Submenu(wardrobe, "Hair and Weight");
		Ui::Number(hair, "wardrobe.weight", "Weight", &g_weight, -100.0f, 100.0f, 1.0f, SetWeight);
		Ui::Action(hair, "wardrobe.changeweight", "Change Weight", ChangeWeight)->SetHotkeyable(false);
		const char* const kBeard[] = { "Chin Length", "Chops Length", "Stache Length" };
		for (int i = 0; i < 3; i++)
			Ui::Number(hair, Ui::Id("wardrobe.beard", kBeard[i]), kBeard[i], &g_beard[i], 0, 10, 1, [i] { ForgetStaleLockTargets(); SetBeard(i); });
		Ui::Number(hair, "wardrobe.hairlength", "Hair Length", &g_hair, 0, 9, 1, [] { ForgetStaleLockTargets(); SetHair(); });
		Ui::Do(hair, "wardrobe.applyinstantly", "Apply Instantly", [] {
			CAMERA::DO_SCREEN_FADE_OUT(500);
			WAIT(1000);
			CAMERA::DO_SCREEN_FADE_IN(500);
		});
		// Go to Barber: the Valentine barber, Rampage's coordinate.
		Ui::Do(hair, "wardrobe.gotobarber", "Go to Barber", [] {
			ENTITY::SET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), -307.223f, 821.904f, 118.738f, true, true, true, false);
		});
		hair->SetOnOpen([](MenuBase*) { ForgetStaleLockTargets(); ReadHair(); if (!g_lockWeight) ReadWeight(); });
		Ui::Transient(hair);
		Ui::Looped(hair, "wardrobe.lockweight", "Lock Weight", [] { if (!g_lockWeight) { g_lockWeight = true; ReadWeight(); } LockWeightTick(); },
			[] { g_lockWeight = false; });
		g_lockHairCommand = Ui::Looped(hair, "wardrobe.lockhair", "Lock Hair", [] { if (!g_lockHair) StartLockHair(); LockHairTick(); },
			[] {
				g_lockHair = false;
				// Turned off by the user, not by the online kill switch or eject.
				if (g_lockHairCommand && !g_lockHairCommand->IsSuspended())
					ClearLockTargets();
			});
		RegisterLockTargets();

		// SubSelfPedMetaTags.
		MenuBase* tags = Menus::Shared().metaTags = Ui::Submenu(wardrobe, "Meta Ped Tags");
		std::vector<std::string> categories(std::begin(kCategories), std::end(kCategories));
		Ui::Choice(tags, "wardrobe.category", "Category", categories, &g_tagCategory, [](int) { ReadTag(); });
		// Rampage's horse tags read by component index instead (a horse has
		// no clothing categories).
		Ui::Number(tags, "wardrobe.index", "Index", &g_tagIndex, 0, 60, 1);
		Ui::Do(tags, "wardrobe.loaddatafromindex", "Load Data from Index", [] { ReadTagAt(g_tagIndex); });
		const char* const kFields[] = { "Drawable", "Albedo", "Normal", "Material", "Palette" };
		for (int i = 0; i < 5; i++)
			Ui::Text(tags, Ui::Id("wardrobe.tag", kFields[i]), kFields[i], &g_tagFields[i]);
		const char* const kTints[] = { "Primary Color", "Secondary Color", "Tertiary Color" };
		for (int i = 0; i < 3; i++)
			Ui::Number(tags, Ui::Id("wardrobe.tag", kTints[i]), kTints[i], &g_tagTints[i], 0, 255, 1);
		Ui::Do(tags, "wardrobe.apply", "Apply", ApplyTag);
		tags->SetOnOpen([](MenuBase*) { ReadTag(); });
		Ui::Transient(tags);

		// SubSelfPedMetaExpressions / SubHorsePedMetaExpressions /
		// SubPedEditorMetaExpressions: the human or the horse expressions,
		// whichever the target ped is.
		Shared().metaExpressions = Ui::ListMenu(wardrobe, "Meta Ped Expressions", [](MenuBase* m) {
			const bool horse = !PED::IS_PED_HUMAN(Me());
			for (size_t i = 0; i < std::size(kExpressions); i++)
			{
				if (kExpressions[i].horse != horse)
					continue;
				g_expressionValues[i] = PED::_GET_CHAR_EXPRESSION(Me(), kExpressions[i].id);
				Ui::Number(m, kExpressions[i].name, &g_expressionValues[i], -1.0f, 1.0f, 0.1f, [i] {
					PED::_SET_CHAR_EXPRESSION(Me(), kExpressions[i].id, g_expressionValues[i]);
					PED::_UPDATE_PED_VARIATION(Me(), FALSE, TRUE, TRUE, TRUE, FALSE);
				});
			}
		});

		// SubSelfCustomizations.
		Shared().overlays = Ui::ListMenu(wardrobe, "Overlay Textures", BuildOverlay);

		Ui::Toggle(wardrobe, "wardrobe.wardrobecam", "Wardrobe Cam", [](bool on) { if (!on) WardrobeCamOff(); }, WardrobeCamTick);
		Ui::Number(wardrobe, "wardrobe.outfitvariation", "Outfit Variation", &g_outfitVariation, 0, 200, 1, ApplyOutfitVariation);
		Ui::Toggle(wardrobe, "wardrobe.keepfacialhair", "Keep Facial Hair", [](bool on) { g_keepFacialHair = on; });
		Ui::Action(wardrobe, "wardrobe.randomcomponents", "Random Components", RandomComponents);
		Ui::Do(wardrobe, "wardrobe.removeallcomponents", "Remove all Components", RemoveAllComponents);
		Ui::Do(wardrobe, "wardrobe.loadcleanbody", "Load Clean Body", LoadCleanBody);
		Ui::Do(wardrobe, "wardrobe.loaddefaultcomponents", "Load Default Components", LoadDefaultComponents);
		Ui::Do(wardrobe, "wardrobe.applydevbeltbuckle", "Apply Dev Belt Buckle", [] { ApplyShopItem(0xD0B13749); });
		Ui::Do(wardrobe, "wardrobe.drophat", "Drop Hat", [] { PED::KNOCK_OFF_PED_PROP(Me(), FALSE, FALSE, FALSE, TRUE); });
		Ui::Looped(wardrobe, "wardrobe.removeoffhandholster", "Remove Off-Hand Holster", [] { RemoveTagTick(kOffHandHolster, true); });
		Ui::Looped(wardrobe, "wardrobe.removesatchel", "Remove Satchel", [] { RemoveTagTick(kSatchel, false); });
		Ui::Toggle(wardrobe, "wardrobe.neverlosehat", "Never Lose Hat", NeverLoseHatOn, NeverLoseHatTick);

		// SubSelfWardrobeComponent / SubSelfWardrobeWearableState.
		g_stateMenu = Ui::DetachedListMenu("Wearable State", BuildStateMenu);
		g_categoryMenu = Ui::DetachedListMenu("Components", [](MenuBase* m) { BuildCategoryMenu(m, g_categoryIndex); });
		Shared().components = Ui::ListMenu(wardrobe, "Components", BuildComponents);
		g_wardrobeGroupMenu = Ui::DetachedListMenu("Wardrobe Items", BuildWardrobeGroup);
		Ui::ListMenu(wardrobe, "Apply Wardrobe Item", BuildWardrobeGroups);

		Ui::Section(wardrobe, "Custom");
		Ui::Action(wardrobe, "Dump Wardrobe Names", DumpWardrobeNames);
		Ui::Action(wardrobe, "wardrobe.enablepedcomponent", "Enable Ped Component", EnableComponent);
		Ui::Action(wardrobe, "wardrobe.disablepedcomponent", "Disable Ped Component", DisableComponent);
		Ui::Action(wardrobe, "wardrobe.setbodycomponent", "Set Body Component", SetBodyComponent);
		Ui::Action(wardrobe, "wardrobe.loadfromdatafile", "Load from Datafile", LoadOutfitPrompt)->SetHotkeyable(false);
	}
}
