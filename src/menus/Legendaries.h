/*
	The game's legendary animal state, shared by Recovery > Collectibles >
	Legendary Animals and Spawner > Ped Spawner > Animals (ours; Rampage
	has no kill state).

	Global_40.f_9319 is an array of 16 hunting zones, 4 slots each
	(hunting_zone_*.ysc, short_update ~line 11326): .f_0 zone revealed,
	.f_1 killed, .f_2 respawn time, .f_3 carcass/pelt pending. The zone
	order is hunting_zone_bear_legendary func_24 / short_update func_1141's.
	The labels are LegendaryAnimals.inc's, so the spawner can match its rows.

	kZoneLocations is where each zone's script starts: its world scenario
	point in the hunting_*.ymt scenario files, generated as
	LegendaryZones.inc by tools/extract_legendary_zones.py (each script's
	own zone index, so in kZones' order).
*/

#pragma once

#include "..\GameUtil.h"

#include <iterator>
#include <string_view>

namespace Legendaries
{
	inline constexpr const char* kZones[] = {
		"Bharati Grizzly Bear", "Beaver", "Big Horn Ram", "White Bison",
		"Boar", "Buck", "Tatanka Bison", "Bull Gator",
		"Cougar", "Coyote", "Elk", "Fox",
		"Moose", "Giaguaro Panther", "Pronghorn", "Wolf",
	};
	inline constexpr int kZoneCount = static_cast<int>(std::size(kZones));

	inline constexpr Vector3 kZoneLocations[] = {
#include "..\data\LegendaryZones.inc"
	};
	static_assert(std::size(kZoneLocations) == kZoneCount);

	// Global_40.f_9319[i /*4*/]: the array's size slot, then 4 slots a zone.
	inline constexpr int kZoneArray = 40 + 9319;
	inline constexpr int kZoneSize = 4;
	inline constexpr int kFieldRevealed = 0;
	inline constexpr int kFieldKilled = 1;

	inline const UINT64* ZoneField(int zone, int field)
	{
		return GameUtil::Global(kZoneArray + 1 + zone * kZoneSize + field);
	}

	inline bool Killed(int zone)
	{
		const UINT64* killed = ZoneField(zone, kFieldKilled);
		return killed && static_cast<int>(*killed) != 0;
	}

	inline bool Revealed(int zone)
	{
		const UINT64* revealed = ZoneField(zone, kFieldRevealed);
		return revealed && static_cast<int>(*revealed) != 0;
	}

	// The zone of a LegendaryAnimals.inc label, or -1 (the Online ones).
	inline int ZoneOf(std::string_view label)
	{
		for (int i = 0; i < kZoneCount; i++)
			if (label == kZones[i])
				return i;
		return -1;
	}
}
