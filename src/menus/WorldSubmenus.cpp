/*
	World submenus: ports Rampage's Submenus::SubWorldOcean ("Water"), the
	Cloud Editor and Ambient Light builders (unnamed in Rampage, switch
	cases 149 and 157), SubWorldLocalPeds/Vehicles/Objects (the managers),
	SubWorldDoorManager, SubWorldTornado, SubWorldIMAPLoader and
	SubWorldStates, plus SubWorld's Door Unlocker row.

	IPL and door lists come from the game scripts (tools/extract_world.py);
	Rampage's own map-set and door tables are not copied. World states
	are the bitset Global_40.f_283 (31 bits per word), set the way
	medium_update's func_1321 does, by id; the game has no names for them.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"
#include "..\LogFallback.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	struct Ipl
	{
		Hash hash;
		const char* name;
	};
	const Ipl kIpls[] = {
#include "..\data\Ipls.inc"
	};
	const Hash kDoors[] = {
#include "..\data\Doors.inc"
	};

	constexpr Hash WEAPON_EXPLOSIVE = 0xE1D2B317; // EXPLODE_PED_HEAD's weapon in Rampage
	constexpr int EXP_TAG_DYNAMITE_VOLATILE = 22;

	std::vector<Ped> OtherPeds()
	{
		std::vector<Ped> out;
		const Ped me = Me();
		for (Ped p : GameUtil::AllPeds())
			if (p != me && !PED::IS_PED_A_PLAYER(p))
				out.push_back(p);
		return out;
	}

	// Every vehicle except the one the player is in.
	std::vector<Vehicle> OtherVehicles()
	{
		std::vector<Vehicle> out;
		const Vehicle mine = PED::GET_VEHICLE_PED_IS_IN(Me(), TRUE);
		for (Vehicle v : GameUtil::AllVehicles())
			if (v != mine)
				out.push_back(v);
		return out;
	}

	void TakeControl(Entity e)
	{
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(e))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, TRUE, TRUE);
	}

	// --- water ---------------------------------------------------------------

	void SetGuarmaHurricane(bool on)
	{
		if (on)
		{
			MISC::_SET_WEATHER_VARIATION("HURRICANE", "HURRICANE_guama");
			WATER::_SET_OCEAN_GUARMA_WATER_QUADRANT(16.95f, 50.04f, 1, 1.15f, 1.28f, -1.0f, 1.86f, 8.1f, TRUE);
		}
		else
		{
			MISC::_CLEAR_WEATHER_VARIATION("HURRICANE", FALSE);
			WATER::_SET_OCEAN_GUARMA_WATER_QUADRANT(0.0f, 50.04f, 1, 1.15f, 1.28f, -1.0f, 1.86f, 8.1f, TRUE);
			WATER::_RESET_GUARMA_WATER_STATE();
		}
	}

	void SetNoWaterModifier(bool on)
	{
		if (on)
			GRAPHICS::SET_TRANSITION_TIMECYCLE_MODIFIER("nowater", 0.0f);
		else
			GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
	}

	// --- clouds and ambient light ----------------------------------------------

	bool g_cloudLayer = false, g_cloudNoise = false, g_cloudPosition = false, g_cloudHeight = false;
	float g_layerX = 0.0f, g_layerY = 0.0f;
	float g_noise[3] = {};
	float g_cloudPos[3] = {};
	float g_height = 2.0f;
	int g_lapseAxis = 0; // X, Y
	float g_lapseSpeed = 0.005f;

	void CloudTick()
	{
		if (g_cloudLayer)
			GRAPHICS::_SET_CLOUD_LAYER(g_layerX, g_layerY, 1);
		if (g_cloudNoise)
			GRAPHICS::_SET_CLOUD_NOISE(g_noise[0], g_noise[1], g_noise[2]);
		if (g_cloudPosition)
			GRAPHICS::_SET_CLOUD_POSITION(g_cloudPos[0], g_cloudPos[1], g_cloudPos[2]);
		if (g_cloudHeight)
			GRAPHICS::_SET_CLOUD_HEIGHT(g_height);
	}

	// Cloudlapse: slides the cloud layer along one axis, wrapping at +-1.
	void CloudlapseTick()
	{
		float& v = g_lapseAxis == 0 ? g_layerX : g_layerY;
		v += g_lapseSpeed;
		if (v > 1.0f)
			v = -1.0f;
		GRAPHICS::_SET_CLOUD_LAYER(g_layerX, g_layerY, 1);
	}

	int g_lightColor[3] = { 255, 255, 255 };
	float g_lightRange = 20.0f;
	float g_lightBrightness = 5.0f;

	void AmbientLightTick()
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		GRAPHICS::DRAW_LIGHT_WITH_RANGE(p.x, p.y, p.z + 1.0f, g_lightColor[0], g_lightColor[1], g_lightColor[2], g_lightRange, g_lightBrightness);
	}

	// --- managers ----------------------------------------------------------------

	// A model/health label over every entity on screen within 150 m.
	template <typename Label>
	void ScannerTick(const std::vector<Entity>& entities, Label label)
	{
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		for (Entity e : entities)
		{
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
			if (GameUtil::DistanceSq(p, me) > 150.0f * 150.0f)
				continue;
			float x = 0.0f, y = 0.0f;
			if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(p.x, p.y, p.z + 1.0f, &x, &y))
				continue;
			DrawTextAt(x, y, label(e).c_str(), 18, { 255, 255, 255, 230 });
		}
	}

	std::string Hex(Hash h) { return std::format("0x{:08X}", h); }

	void PedScannerTick()
	{
		const auto peds = OtherPeds();
		ScannerTick(std::vector<Entity>(peds.begin(), peds.end()), [](Entity e) {
			const char* kind = PED::IS_PED_HUMAN(e) ? "Human" : PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(e)) ? "Horse" : "Animal";
			return std::format("{} {} HP {}", kind, Hex(ENTITY::GET_ENTITY_MODEL(e)), ENTITY::GET_ENTITY_HEALTH(e));
		});
	}

	void VehicleScannerTick()
	{
		const auto vehicles = OtherVehicles();
		ScannerTick(std::vector<Entity>(vehicles.begin(), vehicles.end()), [](Entity e) { return "Vehicle " + Hex(ENTITY::GET_ENTITY_MODEL(e)); });
	}

	void ObjectScannerTick()
	{
		const auto objects = GameUtil::AllObjects();
		ScannerTick(std::vector<Entity>(objects.begin(), objects.end()), [](Entity e) { return "Object " + Hex(ENTITY::GET_ENTITY_MODEL(e)); });
	}

	void ClearAreaAround(int flags)
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		MISC::CLEAR_AREA(p.x, p.y, p.z, 1000.0f, flags);
	}

	void ExplodeAt(Entity e, float shake)
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
		FIRE::ADD_EXPLOSION(p.x, p.y, p.z, EXP_TAG_DYNAMITE_VOLATILE, shake, FALSE, FALSE, 0.0f);
	}

	void ExplodePeds(bool enemiesOnly)
	{
		const Ped me = Me();
		for (Ped p : OtherPeds())
		{
			if (ENTITY::IS_ENTITY_DEAD(p) || (enemiesOnly && !PED::IS_PED_IN_COMBAT(p, me)))
				continue;
			ExplodeAt(p, enemiesOnly ? 1.0f : 3.0f);
			PED::EXPLODE_PED_HEAD(p, WEAPON_EXPLOSIVE);
		}
	}

	void RecruitBodyguard(Ped p)
	{
		if (!ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p) || !PED::IS_PED_HUMAN(p))
			return;
		TakeControl(p);
		Menus::Posse::Add(p);
	}

	// Rampage gives each a weapon from its own list; ours a repeater.
	constexpr Hash WEAPON_REPEATER_CARBINE = 0xF5175BA1;

	void HostilePeds()
	{
		const Ped me = Me();
		for (Ped p : OtherPeds())
		{
			if (ENTITY::IS_ENTITY_DEAD(p) || !PED::IS_PED_HUMAN(p))
				continue;
			WEAPON::GIVE_DELAYED_WEAPON_TO_PED(p, WEAPON_REPEATER_CARBINE, 400, TRUE, 0x2CD419DC);
			TASK::TASK_COMBAT_PED(p, me, 0, 16);
			PED::SET_PED_KEEP_TASK(p, TRUE);
		}
	}

	void DecreasePedsTick()
	{
		PED::SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		PED::_SET_SCENARIO_PED_RANGE_MULTIPLIER_THIS_FRAME(0.0f);
		PED::_SET_AMBIENT_PED_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		PED::_SET_AMBIENT_ANIMAL_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		PED::_SET_AMBIENT_HUMAN_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		PED::_SET_SCENARIO_HUMAN_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		PATH::SET_AMBIENT_PED_RANGE_MULTIPLIER_THIS_FRAME(0.0f);
	}

	void DecreaseVehiclesTick()
	{
		VEHICLE::SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		VEHICLE::SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
		VEHICLE::SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(0.0f);
	}

	// --- doors -----------------------------------------------------------------

	std::string UnlockDoor()
	{
		std::string text;
		if (!GameUtil::PromptText("Enter Door Hash", text) || text.empty())
			return {};
		OBJECT::DOOR_SYSTEM_SET_DOOR_STATE(GameUtil::ParseHash(text), 0);
		return {};
	}

	// DOOR_SYSTEM states: 0 unlocked, 1 locked.
	void BuildDoors(MenuBase* m)
	{
		Ui::Action(m, "Door Unlocker", UnlockDoor);
		int count = 0;
		for (Hash door : kDoors)
		{
			if (!OBJECT::IS_DOOR_REGISTERED_WITH_SYSTEM(door))
				continue;
			count++;
			Ui::Toggle(m, "Locked " + Hex(door), [door](bool on) { OBJECT::DOOR_SYSTEM_SET_DOOR_STATE(door, on ? 1 : 0); })
				->SetState(OBJECT::DOOR_SYSTEM_GET_DOOR_STATE(door) == 1);
		}
		if (!count)
			m->AddItem(new MenuItemLabel([] { return std::string("No known doors registered"); }));
	}

	// --- tornado, black hole, meteors ---------------------------------------------

	// The invisible anchor model Rampage uses for the tornado and black hole.
	constexpr Hash kAnchor = 0xEB319577;

	struct Vortex
	{
		Object anchor = 0;
		Vector3 center{};
	};
	Vortex g_tornado, g_blackHole;

	bool Place(Vortex& v)
	{
		if (v.anchor && ENTITY::DOES_ENTITY_EXIST(v.anchor))
			return true;
		if (!GameUtil::LoadModel(kAnchor))
			return false;
		const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(Me(), 0.0f, 5.0f, 0.0f);
		v.anchor = OBJECT::CREATE_OBJECT(kAnchor, p.x, p.y, p.z, TRUE, FALSE, FALSE, FALSE, FALSE);
		ENTITY::SET_ENTITY_VISIBLE(v.anchor, FALSE);
		ENTITY::FREEZE_ENTITY_POSITION(v.anchor, TRUE);
		v.center = ENTITY::GET_ENTITY_COORDS(v.anchor, FALSE, FALSE);
		return true;
	}

	void Remove(Vortex& v)
	{
		if (v.anchor && ENTITY::DOES_ENTITY_EXIST(v.anchor))
		{
			TakeControl(v.anchor);
			ENTITY::DELETE_ENTITY(&v.anchor);
		}
		v.anchor = 0;
	}

	float Jitter() { return static_cast<float>(MISC::GET_RANDOM_INT_IN_RANGE(-2, 2)); }

	bool g_attractVehicles = true, g_attractPeds = true, g_attractPlayer = false, g_attractObjects = true;
	bool g_deleteEntities = false;
	int g_holeForce = 1;     // Rampage's force choice
	int g_holeForceType = 0; // 0 low, 1 max (lags)

	std::vector<Entity> Attracted(bool includeObjects)
	{
		std::vector<Entity> out;
		const Ped me = Me();
		const Vehicle mine = PED::GET_VEHICLE_PED_IS_IN(me, FALSE);
		if (g_attractVehicles)
			for (Vehicle v : GameUtil::AllVehicles())
				if (v != mine || g_attractPlayer)
					out.push_back(v);
		if (g_attractPeds)
			for (Ped p : GameUtil::AllPeds())
				if (p != me || g_attractPlayer)
					out.push_back(p);
		if (includeObjects && g_attractObjects)
			for (Object o : GameUtil::AllObjects())
				out.push_back(o);
		return out;
	}

	// Pulls everything toward the centre with a sideways swirl.
	void TornadoTick()
	{
		if (!Place(g_tornado))
			return;
		for (Entity e : Attracted(false))
		{
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
			const float dx = g_tornado.center.x - p.x, dy = g_tornado.center.y - p.y;
			ENTITY::APPLY_FORCE_TO_ENTITY(e, 1, dx * 0.05f - dy * 0.1f + Jitter(), dy * 0.05f + dx * 0.1f + Jitter(), 0.5f,
				Jitter(), Jitter(), Jitter(), 0, FALSE, TRUE, TRUE, FALSE, TRUE);
		}
	}

	void BlackHoleTick()
	{
		if (!Place(g_blackHole))
			return;
		const Vector3& c = g_blackHole.center;
		GRAPHICS::_DRAW_MARKER(0x50638AB9, c.x, c.y, c.z, 0.0f, 0.0f, 0.0f, 180.0f, 0.0f, 0.0f, 5.0f, 5.0f, 5.0f, 0, 0, 0, 170,
			TRUE, TRUE, 2, TRUE, nullptr, nullptr, FALSE);
		const int forceType = g_holeForceType == 0 ? 1 : 3;
		for (Entity e : Attracted(true))
		{
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
			const float dx = c.x - p.x, dy = c.y - p.y, dz = c.z - p.z;
			if (g_deleteEntities && dx * dx + dy * dy + dz * dz < 9.0f && e != Me())
			{
				TakeControl(e);
				ENTITY::DELETE_ENTITY(&e);
				continue;
			}
			const float k = 0.05f * (g_holeForce + 1);
			ENTITY::APPLY_FORCE_TO_ENTITY(e, forceType, dx * k, dy * k, dz * k, Jitter(), Jitter(), Jitter(), 0, FALSE, TRUE, TRUE, FALSE, TRUE);
		}
	}

	// Meteor shower: drops random rocks from 220 m at night with an
	// explosion on impact (Rampage's numbers), under a frozen clear sky.
	const char* const kMeteorModels[] = { "p_rock01x", "p_rock02x", "p_boulder01x" };
	std::vector<Object> g_meteors;
	int g_lastMeteor = 0;

	void MeteorTick()
	{
		CLOCK::SET_CLOCK_TIME(0, 0, 0);
		for (auto it = g_meteors.begin(); it != g_meteors.end();)
		{
			Object o = *it;
			if (!ENTITY::DOES_ENTITY_EXIST(o) || ENTITY::HAS_ENTITY_COLLIDED_WITH_ANYTHING(o))
			{
				if (ENTITY::DOES_ENTITY_EXIST(o))
				{
					const Vector3 p = ENTITY::GET_ENTITY_COORDS(o, FALSE, FALSE);
					FIRE::ADD_EXPLOSION(p.x, p.y, p.z, 17, 1.0f, TRUE, FALSE, 1.0f);
					ENTITY::DELETE_ENTITY(&o);
				}
				it = g_meteors.erase(it);
			}
			else
				++it;
		}
		if (MISC::GET_GAME_TIMER() - g_lastMeteor < 1000 || g_meteors.size() >= 10)
			return;
		g_lastMeteor = MISC::GET_GAME_TIMER();
		const Hash model = GameUtil::Joaat(kMeteorModels[MISC::GET_RANDOM_INT_IN_RANGE(0, 3)]);
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !GameUtil::LoadModel(model))
			return;
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const float x = me.x + MISC::GET_RANDOM_FLOAT_IN_RANGE(-15.0f, 15.0f) * 4.0f;
		const float y = me.y + MISC::GET_RANDOM_FLOAT_IN_RANGE(-15.0f, 15.0f) * 4.0f;
		const Object o = OBJECT::CREATE_OBJECT(model, x, y, 220.0f, FALSE, TRUE, TRUE, FALSE, FALSE);
		ENTITY::SET_ENTITY_LOD_DIST(o, 500);
		ENTITY::FREEZE_ENTITY_POSITION(o, FALSE);
		ENTITY::APPLY_FORCE_TO_ENTITY(o, 1, 0.0f, 0.0f, -200.0f, 20.0f, 20.0f, 50.0f, 0, FALSE, TRUE, TRUE, FALSE, TRUE);
		g_meteors.push_back(o);
	}

	void MeteorOff()
	{
		for (Object o : g_meteors)
			if (ENTITY::DOES_ENTITY_EXIST(o))
				ENTITY::DELETE_ENTITY(&o);
		g_meteors.clear();
		CLOCK::SET_CLOCK_TIME(12, 0, 0);
	}

	// --- IPLs and interiors -------------------------------------------------------

	std::string g_customIpl;
	std::string g_entitySet;

	std::string LoadIpl(Hash h, bool load)
	{
		if (load)
		{
			if (!STREAMING::IS_IPL_ACTIVE_HASH(h))
				STREAMING::REQUEST_IPL_HASH(h);
		}
		else if (STREAMING::IS_IPL_ACTIVE_HASH(h))
			STREAMING::REMOVE_IPL_HASH(h);
		return load ? "Loading IPL" : "Unloading IPL";
	}

	std::string TeleportToIpl(Hash h)
	{
		Vector3 p{};
		float radius = 0.0f;
		if (!STREAMING::_GET_IPL_BOUNDING_SPHERE(h, &p, &radius))
			return "IPL not found";
		ENTITY::SET_ENTITY_COORDS(Me(), p.x, p.y, p.z, TRUE, TRUE, TRUE, FALSE);
		return {};
	}

	std::string Iplname(const Ipl& i) { return *i.name ? i.name : Hex(i.hash); }

	std::string EntitySet(bool activate)
	{
		const Interior interior = INTERIOR::GET_INTERIOR_FROM_ENTITY(Me());
		if (!interior)
			return "Not in an interior";
		if (g_entitySet.empty() || !INTERIOR::_IS_INTERIOR_ENTITY_SET_VALID(interior, g_entitySet.c_str()))
			return "Unknown entity set";
		if (activate)
			INTERIOR::ACTIVATE_INTERIOR_ENTITY_SET(interior, g_entitySet.c_str(), 0);
		else
			INTERIOR::DEACTIVATE_INTERIOR_ENTITY_SET(interior, g_entitySet.c_str(), TRUE);
		return {};
	}

	// Rampagio_IPLList.xml: the same <IPL Hash="0x..." Name="..."/> lines
	// as Rampage's Lists\IPLList.xml.
	std::vector<Ipl> g_userIpls;
	std::vector<std::string> g_userIplNames;

	void LoadUserIpls()
	{
		g_userIpls.clear();
		g_userIplNames.clear();
		for (const std::string& line : DataFile::LoadLines(L"Rampagio_IPLList.xml"))
		{
			auto attr = [&](const char* key) -> std::string {
				const std::string k = std::string(key) + "=\"";
				const size_t a = line.find(k);
				if (a == std::string::npos)
					return {};
				const size_t start = a + k.size();
				return line.substr(start, line.find('"', start) - start);
			};
			const std::string hash = attr("Hash"), name = attr("Name");
			if (hash.empty() && name.empty())
				continue;
			g_userIplNames.push_back(name);
			g_userIpls.push_back({ hash.empty() ? GameUtil::Joaat(name) : GameUtil::ParseHash(hash), "" });
		}
	}

	// SubIMAPCustomSet: IPL set files in a RampagioIPLS folder next to the
	// .asi, the same format as Rampage's IPLS folder: <Load Hash="0x..."/>,
	// <Unload Hash="0x..."/> and <Interior ID="..." Name="entity set"/>
	// lines. Load applies them; Unload reverses them.
	int g_iplSetMode = 0; // 0 load, 1 unload

	std::wstring IplSetFolder() { return LogFallback::ModuleDirectory() + L"RampagioIPLS\\"; }

	std::vector<std::filesystem::path> IplSetFiles()
	{
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(IplSetFolder(), ec))
			if (entry.path().extension() == L".xml")
				files.push_back(entry.path());
		std::sort(files.begin(), files.end());
		return files;
	}

	std::string ApplyIplSet(const std::filesystem::path& file)
	{
		std::ifstream in(file);
		if (!in)
			return "Failed to load " + file.filename().string();
		const bool load = g_iplSetMode == 0;
		std::vector<Hash> loads, unloads;
		std::vector<std::pair<int, std::string>> sets;
		std::string line;
		while (std::getline(in, line))
		{
			auto attr = [&](const char* key) -> std::string {
				const std::string k = std::string(key) + "=\"";
				const size_t a = line.find(k);
				if (a == std::string::npos)
					return {};
				const size_t start = a + k.size();
				return line.substr(start, line.find('"', start) - start);
			};
			if (line.find("<Load ") != std::string::npos)
				loads.push_back(GameUtil::ParseHash(attr("Hash")));
			else if (line.find("<Unload ") != std::string::npos)
				unloads.push_back(GameUtil::ParseHash(attr("Hash")));
			else if (line.find("<Interior ") != std::string::npos)
				sets.push_back({ std::atoi(attr("ID").c_str()), attr("Name") });
		}
		for (Hash h : loads)
			load ? STREAMING::REQUEST_IPL_HASH(h) : STREAMING::REMOVE_IPL_HASH(h);
		for (Hash h : unloads)
			load ? STREAMING::REMOVE_IPL_HASH(h) : STREAMING::REQUEST_IPL_HASH(h);
		for (const auto& [interior, name] : sets)
			if (load)
				INTERIOR::ACTIVATE_INTERIOR_ENTITY_SET(interior, name.c_str(), 0);
			else
				INTERIOR::DEACTIVATE_INTERIOR_ENTITY_SET(interior, name.c_str(), TRUE);
		return std::format("{} {}: {} IPLs, {} entity sets", load ? "Loaded" : "Unloaded", file.stem().string(), loads.size() + unloads.size(), sets.size());
	}

	void BuildIplSets(MenuBase* m)
	{
		Ui::Choice(m, "Load Mode", { "Load", "Unload" }, &g_iplSetMode);
		const auto files = IplSetFiles();
		Ui::Action(m, "Load All", [files]
		{
			for (const auto& f : files)
				ApplyIplSet(f);
			return std::format("{} sets", files.size());
		});
		if (files.empty())
			Ui::Section(m, "No files in RampagioIPLS");
		for (const auto& f : files)
			Ui::Action(m, f.stem().string(), [f] { return ApplyIplSet(f); });
	}

	void AddIplRows(MenuBase* m, Hash h, const std::string& label)
	{
		Ui::Toggle(m, label, [h](bool on) { LoadIpl(h, on); })->SetState(STREAMING::IS_IPL_ACTIVE_HASH(h));
	}

	// --- world states --------------------------------------------------------------

	int g_stateId = 0;
	int g_stateOn = 0;

	UINT64* StateWord(int id)
	{
		UINT64* g = GameUtil::Global(40);
		if (!g || id < 0 || id / 31 >= static_cast<int>(g[283]))
			return nullptr;
		return &g[284 + id / 31];
	}

	void ReadState()
	{
		const UINT64* w = StateWord(g_stateId);
		g_stateOn = w && (*w >> (g_stateId % 31) & 1) ? 1 : 0;
	}

	void WriteState(int on)
	{
		UINT64* w = StateWord(g_stateId);
		if (!w)
			return;
		const UINT64 bit = 1ull << (g_stateId % 31);
		*w = on ? (*w | bit) : (*w & ~bit);
		if (UINT64* refresh = GameUtil::Global(1934765))
			*refresh = 0; // medium_update re-reads the states
	}
}

namespace Menus
{
	void BuildWorldSubmenus(MenuBase* world)
	{
		// SubWorldOcean.
		MenuBase* water = Ui::Submenu(world, "Water");
		Ui::Toggle(water, "Guarma Water", [](bool on) { WATER::_SET_WORLD_WATER_TYPE(on ? 1 : 0); });
		Ui::Toggle(water, "Guarma Hurricane Water", SetGuarmaHurricane);
		Ui::Looped(water, "Walk Underwater", [] { WATER::DISABLE_WATER_LOOKUP(); }, [] { WATER::ENABLE_WATER_LOOKUP(); });
		Ui::Section(water, "All Water");
		Ui::Toggle(water, "Disable Water", [](bool on) { SetNoWaterModifier(on); }, [] { WATER::DISABLE_WATER_LOOKUP(); });
		Ui::Toggle(water, "Clear Water", SetNoWaterModifier);

		// Cloud Editor.
		MenuBase* clouds = Ui::Submenu(world, "Cloud Editor");
		Ui::Looped(clouds, "Remove Clouds", [] { GRAPHICS::_SET_CLOUD_HEIGHT(-42069.0f); });
		Ui::Section(clouds, "Layer");
		Ui::Toggle(clouds, "Override Layer", [](bool on) { g_cloudLayer = on; }, CloudTick);
		Ui::Number(clouds, "X", &g_layerX, -1.0f, 1.0f, 0.005f);
		Ui::Number(clouds, "Y", &g_layerY, -1.0f, 1.0f, 0.005f);
		Ui::Section(clouds, "Noise");
		Ui::Toggle(clouds, "Override Noise", [](bool on) { g_cloudNoise = on; }, CloudTick);
		const char* const kXyz[] = { "X", "Y", "Z" };
		for (int i = 0; i < 3; i++)
			Ui::Number(clouds, std::string("Noise ") + kXyz[i], &g_noise[i], -5000.0f, 10000.0f, 2.0f);
		Ui::Section(clouds, "Position");
		Ui::Toggle(clouds, "Override Position", [](bool on) { g_cloudPosition = on; }, CloudTick);
		for (int i = 0; i < 3; i++)
			Ui::Number(clouds, std::string("Position ") + kXyz[i], &g_cloudPos[i], -1000.0f, 10000.0f, 2.0f);
		Ui::Section(clouds, "Height");
		Ui::Toggle(clouds, "Override Height", [](bool on) { g_cloudHeight = on; }, CloudTick);
		Ui::Number(clouds, "Height", &g_height, -1000.0f, 10000.0f, 2.0f);
		Ui::Section(clouds, "Cloudlapse");
		Ui::Choice(clouds, "Axis", { "X", "Y" }, &g_lapseAxis);
		Ui::Number(clouds, "Speed", &g_lapseSpeed, 0.001f, 0.1f, 0.001f);
		Ui::Looped(clouds, "Toggle", CloudlapseTick);

		// SubWorldLocalPeds ("Ped Manager").
		MenuBase* peds = Ui::Submenu(world, "Ped Manager");
		// Ours: open the Ped Editor on the aimed-at or a nearby ped.
		Ui::Action(peds, "Edit Aimed Ped", []() -> std::string {
			Entity e = 0;
			if (!PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(PLAYER::PLAYER_ID(), &e) || !ENTITY::IS_ENTITY_A_PED(e))
				return "Aim at a ped first";
			Menus::PedEditor::Open(e);
			return "";
		});
		Ui::ListMenu(peds, "Edit Nearby Ped", [](MenuBase* m) {
			const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
			std::vector<std::pair<float, Ped>> nearby;
			for (Ped p : OtherPeds())
				nearby.push_back({ GameUtil::DistanceSq(me, ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE)), p });
			std::sort(nearby.begin(), nearby.end());
			if (nearby.size() > 40)
				nearby.resize(40);
			for (const auto& [d, p] : nearby)
				Ui::Do(m, std::format("{:.0f} m  0x{:08X}{}", std::sqrt(d), ENTITY::GET_ENTITY_MODEL(p), ENTITY::IS_ENTITY_DEAD(p) ? " (dead)" : ""),
					[p] { Menus::PedEditor::Open(p); });
			if (nearby.empty())
				Ui::Section(m, "No peds nearby");
		});
		Ui::Do(peds, "Teleport to Me", [] {
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
			for (Ped ped : OtherPeds())
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(ped, p.x, p.y, p.z, FALSE, FALSE, FALSE);
		});
		Ui::Do(peds, "Jump", [] {
			for (Ped p : OtherPeds())
				ENTITY::APPLY_FORCE_TO_ENTITY(p, 1, 0.0f, 0.0f, 35.0f, 0.0f, 0.0f, 0.0f, 1, FALSE, TRUE, TRUE, TRUE, TRUE);
		});
		Ui::Do(peds, "Run to Me", [] {
			for (Ped p : OtherPeds())
				TASK::TASK_GO_TO_ENTITY(p, Me(), -1, 5.0f, 100.0f, 1.0f, 0);
		});
		Ui::Do(peds, "Hands Up", [] {
			for (Ped p : OtherPeds())
				TASK::TASK_HANDS_UP(p, 5000, Me(), 0, 0);
		});
		Ui::Do(peds, "Hogtie", [] {
			for (Ped p : OtherPeds())
				if (PED::IS_PED_HUMAN(p))
					TASK::TASK_CARRIABLE(p, ENTITY::_GET_OPTIMAL_CARRY_CONFIG(p, 1), 0, 0, 0);
		});
		Ui::Do(peds, "Cower", [] {
			for (Ped p : OtherPeds())
				TASK::TASK_COWER(p, 5000, Me(), 0);
		});
		Ui::Do(peds, "Explode", [] {
			ExplodeAt(Me(), 0.0f);
			ExplodePeds(false);
		});
		Ui::Do(peds, "Kill", [] {
			for (Ped p : OtherPeds())
				PED::APPLY_DAMAGE_TO_PED(p, 1000, TRUE, 0, 0);
		});
		Ui::Do(peds, "Auto-Kill Enemies", [] {
			const Ped me = Me();
			for (Ped p : OtherPeds())
				if (PED::IS_PED_IN_COMBAT(p, me) && !ENTITY::IS_ENTITY_DEAD(p))
					PED::EXPLODE_PED_HEAD(p, WEAPON_EXPLOSIVE);
		});
		Ui::Do(peds, "Explode Enemies", [] { ExplodePeds(true); });
		Ui::Do(peds, "Disarm Enemies", [] {
			const Ped me = Me();
			for (Ped p : OtherPeds())
				if (PED::IS_PED_IN_COMBAT(p, me))
					WEAPON::MAKE_PED_DROP_WEAPON(p, TRUE, 0, TRUE, FALSE);
		});
		Ui::Do(peds, "Nearby become Bodyguards", [] {
			const auto others = OtherPeds();
			for (Entity p : GameUtil::Nearby(std::vector<Entity>(others.begin(), others.end()),
				ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE), 30.0f))
				RecruitBodyguard(p);
		});
		Ui::Do(peds, "All become Bodyguards", [] {
			for (Ped p : OtherPeds())
				RecruitBodyguard(p);
		});
		Ui::Do(peds, "Hostile Peds", HostilePeds);
		Ui::Do(peds, "Restore Loot", [] {
			for (Ped p : OtherPeds())
				ENTITY::_SET_ENTITY_FULLY_LOOTED(p, FALSE);
		});
		Ui::Looped(peds, "Ped Scanner", PedScannerTick);
		Ui::Looped(peds, "Decrease Ambient Peds", DecreasePedsTick);
		Ui::Do(peds, "Delete All", [] { ClearAreaAround(0x4000); });

		// SubWorldLocalVehicles ("Vehicle Manager").
		MenuBase* vehicles = Ui::Submenu(world, "Vehicle Manager");
		Ui::Do(vehicles, "Teleport to Me", [] {
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
			for (Vehicle v : OtherVehicles())
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(v, p.x, p.y, p.z, FALSE, FALSE, FALSE);
		});
		Ui::Do(vehicles, "Ascend", [] {
			for (Vehicle v : OtherVehicles())
				ENTITY::APPLY_FORCE_TO_ENTITY(v, 1, 0.0f, 0.0f, 90.0f, 0.0f, 0.0f, 0.0f, 1, FALSE, FALSE, TRUE, FALSE, FALSE);
		});
		Ui::Do(vehicles, "Explode", [] {
			ExplodeAt(Me(), 0.0f);
			for (Vehicle v : OtherVehicles())
			{
				ExplodeAt(v, 3.0f);
				VEHICLE::EXPLODE_VEHICLE(v, FALSE, FALSE, 0, 0);
			}
		});
		Ui::Do(vehicles, "Delete", [] {
			for (Vehicle v : OtherVehicles())
			{
				TakeControl(v);
				ENTITY::DELETE_ENTITY(&v);
			}
		});
		Ui::Looped(vehicles, "Boost Forward", [] {
			for (Vehicle v : OtherVehicles())
				VEHICLE::SET_VEHICLE_FORWARD_SPEED(v, 100.0f);
		});
		Ui::Looped(vehicles, "Vehicle Scanner", VehicleScannerTick);
		Ui::Looped(vehicles, "Decrease Population", DecreaseVehiclesTick);
		Ui::Toggle(vehicles, "Random Trains", [](bool on) { VEHICLE::SET_RANDOM_TRAINS(on); });
		Ui::Toggle(vehicles, "Random Boats", [](bool on) { VEHICLE::SET_RANDOM_BOATS(on); });
		Ui::Do(vehicles, "Delete All", [] { ClearAreaAround(0x28); });

		// SubWorldLocalObjects ("Object Manager"). The Object Finder submenu
		// is part of the tabled Object Editor area.
		MenuBase* objects = Ui::Submenu(world, "Object Manager");
		Ui::Looped(objects, "Object Scanner", ObjectScannerTick);
		Ui::Do(objects, "Delete All", [] { ClearAreaAround(0x2200); });

		// SubWorldDoorManager.
		Ui::ListMenu(world, "Door Manager", BuildDoors);

		// SubWorldTornado ("Tornado & Black Hole").
		MenuBase* tornado = Ui::Submenu(world, "Tornado & Black Hole");
		Ui::Looped(tornado, "Meteor Shower", MeteorTick, MeteorOff);
		Ui::Section(tornado, "Tornado");
		Ui::Looped(tornado, "Enable Tornado", TornadoTick, [] { Remove(g_tornado); });
		Ui::Section(tornado, "Black Hole");
		Ui::Choice(tornado, "Force", { "1", "2", "3", "4", "5" }, &g_holeForce);
		Ui::Choice(tornado, "Force Type", { "Low Force", "Max Force" }, &g_holeForceType);
		Ui::Looped(tornado, "Enable Black Hole", BlackHoleTick, [] { Remove(g_blackHole); });
		Ui::Section(tornado, "Options");
		Ui::Toggle(tornado, "Delete Entities", [](bool on) { g_deleteEntities = on; });
		Ui::Section(tornado, "Attraction Settings");
		Ui::Toggle(tornado, "Vehicles", [](bool on) { g_attractVehicles = on; })->SetState(true);
		Ui::Toggle(tornado, "Peds", [](bool on) { g_attractPeds = on; })->SetState(true);
		Ui::Toggle(tornado, "Player", [](bool on) { g_attractPlayer = on; });
		Ui::Toggle(tornado, "Objects", [](bool on) { g_attractObjects = on; })->SetState(true);

		// SubWorldIMAPLoader, SubIMAPCustom.
		MenuBase* ipl = Ui::Submenu(world, "IPL Loader");
		Ui::ListMenu(ipl, "Map Sets", [](MenuBase* m) {
			for (const Ipl& i : kIpls)
				AddIplRows(m, i.hash, Iplname(i));
		});
		Ui::ListMenu(ipl, "Custom", [](MenuBase* m) {
			LoadUserIpls();
			if (g_userIpls.empty())
			{
				m->AddItem(new MenuItemLabel([] { return std::string("No Rampagio_IPLList.xml"); }));
				return;
			}
			Ui::Do(m, "Enable All", [] { for (const Ipl& i : g_userIpls) STREAMING::REQUEST_IPL_HASH(i.hash); });
			Ui::Do(m, "Disable All", [] { for (const Ipl& i : g_userIpls) STREAMING::REMOVE_IPL_HASH(i.hash); });
			for (size_t i = 0; i < g_userIpls.size(); i++)
				AddIplRows(m, g_userIpls[i].hash, g_userIplNames[i].empty() ? Hex(g_userIpls[i].hash) : g_userIplNames[i]);
		});
		Ui::ListMenu(ipl, "IPL Sets", BuildIplSets);
		Ui::Text(ipl, "IPL", &g_customIpl);
		Ui::Action(ipl, "Load Custom", [] { return LoadIpl(GameUtil::ParseHash(g_customIpl), true); });
		Ui::Action(ipl, "Unload Custom", [] { return LoadIpl(GameUtil::ParseHash(g_customIpl), false); });
		Ui::Action(ipl, "Teleport to Custom", [] { return TeleportToIpl(GameUtil::ParseHash(g_customIpl)); });
		Ui::Section(ipl, "Interior");
		ipl->AddItem(new MenuItemLabel([] { return "Current Interior: " + std::to_string(INTERIOR::GET_INTERIOR_FROM_ENTITY(Me())); }));
		Ui::Text(ipl, "Entity Set", &g_entitySet);
		Ui::Action(ipl, "Load Entity Set", [] { return EntitySet(true); });
		Ui::Action(ipl, "Unload Entity Set", [] { return EntitySet(false); });

		// SubWorldStates.
		MenuBase* states = Ui::Submenu(world, "World States");
		Ui::Number(states, "State", &g_stateId, 0, 31 * 64 - 1, 1, ReadState);
		Ui::Choice(states, "Status", { "Off", "On" }, &g_stateOn, WriteState);
		states->SetOnOpen([](MenuBase*) { ReadState(); });

		// Ambient Light.
		MenuBase* light = Ui::Submenu(world, "Ambient Light");
		Ui::Looped(light, "Toggle", AmbientLightTick);
		const char* const kRgb[] = { "Red", "Green", "Blue" };
		for (int i = 0; i < 3; i++)
			Ui::Number(light, kRgb[i], &g_lightColor[i], 0, 255, 5);
		Ui::Number(light, "Range", &g_lightRange, 0.0f, 5000.0f, 1.0f);
		Ui::Number(light, "Brightness", &g_lightBrightness, 0.0f, 5000.0f, 1.0f);
	}
}
