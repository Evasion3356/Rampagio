/*
	Adapted from the ScriptHookRDR2 SDK's NativeTrainer sample menu framework
	(Alexander Blade, http://dev-c.com), same vendored copy PokerCheat/
	BlackjackCheat/DominoCheat use. Only change from the original: the
	toggle key is configurable (settings.menukey in Rampagio.json, default
	F5, the same as Rampage). The extra item types marked
	"ChallengeCheat addition" came over with the copy from ChallengeCheat.
*/

#pragma once

#include "script.h"
#include "keyboard.h"
#include "ColorRgba.h"

#include <windows.h>
#include <vector>
#include <string>
#include <string_view>
#include <functional>
#include <format>
#include <type_traits>

using namespace std;

namespace Rampagio { class Command; }

class MenuBase;
class MenuController;


// Rampagio addition: the menu's look and input options, edited live in
// Settings and saved by it. Items read their colors from here at draw
// time (MenuItemBase::GetColor*), so a change shows at once.
struct MenuStyle
{
	ColorRgba titleRect { 0, 0, 0, 230 };
	ColorRgba titleText { 255, 255, 255, 255 };
	ColorRgba itemRect { 50, 50, 50, 180 };
	ColorRgba itemText { 255, 255, 255, 200 };
	ColorRgba itemTextActive { 255, 255, 255, 255 };
	ColorRgba border { 204, 0, 0, 255 };       // around the selected row
	ColorRgba sectionRect { 20, 20, 20, 200 };
	ColorRgba sectionText { 200, 160, 90, 255 };
	float left = 0.39f;                         // menu's left edge, 0..1
	float top = 0.05f;
	int linesPerScreen = 11;
	bool sounds = true;
	bool gamepad = true;                        // navigate with the d-pad, A and B
	int gamepadOpen = 1;                        // index into MenuInput::kGamepadOpenNames
};

MenuStyle& Style();

// Rampagio: the virtual key that opens the menu (default F5) and
// MenuItemParagraph's wrap width (units per line, 0 = never wrap). Saved
// by the settings.menukey and settings.wrapwidth commands (Settings.cpp).
int& MenuKey();
int& WrapWidth();

enum eMenuItemClass
{
	Base,
	Title,
	ListTitle,
	Default,
	Switchable,
	Menu
};

class MenuItemBase
{
	float		m_lineWidth;
	float		m_lineHeight;
	float		m_textLeft;
	ColorRgba	m_colorRect;
	ColorRgba	m_colorText;
	ColorRgba	m_colorRectActive;
	ColorRgba	m_colorTextActive;

	MenuBase *	m_menu;
protected:
	MenuItemBase(
		float lineWidth, float lineHeight, float textLeft,
		ColorRgba colorRect, ColorRgba colorText,
		ColorRgba colorRectActive = {}, ColorRgba colorTextActive = {})
		: m_lineWidth(lineWidth), m_lineHeight(lineHeight), m_textLeft(textLeft),
			m_colorRect(colorRect), m_colorText(colorText),
			m_colorRectActive(colorRectActive), m_colorTextActive(colorTextActive)	{}
	void WaitAndDraw(int ms);
	void SetStatusText(string text, int ms = 2500);
public:
	virtual ~MenuItemBase() {}

	virtual eMenuItemClass GetClass() { return eMenuItemClass::Base; }
	virtual void OnDraw(float lineTop, float lineLeft, bool active);
	virtual	void OnSelect() {}
	// Rampagio addition: NUMPAD 4/6 on the active row (value rows use them).
	virtual	void OnLeft() {}
	virtual	void OnRight() {}
	virtual	void OnFrame() {}
	virtual	string GetCaption() { return ""; }
	// Rampagio: the command a row shows (Menu.h id overloads), for F11
	// binding; nullptr for plain rows.
	virtual Rampagio::Command* GetCommand() { return nullptr; }

	float GetLineWidth()  { return m_lineWidth;  }
	virtual float GetLineHeight() { return m_lineHeight; }

	// Rampagio: from Style(), title rows vs. everything else.
	bool IsTitle() { return GetClass() == eMenuItemClass::Title || GetClass() == eMenuItemClass::ListTitle; }
	ColorRgba GetColorRect() { return IsTitle() ? Style().titleRect : Style().itemRect; }
	ColorRgba GetColorText() { return IsTitle() ? Style().titleText : Style().itemText; }

	ColorRgba GetColorRectActive() { return IsTitle() ? Style().titleRect : Style().itemRect; }
	ColorRgba GetColorTextActive() { return IsTitle() ? Style().titleText : Style().itemTextActive; }

	void SetMenu(MenuBase *menu) { m_menu = menu; };
	MenuBase *GetMenu() { return m_menu; };
};

const float
	MenuItemTitle_lineWidth	 = 0.22f,
	MenuItemTitle_lineHeight = 0.06f,
	MenuItemTitle_textLeft	 = 0.01f;

const ColorRgba
	MenuItemTitle_colorRect { 0, 0, 0, 230 },
	MenuItemTitle_colorText { 255, 255, 255, 255 };

class MenuItemTitle : public MenuItemBase
{
	string		m_caption;
public:
	MenuItemTitle(string caption)
		: MenuItemBase(
				MenuItemTitle_lineWidth, MenuItemTitle_lineHeight, MenuItemTitle_textLeft,
				MenuItemTitle_colorRect, MenuItemTitle_colorText
			  ),
		  m_caption(caption) {}
	virtual eMenuItemClass GetClass() { return eMenuItemClass::Title; }
	virtual	string GetCaption() { return m_caption; }
};

class MenuItemListTitle : public MenuItemTitle
{
	int		m_currentItemIndex;
	int		m_itemsTotal;
public:
	MenuItemListTitle(string caption)
		: MenuItemTitle(caption),
			m_currentItemIndex(0), m_itemsTotal(0) {}
	virtual eMenuItemClass GetClass() { return eMenuItemClass::ListTitle; }
	virtual	string GetCaption() { return MenuItemTitle::GetCaption() + "  " + to_string(m_currentItemIndex) + "/" + to_string(m_itemsTotal); }
	void SetCurrentItemInfo(int index, int total) { m_currentItemIndex = index, m_itemsTotal = total; }
};

const float
	MenuItemDefault_lineWidth	= 0.22f,
	MenuItemDefault_lineHeight	= 0.05f,
	MenuItemDefault_textLeft	= 0.01f;

// RDR2-styled palette: a plain grey highlight bar behind every item
// (selected or not, same as the game's own menus), white text throughout,
// and the selected item picked out by a thin red border drawn separately
// in MenuItemBase::OnDraw -- not by a different fill/text color here.
const ColorRgba
	MenuItemDefault_colorRect			{ 50, 50, 50, 180 },
	MenuItemDefault_colorText			{ 255, 255, 255, 200 },
	MenuItemDefault_colorRectActive		{ 50, 50, 50, 180 },
	MenuItemDefault_colorTextActive		{ 255, 255, 255, 255 };

class MenuItemDefault : public MenuItemBase
{
	string		m_caption;
public:
	MenuItemDefault(string caption)
		: MenuItemBase(
			MenuItemDefault_lineWidth, MenuItemDefault_lineHeight, MenuItemDefault_textLeft,
			MenuItemDefault_colorRect, MenuItemDefault_colorText, MenuItemDefault_colorRectActive, MenuItemDefault_colorTextActive
		  ),
		  m_caption(caption) {}
	virtual eMenuItemClass GetClass() { return eMenuItemClass::Default; }
	virtual	string GetCaption() { return m_caption; }
};

class MenuItemSwitchable : public MenuItemDefault
{
	bool	m_state;
public:
	MenuItemSwitchable(string caption)
		: MenuItemDefault(caption),
		m_state(false) {}
	virtual eMenuItemClass GetClass() { return eMenuItemClass::Switchable; }
	virtual void OnDraw(float lineTop, float lineLeft, bool active);
	virtual void OnSelect() { m_state = !m_state; }
	void SetState(bool state) { m_state = state; }
	bool GetState() { return m_state; }
};

class MenuItemMenu : public MenuItemDefault
{
	MenuBase *	m_menu;
public:
	MenuItemMenu(string caption, MenuBase *menu)
		: MenuItemDefault(caption),
		m_menu(menu) {}
	virtual eMenuItemClass GetClass() { return eMenuItemClass::Menu; }
	virtual void OnDraw(float lineTop, float lineLeft, bool active);
	virtual	void OnSelect();
};

// Added for CollectorOffline, kept here: a menu entry that runs an arbitrary
// callback on select, so features don't each need a bespoke MenuItemBase
// subclass.
class MenuItemAction : public MenuItemDefault
{
	std::function<void()>	m_action;
public:
	MenuItemAction(string caption, std::function<void()> action)
		: MenuItemDefault(caption),
		m_action(action) {}
	virtual void OnSelect() override { if (m_action) m_action(); }
};

// ChallengeCheat addition: a read-only row whose caption is recomputed from
// a callback every draw (OnDraw calls GetCaption() every frame already) --
// used for the live "Rank X/Y" status line inside each category's submenu.
// Selecting it does nothing.
class MenuItemLabel : public MenuItemDefault
{
	std::function<std::string()>	m_captionFn;
public:
	MenuItemLabel(std::function<std::string()> captionFn)
		: MenuItemDefault(""),
		m_captionFn(captionFn) {}
	virtual string GetCaption() override { return m_captionFn ? m_captionFn() : ""; }
	virtual void OnSelect() override {}
};

// ChallengeCheat addition: a read-only row that word-wraps a callback's text
// over as many lines as it needs (recomputed when the text changes), so a
// long localized objective ("Rob any 2 coaches or return any 2 stolen coaches
// to the fence") stays inside the menu box. Row height grows with the line
// count -- MenuBase::OnDraw already reads GetLineHeight() per item, which is
// why that accessor is virtual. Selecting it does nothing.
class MenuItemParagraph : public MenuItemDefault
{
	std::function<std::string_view()>	m_textFn; // view must stay valid until the next call
	std::string							m_lastText;   // copied only when the text changes
	std::vector<std::string>		m_lines;
	void Refresh();
public:
	MenuItemParagraph(std::function<std::string_view()> textFn)
		: MenuItemDefault(""),
		m_textFn(textFn) {}
	virtual float GetLineHeight() override;
	virtual void OnDraw(float lineTop, float lineLeft, bool active) override;
	virtual void OnSelect() override {}
};

// ChallengeCheat addition: like MenuItemAction, but the callback returns a
// short result string that's shown via the controller's transient status
// text (same on-screen popup MenuController::SetStatusText already drives)
// instead of requiring the player to alt-tab to the log for immediate
// feedback on whether Advance/Complete actually did anything. An empty
// returned string shows nothing. The caption is ALSO a callback, recomputed
// every draw just like MenuItemLabel's -- lets the caption embed live state
// (e.g. "Advance Bandit 4", where 4 is the next rank) instead of a fixed
// label picked once at menu-build time.
class MenuItemActionStatus : public MenuItemDefault
{
	std::function<std::string()>	m_captionFn;
	std::function<std::string()>	m_action;
public:
	MenuItemActionStatus(std::function<std::string()> captionFn, std::function<std::string()> action)
		: MenuItemDefault(""),
		m_captionFn(captionFn),
		m_action(action) {}
	virtual string GetCaption() override { return m_captionFn ? m_captionFn() : ""; }
	virtual void OnSelect() override
	{
		if (!m_action)
			return;
		std::string result = m_action();
		if (!result.empty())
			SetStatusText(result, 4000);
	}
};

// Rampagio addition: an on/off feature. onChange runs once on every flip
// (true = just turned on); onTick runs every frame while it's on, menu open
// or not -- MenuController::OnFrame reaches every registered menu's items
// each frame. SetOff() flips it off through onChange, so a feature can undo
// whatever it set (used by the online kill switch in script.cpp).
class MenuItemToggle : public MenuItemSwitchable
{
	std::function<void(bool)>	m_onChange;
	std::function<void()>		m_onTick;
	bool						m_persist = true;
public:
	// Rampagio: false keeps it out of Settings' saved toggles (per-ped
	// toggles in the Ped Editor).
	void SetPersist(bool persist) { m_persist = persist; }
	bool Persist() const { return m_persist; }
	MenuItemToggle(string caption, std::function<void(bool)> onChange, std::function<void()> onTick = nullptr)
		: MenuItemSwitchable(caption),
		m_onChange(onChange),
		m_onTick(onTick) {}
	virtual void OnSelect() override
	{
		MenuItemSwitchable::OnSelect();
		if (m_onChange)
			m_onChange(GetState());
	}
	virtual void OnFrame() override
	{
		if (GetState() && m_onTick)
			m_onTick();
	}
	// Rampagio: turns it on through onChange (Settings > Load Toggles).
	void SetOn()
	{
		if (GetState())
			return;
		SetState(true);
		if (m_onChange)
			m_onChange(true);
	}
	void SetOff()
	{
		if (!GetState())
			return;
		SetState(false);
		if (m_onChange)
			m_onChange(false);
	}
};

// Rampagio addition: draws `text` right-aligned-ish at the row's right end,
// the spot MenuItemSwitchable puts [Y]/[N]. Shared by the value rows below.
void DrawRowValue(MenuItemBase* item, float lineTop, float lineLeft, bool active, const std::string& text);

// Screen text in the menu's font (x, y in 0..1); also used for the
// scanners' world labels.
void DrawTextAt(float x, float y, const char* str, int fontSize, ColorRgba color);

// Rampagio addition: a number edited with NUMPAD 4/6, drawn as "< value >"
// on the right. The value lives with the feature (`value` points at it);
// onChange runs after every step, and on select if applyOnSelect.
template <typename T>
class MenuItemNumber : public MenuItemDefault
{
	T*						m_value;
	T						m_min, m_max, m_step;
	std::function<void()>	m_onChange;
	bool					m_applyOnSelect;
	void Step(T delta)
	{
		T v = *m_value + delta;
		*m_value = v < m_min ? m_min : v > m_max ? m_max : v;
		if (m_onChange)
			m_onChange();
	}
public:
	MenuItemNumber(string caption, T* value, T min, T max, T step, std::function<void()> onChange, bool applyOnSelect)
		: MenuItemDefault(caption),
		m_value(value), m_min(min), m_max(max), m_step(step), m_onChange(onChange), m_applyOnSelect(applyOnSelect) {}
	virtual void OnLeft() override { Step(-m_step); }
	virtual void OnRight() override { Step(m_step); }
	virtual void OnSelect() override { if (m_applyOnSelect && m_onChange) m_onChange(); }
	virtual void OnDraw(float lineTop, float lineLeft, bool active) override
	{
		MenuItemDefault::OnDraw(lineTop, lineLeft, active);
		if constexpr (std::is_floating_point_v<T>)
			DrawRowValue(this, lineTop, lineLeft, active, std::format("< {:.2f} >", *m_value));
		else
			DrawRowValue(this, lineTop, lineLeft, active, std::format("< {} >", *m_value));
	}
};

// Rampagio addition: pick one of several named options with NUMPAD 4/6.
// onChange(index) runs after every change, and on select.
class MenuItemChoice : public MenuItemDefault
{
	std::vector<std::string>	m_options;
	int*						m_index;
	std::function<void(int)>	m_onChange;
public:
	MenuItemChoice(string caption, std::vector<std::string> options, int* index, std::function<void(int)> onChange)
		: MenuItemDefault(caption),
		m_options(std::move(options)), m_index(index), m_onChange(onChange) {}
	virtual void OnLeft() override;
	virtual void OnRight() override;
	virtual void OnSelect() override { if (m_onChange) m_onChange(*m_index); }
	virtual void OnDraw(float lineTop, float lineLeft, bool active) override;
};

// Rampagio addition: a heading inside a menu ("Toggles", "Tanks", ...).
// Drawn dimmed; selecting it does nothing.
class MenuItemSection : public MenuItemDefault
{
public:
	MenuItemSection(string caption) : MenuItemDefault(caption) {}
	virtual void OnDraw(float lineTop, float lineLeft, bool active) override;
};

const float
	MenuBase_lineOverlap = 1.0f / 40.0f,
	// Thickness of the red border MenuItemBase::OnDraw adds around
	// whichever item is currently active, in the same 0..1 normalized
	// units as everything else -- RDR2's own menus (and
	// Githubs/RDR2-Native-Menu-Base's DrawSelectionBox()) pick out the
	// selected row with exactly this: a plain grey fill on every row,
	// red only on the selected one's edge.
	MenuBase_activeBorderThickness = 0.0025f;

class MenuBase
{
	MenuItemTitle *				m_itemTitle;
	vector<MenuItemBase *>		m_items;

	// Rampagio: one absolute index; the page comes from Style().linesPerScreen
	// at draw time, so changing it in Settings can't strand the selection.
	int		m_activeIndex;

	MenuController *			m_controller;
	std::function<void(MenuBase*)>	m_onOpen; // Rampagio addition
public:
	MenuBase(MenuItemTitle *itemTitle)
		: m_itemTitle(itemTitle),
		  m_activeIndex(0) {}
	~MenuBase()
	{
		ClearItems();
	}
	void AddItem(MenuItemBase *item) { item->SetMenu(this); m_items.push_back(item); }
	// Rampagio additions: menus whose rows are rebuilt each time they open
	// (lists of nearby peds, saved files, ...). onOpen runs right before
	// the menu is pushed; it usually calls ClearItems() and re-adds rows.
	void SetOnOpen(std::function<void(MenuBase*)> onOpen) { m_onOpen = std::move(onOpen); }
	void Open() { if (m_onOpen) m_onOpen(this); }
	// Rebuilds the rows in place, keeping the selection where it still fits.
	void Reopen()
	{
		const int index = GetActiveItemIndex();
		Open();
		const int last = static_cast<int>(m_items.size()) - 1;
		m_activeIndex = index > last ? (last < 0 ? 0 : last) : index;
	}
	void ClearItems()
	{
		for (auto item : m_items)
			delete item;
		m_items.clear();
		m_activeIndex = 0;
	}
	size_t GetItemCount() const { return m_items.size(); }
	int GetActiveItemIndex() { return m_activeIndex; }
	// Rampagio additions, for Settings > Search and the hotkeys.
	MenuItemTitle* GetTitle() { return m_itemTitle; }
	const vector<MenuItemBase *>& GetItems() const { return m_items; }
	void SetActiveItemIndex(int index) { if (index >= 0 && index < static_cast<int>(m_items.size())) m_activeIndex = index; }
	void OnDraw();
	int OnInput();
	void OnFrame()
	{
		for (size_t i = 0; i < m_items.size(); i++)
			m_items[i]->OnFrame();
	}
	void SetController(MenuController *controller) { m_controller = controller; }
	MenuController *GetController() { return m_controller; }
};

struct MenuInputButtonState
{
	bool a, b, up, down, l, r;
};

class MenuInput
{
public:
	// Frontend (control group 2) inputs for gamepad navigation; RDR2's
	// control hashes are joaat of the input names.
	static constexpr Hash INPUT_FRONTEND_UP = 0x6319DB71;
	static constexpr Hash INPUT_FRONTEND_DOWN = 0x05CA7C52;
	static constexpr Hash INPUT_FRONTEND_LEFT = 0xA65EBAB4;
	static constexpr Hash INPUT_FRONTEND_RIGHT = 0xDEB34313;
	static constexpr Hash INPUT_FRONTEND_ACCEPT = 0xC7B5340A;
	static constexpr Hash INPUT_FRONTEND_CANCEL = 0x156F7119;
	static constexpr Hash INPUT_FRONTEND_RB = 0x17BEC168;
	static constexpr Hash INPUT_FRONTEND_LB = 0xE885EF16;
	static constexpr Hash INPUT_FRONTEND_X = 0x6DB8C62F;

	// Gamepad combos that open the menu (Settings > Gamepad Open Key).
	static constexpr const char* kGamepadOpenNames[] = { "None", "RB + Left", "LB + RB", "RB + X" };

	static bool Pad(Hash input) { return PAD::IS_DISABLED_CONTROL_PRESSED(2, input) != 0; }
	static bool GamepadOpenPressed()
	{
		static bool wasDown = false;
		bool down = false;
		switch (Style().gamepadOpen)
		{
		case 1: down = Pad(INPUT_FRONTEND_RB) && Pad(INPUT_FRONTEND_LEFT); break;
		case 2: down = Pad(INPUT_FRONTEND_LB) && Pad(INPUT_FRONTEND_RB); break;
		case 3: down = Pad(INPUT_FRONTEND_RB) && Pad(INPUT_FRONTEND_X); break;
		}
		const bool pressed = down && !wasDown;
		wasDown = down;
		return pressed && !PAD::IS_USING_KEYBOARD_AND_MOUSE(2);
	}
	// Toggle key is MenuKey() (default F5), or a gamepad combo.
	static bool MenuSwitchPressed()
	{
		return IsKeyJustUp(MenuKey()) || GamepadOpenPressed();
	}
	static MenuInputButtonState GetButtonState()
	{
		const bool pad = Style().gamepad && !PAD::IS_USING_KEYBOARD_AND_MOUSE(2);
		if (pad)
			for (Hash input : { INPUT_FRONTEND_UP, INPUT_FRONTEND_DOWN, INPUT_FRONTEND_LEFT, INPUT_FRONTEND_RIGHT, INPUT_FRONTEND_ACCEPT, INPUT_FRONTEND_CANCEL })
				PAD::DISABLE_CONTROL_ACTION(2, input, TRUE);
		return {
			IsKeyDown(VK_NUMPAD5) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_RETURN)) || (pad && Pad(INPUT_FRONTEND_ACCEPT)),
			IsKeyDown(VK_NUMPAD0) || MenuSwitchPressed() || IsKeyDown(VK_BACK) || (pad && Pad(INPUT_FRONTEND_CANCEL)),
			IsKeyDown(VK_NUMPAD8) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_UP)) || (pad && Pad(INPUT_FRONTEND_UP)),
			IsKeyDown(VK_NUMPAD2) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_DOWN)) || (pad && Pad(INPUT_FRONTEND_DOWN)),
			IsKeyDown(VK_NUMPAD4) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_LEFT)) || (pad && Pad(INPUT_FRONTEND_LEFT)),
			IsKeyDown(VK_NUMPAD6) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_RIGHT)) || (pad && Pad(INPUT_FRONTEND_RIGHT))
		};
	}
	static void MenuInputBeep()
	{
		if (!Style().sounds)
			return;
		AUDIO::_STOP_SOUND_WITH_NAME("NAV_RIGHT", "HUD_SHOP_SOUNDSET");
		AUDIO::PLAY_SOUND_FRONTEND(const_cast<char*>("NAV_RIGHT"), const_cast<char*>("HUD_SHOP_SOUNDSET"), 1, 0);
	}
};


class MenuController
{
	vector<MenuBase *>		m_menuList;
	vector<MenuBase *>		m_menuStack;

	DWORD	m_inputTurnOnTime;

	string	m_statusText;
	DWORD	m_statusTextMaxTicks;
	bool	m_reopenPending = false;

	void InputWait(int ms)		{	m_inputTurnOnTime = GetTickCount() + ms; }
	bool InputIsOnWait()		{	return m_inputTurnOnTime > GetTickCount(); }
	MenuBase *GetActiveMenu()	{	return m_menuStack.size() ? m_menuStack[m_menuStack.size() - 1] : NULL; }
	void DrawStatusText();
public:
	MenuController()
		: m_inputTurnOnTime(0), m_statusTextMaxTicks(0) {}
	~MenuController()
	{
		for (auto menu : m_menuList)
			delete menu;
	}
	bool HasActiveMenu()			{	return m_menuStack.size() > 0; }
	// Rampagio additions, for Settings > Search and the hotkeys.
	const vector<MenuBase *>& GetMenus() const { return m_menuList; }
	const vector<MenuBase *>& GetStack() const { return m_menuStack; }
	MenuBase *GetTopMenu()			{	return GetActiveMenu(); }
	void PushMenu(MenuBase *menu)	{	if (IsMenuRegistered(menu)) m_menuStack.push_back(menu); }
	void PopMenu()					{   if (m_menuStack.size()) m_menuStack.pop_back(); }
	void SetStatusText(string text, int ms) { m_statusText = text, m_statusTextMaxTicks = GetTickCount() + ms; }
	bool IsMenuRegistered(MenuBase *menu)
	{
		for (size_t i = 0; i < m_menuList.size(); i++)
			if (m_menuList[i] == menu)
				return true;
		return false;
	}
	void RegisterMenu(MenuBase *menu)
	{
		if (!IsMenuRegistered(menu))
		{
			menu->SetController(this);
			m_menuList.push_back(menu);
		}
	}
	void OnDraw()
	{
		if (auto menu = GetActiveMenu())
			menu->OnDraw();
		DrawStatusText();
	}
	void OnInput()
	{
		if (InputIsOnWait())
			return;
		if (auto menu = GetActiveMenu())
			if (int waitTime = menu->OnInput())
				InputWait(waitTime);
	}
	void OnFrame()
	{
		for (size_t i = 0; i < m_menuList.size(); i++)
			m_menuList[i]->OnFrame();
	}
	// Rampagio addition: rebuilds the top menu after this frame's input, so
	// a row can ask for its own list to refresh without deleting itself
	// mid-call.
	void ReopenActiveLater()		{	m_reopenPending = true; }
	void Update()
	{
		OnDraw();
		OnInput();
		if (m_reopenPending)
		{
			m_reopenPending = false;
			if (auto menu = GetActiveMenu())
				menu->Reopen();
		}
		OnFrame();
	}
};
