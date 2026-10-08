#include "Menu.h"
#include "GameUtil.h"
#include "Log.h"
#include "core\commands\Commands.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <format>

namespace
{
	MenuController g_controller;
	MenuBase* g_root = nullptr;
	std::vector<MenuItemToggle*> g_toggles; // plain toggles, owned by their menus
	int g_listBuildDepth = 0; // > 0 while a ListMenu's build runs

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
			++g_listBuildDepth;
			build(m);
			--g_listBuildDepth;
		});
	}

	// Command ids are for rows built once: a ListMenu rebuild would create
	// the command again (refused as a duplicate, so never saved).
	template <typename T>
	T* Registered(T* command)
	{
		assert(g_listBuildDepth == 0 && "command row inside a ListMenu build; use the id-less overload");
		assert(command->IsRegistered() && "duplicate or invalid command id (see Rampagio.log)");
		if (g_listBuildDepth > 0)
			Log::Write("[Menu] Command row \"{}\" is built inside a list menu, so it isn't saved", command->GetName());
		return command;
	}

	class CommandToggleItem : public MenuItemSwitchable
	{
		Rampagio::BoolCommand* m_command;
	public:
		CommandToggleItem(Rampagio::BoolCommand* command) : MenuItemSwitchable(command->GetLabel()), m_command(command) {}
		Rampagio::Command* GetCommand() override { return m_command; }
		void OnSelect() override { m_command->Call(); }
		void OnDraw(float lineTop, float lineLeft, bool active) override
		{
			SetState(m_command->GetState());
			MenuItemSwitchable::OnDraw(lineTop, lineLeft, active);
		}
	};

	template <typename T>
	class CommandNumberItem : public MenuItemDefault
	{
		Rampagio::NumberCommand<T>* m_command;
		bool m_applyOnSelect;
	public:
		CommandNumberItem(Rampagio::NumberCommand<T>* command, bool applyOnSelect)
			: MenuItemDefault(command->GetLabel()), m_command(command), m_applyOnSelect(applyOnSelect) {}
		Rampagio::Command* GetCommand() override { return m_command; }
		void OnLeft() override { m_command->Step(-1); }
		void OnRight() override { m_command->Step(1); }
		void OnSelect() override { if (m_applyOnSelect) m_command->Call(); }
		void OnDraw(float lineTop, float lineLeft, bool active) override
		{
			MenuItemDefault::OnDraw(lineTop, lineLeft, active);
			if constexpr (std::is_floating_point_v<T>)
				DrawRowValue(this, lineTop, lineLeft, active, std::format("< {:.2f} >", m_command->GetState()));
			else
				DrawRowValue(this, lineTop, lineLeft, active, std::format("< {} >", m_command->GetState()));
		}
	};

	class CommandChoiceItem : public MenuItemDefault
	{
		Rampagio::ListCommand* m_command;
	public:
		CommandChoiceItem(Rampagio::ListCommand* command) : MenuItemDefault(command->GetLabel()), m_command(command) {}
		Rampagio::Command* GetCommand() override { return m_command; }
		void OnLeft() override { m_command->Step(-1); }
		void OnRight() override { m_command->Step(1); }
		void OnSelect() override { m_command->Call(); }
		void OnDraw(float lineTop, float lineLeft, bool active) override
		{
			MenuItemDefault::OnDraw(lineTop, lineLeft, active);
			if (!m_command->GetList().empty())
				DrawRowValue(this, lineTop, lineLeft, active, "< " + m_command->GetSelected() + " >");
		}
	};

	class CommandTextItem : public MenuItemDefault
	{
		Rampagio::StringCommand* m_command;
	public:
		CommandTextItem(Rampagio::StringCommand* command) : MenuItemDefault(""), m_command(command) {}
		Rampagio::Command* GetCommand() override { return m_command; }
		string GetCaption() override
		{
			const std::string& value = m_command->GetState();
			return m_command->GetLabel() + ": " + (value.empty() ? "Not set" : value);
		}
		void OnSelect() override
		{
			std::string text = m_command->GetState();
			if (GameUtil::PromptText(m_command->GetLabel().c_str(), text, 100))
				m_command->SetState(text);
		}
	};

	class CommandActionItem : public MenuItemDefault
	{
		Rampagio::ActionCommand* m_command;
	public:
		CommandActionItem(Rampagio::ActionCommand* command) : MenuItemDefault(command->GetLabel()), m_command(command) {}
		Rampagio::Command* GetCommand() override { return m_command; }
		void OnSelect() override
		{
			const std::string result = m_command->Run();
			if (!result.empty())
				SetStatusText(result, 4000);
		}
	};

	// The root menu's title: Style().title (Settings > Theme > Menu Title).
	class RootTitle : public MenuItemTitle
	{
	public:
		RootTitle() : MenuItemTitle("Rampagio") {}
		std::string GetCaption() override { return Style().title.empty() ? "Rampagio" : Style().title; }
	};
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
			g_root = new MenuBase(new RootTitle());
			g_controller.RegisterMenu(g_root);
		}
		return g_root;
	}

	MenuBase* Submenu(MenuBase* parent, const std::string& title)
	{
		return NewMenu(parent, title);
	}

	void Link(MenuBase* parent, const std::string& title, MenuBase* menu)
	{
		parent->AddItem(new MenuItemMenu(title, menu));
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

	Rampagio::ActionCommand* Action(MenuBase* menu, const std::string& id, const std::string& caption, std::function<std::string()> action)
	{
		auto* command = Registered(new Rampagio::ActionCommand(id, caption, "", std::move(action)));
		menu->AddItem(new CommandActionItem(command));
		return command;
	}

	Rampagio::ActionCommand* Do(MenuBase* menu, const std::string& id, const std::string& caption, std::function<void()> action)
	{
		return Action(menu, id, caption, [action] { action(); return std::string(); });
	}

	Rampagio::BoolCommand* Toggle(MenuBase* menu, const std::string& id, const std::string& caption, std::function<void(bool)> onChange, std::function<void()> onTick)
	{
		Rampagio::BoolCommand* command = onTick
			? new Rampagio::LoopedCommand(id, caption, "", std::move(onTick), std::move(onChange))
			: new Rampagio::BoolCommand(id, caption, "", std::move(onChange));
		menu->AddItem(new CommandToggleItem(Registered(command)));
		return command;
	}

	Rampagio::LoopedCommand* Looped(MenuBase* menu, const std::string& id, const std::string& caption, std::function<void()> onTick, std::function<void()> onOff)
	{
		auto onChange = onOff ? std::function<void(bool)>([onOff](bool on) { if (!on) onOff(); }) : nullptr;
		auto* command = Registered(new Rampagio::LoopedCommand(id, caption, "", std::move(onTick), std::move(onChange)));
		menu->AddItem(new CommandToggleItem(command));
		return command;
	}

	Rampagio::IntCommand* Number(MenuBase* menu, const std::string& id, const std::string& caption, int* value, int min, int max, int step, std::function<void()> onChange, bool applyOnSelect)
	{
		auto* command = Registered(new Rampagio::IntCommand(id, caption, "", min, max, step, *value, value, onChange));
		command->SetHotkeyable(applyOnSelect && onChange);
		menu->AddItem(new CommandNumberItem<int>(command, applyOnSelect));
		return command;
	}

	Rampagio::FloatCommand* Number(MenuBase* menu, const std::string& id, const std::string& caption, float* value, float min, float max, float step, std::function<void()> onChange, bool applyOnSelect)
	{
		auto* command = Registered(new Rampagio::FloatCommand(id, caption, "", min, max, step, *value, value, onChange));
		command->SetHotkeyable(applyOnSelect && onChange);
		menu->AddItem(new CommandNumberItem<float>(command, applyOnSelect));
		return command;
	}

	Rampagio::ListCommand* Choice(MenuBase* menu, const std::string& id, const std::string& caption, std::vector<std::string> options, int* index, std::function<void(int)> onChange)
	{
		auto change = onChange ? std::function<void()>([onChange, index] { onChange(*index); }) : nullptr;
		auto* command = Registered(new Rampagio::ListCommand(id, caption, "", std::move(options), *index, index, std::move(change)));
		menu->AddItem(new CommandChoiceItem(command));
		return command;
	}

	Rampagio::StringCommand* Text(MenuBase* menu, const std::string& id, const std::string& caption, std::string* value, std::function<void()> onChange)
	{
		auto* command = Registered(new Rampagio::StringCommand(id, caption, "", *value, value, std::move(onChange)));
		command->SetHotkeyable(false);
		menu->AddItem(new CommandTextItem(command));
		return command;
	}

	std::string Id(std::string_view prefix, std::string_view caption)
	{
		std::string slug;
		bool color = false;
		for (char c : caption)
		{
			if (c == '~')
				color = !color; // skip ~COLOR_...~ codes
			else if (!color && std::isalnum(static_cast<unsigned char>(c)))
				slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		const std::string base = std::string(prefix) + "." + slug;
		std::string id = base;
		for (int n = 2; Rampagio::Commands::GetCommand(id); ++n)
			id = base + std::to_string(n);
		return id;
	}

	void Transient(MenuBase* menu)
	{
		for (MenuItemBase* item : menu->GetItems())
			if (Rampagio::Command* command = item->GetCommand())
				command->SetTransient();
	}

	void DisableAllToggles()
	{
		for (auto* toggle : g_toggles)
			toggle->SetOff();
		while (g_controller.HasActiveMenu())
			g_controller.PopMenu();
	}
}
