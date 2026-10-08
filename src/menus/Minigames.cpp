/*
	Miscellaneous > Minigames: the sibling minigame mods (ours, not in
	Rampage), linked as static libraries from their submodules (Goal B in
	CLAUDE.md). Each library holds the mechanism; the rows here own its
	options, so they save in Rampagio.json like any other row.

	Five Finger Fillet: external/FFFCheat (FFFCheatLib). Patches the
	fillet_sp script while it runs, each patch behind its own row; see that
	repo's CLAUDE.md for the bytecode and guards.
*/

#include "Menus.h"
#include "..\Log.h"
#include "..\..\external\FFFCheat\src\FFFCheat.h"
#include "..\..\external\FFFCheat\src\FFFCheatLog.h"

namespace
{
	// ---- Five Finger Fillet ----

	FFFCheat::Options g_fillet;

	// Runs once per frame while any fillet row is on.
	void FilletTick()
	{
		static int lastFrame = -1;
		const int frame = MISC::GET_FRAME_COUNT();
		if (frame == lastFrame)
			return;
		lastFrame = frame;
		static const bool initialized = [] {
			FFFCheat::Log::SetSink([](std::string_view line) { Log::Write("[FFFCheat] {}", line); });
			FFFCheat::SetOptionsProvider([] { return g_fillet; });
			return true;
		}();
		FFFCheat::OnTick();
	}

	// A fillet option row: on by default, as the standalone FFFCheat is.
	void FilletToggle(MenuBase* menu, const char* id, const char* caption, bool FFFCheat::Options::* option)
	{
		g_fillet.*option = true;
		Ui::Toggle(menu, id, caption, [option](bool on) {
			g_fillet.*option = on;
			FFFCheat::Reapply();
		}, FilletTick)->SetDefault(true);
	}

	void BuildFiveFingerFillet(MenuBase* minigames)
	{
		MenuBase* menu = Ui::Submenu(minigames, "Five Finger Fillet");
		FilletToggle(menu, "minigames.fff.anybuttoncounts", "Any Button Counts", &FFFCheat::Options::AnyButtonCounts);
		FilletToggle(menu, "minigames.fff.ignoreearlypresses", "Ignore Early Presses", &FFFCheat::Options::IgnoreEarlyPress);
		FilletToggle(menu, "minigames.fff.hidetimermessage", "Hide Timer Message", &FFFCheat::Options::HideTimerMessage);
	}
}

namespace Menus
{
	void BuildMinigames(MenuBase* minigames)
	{
		BuildFiveFingerFillet(minigames);
	}

	void ShutdownMinigames()
	{
		FFFCheat::Shutdown();
	}
}
