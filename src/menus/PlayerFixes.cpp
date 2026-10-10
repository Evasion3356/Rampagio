/*
	Player > Fixes (ours, not in Rampage): the frame-timing fixes from the
	FishingFix submodule (external/FishingFix, FishingFixLib.vcxproj). Its
	src/FishingFix.cpp and src/DeadEyeFix.cpp headers have the derivation.

	Above ~60 FPS the script thread reads the fishing task's state faster
	than the task's worker thread updates it, so a cast never commits; Dead
	Eye has the same race. Each fix busy-waits 4 ms on the script thread
	only while its race window is open, so both are on by default, and
	their saved state comes back on start like an option's.

	The standalone FishingFix.asi runs the same two fixes. While it's
	loaded, ours stand down so the wait isn't applied twice.
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

	// True while the standalone FishingFix.asi is loaded; logs each change.
	bool StandaloneLoaded()
	{
		static int logged = -1;
		const bool loaded = GetModuleHandleW(L"FishingFix.asi") != nullptr;
		if (static_cast<int>(loaded) != logged)
		{
			const bool first = logged == -1;
			logged = loaded;
			if (loaded)
				Log::Write("[FishingFix] FishingFix.asi is loaded; Player > Fixes stands down while it is");
			else if (!first)
				Log::Write("[FishingFix] FishingFix.asi is gone; Player > Fixes runs again");
		}
		return loaded;
	}
}

namespace Menus
{
	void BuildPlayerFixes(MenuBase* self)
	{
		Ui::Section(self, "Fixes");
		Ui::Looped(self, "player.fishingcastfix", "Fishing Cast Fix", [] {
			if (StandaloneLoaded())
				return;
			InitFishingFix();
			FishingFix::Tick();
		})->SetDefault(true);
		Ui::Looped(self, "player.deadeyefix", "Dead Eye Fix", [] {
			if (StandaloneLoaded())
				return;
			InitFishingFix();
			FishingFix::DeadEyeFix::OnTick();
		})->SetDefault(true);
	}
}
