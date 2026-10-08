/*
	Spawner > Object Spawner: ports Rampage's Submenus::SubObjectSpawner,
	SubObjSpawnerDatabase, SubObjSpawnerAllObjs, SubObjSpawnerPropsets,
	SubObjSpawnerLoadSave and SubPlantSpawner. Object, propset and herb
	composite names come from the game scripts (tools/extract_objects.py);
	All Objects also reads a user-supplied Rampagio_ObjectList.txt (same
	format as Rampage's Lists\ObjectList.txt).

	Load / Save writes Rampage's spooner database XML (Map > Placement,
	Vehicle, Ped) into a Rampagio_Spooner folder, with the Ped Spawner's
	database and the spawned vehicles (Menus::SpawnerDb), and loads those
	files and Rampage's own (RampageFiles\Spooner). Sets saved by earlier
	builds in Rampagio_Spooner.json still load. The Object Editor (moving and rotating a
	selected object) is tabled with the rest of that area.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"
#include "..\Xml.h"

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

	// --- save / load -----------------------------------------------------------------

	const wchar_t* kSpoonerFile = L"Rampagio_Spooner.json"; // sets from earlier builds
	constexpr wchar_t kSpoonerFolder[] = L"Rampagio_Spooner";
	bool g_addToDatabase = true;

	std::string Hex(Hash h) { return std::format("0x{:X}", h); }
	std::string Bool(bool b) { return b ? "true" : "false"; }

	std::wstring SpoonerPath(const std::string& name)
	{
		return std::wstring(kSpoonerFolder) + L"\\" + std::wstring(name.begin(), name.end()) + L".xml";
	}

	// Rampage's PositionRotation: X, Y, Z, Pitch, Roll, Yaw (rotation order 2).
	void AddPositionRotation(Xml::Node& parent, Entity e)
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, FALSE, FALSE);
		const Vector3 r = ENTITY::GET_ENTITY_ROTATION(e, 2);
		Xml::Node& pr = parent.Add("PositionRotation");
		pr.Add("X", std::format("{}", p.x));
		pr.Add("Y", std::format("{}", p.y));
		pr.Add("Z", std::format("{}", p.z));
		pr.Add("Pitch", std::format("{}", r.x));
		pr.Add("Roll", std::format("{}", r.y));
		pr.Add("Yaw", std::format("{}", r.z));
	}

	Vector3 PositionOf(const Xml::Node* pr)
	{
		return pr ? Vector3{ pr->Float("X"), pr->Float("Y"), pr->Float("Z") } : Vector3{};
	}

	void ApplyPositionRotation(Entity e, const Xml::Node* pr)
	{
		if (!pr)
			return;
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, pr->Float("X"), pr->Float("Y"), pr->Float("Z"), FALSE, FALSE, FALSE);
		ENTITY::SET_ENTITY_ROTATION(e, pr->Float("Pitch"), pr->Float("Roll"), pr->Float("Yaw"), 2, TRUE);
	}

	// Outfit components Rampage leaves out of a saved ped (the body itself).
	bool IsBodyCategory(Hash category)
	{
		for (const char* c : { "heads", "hair", "teeth", "eyes", "bodies_upper", "beards_chin", "beards_chops", "beards_mustache", "beards_complete" })
			if (category == GameUtil::Joaat(c))
				return true;
		return false;
	}

	void AddPed(Xml::Node& map, const Menus::SpawnerDb::Entry& entry)
	{
		const Ped ped = entry.entity;
		Xml::Node& n = map.Add("Ped");
		n.Add("ModelHash", Hex(ENTITY::GET_ENTITY_MODEL(ped)));
		n.Add("HashName", entry.model);
		n.Add("InitialHandle", std::to_string(ped));
		n.Add("Health", std::to_string(ENTITY::GET_ENTITY_HEALTH(ped)));
		n.Add("Variation", "0");
		n.Add("Scenario");
		AddPositionRotation(n, ped);
		Xml::Node& flags = n.Add("Flags");
		flags.Add("Invincible", Bool(!ENTITY::_GET_ENTITY_CAN_BE_DAMAGED(ped)));
		flags.Add("Frozen", Bool(ENTITY::_IS_ENTITY_FROZEN(ped)));
		const std::vector<Ped>& posse = Menus::Posse::Members();
		flags.Add("Bodyguard", Bool(std::find(posse.begin(), posse.end(), ped) != posse.end()));
		flags.Add("Relationship", Hex(PED::GET_PED_RELATIONSHIP_GROUP_HASH(ped)));
		flags.Add("Interactable", "false");
		Xml::Node& weapons = n.Add("Weapons");
		weapons.Add("Weapon", Hex(WEAPON::_GET_PED_CURRENT_HELD_WEAPON(ped)));
		weapons.Add("Accuracy", std::to_string(PED::GET_PED_ACCURACY(ped)));
		Xml::Node& outfit = n.Add("Outfit");
		const int count = PED::_GET_NUM_COMPONENTS_IN_PED(ped);
		for (int i = 0; i < count; i++)
		{
			if (IsBodyCategory(PED::_GET_CATEGORY_OF_COMPONENT_AT_INDEX(ped, i, 0)))
				continue;
			BOOL flag = FALSE;
			Hash state = 0;
			if (const Hash item = PED::_GET_SHOP_ITEM_COMPONENT_AT_INDEX(ped, i, TRUE, &flag, &state))
				outfit.Add("LoadComponent").Add("Hash", Hex(item));
		}
		Xml::Node& tags = n.Add("MetaTags");
		for (int i = 0; i < count; i++)
		{
			Hash drawable = 0, albedo = 0, normal = 0, material = 0, palette = 0;
			int tint0 = 0, tint1 = 0, tint2 = 0;
			PED::GET_META_PED_ASSET_GUIDS(ped, i, &drawable, &albedo, &normal, &material);
			PED::GET_META_PED_ASSET_TINT(ped, i, &palette, &tint0, &tint1, &tint2);
			Xml::Node& tag = tags.Add("MetaTag");
			tag.Add("Drawable", Hex(drawable));
			tag.Add("Albedo", Hex(albedo));
			tag.Add("Normal", Hex(normal));
			tag.Add("Material", Hex(material));
			tag.Add("Palette", Hex(palette));
			tag.Add("PrimaryColor", std::to_string(tint0));
			tag.Add("SecondaryColor", std::to_string(tint1));
			tag.Add("TertiaryColor", std::to_string(tint2));
		}
	}

	std::string SaveSet()
	{
		std::string name;
		if (!GameUtil::PromptText("Set name", name, 40) || name.empty())
			return {};
		Xml::Node map;
		map.name = "Map";
		Xml::Node& meta = map.Add("MapMeta");
		meta.Add("Creator", PLAYER::GET_PLAYER_NAME(PLAYER::PLAYER_ID()));
		meta.Add("RampageVersion", "Rampagio");
		int count = 0;
		for (const SpawnedObject& s : g_objects)
		{
			if (!ENTITY::DOES_ENTITY_EXIST(s.object))
				continue;
			Xml::Node& n = map.Add("Placement");
			n.Add("ModelHash", Hex(ENTITY::GET_ENTITY_MODEL(s.object)));
			n.Add("HashName", s.model);
			n.Add("InitialHandle", std::to_string(s.object));
			n.Add("Texture", "0");
			n.Add("LOD", std::to_string(ENTITY::GET_ENTITY_LOD_DIST(s.object)));
			n.Add("Dynamic", "false");
			n.Add("Frozen", Bool(ENTITY::_IS_ENTITY_FROZEN(s.object)));
			AddPositionRotation(n, s.object);
			count++;
		}
		for (const Menus::SpawnerDb::Entry& v : Menus::SpawnerDb::Vehicles())
		{
			Xml::Node& n = map.Add("Vehicle");
			n.Add("ModelHash", Hex(ENTITY::GET_ENTITY_MODEL(v.entity)));
			n.Add("HashName", v.model);
			n.Add("InitialHandle", std::to_string(v.entity));
			n.Add("Tint", std::to_string(VEHICLE::_GET_VEHICLE_TINT(v.entity)));
			n.Add("Livery", std::to_string(VEHICLE::_GET_VEHICLE_LIVERY(v.entity)));
			AddPositionRotation(n, v.entity);
			count++;
		}
		for (const Menus::SpawnerDb::Entry& p : Menus::SpawnerDb::Peds())
		{
			AddPed(map, p);
			count++;
		}
		return DataFile::SaveText(SpoonerPath(name), Xml::Write(map)) ? std::format("Saved database with {} entities", count) : "Couldn't save";
	}

	void LoadPed(const Xml::Node& n)
	{
		const Hash model = GameUtil::ParseHash(n.Text("ModelHash"));
		if (!GameUtil::LoadModel(model))
			return;
		const Xml::Node* pr = n.Child("PositionRotation");
		const Vector3 at = PositionOf(pr);
		const Ped ped = PED::CREATE_PED(model, at.x, at.y, at.z, pr ? pr->Float("Yaw") : 0.0f, FALSE, FALSE, FALSE, FALSE);
		PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
		ApplyPositionRotation(ped, pr);
		if (n.Child("Health"))
			ENTITY::SET_ENTITY_HEALTH(ped, n.Int("Health"), 0);
		if (const int variation = n.Int("Variation"))
			PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, variation, FALSE);
		if (const Xml::Node* flags = n.Child("Flags"))
		{
			ENTITY::SET_ENTITY_INVINCIBLE(ped, flags->Bool("Invincible"));
			ENTITY::FREEZE_ENTITY_POSITION(ped, flags->Bool("Frozen"));
			if (const Hash group = GameUtil::ParseHash(flags->Text("Relationship")))
				PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, group);
			if (flags->Bool("Bodyguard"))
				Menus::Posse::Add(ped);
		}
		if (const Xml::Node* weapons = n.Child("Weapons"))
		{
			for (const Xml::Node* w : weapons->Children("Weapon"))
				if (const Hash weapon = GameUtil::ParseHash(w->text); weapon && weapon != GameUtil::Joaat("WEAPON_UNARMED"))
					WEAPON::GIVE_DELAYED_WEAPON_TO_PED(ped, weapon, 100, TRUE, 0);
			if (weapons->Child("Accuracy"))
				PED::SET_PED_ACCURACY(ped, weapons->Int("Accuracy"));
		}
		if (const Xml::Node* outfit = n.Child("Outfit"))
			for (const Xml::Node* c : outfit->Children("LoadComponent"))
				if (const Hash item = GameUtil::ParseHash(c->Text("Hash")))
					PED::_APPLY_SHOP_ITEM_TO_PED(ped, item, TRUE, FALSE, FALSE);
		if (const Xml::Node* tags = n.Child("MetaTags"))
			for (const Xml::Node* t : tags->Children("MetaTag"))
				if (const Hash drawable = GameUtil::ParseHash(t->Text("Drawable")))
					PED::_SET_META_PED_TAG(ped, drawable, GameUtil::ParseHash(t->Text("Albedo")), GameUtil::ParseHash(t->Text("Normal")),
						GameUtil::ParseHash(t->Text("Material")), GameUtil::ParseHash(t->Text("Palette")),
						t->Int("PrimaryColor"), t->Int("SecondaryColor"), t->Int("TertiaryColor"));
		for (int i = 0; i < 100 && !PED::IS_PED_READY_TO_RENDER(ped); i++)
			WAIT(0);
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
		if (g_addToDatabase)
			Menus::SpawnerDb::AddPed(ped, n.Text("HashName", n.Text("ModelHash")));
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	}

	std::string LoadXml(const std::string& name)
	{
		Xml::Node map;
		if (!Xml::Parse(DataFile::LoadText(SpoonerPath(name)), map) || map.name != "Map")
			return std::format("{}.xml isn't a spooner database", name);
		CAMERA::DO_SCREEN_FADE_OUT(500);
		WAIT(500);
		int count = 0;
		for (const Xml::Node* n : map.Children("Placement"))
		{
			const Hash model = GameUtil::ParseHash(n->Text("ModelHash"));
			if (!GameUtil::LoadModel(model))
				continue;
			const Xml::Node* pr = n->Child("PositionRotation");
			const Vector3 at = PositionOf(pr);
			const Object o = OBJECT::CREATE_OBJECT_NO_OFFSET(model, at.x, at.y, at.z, FALSE, FALSE, n->Bool("Dynamic"), FALSE);
			ApplyPositionRotation(o, pr);
			if (const int texture = n->Int("Texture"))
				OBJECT::SET_OBJECT_TINT_INDEX(o, texture);
			if (const int lod = n->Int("LOD"))
				ENTITY::SET_ENTITY_LOD_DIST(o, lod);
			ENTITY::FREEZE_ENTITY_POSITION(o, n->Bool("Frozen", true));
			if (g_addToDatabase)
				g_objects.push_back({ o, n->Text("HashName", n->Text("ModelHash")) });
			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			count++;
		}
		for (const Xml::Node* n : map.Children("Vehicle"))
		{
			const Hash model = GameUtil::ParseHash(n->Text("ModelHash"));
			if (!GameUtil::LoadModel(model))
				continue;
			const Xml::Node* pr = n->Child("PositionRotation");
			const Vector3 at = PositionOf(pr);
			const Vehicle v = VEHICLE::CREATE_VEHICLE(model, at.x, at.y, at.z, pr ? pr->Float("Yaw") : 0.0f, FALSE, FALSE, FALSE, FALSE);
			ApplyPositionRotation(v, pr);
			VEHICLE::_SET_VEHICLE_TINT(v, n->Int("Tint"));
			VEHICLE::_SET_VEHICLE_LIVERY(v, n->Int("Livery"));
			if (g_addToDatabase)
				Menus::SpawnerDb::AddVehicle(v, n->Text("HashName", n->Text("ModelHash")));
			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			count++;
		}
		for (const Xml::Node* n : map.Children("Ped"))
		{
			LoadPed(*n);
			count++;
		}
		CAMERA::DO_SCREEN_FADE_IN(500);
		const Xml::Node* meta = map.Child("MapMeta");
		return std::format("Loaded {} by {} ({} entities)", name, meta ? meta->Text("Creator", "?") : "?", count);
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
		const std::vector<std::string> files = DataFile::ListFiles(kSpoonerFolder, L".xml");
		for (const std::string& name : files)
			Ui::Action(m, name, [name] { return LoadXml(name); });
		const nlohmann::json saved = DataFile::LoadJson(kSpoonerFile);
		if (files.empty() && saved.empty())
			m->AddItem(new MenuItemLabel([] { return std::string("No files found"); }));
		for (const auto& [name, set] : saved.items())
		{
			const std::string n = name;
			Ui::Action(m, n + " (json)", [n] { return LoadSet(n); });
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
