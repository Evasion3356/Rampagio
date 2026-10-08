/*
	Vehicle menu: ports Rampage's Submenus::SubVehicle, SubVehiclePv
	("Blip"), SubVehicleAI, SubVehicleChaffeur, SubVehiclePaintOptions,
	SubVehiclePropsets, SubVehicleExtras, SubTrainCreator and
	SubTrainWhistle. Propset and train config lists come from the game
	scripts (tools/extract_vehicles.py); Rampage keeps its own tables.
*/

#include "Menus.h"
#include "..\GameUtil.h"

#include <cmath>
#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	constexpr Hash INPUT_SPRINT = 0x5B9FD4E2;    // Rampage's boost key
	constexpr Hash INPUT_VEH_BRAKE = 0x6E1F639B; // stop / push back
	constexpr Hash INPUT_MOVE_UP_ONLY = 0x8FD015D8;
	constexpr Hash INPUT_MOVE_LEFT_ONLY = 0x7065027D;
	constexpr Hash INPUT_MOVE_DOWN_ONLY = 0xD27782E3;
	constexpr Hash INPUT_MOVE_RIGHT_ONLY = 0xB4E465B4;

	struct Propset
	{
		const char* name;
		bool light;
	};
	const Propset kPropsets[] = {
#include "..\data\VehiclePropsets.inc"
	};
	struct TrainConfig
	{
		Hash hash;
		const char* name;
	};
	const TrainConfig kTrainConfigs[] = {
#include "..\data\TrainConfigs.inc"
	};

	Vehicle CurrentVehicle()
	{
		const Ped ped = Me();
		return PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) : 0;
	}

	// Runs `fn` on every draft animal of the player's current wagon.
	template <typename Fn>
	void ForEachDraftPed(Fn fn)
	{
		const Vehicle v = CurrentVehicle();
		if (!v)
			return;
		const Hash model = ENTITY::GET_ENTITY_MODEL(v);
		if (!VEHICLE::_IS_THIS_MODEL_A_DRAFT_VEHICLE(model))
			return;
		const int count = VEHICLE::_GET_NUM_DRAFT_VEHICLE_HARNESS_PED(model);
		for (int i = 0; i <= count; i++)
			if (const Ped p = VEHICLE::_GET_PED_IN_DRAFT_HARNESS(v, i))
				fn(p);
	}

	// --- main rows ---------------------------------------------------------

	void SetInvincible(Vehicle v, bool on)
	{
		VEHICLE::SET_VEHICLE_CAN_BREAK(v, !on);
		VEHICLE::SET_VEHICLE_WHEELS_CAN_BREAK(v, !on);
		VEHICLE::SET_VEHICLE_WHEELS_CAN_BREAK_OFF_WHEN_BLOW_UP(v, !on);
		VEHICLE::SET_VEHICLE_EXPLODES_ON_HIGH_EXPLOSION_DAMAGE(v, !on);
		VEHICLE::SET_VEHICLE_HAS_STRONG_AXLES(v, on);
		VEHICLE::SET_VEHICLE_HAS_UNBREAKABLE_LIGHTS(v, on);
		VEHICLE::SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED(v, !on);
		VEHICLE::_SET_DRAFT_VEHICLE_ANIMALS_CAN_DETACH(v, !on);
		VEHICLE::_SET_DRAFT_VEHICLE_YOKE_CAN_BREAK(v, !on);
		VEHICLE::_SET_DRAFT_VEHICLE_ALLOW_DRAFT_ANIMAL_AUTO_CREATION(v, on);
		ENTITY::SET_ENTITY_CAN_BE_DAMAGED(v, !on);
		ENTITY::SET_ENTITY_INVINCIBLE(v, on);
	}

	// Ours: the vehicle setters are re-applied every frame, so a new vehicle
	// is covered too, and switching off restores the last one.
	Vehicle g_invincibleVehicle = 0;

	void InvincibleTick()
	{
		const Vehicle v = CurrentVehicle();
		if (v)
		{
			SetInvincible(v, true);
			g_invincibleVehicle = v;
		}
	}

	void InvincibleOff()
	{
		if (g_invincibleVehicle && ENTITY::DOES_ENTITY_EXIST(g_invincibleVehicle))
			SetInvincible(g_invincibleVehicle, false);
		g_invincibleVehicle = 0;
	}

	constexpr int kAlpha[] = { 0, 50, 120, 160, 255 };
	int g_vehicleOpacity = 4;
	int g_draftOpacity = 4;

	float g_flySpeed = 2.0f;

	// Moves the vehicle along the camera's facing, like Rampage's: WASD or
	// the move controls, with collision kept on.
	void FlyTick()
	{
		const Vehicle v = CurrentVehicle();
		if (!v)
			return;
		const Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
		const float pitch = rot.x * 3.14159265f / 180.0f;
		const float yaw = rot.z * 3.14159265f / 180.0f;
		const float c = std::fabs(std::cos(pitch));
		const float dx = -std::sin(yaw) * c, dy = std::cos(yaw) * c, dz = std::sin(pitch);
		Vector3 p = ENTITY::GET_ENTITY_COORDS(v, FALSE, FALSE);
		ENTITY::SET_ENTITY_COLLISION(v, TRUE, TRUE);
		ENTITY::SET_ENTITY_ROTATION(v, rot.x, rot.y, rot.z, 2, TRUE);
		const float s = g_flySpeed;
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_UP_ONLY))
		{
			p.x += dx * s;
			p.y += dy * s;
			p.z += dz * s;
		}
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_DOWN_ONLY))
		{
			p.x -= dx * s;
			p.y -= dy * s;
			p.z -= dz * s;
		}
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_LEFT_ONLY))
		{
			p.x -= dy * s;
			p.y += dx * s;
		}
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_RIGHT_ONLY))
		{
			p.x += dy * s;
			p.y -= dx * s;
		}
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(v, p.x, p.y, p.z, TRUE, TRUE, TRUE);
	}

	// An invisible frozen platform (the model Rampage uses) kept under the
	// vehicle while it's over water.
	constexpr Hash kWaterPlatform = 0xAF4F05AF;
	Object g_platform = 0;

	void DriveOnWaterTick()
	{
		const Ped ped = Me();
		const Entity e = PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) : ped;
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
		float water = 0.0f;
		const bool overWater = WATER::TEST_VERTICAL_PROBE_AGAINST_ALL_WATER(p.x, p.y, p.z, 0, &water) != 0;
		if (!overWater)
		{
			if (g_platform && ENTITY::DOES_ENTITY_EXIST(g_platform))
				ENTITY::SET_ENTITY_COORDS(g_platform, 0.0f, 0.0f, -1000.0f, FALSE, FALSE, FALSE, TRUE);
			return;
		}
		if (!g_platform || !ENTITY::DOES_ENTITY_EXIST(g_platform))
		{
			if (!GameUtil::LoadModel(kWaterPlatform))
				return;
			g_platform = OBJECT::CREATE_OBJECT(kWaterPlatform, p.x, p.y, water, TRUE, TRUE, FALSE, FALSE, FALSE);
			ENTITY::FREEZE_ENTITY_POSITION(g_platform, TRUE);
			ENTITY::SET_ENTITY_VISIBLE(g_platform, FALSE);
		}
		const Vector3 r = ENTITY::GET_ENTITY_ROTATION(e, 2);
		ENTITY::SET_ENTITY_COORDS(g_platform, p.x, p.y, water - 0.1f, FALSE, FALSE, FALSE, TRUE);
		ENTITY::SET_ENTITY_ROTATION(g_platform, 0.0f, 0.0f, r.z, 2, TRUE);
		// Lift the vehicle back on top if it sank below the surface.
		if (p.z < water)
			ENTITY::SET_ENTITY_COORDS(e, p.x, p.y, water + 0.5f, FALSE, FALSE, FALSE, TRUE);
	}

	void DriveOnWaterOff()
	{
		if (g_platform && ENTITY::DOES_ENTITY_EXIST(g_platform))
		{
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_platform, TRUE, TRUE);
			ENTITY::DELETE_ENTITY(&g_platform);
		}
		g_platform = 0;
	}

	// Rampage's numbers: sprint adds 0.45 to the forward speed each frame,
	// brake stops dead, holding brake pushes backwards.
	void BoostTick(bool train)
	{
		const Vehicle v = CurrentVehicle();
		if (!v)
			return;
		if (train && !VEHICLE::IS_THIS_MODEL_A_TRAIN(ENTITY::GET_ENTITY_MODEL(v)))
			return;
		auto setSpeed = [&](float s) { train ? VEHICLE::SET_TRAIN_SPEED(v, s) : VEHICLE::SET_VEHICLE_FORWARD_SPEED(v, s); };
		if (train)
			VEHICLE::_SET_TRAIN_MAX_SPEED(v, 200.0f); // Rampage writes this into the train directly
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_SPRINT))
			setSpeed(ENTITY::GET_ENTITY_SPEED_VECTOR(v, TRUE).y + 0.45f);
		if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_VEH_BRAKE))
			setSpeed(0.0f);
		else if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_VEH_BRAKE))
			ENTITY::APPLY_FORCE_TO_ENTITY(v, 1, 0.0f, -0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0, TRUE, TRUE, TRUE, FALSE, TRUE);
	}

	float g_groundForce = 0.4f;

	void StickToGroundTick()
	{
		const Ped ped = Me();
		if (!PED::IS_PED_SITTING_IN_ANY_VEHICLE(ped))
			return;
		const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(ped, FALSE);
		ENTITY::APPLY_FORCE_TO_ENTITY(v, 1, 0.0f, 0.0f, -g_groundForce, 0.0f, 0.0f, 0.0f, 1, TRUE, TRUE, TRUE, TRUE, TRUE);
	}

	// Config flag 207 is the one Rampage sets for flaming hooves.
	void SetFlamingHooves(bool on) { ForEachDraftPed([on](Ped p) { PED::SET_PED_CONFIG_FLAG(p, 207, on); }); }

	std::string Sitting(std::function<void(Vehicle)> fn)
	{
		const Ped ped = Me();
		if (!PED::IS_PED_SITTING_IN_ANY_VEHICLE(ped))
			return "Not in a vehicle";
		fn(PED::GET_VEHICLE_PED_IS_IN(ped, FALSE));
		return {};
	}

	std::string Flip()
	{
		const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(Me(), FALSE);
		if (!v)
			return "Not in a vehicle";
		const float roll = ENTITY::GET_ENTITY_ROLL(v);
		if (ENTITY::IS_ENTITY_UPSIDEDOWN(v) && (roll > 160.0f || roll < -160.0f)
			&& !ENTITY::IS_ENTITY_IN_AIR(v, 0) && !ENTITY::IS_ENTITY_IN_WATER(v))
		{
			const Vector3 r = ENTITY::GET_ENTITY_ROTATION(v, 2);
			ENTITY::SET_ENTITY_ROTATION(v, 0.0f, 0.0f, r.z, 2, TRUE);
			return {};
		}
		return "Not upside down";
	}

	std::string DeleteVehicle()
	{
		Vehicle v = PED::GET_VEHICLE_PED_IS_IN(Me(), FALSE);
		if (!v || !ENTITY::DOES_ENTITY_EXIST(v))
			return "Not in a vehicle";
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(v))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, TRUE, TRUE);
		VEHICLE::DELETE_VEHICLE(&v);
		return {};
	}

	// --- blip (SubVehiclePv) -------------------------------------------------

	constexpr Hash BLIP_STYLE_PLAYER_COACH = 0x25AB0484;
	Vehicle g_blipVehicle = 0;
	Blip g_blip = 0;

	bool BlipVehicleExists() { return g_blipVehicle && ENTITY::IS_ENTITY_A_VEHICLE(g_blipVehicle); }

	std::string AddBlip()
	{
		const Ped ped = Me();
		if (!PED::IS_PED_SITTING_IN_ANY_VEHICLE(ped))
			return "Not in a vehicle";
		if (g_blip && MAP::DOES_BLIP_EXIST(g_blip))
			MAP::REMOVE_BLIP(&g_blip);
		g_blipVehicle = PED::GET_VEHICLE_PED_IS_IN(ped, FALSE);
		g_blip = MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_PLAYER_COACH, g_blipVehicle);
		MAP::SET_BLIP_NAME_FROM_TEXT_FILE(g_blip, "BLIP_AMBIENT_COACH");
		return {};
	}

	std::string TeleportToBlipVehicle()
	{
		if (!BlipVehicleExists())
			return "No blipped vehicle";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(g_blipVehicle, TRUE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Me(), p.x, p.y, p.z, FALSE, FALSE, FALSE);
		PED::SET_PED_INTO_VEHICLE(Me(), g_blipVehicle, -1);
		return {};
	}

	std::string BlipVehicleToMe()
	{
		if (!BlipVehicleExists())
			return "No blipped vehicle";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(g_blipVehicle, p.x, p.y, p.z, FALSE, FALSE, FALSE);
		return {};
	}

	std::string DriveBlipVehicleToMe()
	{
		if (!BlipVehicleExists())
			return "No blipped vehicle";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		TASK::_TASK_VEHICLE_DRIVE_TO_DESTINATION_2(g_blipVehicle, p.x, p.y, p.z, 6.0f, 0x4200000, 3, 3.0f, 3.0f);
		return {};
	}

	// --- vehicle AI ------------------------------------------------------------

	float g_aiSpeed = 8.0f;
	constexpr int kDriveFlags = 0xC00A7; // Rampage's driving mode for the drive-to tasks

	void DriveSequence(Ped driver, Vehicle v, const Vector3& to, float speed)
	{
		int seq = 0;
		TASK::OPEN_SEQUENCE_TASK(&seq);
		TASK::TASK_VEHICLE_DRIVE_TO_COORD(0, v, to.x, to.y, to.z, speed, 0, ENTITY::GET_ENTITY_MODEL(v), kDriveFlags, 5.0f, 1.0f);
		TASK::CLOSE_SEQUENCE_TASK(seq);
		TASK::TASK_PERFORM_SEQUENCE(driver, seq);
		TASK::CLEAR_SEQUENCE_TASK(&seq);
	}

	bool WaypointGround(Vector3& out)
	{
		if (!MAP::IS_WAYPOINT_ACTIVE())
			return false;
		out = MAP::_GET_WAYPOINT_COORDS();
		float z = 0.0f;
		out.z = MISC::GET_GROUND_Z_FOR_3D_COORD(out.x, out.y, 1000.0f, &z, FALSE) ? z : 115.0f;
		return true;
	}

	Rampagio::BoolCommand* g_travelWaypoint = nullptr;

	void TravelToWaypoint(bool on)
	{
		const Ped ped = Me();
		TASK::CLEAR_PED_TASKS(ped, FALSE, FALSE);
		if (!on)
			return;
		Vector3 to;
		if (!WaypointGround(to) || !PED::IS_PED_SITTING_IN_ANY_VEHICLE(ped))
		{
			Ui::Controller().SetStatusText(PED::IS_PED_SITTING_IN_ANY_VEHICLE(ped) ? "Waypoint not active" : "Not in a vehicle", 2500);
			g_travelWaypoint->Sync(false);
			return;
		}
		DriveSequence(ped, PED::GET_VEHICLE_PED_IS_IN(ped, FALSE), to, g_aiSpeed);
	}

	void TravelAround(bool on)
	{
		const Ped ped = Me();
		TASK::CLEAR_PED_TASKS(ped, FALSE, FALSE);
		if (!on || !PED::IS_PED_SITTING_IN_ANY_VEHICLE(ped))
			return;
		TASK::TASK_VEHICLE_DRIVE_WANDER(ped, PED::GET_VEHICLE_PED_IS_IN(ped, FALSE), g_aiSpeed, 0);
	}

	// --- chauffeur ---------------------------------------------------------------

	constexpr Hash kCoachDriver = 0x76F815AD; // s_m_m_coachtaxidriver_01
	constexpr Hash REL_COMPANION_GROUP = 0xB5A1D680;
	float g_chauffeurSpeed = 6.0f;
	Vehicle g_chauffeurVehicle = 0;
	Ped g_chauffeur = 0;

	void DeleteChauffeur()
	{
		if (g_chauffeurVehicle && ENTITY::DOES_ENTITY_EXIST(g_chauffeurVehicle))
			VEHICLE::DELETE_VEHICLE(&g_chauffeurVehicle);
		if (g_chauffeur && ENTITY::DOES_ENTITY_EXIST(g_chauffeur))
			PED::DELETE_PED(&g_chauffeur);
		g_chauffeurVehicle = g_chauffeur = 0;
	}

	std::string DriveToWaypoint()
	{
		if (!g_chauffeur || !ENTITY::DOES_ENTITY_EXIST(g_chauffeur))
			return "No chauffeur";
		Vector3 to;
		if (!WaypointGround(to))
			return "Waypoint not active";
		DriveSequence(g_chauffeur, g_chauffeurVehicle, to, g_chauffeurSpeed);
		return {};
	}

	std::string SpawnChauffeur(const std::string& name)
	{
		const Hash model = GameUtil::ParseHash(name);
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_A_VEHICLE(model))
			return "Model is invalid.";
		if (!GameUtil::LoadModel(model) || !GameUtil::LoadModel(kCoachDriver))
			return "Failed to Load";
		DeleteChauffeur();
		const Ped ped = Me();
		Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ped, 0.0f, 5.0f, 0.0f);
		Vector3 road;
		if (MAP::_FIND_CLOSEST_GPS_POSITION(p.x, p.y, p.z, &road))
			p = road;
		g_chauffeurVehicle = VEHICLE::CREATE_VEHICLE(model, p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(ped), FALSE, FALSE, FALSE, FALSE);
		VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(g_chauffeurVehicle, FALSE);
		g_chauffeur = PED::CREATE_PED_INSIDE_VEHICLE(g_chauffeurVehicle, kCoachDriver, -1, FALSE, FALSE, FALSE);
		if (ENTITY::DOES_ENTITY_EXIST(g_chauffeur))
		{
			VEHICLE::SET_PED_OWNS_VEHICLE(g_chauffeur, g_chauffeurVehicle);
			PED::SET_PED_CONFIG_FLAG(g_chauffeur, 113, TRUE);
			PED::SET_PED_CONFIG_FLAG(g_chauffeur, 279, TRUE);
			PED::SET_PED_COMBAT_ATTRIBUTES(g_chauffeur, 17, TRUE);
			PED::SET_PED_COMBAT_ATTRIBUTES(g_chauffeur, 58, TRUE);
			PED::SET_PED_RELATIONSHIP_GROUP_HASH(g_chauffeur, REL_COMPANION_GROUP);
			PED::FORCE_PED_AI_AND_ANIMATION_UPDATE(g_chauffeur, TRUE, TRUE);
			PED::SET_PED_AS_GROUP_MEMBER(g_chauffeur, PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID()));
		}
		const Blip blip = MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_PLAYER_COACH, g_chauffeurVehicle);
		MAP::SET_BLIP_NAME_FROM_TEXT_FILE(blip, "BLIP_AMBIENT_COACH");
		// Into the first free passenger seat.
		const int seats = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(g_chauffeurVehicle);
		PED::SET_PED_INTO_VEHICLE(ped, g_chauffeurVehicle, seats > 0 ? 0 : -2);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(kCoachDriver);
		Vector3 to;
		if (WaypointGround(to))
			DriveSequence(g_chauffeur, g_chauffeurVehicle, to, g_chauffeurSpeed);
		return {};
	}

	// --- paint, propsets, extras ---------------------------------------------------

	int g_tint = 0;
	int g_livery = 0;

	void BuildPropsets(MenuBase* m)
	{
		const Vehicle v = PED::GET_VEHICLE_PED_IS_USING(Me());
		if (!v)
		{
			m->AddItem(new MenuItemLabel([] { return std::string("Not in a vehicle"); }));
			return;
		}
		Ui::Do(m, "Remove all", [v] {
			PROPSET::_REMOVE_VEHICLE_PROP_SETS(v);
			PROPSET::_REMOVE_VEHICLE_LIGHT_PROP_SETS(v);
		});
		for (bool lights : { false, true })
		{
			Ui::Section(m, lights ? "Lights" : "Propsets");
			for (const Propset& p : kPropsets)
			{
				if (p.light != lights)
					continue;
				const Hash h = GameUtil::Joaat(p.name);
				Ui::Do(m, p.name, [v, h, lights] {
					if (lights)
						PROPSET::_ADD_LIGHT_PROP_SET_TO_VEHICLE(v, h);
					else
						PROPSET::_ADD_PROP_SET_FOR_VEHICLE(v, h);
				});
			}
		}
	}

	void BuildExtras(MenuBase* m)
	{
		const Vehicle v = PED::GET_VEHICLE_PED_IS_USING(Me());
		int count = 0;
		for (int id = 0; v && id <= 12; id++)
		{
			if (!VEHICLE::DOES_EXTRA_EXIST(v, id))
				continue;
			count++;
			Ui::Toggle(m, std::format("Extra {}", id), [v, id](bool on) { VEHICLE::SET_VEHICLE_EXTRA(v, id, !on); })
				->SetState(VEHICLE::IS_VEHICLE_EXTRA_TURNED_ON(v, id));
		}
		if (!count)
			m->AddItem(new MenuItemLabel([] { return std::string("No extras"); }));
	}

	// --- trains ------------------------------------------------------------------------

	int g_trainConfig = 0;
	int g_trainDirection = 0;
	bool g_trainPassengers = false;
	bool g_trainAi = true;
	Vehicle g_train = 0;

	std::string TrainConfigLabel(const TrainConfig& c)
	{
		const int cars = VEHICLE::_GET_NUM_CARS_FROM_TRAIN_CONFIG(c.hash);
		return std::format("{} ({} cars)", *c.name ? c.name : std::format("0x{:08X}", c.hash), cars);
	}

	std::string SpawnTrain()
	{
		const Hash config = kTrainConfigs[g_trainConfig].hash;
		const int cars = VEHICLE::_GET_NUM_CARS_FROM_TRAIN_CONFIG(config);
		if (cars <= 0)
			return "Unknown train config";
		for (int i = 0; i < cars; i++)
			if (!GameUtil::LoadModel(VEHICLE::_GET_TRAIN_MODEL_FROM_TRAIN_CONFIG_BY_CAR_INDEX(config, i)))
				return "Failed to Load";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		g_train = VEHICLE::_CREATE_MISSION_TRAIN(config, p.x, p.y, p.z, g_trainDirection == 0, g_trainPassengers, TRUE, g_trainAi);
		VEHICLE::SET_VEHICLE_IS_CONSIDERED_BY_PLAYER(g_train, TRUE);
		const Blip blip = MAP::BLIP_ADD_FOR_ENTITY(0xE8302B3F, g_train);
		MAP::BLIP_ADD_MODIFIER(blip, 0x32850803);
		MAP::SET_BLIP_NAME_FROM_TEXT_FILE(blip, "BLIP_AMBIENT_TRAIN");
		if (!g_trainAi)
		{
			VEHICLE::SET_TRAIN_SPEED(g_train, 0.0f);
			VEHICLE::SET_TRAIN_CRUISE_SPEED(g_train, 0.0f);
		}
		return {};
	}

	bool TrainExists() { return g_train && ENTITY::DOES_ENTITY_EXIST(g_train); }

	std::string RotateTrain()
	{
		if (!TrainExists())
			return "No train";
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		VEHICLE::_SET_MISSION_TRAIN_WARP_TO_COORDS(g_train, p.x, p.y, p.z, !VEHICLE::_GET_TRAIN_DIRECTION(g_train));
		return {};
	}

	void Whistle(const char* sequence)
	{
		if (TrainExists())
			VEHICLE::_TRIGGER_TRAIN_WHISTLE(g_train, sequence, FALSE, FALSE);
	}

	void BuildTrainExtras(MenuBase* m)
	{
		if (!TrainExists())
		{
			m->AddItem(new MenuItemLabel([] { return std::string("No train"); }));
			return;
		}
		for (int car = 0; ; car++)
		{
			const Vehicle c = car == 0 ? g_train : VEHICLE::GET_TRAIN_CARRIAGE(g_train, car);
			if (!c || !ENTITY::DOES_ENTITY_EXIST(c))
				break;
			for (int id = 0; id <= 12; id++)
				if (VEHICLE::DOES_EXTRA_EXIST(c, id))
					Ui::Toggle(m, std::format("Carriage {} Extra {}", car, id), [c, id](bool on) { VEHICLE::SET_VEHICLE_EXTRA(c, id, !on); })
						->SetState(VEHICLE::IS_VEHICLE_EXTRA_TURNED_ON(c, id));
		}
	}
}

namespace Menus
{
	void BuildVehicle(MenuBase* root)
	{
		MenuBase* vehicle = Ui::Submenu(root, "Vehicle");

		MenuBase* blip = Ui::Submenu(vehicle, "Blip");
		Ui::Action(blip, "vehicle.addblip", "Add Blip", AddBlip);
		Ui::Action(blip, "vehicle.teleportto", "Teleport to", TeleportToBlipVehicle);
		Ui::Action(blip, "vehicle.teleporttome", "Teleport to Me", BlipVehicleToMe);
		Ui::Action(blip, "vehicle.drivetome", "Drive to Me", DriveBlipVehicleToMe);

		MenuBase* ai = Ui::Submenu(vehicle, "Vehicle AI");
		Ui::Number(ai, "vehicle.vehicleai.speed", "Speed", &g_aiSpeed, 3.0f, 14.0f, 1.0f, [] { TASK::SET_DRIVE_TASK_CRUISE_SPEED(Me(), g_aiSpeed); });
		// Drives (task sequences), not settings: not saved.
		g_travelWaypoint = Ui::Toggle(ai, "vehicle.traveltowaypoint", "Travel to Waypoint", TravelToWaypoint);
		g_travelWaypoint->SetTransient();
		Ui::Toggle(ai, "vehicle.travelaround", "Travel Around", TravelAround)->SetTransient();

		MenuBase* chauffeur = Ui::Submenu(vehicle, "Chauffeur");
		Ui::Number(chauffeur, "vehicle.chauffeur.speed", "Speed", &g_chauffeurSpeed, 3.0f, 14.0f, 1.0f, [] {
			if (g_chauffeur && ENTITY::DOES_ENTITY_EXIST(g_chauffeur))
				TASK::SET_DRIVE_TASK_CRUISE_SPEED(g_chauffeur, g_chauffeurSpeed);
		});
		Ui::Action(chauffeur, "vehicle.spawnchauffeur", "Spawn Chauffeur", [] { return SpawnChauffeur("stagecoach001x"); });
		Ui::Action(chauffeur, "vehicle.custominput", "Custom Input", [] {
			std::string name;
			if (!GameUtil::PromptText("Enter Vehicle Model:", name) || name.empty())
				return std::string();
			return SpawnChauffeur(name);
		})->SetHotkeyable(false);
		Ui::Action(chauffeur, "vehicle.drivetolocation", "Drive to Location", DriveToWaypoint);
		Ui::Do(chauffeur, "vehicle.deletechauffeur", "Delete Chauffeur", DeleteChauffeur);

		MenuBase* paint = Ui::Submenu(vehicle, "Paint Options");
		Ui::Number(paint, "vehicle.tint", "Tint", &g_tint, 0, 255, 1, [] {
			if (const Vehicle v = CurrentVehicle())
				VEHICLE::_SET_VEHICLE_TINT(v, g_tint);
		});
		Ui::Number(paint, "vehicle.livery", "Livery", &g_livery, 0, 255, 1, [] {
			if (const Vehicle v = CurrentVehicle())
				VEHICLE::_SET_VEHICLE_LIVERY(v, g_livery);
		});

		Ui::ListMenu(vehicle, "Propsets", BuildPropsets);
		Ui::ListMenu(vehicle, "Customization", BuildExtras);

		MenuBase* train = Ui::Submenu(vehicle, "Train Creator");
		std::vector<std::string> configs;
		for (const TrainConfig& c : kTrainConfigs)
			configs.push_back(*c.name ? c.name : std::format("Config 0x{:08X}", c.hash));
		Ui::Choice(train, "vehicle.configuration", "Configuration", configs, &g_trainConfig);
		train->AddItem(new MenuItemLabel([] { return "Cars: " + TrainConfigLabel(kTrainConfigs[g_trainConfig]); }));
		Ui::Choice(train, "vehicle.direction", "Direction", { "Forward", "Backward" }, &g_trainDirection);
		Ui::Toggle(train, "vehicle.trainpassengers", "Train Passengers", [](bool on) { g_trainPassengers = on; });
		Ui::Toggle(train, "vehicle.aicontrolled", "AI Controlled", [](bool on) { g_trainAi = on; })->SetDefault(g_trainAi);
		Ui::Action(train, "vehicle.spawn", "Spawn", SpawnTrain);
		Ui::Action(train, "vehicle.rotate", "Rotate", RotateTrain);
		Ui::Do(train, "vehicle.delete", "Delete", [] {
			if (TrainExists())
				VEHICLE::DELETE_MISSION_TRAIN(&g_train);
			g_train = 0;
		});
		MenuBase* whistle = Ui::Submenu(train, "Whistle");
		const char* const kWhistles[][2] = { { "Acknowledge", "ACKNOWLEDGE" }, { "Backing Up", "BACKING_UP" },
			{ "Crossing", "CROSSING" }, { "Danger", "DANGER" }, { "Moving", "MOVING" }, { "Next Station", "NEXT_STATION" },
			{ "Passing", "PASSING" }, { "Stopped", "STOPPED" } };
		for (auto& w : kWhistles)
		{
			const char* seq = w[1];
			Ui::Do(whistle, Ui::Id("vehicle.whistle", w[0]), w[0], [seq] { Whistle(seq); });
		}
		Ui::ListMenu(train, "Extras", BuildTrainExtras);

		Ui::Section(vehicle, "Toggles");
		Ui::Toggle(vehicle, "vehicle.invinciblevehicle", "Invincible Vehicle", [](bool on) { if (!on) InvincibleOff(); }, InvincibleTick);
		Ui::Toggle(vehicle, "vehicle.invisiblevehicle", "Invisible Vehicle", [](bool on) {
			if (const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(Me(), FALSE))
				ENTITY::SET_ENTITY_VISIBLE(v, !on);
		});
		Ui::Toggle(vehicle, "vehicle.invisibledraftpeds", "Invisible Draft Peds", [](bool on) { ForEachDraftPed([on](Ped p) { ENTITY::SET_ENTITY_VISIBLE(p, !on); }); });
		Ui::Choice(vehicle, "vehicle.vehicleopacity", "Vehicle Opacity", { "0%", "25%", "50%", "75%", "100%" }, &g_vehicleOpacity, [](int i) {
			if (const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(Me(), FALSE))
				ENTITY::SET_ENTITY_ALPHA(v, kAlpha[i], FALSE);
		});
		Ui::Choice(vehicle, "vehicle.draftpedsopacity", "Draft Peds Opacity", { "0%", "25%", "50%", "75%", "100%" }, &g_draftOpacity, [](int i) {
			ForEachDraftPed([i](Ped p) { ENTITY::SET_ENTITY_ALPHA(p, kAlpha[i], FALSE); });
		});
		Ui::Looped(vehicle, "vehicle.vehicleflymode", "Vehicle Fly Mode", FlyTick);
		Ui::Number(vehicle, "vehicle.flyspeed", "Fly Speed", &g_flySpeed, 1.0f, 8.0f, 1.0f);
		Ui::Looped(vehicle, "vehicle.driveonwater", "Drive On Water", DriveOnWaterTick, DriveOnWaterOff);
		Ui::Looped(vehicle, "vehicle.speedboost", "Speed Boost", [] { BoostTick(false); });
		Ui::Looped(vehicle, "vehicle.trainboost", "Train Boost", [] { BoostTick(true); });
		Ui::Looped(vehicle, "vehicle.sticktoground", "Stick to Ground", StickToGroundTick);
		Ui::Number(vehicle, "vehicle.groundforce", "Ground Force", &g_groundForce, 0.1f, 5.0f, 0.1f);
		Ui::Toggle(vehicle, "vehicle.draftpedsflaminghooves", "Draft Peds Flaming Hooves", SetFlamingHooves);

		Ui::Section(vehicle, "Actions");
		Ui::Action(vehicle, "vehicle.shuffleseat", "Shuffle Seat", [] { return Sitting([](Vehicle v) { TASK::TASK_SHUFFLE_TO_NEXT_VEHICLE_SEAT(Me(), v); }); });
		Ui::Action(vehicle, "vehicle.repair", "Repair", [] { return Sitting([](Vehicle v) { VEHICLE::SET_VEHICLE_FIXED(v); }); });
		Ui::Action(vehicle, "vehicle.clean", "Clean", [] {
			return Sitting([](Vehicle v) {
				VEHICLE::SET_VEHICLE_DIRT_LEVEL(v, 0.0f);
				VEHICLE::_SET_VEHICLE_MUD_LEVEL(v, 0.0f);
				VEHICLE::_SET_VEHICLE_SNOW_LEVEL(v, 0.0f);
				VEHICLE::_SET_VEHICLE_WET_LEVEL(v, 0.0f);
			});
		});
		Ui::Action(vehicle, "vehicle.flip", "Flip", Flip);
		Ui::Action(vehicle, "vehicle.stop", "Stop", [] { return Sitting([](Vehicle v) { VEHICLE::BRING_VEHICLE_TO_HALT(v, 10.5f, -1, FALSE); }); });
		Ui::Action(vehicle, "vehicle.detachwheels", "Detach Wheels", [] {
			const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(Me(), FALSE);
			if (!v)
				return std::string("Not in a vehicle");
			for (int wheel = 0; wheel < 4; wheel++)
				VEHICLE::_BREAK_OFF_VEHICLE_WHEEL(v, wheel);
			return std::string();
		});
		Ui::Action(vehicle, "vehicle.deletevehicle", "Delete Vehicle", DeleteVehicle);
	}
}
