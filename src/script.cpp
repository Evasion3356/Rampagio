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
#include "menus/Menus.h"
#include "core/settings/Settings.h"
#include "core/commands/Commands.h"

namespace
{
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
}

void ScriptMain()
{
	Log::Write("Rampagio started");

	BuildMenu();
	LoadSettings();

	bool wasOnline = false;
	while (true)
	{
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
