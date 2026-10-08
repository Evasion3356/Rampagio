/*
	Horse menu: ports Rampage's Submenus::SubSelfHorse rows. Rampage acts on
	the current mount for most rows and on its tracked "primary horse" for
	the rest; we use GameUtil::PlayerHorse() (mount, else last mount, else
	saddle horse) for both.

	Also SubHorseBlip, SubHorseLoader (ours: Rampagio_Horses.json), SubHorseStats,
	SubMobileStable / SubMobileStableComponent (Rampage's item tables, its
	named families with a tint pick, data\MobileStable.inc, plus every tack
	item the game scripts name under All Tack, tools/extract_peds.py) and the
	horse's Meta Ped Tags / Expressions, which open the Player's menus bound
	to the horse (Menus::Target).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\keyboard.h"
#include "..\DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <deque>
#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Ped Mount() { return GameUtil::PlayerMount(); }
	Ped Horse() { return GameUtil::PlayerHorse(); }

	constexpr Hash INPUT_SPRINT = 0x8FFC75D6;
	constexpr Hash INPUT_HORSE_SPRINT = 0xE4D2CE1D;
	constexpr Hash INPUT_MOVE_UP_ONLY = 0x8FD015D8;
	constexpr Hash INPUT_MOVE_LEFT_ONLY = 0x7065027D;
	constexpr Hash INPUT_MOVE_DOWN_ONLY = 0xD27782E3;
	constexpr Hash INPUT_MOVE_RIGHT_ONLY = 0xB4E465B4;
	constexpr Hash INPUT_WHISTLE_HORSEBACK = 0x24978A28;

	// Applies `fn` to the current mount, if any.
	template <typename Fn>
	void OnMount(Fn fn)
	{
		if (const Ped m = Mount())
			fn(m);
	}

	// Tracks which horse a set-and-forget toggle was applied to, so it moves
	// to a new mount and is undone on the old one.
	struct MountState
	{
		Ped applied = 0;
		void Tick(void (*set)(Ped, bool))
		{
			const Ped m = Mount();
			if (!m || m == applied)
				return;
			Off(set);
			set(m, true);
			applied = m;
		}
		void Off(void (*set)(Ped, bool))
		{
			if (applied && ENTITY::DOES_ENTITY_EXIST(applied))
				set(applied, false);
			applied = 0;
		}
	};

	void SetInvincible(Ped h, bool on) { ENTITY::SET_ENTITY_INVINCIBLE(h, on); }
	void SetInvisible(Ped h, bool on) { ENTITY::SET_ENTITY_VISIBLE(h, !on); }
	void SetNoRagdoll(Ped h, bool on)
	{
		PED::SET_PED_CAN_RAGDOLL(h, !on);
		PED::SET_PED_CAN_RAGDOLL_FROM_PLAYER_IMPACT(h, !on);
		PED::SET_PED_RAGDOLL_ON_COLLISION(h, !on, FALSE);
	}
	void SetCalm(Ped h, bool on)
	{
		PED::_SET_PED_MOTIVATION(h, 3, 0.0f, 0);
		PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(h, on);
	}
	void SetFlamingHooves(Ped h, bool on) { PED::SET_PED_CONFIG_FLAG(h, 207, on); }

	MountState g_invincible, g_invisible, g_noRagdoll, g_calm, g_hooves;

	float g_scale = 1.0f;
	void ApplyScale() { OnMount([](Ped m) { PED::_SET_PED_SCALE(m, g_scale); }); }

	// Super speed: hold sprint to push the horse forward; it stops dead when
	// sprint is released. Neither horse nor rider can ragdoll meanwhile.
	float g_superSpeed = 35.0f;
	void SuperSpeedTick()
	{
		PAD::DISABLE_CONTROL_ACTION(0, INPUT_HORSE_SPRINT, TRUE);
		PED::SET_PED_CAN_RAGDOLL(Me(), FALSE);
		const Ped m = Mount();
		if (!m)
			return;
		PED::SET_PED_CAN_RAGDOLL(m, FALSE);
		if (ENTITY::IS_ENTITY_IN_AIR(m, 0))
			return;
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_SPRINT))
		{
			ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(m, FALSE);
			ENTITY::APPLY_FORCE_TO_ENTITY(m, 1, 0.0f, g_superSpeed, 0.0f, 0.0f, 0.0f, 0.0f, 0, TRUE, TRUE, TRUE, FALSE, TRUE);
		}
		else if (PAD::IS_DISABLED_CONTROL_JUST_RELEASED(0, INPUT_SPRINT))
			ENTITY::SET_ENTITY_VELOCITY(m, 0.0f, 0.0f, 0.0f);
	}

	void SuperSpeedOff()
	{
		PED::SET_PED_CAN_RAGDOLL(Me(), TRUE);
		OnMount([](Ped m) { PED::SET_PED_CAN_RAGDOLL(m, TRUE); });
	}

	// Horse fly mode: the horse faces the camera and WASD (or the move stick)
	// moves it along the camera's direction, Rampage's control scheme. Ours
	// moves in 3D (camera pitch included), so looking up climbs.
	float g_flySpeed = 1.0f;
	void FlyTick()
	{
		const Ped m = Mount();
		if (!m)
			return;
		const Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
		const float yaw = rot.z * 0.0174532925f, pitch = rot.x * 0.0174532925f;
		const float fx = -sinf(yaw) * cosf(pitch), fy = cosf(yaw) * cosf(pitch), fz = sinf(pitch);
		const float rx = cosf(yaw), ry = sinf(yaw);
		ENTITY::SET_ENTITY_ROTATION(m, 0.0f, 0.0f, rot.z, 2, TRUE);
		Vector3 p = ENTITY::GET_ENTITY_COORDS(m, FALSE, FALSE);
		float dx = 0.0f, dy = 0.0f, dz = 0.0f;
		if (IsKeyDown('W') || PAD::IS_CONTROL_PRESSED(0, INPUT_MOVE_UP_ONLY)) { dx += fx; dy += fy; dz += fz; }
		if (IsKeyDown('S') || PAD::IS_CONTROL_PRESSED(0, INPUT_MOVE_DOWN_ONLY)) { dx -= fx; dy -= fy; dz -= fz; }
		if (IsKeyDown('A') || PAD::IS_CONTROL_PRESSED(0, INPUT_MOVE_LEFT_ONLY)) { dx -= rx; dy -= ry; }
		if (IsKeyDown('D') || PAD::IS_CONTROL_PRESSED(0, INPUT_MOVE_RIGHT_ONLY)) { dx += rx; dy += ry; }
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(m, p.x + dx * g_flySpeed, p.y + dy * g_flySpeed, p.z + dz * g_flySpeed, TRUE, TRUE, TRUE);
	}

	void FillCores(Ped h)
	{
		ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(h, 0, 100);
		ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(h, 1, 100);
	}

	void HealHorse(Ped h)
	{
		ENTITY::SET_ENTITY_HEALTH(h, ENTITY::GET_ENTITY_MAX_HEALTH(h, FALSE), 0);
	}

	void FillHorseCores(Ped h)
	{
		FillCores(h);
		PED::_RESTORE_PED_STAMINA(h, 100.0f);
		HealHorse(h);
	}

	// Rampage plays the "feed horse" interaction first, then gilds the cores.
	std::string CoresOverpower()
	{
		const Ped m = Mount();
		if (!m)
			return "Not riding a horse";
		TASK::TASK_ANIMAL_INTERACTION(Me(), m, 0xAF387403, 0x22445D3A, FALSE);
		WAIT(1000);
		for (int i = 0; i < 2; i++)
		{
			ATTRIBUTE::ENABLE_ATTRIBUTE_OVERPOWER(m, i, 900.0f, TRUE);
			ATTRIBUTE::_ENABLE_ATTRIBUTE_CORE_OVERPOWER(m, i, 100.0f, TRUE);
		}
		return {};
	}

	std::string Rename()
	{
		const Ped m = Mount();
		if (!m)
			return "Not riding a horse";
		std::string name;
		if (!GameUtil::PromptText("Horse name", name, 30) || name.empty())
			return {};
		PED::_SET_PED_PROMPT_NAME(m, name.c_str());
		return TrFormat("Renamed to {}", name);
	}

	void Ragdoll()
	{
		const Ped m = Mount();
		if (!m)
			return;
		SetNoRagdoll(m, false);
		PED::SET_PED_TO_RAGDOLL(m, 2000, 2000, 0, TRUE, TRUE, "DraggedByCart");
	}

	// Makes the horse the player's saddle horse, with the ownership and
	// config flags the game sets on a purchased horse.
	std::string SetAsPrimary()
	{
		const Ped h = Mount();
		if (!h)
			return "Not riding a horse";
		const Player player = PLAYER::PLAYER_ID();
		PLAYER::_SET_PED_AS_SADDLE_HORSE_FOR_PLAYER(player, h);
		PED::_CLEAR_ACTIVE_ANIMAL_OWNER(h, FALSE);
		PED::SET_PED_OWNS_ANIMAL(Me(), h, FALSE);
		PED::_SET_PED_PERSONALITY(h, 0x7C73408);
		POPULATION::_SET_PED_SHOULD_IGNORE_AVOIDANCE_VOLUMES(h, 1);
		PED::_SET_PED_CAN_BE_LASSOED(h, FALSE);
		PLAYER::_SET_PLAYER_MOUNT_STATE_ACTIVE(player, TRUE);
		PED::REQUEST_PED_VISIBILITY_TRACKING(h);
		FLOCK::_SET_ANIMAL_IS_WILD(h, FALSE);
		for (int flag : { 211, 208, 209, 400, 297, 277, 319, 6 })
			PED::SET_PED_CONFIG_FLAG(h, flag, TRUE);
		for (int flag : { 136, 312, 113, 301 })
			PED::SET_PED_CONFIG_FLAG(h, flag, FALSE);
		FLOCK::SET_ANIMAL_TUNING_BOOL_PARAM(h, 25, FALSE);
		FLOCK::SET_ANIMAL_TUNING_BOOL_PARAM(h, 24, FALSE);
		return "Set as primary horse";
	}

	void RemovePelts(Ped h)
	{
		for (int i = 0; i < 4; i++)
			if (const int pelt = PED::_GET_PELT_FROM_HORSE(h, i))
				PED::_CLEAR_PELT_FROM_HORSE(h, pelt);
	}

	void DeleteHorse(Ped h)
	{
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(h))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(h, TRUE, TRUE);
		Entity e = h;
		ENTITY::DELETE_ENTITY(&e);
	}

	std::string TeleportHorseToMe()
	{
		const Ped h = Horse();
		if (!h)
			return "No horse";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(h, p.x, p.y, p.z, TRUE, TRUE, TRUE);
		return {};
	}

	std::string TeleportToHorse()
	{
		const Ped h = Horse();
		if (!h)
			return "No horse";
		PED::SET_PED_ONTO_MOUNT(Me(), h, -1, FALSE);
		return {};
	}

	void WhistleTick()
	{
		if (!PAD::IS_CONTROL_PRESSED(0, INPUT_WHISTLE_HORSEBACK) || PED::IS_PED_ON_MOUNT(Me()))
			return;
		if (const Ped h = Horse())
			PED::SET_PED_ONTO_MOUNT(Me(), h, -1, FALSE);
	}

	std::string PlayAnim(const char* dict, const char* anim)
	{
		const Ped h = Horse();
		if (!h)
			return "No horse";
		if (!GameUtil::LoadAnimDict(dict))
			return "Animation didn't load";
		TASK::TASK_PLAY_ANIM(h, dict, anim, 8.0f, -8.0f, -1, 0, 0.0f, FALSE, 0, FALSE, "", FALSE);
		WAIT(100);
		STREAMING::REMOVE_ANIM_DICT(dict);
		return {};
	}

	std::string Revive()
	{
		const Ped h = Horse();
		if (!h)
			return "No horse";
		const int max = PED::GET_PED_MAX_HEALTH(h);
		PED::SET_PED_MAX_HEALTH(h, max);
		ENTITY::SET_ENTITY_HEALTH(h, max, 0);
		PED::RESURRECT_PED(h);
		PED::REVIVE_INJURED_PED(h);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(h, TRUE, TRUE);
		return {};
	}

	// Side saddle: while riding, loop the female rear-seat riding anim that
	// matches the horse's gait, slope and turn.
	std::string g_sideDict;
	const char* g_sideAnim = "idle";
	void SideSaddleTick()
	{
		const Ped ped = Me();
		const Ped m = Mount();
		if (!m || !PED::IS_PED_FULLY_ON_MOUNT(ped, TRUE))
			return;
		const Vector3 v = ENTITY::GET_ENTITY_SPEED_VECTOR(m, TRUE);
		const char* gait;
		const char* anim;
		if (v.y < 2.0f)
			gait = anim = "idle";
		else
		{
			const bool canter = v.y < 5.0f;
			gait = v.z > 0.2f ? (canter ? "cantern@slope@up" : "gallop@slope@up")
				: v.z < -0.2f ? (canter ? "cantern@slope@down" : "gallop@slope@down")
				: (canter ? "cantern" : "gallop");
			anim = v.x > 0.2f ? "turn_l2" : v.x < -0.2f ? "turn_r2" : "turn";
		}
		const std::string dict = std::format("veh_horseback@seat_rear@female@left@normal@{}", gait);
		if (ENTITY::IS_ENTITY_PLAYING_ANIM(ped, dict.c_str(), anim, 17))
			return;
		if (!g_sideDict.empty())
			TASK::STOP_ANIM_TASK(ped, g_sideDict.c_str(), g_sideAnim, 0.0f);
		if (GameUtil::LoadAnimDict(dict.c_str()))
			TASK::TASK_PLAY_ANIM(ped, dict.c_str(), anim, 1.0f, 1.0f, -1, 17, 0.0f, FALSE, 0, FALSE, "", FALSE);
		g_sideDict = dict;
		g_sideAnim = anim;
	}

	void SideSaddleOff()
	{
		if (!g_sideDict.empty())
			TASK::STOP_ANIM_TASK(Me(), g_sideDict.c_str(), g_sideAnim, 0.0f);
		g_sideDict.clear();
	}
	// --- blip (SubHorseBlip) ------------------------------------------------------

	constexpr Hash BLIP_STYLE_HORSE = 0x8D39991C; // Rampage's horse blip style
	Ped g_blipHorse = 0;
	Blip g_horseBlip = 0;

	std::string AddHorseBlip()
	{
		const Ped mount = Mount();
		if (!mount)
			return "Get on a horse first";
		if (g_horseBlip && MAP::DOES_BLIP_EXIST(g_horseBlip))
			MAP::REMOVE_BLIP(&g_horseBlip);
		g_blipHorse = mount;
		g_horseBlip = MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_HORSE, mount);
		MAP::SET_BLIP_NAME_FROM_TEXT_FILE(g_horseBlip, "BLIP_AMBIENT_HORSE");
		return "";
	}

	std::string TeleportToBlipHorse()
	{
		if (!ENTITY::DOES_ENTITY_EXIST(g_blipHorse))
			return "No blipped horse";
		const Vector3 v = ENTITY::GET_ENTITY_COORDS(g_blipHorse, TRUE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Me(), v.x, v.y, v.z, TRUE, TRUE, TRUE);
		PED::SET_PED_ONTO_MOUNT(Me(), g_blipHorse, -1, TRUE);
		return "";
	}

	std::string BlipHorseToMe()
	{
		if (!ENTITY::DOES_ENTITY_EXIST(g_blipHorse))
			return "No blipped horse";
		const Vector3 v = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(g_blipHorse, v.x, v.y, v.z, TRUE, TRUE, TRUE);
		return "";
	}

	// --- loader (SubHorseLoader; ours: Rampagio_Horses.json)------------------------

	const wchar_t* kHorsesFile = L"Rampagio_Horses.json";
	constexpr int kGenderExpression = 0xA28B; // 1.0 female, 0.0 male (mp_intro HORSE_GENDER_* labels)

	std::string SaveHorse()
	{
		const Ped horse = Horse();
		if (!horse)
			return "No horse";
		std::string name;
		if (!GameUtil::PromptText("Horse name", name, 40) || name.empty())
			return "";
		nlohmann::json saved = { { "model", ENTITY::GET_ENTITY_MODEL(horse) }, { "gender", PED::_GET_CHAR_EXPRESSION(horse, kGenderExpression) } };
		nlohmann::json tags = nlohmann::json::array();
		const int count = PED::_GET_NUM_COMPONENTS_IN_PED(horse);
		for (int i = 0; i < count; i++)
		{
			Hash d = 0, a = 0, n = 0, m = 0, palette = 0;
			int t0 = 0, t1 = 0, t2 = 0;
			PED::GET_META_PED_ASSET_GUIDS(horse, i, &d, &a, &n, &m);
			PED::GET_META_PED_ASSET_TINT(horse, i, &palette, &t0, &t1, &t2);
			tags.push_back({ d, a, n, m, palette, t0, t1, t2 });
		}
		saved["tags"] = std::move(tags);
		nlohmann::json file = DataFile::LoadJson(kHorsesFile);
		file[name] = std::move(saved);
		Ui::Controller().ReopenActiveLater();
		return DataFile::SaveJson(kHorsesFile, file) ? "Saved " + name : "Couldn't save";
	}

	std::string LoadHorse(const std::string& name)
	{
		const nlohmann::json file = DataFile::LoadJson(kHorsesFile);
		auto it = file.find(name);
		if (it == file.end() || !it->is_object() || !it->contains("model") || !it->at("model").is_number())
			return TrFormat("No horse named {}", name);
		const nlohmann::json& saved = *it;
		const Hash model = saved["model"].get<Hash>();
		if (!GameUtil::LoadModel(model))
			return "Couldn't load the model";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const Ped horse = PED::CREATE_PED(model, p.x + 2.0f, p.y, p.z, ENTITY::GET_ENTITY_HEADING(Me()), FALSE, TRUE, FALSE, FALSE);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		if (!horse)
			return "Couldn't spawn it";
		PED::_SET_RANDOM_OUTFIT_VARIATION(horse, TRUE);
		for (int i = PED::_GET_NUM_COMPONENTS_IN_PED(horse) - 1; i >= 0; i--)
			if (const Hash category = PED::_GET_PED_COMPONENT_CATEGORY_BY_INDEX(horse, i))
				PED::REMOVE_TAG_FROM_META_PED(horse, category, 0);
		for (const nlohmann::json& t : saved.value("tags", nlohmann::json::array()))
		{
			if (!t.is_array() || t.size() != 8 || !std::all_of(t.begin(), t.end(), [](const nlohmann::json& v) { return v.is_number(); }))
				continue;
			PED::_SET_META_PED_TAG(horse, t[0].get<Hash>(), t[1].get<Hash>(), t[2].get<Hash>(), t[3].get<Hash>(), t[4].get<Hash>(),
				t[5].get<int>(), t[6].get<int>(), t[7].get<int>());
		}
		const auto gender = saved.find("gender");
		PED::_SET_CHAR_EXPRESSION(horse, kGenderExpression, gender != saved.end() && gender->is_number() ? gender->get<float>() : 0.0f);
		PED::_UPDATE_PED_VARIATION(horse, FALSE, TRUE, TRUE, TRUE, FALSE);
		return "";
	}

	void BuildLoader(MenuBase* m)
	{
		Ui::Action(m, "Save Current", SaveHorse);
		Ui::Section(m, "Load Horse");
		const nlohmann::json saved = DataFile::LoadJson(kHorsesFile);
		if (saved.empty())
			Ui::Section(m, "No saved horses");
		for (const auto& [name, horse] : saved.items())
			Ui::Action(m, name, [name] { return LoadHorse(name); });
		if (!saved.empty())
		{
			Ui::Section(m, "Delete");
			for (const auto& [name, horse] : saved.items())
				Ui::Action(m, TrFormat("Delete {}", name), [name]
				{
					nlohmann::json file = DataFile::LoadJson(kHorsesFile);
					file.erase(name);
					DataFile::SaveJson(kHorsesFile, file);
					Ui::Controller().ReopenActiveLater();
					return TrFormat("Deleted {}", name);
				});
		}
	}

	// --- stats (SubHorseStats) -------------------------------------------------------

	int g_gender = 0; // 0 female, 1 male
	struct HorseStat
	{
		const char* name;
		int attribute;
		int value;
	};
	HorseStat g_stats[] = {
		{ "Health Core", 0, 0 }, { "Stamina Core", 1, 0 }, { "Handling Level", 4, 0 }, { "Speed Level", 5, 0 },
		{ "Acceleration Level", 6, 0 }, { "Bonding Level", 7, 0 }, { "Weight", 13, 0 },
	};

	std::string MaxBonding()
	{
		const Ped mount = Mount();
		if (!mount)
			return "Get on a horse first";
		if (PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID()) != mount)
			return "Only works for your active saddle horse";
		// Global_40.f_1095 is the active horse slot; the slot's bonding
		// points are .f_1[slot /*436*/].f_372.f_1 (as the scripts write it).
		UINT64* slotGlobal = GameUtil::Global(40 + 1095);
		if (!slotGlobal)
			return "Script globals aren't available";
		const int slot = *reinterpret_cast<int*>(slotGlobal);
		UINT64* points = GameUtil::Global(40 + 1095 + 1 + 436 * slot + 372 + 1);
		if (!points)
			return "Script globals aren't available";
		const float needed = static_cast<float>(ATTRIBUTE::GET_DEFAULT_ATTRIBUTE_POINTS_NEEDED_FOR_RANK(ENTITY::GET_ENTITY_MODEL(mount), 7, 4)) - 2.0f;
		float& current = *reinterpret_cast<float*>(points);
		if (needed > current)
			current = needed;
		return "Bonding maxed";
	}

	// --- mobile stable (SubMobileStable / SubMobileStableComponent) -----------------

	struct Tack
	{
		const char* group;
		const char* name;
	};
	const Tack kTack[] = {
#include "..\data\HorseTack.inc"
	};
	struct StableItem
	{
		const char* kind;
		const char* family;
		Hash item;
	};
	const StableItem kStableItems[] = {
#include "..\data\MobileStable.inc"
	};
	// Rampage's kinds and the tag category each one's Disable removes.
	struct StableKind
	{
		const char* name;
		const char* category;
	};
	const StableKind kStableKinds[] = { { "Saddles", "HORSE_SADDLES" }, { "Saddle Bags", "horse_saddlebags" },
		{ "Stirrups", "saddle_stirrups" }, { "Horns", "saddle_horns" }, { "Blankets", "horse_blankets" },
		{ "Bedrolls", "horse_bedrolls" }, { "Manes", "horse_manes" }, { "Tails", "horse_tails" },
		{ "Body Components", nullptr } };
	std::deque<int> g_stableTints;

	// Tack categories Remove All clears (13, as Rampage's).
	const char* const kTackCategories[] = { "HORSE_SADDLES", "horse_blankets", "horse_bedrolls", "horse_saddlebags",
		"saddle_horns", "saddle_stirrups", "saddle_lanterns", "horse_manes", "horse_tails", "horse_mustache",
		"horse_bridles", "horse_shoes", "horse_holsters" };

	void ApplyTack(Hash item)
	{
		OnMount([item](Ped m) {
			PED::_APPLY_SHOP_ITEM_TO_PED(m, item, FALSE, FALSE, FALSE);
			PED::_APPLY_SHOP_ITEM_TO_PED(m, item, FALSE, TRUE, FALSE);
			PED::_UPDATE_PED_VARIATION(m, FALSE, TRUE, TRUE, TRUE, FALSE);
		});
	}

	void RemoveTack(Hash category)
	{
		OnMount([category](Ped m) {
			PED::REMOVE_TAG_FROM_META_PED(m, category, 0);
			PED::_UPDATE_PED_VARIATION(m, FALSE, TRUE, TRUE, TRUE, FALSE);
		});
	}

	// SubMobileStableComponent: a family picks one of its tint variants;
	// a plain list applies the item.
	void BuildStableKind(MenuBase* stable, const StableKind& kind)
	{
		MenuBase* sub = Ui::Submenu(stable, kind.name);
		const std::string prefix = Ui::Id("horse.stable", kind.name);
		if (kind.category)
		{
			const Hash category = GameUtil::Joaat(kind.category);
			Ui::Do(sub, prefix + ".disable", "Disable", [category] { RemoveTack(category); });
		}
		std::vector<std::string_view> families;
		for (const StableItem& i : kStableItems)
			if (kind.name == std::string_view(i.kind) && *i.family
				&& std::find(families.begin(), families.end(), i.family) == families.end())
				families.push_back(i.family);
		for (std::string_view family : families)
		{
			std::vector<Hash> tints;
			for (const StableItem& i : kStableItems)
				if (kind.name == std::string_view(i.kind) && family == i.family)
					tints.push_back(i.item);
			int* tint = &g_stableTints.emplace_back(0);
			const std::string name(family);
			Ui::Number(sub, Ui::Id(prefix, name), name, tint, 0, static_cast<int>(tints.size()) - 1, 1,
				[tint, tints] { ApplyTack(tints[*tint]); }, true)->SetTransient();
		}
		int n = 0;
		for (const StableItem& i : kStableItems)
			if (kind.name == std::string_view(i.kind) && !*i.family)
			{
				const Hash item = i.item;
				const std::string caption = std::format("{} 0x{:08X}", ++n, item);
				Ui::Do(sub, Ui::Id(prefix, std::format("0x{:08X}", item)), caption, [item] { ApplyTack(item); });
			}
	}

	void BuildMobileStable(MenuBase* horse)
	{
		MenuBase* stable = Ui::Submenu(horse, "Mobile Stable");
		for (const StableKind& kind : kStableKinds)
			BuildStableKind(stable, kind);
		// Every tack item the game scripts name, by group (ours).
		MenuBase* all = Ui::Submenu(stable, "All Tack");
		std::vector<std::string> groups;
		for (const Tack& t : kTack)
			if (std::find(groups.begin(), groups.end(), t.group) == groups.end())
				groups.push_back(t.group);
		for (const std::string& group : groups)
		{
			MenuBase* sub = Ui::Submenu(all, group);
			int n = 0;
			for (const Tack& t : kTack)
				if (group == t.group)
				{
					const Hash item = GameUtil::Joaat(t.name);
					Ui::Do(sub, Ui::Id("horse.tack", t.name + std::strlen("HORSE_EQUIPMENT_")), std::format("{} {}", ++n, t.name + std::strlen("HORSE_EQUIPMENT_")), [item] { ApplyTack(item); });
				}
		}
		Ui::Section(stable, "Custom");
		Ui::Action(stable, "horse.addcomponent", "Add Component", []() -> std::string
		{
			std::string text;
			if (!GameUtil::PromptText("Tack item name or hash:", text) || text.empty())
				return "";
			const Hash item = GameUtil::ParseHash(text);
			if (!ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(item, 0))
				return "Not an item the game knows";
			ApplyTack(item);
			return "";
		})->SetHotkeyable(false);
		Ui::Action(stable, "horse.removecomponent", "Remove Component", []() -> std::string
		{
			std::string text;
			if (!GameUtil::PromptText("Category name or hash:", text) || text.empty())
				return "";
			RemoveTack(GameUtil::ParseHash(text));
			return "";
		})->SetHotkeyable(false);
		Ui::Do(stable, "horse.removeall", "Remove All", [] {
			for (const char* category : kTackCategories)
				RemoveTack(GameUtil::Joaat(category));
		});
	}
}

namespace Menus
{
	void BuildHorse(MenuBase* root)
	{
		MenuBase* horse = Ui::Submenu(root, "Horse");
		// Meta Ped Tags / Expressions below act on the horse.
		Target::Bind(horse, [] { return GameUtil::PlayerHorse(); });

		MenuBase* blip = Ui::Submenu(horse, "Blip");
		Ui::Action(blip, "horse.addblip", "Add Blip", AddHorseBlip);
		Ui::Action(blip, "horse.teleportto", "Teleport to", TeleportToBlipHorse);
		Ui::Action(blip, "horse.teleporttome", "Teleport to Me", BlipHorseToMe);
		Ui::ListMenu(horse, "Horse Loader", BuildLoader);
		Ui::Link(horse, "Meta Ped Tags", Shared().metaTags);
		Ui::Link(horse, "Meta Ped Expressions", Shared().metaExpressions);

		MenuBase* stats = Ui::Submenu(horse, "Horse Stats");
		Ui::Choice(stats, "horse.gender", "Gender", { "Female", "Male" }, &g_gender, [](int g) {
			OnMount([g](Ped m) {
				PED::_SET_CHAR_EXPRESSION(m, kGenderExpression, g == 1 ? 0.0f : 1.0f);
				PED::_UPDATE_PED_VARIATION(m, FALSE, TRUE, TRUE, TRUE, FALSE);
			});
		});
		Ui::Do(stats, "horse.maxhorsecores", "Max Horse Cores", [] {
			OnMount([](Ped m) {
				ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(m, 0, ATTRIBUTE::GET_MAX_ATTRIBUTE_RANK(m, 0));
				ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(m, 1, ATTRIBUTE::GET_MAX_ATTRIBUTE_RANK(m, 1));
			});
		});
		Ui::Action(stats, "horse.maxhorsebonding", "Max Horse Bonding", MaxBonding);
		Ui::Section(stats, "Custom");
		for (HorseStat& s : g_stats)
		{
			HorseStat* stat = &s;
			Ui::Number(stats, Ui::Id("horse.stat", s.name), s.name, &s.value, 0, 10, 1,
				[stat] { OnMount([stat](Ped m) { ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(m, stat->attribute, stat->value); }); }, true);
		}
		stats->SetOnOpen([](MenuBase*) {
			if (const Ped m = Mount())
			{
				g_gender = PED::_GET_CHAR_EXPRESSION(m, kGenderExpression) < 0.5f ? 1 : 0;
				for (HorseStat& s : g_stats)
					s.value = ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(m, s.attribute);
			}
		});
		BuildMobileStable(horse);

		Ui::Section(horse, "Toggles");
		Ui::Looped(horse, "horse.invincible", "Invincible", [] { g_invincible.Tick(SetInvincible); }, [] { g_invincible.Off(SetInvincible); });
		Ui::Looped(horse, "horse.invisible", "Invisible", [] { g_invisible.Tick(SetInvisible); }, [] { g_invisible.Off(SetInvisible); });
		Ui::Looped(horse, "horse.staminaneverdrain", "Stamina Never Drain", [] { OnMount([](Ped m) { PED::_RESTORE_PED_STAMINA(m, 100.0f); }); });
		Ui::Looped(horse, "horse.neverragdoll", "Never Ragdoll", [] { g_noRagdoll.Tick(SetNoRagdoll); }, [] { g_noRagdoll.Off(SetNoRagdoll); });
		Ui::Number(horse, "horse.horsescale", "Horse Scale", &g_scale, 0.1f, 10.0f, 0.05f, ApplyScale);
		Ui::Looped(horse, "horse.superspeed", "Super Speed", SuperSpeedTick, SuperSpeedOff);
		Ui::Number(horse, "horse.superspeedforce", "Super Speed Force", &g_superSpeed, 5.0f, 200.0f, 5.0f);
		Ui::Looped(horse, "horse.horseflymode", "Horse Fly Mode", FlyTick);
		Ui::Number(horse, "horse.flyspeed", "Fly Speed", &g_flySpeed, 0.1f, 10.0f, 0.1f);
		Ui::Looped(horse, "horse.horsecoresneverdrain", "Horse Cores Never Drain", [] { OnMount(FillCores); });
		Ui::Looped(horse, "horse.horsealwayscalm", "Horse Always Calm", [] { g_calm.Tick(SetCalm); }, [] { g_calm.Off(SetCalm); });
		Ui::Looped(horse, "horse.flaminghooves", "Flaming Hooves", [] { g_hooves.Tick(SetFlamingHooves); }, [] { g_hooves.Off(SetFlamingHooves); });
		Ui::Toggle(horse, "horse.mountcover", "Mount Cover", [](bool on) { PED::SET_PED_CONFIG_FLAG(Me(), 560, on); });
		Ui::Looped(horse, "horse.sidesaddleriding", "Side Saddle Riding", SideSaddleTick, SideSaddleOff);
		Ui::Looped(horse, "horse.alwaysclean", "Always Clean", [] { OnMount([](Ped m) { PED::CLEAR_PED_ENV_DIRT(m); }); });
		Ui::Looped(horse, "horse.horseteleportwhistle", "Horse Teleport Whistle", WhistleTick);

		Ui::Section(horse, "Actions");
		Ui::Do(horse, "horse.fillhorsecores", "Fill Horse Cores", [] { OnMount(FillHorseCores); });
		Ui::Action(horse, "horse.coresoverpower", "Cores Overpower", CoresOverpower);
		Ui::Do(horse, "horse.quickboost", "Quick Boost", [] { PLAYER::BOOST_PLAYER_HORSE_SPEED_FOR_TIME(PLAYER::PLAYER_ID(), 10000.0f, 10000); });
		Ui::Do(horse, "horse.quickstop", "Quick Stop", [] { OnMount([](Ped m) { TASK::TASK_HORSE_ACTION(m, 3, 0, 0); }); });
		Ui::Do(horse, "horse.heal", "Heal", [] { OnMount(HealHorse); });
		Ui::Do(horse, "horse.clean", "Clean", [] { OnMount([](Ped m) { PED::CLEAR_PED_ENV_DIRT(m); }); });
		Ui::Do(horse, "horse.clone", "Clone", [] { OnMount([](Ped m) { PED::CLONE_PED(m, TRUE, TRUE, TRUE); }); });
		Ui::Action(horse, "horse.rename", "Rename", Rename)->SetHotkeyable(false);
		Ui::Do(horse, "horse.ragdoll", "Ragdoll", Ragdoll);
		Ui::Action(horse, "horse.setasprimaryhorse", "Set As Primary Horse", SetAsPrimary);
		Ui::Do(horse, "horse.removepelts", "Remove Pelts", [] { OnMount(RemovePelts); });
		Ui::Do(horse, "horse.kill", "Kill", [] { OnMount([](Ped m) { ENTITY::SET_ENTITY_HEALTH(m, 0, 0); }); });
		Ui::Do(horse, "horse.delete", "Delete", [] { OnMount(DeleteHorse); });
		Ui::Action(horse, "horse.teleporthorsetome", "Teleport Horse to Me", TeleportHorseToMe);
		Ui::Action(horse, "horse.teleporttohorse", "Teleport to Horse", TeleportToHorse);
		Ui::Action(horse, "horse.playshittinganimation", "Play Shitting Animation", [] { return PlayAnim("creatures_mammal@horse@normal@idle@idle_variation@shitting", "idle_transition"); });
		Ui::Action(horse, "horse.playinjuredanimation", "Play Injured Animation", [] { return PlayAnim("creatures_mammal@horse@injured_critical@canter@slope@right", "stop_l"); });
		Ui::Action(horse, "horse.revivehorse", "Revive Horse", Revive);
		Ui::Do(horse, "horse.viewhorsecargo", "View Horse Cargo", [] { UIAPPS::LAUNCH_UIAPP_BY_HASH(0xFFC21415); });
	}
}
