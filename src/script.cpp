/*
	Rampagio -- a ScriptHookRDR2 singleplayer trainer menu, re-implemented
	feature by feature from what the Rampage trainer is reverse-engineered
	to do (see CLAUDE.md for the workflow and the Rampage deobfuscation
	tooling).

	Press F5 in-game to open the menu (NUMPAD 8/2 to move, NUMPAD 5 to
	select, NUMPAD 0/Backspace/F5 to back out -- same controls as the
	sibling mods' menus, which this is built on).

	Singleplayer only: while Red Dead Online is running, every toggle is
	switched off and the menu won't open (same rule as Rampage's own
	net_main_online kill switch).
*/

#include "Menu.h"
#include "Log.h"
#include "LogFallback.h"
#include "GameUtil.h"
#include "GamePointers.h"
#include "Localization.h"
#include "menus/Menus.h"
#include "core/settings/Settings.h"
#include "core/commands/Commands.h"

namespace
{
	DWORD g_scriptThreadId = 0; // the OS thread ScriptMain's fiber runs on

	void BuildMenu()
	{
		MenuBase* root = Ui::Root();
		Menus::BuildPlayer(root);
		Menus::BuildHorse(root);
		Menus::BuildWeapons(root);
		Menus::BuildVehicle(root);
		Menus::BuildTeleport(root);
		Menus::BuildSpawner(root);
		Menus::BuildWorld(root);
		Menus::BuildRecovery(root);
		Menus::BuildMiscellaneous(root);
		Menus::BuildSettings(root);
		Menus::PedEditor::Build(); // links to the Player menus, so after them
	}

	// Rampagio.json next to the .asi, or in %LOCALAPPDATA%\RDR2ASIMods\ when
	// the game folder isn't writable.
	void LoadSettings()
	{
		const LogFallback::SettingsPaths paths = LogFallback::ResolveSettings(
			LogFallback::ModuleDirectory(), L"Rampagio.json", LogFallback::FallbackDirectory());
		if (paths.usedFallback)
			Log::Write("The game folder isn't writable, so settings are saved to {}", LogFallback::ToUtf8(paths.write));
		Menus::RegisterSettings();
		Rampagio::Settings::Initialize(paths.read, paths.write);
		Menus::ApplyLoadedSettings();
		Rampagio::Settings::Flush(); // creates the file, with every value
	}

	// Natives are only safe from DllMain when ScriptHookRDR2 unloads us on
	// the game thread our script ran on, while a game script thread is
	// active (it runs its scripts, and so its reload, from inside a
	// GtaThread::Run hook). Anywhere else, a native that needs the active
	// script thread would crash.
	bool CanCallNatives()
	{
		if (g_scriptThreadId == 0 || GetCurrentThreadId() != g_scriptThreadId)
			return false;
		const GamePointers::Pointers* pointers = GamePointers::Cached();
		return pointers && *pointers->CurrentScriptThread;
	}

	void UndoFeatures()
	{
		// Same as the online kill switch: the saved states stay as they are.
		Rampagio::Commands::Suspend();
		Ui::DisableAllToggles();
	}

	// No C++ objects here, so it can use SEH: a hook that faults during
	// unload is logged instead of taking the game down.
	bool UndoFeaturesGuarded()
	{
		__try
		{
			UndoFeatures();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

void ScriptUnload(bool processExit)
{
	// At process exit the game state goes away anyway.
	if (!processExit && g_scriptThreadId != 0)
	{
		if (!CanCallNatives())
			Log::Write("Ejected outside a game script thread, so features stay applied until the game restarts");
		else if (UndoFeaturesGuarded())
			Log::Write("Ejected: every feature switched off");
		else
			Log::Write("Ejected: a feature faulted while switching off; the rest may stay applied");
	}
	// Writes what the 1 s throttle hasn't yet: only the JSON the script
	// thread built, so no feature state is read from here.
	Rampagio::Settings::TryFlush();
}

void ScriptMain()
{
	Log::Write("Rampagio started");
	g_scriptThreadId = GetCurrentThreadId();

	BuildMenu();
	LoadSettings();
	// Resolved now so an eject can check for an active script thread
	// (ScriptUnload); features resolve them on first use anyway.
	GamePointers::Get();

	bool wasOnline = false;
	DWORD languageRead = 0;
	while (true)
	{
		// The game's language can change in its settings menu.
		if (GetTickCount() - languageRead > 3000)
		{
			Localization::Refresh();
			languageRead = GetTickCount();
		}

		const bool online = GameUtil::IsOnline();
		if (online && !wasOnline)
		{
			Log::Write("Red Dead Online detected -- switching everything off");
			// Undoes every command without changing its saved state; stays
			// suspended for the rest of the session.
			Rampagio::Commands::Suspend();
			Ui::DisableAllToggles();
		}
		wasOnline = online;

		if (!online)
		{
			MenuController& menus = Ui::Controller();
			if (!menus.HasActiveMenu() && MenuInput::MenuSwitchPressed())
				menus.PushMenu(Ui::Root());

			menus.Update();
			Rampagio::Commands::RunLoopedCommands();
			Menus::TickSettings();
		}
		Rampagio::Settings::Tick();

		WAIT(0);
	}
}
