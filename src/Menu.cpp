#include "Menu.h"
#include "GameUtil.h"

#include <algorithm>
#include <cctype>

namespace
{
	MenuController g_controller;
	MenuBase* g_root = nullptr;
	std::vector<MenuItemToggle*> g_toggles; // owned by their menus

	MenuBase* NewMenu(MenuBase* parent, const std::string& title)
	{
		MenuBase* menu = new MenuBase(new MenuItemListTitle(title));
		g_controller.RegisterMenu(menu); // MenuItemMenu::OnSelect's PushMenu only accepts registered menus
		if (parent)
			parent->AddItem(new MenuItemMenu(title, menu));
		return menu;
	}

	void SetBuild(MenuBase* menu, std::function<void(MenuBase*)> build)
	{
		menu->SetOnOpen([build](MenuBase* m)
		{
			std::erase_if(g_toggles, [m](MenuItemToggle* t) { return t->GetMenu() == m; });
			m->ClearItems();
			build(m);
		});
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
		SetBuild(menu, std::move(build));
		return menu;
	}

	MenuBase* DetachedListMenu(const std::string& title, std::function<void(MenuBase*)> build)
	{
		MenuBase* menu = NewMenu(nullptr, title);
		SetBuild(menu, std::move(build));
		return menu;
	}

	void Push(MenuBase* menu)
	{
		menu->Open();
		g_controller.PushMenu(menu);
	}

	MenuBase* NameList(MenuBase* parent, const std::string& title, std::span<const char* const> names,
		std::function<void(const std::string&)> onPick, std::function<void(MenuBase*)> extra)
	{
		MenuBase* menu = NewMenu(parent, title);
		if (extra)
			extra(menu);
		Do(menu, "Custom Input", [onPick]
		{
			std::string name;
			if (GameUtil::PromptText("Enter Name:", name) && !name.empty())
				onPick(name);
		});
		ListMenu(menu, "Search", [names, onPick](MenuBase* results)
		{
			std::string text;
			if (!GameUtil::PromptText("Search:", text) || text.empty())
				return;
			auto lower = [](std::string s)
			{
				std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return s;
			};
			text = lower(text);
			for (const char* name : names)
				if (lower(name).find(text) != std::string::npos)
					Do(results, name, [onPick, name] { onPick(name); });
			if (results->GetItemCount() == 0)
				Section(results, "No matches");
		});
		Section(menu, "All");
		for (const char* name : names)
			Do(menu, name, [onPick, name] { onPick(name); });
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

	const std::vector<MenuItemToggle*>& AllToggles()
	{
		return g_toggles;
	}

	std::string Key(MenuItemBase* item)
	{
		MenuBase* menu = item->GetMenu();
		const std::string title = menu ? menu->GetTitle()->MenuItemTitle::GetCaption() : "";
		return title + " > " + item->GetCaption();
	}

	MenuItemBase* Find(const std::string& key)
	{
		for (MenuBase* menu : g_controller.GetMenus())
			for (MenuItemBase* item : menu->GetItems())
				if (Key(item) == key)
					return item;
		return nullptr;
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

	void Text(MenuBase* menu, const std::string& caption, std::string* value, std::function<void()> onChange)
	{
		menu->AddItem(new MenuItemActionStatus(
			[caption, value] { return caption + ": " + (value->empty() ? "Not set" : *value); },
			[caption, value, onChange]
			{
				std::string text = *value;
				if (GameUtil::PromptText(caption.c_str(), text, 100))
				{
					*value = text;
					if (onChange)
						onChange();
				}
				return std::string();
			}));
	}

	void DisableAllToggles()
	{
		for (auto* toggle : g_toggles)
			toggle->SetOff();
		while (g_controller.HasActiveMenu())
			g_controller.PopMenu();
	}
}
