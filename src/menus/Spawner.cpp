/*
	Spawner menu: ports Rampage's Submenus::SubPedSpawner, SubPedSpawnerSettings,
	SubPedSpawnerDatabase, SubPedSpawnerPeds/PedsList, SubPedSpawnerHorses,
	SubPedSpawnerAnimal, SubPedSpawnerFish, SubPedSpawnerAddon,
	SubPedSpawnerDispatch and SubVehicleSpawner. Model lists come from the
	game scripts (tools/extract_models.py, tools/extract_vehicles.py);
	addon peds from a user-supplied Rampagio_AddonPeds.txt (same format as
	Rampage's Lists\AddonPeds.txt).

	Each dispatch response first sets the law region Rampage's table ties
	it to (data/LawDispatchRegions.inc).

	The legendary animals and fish are Rampage's table (model plus outfit
	preset, data/LegendaryAnimals.inc).

	The vehicle JSON Loader reads Rampage's vehicle files (RampageFiles\Vehicle)
	from a Rampagio_Vehicles folder next to Rampagio.json.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <map>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	struct PedModel
	{
		const char* group;
		const char* name;
	};
	const PedModel kPedModels[] = {
#include "..\data\PedModels.inc"
	};
	const char* const kHorseModels[] = {
#include "..\data\HorseModels.inc"
	};
	const char* const kAnimalModels[] = {
#include "..\data\AnimalModels.inc"
	};
	const char* const kVehicleModels[] = {
#include "..\data\VehicleModels.inc"
	};
	const char* const kLawResponses[] = {
#include "..\data\LawResponses.inc"
	};
	// Rampage's response table: the law region (and state) each response
	// belongs to, set before dispatching it. 0 means none.
	// Rampage's legendary animals and fish: a model plus the outfit preset
	// that makes it the legendary.
	struct Legendary { const char* kind; const char* label; const char* model; int preset; };
	const Legendary kLegendaries[] = {
#include "..\data\LegendaryAnimals.inc"
	};
	struct LawDispatchRegion { const char* response; Hash region; Hash state; };
	const LawDispatchRegion kLawDispatchRegions[] = {
#include "..\data\LawDispatchRegions.inc"
	};

	constexpr Hash REL_COMPANION_GROUP = 0xB5A1D680;
	constexpr Hash REL_PLAYER_ENEMY = 0x4BAD542C;
	constexpr Hash REL_NO_RELATIONSHIP = 0x252FF97D;
	constexpr Hash FIRING_PATTERN_FULL_AUTO = 0xC6EE6B4C;
	constexpr Hash BLIP_STYLE_COMPANION = 0x19365607;
	constexpr Hash BLIP_STYLE_ENEMY = 0x318C617C;

	// --- settings ---------------------------------------------------------

	bool g_setOnHorse = false;
	bool g_spawnDead = false;
	bool g_spawnSedated = false;
	bool g_asBodyguard = false;
	bool g_asEnemy = false;
	bool g_frozen = false;
	bool g_invincible = false;
	bool g_randomOutfit = false;
	bool g_autoDespawn = false;
	bool g_blockHonor = false;
	bool g_interactable = false;
	bool g_bypassRelationship = false;
	bool g_useScale = false;
	float g_scale = 1.0f;
	bool g_useHealth = false;
	int g_health = 100;
	int g_horseGender = 0; // 0 female, 1 male

	// --- the spawned-ped database --------------------------------------------

	struct Spawned
	{
		Ped ped;
		std::string model;
	};
	std::vector<Spawned> g_spawned;

	void Track(Ped ped, const std::string& model)
	{
		if (ENTITY::DOES_ENTITY_EXIST(ped))
			g_spawned.push_back({ ped, model });
	}

	void MakeBodyguard(Ped ped)
	{
		const int group = PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID());
		PED::SET_PED_COMBAT_ABILITY(ped, 2);
		PED::SET_PED_COMBAT_MOVEMENT(ped, 2);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 17, FALSE);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 113, TRUE);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 46, TRUE);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 58, TRUE);
		PED::SET_PED_ACCURACY(ped, 100);
		PED::SET_PED_FIRING_PATTERN(ped, FIRING_PATTERN_FULL_AUTO);
		PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, REL_COMPANION_GROUP);
		PED::SET_PED_AS_GROUP_LEADER(Me(), group, TRUE);
		PED::SET_PED_CONFIG_FLAG(ped, 279, TRUE);
		PED::SET_GROUP_SEPARATION_RANGE(group, 400.0f);
		PED::SET_GROUP_FORMATION_SPACING(group, 1.5f, -1.0f, -1.0f);
		PED::SET_PED_CAN_PLAY_AMBIENT_ANIMS(ped, TRUE);
		PED::SET_PED_CAN_TELEPORT_TO_GROUP_LEADER(ped, group, TRUE);
		PED::SET_PED_AS_GROUP_MEMBER(ped, group);
		MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_COMPANION, ped);
	}

	void MakeEnemy(Ped ped)
	{
		const Ped me = Me();
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 17, FALSE);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, TRUE);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 46, TRUE);
		PED::SET_PED_COMBAT_ABILITY(ped, 1);
		PED::SET_PED_COMBAT_MOVEMENT(ped, 2);
		PED::SET_PED_FIRING_PATTERN(ped, FIRING_PATTERN_FULL_AUTO);
		PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, REL_PLAYER_ENEMY);
		TASK::TASK_COMBAT_PED(ped, me, 0, 16);
		PED::SET_PED_KEEP_TASK(ped, TRUE);
		MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_ENEMY, ped);
	}

	// Lets the player lock on and interact (greet, antagonize, ...).
	void MakeInteractable(Ped ped)
	{
		MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(ped);
		PED::SET_PED_CONFIG_FLAG(ped, 178, TRUE);
		for (int flag : { 315, 331, 130, 301 })
			PED::SET_PED_CONFIG_FLAG(ped, flag, FALSE);
		MISC::REGISTER_INTERACTION_LOCKON_PROMPT(ped, "INTERACT_LOCKON", 7.0f, 0.0f, 0, 0.0f, 0.0f, 0, FALSE, -1);
	}

	// outfitPreset < 0 keeps the random outfit.
	std::string SpawnPed(const std::string& name, int outfitPreset = -1)
	{
		const Hash model = GameUtil::ParseHash(name);
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model))
			return std::format("Model {} is invalid", name);
		if (!GameUtil::LoadModel(model))
			return "Failed to Load";
		const Ped me = Me();
		const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(me, 0.0f, 5.0f, 0.0f);
		Ped ped = PED::CREATE_PED(model, p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(me), FALSE, FALSE, FALSE, FALSE);
		PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
		if (outfitPreset >= 0)
			PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, outfitPreset, FALSE);
		if (g_blockHonor)
		{
			PED::SET_PED_CONFIG_FLAG(ped, 6, TRUE);
			DECORATOR::DECOR_SET_INT(ped, "honor_override", 0);
		}
		if (g_bypassRelationship)
		{
			PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, REL_NO_RELATIONSHIP);
			PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(ped, PLAYER::PLAYER_ID(), TRUE);
		}
		if (g_useHealth)
			ENTITY::SET_ENTITY_HEALTH(ped, g_health, 0);
		if (g_spawnDead)
			ENTITY::SET_ENTITY_HEALTH(ped, 0, 0);
		if (g_asBodyguard)
			MakeBodyguard(ped);
		if (g_asEnemy)
			MakeEnemy(ped);
		if (g_frozen)
			ENTITY::FREEZE_ENTITY_POSITION(ped, TRUE);
		if (g_invincible)
			ENTITY::SET_ENTITY_INVINCIBLE(ped, TRUE);
		if (g_randomOutfit)
		{
			const int count = PED::GET_NUM_META_PED_OUTFITS(ped);
			if (count > 0)
				PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, MISC::GET_RANDOM_INT_IN_RANGE(0, count), FALSE);
		}
		if (g_spawnSedated && ENTITY::GET_IS_ANIMAL(ped))
			PED::SET_PED_CONFIG_FLAG(ped, 580, TRUE);
		if (g_useScale)
			PED::_SET_PED_SCALE(ped, g_scale);
		if (g_interactable)
			MakeInteractable(ped);
		Track(ped, name);
		if (g_autoDespawn)
			ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return {};
	}

	constexpr Hash kSaddle = 0x4B96E611; // the meta outfit Rampage equips before mounting

	std::string SpawnHorse(const std::string& name)
	{
		const Hash model = GameUtil::ParseHash(name);
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model))
			return std::format("Model {} is invalid", name);
		if (!GameUtil::LoadModel(model))
			return "Failed to Load";
		const Ped me = Me();
		const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(me, 0.0f, 5.0f, 0.0f);
		const Ped horse = PED::CREATE_PED(model, p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(me), FALSE, FALSE, FALSE, FALSE);
		PED::_SET_RANDOM_OUTFIT_VARIATION(horse, TRUE);
		PED::_SET_CHAR_EXPRESSION(horse, 0xA28B, g_horseGender == 1 ? 1.0f : 0.0f); // horse gender
		PED::SET_PED_CONFIG_FLAG(horse, 6, TRUE);
		DECORATOR::DECOR_SET_BOOL(horse, "bHorseHasBeenStolen", FALSE);
		DECORATOR::DECOR_SET_INT(horse, "honor_block", -1);
		if (g_setOnHorse)
		{
			PED::_EQUIP_META_PED_OUTFIT(horse, kSaddle);
			PED::SET_PED_ONTO_MOUNT(me, horse, -1, TRUE);
		}
		PED::_UPDATE_PED_VARIATION(horse, FALSE, TRUE, TRUE, TRUE, FALSE);
		Track(horse, name);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return {};
	}

	bool PedAvailable(const char* name)
	{
		const Hash h = GameUtil::Joaat(name);
		return STREAMING::IS_MODEL_IN_CDIMAGE(h) && STREAMING::IS_MODEL_A_PED(h);
	}

	void AddPedRows(MenuBase* m, std::span<const char* const> names, bool horses = false)
	{
		for (const char* name : names)
		{
			if (!PedAvailable(name))
				continue;
			const std::string n = name;
			if (horses)
				Ui::Action(m, n, [n] { return SpawnHorse(n); });
			else
				Ui::Action(m, n, [n] { return SpawnPed(n); });
		}
	}

	void AddLegendaryRows(MenuBase* m, std::string_view kind)
	{
		for (const Legendary& l : kLegendaries)
		{
			if (kind != l.kind || !PedAvailable(l.model))
				continue;
			const std::string model = l.model;
			const int preset = l.preset;
			Ui::Action(m, l.label, [model, preset] { return SpawnPed(model, preset); });
		}
	}

	template <typename Pred>
	std::vector<const char*> Animals(Pred pred)
	{
		std::vector<const char*> out;
		for (const char* n : kAnimalModels)
			if (pred(std::string_view(n)))
				out.push_back(n);
		return out;
	}

	std::string Random(std::span<const char* const> names, bool horse)
	{
		std::vector<const char*> valid;
		for (const char* n : names)
			if (PedAvailable(n))
				valid.push_back(n);
		if (valid.empty())
			return "Nothing to spawn";
		const std::string n = valid[MISC::GET_RANDOM_INT_IN_RANGE(0, static_cast<int>(valid.size()))];
		return horse ? SpawnHorse(n) : SpawnPed(n);
	}

	// "Take control over an existing ped": the first ped of that model in
	// the world joins the database, as Rampage's Hijack does.
	std::string HijackPed()
	{
		std::string name;
		if (!GameUtil::PromptText("Enter Name or Hash:", name) || name.empty())
			return {};
		const Hash model = GameUtil::ParseHash(name);
		for (Ped p : GameUtil::AllPeds())
			if (p != Me() && ENTITY::GET_ENTITY_MODEL(p) == model)
			{
				if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(p))
					ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, TRUE, TRUE);
				Track(p, name);
				return "Added to the database";
			}
		return "No ped of that model";
	}

	void DeletePed(Ped p)
	{
		if (!ENTITY::DOES_ENTITY_EXIST(p))
			return;
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(p))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, TRUE, TRUE);
		PED::DELETE_PED(&p);
	}

	// Rampage's database choice: clear the list, delete all, delete dead,
	// teleport all to me.
	int g_databaseAction = 0;

	void RunDatabaseAction()
	{
		switch (g_databaseAction)
		{
		case 0:
			g_spawned.clear();
			break;
		case 1:
			for (auto& s : g_spawned)
				DeletePed(s.ped);
			g_spawned.clear();
			break;
		case 2:
			std::erase_if(g_spawned, [](Spawned& s) {
				if (ENTITY::DOES_ENTITY_EXIST(s.ped) && !ENTITY::IS_ENTITY_DEAD(s.ped))
					return false;
				DeletePed(s.ped);
				return true;
			});
			break;
		case 3:
		{
			const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), FALSE, FALSE);
			for (auto& s : g_spawned)
				if (ENTITY::DOES_ENTITY_EXIST(s.ped) && !ENTITY::IS_ENTITY_DEAD(s.ped))
					ENTITY::SET_ENTITY_COORDS(s.ped, me.x, me.y, me.z, FALSE, FALSE, FALSE, FALSE);
			break;
		}
		}
		Ui::Controller().ReopenActiveLater();
	}

	MenuBase* g_pedMenu = nullptr;
	Ped g_selectedPed = 0;

	void BuildSelectedPed(MenuBase* m)
	{
		const Ped p = g_selectedPed;
		if (!ENTITY::DOES_ENTITY_EXIST(p))
		{
			m->AddItem(new MenuItemLabel([] { return std::string("Ped no longer exists"); }));
			return;
		}
		Ui::Do(m, "Ped Editor", [p] { Menus::PedEditor::Open(p); });
		Ui::Do(m, "Teleport to Ped", [p] {
			const Vector3 v = ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE);
			ENTITY::SET_ENTITY_COORDS(Me(), v.x, v.y, v.z, FALSE, FALSE, FALSE, FALSE);
		});
		Ui::Do(m, "Teleport Ped to Me", [p] {
			const Vector3 v = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
			ENTITY::SET_ENTITY_COORDS(p, v.x, v.y, v.z, FALSE, FALSE, FALSE, FALSE);
		});
		Ui::Do(m, "Add to Posse", [p] { Menus::Posse::Add(p); });
		Ui::Do(m, "Make Bodyguard", [p] { MakeBodyguard(p); });
		Ui::Do(m, "Make Enemy", [p] { MakeEnemy(p); });
		Ui::Do(m, "Revive", [p] { PED::RESURRECT_PED(p); PED::REVIVE_INJURED_PED(p); });
		Ui::Do(m, "Kill", [p] { ENTITY::SET_ENTITY_HEALTH(p, 0, 0); });
		Ui::Do(m, "Delete", [p] {
			DeletePed(p);
			std::erase_if(g_spawned, [p](Spawned& s) { return s.ped == p; });
		});
	}

	void BuildDatabase(MenuBase* m)
	{
		Ui::Choice(m, "Action", { "Clear List", "Delete All", "Delete Dead", "Teleport All to Me" }, &g_databaseAction);
		Ui::Do(m, "Run Action", RunDatabaseAction);
		int i = 0;
		for (const Spawned& s : g_spawned)
		{
			const Ped p = s.ped;
			const bool alive = ENTITY::DOES_ENTITY_EXIST(p) && !ENTITY::IS_ENTITY_DEAD(p);
			m->AddItem(new MenuItemAction(std::format("{} {}{}", ++i, s.model, alive ? "" : " (dead)"), [p] {
				g_selectedPed = p;
				Ui::Push(g_pedMenu);
			}));
		}
	}

	// --- law dispatch ------------------------------------------------------------

	float g_dispatchMultiplier = 1.0f;

	void Dispatch(Hash response, Hash region, Hash state)
	{
		const Player me = PLAYER::PLAYER_ID();
		if (region != 0)
			LAW::_SET_LAW_REGION(me, region, state);
		LAW::_REPORT_PLAYER_LAW_DISPATCH_RESPONSE_OVERRIDE(me, response);
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		LAW::_CREATE_LAW_DISPATCH_RESPONSE_FOR_COORDS(p.x, p.y, p.z, response);
		LAW::_0xBD944A3D36E992DE();
		PLAYER::REPORT_POLICE_SPOTTED_PLAYER(me);
		LAW::_SET_CANT_LOSE_LAW_THIS_RESPONSE(TRUE);
		LAW::_FORCE_LAW_ON_LOCAL_PLAYER_IMMEDIATELY();
	}

	// --- vehicle spawner -------------------------------------------------------------

	bool g_spawnInVehicle = true;
	bool g_deletePrevious = false;
	bool g_vehicleInvincible = false;
	Vehicle g_lastSpawned = 0;

	std::string SpawnVehicle(const std::string& name)
	{
		const Hash model = GameUtil::ParseHash(name);
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_A_VEHICLE(model))
			return "Model is invalid.";
		if (!GameUtil::LoadModel(model))
			return "Failed to Load";
		if (g_deletePrevious && g_lastSpawned && ENTITY::DOES_ENTITY_EXIST(g_lastSpawned))
			VEHICLE::DELETE_VEHICLE(&g_lastSpawned);
		const Ped me = Me();
		const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(me, 0.0f, 5.0f, 0.0f);
		const Vehicle v = VEHICLE::CREATE_VEHICLE(model, p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(me), FALSE, FALSE, FALSE, FALSE);
		VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(v, FALSE);
		if (g_vehicleInvincible)
			ENTITY::SET_ENTITY_INVINCIBLE(v, TRUE);
		if (g_spawnInVehicle)
			PED::SET_PED_INTO_VEHICLE(me, v, -1);
		g_lastSpawned = v;
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return {};
	}

	// --- vehicle JSON loader ----------------------------------------------------------
	// Rampage's format: { "vehicle": { "model", "livery", "liveryIdx", "tint",
	// "tintId", ... }, "item<N>": { "model", "attachmentOffsetX/Y/Z",
	// "rotationX/Y/Z", "boneIdx", "lockRotation" }, ... }; the items are
	// objects attached to the vehicle.

	constexpr wchar_t kVehicleFolder[] = L"Rampagio_Vehicles";

	std::string LoadVehicleJson(const std::string& name)
	{
		const nlohmann::json file = DataFile::LoadJson(std::wstring(kVehicleFolder) + L"\\" + std::wstring(name.begin(), name.end()) + L".json");
		const auto vehicle = file.find("vehicle");
		if (vehicle == file.end() || !vehicle->is_object())
			return std::format("{}.json has no vehicle", name);
		const std::string error = SpawnVehicle(vehicle->value("model", std::string()));
		if (!error.empty())
			return error;
		const Vehicle v = g_lastSpawned;
		DECORATOR::DECOR_SET_BOOL(v, "wagon_block_honor", TRUE);
		VEHICLE::SET_VEHICLE_INFLUENCES_WANTED_LEVEL(v, FALSE);
		if (vehicle->value("livery", false))
			VEHICLE::_SET_VEHICLE_LIVERY(v, vehicle->value("liveryIdx", 0));
		if (vehicle->value("tint", false))
			VEHICLE::_SET_VEHICLE_TINT(v, vehicle->value("tintId", 0));
		const Vector3 at = ENTITY::GET_ENTITY_COORDS(v, TRUE, FALSE);
		int attached = 0;
		for (const auto& [key, item] : file.items())
		{
			if (key.find("item") == std::string::npos || !item.is_object())
				continue;
			const Hash model = GameUtil::ParseHash(item.value("model", std::string()));
			if (!GameUtil::LoadModel(model))
				continue;
			const Object o = OBJECT::CREATE_OBJECT_NO_OFFSET(model, at.x, at.y, at.z - 500.0f, TRUE, TRUE, FALSE, FALSE);
			ENTITY::ATTACH_ENTITY_TO_ENTITY(o, v, item.value("boneIdx", 0),
				item.value("attachmentOffsetX", 0.0f), item.value("attachmentOffsetY", 0.0f), item.value("attachmentOffsetZ", 0.0f),
				item.value("rotationX", 0.0f), item.value("rotationY", 0.0f), item.value("rotationZ", 0.0f),
				FALSE, FALSE, TRUE, FALSE, 2, item.value("lockRotation", false), FALSE, FALSE);
			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			attached++;
		}
		return std::format("Loaded {} with {} entities", name, attached);
	}

	void BuildVehicleJson(MenuBase* m)
	{
		for (const std::string& name : DataFile::ListFiles(kVehicleFolder, L".json"))
			Ui::Action(m, name, [name] {
				try
				{
					return LoadVehicleJson(name);
				}
				catch (const nlohmann::json::exception& e)
				{
					return std::format("{}.json: {}", name, e.what());
				}
			});
		if (m->GetItemCount() == 0)
			m->AddItem(new MenuItemLabel([] { return std::string("No files in Rampagio_Vehicles"); }));
	}

	std::string HijackVehicle()
	{
		std::string name;
		if (!GameUtil::PromptText("Enter Name or Hash:", name) || name.empty())
			return {};
		const Hash model = GameUtil::ParseHash(name);
		for (Vehicle v : GameUtil::AllVehicles())
			if (ENTITY::GET_ENTITY_MODEL(v) == model)
			{
				if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(v))
					ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, TRUE, TRUE);
				PED::SET_PED_INTO_VEHICLE(Me(), v, -1);
				g_lastSpawned = v;
				return {};
			}
		return "No vehicle of that model";
	}

	enum class VehicleKind { Train, Wagon, Boat, Cannon, Other };

	VehicleKind KindOf(const char* name)
	{
		const Hash h = GameUtil::Joaat(name);
		if (VEHICLE::IS_THIS_MODEL_A_TRAIN(h))
			return VehicleKind::Train;
		if (VEHICLE::IS_THIS_MODEL_A_BOAT(h))
			return VehicleKind::Boat;
		const std::string_view n(name);
		if (n.find("cannon") != n.npos || n.find("gatling") != n.npos)
			return VehicleKind::Cannon;
		if (VEHICLE::_IS_THIS_MODEL_A_DRAFT_VEHICLE(h))
			return VehicleKind::Wagon;
		return VehicleKind::Other;
	}

	void VehicleList(MenuBase* parent, const char* title, VehicleKind kind)
	{
		Ui::ListMenu(parent, title, [kind](MenuBase* m) {
			for (const char* name : kVehicleModels)
			{
				const Hash h = GameUtil::Joaat(name);
				if (!STREAMING::IS_MODEL_IN_CDIMAGE(h) || !STREAMING::IS_MODEL_A_VEHICLE(h) || KindOf(name) != kind)
					continue;
				const std::string n = name;
				Ui::Action(m, n, [n] { return SpawnVehicle(n); });
			}
		});
	}

	std::string Lower(std::string s)
	{
		for (auto& c : s)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return s;
	}
}

namespace Menus
{
	void BuildSpawner(MenuBase* root)
	{
		MenuBase* spawner = Ui::Submenu(root, "Spawner");

		// SubPedSpawner.
		MenuBase* peds = Ui::Submenu(spawner, "Ped Spawner");
		MenuBase* settings = Ui::Submenu(peds, "Spawner Settings");
		struct Flag { const char* name; bool* value; };
		const Flag kFlags[] = { { "Set Player On Horse", &g_setOnHorse }, { "Spawn Dead", &g_spawnDead },
			{ "Spawn Sedated", &g_spawnSedated }, { "Spawn As Bodyguard", &g_asBodyguard }, { "Spawn As Enemy", &g_asEnemy },
			{ "Spawn Frozen", &g_frozen }, { "Spawn Invincible", &g_invincible }, { "Spawn with Random Outfit", &g_randomOutfit },
			{ "Auto Despawn", &g_autoDespawn }, { "Block Witness and Honor", &g_blockHonor },
			{ "Spawn Interactable", &g_interactable }, { "Bypass Relationship", &g_bypassRelationship } };
		for (const Flag& f : kFlags)
		{
			bool* value = f.value;
			Ui::Toggle(settings, Ui::Id("spawner.ped", f.name), f.name, [value](bool on) { *value = on; })->SetDefault(*value);
		}
		Ui::Section(settings, "Values");
		Ui::Toggle(settings, "spawner.scale", "Scale", [](bool on) { g_useScale = on; });
		Ui::Number(settings, "spawner.scalevalue", "Scale Value", &g_scale, 0.01f, 5.0f, 0.01f);
		Ui::Toggle(settings, "spawner.health", "Health", [](bool on) { g_useHealth = on; });
		Ui::Number(settings, "spawner.healthvalue", "Health Value", &g_health, 1, 10000, 10);

		g_pedMenu = Ui::DetachedListMenu("Spawned Ped", BuildSelectedPed);
		Ui::ListMenu(peds, "Ped Database", BuildDatabase);

		Ui::Section(peds, "Models");
		MenuBase* humans = Ui::Submenu(peds, "Humans");
		std::map<std::string, std::vector<const char*>> groups;
		for (const PedModel& p : kPedModels)
			groups[p.group].push_back(p.name);
		for (auto& [group, names] : groups)
			Ui::ListMenu(humans, group, [names](MenuBase* m) { AddPedRows(m, names); });

		Ui::ListMenu(peds, "Horses", [](MenuBase* m) {
			Ui::Choice(m, "Gender", { "Female", "Male" }, &g_horseGender);
			Ui::Action(m, "Random Horse", [] { return Random(kHorseModels, true); });
			AddPedRows(m, kHorseModels, true);
		});
		Ui::ListMenu(peds, "Animals", [](MenuBase* m) {
			Ui::Action(m, "Random Animal", [] { return Random(kAnimalModels, false); });
			Ui::Section(m, "Legendary Animals");
			AddLegendaryRows(m, "Animal");
			Ui::Section(m, "Dogs");
			AddPedRows(m, Animals([](std::string_view n) { return n.starts_with("a_c_dog"); }));
			Ui::Section(m, "All Animals");
			AddPedRows(m, Animals([](std::string_view n) { return !n.starts_with("a_c_fish"); }));
		});
		Ui::ListMenu(peds, "Fishes", [](MenuBase* m) {
			const auto fish = Animals([](std::string_view n) { return n.starts_with("a_c_fish"); });
			Ui::Action(m, "Random Fish", [fish] { return Random(fish, false); });
			Ui::Section(m, "Legendary Fish");
			AddLegendaryRows(m, "Fish");
			Ui::Section(m, "All Fish");
			AddPedRows(m, fish);
		});
		Ui::ListMenu(peds, "Addon Peds", [](MenuBase* m) {
			for (const std::string& line : DataFile::LoadLines(L"Rampagio_AddonPeds.txt"))
				Ui::Action(m, line, [line] { return SpawnPed(line); });
			if (m->GetItemCount() == 0)
				m->AddItem(new MenuItemLabel([] { return std::string("No Rampagio_AddonPeds.txt"); }));
		});
		Ui::ListMenu(peds, "Search Peds", [](MenuBase* m) {
			std::string text;
			if (!GameUtil::PromptText("Search:", text) || text.empty())
				return;
			text = Lower(text);
			std::vector<const char*> found;
			for (const PedModel& p : kPedModels)
				if (std::string_view(p.name).find(text) != std::string_view::npos)
					found.push_back(p.name);
			for (const char* n : kAnimalModels)
				if (std::string_view(n).find(text) != std::string_view::npos)
					found.push_back(n);
			AddPedRows(m, found);
			std::vector<const char*> horses;
			for (const char* n : kHorseModels)
				if (std::string_view(n).find(text) != std::string_view::npos)
					horses.push_back(n);
			AddPedRows(m, horses, true);
		});
		Ui::Action(peds, "spawner.pedspawner.custominput", "Custom Input", [] {
			std::string name;
			if (!GameUtil::PromptText("Enter Name or Hash:", name) || name.empty())
				return std::string();
			return SpawnPed(name);
		})->SetHotkeyable(false);
		Ui::Action(peds, "spawner.hijackped", "Hijack Ped", HijackPed)->SetHotkeyable(false);

		// SubPedSpawnerDispatch.
		MenuBase* dispatch = Ui::Submenu(spawner, "Law Dispatch Spawner");
		Ui::Number(dispatch, "spawner.dispatchmultiplier", "Dispatch Multiplier", &g_dispatchMultiplier, 0.1f, 10.0f, 0.1f,
			[] { LAW::_SET_DISPATCH_MULTIPLIER_OVERRIDE(g_dispatchMultiplier); });
		Ui::Section(dispatch, "Responses");
		// The scripts' responses, then the ones only Rampage's table has.
		std::vector<std::string_view> responses(std::begin(kLawResponses), std::end(kLawResponses));
		for (const LawDispatchRegion& entry : kLawDispatchRegions)
			if (std::find(responses.begin(), responses.end(), entry.response) == responses.end())
				responses.push_back(entry.response);
		for (std::string_view response : responses)
		{
			Hash region = 0, state = 0;
			for (const LawDispatchRegion& entry : kLawDispatchRegions)
				if (response == entry.response)
					region = entry.region, state = entry.state;
			const std::string name(response);
			const Hash h = GameUtil::Joaat(name);
			Ui::Do(dispatch, Ui::Id("spawner.dispatch", name), name, [h, region, state] { Dispatch(h, region, state); });
		}

		// SubVehicleSpawner. The settings are ours (Rampage's are not in its
		// menu inventory).
		MenuBase* vehicles = Ui::Submenu(spawner, "Vehicle Spawner");
		MenuBase* vsettings = Ui::Submenu(vehicles, "Spawner Settings");
		Ui::Toggle(vsettings, "spawner.spawninvehicle", "Spawn In Vehicle", [](bool on) { g_spawnInVehicle = on; })->SetDefault(g_spawnInVehicle);
		Ui::Toggle(vsettings, "spawner.deleteprevious", "Delete Previous", [](bool on) { g_deletePrevious = on; });
		Ui::Toggle(vsettings, "spawner.spawninvincible", "Spawn Invincible", [](bool on) { g_vehicleInvincible = on; });
		Ui::Action(vehicles, "spawner.vehiclespawner.custominput", "Custom Input", [] {
			std::string name;
			if (!GameUtil::PromptText("Enter Name or Hash:", name) || name.empty())
				return std::string();
			return SpawnVehicle(name);
		})->SetHotkeyable(false);
		Ui::Action(vehicles, "spawner.hijackvehicle", "Hijack Vehicle", HijackVehicle)->SetHotkeyable(false);
		VehicleList(vehicles, "Train Spawner", VehicleKind::Train);
		VehicleList(vehicles, "Wagon Spawner", VehicleKind::Wagon);
		VehicleList(vehicles, "Boat Spawner", VehicleKind::Boat);
		VehicleList(vehicles, "Cannon Spawner", VehicleKind::Cannon);
		VehicleList(vehicles, "Other Vehicles", VehicleKind::Other);
		Ui::ListMenu(vehicles, "JSON Loader", BuildVehicleJson);

		BuildObjectSpawner(spawner);
	}
}
