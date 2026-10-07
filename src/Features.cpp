#include "Features.h"
#include "GameUtil.h"
#include "Log.h"

namespace
{
	// ATTRIBUTE core indices: 0 health, 1 stamina, 2 Dead Eye.
	constexpr int kCoreHealth = 0;
	constexpr int kCoreStamina = 1;
	constexpr int kCoreDeadEye = 2;

	void FillHealth(Ped ped)
	{
		ENTITY::SET_ENTITY_HEALTH(ped, ENTITY::GET_ENTITY_MAX_HEALTH(ped, FALSE), 0);
	}

	// The mount the horse toggle last applied to, so turning it off (or
	// switching horses) clears invincibility on the right ped.
	Ped g_invincibleMount = 0;
}

namespace Features
{
	std::string HealPlayer()
	{
		const Ped ped = PLAYER::PLAYER_PED_ID();
		FillHealth(ped);
		for (int core : { kCoreHealth, kCoreStamina, kCoreDeadEye })
			ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped, core, 100);
		PLAYER::RESTORE_PLAYER_STAMINA(PLAYER::PLAYER_ID(), 100.0f);
		return {};
	}

	std::string CleanPlayer()
	{
		const Ped ped = PLAYER::PLAYER_PED_ID();
		PED::CLEAR_PED_WETNESS(ped);
		PED::CLEAR_PED_BLOOD_DAMAGE(ped);
		PED::CLEAR_PED_ENV_DIRT(ped);
		return {};
	}

	// Rampage's wanted helper (sub_180058920) ends with
	// SET_WANTED_SCORE(player, 0) after setting the bounty.
	std::string ClearBounty()
	{
		const Player player = PLAYER::PLAYER_ID();
		LAW::SET_BOUNTY(player, 0);
		LAW::SET_WANTED_SCORE(player, 0);
		return {};
	}

	// Same pair Rampage's invincibility toggle (sub_1800AE0B0) sets.
	void InvinciblePlayer_OnChange(bool on)
	{
		PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), on);
		ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), on);
	}

	std::string HealHorse()
	{
		const Ped mount = GameUtil::PlayerMount();
		if (!mount)
			return "Not riding a horse";
		FillHealth(mount);
		for (int core : { kCoreHealth, kCoreStamina })
			ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(mount, core, 100);
		return {};
	}

	void InvincibleHorse_OnChange(bool on)
	{
		if (!on && g_invincibleMount && ENTITY::DOES_ENTITY_EXIST(g_invincibleMount))
			ENTITY::SET_ENTITY_INVINCIBLE(g_invincibleMount, FALSE);
		g_invincibleMount = 0;
	}

	void InvincibleHorse_OnTick()
	{
		const Ped mount = GameUtil::PlayerMount();
		if (!mount || mount == g_invincibleMount)
			return;
		if (g_invincibleMount && ENTITY::DOES_ENTITY_EXIST(g_invincibleMount))
			ENTITY::SET_ENTITY_INVINCIBLE(g_invincibleMount, FALSE);
		ENTITY::SET_ENTITY_INVINCIBLE(mount, TRUE);
		g_invincibleMount = mount;
	}

	std::string TeleportToWaypoint()
	{
		if (!MAP::IS_WAYPOINT_ACTIVE())
			return "No waypoint set";
		const Vector3 target = MAP::_GET_WAYPOINT_COORDS();
		// Moving the mount carries the rider with it.
		const Ped mount = GameUtil::PlayerMount();
		const Entity entity = mount ? mount : PLAYER::PLAYER_PED_ID();
		if (!GameUtil::TeleportToGround(entity, target.x, target.y))
		{
			Log::Write("TeleportToWaypoint -- no ground found at ({}, {})", target.x, target.y);
			return "Couldn't find the ground there";
		}
		return {};
	}

	std::string AddClockHours(int hours)
	{
		CLOCK::ADD_TO_CLOCK_TIME(hours, 0, 0);
		return {};
	}
}
