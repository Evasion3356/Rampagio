/*
	Player > Wardrobe: ports Rampage's Submenus::SubSelfWardrobe (the rows
	besides Walk Styles and Damage Packs, which are in PlayerSubmenus.cpp),
	SubSelfWardrobeComponent, SubSelfWardrobeWearableState,
	SubSelfOutfitSaver, SubSelfFacialHair ("Hair and Weight"),
	SubSelfPedMetaTags, SubSelfPedMetaExpressions and SubSelfCustomizations
	("Overlay Textures"). The Model Changer is in ModelChanger.cpp.

	The clothing list is user-supplied, like the other lists:
	Rampagio_ClothingDb.xml next to Rampagio.ini, in the same format as
	Rampage's Lists\ClothingDb.xml (<Component> entries with IsMP, PedType,
	Category and Hash). Without it, the Custom rows still take any hash.
	Category names are the game's metaped category names; the expression
	names come from alloc8or's MetaPedExpression list
	(tools/extract_expressions.py). Saved outfits go in Rampagio_Outfits.ini
	(ours; Rampage writes one XML per outfit).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

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

	std::string TagValue(const std::string& line, const char* tag)
	{
		const std::string open = std::string("<") + tag + ">";
		const size_t a = line.find(open);
		if (a == std::string::npos)
			return {};
		const size_t start = a + open.size();
		const size_t end = line.find('<', start);
		return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
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
				item.pedType = std::atoi(v.c_str());
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

	void NeverLoseHatTick()
	{
		const Ped ped = Me();
		if (PED::_IS_META_PED_USING_COMPONENT(ped, kHat))
			return;
		const Object hat = PED::_GET_PED_LAST_DROPPED_HAT(ped);
		if (hat && ENTITY::DOES_ENTITY_EXIST(hat))
			TASK::TASK_PICKUP_CARRIABLE_ENTITY(ped, hat);
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

	std::string ApplyShopItemPrompt()
	{
		Hash h = 0;
		if (auto err = PromptHash("Enable a Ped Shop Item by Hash", h); !err.empty())
			return err;
		PED::_APPLY_SHOP_ITEM_TO_PED(Me(), h, TRUE, TRUE, FALSE);
		return {};
	}

	// --- outfits ---------------------------------------------------------

	const wchar_t* kOutfitsFile = L"Rampagio_Outfits.ini";
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
		DataFile::Ini ini = DataFile::Load(kOutfitsFile);
		ini.sections.erase(name);
		auto& section = ini.sections[name];
		section["model"] = Hex(ENTITY::GET_ENTITY_MODEL(ped));
		section["removeAll"] = g_removeAllOnLoad ? "1" : "0";
		std::string components;
		const int count = PED::_GET_NUM_COMPONENTS_IN_PED(ped);
		for (int i = 0; i < count; i++)
		{
			BOOL status = FALSE;
			Hash state = 0;
			const Hash item = PED::_GET_SHOP_ITEM_COMPONENT_AT_INDEX(ped, i, TRUE, &status, &state);
			if (item)
				components += (components.empty() ? "" : ",") + Hex(item);
		}
		section["components"] = components;
		if (completeModel)
			for (int i = 0; i < count; i++)
			{
				const MetaTag t = ReadMetaTag(ped, i);
				section[std::format("tag{}", i)] = std::format("{},{},{},{},{},{},{},{}", Hex(t.drawable), Hex(t.albedo),
					Hex(t.normal), Hex(t.material), Hex(t.palette), t.tint0, t.tint1, t.tint2);
			}
		return DataFile::Save(kOutfitsFile, ini) ? "Saved " + name : "Couldn't save";
	}

	std::vector<std::string> Split(const std::string& s)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (start <= s.size())
		{
			const size_t comma = s.find(',', start);
			out.push_back(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
		return out;
	}

	std::string LoadOutfit(const std::string& name)
	{
		DataFile::Ini ini = DataFile::Load(kOutfitsFile);
		auto it = ini.sections.find(name);
		if (it == ini.sections.end())
			return "No outfit named " + name;
		auto& section = it->second;
		const Ped ped = Me();
		const bool mp = IsMpModel(ped);
		if (section["removeAll"] == "1")
			RemoveAllComponents();
		for (const std::string& h : Split(section["components"]))
			if (!h.empty())
				PED::_APPLY_SHOP_ITEM_TO_PED(ped, GameUtil::ParseHash(h), FALSE, mp, FALSE);
		Refresh(ped, mp);
		WaitReady(ped);
		for (int i = 0; ; i++)
		{
			auto tag = section.find(std::format("tag{}", i));
			if (tag == section.end())
				break;
			const auto v = Split(tag->second);
			if (v.size() != 8)
				continue;
			ApplyMetaTag(ped, { GameUtil::ParseHash(v[0]), GameUtil::ParseHash(v[1]), GameUtil::ParseHash(v[2]),
				GameUtil::ParseHash(v[3]), GameUtil::ParseHash(v[4]), std::atoi(v[5].c_str()), std::atoi(v[6].c_str()),
				std::atoi(v[7].c_str()) });
		}
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
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
		DataFile::Ini ini = DataFile::Load(kOutfitsFile);
		if (!ini.sections.empty())
			Ui::Section(m, "Saved Outfits");
		for (auto& [name, section] : ini.sections)
		{
			const std::string n = name;
			Ui::Action(m, n, [n] { return LoadOutfit(n); });
		}
		if (!ini.sections.empty())
		{
			Ui::Section(m, "Delete");
			for (auto& [name, section] : ini.sections)
			{
				const std::string n = name;
				Ui::Action(m, "Delete " + n, [n] {
					DataFile::Ini file = DataFile::Load(kOutfitsFile);
					file.sections.erase(n);
					DataFile::Save(kOutfitsFile, file);
					Ui::Controller().ReopenActiveLater();
					return "Deleted " + n;
				});
			}
		}
	}

	// --- hair and weight ---------------------------------------------------

	// Global_40.f_7731[3 /*5*/]: chin, chops and mustache length (0..10)
	// with a growth timestamp in .f_3; Global_40.f_7748: hair, length in
	// .f_1 (0..9) and timestamp in .f_5. Global_40.f_11095.f_11[13]: body
	// weight (-100..100). Rampage zeroes the timestamps so the hair doesn't
	// grow back at once.
	int g_beard[3] = {};
	int g_hair = 0;

	UINT64* Global40() { return GameUtil::Global(40); }

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
			g[7735 + 5 * i] = 0;
		}
	}

	void SetHair()
	{
		if (UINT64* g = Global40())
		{
			g[7749] = g_hair;
			g[7753] = 0;
		}
	}

	float* WeightSlot()
	{
		UINT64* g = Global40();
		return g ? reinterpret_cast<float*>(&g[11120]) : nullptr;
	}

	// After the write Rampage raises Global_1347477.f_201 and clears f_198
	// and f_200, which makes the game re-apply the player's body.
	std::string ChangeWeight()
	{
		std::string text;
		if (!GameUtil::PromptText("Enter Weight -100 to 100", text) || text.empty())
			return {};
		float* slot = WeightSlot();
		UINT64* refresh = GameUtil::Global(1347477);
		if (!slot || !refresh)
			return "Script globals not found";
		*slot = std::clamp(static_cast<float>(std::atof(text.c_str())), -100.0f, 100.0f);
		WAIT(300);
		refresh[201] = 1;
		refresh[198] = 0;
		refresh[200] = 0;
		return {};
	}

	// --- meta ped tags -----------------------------------------------------

	int g_tagCategory = 0;
	std::string g_tagFields[5]; // drawable, albedo, normal, material, palette
	int g_tagTints[3] = {};

	void ReadTag()
	{
		const Ped ped = Me();
		const int index = ComponentIndexInCategory(ped, GameUtil::Joaat(kCategories[g_tagCategory]));
		const MetaTag t = index >= 0 ? ReadMetaTag(ped, index) : MetaTag{};
		const Hash values[5] = { t.drawable, t.albedo, t.normal, t.material, t.palette };
		for (int i = 0; i < 5; i++)
			g_tagFields[i] = Hex(values[i]);
		g_tagTints[0] = t.tint0;
		g_tagTints[1] = t.tint1;
		g_tagTints[2] = t.tint2;
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
	// Rampage's Apply does. Rampage picks each layer's texture from its own
	// tables (TX Id); ours takes the texture hashes typed in.
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
		Ui::Text(m, "Albedo", &o.albedo);
		Ui::Text(m, "Normal", &o.normal);
		Ui::Text(m, "Material", &o.material);
		Ui::Number(m, "Variation", &o.variation, 0, 64, 1);
		Ui::Number(m, "Opacity", &o.opacity, 0.0f, 1.0f, 0.1f);
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
		Ui::ListMenu(wardrobe, "Outfits", BuildOutfits);
		BuildModelChanger(wardrobe);
	}

	void BuildWardrobe(MenuBase* wardrobe)
	{
		// SubSelfFacialHair ("Hair and Weight"). Rampage's Go to Barber uses a
		// coordinate from its own table; not ported.
		MenuBase* hair = Ui::Submenu(wardrobe, "Hair and Weight");
		hair->AddItem(new MenuItemLabel([] {
			const float* w = WeightSlot();
			return std::format("Current Weight: {:.0f}", w ? *w : 0.0f);
		}));
		Ui::Action(hair, "Change Weight", ChangeWeight);
		const char* const kBeard[] = { "Chin Length", "Chops Length", "Stache Length" };
		for (int i = 0; i < 3; i++)
			Ui::Number(hair, kBeard[i], &g_beard[i], 0, 10, 1, [i] { SetBeard(i); });
		Ui::Number(hair, "Hair Length", &g_hair, 0, 9, 1, SetHair);
		Ui::Do(hair, "Apply Instantly", [] {
			CAMERA::DO_SCREEN_FADE_OUT(500);
			WAIT(1000);
			CAMERA::DO_SCREEN_FADE_IN(500);
		});
		hair->SetOnOpen([](MenuBase*) { ReadHair(); });

		// SubSelfPedMetaTags.
		MenuBase* tags = Ui::Submenu(wardrobe, "Meta Ped Tags");
		std::vector<std::string> categories(std::begin(kCategories), std::end(kCategories));
		Ui::Choice(tags, "Category", categories, &g_tagCategory, [](int) { ReadTag(); });
		const char* const kFields[] = { "Drawable", "Albedo", "Normal", "Material", "Palette" };
		for (int i = 0; i < 5; i++)
			Ui::Text(tags, kFields[i], &g_tagFields[i]);
		const char* const kTints[] = { "Primary Color", "Secondary Color", "Tertiary Color" };
		for (int i = 0; i < 3; i++)
			Ui::Number(tags, kTints[i], &g_tagTints[i], 0, 255, 1);
		Ui::Do(tags, "Apply", ApplyTag);
		tags->SetOnOpen([](MenuBase*) { ReadTag(); });

		// SubSelfPedMetaExpressions: human expressions only (the horse ones
		// belong to the horse menus).
		MenuBase* expressions = Ui::Submenu(wardrobe, "Meta Ped Expressions");
		for (size_t i = 0; i < std::size(kExpressions); i++)
		{
			if (kExpressions[i].horse)
				continue;
			Ui::Number(expressions, kExpressions[i].name, &g_expressionValues[i], -1.0f, 1.0f, 0.1f, [i] {
				PED::_SET_CHAR_EXPRESSION(Me(), kExpressions[i].id, g_expressionValues[i]);
				PED::_UPDATE_PED_VARIATION(Me(), FALSE, TRUE, TRUE, TRUE, FALSE);
			});
		}
		expressions->SetOnOpen([](MenuBase*) {
			for (size_t i = 0; i < std::size(kExpressions); i++)
				g_expressionValues[i] = PED::_GET_CHAR_EXPRESSION(Me(), kExpressions[i].id);
		});

		// SubSelfCustomizations.
		Ui::ListMenu(wardrobe, "Overlay Textures", BuildOverlay);

		Ui::Toggle(wardrobe, "Wardrobe Cam", [](bool on) { if (!on) WardrobeCamOff(); }, WardrobeCamTick);
		Ui::Number(wardrobe, "Outfit Variation", &g_outfitVariation, 0, 200, 1, ApplyOutfitVariation);
		Ui::Toggle(wardrobe, "Keep Facial Hair", [](bool on) { g_keepFacialHair = on; });
		Ui::Action(wardrobe, "Random Components", RandomComponents);
		Ui::Do(wardrobe, "Remove all Components", RemoveAllComponents);
		Ui::Do(wardrobe, "Load Clean Body", LoadCleanBody);
		Ui::Do(wardrobe, "Load Default Components", LoadDefaultComponents);
		Ui::Do(wardrobe, "Apply Dev Belt Buckle", [] { ApplyShopItem(0xD0B13749); });
		Ui::Do(wardrobe, "Drop Hat", [] { PED::KNOCK_OFF_PED_PROP(Me(), FALSE, FALSE, FALSE, TRUE); });
		Ui::Looped(wardrobe, "Remove Off-Hand Holster", [] { RemoveTagTick(kOffHandHolster, true); });
		Ui::Looped(wardrobe, "Remove Satchel", [] { RemoveTagTick(kSatchel, false); });
		Ui::Looped(wardrobe, "Never Lose Hat", NeverLoseHatTick);

		// SubSelfWardrobeComponent / SubSelfWardrobeWearableState.
		g_stateMenu = Ui::DetachedListMenu("Wearable State", BuildStateMenu);
		g_categoryMenu = Ui::DetachedListMenu("Components", [](MenuBase* m) { BuildCategoryMenu(m, g_categoryIndex); });
		Ui::ListMenu(wardrobe, "Components", BuildComponents);

		Ui::Section(wardrobe, "Custom");
		Ui::Action(wardrobe, "Enable Ped Component", EnableComponent);
		Ui::Action(wardrobe, "Disable Ped Component", DisableComponent);
		Ui::Action(wardrobe, "Set Body Component", SetBodyComponent);
		Ui::Action(wardrobe, "Apply Shop Item", ApplyShopItemPrompt);
		Ui::Action(wardrobe, "Load from Datafile", LoadOutfitPrompt);
	}
}
