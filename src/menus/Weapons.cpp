/*
	Weapon menu: ports Rampage's Submenus::SubWeapons, SubWeaponsManage,
	SubWeaponsAmmunition, SubWeaponsGive and SubWeaponModifiers rows. The
	weapon and ammo lists are the game's item names (filtered through
	IS_WEAPON_VALID at runtime), not Rampage's tables.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\keyboard.h"

#include <cmath>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Player MyPlayer() { return PLAYER::PLAYER_ID(); }

	constexpr Hash ADD_REASON_DEFAULT = 0x2CD419DC;
	constexpr Hash REMOVE_REASON_DEFAULT = 0xF77DE93D;
	constexpr Hash INPUT_ATTACK = 0x6FED71BC;   // read from group 2, as Rampage does
	constexpr Hash INPUT_AIM = 0x51104035;
	constexpr Hash INPUT_SNIPER_ZOOM_IN = 0x81457A1A;
	constexpr Hash INPUT_SNIPER_ZOOM_OUT = 0x9DA42644;
	constexpr int ARMED_ANY_GUN = 4;   // IS_PED_ARMED flag: guns only
	constexpr int ARMED_ANY = 7;

	const char* const kWeapons[] = {
		"WEAPON_REVOLVER_CATTLEMAN", "WEAPON_REVOLVER_CATTLEMAN_JOHN", "WEAPON_REVOLVER_CATTLEMAN_MEXICAN",
		"WEAPON_REVOLVER_DOUBLEACTION", "WEAPON_REVOLVER_DOUBLEACTION_GAMBLER", "WEAPON_REVOLVER_DOUBLEACTION_MICAH",
		"WEAPON_REVOLVER_DOUBLEACTION_EXOTIC", "WEAPON_REVOLVER_LEMAT", "WEAPON_REVOLVER_SCHOFIELD",
		"WEAPON_REVOLVER_SCHOFIELD_CALLOWAY", "WEAPON_REVOLVER_SCHOFIELD_GOLDEN", "WEAPON_REVOLVER_NAVY",
		"WEAPON_PISTOL_VOLCANIC", "WEAPON_PISTOL_SEMIAUTO", "WEAPON_PISTOL_MAUSER", "WEAPON_PISTOL_MAUSER_DRUNK",
		"WEAPON_PISTOL_M1899",
		"WEAPON_REPEATER_CARBINE", "WEAPON_REPEATER_WINCHESTER", "WEAPON_REPEATER_HENRY", "WEAPON_REPEATER_EVANS",
		"WEAPON_RIFLE_VARMINT", "WEAPON_RIFLE_SPRINGFIELD", "WEAPON_RIFLE_BOLTACTION", "WEAPON_RIFLE_ELEPHANT",
		"WEAPON_SNIPERRIFLE_ROLLINGBLOCK", "WEAPON_SNIPERRIFLE_ROLLINGBLOCK_EXOTIC", "WEAPON_SNIPERRIFLE_CARCANO",
		"WEAPON_SHOTGUN_DOUBLEBARREL", "WEAPON_SHOTGUN_DOUBLEBARREL_EXOTIC", "WEAPON_SHOTGUN_SAWEDOFF",
		"WEAPON_SHOTGUN_PUMP", "WEAPON_SHOTGUN_REPEATING", "WEAPON_SHOTGUN_SEMIAUTO",
		"WEAPON_BOW", "WEAPON_BOW_IMPROVED",
		"WEAPON_MELEE_KNIFE", "WEAPON_MELEE_KNIFE_JAWBONE", "WEAPON_MELEE_MACHETE", "WEAPON_MELEE_HATCHET",
		"WEAPON_MELEE_CLEAVER", "WEAPON_MELEE_ANCIENT_HATCHET", "WEAPON_MELEE_LANTERN", "WEAPON_MELEE_DAVY_LANTERN",
		"WEAPON_MELEE_TORCH",
		"WEAPON_THROWN_THROWING_KNIVES", "WEAPON_THROWN_TOMAHAWK", "WEAPON_THROWN_TOMAHAWK_ANCIENT",
		"WEAPON_THROWN_DYNAMITE", "WEAPON_THROWN_MOLOTOV", "WEAPON_THROWN_POISONBOTTLE", "WEAPON_THROWN_BOLAS",
		"WEAPON_LASSO", "WEAPON_LASSO_REINFORCED", "WEAPON_FISHINGROD", "WEAPON_KIT_BINOCULARS", "WEAPON_KIT_CAMERA",
	};

	const char* const kAmmo[] = {
		"AMMO_REVOLVER", "AMMO_REVOLVER_EXPRESS", "AMMO_REVOLVER_EXPRESS_EXPLOSIVE", "AMMO_REVOLVER_HIGH_VELOCITY",
		"AMMO_REVOLVER_SPLIT_POINT", "AMMO_PISTOL", "AMMO_PISTOL_EXPRESS", "AMMO_PISTOL_EXPRESS_EXPLOSIVE",
		"AMMO_PISTOL_HIGH_VELOCITY", "AMMO_PISTOL_SPLIT_POINT", "AMMO_REPEATER", "AMMO_REPEATER_EXPRESS",
		"AMMO_REPEATER_EXPRESS_EXPLOSIVE", "AMMO_REPEATER_HIGH_VELOCITY", "AMMO_REPEATER_SPLIT_POINT",
		"AMMO_RIFLE", "AMMO_RIFLE_EXPRESS", "AMMO_RIFLE_EXPRESS_EXPLOSIVE", "AMMO_RIFLE_HIGH_VELOCITY",
		"AMMO_RIFLE_SPLIT_POINT", "AMMO_RIFLE_VARMINT", "AMMO_RIFLE_ELEPHANT", "AMMO_22", "AMMO_SHOTGUN",
		"AMMO_SHOTGUN_BUCKSHOT_INCENDIARY", "AMMO_SHOTGUN_SLUG", "AMMO_SHOTGUN_EXPRESS_EXPLOSIVE",
		"AMMO_ARROW", "AMMO_ARROW_DYNAMITE", "AMMO_ARROW_FIRE", "AMMO_ARROW_IMPROVED", "AMMO_ARROW_POISON",
		"AMMO_ARROW_SMALL_GAME", "AMMO_DYNAMITE", "AMMO_DYNAMITE_VOLATILE", "AMMO_MOLOTOV", "AMMO_MOLOTOV_VOLATILE",
		"AMMO_THROWING_KNIVES", "AMMO_TOMAHAWK", "AMMO_POISONBOTTLE", "AMMO_BOLAS",
	};

	Hash CurrentWeapon()
	{
		Hash weapon = 0;
		WEAPON::GET_CURRENT_PED_WEAPON(Me(), &weapon, TRUE, 0, FALSE);
		return weapon;
	}

	// Where the camera is looking, `distance` metres out.
	Vector3 CamTarget(float distance)
	{
		const Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
		const Vector3 from = CAMERA::GET_GAMEPLAY_CAM_COORD();
		const float yaw = rot.z * 0.0174532925f, pitch = rot.x * 0.0174532925f;
		Vector3 to = from;
		to.x += -sinf(yaw) * cosf(pitch) * distance;
		to.y += cosf(yaw) * cosf(pitch) * distance;
		to.z += sinf(pitch) * distance;
		return to;
	}

	Vector3 CamDirection()
	{
		const Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
		const float yaw = rot.z * 0.0174532925f, pitch = rot.x * 0.0174532925f;
		Vector3 d{};
		d.x = -sinf(yaw) * cosf(pitch);
		d.y = cosf(yaw) * cosf(pitch);
		d.z = sinf(pitch);
		return d;
	}

	bool AttackPressed()
	{
		return IsKeyDown(VK_LBUTTON) || PAD::IS_DISABLED_CONTROL_PRESSED(2, INPUT_ATTACK);
	}

	// True on the frame the player fired a gun, with where the shot landed.
	bool ShotImpact(Vector3& at)
	{
		const Ped ped = Me();
		return PED::IS_PED_SHOOTING(ped) && WEAPON::IS_PED_ARMED(ped, ARMED_ANY_GUN)
			&& WEAPON::GET_PED_LAST_WEAPON_IMPACT_COORD(ped, &at);
	}

	// The entity the player is free-aiming at, or 0.
	Entity AimedEntity()
	{
		Entity e = 0;
		if (PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(MyPlayer(), &e) && ENTITY::DOES_ENTITY_EXIST(e))
			return e;
		return 0;
	}

	void DeleteEntity(Entity e)
	{
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(e))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, TRUE, TRUE);
		ENTITY::DELETE_ENTITY(&e);
	}

	// --- toggles --------------------------------------------------------

	void SlowMoAimTick()
	{
		const bool aiming = PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer()) && WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN);
		MISC::SET_TIME_SCALE(aiming ? 0.3f : 1.0f);
	}

	// Rampage keeps first person off unless aiming; switch to first person
	// before turning it on.
	void FirstPersonAimTick()
	{
		if (!PAD::IS_CONTROL_PRESSED(0, 0xF84FA74F) || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN))
			CAMERA::DISABLE_ON_FOOT_FIRST_PERSON_VIEW_THIS_UPDATE();
	}

	// Rapid fire: firing is disabled and bullets are spawned straight down
	// the camera line every frame the trigger is held. Tenfold adds two
	// more lines spread a little apart.
	void SpawnBullets(int lines)
	{
		const Ped ped = Me();
		if (PED::IS_PED_IN_ANY_VEHICLE(ped, TRUE) || !WEAPON::IS_PED_ARMED(ped, ARMED_ANY))
			return;
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		if (!AttackPressed())
			return;
		const Vector3 from = CAMERA::GET_GAMEPLAY_CAM_COORD();
		const Vector3 dir = CamDirection();
		const Hash weapon = CurrentWeapon();
		for (int i = 0; i < lines; i++)
		{
			const float spread = (i - (lines - 1) / 2.0f) * 0.05f;
			const float sx = from.x + dir.x * 2.0f, sy = from.y + dir.y * 2.0f, sz = from.z + dir.z * 2.0f;
			MISC::SHOOT_SINGLE_BULLET_BETWEEN_COORDS(sx, sy, sz,
				from.x + (dir.x + spread * dir.y) * 1000.0f, from.y + (dir.y - spread * dir.x) * 1000.0f, from.z + dir.z * 1000.0f,
				100, TRUE, weapon, ped, TRUE, TRUE, -1.0f, FALSE);
		}
	}

	bool g_noReload = false;
	void RapidGunTick()
	{
		const Ped ped = Me();
		if (PED::IS_PED_IN_ANY_VEHICLE(ped, TRUE) || !WEAPON::IS_PED_ARMED(ped, ARMED_ANY))
			return;
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		WEAPON::SET_PED_INFINITE_AMMO(ped, TRUE, CurrentWeapon());
		WEAPON::_SET_PED_INFINITE_AMMO_CLIP(ped, TRUE);
		if (AttackPressed())
			PED::_SHOOT_TRIGGER_AT_COORDS(ped, 0.0f, 0.0f, 0.0f, 1, -1.0f, 0, -1.0f);
	}

	void RapidGunOff()
	{
		WEAPON::SET_PED_INFINITE_AMMO(Me(), g_noReload, CurrentWeapon());
		WEAPON::_SET_PED_INFINITE_AMMO_CLIP(Me(), g_noReload);
	}

	void AutoCockTick()
	{
		if (!PED::IS_PED_RELOADING(Me()))
			WEAPON::_SET_FORCE_CURRENT_WEAPON_INTO_COCKED_STATE(Me(), 0);
	}

	void ImpactExplosion(int type)
	{
		Vector3 at;
		if (ShotImpact(at))
			FIRE::ADD_OWNED_EXPLOSION(Me(), at.x, at.y, at.z, type, 1.0f, TRUE, FALSE, 0.0f);
	}

	// Rampage tops up a fixed list of ammo types after every shot; ours
	// tops up the type the current weapon uses.
	void InfiniteAmmoTick()
	{
		const Ped ped = Me();
		if (PED::IS_PED_SHOOTING(ped))
			WEAPON::_ADD_AMMO_TO_PED_BY_TYPE(ped, WEAPON::GET_PED_AMMO_TYPE_FROM_WEAPON(ped, CurrentWeapon()), 400, ADD_REASON_DEFAULT);
	}

	void NoReloadTick()
	{
		const Hash weapon = CurrentWeapon();
		if (WEAPON::IS_WEAPON_VALID(weapon))
		{
			WEAPON::SET_PED_INFINITE_AMMO(Me(), TRUE, weapon);
			WEAPON::_SET_PED_INFINITE_AMMO_CLIP(Me(), TRUE);
		}
	}

	void NoReloadOff()
	{
		WEAPON::SET_PED_INFINITE_AMMO(Me(), FALSE, CurrentWeapon());
		WEAPON::_SET_PED_INFINITE_AMMO_CLIP(Me(), FALSE);
	}

	void TeleportGunTick()
	{
		Vector3 at;
		if (ShotImpact(at))
			ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Me(), at.x, at.y, at.z, TRUE, TRUE, TRUE);
	}

	void LightningGunTick()
	{
		Vector3 at;
		if (ShotImpact(at))
			MISC::_FORCE_LIGHTNING_FLASH_AT_COORDS(at.x, at.y, at.z, 0.0f);
	}

	// Gravity gun: aim at an entity and hold aim to carry it in front of
	// the camera; zoom in/out moves it; firing throws it.
	Entity g_held = 0;
	float g_holdDistance = 8.0f;
	DWORD g_grabCooldown = 0;
	void GravityGunTick()
	{
		const Ped ped = Me();
		const bool aiming = PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer());
		if (g_held && (!ENTITY::DOES_ENTITY_EXIST(g_held) || !aiming))
		{
			g_held = 0;
			return;
		}
		if (!g_held)
		{
			if (!aiming || GetTickCount() < g_grabCooldown)
				return;
			Entity e = AimedEntity();
			if (!e)
				return;
			if (ENTITY::IS_ENTITY_A_PED(e) && PED::IS_PED_IN_ANY_VEHICLE(e, FALSE))
				e = PED::GET_VEHICLE_PED_IS_IN(e, FALSE);
			ENTITY::FREEZE_ENTITY_POSITION(e, FALSE);
			g_held = e;
			return;
		}
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(2, INPUT_SNIPER_ZOOM_IN))
			g_holdDistance += 2.0f;
		if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(2, INPUT_SNIPER_ZOOM_OUT) && g_holdDistance > 3.0f)
			g_holdDistance -= 2.0f;
		if (ENTITY::IS_ENTITY_A_PED(g_held) && !PED::IS_PED_RAGDOLL(g_held))
			PED::SET_PED_TO_RAGDOLL(g_held, 2000, 2000, 0, TRUE, TRUE, "DraggedByCart");
		const Vector3 target = CamTarget(g_holdDistance);
		const Vector3 now = ENTITY::GET_ENTITY_COORDS(g_held, FALSE, FALSE);
		if (AttackPressed())
		{
			const Vector3 d = CamDirection();
			ENTITY::SET_ENTITY_VELOCITY(g_held, d.x * 80.0f, d.y * 80.0f, d.z * 80.0f);
			g_held = 0;
			g_grabCooldown = GetTickCount() + 1000;
			return;
		}
		ENTITY::SET_ENTITY_VELOCITY(g_held, (target.x - now.x) * 4.0f, (target.y - now.y) * 4.0f, (target.z - now.z) * 4.0f);
	}

	// Soul swap: shoot a ped to leave your body behind (a clone) and take
	// over the target's body and position.
	void SoulSwapTick()
	{
		const Ped me = Me();
		const Entity target = AimedEntity();
		if (!target || !ENTITY::IS_ENTITY_A_PED(target) || target == me)
			return;
		if (!PED::IS_PED_SHOOTING(me) || !WEAPON::IS_PED_ARMED(me, ARMED_ANY_GUN))
			return;
		const Vector3 pos = ENTITY::GET_ENTITY_COORDS(target, FALSE, FALSE);
		const Vector3 rot = ENTITY::GET_ENTITY_ROTATION(target, 2);
		PED::CLONE_PED(me, FALSE, TRUE, TRUE);
		PED::CLONE_PED_TO_TARGET(target, me);
		Ped victim = target;
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(victim))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(victim, TRUE, TRUE);
		PED::DELETE_PED(&victim);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(me, pos.x, pos.y, pos.z, TRUE, TRUE, TRUE);
		ENTITY::SET_ENTITY_ROTATION(me, rot.x, rot.y, rot.z, 2, TRUE);
		WAIT(200);
	}

	// Magnet: while aiming, pulls every nearby ped and vehicle toward a
	// point 15m down the camera line.
	void MagnetTick()
	{
		if (!PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer()) || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN))
			return;
		const Vector3 point = CamTarget(15.0f);
		GRAPHICS::_DRAW_MARKER(0x50638AB9, point.x, point.y, point.z, 0, 0, 0, 180.0f, 0, 0, 1.0f, 1.0f, 1.0f, 0, 200, 0, 170, TRUE, TRUE, 2, TRUE, nullptr, nullptr, FALSE);
		const Ped me = Me();
		const Ped horse = GameUtil::PlayerHorse();
		std::vector<Entity> all = GameUtil::AllPeds();
		for (Vehicle v : GameUtil::AllVehicles())
			all.push_back(v);
		for (Entity e : GameUtil::Nearby(all, point, 60.0f))
		{
			if (e == me || e == horse)
				continue;
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, FALSE, FALSE);
			ENTITY::SET_ENTITY_VELOCITY(e, (point.x - p.x) * 2.0f, (point.y - p.y) * 2.0f, (point.z - p.z) * 2.0f);
		}
	}

	// Pickup gun: hold aim on an entity to carry it at a fixed distance.
	void PickupGunTick()
	{
		Entity e = AimedEntity();
		if (!e || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN))
			return;
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		if (!(IsKeyDown(VK_RBUTTON) || PAD::IS_DISABLED_CONTROL_PRESSED(2, INPUT_AIM)))
			return;
		if (ENTITY::IS_ENTITY_A_PED(e) && PED::IS_PED_IN_ANY_VEHICLE(e, FALSE))
			e = PED::GET_VEHICLE_PED_IS_IN(e, FALSE);
		const Vector3 p = CamTarget(10.0f);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, p.x, p.y, p.z, FALSE, FALSE, FALSE);
	}

	void PerfectPeltTick()
	{
		const Entity e = AimedEntity();
		if (!e || !ENTITY::IS_ENTITY_A_PED(e) || PED::IS_PED_HUMAN(e) || PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(e)))
			return;
		PED::_SET_PED_QUALITY(e, 2);
		PED::_SET_PED_DAMAGE_CLEANLINESS(e, 2);
	}

	void ForceGunTick()
	{
		Entity e = AimedEntity();
		if (!e || !PED::IS_PED_SHOOTING(Me()))
			return;
		if (ENTITY::IS_ENTITY_A_PED(e))
		{
			if (PED::IS_PED_IN_ANY_VEHICLE(e, TRUE))
				e = PED::GET_VEHICLE_PED_IS_IN(e, FALSE);
			else if (PED::IS_PED_ON_MOUNT(e))
				e = PED::GET_MOUNT(e);
		}
		const Vector3 d = CamDirection();
		ENTITY::APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS(e, 1, d.x * 100.0f, d.y * 100.0f, d.z * 100.0f, 0, FALSE, TRUE, FALSE);
	}

	// Freeze gun: shoot to freeze; SHIFT while aiming switches to unfreeze.
	bool g_unfreezeMode = false;
	void FreezeGunTick()
	{
		if (!PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer()) || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN))
			return;
		if (IsKeyJustUp(VK_SHIFT))
			g_unfreezeMode = !g_unfreezeMode;
		const Entity e = AimedEntity();
		if (e && AttackPressed())
		{
			PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
			ENTITY::FREEZE_ENTITY_POSITION(e, !g_unfreezeMode);
		}
	}

	// Drive-it gun: shoot a vehicle or horse to take its seat, throwing the
	// rider out.
	void DriveItTick()
	{
		const Ped me = Me();
		Entity e = AimedEntity();
		if (!e || !WEAPON::IS_PED_ARMED(me, ARMED_ANY_GUN) || !AttackPressed())
			return;
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		if (ENTITY::IS_ENTITY_A_PED(e) && !PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(e)))
		{
			const Ped rider = e;
			if (PED::IS_PED_ON_MOUNT(rider))
				e = PED::GET_MOUNT(rider);
			else if (PED::IS_PED_IN_ANY_VEHICLE(rider, FALSE))
				e = PED::GET_VEHICLE_PED_IS_IN(rider, FALSE);
			else
				return;
			DeleteEntity(rider);
		}
		if (ENTITY::IS_ENTITY_A_VEHICLE(e))
			PED::SET_PED_INTO_VEHICLE(me, e, -1);
		else if (ENTITY::IS_ENTITY_A_PED(e))
			PED::SET_PED_ONTO_MOUNT(me, e, -1, TRUE);
	}

	void BleedOutTick()
	{
		const Entity e = AimedEntity();
		const Ped me = Me();
		if (!e || !ENTITY::IS_ENTITY_A_PED(e) || !PED::IS_PED_SHOOTING(me))
			return;
		WAIT(0);
		if (!ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(e, me, TRUE, TRUE))
			return;
		int bone = 0;
		PED::GET_PED_LAST_DAMAGE_BONE(e, &bone);
		TASK::_TASK_ANIMAL_BLEED_OUT(e, me, FALSE, CurrentWeapon(), 0, bone);
	}

	// Ours: a red line from the weapon down the camera line while aiming
	// (Rampage draws its own overlay line).
	void LaserTick()
	{
		const Ped me = Me();
		if (!PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer()) || !WEAPON::IS_PED_ARMED(me, ARMED_ANY_GUN))
			return;
		const Entity gun = WEAPON::GET_CURRENT_PED_WEAPON_ENTITY_INDEX(me, 0);
		const Vector3 from = gun ? ENTITY::GET_ENTITY_COORDS(gun, FALSE, FALSE) : ENTITY::GET_ENTITY_COORDS(me, FALSE, FALSE);
		const Vector3 to = CamTarget(300.0f);
		GRAPHICS::DRAW_LINE(from.x, from.y, from.z, to.x, to.y, to.z, 255, 0, 0, 255);
	}

	void DeleteGunTick()
	{
		const Entity e = AimedEntity();
		if (e && e != Me() && PED::IS_PED_SHOOTING(Me()))
			DeleteEntity(e);
	}

	void ReviveGunTick()
	{
		const Entity e = AimedEntity();
		if (!e || !ENTITY::IS_ENTITY_A_PED(e) || !AttackPressed())
			return;
		if (!ENTITY::IS_ENTITY_DEAD(e) && !PED::IS_PED_INJURED(e))
			return;
		PED::RESURRECT_PED(e);
		PED::REVIVE_INJURED_PED(e);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(e, TRUE, TRUE);
	}

	// Cycles the lantern light through the colour wheel.
	void DiscoLanternTick()
	{
		const Entity lantern = WEAPON::GET_CURRENT_PED_WEAPON_ENTITY_INDEX(Me(), 0);
		if (!lantern)
			return;
		const float t = GetTickCount() / 500.0f;
		const int r = static_cast<int>(127.5f + 127.5f * sinf(t));
		const int g = static_cast<int>(127.5f + 127.5f * sinf(t + 2.094f));
		const int b = static_cast<int>(127.5f + 127.5f * sinf(t + 4.189f));
		GRAPHICS::_SET_LIGHTS_COLOR_FOR_ENTITY(lantern, r, g, b);
		GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(lantern);
	}

	// --- manage ---------------------------------------------------------

	void GiveWeapon(Hash weapon, bool replace)
	{
		const Ped ped = Me();
		if (replace && WEAPON::HAS_PED_GOT_WEAPON(ped, weapon, 0, FALSE))
			WEAPON::REMOVE_WEAPON_FROM_PED(ped, weapon, TRUE, REMOVE_REASON_DEFAULT);
		int ammo = 0;
		WEAPON::GET_MAX_AMMO(ped, &ammo, weapon);
		WEAPON::GIVE_WEAPON_TO_PED(ped, weapon, ammo, FALSE, TRUE, 0, TRUE, 0.5f, 1.0f, ADD_REASON_DEFAULT, TRUE, 0.0f, FALSE);
	}

	std::string GiveAll()
	{
		int given = 0;
		for (const char* name : kWeapons)
		{
			const Hash weapon = GameUtil::Joaat(name);
			if (WEAPON::IS_WEAPON_VALID(weapon) && !WEAPON::HAS_PED_GOT_WEAPON(Me(), weapon, 0, FALSE))
			{
				GiveWeapon(weapon, false);
				given++;
			}
		}
		return "Gave " + std::to_string(given) + " weapons";
	}

	std::string GiveCustom()
	{
		std::string name;
		if (!GameUtil::PromptText("Weapon name or hash", name, 60) || name.empty())
			return {};
		Hash weapon = 0;
		try
		{
			weapon = name.rfind("0x", 0) == 0 ? static_cast<Hash>(std::stoul(name, nullptr, 16)) : GameUtil::Joaat(name);
		}
		catch (...)
		{
			return "Bad hash";
		}
		if (!WEAPON::IS_WEAPON_VALID(weapon))
			return "Not a weapon: " + name;
		GiveWeapon(weapon, true);
		return {};
	}

	void FillAmmo(Hash ammo)
	{
		WEAPON::_ADD_AMMO_TO_PED_BY_TYPE(Me(), ammo, 400, ADD_REASON_DEFAULT);
	}

	void FillAllAmmo()
	{
		for (const char* name : kAmmo)
			FillAmmo(GameUtil::Joaat(name));
		AUDIO::PLAY_SOUND_FRONTEND("AMMO", "PICKUP_SOUNDSET", FALSE, 0);
	}

	void FillCurrentAmmo()
	{
		FillAmmo(WEAPON::GET_PED_AMMO_TYPE_FROM_WEAPON(Me(), CurrentWeapon()));
		AUDIO::PLAY_SOUND_FRONTEND("AMMO", "PICKUP_SOUNDSET", FALSE, 0);
	}

	// --- modifiers ------------------------------------------------------

	float g_damage = 1.0f, g_melee = 1.0f, g_defense = 1.0f;
	int g_accuracy = 50;
	void ApplyModifiers()
	{
		PLAYER::SET_PLAYER_WEAPON_DAMAGE_MODIFIER(MyPlayer(), g_damage);
		PLAYER::SET_PLAYER_MELEE_WEAPON_DAMAGE_MODIFIER(MyPlayer(), g_melee);
		PLAYER::SET_PLAYER_WEAPON_DEFENSE_MODIFIER(MyPlayer(), g_defense);
	}

	void ResetModifiers()
	{
		g_damage = g_melee = g_defense = 1.0f;
		ApplyModifiers();
	}
}

namespace Menus
{
	std::span<const char* const> WeaponNames()
	{
		return kWeapons;
	}

	void BuildWeapons(MenuBase* root)
	{
		MenuBase* weapons = Ui::Submenu(root, "Weapon");

		MenuBase* manage = Ui::Submenu(weapons, "Manage Weapons");
		Ui::Action(manage, "Weapon Locker", []
		{
			if (MISC::GET_MISSION_FLAG())
				return std::string("Not during a mission");
			if (!SCRIPT::DOES_SCRIPT_EXIST("weapon_locker"))
				return std::string("weapon_locker doesn't exist");
			SCRIPT::REQUEST_SCRIPT("weapon_locker");
			for (int i = 0; i < 300 && !SCRIPT::HAS_SCRIPT_LOADED("weapon_locker"); i++)
				WAIT(10);
			SCRIPT::START_NEW_SCRIPT("weapon_locker", 1024);
			SCRIPT::SET_SCRIPT_AS_NO_LONGER_NEEDED("weapon_locker");
			return std::string();
		});
		Ui::Action(manage, "Give All", GiveAll);
		Ui::Do(manage, "Remove All", [] { WEAPON::REMOVE_ALL_PED_WEAPONS(Me(), TRUE, TRUE); });
		Ui::Do(manage, "Drop Current", [] { WEAPON::MAKE_PED_DROP_WEAPON(Me(), TRUE, 0, TRUE, FALSE); });
		Ui::Do(manage, "Remove Current", [] { WEAPON::REMOVE_WEAPON_FROM_PED(Me(), CurrentWeapon(), TRUE, REMOVE_REASON_DEFAULT); });
		Ui::Action(manage, "Give Custom", GiveCustom);
		MenuBase* give = Ui::ListMenu(manage, "Give Weapon", [](MenuBase* m)
		{
			for (const char* name : kWeapons)
			{
				const Hash weapon = GameUtil::Joaat(name);
				if (WEAPON::IS_WEAPON_VALID(weapon))
					Ui::Do(m, name + 7, [weapon] { GiveWeapon(weapon, true); });
			}
		});
		(void)give;

		MenuBase* ammo = Ui::Submenu(weapons, "Ammunition");
		Ui::Do(ammo, "Fill Ammo", FillCurrentAmmo);
		Ui::Do(ammo, "Fill Ammo (All)", FillAllAmmo);
		Ui::Do(ammo, "Remove All Ammo", [] { WEAPON::_HIDE_PED_WEAPONS(Me(), 2, TRUE); WEAPON::_REMOVE_ALL_PED_AMMO(Me()); });
		Ui::Section(ammo, "Ammo Types");
		for (const char* name : kAmmo)
		{
			const Hash hash = GameUtil::Joaat(name);
			Ui::Do(ammo, name + 5, [hash] { FillAmmo(hash); });
		}

		MenuBase* mods = Ui::Submenu(weapons, "Weapon Modifiers");
		Ui::Number(mods, "Damage Modifier", &g_damage, 0.0f, 100.0f, 0.5f, ApplyModifiers);
		Ui::Number(mods, "Melee Modifier", &g_melee, 0.0f, 100.0f, 0.5f, ApplyModifiers);
		Ui::Number(mods, "Defense Modifier", &g_defense, 0.0f, 100.0f, 0.5f, ApplyModifiers);
		Ui::Number(mods, "Accuracy", &g_accuracy, 0, 100, 5, [] { PED::SET_PED_ACCURACY(Me(), g_accuracy); });
		Ui::Do(mods, "Reset", ResetModifiers);

		BuildWeaponSubmenus(weapons); // Visuals, Aimbot, Bullets
		BuildWeaponExtras(weapons, manage, ammo, mods);
		Ui::Toggle(weapons, "Disable Dual Wield", [](bool on) { WEAPON::_SET_ALLOW_DUAL_WIELD(Me(), !on); });
		Ui::Section(weapons, "Weapon Mods");
		Ui::Looped(weapons, "Slow Motion on Aiming", SlowMoAimTick, [] { MISC::SET_TIME_SCALE(1.0f); });
		Ui::Looped(weapons, "First Person on Aim", FirstPersonAimTick);
		Ui::Looped(weapons, "Rapid Fire", [] { SpawnBullets(1); });
		Ui::Looped(weapons, "Rapid Gun", RapidGunTick, RapidGunOff);
		Ui::Looped(weapons, "Auto Cock", AutoCockTick);
		Ui::Looped(weapons, "Tenfold Bullets", [] { SpawnBullets(3); });
		Ui::Looped(weapons, "Explosive Ammo", [] { ImpactExplosion(22); });
		Ui::Looped(weapons, "Fire Ammo", [] { ImpactExplosion(30); });
		Ui::Looped(weapons, "Infinite Ammo", InfiniteAmmoTick);
		Ui::Looped(weapons, "No Reload", [] { g_noReload = true; NoReloadTick(); }, [] { g_noReload = false; NoReloadOff(); });
		Ui::Toggle(weapons, "One Hit Kill", [](bool on) { PLAYER::SET_PLAYER_WEAPON_DAMAGE_MODIFIER(MyPlayer(), on ? 100.0f : g_damage); });
		Ui::Toggle(weapons, "Super Punch", [](bool on) { PLAYER::SET_PLAYER_MELEE_WEAPON_DAMAGE_MODIFIER(MyPlayer(), on ? 100.0f : g_melee); });
		Ui::Section(weapons, "Guns");
		Ui::Looped(weapons, "Teleport Gun", TeleportGunTick);
		Ui::Looped(weapons, "Lightning Strike Gun", LightningGunTick);
		Ui::Looped(weapons, "Gravity Gun", GravityGunTick, [] { g_held = 0; });
		Ui::Looped(weapons, "Soul Swap Gun", SoulSwapTick);
		Ui::Looped(weapons, "Magnet Gun", MagnetTick);
		Ui::Looped(weapons, "Pickup Gun", PickupGunTick);
		Ui::Looped(weapons, "Perfect Pelt Gun", PerfectPeltTick);
		Ui::Looped(weapons, "Force Gun", ForceGunTick);
		Ui::Looped(weapons, "Freeze Gun", FreezeGunTick);
		Ui::Looped(weapons, "Drive it Gun", DriveItTick);
		Ui::Looped(weapons, "Bleed Out Gun", BleedOutTick);
		Ui::Looped(weapons, "Weapon Laser", LaserTick);
		Ui::Looped(weapons, "Delete Gun", DeleteGunTick);
		Ui::Looped(weapons, "Revive Gun", ReviveGunTick);
		Ui::Looped(weapons, "Disco Lantern", DiscoLanternTick);
	}
}
