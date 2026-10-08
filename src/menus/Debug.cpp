/*
	Debug: the start of the rework of Rampage's Script Tools area
	(Submenus::SubScriptTools: Script Monitor, Script Patcher, Loader,
	Terminator), which the user wants redone rather than ported (see
	CLAUDE.md, Next steps). Rampage's Debug submenu (Debug Gun) is still
	tabled.

	- Script Monitor (ours): an ImGui window (src/debug/ScriptMonitor.h)
	  that also does what Rampage's Script Patcher, Script Loader and
	  Script Terminator do, without the user having to know script hashes,
	  function offsets or argument counts.

	Not translated: the Script Monitor is for people reading the
	decompiled scripts, so its row stays in English too (src/lang/ignore.txt).
*/

#include "Menus.h"
#include "..\debug\ScriptMonitor.h"

namespace Menus
{
	void BuildDebug(MenuBase* root)
	{
		MenuBase* debug = Ui::Submenu(root, "Debug");
		Ui::Do(debug, "debug.scriptmonitor", "Script Monitor", [] { ScriptMonitor::SetOpen(true); });
		Ui::Describe(debug, "Script threads, their functions, and function/native hooks, in an overlay window.\nThe menu key closes it.");
	}
}
