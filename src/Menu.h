/*
	Small builder API over scriptmenu.h, used by every menu area under
	src/menus/. Each area file declares its rows and implements them in the
	same place, e.g.

		MenuBase* self = Ui::Submenu(root, "Player");
		Ui::Toggle(self, "Godmode", SetGodmode);
		Ui::Action(self, "Heal", Heal);

	Rows:
	  Action   one-shot; returns a status string (empty = no popup)
	  Toggle   onChange(bool) on every flip, onTick() every frame while on
	  Number   int/float edited with NUMPAD 4/6; onChange after each step
	  Choice   one of several named options with NUMPAD 4/6
	  Section  a heading row
	  Submenu  a nested menu; ListMenu is one rebuilt each time it opens
*/

#pragma once

#include "scriptmenu.h"

#include <functional>
#include <string>
#include <vector>

namespace Ui
{
	MenuController& Controller();

	// The root menu, created on first use.
	MenuBase* Root();

	MenuBase* Submenu(MenuBase* parent, const std::string& title);
	// A submenu whose rows build(menu) recreates every time it opens; build
	// gets an empty menu.
	MenuBase* ListMenu(MenuBase* parent, const std::string& title, std::function<void(MenuBase*)> build);

	void Action(MenuBase* menu, const std::string& caption, std::function<std::string()> action);
	// Same, for actions with nothing to report.
	void Do(MenuBase* menu, const std::string& caption, std::function<void()> action);
	MenuItemToggle* Toggle(MenuBase* menu, const std::string& caption, std::function<void(bool)> onChange, std::function<void()> onTick = nullptr);
	// A toggle that only needs a per-frame effect.
	MenuItemToggle* Looped(MenuBase* menu, const std::string& caption, std::function<void()> onTick, std::function<void()> onOff = nullptr);
	void Number(MenuBase* menu, const std::string& caption, int* value, int min, int max, int step, std::function<void()> onChange = nullptr, bool applyOnSelect = false);
	void Number(MenuBase* menu, const std::string& caption, float* value, float min, float max, float step, std::function<void()> onChange = nullptr, bool applyOnSelect = false);
	void Choice(MenuBase* menu, const std::string& caption, std::vector<std::string> options, int* index, std::function<void(int)> onChange = nullptr);
	void Section(MenuBase* menu, const std::string& caption);

	// Switches every toggle off through its onChange (online kill switch).
	void DisableAllToggles();
}
