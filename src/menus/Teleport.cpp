/*
	Teleport menu: ports Rampage's Submenus::SubTeleport rows. The location
	lists (Common Locations, the region submenus and Shops and Services) are
	Rampage's names and coordinates, carried over into data\Teleports.inc by
	tools\extract_rampage_teleports_ida.py. Custom locations are saved to
	Rampagio_Teleports.json. Blips lists the map location and mission
	blips the scripts keep in globals (1491.50 indices, the ones Rampage
	reads), named through Rampage's blip type table (data\BlipLabels.inc).
*/

#include "Menus.h"
#include "..\DataFile.h"

#include <nlohmann/json.hpp>
#include "..\GameUtil.h"
#include "..\Log.h"

#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	// The entity to move: the mount or vehicle when riding/driving, so the
	// player comes along.
	Entity Mover()
	{
		const Ped ped = Me();
		if (const Ped mount = GameUtil::PlayerMount())
			return mount;
		if (PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE))
			return PED::GET_VEHICLE_PED_IS_IN(ped, FALSE);
		return ped;
	}

	std::string ToGround(float x, float y)
	{
		if (!GameUtil::TeleportToGround(Mover(), x, y))
			return "Couldn't find the ground there";
		return {};
	}

	std::string ToWaypoint()
	{
		if (!MAP::IS_WAYPOINT_ACTIVE())
			return "No waypoint set";
		const Vector3 target = MAP::_GET_WAYPOINT_COORDS();
		return ToGround(target.x, target.y);
	}

	// --- blips -------------------------------------------------------------------

	struct BlipLabel { int type; Hash label; };
	const BlipLabel kBlipLabels[] = {
#include "..\data\BlipLabels.inc"
	};

	// Global_36308[i]: map location blips; Global_40.f_7862[i /*4*/].f_0
	// is each one's type. Global_1835011[i /*74*/].f_27 / .f_26: mission
	// blips and their label hashes, Global_1879534.f_7300 of them.
	constexpr int kLocationBlips = 36308;
	constexpr int kLocationTypes = 40, kLocationTypesField = 7862;
	constexpr int kMissionBlips = 1835011, kMissionStride = 74;
	constexpr int kMissionCount = 1879534, kMissionCountField = 7300;

	void AddBlipRow(MenuBase* m, Blip blip, Hash label, int index)
	{
		if (!MAP::DOES_BLIP_EXIST(blip))
			return;
		const std::string name = TrFormat("Blip ({}) {}", index, label ? GameUtil::ItemName(label, "") : std::string());
		Ui::Action(m, name, [blip] {
			if (!MAP::DOES_BLIP_EXIST(blip))
				return std::string("Blip is gone");
			const Vector3 p = MAP::GET_BLIP_COORDS(blip);
			return ToGround(p.x, p.y);
		});
	}

	void BuildBlips(MenuBase* m)
	{
		const UINT64* blips = GameUtil::Global(kLocationBlips);
		const UINT64* types = GameUtil::Global(kLocationTypes) + kLocationTypesField;
		const int count = static_cast<int>(blips[0]);
		for (int i = 0; i < count && i < static_cast<int>(types[0]); i++)
		{
			const int type = static_cast<int>(types[1 + 4 * i]);
			Hash label = 0;
			for (const BlipLabel& l : kBlipLabels)
				if (l.type == type)
					label = l.label;
			AddBlipRow(m, static_cast<Blip>(blips[1 + i]), label, i);
		}
		Ui::Section(m, "Missions");
		const UINT64* missions = GameUtil::Global(kMissionBlips);
		const int missionCount = static_cast<int>(GameUtil::Global(kMissionCount)[kMissionCountField]);
		for (int i = 0; i < missionCount && i < static_cast<int>(missions[0]); i++)
		{
			const UINT64* entry = missions + 1 + kMissionStride * i;
			AddBlipRow(m, static_cast<Blip>(entry[27]), static_cast<Hash>(entry[26]), i);
		}
		if (m->GetItemCount() <= 1)
			m->AddItem(new MenuItemLabel([] { return std::string("No active blips"); }));
	}

	void AutoWaypointTick()
	{
		if (MAP::IS_WAYPOINT_ACTIVE())
		{
			ToWaypoint();
			MAP::CLEAR_GPS_PLAYER_WAYPOINT();
		}
	}

	// Rampage's "elevation safe" variants drop the target height to -200 and
	// rely on the game snapping up; ours probes for the ground instead.
	std::string Step(float forward, bool groundProbe)
	{
		const Entity e = Mover();
		const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(e, 0.0f, forward, 0.0f);
		if (groundProbe)
			return ToGround(p.x, p.y);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, p.x, p.y, p.z, TRUE, TRUE, TRUE);
		return {};
	}

	std::string LastVehicle()
	{
		const Vehicle v = VEHICLE::GET_LAST_DRIVEN_VEHICLE();
		if (!v || !ENTITY::DOES_ENTITY_EXIST(v))
			return "No last vehicle";
		PED::SET_PED_INTO_VEHICLE(Me(), v, -1);
		return {};
	}

	std::string LastHorse()
	{
		const Ped h = PED::_GET_LAST_MOUNT(Me());
		if (!h || !ENTITY::DOES_ENTITY_EXIST(h))
			return "No last horse";
		PED::SET_PED_ONTO_MOUNT(Me(), h, -1, TRUE);
		return {};
	}

	std::string NearestVehicle()
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const Vehicle v = VEHICLE::GET_CLOSEST_VEHICLE(p.x, p.y, p.z, 25.0f, 0, 0);
		if (!v || !ENTITY::DOES_ENTITY_EXIST(v))
			return "No vehicle within 25m";
		PED::SET_PED_INTO_VEHICLE(Me(), v, -1);
		return {};
	}

	std::string NearestTrainTrack()
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const Vector3 t = VEHICLE::_GET_NEAREST_TRAIN_TRACK_POSITION(p.x, p.y, p.z);
		ENTITY::SET_ENTITY_COORDS(Me(), t.x, t.y, t.z, TRUE, TRUE, TRUE, FALSE);
		return {};
	}

	// --- custom locations ---------------------------------------------

	const wchar_t* kTeleportsFile = L"Rampagio_Teleports.json";

	std::string SaveCurrent()
	{
		std::string name;
		if (!GameUtil::PromptText("Location name", name, 40) || name.empty())
			return {};
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		nlohmann::json file = DataFile::LoadJson(kTeleportsFile);
		file[name] = { { "x", p.x }, { "y", p.y }, { "z", p.z }, { "heading", ENTITY::GET_ENTITY_HEADING(Me()) } };
		return DataFile::SaveJson(kTeleportsFile, file) ? "Saved " + name : "Couldn't save";
	}

	bool ParseFloat(const std::string& s, float& out)
	{
		try
		{
			size_t used = 0;
			out = std::stof(s, &used);
			return used > 0;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string CustomInput()
	{
		float coords[3] = {};
		const char* titles[3] = { "X", "Y", "Z (blank = ground)" };
		for (int i = 0; i < 3; i++)
		{
			std::string text;
			if (!GameUtil::PromptText(titles[i], text, 20))
				return {};
			if (i == 2 && text.empty())
				return ToGround(coords[0], coords[1]);
			if (!ParseFloat(text, coords[i]))
				return TrFormat("Not a number: {}", text);
		}
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Mover(), coords[0], coords[1], coords[2], FALSE, FALSE, TRUE);
		return {};
	}

	// A saved location's coordinates; false if a field is missing.
	bool ReadPlace(const nlohmann::json& place, float& x, float& y, float& z)
	{
		auto number = [&place](const char* key, float& out)
		{
			auto it = place.find(key);
			if (it == place.end() || !it->is_number())
				return false;
			out = it->get<float>();
			return true;
		};
		return place.is_object() && number("x", x) && number("y", y) && number("z", z);
	}

	void BuildCustomList(MenuBase* menu)
	{
		const nlohmann::json file = DataFile::LoadJson(kTeleportsFile);
		for (const auto& [name, place] : file.items())
		{
			float x, y, z;
			if (!ReadPlace(place, x, y, z))
				continue;
			Ui::Do(menu, name, [x, y, z]
			{
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Mover(), x, y, z, FALSE, FALSE, TRUE);
			});
		}
		if (menu->GetItemCount() == 0)
			Ui::Section(menu, "Nothing saved yet");
	}

	void BuildDeleteList(MenuBase* menu)
	{
		const nlohmann::json saved = DataFile::LoadJson(kTeleportsFile);
		for (const auto& [name, place] : saved.items())
		{
			const std::string key = name;
			Ui::Action(menu, name, [key]
			{
				nlohmann::json file = DataFile::LoadJson(kTeleportsFile);
				file.erase(key);
				return DataFile::SaveJson(kTeleportsFile, file) ? "Deleted " + key : std::string("Couldn't save");
			});
		}
		if (menu->GetItemCount() == 0)
			Ui::Section(menu, "Nothing saved yet");
	}

	struct Place
	{
		const char* menu;    // submenu of Teleport
		const char* nested;  // submenu inside it, or ""
		const char* section; // section header before the row, or ""
		const char* idPrefix;
		const char* name;
		float x, y, z;
	};

	constexpr Place kPlaces[] = {
#include "..\data\Teleports.inc"
	};

	// One submenu per Place::menu (and Place::nested), in table order, with
	// a section row wherever Place::section changes.
	void BuildPlaces(MenuBase* tp)
	{
		std::string menuName, nestedName, section;
		MenuBase* menu = nullptr;
		MenuBase* target = nullptr;
		for (const Place& place : kPlaces)
		{
			if (place.menu != menuName)
			{
				menuName = place.menu;
				menu = Ui::Submenu(tp, menuName);
				nestedName.clear();
				target = menu;
				section.clear();
			}
			if (place.nested != nestedName)
			{
				nestedName = place.nested;
				target = nestedName.empty() ? menu : Ui::Submenu(menu, nestedName);
				section.clear();
			}
			if (place.section != section)
			{
				section = place.section;
				if (!section.empty())
					Ui::Section(target, section);
			}
			const float x = place.x, y = place.y, z = place.z;
			Ui::Do(target, Ui::Id(place.idPrefix, place.name), place.name, [x, y, z]
			{
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Mover(), x, y, z, FALSE, FALSE, TRUE);
			});
		}
	}
}

namespace Menus
{
	void BuildTeleport(MenuBase* root)
	{
		MenuBase* tp = Ui::Submenu(root, "Teleport");

		Ui::Action(tp, "teleport.teleporttowaypoint", "Teleport to Waypoint", ToWaypoint);
		Ui::Looped(tp, "teleport.autoteleporttowaypoint", "Auto Teleport to Waypoint", AutoWaypointTick);
		Ui::Do(tp, "teleport.removewaypoint", "Remove Waypoint", [] { MAP::CLEAR_GPS_PLAYER_WAYPOINT(); });

		Ui::Section(tp, "Custom Locations");
		Ui::Action(tp, "teleport.savecurrent", "Save Current", SaveCurrent)->SetHotkeyable(false);
		Ui::Action(tp, "teleport.custominput", "Custom Input", CustomInput)->SetHotkeyable(false);
		Ui::ListMenu(tp, "Load Custom", BuildCustomList);
		Ui::ListMenu(tp, "Delete Custom", BuildDeleteList);

		Ui::Section(tp, "Directional");
		Ui::Action(tp, "teleport.forward", "Forward", [] { return Step(5.0f, false); });
		Ui::Action(tp, "teleport.forwardelevationsafe", "Forward (Elevation Safe)", [] { return Step(5.0f, true); });
		Ui::Action(tp, "teleport.backward", "Backward", [] { return Step(-5.0f, false); });
		Ui::Action(tp, "teleport.backwardelevationsafe", "Backward (Elevation Safe)", [] { return Step(-5.0f, true); });

		Ui::Section(tp, "Vehicle / Horse");
		Ui::Action(tp, "teleport.lastvehicle", "Last Vehicle", LastVehicle);
		Ui::Action(tp, "teleport.lasthorse", "Last Horse", LastHorse);
		Ui::Action(tp, "teleport.nearestvehicle", "Nearest Vehicle", NearestVehicle);
		Ui::Action(tp, "teleport.nearesttraintrack", "Nearest Train Track", NearestTrainTrack);

		Ui::Section(tp, "Locations");
		Ui::ListMenu(tp, "Blips", BuildBlips);
		BuildPlaces(tp);
	}
}
