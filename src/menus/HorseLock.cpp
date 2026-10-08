/*
	Horse > Horse Stats: Keep Cores Golden and Lock Stats (ours, not in
	Rampage). Ported from ..\GoldHorse's HorseStatLock.cpp (itself from
	..\HorseStatLock), singleplayer only: GoldHorse's Online bonding parts
	are left out. Its header comment has the script audit; in short:

	- Both act on the game's own main horse, Global_1900383[slot, stride
	  45] for slot Global_40.f_1095.f_3054 (player_horse), found while
	  dismounted and never a stolen or borrowed mount. The globals are
	  build-specific, so they're used only while the arrays' size words
	  match 1491.50's (7 horse slots, 2 cores, 3 statuses).
	- Health, stamina, handling, speed and acceleration base ranks (0, 1,
	  4, 5, 6) aren't saved: a reload brings back the breed's defaults, so
	  Lock Stats reapplies the Horse Stats values whenever they differ
	  (confirmed live in HorseStatLock, 2026-09-27). Weight (13) is saved
	  but drifts: player_horse's status tick moves it a point whenever the
	  save record's accumulator reaches +-1, so the lock zeroes that
	  accumulator too.
	- Keep Cores Golden re-enables health and stamina core and attribute
	  overpower (GoldHorse's bSetHorseCoresToGolden); Rampage's Cores
	  Overpower row does it once.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\Log.h"

#include <vector>

namespace
{
	constexpr int kHorseSlots = 7;
	constexpr int kHorsePeds = 1900383;       // [slot, stride 45]: live horse ped
	constexpr int kHorsePedStride = 45;
	constexpr int kHorseRecords = 40 + 1095 + 1; // Global_40.f_1095.f_1[slot, stride 436]: save record
	constexpr int kHorseRecordStride = 436;
	constexpr int kMainHorseSlot = 40 + 1095 + 3054;
	constexpr int kRecordCores = 398;          // [2, stride 4]
	constexpr int kRecordStatuses = 407;       // [3, stride 4]: dirtiness, hunger, weight
	constexpr int kWeightStatus = 2;
	constexpr int kWeightAttribute = 13;
	constexpr int kTickMs = 100;               // GoldHorse's interval

	int ReadInt(int index)
	{
		const UINT64* slot = GameUtil::Global(index);
		return slot ? static_cast<int>(*slot & 0xFFFFFFFFu) : 0;
	}

	// Element i of the array whose size word is at `array`.
	int Element(int array, int i, int stride) { return array + 1 + i * stride; }

	bool LayoutMatches()
	{
		const int record0 = Element(kHorseRecords, 0, kHorseRecordStride);
		return ReadInt(kHorsePeds) == kHorseSlots && ReadInt(kHorseRecords) == kHorseSlots
			&& ReadInt(record0 + kRecordCores) == 2 && ReadInt(record0 + kRecordStatuses) == 3;
	}

	struct MainHorse
	{
		Ped ped = 0;
		int slot = -1;
	};

	MainHorse FindMainHorse()
	{
		if (!LayoutMatches())
			return {};
		const int slot = ReadInt(kMainHorseSlot);
		if (slot < 0 || slot >= kHorseSlots)
			return {};
		const Ped horse = ReadInt(Element(kHorsePeds, slot, kHorsePedStride));
		if (!horse || !ENTITY::DOES_ENTITY_EXIST(horse) || ENTITY::IS_ENTITY_DEAD(horse) || PED::IS_PED_INJURED(horse))
			return {};
		return { horse, slot };
	}

	// The record's weight-drift accumulator (statuses[weight].f_1, a float).
	void ZeroWeightDrift(int slot)
	{
		const int record = Element(kHorseRecords, slot, kHorseRecordStride);
		if (UINT64* accumulator = GameUtil::Global(Element(record + kRecordStatuses, kWeightStatus, 4) + 1))
			*reinterpret_cast<float*>(accumulator) = 0.0f;
	}

	// Runs `apply` on the main horse at most every kTickMs.
	template <typename F>
	void Throttled(int& next, F apply)
	{
		const int now = MISC::GET_GAME_TIMER();
		if (now < next)
			return;
		next = now + kTickMs;
		if (const MainHorse main = FindMainHorse(); main.ped)
			apply(main);
	}

	std::vector<Menus::HorseLock::Target> g_targets;
	bool g_locked = false;

	void LockTick()
	{
		static int next = 0;
		Throttled(next, [](const MainHorse& main) {
			for (const Menus::HorseLock::Target& target : g_targets)
			{
				if (target.attribute == kWeightAttribute)
					ZeroWeightDrift(main.slot);
				if (ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(main.ped, target.attribute) != *target.value)
					ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(main.ped, target.attribute, *target.value);
			}
		});
	}

	void GoldenCoresTick()
	{
		static int next = 0;
		Throttled(next, [](const MainHorse& main) {
			for (int core = 0; core < 2; core++)
			{
				ATTRIBUTE::_ENABLE_ATTRIBUTE_CORE_OVERPOWER(main.ped, core, 1000.0f, FALSE);
				ATTRIBUTE::ENABLE_ATTRIBUTE_OVERPOWER(main.ped, core, 1000.0f, FALSE);
			}
		});
	}
}

namespace Menus::HorseLock
{
	void Build(MenuBase* stats, std::vector<Target> targets)
	{
		g_targets = std::move(targets);
		Ui::Looped(stats, "horse.keepcoresgolden", "Keep Cores Golden", GoldenCoresTick);
		Ui::Describe(stats, "Keeps your main horse's health and stamina cores gold, mounted or not.");
		Ui::Looped(stats, "horse.lockstats", "Lock Stats", [] { g_locked = true; LockTick(); }, [] { g_locked = false; });
		Ui::Describe(stats, "Holds your main horse at the values below: the game forgets the ranks on reload, and weight drifts.");
	}

	bool Locked() { return g_locked; }
}
