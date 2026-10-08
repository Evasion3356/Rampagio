/*
	Spawner > Object Spawner: ports Rampage's Submenus::SubObjectSpawner,
	SubObjSpawnerDatabase, SubObjSpawnerAllObjs, SubObjSpawnerPropsets,
	SubObjSpawnerLoadSave and SubPlantSpawner. Object, propset and herb
	composite names come from the game scripts (tools/extract_objects.py);
	All Objects also reads a user-supplied Rampagio_ObjectList.txt (same
	format as Rampage's Lists\ObjectList.txt).

	Saved sets go in Rampagio_Spooner.json (ours); Rampage reads and writes
	its own spooner XML files. The Object Editor (moving and rotating a
	selected object) is tabled with the rest of that area.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	const char* const kObjects[] = {
#include "..\data\ObjectModels.inc"
	};
	const char* const kPropsets[] = {
#include "..\data\Propsets.inc"
	};
	const char* const kComposites[] = {
#include "..\data\Composites.inc"
	};

	// --- creator cam -----------------------------------------------------------

	constexpr Hash INPUT_SPRINT = 0x8FFC75D6;
	constexpr Hash INPUT_LOOK_LR = 0xA987235F;
	constexpr Hash INPUT_LOOK_UD = 0xD2047988;
	constexpr Hash INPUT_MOVE_LR = 0x4D8FB4C1;
	constexpr Hash INPUT_MOVE_UD = 0xFDA83190;

	Cam g_cam = 0;
	float g_camFov = 50.0f;
	float g_camSpeed = 0.5f;
	bool g_camTakePlayer = true; // Rampage's Creator Settings
	bool g_camClearSpace = false;

	void CreatorCamTick()
	{
		const Ped ped = Me();
		if (!CAMERA::DOES_CAM_EXIST(g_cam))
		{
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, FALSE, FALSE);
			g_cam = CAMERA::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", TRUE);
			CAMERA::SET_CAM_COORD(g_cam, p.x, p.y, p.z + 2.0f);
			CAMERA::SET_CAM_ROT(g_cam, 0.0f, 0.0f, ENTITY::GET_ENTITY_HEADING(ped), 2);
			CAMERA::SET_CAM_ACTIVE(g_cam, TRUE);
			CAMERA::RENDER_SCRIPT_CAMS(TRUE, FALSE, 3000, TRUE, FALSE, 0);
			ENTITY::SET_ENTITY_VISIBLE(ped, !g_camTakePlayer);
		}
		CAMERA::SET_CAM_FOV(g_cam, g_camFov);
		PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
		Vector3 rot = CAMERA::GET_CAM_ROT(g_cam, 2);
		rot.z -= PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_LR) * 5.0f;
		rot.x = std::clamp(rot.x - PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_UD) * 5.0f, -89.0f, 89.0f);
		CAMERA::SET_CAM_ROT(g_cam, rot.x, 0.0f, rot.z, 2);
		const float pitch = rot.x * 3.14159265f / 180.0f, yaw = rot.z * 3.14159265f / 180.0f;
		const float c = std::cos(pitch);
		const float fx = -std::sin(yaw) * c, fy = std::cos(yaw) * c, fz = std::sin(pitch);
		float speed = g_camSpeed;
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_SPRINT))
			speed *= 4.0f;
		const float forward = -PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_UD) * speed;
		const float right = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_LR) * speed;
		Vector3 p = CAMERA::GET_CAM_COORD(g_cam);
		p.x += fx * forward + fy * right;
		p.y += fy * forward - fx * right;
		p.z += fz * forward;
		CAMERA::SET_CAM_COORD(g_cam, p.x, p.y, p.z);
		// Keep the player (and the streaming focus) under the camera, or
		// just the focus when the player stays behind.
		if (g_camTakePlayer)
		{
			ENTITY::SET_ENTITY_COORDS_NO_OFFSET(ped, p.x, p.y, p.z, TRUE, g_camClearSpace, TRUE);
			ENTITY::SET_ENTITY_HEADING(ped, rot.z);
		}
		else
			STREAMING::SET_FOCUS_POS_AND_VEL(p.x, p.y, p.z, 0.0f, 0.0f, 0.0f);
	}

	void CreatorCamOff()
	{
		const Ped ped = Me();
		ENTITY::SET_ENTITY_VISIBLE(ped, TRUE);
		if (CAMERA::DOES_CAM_EXIST(g_cam))
		{
			const Vector3 p = CAMERA::GET_CAM_COORD(g_cam);
			if (g_camTakePlayer)
				GameUtil::TeleportToGround(ped, p.x, p.y);
			STREAMING::CLEAR_FOCUS();
			CAMERA::SET_CAM_ACTIVE(g_cam, FALSE);
			CAMERA::DESTROY_CAM(g_cam, FALSE);
			CAMERA::RENDER_SCRIPT_CAMS(FALSE, FALSE, 3000, TRUE, FALSE, 0);
		}
		g_cam = 0;
	}

	// Where to put new things: in front of the creator cam when it's on,
	// else 5 m in front of the player.
	Vector3 SpawnPoint(float& heading)
	{
		if (CAMERA::DOES_CAM_EXIST(g_cam))
		{
			const Vector3 rot = CAMERA::GET_CAM_ROT(g_cam, 2);
			const float yaw = rot.z * 3.14159265f / 180.0f;
			Vector3 p = CAMERA::GET_CAM_COORD(g_cam);
			p.x += -std::sin(yaw) * 5.0f;
			p.y += std::cos(yaw) * 5.0f;
			heading = rot.z;
			return p;
		}
		heading = ENTITY::GET_ENTITY_HEADING(Me());
		return ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(Me(), 0.0f, 5.0f, 0.0f);
	}

	// --- the object database -----------------------------------------------------

	struct SpawnedObject
	{
		Object object;
		std::string model;
	};
	std::vector<SpawnedObject> g_objects;

	std::string SpawnObject(const std::string& name)
	{
		const Hash model = GameUtil::ParseHash(name);
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model))
			return "Model is invalid.";
		if (!GameUtil::LoadModel(model))
			return "Failed to Load";
		float heading = 0.0f;
		const Vector3 p = SpawnPoint(heading);
		const Object o = OBJECT::CREATE_OBJECT(model, p.x, p.y, p.z, FALSE, FALSE, TRUE, FALSE, FALSE);
		ENTITY::SET_ENTITY_HEADING(o, heading);
		OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(o, FALSE);
		g_objects.push_back({ o, name });
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return {};
	}

	void DeleteObject(Object o)
	{
		if (!ENTITY::DOES_ENTITY_EXIST(o))
			return;
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(o))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(o, TRUE, TRUE);
		ENTITY::DELETE_ENTITY(&o);
	}

	std::string HijackObject()
	{
		std::string name;
		if (!GameUtil::PromptText("Enter Name or Hash:", name) || name.empty())
			return {};
		const Hash model = GameUtil::ParseHash(name);
		for (Object o : GameUtil::AllObjects())
			if (ENTITY::GET_ENTITY_MODEL(o) == model)
			{
				if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(o))
					ENTITY::SET_ENTITY_AS_MISSION_ENTITY(o, TRUE, TRUE);
				g_objects.push_back({ o, name });
				return "Added to the database";
			}
		return "No object of that model";
	}

	int g_databaseAction = 0; // clear list, delete all

	MenuBase* g_objectMenu = nullptr;
	Object g_selected = 0;

	void BuildSelectedObject(MenuBase* m)
	{
		const Object o = g_selected;
		if (!ENTITY::DOES_ENTITY_EXIST(o))
		{
			m->AddItem(new MenuItemLabel([] { return std::string("Object no longer exists"); }));
			return;
		}
		Ui::Do(m, "Teleport to Object", [o] {
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(o, TRUE, FALSE);
			ENTITY::SET_ENTITY_COORDS(Me(), p.x, p.y, p.z + 1.0f, FALSE, FALSE, FALSE, FALSE);
		});
		Ui::Do(m, "Bring to Me", [o] {
			float heading = 0.0f;
			const Vector3 p = SpawnPoint(heading);
			ENTITY::SET_ENTITY_COORDS(o, p.x, p.y, p.z, FALSE, FALSE, FALSE, FALSE);
			OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(o, FALSE);
		});
		Ui::Do(m, "Place on Ground", [o] { OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(o, FALSE); });
		Ui::Toggle(m, "Frozen", [o](bool on) { ENTITY::FREEZE_ENTITY_POSITION(o, on); });
		Ui::Toggle(m, "Collision", [o](bool on) { ENTITY::SET_ENTITY_COLLISION(o, on, TRUE); })->SetState(true);
		Ui::Toggle(m, "Visible", [o](bool on) { ENTITY::SET_ENTITY_VISIBLE(o, on); })->SetState(ENTITY::IS_ENTITY_VISIBLE(o));
		Ui::Do(m, "Delete", [o] {
			DeleteObject(o);
			std::erase_if(g_objects, [o](SpawnedObject& s) { return s.object == o; });
			Ui::Controller().PopMenu();
		});
	}

	void BuildDatabase(MenuBase* m)
	{
		std::erase_if(g_objects, [](SpawnedObject& s) { return !ENTITY::DOES_ENTITY_EXIST(s.object); });
		Ui::Choice(m, "Action", { "Clear List", "Delete All" }, &g_databaseAction);
		Ui::Do(m, "Run Action", [] {
			if (g_databaseAction == 1)
				for (auto& s : g_objects)
					DeleteObject(s.object);
			g_objects.clear();
			Ui::Controller().ReopenActiveLater();
		});
		int i = 0;
		for (const SpawnedObject& s : g_objects)
		{
			const Object o = s.object;
			m->AddItem(new MenuItemAction(std::format("{} {}", ++i, s.model), [o] {
				g_selected = o;
				Ui::Push(g_objectMenu);
			}));
		}
	}

	// --- propsets and composites ---------------------------------------------------------

	std::vector<int> g_propsets;

	std::string SpawnPropset(const std::string& name)
	{
		const Hash h = GameUtil::ParseHash(name);
		PROPSET::_REQUEST_PROP_SET(h);
		for (int i = 0; i < 200 && !PROPSET::_HAS_PROP_SET_LOADED(h); i++)
			WAIT(10);
		if (!PROPSET::_HAS_PROP_SET_LOADED(h))
			return "Failed to Load";
		float heading = 0.0f;
		const Vector3 p = SpawnPoint(heading);
		g_propsets.push_back(PROPSET::_CREATE_PROP_SET(h, p.x, p.y, p.z, 0, heading, 1200.0f, FALSE, TRUE));
		return {};
	}

	std::string SpawnComposite(const char* name)
	{
		const Hash h = GameUtil::Joaat(name);
		TASK::_REQUEST_HERB_COMPOSITE_ASSET(h);
		for (int i = 0; i < 200 && !TASK::ARE_COMPOSITE_LOOTABLE_ENTITY_DEF_ASSETS_LOADED(h); i++)
			WAIT(10);
		if (!TASK::ARE_COMPOSITE_LOOTABLE_ENTITY_DEF_ASSETS_LOADED(h))
			return "Herb is invalid.";
		const Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(Me(), 0.0f, 5.0f, 0.0f);
		alignas(8) UINT64 out[16] = {};
		TASK::_CREATE_HERB_COMPOSITES(h, p.x, p.y, p.z, 0.0f, 0, reinterpret_cast<Any*>(out), -1);
		return {};
	}

	// --- save / load (ours: Rampagio_Spooner.json)--------------------------------------

	const wchar_t* kSpoonerFile = L"Rampagio_Spooner.json";
	bool g_addToDatabase = true;

	std::string SaveSet()
	{
		std::string name;
		if (!GameUtil::PromptText("Set name", name, 40) || name.empty())
			return {};
		nlohmann::json objects = nlohmann::json::array();
		for (const SpawnedObject& s : g_objects)
		{
			if (!ENTITY::DOES_ENTITY_EXIST(s.object))
				continue;
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(s.object, FALSE, FALSE);
			const Vector3 r = ENTITY::GET_ENTITY_ROTATION(s.object, 2);
			objects.push_back({ { "model", s.model }, { "x", p.x }, { "y", p.y }, { "z", p.z }, { "rx", r.x }, { "ry", r.y }, { "rz", r.z } });
		}
		const size_t count = objects.size();
		nlohmann::json file = DataFile::LoadJson(kSpoonerFile);
		file[name] = std::move(objects);
		return DataFile::SaveJson(kSpoonerFile, file) ? std::format("Saved {} objects", count) : "Couldn't save";
	}

	std::string LoadSet(const std::string& name)
	{
		const nlohmann::json file = DataFile::LoadJson(kSpoonerFile);
		auto it = file.find(name);
		if (it == file.end() || !it->is_array())
			return "Not found";
		int count = 0;
		for (const nlohmann::json& saved : *it)
		{
			try
			{
				const std::string modelName = saved.at("model").get<std::string>();
				const Hash model = GameUtil::ParseHash(modelName);
				if (!GameUtil::LoadModel(model))
					continue;
				const Object o = OBJECT::CREATE_OBJECT(model, saved.at("x").get<float>(), saved.at("y").get<float>(), saved.at("z").get<float>(),
					FALSE, FALSE, TRUE, FALSE, FALSE);
				ENTITY::SET_ENTITY_ROTATION(o, saved.at("rx").get<float>(), saved.at("ry").get<float>(), saved.at("rz").get<float>(), 2, TRUE);
				ENTITY::FREEZE_ENTITY_POSITION(o, TRUE);
				if (g_addToDatabase)
					g_objects.push_back({ o, modelName });
				STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
				count++;
			}
			catch (const nlohmann::json::exception&)
			{
				// a damaged entry; load the rest
			}
		}
		return std::format("Loaded {} objects", count);
	}

	void BuildLoadSave(MenuBase* m)
	{
		Ui::Toggle(m, "Add Entities to Database", [](bool on) { g_addToDatabase = on; })->SetState(g_addToDatabase);
		Ui::Action(m, "Save Database", SaveSet);
		const nlohmann::json saved = DataFile::LoadJson(kSpoonerFile);
		if (saved.empty())
			m->AddItem(new MenuItemLabel([] { return std::string("No files found"); }));
		for (const auto& [name, set] : saved.items())
		{
			const std::string n = name;
			Ui::Action(m, n, [n] { return LoadSet(n); });
		}
	}

	std::string Lower(std::string s)
	{
		for (auto& c : s)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return s;
	}

	bool ObjectAvailable(const char* name) { return STREAMING::IS_MODEL_IN_CDIMAGE(GameUtil::Joaat(name)); }
}

namespace Menus
{
	void BuildObjectSpawner(MenuBase* spawner)
	{
		MenuBase* objects = Ui::Submenu(spawner, "Object Spawner");
		MenuBase* cam = Ui::Submenu(objects, "Cam Settings");
		Ui::Number(cam, "objectspawner.fov", "FOV", &g_camFov, 10.0f, 120.0f, 5.0f);
		Ui::Number(cam, "objectspawner.speed", "Speed", &g_camSpeed, 0.05f, 5.0f, 0.05f);
		Ui::Toggle(cam, "objectspawner.takeplayerwithcam", "Take Player With Cam", [](bool on) { g_camTakePlayer = on; })->SetDefault(true);
		Ui::Toggle(cam, "objectspawner.clearspaceforplayer", "Clear Space for Player", [](bool on) { g_camClearSpace = on; });
		Ui::Toggle(objects, "objectspawner.creatorcam", "Creator Cam", [](bool on) { if (!on) CreatorCamOff(); }, CreatorCamTick);

		g_objectMenu = Ui::DetachedListMenu("Object", BuildSelectedObject);
		Ui::ListMenu(objects, "Object Database", BuildDatabase);
		Ui::ListMenu(objects, "Load / Save", BuildLoadSave);
		Ui::ListMenu(objects, "All Objects", [](MenuBase* m) {
			const auto user = DataFile::LoadLines(L"Rampagio_ObjectList.txt");
			if (!user.empty())
			{
				Ui::Section(m, "Rampagio_ObjectList.txt");
				for (const std::string& name : user)
					Ui::Action(m, name, [name] { return SpawnObject(name); });
				Ui::Section(m, "Game Scripts");
			}
			for (const char* name : kObjects)
				if (ObjectAvailable(name))
				{
					const std::string n = name;
					Ui::Action(m, n, [n] { return SpawnObject(n); });
				}
		});
		Ui::ListMenu(objects, "Propsets", [](MenuBase* m) {
			Ui::Action(m, "Custom Input", [] {
				std::string name;
				if (!GameUtil::PromptText("Enter Propset Name or Hash:", name) || name.empty())
					return std::string();
				return SpawnPropset(name);
			});
			Ui::Do(m, "Delete Spawned Propsets", [] {
				for (int ps : g_propsets)
					PROPSET::_DELETE_PROP_SET(ps, TRUE, TRUE);
				g_propsets.clear();
			});
			for (const char* name : kPropsets)
			{
				const std::string n = name;
				Ui::Action(m, n, [n] { return SpawnPropset(n); });
			}
		});
		Ui::ListMenu(objects, "Search Objects", [](MenuBase* m) {
			std::string text;
			if (!GameUtil::PromptText("Search:", text) || text.empty())
				return;
			text = Lower(text);
			std::vector<std::string> names;
			for (const std::string& name : DataFile::LoadLines(L"Rampagio_ObjectList.txt"))
				if (Lower(name).find(text) != std::string::npos)
					names.push_back(name);
			for (const char* name : kObjects)
				if (std::string_view(name).find(text) != std::string_view::npos && ObjectAvailable(name))
					names.push_back(name);
			for (const std::string& n : names)
				Ui::Action(m, n, [n] { return SpawnObject(n); });
		});
		Ui::Action(objects, "objectspawner.custominput", "Custom Input", [] {
			std::string name;
			if (!GameUtil::PromptText("Enter Name or Hash:", name) || name.empty())
				return std::string();
			return SpawnObject(name);
		})->SetHotkeyable(false);
		Ui::Action(objects, "objectspawner.hijackobject", "Hijack Object", HijackObject)->SetHotkeyable(false);

		MenuBase* plants = Ui::Submenu(spawner, "Plant Spawner");
		for (const char* name : kComposites)
			Ui::Action(plants, Ui::Id("objectspawner.plant", name), name, [name] { return SpawnComposite(name); });
	}
}
