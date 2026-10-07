/*
	Small game-state helpers shared by the features.
*/

#pragma once

#include "script.h"
#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <string>
#include <vector>

namespace GameUtil
{
	// True while Red Dead Online is running (net_main_online is active).
	// Same check Rampage's main loop uses for its own online kill switch:
	// NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(joaat("net_main_online"), -1, 0, 0).
	bool IsOnline();

	inline Hash Joaat(std::string_view s) { return rage::Joaat(s); }

	// The player's current mount, or 0 when not riding.
	Ped PlayerMount();

	// Moves `entity` to (x, y) on the ground, probing downward from high up
	// until collision there has streamed in. Returns false (and leaves the
	// entity at a high fallback height) if no ground was found in time.
	bool TeleportToGround(Entity entity, float x, float y);

	// Every ped / vehicle / object the game currently has (ScriptHookRDR2's
	// pool walk), excluding nothing.
	std::vector<Ped> AllPeds();
	std::vector<Vehicle> AllVehicles();
	std::vector<Object> AllObjects();

	// Entities within `radius` of `center`.
	std::vector<Entity> Nearby(const std::vector<Entity>& entities, const Vector3& center, float radius);

	// Pointer to a script global, or nullptr. Slots are 8 bytes.
	UINT64* Global(int index);

	float DistanceSq(const Vector3& a, const Vector3& b);

	// Loads a model, waiting up to ~2s. False if it never loaded.
	bool LoadModel(Hash model);
	bool LoadAnimDict(const char* dict);

	// The player's horse: the current mount, else the last one, else the
	// saddle horse. 0 if none exists.
	Ped PlayerHorse();

	// On-screen keyboard; returns false if cancelled.
	bool PromptText(const char* title, std::string& text, int maxLength = 60);
}
