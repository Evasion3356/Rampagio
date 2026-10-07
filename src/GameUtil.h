/*
	Small game-state helpers shared by the features.
*/

#pragma once

#include "script.h"

namespace GameUtil
{
	// True while Red Dead Online is running (net_main_online is active).
	// Same check Rampage's main loop uses for its own online kill switch:
	// NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(joaat("net_main_online"), -1, 0, 0).
	bool IsOnline();

	// The player's current mount, or 0 when not riding.
	Ped PlayerMount();

	// Moves `entity` to (x, y) on the ground, probing downward from high up
	// until collision there has streamed in. Returns false (and leaves the
	// entity at a high fallback height) if no ground was found in time.
	bool TeleportToGround(Entity entity, float x, float y);
}
