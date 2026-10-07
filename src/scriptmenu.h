/*
	Adapted from the ScriptHookRDR2 SDK's NativeTrainer sample menu framework
	(Alexander Blade, http://dev-c.com), same vendored copy PokerCheat/
	BlackjackCheat/DominoCheat use. Only change from the original: the
	toggle key is configurable (Rampagio.ini, default F5, the same as
	Rampage). The extra item types marked
	"ChallengeCheat addition" came over with the copy from ChallengeCheat.
*/

#pragma once

#include "script.h"
#include "keyboard.h"
#include "Config.h"

#include <windows.h>
#include <vector>
#include <string>
#include <string_view>
#include <functional>

using namespace std;

class MenuBase;
class MenuController;

struct ColorRgba
{
	unsigned char	r, g, b, a;
};

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
	virtual	void OnFrame() {}
	virtual	string GetCaption() { return ""; }

	float GetLineWidth()  { return m_lineWidth;  }
	virtual float GetLineHeight() { return m_lineHeight; }

	ColorRgba GetColorRect() { return m_colorRect; }
	ColorRgba GetColorText() { return m_colorText; }

	ColorRgba GetColorRectActive() { return m_colorRectActive; }
	ColorRgba GetColorTextActive() { return m_colorTextActive; }

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
public:
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
	void SetOff()
	{
		if (!GetState())
			return;
		SetState(false);
		if (m_onChange)
			m_onChange(false);
	}
};

const int
	MenuBase_linesPerScreen = 11;

const float
	MenuBase_menuTop  = 0.05f,
	MenuBase_menuLeft = 0.5f - MenuItemDefault_lineWidth / 2.0f,
	MenuBase_lineOverlap = 1.0f / 40.0f,
	// Thickness of the red border MenuItemBase::OnDraw adds around
	// whichever item is currently active, in the same 0..1 normalized
	// units as everything else -- RDR2's own menus (and
	// Githubs/RDR2-Native-Menu-Base's DrawSelectionBox()) pick out the
	// selected row with exactly this: a plain grey fill on every row,
	// red only on the selected one's edge.
	MenuBase_activeBorderThickness = 0.0025f;

const ColorRgba
	MenuBase_activeBorderColor { 204, 0, 0, 255 };

class MenuBase
{
	MenuItemTitle *				m_itemTitle;
	vector<MenuItemBase *>		m_items;

	int		m_activeLineIndex;
	int		m_activeScreenIndex;

	MenuController *			m_controller;
public:
	MenuBase(MenuItemTitle *itemTitle)
		: m_itemTitle(itemTitle),
		  m_activeLineIndex(0), m_activeScreenIndex(0) {}
	~MenuBase()
	{
		for (auto item : m_items)
			delete item;
	}
	void AddItem(MenuItemBase *item) { item->SetMenu(this); m_items.push_back(item); }
	int GetActiveItemIndex() { return m_activeScreenIndex * MenuBase_linesPerScreen + m_activeLineIndex; }
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
	// Toggle key comes from Rampagio.ini's [General] MenuKey (default F5,
	// see Config.h).
	static bool MenuSwitchPressed()
	{
		return IsKeyJustUp(Config::Get().MenuKey);
	}
	static MenuInputButtonState GetButtonState()
	{
		return {
			IsKeyDown(VK_NUMPAD5) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_RETURN)),
			IsKeyDown(VK_NUMPAD0) || MenuSwitchPressed() || IsKeyDown(VK_BACK),
			IsKeyDown(VK_NUMPAD8) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_UP)),
			IsKeyDown(VK_NUMPAD2) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_DOWN)),
			IsKeyDown(VK_NUMPAD6) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_RIGHT)),
			IsKeyDown(VK_NUMPAD4) || (IsKeyDownLong(VK_CONTROL) && IsKeyDown(VK_LEFT))
		};
	}
	static void MenuInputBeep()
	{
		AUDIO::STOP_SOUND_FRONTEND(const_cast<char*>("NAV_RIGHT"), const_cast<char*>("HUD_SHOP_SOUNDSET"));
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
	void Update()
	{
		OnDraw();
		OnInput();
		OnFrame();
	}
};
