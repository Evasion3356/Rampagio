/*
	Small builder API over scriptmenu.h, used by every menu area under
	src/menus/. Each area file declares its rows and implements them in the
	same place, e.g.

		MenuBase* self = Ui::Submenu(root, "Player");
		Ui::Toggle(self, "player.godmode", "Godmode", SetGodmode);
		Ui::Action(self, "player.heal", "Heal", Heal);

	Rows:
	  Action   one-shot; returns a status string (empty = no popup)
	  Toggle   onChange(bool) on every flip, onTick() every frame while on
	  Number   int/float edited with NUMPAD 4/6; onChange after each step
	  Choice   one of several named options with NUMPAD 4/6
	  Section  a heading row
	  Text     "Caption: value"; selecting it types a new value
	  Submenu  a nested menu; ListMenu is one rebuilt each time it opens
	  NameList a submenu picking one of many names, with Search and Custom
	           Input rows

	Commands: the overloads that take an id first ("<area>.<feature>",
	lowercase) create a command (src/core/commands) for the row and
	register it. Its state is saved in Rampagio.json under that id, it can
	be bound to a hotkey (F11), and toggles tick from the main loop
	(Commands::RunLoopedCommands), so they work in menus never opened. Use
	them for every row built once in a Menus::BuildXxx. Rows built inside a
	ListMenu/DetachedListMenu build, or per ped, use the id-less overloads:
	those rows aren't saved and can't be bound.
*/

#pragma once

#include "scriptmenu.h"
#include "core\commands\BoolCommand.h"
#include "core\commands\LoopedCommand.h"
#include "core\commands\ValueCommands.h"
#include "core\commands\ActionCommand.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Ui
{
	MenuController& Controller();

	// The root menu, created on first use.
	MenuBase* Root();

	MenuBase* Submenu(MenuBase* parent, const std::string& title);
	// A row in `parent` that opens an existing menu, so one menu can be
	// reached from two places (the Ped Editor reuses the Player submenus).
	void Link(MenuBase* parent, const std::string& title, MenuBase* menu);
	// A submenu whose rows build(menu) recreates every time it opens; build
	// gets an empty menu.
	MenuBase* ListMenu(MenuBase* parent, const std::string& title, std::function<void(MenuBase*)> build);
	// A ListMenu with no row leading to it; show it with Push.
	MenuBase* DetachedListMenu(const std::string& title, std::function<void(MenuBase*)> build);
	// Rebuilds `menu` if it's a list and shows it on top of the current one.
	void Push(MenuBase* menu);

	// A submenu with a row per name, plus "Custom Input" (type any name) and
	// "Search" (type part of a name, list the matches) above them. onPick
	// gets the chosen name; extra(menu) adds rows (Stop, Clear, ...) first.
	MenuBase* NameList(MenuBase* parent, const std::string& title, std::span<const char* const> names,
		std::function<void(const std::string&)> onPick, std::function<void(MenuBase*)> extra = nullptr);

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
	// Sets the text shown under the menu while the row last added to `menu`
	// is selected (lines split by '\n'), for rows the description tables
	// (src/Descriptions.h) don't cover.
	void Describe(MenuBase* menu, std::string text);
	// Sets the model whose picture Settings > Theme > Spawner Previews shows
	// while the row last added to `menu` is selected (Previews.h); variant
	// is its outfit preset, -1 for any.
	void Preview(MenuBase* menu, Hash model, int variant = -1);
	// Sets the place Settings > Theme > Teleport Map shows beside the menu
	// while the row last added to `menu` is selected (TeleportMap.h).
	void MapPoint(MenuBase* menu, float x, float y);
	// Shows "caption: *value" ("Not set" while empty); selecting it opens the
	// on-screen keyboard on the current value. onChange runs after an edit.
	void Text(MenuBase* menu, const std::string& caption, std::string* value, std::function<void()> onChange = nullptr);

	// Command rows (see the header comment). Toggle returns a
	// LoopedCommand when it has an onTick. Number's applyOnSelect also makes
	// it hotkeyable (select = apply); Choice is hotkeyable when it has an
	// onChange; Text never is (it opens the on-screen keyboard).
	Rampagio::ActionCommand* Action(MenuBase* menu, const std::string& id, const std::string& caption, std::function<std::string()> action);
	Rampagio::ActionCommand* Do(MenuBase* menu, const std::string& id, const std::string& caption, std::function<void()> action);
	Rampagio::BoolCommand* Toggle(MenuBase* menu, const std::string& id, const std::string& caption, std::function<void(bool)> onChange, std::function<void()> onTick = nullptr);
	Rampagio::LoopedCommand* Looped(MenuBase* menu, const std::string& id, const std::string& caption, std::function<void()> onTick, std::function<void()> onOff = nullptr);
	Rampagio::IntCommand* Number(MenuBase* menu, const std::string& id, const std::string& caption, int* value, int min, int max, int step, std::function<void()> onChange = nullptr, bool applyOnSelect = false);
	Rampagio::FloatCommand* Number(MenuBase* menu, const std::string& id, const std::string& caption, float* value, float min, float max, float step, std::function<void()> onChange = nullptr, bool applyOnSelect = false);
	Rampagio::ListCommand* Choice(MenuBase* menu, const std::string& id, const std::string& caption, std::vector<std::string> options, int* index, std::function<void(int)> onChange = nullptr);
	Rampagio::StringCommand* Text(MenuBase* menu, const std::string& id, const std::string& caption, std::string* value, std::function<void()> onChange = nullptr);

	// An id for a row built in a loop over a fixed table: prefix + "." +
	// caption lowercased, keeping a-z and 0-9 ("player.proof", "Bullets" ->
	// "player.proof.bullets"). A repeat gets "2", "3", ... appended, so the
	// table's order decides those.
	std::string Id(std::string_view prefix, std::string_view caption);
	// Marks every command row in `menu` as not saved: for menus whose rows
	// show the game's own state, re-read when the menu opens.
	void Transient(MenuBase* menu);

	// Online kill switch and eject, for the rows Commands::Suspend doesn't reach:
	// switches every plain toggle (list and per-ped rows) off through its
	// onChange and closes the menu.
	void DisableAllToggles();
}
