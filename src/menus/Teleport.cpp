/*
	Teleport menu: ports Rampage's Submenus::SubTeleport rows. The town list
	is our own (approximate centres; the ground probe finds the height), not
	Rampage's location tables. Custom locations are saved to
	Rampagio_Teleports.ini.
*/

#include "Menus.h"
#include "..\DataFile.h"
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

	const wchar_t* kTeleportsFile = L"Rampagio_Teleports.ini";

	std::string SaveCurrent()
	{
		std::string name;
		if (!GameUtil::PromptText("Location name", name, 40) || name.empty())
			return {};
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		DataFile::Ini ini = DataFile::Load(kTeleportsFile);
		auto& section = ini.sections[name];
		section["x"] = std::format("{:.3f}", p.x);
		section["y"] = std::format("{:.3f}", p.y);
		section["z"] = std::format("{:.3f}", p.z);
		section["heading"] = std::format("{:.1f}", ENTITY::GET_ENTITY_HEADING(Me()));
		return DataFile::Save(kTeleportsFile, ini) ? "Saved " + name : "Couldn't save";
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
				return "Not a number: " + text;
		}
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Mover(), coords[0], coords[1], coords[2], FALSE, FALSE, TRUE);
		return {};
	}

	// Shops and Services (ours): Rampage's list is its own table, and the
	// game scripts keep shop doors without names, so the list is a
	// user-supplied Rampagio_Shops.txt: "Name, x, y, z" per line.
	void BuildShops(MenuBase* menu)
	{
		int count = 0;
		for (const std::string& line : DataFile::LoadLines(L"Rampagio_Shops.txt"))
		{
			const size_t c1 = line.find(',');
			if (c1 == std::string::npos)
				continue;
			float x = 0, y = 0, z = 0;
			if (sscanf_s(line.c_str() + c1 + 1, " %f , %f , %f", &x, &y, &z) < 2)
				continue;
			const std::string name = line.substr(0, c1);
			++count;
			Ui::Do(menu, name, [x, y, z] {
				if (z != 0.0f)
					ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Me(), x, y, z, FALSE, FALSE, FALSE);
				else
					GameUtil::TeleportToGround(Me(), x, y);
			});
		}
		if (!count)
			Ui::Section(menu, "Add lines \"Name, x, y, z\" to Rampagio_Shops.txt");
	}

	void BuildCustomList(MenuBase* menu)
	{
		DataFile::Ini ini = DataFile::Load(kTeleportsFile);
		for (auto& [name, section] : ini.sections)
		{
			float x, y, z;
			if (!ParseFloat(section["x"], x) || !ParseFloat(section["y"], y) || !ParseFloat(section["z"], z))
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
		DataFile::Ini ini = DataFile::Load(kTeleportsFile);
		for (auto& [name, section] : ini.sections)
		{
			const std::string key = name;
			Ui::Action(menu, name, [key]
			{
				DataFile::Ini file = DataFile::Load(kTeleportsFile);
				file.sections.erase(key);
				return DataFile::Save(kTeleportsFile, file) ? "Deleted " + key : std::string("Couldn't save");
			});
		}
		if (menu->GetItemCount() == 0)
			Ui::Section(menu, "Nothing saved yet");
	}

	struct Place { const char* name; float x, y; };

	// Our own approximate town centres.
	constexpr Place kTowns[] = {
		{ "Annesburg", 2930.0f, 1300.0f },
		{ "Armadillo", -3660.0f, -2600.0f },
		{ "Beecher's Hope", -1640.0f, -1440.0f },
		{ "Benedict Point", -5240.0f, -3460.0f },
		{ "Blackwater", -800.0f, -1300.0f },
		{ "Braithwaite Manor", 1010.0f, -1730.0f },
		{ "Butcher Creek", 2560.0f, 820.0f },
		{ "Caliga Hall", 1820.0f, -1340.0f },
		{ "Colter", -1350.0f, 2410.0f },
		{ "Cornwall Kerosene & Tar", 470.0f, 640.0f },
		{ "Emerald Ranch", 1430.0f, 300.0f },
		{ "Horseshoe Overlook", -160.0f, 640.0f },
		{ "Lagras", 2090.0f, -600.0f },
		{ "MacFarlane's Ranch", -2370.0f, -2390.0f },
		{ "Manzanita Post", -1960.0f, -1610.0f },
		{ "Rhodes", 1270.0f, -1300.0f },
		{ "Saint Denis", 2630.0f, -1250.0f },
		{ "Sisika Penitentiary", 3340.0f, -660.0f },
		{ "Strawberry", -1790.0f, -420.0f },
		{ "Tumbleweed", -5500.0f, -2950.0f },
		{ "Valentine", -290.0f, 790.0f },
		{ "Van Horn", 2970.0f, 560.0f },
		{ "Wapiti", 500.0f, 2200.0f },
	};
}

namespace Menus
{
	void BuildTeleport(MenuBase* root)
	{
		MenuBase* tp = Ui::Submenu(root, "Teleport");

		Ui::Action(tp, "Teleport to Waypoint", ToWaypoint);
		Ui::Looped(tp, "Auto Teleport to Waypoint", AutoWaypointTick);
		Ui::Do(tp, "Remove Waypoint", [] { MAP::CLEAR_GPS_PLAYER_WAYPOINT(); });

		Ui::Section(tp, "Custom Locations");
		Ui::Action(tp, "Save Current", SaveCurrent);
		Ui::Action(tp, "Custom Input", CustomInput);
		Ui::ListMenu(tp, "Load Custom", BuildCustomList);
		Ui::ListMenu(tp, "Delete Custom", BuildDeleteList);

		Ui::Section(tp, "Directional");
		Ui::Action(tp, "Forward", [] { return Step(5.0f, false); });
		Ui::Action(tp, "Forward (Elevation Safe)", [] { return Step(5.0f, true); });
		Ui::Action(tp, "Backward", [] { return Step(-5.0f, false); });
		Ui::Action(tp, "Backward (Elevation Safe)", [] { return Step(-5.0f, true); });

		Ui::Section(tp, "Vehicle / Horse");
		Ui::Action(tp, "Last Vehicle", LastVehicle);
		Ui::Action(tp, "Last Horse", LastHorse);
		Ui::Action(tp, "Nearest Vehicle", NearestVehicle);
		Ui::Action(tp, "Nearest Train Track", NearestTrainTrack);

		Ui::ListMenu(tp, "Shops and Services", BuildShops);
		MenuBase* towns = Ui::Submenu(tp, "Common Locations");
		for (const Place& place : kTowns)
		{
			const float x = place.x, y = place.y;
			Ui::Action(towns, place.name, [x, y] { return ToGround(x, y); });
		}
	}
}
