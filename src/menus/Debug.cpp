/*
	Debug: the start of the rework of Rampage's Script Tools area
	(Submenus::SubScriptTools: Script Monitor, Script Patcher, Loader,
	Terminator), which the user wants redone rather than ported (see
	CLAUDE.md, Next steps). Rampage's Debug submenu (Debug Gun) is still
	tabled.

	- Script Monitor (ours): an ImGui window (src/debug/ScriptMonitor.h)
	  that also does what Rampage's Script Patcher, Script Loader, Script
	  Terminator and script editor (Restart, Terminate, Force Cleanup) do,
	  without the user having to know script hashes, function offsets,
	  argument counts, stack sizes or cleanup flags.
	- Global Editor (Rampage's SubGlobalEditor, ours in how it's driven):
	  an ImGui window (src/debug/GlobalEditor.h) taking globals as the
	  decompiled scripts write them, with a saved watch list.

	- Log (ours; it stands in for Rampage's Settings > Window Manager, whose
	  other windows Rampagio covers elsewhere): Rampagio.log's lines this
	  session in an ImGui window (src/debug/LogWindow.h).

	Rampage's Misc > Dev rows Global Editor and Script Tools open the same
	windows (Misc.cpp).

	Not translated: the Script Monitor is for people reading the
	decompiled scripts, so its row stays in English too (src/lang/ignore.txt).
*/

#include "Menus.h"
#include "..\debug\ScriptMonitor.h"
#include "..\debug\GlobalEditor.h"
#include "..\debug\LogWindow.h"

namespace Menus
{
	void BuildDebug(MenuBase* root)
	{
		MenuBase* debug = Ui::Submenu(root, "Debug");
		Ui::Do(debug, "debug.scriptmonitor", "Script Monitor", [] { ScriptMonitor::SetOpen(true); });
		Ui::Describe(debug, "Script threads, their functions, and function/native hooks, in an overlay window.\nThe menu key closes it.");
		Ui::Do(debug, "debug.globaleditor", "Global Editor", [] { GlobalEditor::SetOpen(true); });
		Ui::Describe(debug, "Watch and edit script globals, written as the decompiled scripts write them.\nThe menu key closes it.");
		Ui::Do(debug, "debug.log", "Log", [] { LogWindow::SetOpen(true); });
		Ui::Describe(debug, "Rampagio.log as it's written, in an overlay window.\nThe menu key closes it.");
	}
}
