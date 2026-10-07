#include "Menu.h"

namespace
{
	MenuController g_controller;
	MenuBase* g_root = nullptr;
	std::vector<MenuItemToggle*> g_toggles; // owned by their menus

	MenuBase* NewMenu(MenuBase* parent, const std::string& title)
	{
		MenuBase* menu = new MenuBase(new MenuItemListTitle(title));
		g_controller.RegisterMenu(menu); // MenuItemMenu::OnSelect's PushMenu only accepts registered menus
		parent->AddItem(new MenuItemMenu(title, menu));
		return menu;
	}
}

namespace Ui
{
	MenuController& Controller()
	{
		return g_controller;
	}

	MenuBase* Root()
	{
		if (!g_root)
		{
			g_root = new MenuBase(new MenuItemTitle("Rampagio"));
			g_controller.RegisterMenu(g_root);
		}
		return g_root;
	}

	MenuBase* Submenu(MenuBase* parent, const std::string& title)
	{
		return NewMenu(parent, title);
	}

	MenuBase* ListMenu(MenuBase* parent, const std::string& title, std::function<void(MenuBase*)> build)
	{
		MenuBase* menu = NewMenu(parent, title);
		menu->SetOnOpen([build](MenuBase* m)
		{
			std::erase_if(g_toggles, [m](MenuItemToggle* t) { return t->GetMenu() == m; });
			m->ClearItems();
			build(m);
		});
		return menu;
	}

	void Action(MenuBase* menu, const std::string& caption, std::function<std::string()> action)
	{
		menu->AddItem(new MenuItemActionStatus([caption]() { return caption; }, action));
	}

	void Do(MenuBase* menu, const std::string& caption, std::function<void()> action)
	{
		menu->AddItem(new MenuItemAction(caption, action));
	}

	MenuItemToggle* Toggle(MenuBase* menu, const std::string& caption, std::function<void(bool)> onChange, std::function<void()> onTick)
	{
		auto* toggle = new MenuItemToggle(caption, onChange, onTick);
		menu->AddItem(toggle);
		g_toggles.push_back(toggle);
		return toggle;
	}

	MenuItemToggle* Looped(MenuBase* menu, const std::string& caption, std::function<void()> onTick, std::function<void()> onOff)
	{
		return Toggle(menu, caption, [onOff](bool on) { if (!on && onOff) onOff(); }, onTick);
	}

	void Number(MenuBase* menu, const std::string& caption, int* value, int min, int max, int step, std::function<void()> onChange, bool applyOnSelect)
	{
		menu->AddItem(new MenuItemNumber<int>(caption, value, min, max, step, onChange, applyOnSelect));
	}

	void Number(MenuBase* menu, const std::string& caption, float* value, float min, float max, float step, std::function<void()> onChange, bool applyOnSelect)
	{
		menu->AddItem(new MenuItemNumber<float>(caption, value, min, max, step, onChange, applyOnSelect));
	}

	void Choice(MenuBase* menu, const std::string& caption, std::vector<std::string> options, int* index, std::function<void(int)> onChange)
	{
		menu->AddItem(new MenuItemChoice(caption, std::move(options), index, onChange));
	}

	void Section(MenuBase* menu, const std::string& caption)
	{
		menu->AddItem(new MenuItemSection(caption));
	}

	void DisableAllToggles()
	{
		for (auto* toggle : g_toggles)
			toggle->SetOff();
		while (g_controller.HasActiveMenu())
			g_controller.PopMenu();
	}
}
