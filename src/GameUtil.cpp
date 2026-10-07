#include "GameUtil.h"

#include "..\external\RDR-Classes\rage\joaat.hpp"

namespace GameUtil
{
	bool IsOnline()
	{
		return NETWORK::NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(rage::Joaat("net_main_online"), -1, FALSE, 0) != FALSE;
	}

	Ped PlayerMount()
	{
		const Ped ped = PLAYER::PLAYER_PED_ID();
		return PED::IS_PED_ON_MOUNT(ped) ? PED::GET_MOUNT(ped) : 0;
	}

	bool TeleportToGround(Entity entity, float x, float y)
	{
		// Collision far from the player isn't loaded yet, so the ground
		// probe fails until it streams in: park the entity high above the
		// target, request collision, and retry from the top down for ~2s.
		constexpr float kProbeHeights[] = { 1000.0f, 800.0f, 600.0f, 400.0f, 300.0f, 200.0f, 150.0f, 100.0f, 50.0f, 25.0f, 0.0f };
		for (int attempt = 0; attempt < 20; attempt++)
		{
			for (float height : kProbeHeights)
			{
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, x, y, height, FALSE, FALSE, FALSE);
				STREAMING::REQUEST_COLLISION_AT_COORD(x, y, height);
				float groundZ = 0.0f;
				if (MISC::GET_GROUND_Z_FOR_3D_COORD(x, y, height, &groundZ, FALSE))
				{
					ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, x, y, groundZ + 1.0f, FALSE, FALSE, FALSE);
					return true;
				}
			}
			WAIT(100);
		}
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, x, y, 1000.0f, FALSE, FALSE, FALSE);
		return false;
	}
}
