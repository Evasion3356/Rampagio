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
#include "GameUtil.h"
#include "menus/Menus.h"

namespace
{
	void BuildMenu()
	{
		MenuBase* root = Ui::Root();
		Menus::BuildPlayer(root);
		Menus::BuildHorse(root);
		Menus::BuildWeapons(root);
		Menus::BuildTeleport(root);
		Menus::BuildWorld(root);
		Menus::BuildRecovery(root);
	}
}

void ScriptMain()
{
	Log::Write("Rampagio started");

	BuildMenu();

	bool wasOnline = false;
	while (true)
	{
		const bool online = GameUtil::IsOnline();
		if (online && !wasOnline)
		{
			Log::Write("Red Dead Online detected -- switching everything off");
			Ui::DisableAllToggles();
		}
		wasOnline = online;

		if (!online)
		{
			MenuController& menus = Ui::Controller();
			if (!menus.HasActiveMenu() && MenuInput::MenuSwitchPressed())
				menus.PushMenu(Ui::Root());

			menus.Update();
		}

		WAIT(0);
	}
}
