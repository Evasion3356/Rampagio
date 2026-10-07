/*
	Rampagio -- a ScriptHookRDR2 singleplayer trainer menu, re-implemented
	feature by feature from what the Rampage trainer is reverse-engineered
	to do (see CLAUDE.md for the workflow and the Rampage deobfuscation
	tooling).

	Press F7 in-game to open the menu (NUMPAD 8/2 to move, NUMPAD 5 to
	select, NUMPAD 0/Backspace/F7 to back out -- same controls as the
	sibling mods' menus, which this is built on).

	Singleplayer only: while Red Dead Online is running, every toggle is
	switched off and the menu won't open (same rule as Rampage's own
	net_main_online kill switch).
*/

#include "scriptmenu.h" // pulls in script.h (natives/types/enums/main) and keyboard.h
#include "Log.h"
#include "Features.h"
#include "GameUtil.h"

#include <string>
#include <vector>

namespace
{
	MenuController g_menuController;
	MenuBase* g_mainMenu = nullptr;
	std::vector<MenuItemToggle*> g_toggles; // owned by their menus

	MenuBase* NewSubmenu(const std::string& title)
	{
		MenuBase* menu = new MenuBase(new MenuItemTitle(title));
		g_menuController.RegisterMenu(menu); // required for MenuItemMenu::OnSelect's PushMenu to accept it
		g_mainMenu->AddItem(new MenuItemMenu(title, menu));
		return menu;
	}

	void AddAction(MenuBase* menu, const std::string& caption, std::function<std::string()> action)
	{
		menu->AddItem(new MenuItemActionStatus([caption]() { return caption; }, action));
	}

	void AddToggle(MenuBase* menu, const std::string& caption, std::function<void(bool)> onChange, std::function<void()> onTick = nullptr)
	{
		auto* toggle = new MenuItemToggle(caption, onChange, onTick);
		menu->AddItem(toggle);
		g_toggles.push_back(toggle);
	}

	void BuildMenu()
	{
		g_mainMenu = new MenuBase(new MenuItemTitle("Rampagio"));
		g_menuController.RegisterMenu(g_mainMenu);

		MenuBase* player = NewSubmenu("Player");
		AddToggle(player, "Invincible", Features::InvinciblePlayer_OnChange);
		AddAction(player, "Heal", Features::HealPlayer);
		AddAction(player, "Clean", Features::CleanPlayer);
		AddAction(player, "Clear Bounty", Features::ClearBounty);

		MenuBase* horse = NewSubmenu("Horse");
		AddToggle(horse, "Invincible", Features::InvincibleHorse_OnChange, Features::InvincibleHorse_OnTick);
		AddAction(horse, "Heal", Features::HealHorse);

		MenuBase* teleport = NewSubmenu("Teleport");
		AddAction(teleport, "To Waypoint", Features::TeleportToWaypoint);

		MenuBase* world = NewSubmenu("World");
		AddAction(world, "Time +1 Hour", [] { return Features::AddClockHours(1); });
	}

	void DisableAll()
	{
		for (auto* toggle : g_toggles)
			toggle->SetOff();
		while (g_menuController.HasActiveMenu())
			g_menuController.PopMenu();
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
			DisableAll();
		}
		wasOnline = online;

		if (!online)
		{
			if (!g_menuController.HasActiveMenu() && MenuInput::MenuSwitchPressed())
				g_menuController.PushMenu(g_mainMenu);

			g_menuController.Update();
		}

		WAIT(0);
	}
}
