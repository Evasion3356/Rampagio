/*
	Horse menu: ports Rampage's Submenus::SubSelfHorse rows. Rampage acts on
	the current mount for most rows and on its tracked "primary horse" for
	the rest; we use GameUtil::PlayerHorse() (mount, else last mount, else
	saddle horse) for both.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\keyboard.h"

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
		return "Renamed to " + name;
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
}

namespace Menus
{
	void BuildHorse(MenuBase* root)
	{
		MenuBase* horse = Ui::Submenu(root, "Horse");

		Ui::Section(horse, "Toggles");
		Ui::Looped(horse, "Invincible", [] { g_invincible.Tick(SetInvincible); }, [] { g_invincible.Off(SetInvincible); });
		Ui::Looped(horse, "Invisible", [] { g_invisible.Tick(SetInvisible); }, [] { g_invisible.Off(SetInvisible); });
		Ui::Looped(horse, "Stamina Never Drain", [] { OnMount([](Ped m) { PED::_RESTORE_PED_STAMINA(m, 100.0f); }); });
		Ui::Looped(horse, "Never Ragdoll", [] { g_noRagdoll.Tick(SetNoRagdoll); }, [] { g_noRagdoll.Off(SetNoRagdoll); });
		Ui::Number(horse, "Horse Scale", &g_scale, 0.1f, 10.0f, 0.05f, ApplyScale);
		Ui::Looped(horse, "Super Speed", SuperSpeedTick, SuperSpeedOff);
		Ui::Number(horse, "Super Speed Force", &g_superSpeed, 5.0f, 200.0f, 5.0f);
		Ui::Looped(horse, "Horse Fly Mode", FlyTick);
		Ui::Number(horse, "Fly Speed", &g_flySpeed, 0.1f, 10.0f, 0.1f);
		Ui::Looped(horse, "Horse Cores Never Drain", [] { OnMount(FillCores); });
		Ui::Looped(horse, "Horse Always Calm", [] { g_calm.Tick(SetCalm); }, [] { g_calm.Off(SetCalm); });
		Ui::Looped(horse, "Flaming Hooves", [] { g_hooves.Tick(SetFlamingHooves); }, [] { g_hooves.Off(SetFlamingHooves); });
		Ui::Toggle(horse, "Mount Cover", [](bool on) { PED::SET_PED_CONFIG_FLAG(Me(), 560, on); });
		Ui::Looped(horse, "Side Saddle Riding", SideSaddleTick, SideSaddleOff);
		Ui::Looped(horse, "Always Clean", [] { OnMount([](Ped m) { PED::CLEAR_PED_ENV_DIRT(m); }); });
		Ui::Looped(horse, "Horse Teleport Whistle", WhistleTick);

		Ui::Section(horse, "Actions");
		Ui::Do(horse, "Fill Horse Cores", [] { OnMount(FillHorseCores); });
		Ui::Action(horse, "Cores Overpower", CoresOverpower);
		Ui::Do(horse, "Quick Boost", [] { PLAYER::BOOST_PLAYER_HORSE_SPEED_FOR_TIME(PLAYER::PLAYER_ID(), 10000.0f, 10000); });
		Ui::Do(horse, "Quick Stop", [] { OnMount([](Ped m) { TASK::TASK_HORSE_ACTION(m, 3, 0, 0); }); });
		Ui::Do(horse, "Heal", [] { OnMount(HealHorse); });
		Ui::Do(horse, "Clean", [] { OnMount([](Ped m) { PED::CLEAR_PED_ENV_DIRT(m); }); });
		Ui::Do(horse, "Clone", [] { OnMount([](Ped m) { PED::CLONE_PED(m, TRUE, TRUE, TRUE); }); });
		Ui::Action(horse, "Rename", Rename);
		Ui::Do(horse, "Ragdoll", Ragdoll);
		Ui::Action(horse, "Set As Primary Horse", SetAsPrimary);
		Ui::Do(horse, "Remove Pelts", [] { OnMount(RemovePelts); });
		Ui::Do(horse, "Kill", [] { OnMount([](Ped m) { ENTITY::SET_ENTITY_HEALTH(m, 0, 0); }); });
		Ui::Do(horse, "Delete", [] { OnMount(DeleteHorse); });
		Ui::Action(horse, "Teleport Horse to Me", TeleportHorseToMe);
		Ui::Action(horse, "Teleport to Horse", TeleportToHorse);
		Ui::Action(horse, "Play Shitting Animation", [] { return PlayAnim("creatures_mammal@horse@normal@idle@idle_variation@shitting", "idle_transition"); });
		Ui::Action(horse, "Play Injured Animation", [] { return PlayAnim("creatures_mammal@horse@injured_critical@canter@slope@right", "stop_l"); });
		Ui::Action(horse, "Revive Horse", Revive);
		Ui::Do(horse, "View Horse Cargo", [] { UIAPPS::LAUNCH_UIAPP_BY_HASH(0xFFC21415); });
	}
}
