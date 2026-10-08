/*
	Small game-state helpers shared by the features.
*/

#pragma once

#include "script.h"
#include "ExtraNatives.h"
#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <cstdint>
#include <string>
#include <string_view>
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

	// A typed name or hash: "0x..." hex, plain decimal, else joaat of the
	// name. 0 for empty text.
	Hash ParseHash(std::string_view text);

	// ---- Inventory ----

	// Inventory 1 is singleplayer's; 5 is the game's backup inventory.
	constexpr int kInventorySp = 1;
	constexpr int kInventorySpBackup = 5;
	constexpr Hash kRemoveReasonDefault = 0xF77DE93D; // REMOVE_REASON_DEFAULT

	// An inventory GUID is a script struct<4>: four 8-byte script words,
	// 32 bytes. (The SDK's Any is 4 bytes, so an Any[4] is too small and
	// the native overruns it.) The slot struct the scripts build is the
	// same four words plus the slot id in a fifth.
	struct ItemGuid
	{
		std::uint64_t w[4] = {};
		Any* Ptr() { return reinterpret_cast<Any*>(w); }
	};
	struct SlotGuid
	{
		std::uint64_t w[5] = {}; // w[0..3] parent guid, w[4] slot id
		Any* Ptr() { return reinterpret_cast<Any*>(w); }
		Hash Slot() const { return static_cast<Hash>(w[4]); }
	};
	static_assert(sizeof(ItemGuid) == 32 && sizeof(SlotGuid) == 40);

	// A script stat id, struct<2>: f_0 the stat, or a verb such as
	// joaat("Pick") for per-item stats; f_1 the item (0 for none). Two
	// 8-byte script words.
	struct StatId
	{
		std::uint64_t stat = 0;
		std::uint64_t item = 0;
		Any* Ptr() { return reinterpret_cast<Any*>(this); }
	};
	static_assert(sizeof(StatId) == 16);

	// The inventory's root "character" GUID, the parent of everything in it.
	ItemGuid CharacterGuid(int inventoryId);

	// Adds `quantity` of `item` the way the game's scripts do by default
	// (flow_controller's add path, as worked out in CigCardTest): satchel,
	// else wardrobe, else the item's default slot under the character.
	// On failure `error` says why.
	bool AddInventoryItem(Hash item, int quantity, std::string& error);

	// An item's name in the game's current language: the item hash is
	// also its text label. `fallback` if the game has no text for it.
	std::string ItemName(Hash item, std::string_view fallback);
}
