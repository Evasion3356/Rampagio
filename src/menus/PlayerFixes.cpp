/*
	Player > Fixes (ours, not in Rampage): the frame-timing fixes from the
	FishingFix submodule (external/FishingFix, FishingFixLib.vcxproj). Its
	src/FishingFix.cpp and src/DeadEyeFix.cpp headers have the derivation.

	Above ~60 FPS the script thread reads the fishing task's state faster
	than the task's worker thread updates it, so a cast never commits; Dead
	Eye has the same race. Each fix busy-waits 4 ms on the script thread
	only while its race window is open, so both are on by default, and
	their saved state comes back on start like an option's.
*/

#include "Menus.h"
#include "..\Log.h"
#include "..\..\external\FishingFix\src\DeadEyeFix.h"
#include "..\..\external\FishingFix\src\FishingFix.h"
#include "..\..\external\FishingFix\src\FishingFixLog.h"

namespace
{
	// Resolves the library's signatures and routes its log into ours.
	void InitFishingFix()
	{
		static bool initialized = false;
		if (initialized)
			return;
		initialized = true;
		FishingFix::Log::SetSink([](std::string_view line) { Log::Write("[FishingFix] {}", line); });
		FishingFix::Init();
	}
}

namespace Menus
{
	void BuildPlayerFixes(MenuBase* self)
	{
		Ui::Section(self, "Fixes");
		Ui::Looped(self, "player.fishingcastfix", "Fishing Cast Fix", [] {
			InitFishingFix();
			FishingFix::Tick();
		})->SetDefault(true)->SetAlwaysRestore();
		Ui::Looped(self, "player.deadeyefix", "Dead Eye Fix", [] {
			InitFishingFix();
			FishingFix::DeadEyeFix::OnTick();
		})->SetDefault(true)->SetAlwaysRestore();
	}
}
