/*
	Weapon submenus: ports Rampage's Submenus::SubWeaponVisuals,
	SubWeaponsAimbot and SubWeaponsBullets.

	Ours: the Particle Gun uses the effects the game scripts start
	(Effects.inc, the Player > Effects list); the Ped and Vehicle Guns pick
	from our own short model lists; Remote Cannonball steers with the
	camera and explodes on impact, without Rampage's overlay. Not ported:
	Disable Hitmarker and Disable Hit Feedback (Rampage byte-patches the
	game's HUD code for those).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\keyboard.h"

#include <cmath>
#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Player MyPlayer() { return PLAYER::PLAYER_ID(); }

	constexpr Hash INPUT_ATTACK = 0x6FED71BC; // group 2, as Rampage reads it
	constexpr Hash INPUT_SPECIAL_ABILITY = 0xCEE12B50;
	constexpr Hash WEAPON_UNARMED = 0xA2719263;
	constexpr int ARMED_ANY_GUN = 4;

	Vector3 Direction(const Vector3& rot)
	{
		const float yaw = rot.z * 0.0174532925f, pitch = rot.x * 0.0174532925f;
		return { -std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch) };
	}

	Entity CurrentWeaponEntity() { return WEAPON::GET_CURRENT_PED_WEAPON_ENTITY_INDEX(Me(), 0); }

	Hash CurrentWeapon()
	{
		Hash w = 0;
		WEAPON::GET_CURRENT_PED_WEAPON(Me(), &w, TRUE, 0, FALSE);
		return w;
	}

	bool Shot(Vector3& at)
	{
		const Ped ped = Me();
		return PED::IS_PED_SHOOTING(ped) && WEAPON::IS_PED_ARMED(ped, ARMED_ANY_GUN) && WEAPON::GET_PED_LAST_WEAPON_IMPACT_COORD(ped, &at);
	}

	bool Firing() { return IsKeyDown(VK_LBUTTON) || PAD::IS_DISABLED_CONTROL_PRESSED(2, INPUT_ATTACK); }

	// --- visuals -----------------------------------------------------------------

	int g_opacity = 255;
	bool g_crosshair = false;
	int g_crosshairType = 0;
	int g_crosshairColor[4] = { 255, 255, 255, 255 };
	int g_arrowTrail = 0;
	constexpr Hash kArrowTrails[] = { 0, 0x2740A5AD, 0xB880EB2A }; // Rampage's two trail hashes

	struct Crosshair
	{
		const char* name;
		const char* dict;
		const char* texture;
		float size;
	};
	const Crosshair kCrosshairs[] = {
		{ "Map Cross", "pausemenu_map", "map_crosshair", 0.025f },
		{ "Marked for Death", "overhead", "overhead_marked_for_death", 0.02f },
		{ "Honor Pointer", "generic_textures", "honor_pointer_menu", 0.01f },
		{ "Star", "generic_textures", "rating_star", 0.02f },
		{ "Shield", "generic_textures", "shield", 0.02f },
		{ "Gold Stamp", "generic_textures", "stamp_gold", 0.02f },
	};

	void CrosshairTick()
	{
		const Crosshair& c = kCrosshairs[g_crosshairType];
		if (!TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(c.dict))
		{
			TXD::REQUEST_STREAMED_TEXTURE_DICT(c.dict, FALSE);
			return;
		}
		GRAPHICS::DRAW_SPRITE(c.dict, c.texture, 0.5f, 0.5f, c.size, c.size * 1.7778f, 0.0f,
			g_crosshairColor[0], g_crosshairColor[1], g_crosshairColor[2], g_crosshairColor[3], FALSE);
	}

	void SetWeaponCondition(Entity weapon, float level)
	{
		if (!weapon)
			return;
		WEAPON::_SET_WEAPON_DAMAGE(weapon, level, TRUE);
		WEAPON::_SET_WEAPON_DIRT(weapon, level, TRUE);
		WEAPON::_SET_WEAPON_SOOT(weapon, level, TRUE);
		WEAPON::_SET_WEAPON_DEGRADATION(weapon, level);
	}

	void AutoCleanTick()
	{
		for (int slot : { 0, 1 })
			if (const Entity w = WEAPON::GET_CURRENT_PED_WEAPON_ENTITY_INDEX(Me(), slot))
				if (WEAPON::GET_WEAPON_DEGRADATION(w) > 0.0f)
					SetWeaponCondition(w, 0.0f);
	}

	void BuildVisuals(MenuBase* weapons)
	{
		MenuBase* v = Ui::Submenu(weapons, "Weapon Visuals");
		Ui::Toggle(v, "Invisible Weapon", [](bool on) { if (const Entity w = CurrentWeaponEntity()) ENTITY::SET_ENTITY_VISIBLE(w, !on); });
		Ui::Number(v, "Opacity", &g_opacity, 0, 255, 15, [] { if (const Entity w = CurrentWeaponEntity()) ENTITY::SET_ENTITY_ALPHA(w, g_opacity, FALSE); });
		Ui::Looped(v, "Crosshair", CrosshairTick);
		std::vector<std::string> names;
		for (const Crosshair& c : kCrosshairs)
			names.push_back(c.name);
		Ui::Choice(v, "Crosshair Type", names, &g_crosshairType);
		MenuBase* color = Ui::Submenu(v, "Crosshair Color");
		const char* const kRgba[] = { "Red", "Green", "Blue", "Alpha" };
		for (int i = 0; i < 4; i++)
			Ui::Number(color, kRgba[i], &g_crosshairColor[i], 0, 255, 5);
		Ui::Choice(v, "Arrow Trail", { "Default", "Trail 1", "Trail 2" }, &g_arrowTrail,
			[](int i) { WEAPON::_SET_ARROW_TRAIL_FX(Me(), kArrowTrails[i]); });
		Ui::Section(v, "Condition");
		Ui::Toggle(v, "No Degradation", [](bool on) { PLAYER::_SET_WEAPON_DEGRADATION_MODIFIER(MyPlayer(), on ? 0.0f : 1.0f); });
		Ui::Looped(v, "Automatic Clean", AutoCleanTick);
		Ui::Do(v, "Apply Dirt", [] { SetWeaponCondition(CurrentWeaponEntity(), 100.0f); });
		Ui::Do(v, "Clean", []
		{
			SetWeaponCondition(CurrentWeaponEntity(), 0.0f);
			AUDIO::PLAY_SOUND_FRONTEND("GUN_OIL", "PICKUP_SOUNDSET", TRUE, 0);
		});
		Ui::Section(v, "Misc");
		Ui::Looped(v, "Clear Dead Eye", []
		{
			if (PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer()) && CurrentWeapon() != WEAPON_UNARMED && PAD::IS_CONTROL_PRESSED(0, INPUT_SPECIAL_ABILITY))
				GRAPHICS::ANIMPOSTFX_STOP_ALL();
		});
		Ui::Looped(v, "Disable Radial Reticle", [] { PED::SET_PED_RESET_FLAG(Me(), 293, TRUE); });
	}

	// --- aimbot ------------------------------------------------------------------

	int g_targetingMode = 1;
	float g_lockonRange = 0.0f;
	int g_bone = 0;
	int g_aimTargets = 0; // all, humans, animals
	bool g_autoShoot = false;
	bool g_skipDead = true;
	const char* const kBones[] = { "SKEL_Head", "SKEL_Neck_1", "SKEL_Spine_Root", "SKEL_L_Hand", "SKEL_R_Hand", "SKEL_R_Foot", "SKEL_L_Foot" };

	Vector3 BoneCoords(Ped ped)
	{
		const int bone = ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(ped, kBones[g_bone]);
		return ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(ped, bone);
	}

	void ShootAt(const Vector3& to)
	{
		const Vector3 from = CAMERA::GET_GAMEPLAY_CAM_COORD();
		MISC::SHOOT_SINGLE_BULLET_BETWEEN_COORDS(from.x, from.y, from.z, to.x, to.y, to.z, 100, TRUE, CurrentWeapon(), Me(), TRUE, TRUE, -1.0f, FALSE);
	}

	bool WantedTarget(Ped p)
	{
		if (p == Me() || ENTITY::IS_ENTITY_DEAD(p) || (g_skipDead && PED::IS_PED_DEAD_OR_DYING(p, FALSE)))
			return false;
		if (g_aimTargets == 1)
			return PED::IS_PED_HUMAN(p) != 0;
		if (g_aimTargets == 2)
			return !PED::IS_PED_HUMAN(p);
		return true;
	}

	// Triggerbot: fires at the chosen bone of the ped under the crosshair.
	void TriggerbotTick()
	{
		Entity e = 0;
		if (!PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(MyPlayer(), &e) || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN) || !ENTITY::IS_ENTITY_A_PED(e))
			return;
		if (!WantedTarget(e))
			return;
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		ShootAt(BoneCoords(e));
	}

	// Aimbot: while aiming, snaps the aim to the nearest visible wanted ped
	// and fires there when the trigger is pulled (or always, with Auto
	// Shoot).
	void AimbotTick()
	{
		if (!PLAYER::IS_PLAYER_FREE_AIMING(MyPlayer()) || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN))
			return;
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const float range = g_lockonRange > 0.0f ? g_lockonRange : 150.0f;
		Ped best = 0;
		float bestD = range * range;
		for (Ped p : GameUtil::AllPeds())
		{
			if (!WantedTarget(p))
				continue;
			const float d = GameUtil::DistanceSq(me, ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE));
			if (d < bestD && ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(Me(), p, 17))
			{
				bestD = d;
				best = p;
			}
		}
		if (!best)
			return;
		const Vector3 at = BoneCoords(best);
		TASK::TASK_AIM_GUN_AT_COORD(Me(), at.x, at.y, at.z, 500, TRUE, FALSE);
		if (g_autoShoot || Firing())
		{
			PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
			ShootAt(at);
		}
	}

	void BuildAimbot(MenuBase* weapons)
	{
		MenuBase* a = Ui::Submenu(weapons, "Aimbot");
		Ui::Choice(a, "Targeting Mode", { "Wide", "Normal", "Narrow", "Free Aim" }, &g_targetingMode,
			[](int mode) { PLAYER::SET_PLAYER_TARGETING_MODE(mode); });
		Ui::Toggle(a, "Lockon Range Override", [](bool on)
		{
			if (on && g_lockonRange <= 0.0f)
				g_lockonRange = WEAPON::_GET_MAX_LOCKON_DISTANCE_OF_CURRENT_PED_WEAPON(Me());
			if (!on)
				PLAYER::SET_PLAYER_LOCKON_RANGE_OVERRIDE(MyPlayer(), WEAPON::_GET_MAX_LOCKON_DISTANCE_OF_CURRENT_PED_WEAPON(Me()));
		}, [] { PLAYER::SET_PLAYER_LOCKON_RANGE_OVERRIDE(MyPlayer(), g_lockonRange); });
		Ui::Number(a, "Lockon Range", &g_lockonRange, 0.0f, 1000.0f, 5.0f);
		Ui::Do(a, "Reset", []
		{
			g_lockonRange = WEAPON::_GET_MAX_LOCKON_DISTANCE_OF_CURRENT_PED_WEAPON(Me());
			PLAYER::SET_PLAYER_LOCKON_RANGE_OVERRIDE(MyPlayer(), g_lockonRange);
		});
		Ui::Choice(a, "Bone", { "Head", "Neck", "Spine", "Left Hand", "Right Hand", "Right Foot", "Left Foot" }, &g_bone);
		Ui::Choice(a, "Targets", { "All Peds", "Humans", "Animals" }, &g_aimTargets);
		Ui::Toggle(a, "Ignore Dying Peds", [](bool on) { g_skipDead = on; })->SetState(true);
		Ui::Looped(a, "Triggerbot", TriggerbotTick, [] { PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), FALSE); });
		Ui::Looped(a, "Aimbot", AimbotTick);
		Ui::Toggle(a, "Auto Shoot", [](bool on) { g_autoShoot = on; });
	}

	// --- bullets -----------------------------------------------------------------

	int g_explosionType = 22;
	int g_bulletType = 0;
	const char* const kBulletWeapons[] = { "WEAPON_TURRET_CANNON", "WEAPON_TURRET_GATLING", "WEAPON_BOW",
		"WEAPON_THROWN_MOLOTOV", "WEAPON_TURRET_REVOLVING_CANNON" };
	int g_particle = 0;
	int g_size = 0;
	const float kSizes[] = { 2.0f, 0.3f, 1.0f };
	int g_pedGun = 0;
	const char* const kPedGunModels[] = { "A_C_Chicken_01", "A_C_Pig_01", "A_C_Cow", "A_C_Bear_01", "A_C_Horse_Morgan_Bay", "A_M_M_RancherTravelers_Cool_01" };
	int g_vehicleGun = 0;
	const char* const kVehicleGunModels[] = { "cart01", "wagon02x", "buggy01", "coach2", "stagecoach001x", "handcart", "canoe", "rowboat" };

	struct Effect
	{
		const char* asset;
		const char* name;
	};
	const Effect kEffects[] = {
#include "..\data\Effects.inc"
	};

	void ExplosionGunTick()
	{
		Vector3 at;
		if (Shot(at))
			FIRE::ADD_OWNED_EXPLOSION(Me(), at.x, at.y, at.z, g_explosionType, 1.0f, TRUE, FALSE, 0.1f);
	}

	// Bullet Type: each shot also fires the chosen weapon's projectile from
	// the hand along the camera ray.
	void BulletTypeTick()
	{
		const Ped me = Me();
		if (!PED::IS_PED_SHOOTING(me) || !WEAPON::IS_PED_ARMED(me, ARMED_ANY_GUN))
			return;
		const Hash weapon = GameUtil::Joaat(kBulletWeapons[g_bulletType]);
		if (!WEAPON::_HAS_WEAPON_ASSET_LOADED(weapon))
		{
			WEAPON::_REQUEST_WEAPON_ASSET(weapon, 0, FALSE);
			return;
		}
		const Vector3 cam = CAMERA::GET_GAMEPLAY_CAM_COORD();
		const Vector3 d = Direction(CAMERA::GET_GAMEPLAY_CAM_ROT(2));
		const Vector3 hand = ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(me, ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(me, "PH_R_Hand"));
		MISC::SHOOT_SINGLE_BULLET_BETWEEN_COORDS(hand.x, hand.y, hand.z, cam.x + d.x * 1000.0f, cam.y + d.y * 1000.0f, cam.z + d.z * 1000.0f,
			100, TRUE, weapon, me, TRUE, FALSE, -1.0f, FALSE);
	}

	void ParticleGunTick()
	{
		Vector3 at;
		if (!Shot(at) || std::size(kEffects) == 0)
			return;
		const Effect& e = kEffects[g_particle % std::size(kEffects)];
		STREAMING::REQUEST_NAMED_PTFX_ASSET(GameUtil::Joaat(e.asset));
		for (int i = 0; i < 50 && !STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(GameUtil::Joaat(e.asset)); i++)
			WAIT(0);
		GRAPHICS::USE_PARTICLE_FX_ASSET(e.asset);
		GRAPHICS::START_PARTICLE_FX_NON_LOOPED_AT_COORD(e.name, at.x, at.y, at.z, 0.0f, 0.0f, 0.0f, 1.0f, FALSE, FALSE, FALSE);
	}

	void SizeGunTick()
	{
		Entity e = 0;
		if (!PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(MyPlayer(), &e) || !WEAPON::IS_PED_ARMED(Me(), ARMED_ANY_GUN) || !ENTITY::IS_ENTITY_A_PED(e))
		{
			PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), FALSE);
			return;
		}
		if (Firing())
		{
			PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
			PED::_SET_PED_SCALE(e, kSizes[g_size]);
		}
	}

	void CoordsGunTick()
	{
		Vector3 at;
		if (Shot(at))
			Ui::Controller().SetStatusText(std::format("{:.3f}, {:.3f}, {:.3f}", at.x, at.y, at.z), 4000);
	}

	// Throws the model out along the camera ray with a strong push.
	template <typename Create>
	void LaunchFromCamera(Hash model, float distance, Create create)
	{
		const Ped me = Me();
		if (!PED::IS_PED_SHOOTING(me) || !WEAPON::IS_PED_ARMED(me, ARMED_ANY_GUN) || !GameUtil::LoadModel(model))
			return;
		const Vector3 cam = CAMERA::GET_GAMEPLAY_CAM_COORD();
		const Vector3 d = Direction(CAMERA::GET_GAMEPLAY_CAM_ROT(2));
		const Vector3 at{ cam.x + d.x * distance, cam.y + d.y * distance, cam.z + d.z * distance };
		Entity e = create(model, at, ENTITY::GET_ENTITY_HEADING(me));
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		if (!e)
			return;
		ENTITY::APPLY_FORCE_TO_ENTITY(e, 1, d.x * 300.0f, d.y * 300.0f, d.z * 300.0f, 0, 0, 0, 0, FALSE, TRUE, TRUE, FALSE, TRUE);
		ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&e);
	}

	void PedGunTick()
	{
		LaunchFromCamera(GameUtil::Joaat(kPedGunModels[g_pedGun]), 4.0f, [](Hash model, const Vector3& at, float heading) -> Entity
		{
			const Ped p = PED::CREATE_PED(model, at.x, at.y, at.z, heading, FALSE, TRUE, FALSE, FALSE);
			if (p)
				PED::_SET_RANDOM_OUTFIT_VARIATION(p, TRUE);
			return p;
		});
	}

	void VehicleGunTick()
	{
		LaunchFromCamera(GameUtil::Joaat(kVehicleGunModels[g_vehicleGun]), 10.0f, [](Hash model, const Vector3& at, float heading) -> Entity
		{
			return VEHICLE::CREATE_VEHICLE(model, at.x, at.y, at.z, heading, FALSE, TRUE, TRUE, FALSE);
		});
	}

	// Remote Cannonball (ours): fire to launch a cannonball, steer it with
	// the camera; it explodes when it hits something.
	Object g_ball = 0;
	Cam g_ballCam = 0;

	void EndCannonball()
	{
		if (ENTITY::DOES_ENTITY_EXIST(g_ball))
		{
			Entity e = g_ball;
			ENTITY::DELETE_ENTITY(&e);
		}
		g_ball = 0;
		if (CAMERA::DOES_CAM_EXIST(g_ballCam))
		{
			CAMERA::SET_CAM_ACTIVE(g_ballCam, FALSE);
			CAMERA::DESTROY_CAM(g_ballCam, FALSE);
			CAMERA::RENDER_SCRIPT_CAMS(FALSE, TRUE, 700, TRUE, TRUE, 0);
		}
		g_ballCam = 0;
		STREAMING::CLEAR_FOCUS();
		ENTITY::FREEZE_ENTITY_POSITION(Me(), FALSE);
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), FALSE);
	}

	void CannonballTick()
	{
		const Ped me = Me();
		if (!g_ball)
		{
			if (!PED::IS_PED_SHOOTING(me) || !WEAPON::IS_PED_ARMED(me, ARMED_ANY_GUN))
				return;
			const Hash model = GameUtil::Joaat("p_cannonball01x");
			if (!GameUtil::LoadModel(model))
				return;
			const Entity weapon = CurrentWeaponEntity();
			const Vector3 at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(weapon ? weapon : me, 0.0f, 1.0f, 0.0f);
			g_ball = OBJECT::CREATE_OBJECT(model, at.x, at.y, at.z, FALSE, TRUE, FALSE, FALSE, FALSE);
			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			if (!g_ball)
				return;
			g_ballCam = CAMERA::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", TRUE);
			CAMERA::ATTACH_CAM_TO_ENTITY(g_ballCam, g_ball, 0.0f, -2.0f, 0.5f, TRUE);
			CAMERA::SET_CAM_ACTIVE(g_ballCam, TRUE);
			CAMERA::RENDER_SCRIPT_CAMS(TRUE, FALSE, 700, TRUE, TRUE, 0);
			ENTITY::SET_ENTITY_VISIBLE(g_ball, FALSE);
			STREAMING::SET_FOCUS_ENTITY(g_ball);
			ENTITY::FREEZE_ENTITY_POSITION(me, TRUE);
			return;
		}
		if (!ENTITY::DOES_ENTITY_EXIST(g_ball))
			return EndCannonball();
		PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), TRUE);
		const Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
		CAMERA::SET_CAM_ROT(g_ballCam, rot.x, rot.y, rot.z, 2);
		ENTITY::SET_ENTITY_ROTATION(g_ball, rot.x, rot.y, rot.z, 2, TRUE);
		const Vector3 d = Direction(rot);
		Vector3 p = ENTITY::GET_ENTITY_COORDS(g_ball, FALSE, FALSE);
		p = { p.x + d.x * 1.5f, p.y + d.y * 1.5f, p.z + d.z * 1.5f };
		ENTITY::SET_ENTITY_COORDS(g_ball, p.x, p.y, p.z, FALSE, FALSE, FALSE, FALSE);
		float ground = 0.0f;
		MISC::GET_GROUND_Z_FOR_3D_COORD(p.x, p.y, p.z, &ground, FALSE);
		if (ENTITY::HAS_ENTITY_COLLIDED_WITH_ANYTHING(g_ball) || std::fabs(p.z - ground) <= 0.5f)
		{
			FIRE::ADD_EXPLOSION(p.x, p.y, p.z, 22, 10.0f, TRUE, FALSE, 0.4f);
			PAD::SET_CONTROL_SHAKE(0, 300, 200);
			EndCannonball();
		}
	}

	void BuildBullets(MenuBase* weapons)
	{
		MenuBase* b = Ui::Submenu(weapons, "Weapon Bullets");
		Ui::Looped(b, "Explosion Gun", ExplosionGunTick);
		Ui::Number(b, "Explosion Type", &g_explosionType, 0, 40, 1);
		Ui::Looped(b, "Bullet Type Gun", BulletTypeTick);
		std::vector<std::string> bullets;
		for (const char* w : kBulletWeapons)
			bullets.push_back(w + 7);
		Ui::Choice(b, "Bullet Type", bullets, &g_bulletType);
		Ui::Looped(b, "Particle Gun", ParticleGunTick);
		std::vector<std::string> particles;
		for (const Effect& e : kEffects)
			particles.push_back(e.name);
		Ui::Choice(b, "Particle", particles, &g_particle);
		Ui::Looped(b, "Size Gun", SizeGunTick, [] { PLAYER::DISABLE_PLAYER_FIRING(MyPlayer(), FALSE); });
		Ui::Choice(b, "Size", { "Large", "Small", "Normal" }, &g_size);
		Ui::Looped(b, "Coords Gun", CoordsGunTick);
		Ui::Looped(b, "Remote Cannonball", CannonballTick, EndCannonball);
		Ui::Looped(b, "Ped Gun", PedGunTick);
		std::vector<std::string> peds(std::begin(kPedGunModels), std::end(kPedGunModels));
		Ui::Choice(b, "Ped Model", peds, &g_pedGun);
		Ui::Looped(b, "Vehicle Gun", VehicleGunTick);
		std::vector<std::string> vehicles(std::begin(kVehicleGunModels), std::end(kVehicleGunModels));
		Ui::Choice(b, "Vehicle Model", vehicles, &g_vehicleGun);
	}
}

namespace Menus
{
	void BuildWeaponSubmenus(MenuBase* weapons)
	{
		BuildVisuals(weapons);
		BuildAimbot(weapons);
		BuildBullets(weapons);
	}
}
