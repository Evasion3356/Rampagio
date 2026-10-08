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
// time, so a change shows at once. The defaults are Rampage's own (its
// theme globals and Settings.json defaults): a red header and scroller
// over translucent black, the $title1/$body fonts, top left, 12 rows.
struct MenuStyle
{
	ColorRgba main { 138, 3, 3, 255 };          // accent line (Rampage's "Main Color")
	ColorRgba header { 138, 3, 3, 255 };        // the title box
	ColorRgba titleText { 255, 255, 255, 255 };
	ColorRgba subheader { 0, 0, 0, 255 };       // submenu name and counter bar
	ColorRgba base { 0, 0, 0, 170 };            // behind the rows
	ColorRgba scroller { 138, 3, 3, 255 };      // the selected row's bar
	ColorRgba text { 255, 255, 255, 255 };      // row text (fixed white in Rampage)
	ColorRgba selectedText { 255, 255, 255, 255 };
	ColorRgba footer { 0, 0, 0, 220 };
	ColorRgba sectionText { 170, 170, 170, 255 }; // ours: Section captions
	float left = 0.03f;                         // menu's left edge, 0..1
	float top = 0.02f;
	int linesPerScreen = 12;
	bool sounds = true;
	bool gamepad = true;                        // navigate with the d-pad, A and B
	int gamepadOpen = 1;                        // index into MenuInput::kGamepadOpenNames
	int titleFont = 0;                          // index into kTitleFonts (the header)
	int bodyFont = 0;                           // index into kBodyFonts (everything else)
	std::string title = "Rampagio";             // the header's text
	bool invertColors = false;                  // draw every menu color inverted
	bool centeredTitle = true;                  // center the header's text
	bool smoothScroll = true;                   // the scroller glides to the selected row
	int scrollSmoothness = 4;                   // frames' divisor for that glide (Rampage's default)
	bool mouse = false;                         // cursor: hover, click, right-click back, wheel
};

// Rampage's Main Font and Body Font choices: Scaleform font faces.
inline constexpr const char* kTitleFonts[] = { "$title1", "$Font5", "$chalk", "$catalog1", "$catalog5", "$ledger", "$gamername" };
inline constexpr const char* kBodyFonts[] = { "$body", "$title1", "$Font5", "$chalk", "$catalog1", "$catalog5", "$Debug_REG",
	"$body1", "$wantedPostersGeneric", "$ledger", "$gamername", "$Font2" };

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
	string		m_description; // Rampagio: SetDescription's text, else looked up
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
	// Rampagio: the text drawn under the menu while this row is selected
	// (lines split by '\n'): the row's own, else the command's, else
	// Descriptions::Find for this menu's title and the caption.
	void SetDescription(string text) { m_description = std::move(text); }
	std::string_view GetDescription();

	float GetLineWidth()  { return m_lineWidth;  }
	virtual float GetLineHeight() { return m_lineHeight; }

	// Rampagio: the row's text color from Style().
	ColorRgba GetTextColor(bool active) { return active ? Style().selectedText : Style().text; }
	// Rampagio: false for rows the selection skips (Section).
	virtual bool IsSelectable() { return true; }

	void SetMenu(MenuBase *menu) { m_menu = menu; };
	MenuBase *GetMenu() { return m_menu; };
};

// Rampagio: Rampage's menu geometry (screen units, 0..1), read from its
// draw code. The menu is one column kWidth wide whose left edge is
// Style().left - 0.001 (Rampage centers every box at left + 0.114). From
// Style().top down: the header box (centered at +0.046, 0.105 tall), the
// subheader bar (centered at +0.1115, 0.035 tall), the rows from +0.128,
// kRowHeight each, then a footer row. See MenuBase::OnDraw.
namespace MenuLayout
{
	constexpr float kWidth = 0.23f;
	constexpr float kRowHeight = 0.033f;
	constexpr float kTextPad = 0.005f;      // row text's x from the box's left edge
	constexpr float kTextDrop = 0.003f;     // row text's y from the row's top
	constexpr float kTextScale = 0.32f;     // _BG_SET_TEXT_SCALE for row text
}

const float
	MenuItemTitle_lineWidth	 = MenuLayout::kWidth,
	MenuItemTitle_lineHeight = MenuLayout::kRowHeight,
	MenuItemTitle_textLeft	 = MenuLayout::kTextPad;

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
	// Rampagio: the counter is drawn in the subheader (MenuBase::OnDraw).
	virtual	string GetCaption() { return MenuItemTitle::GetCaption(); }
	void SetCurrentItemInfo(int index, int total) { m_currentItemIndex = index, m_itemsTotal = total; }
};

const float
	MenuItemDefault_lineWidth	= MenuLayout::kWidth,
	MenuItemDefault_lineHeight	= MenuLayout::kRowHeight,
	MenuItemDefault_textLeft	= MenuLayout::kTextPad;

// Unused since the Rampage-style renderer: rows take their colors from
// Style() (the box, base and scroller are drawn by MenuBase::OnDraw).
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

// Rampagio addition: an on/off row that isn't a command (rows built in list
// menus or per ped; see Menu.h). onChange runs once on every flip (true =
// just turned on); onTick runs every frame while it's on, menu open or not
// -- MenuController::OnFrame reaches every registered menu's items each
// frame. SetOff() flips it off through onChange, so a feature can undo
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

// Rampagio addition: draws a value row's `value` right-aligned at the row's
// right end, as Rampage does: "<- value ->" with $Font5 arrows while the row
// is selected, the plain value otherwise. Shared by the value rows below.
void DrawRowValue(MenuItemBase* item, float lineTop, float lineLeft, bool active, const std::string& value);

// Screen text in the menu's font (x, y in 0..1); used by the overlays and
// the scanners' world labels. fontSize is the Scaleform SIZE.
// face: a Scaleform font face; nullptr uses Style()'s body font. With
// center, x is the text's center (0..1) instead of its left edge.
void DrawTextAt(float x, float y, const char* str, int fontSize, ColorRgba color, const char* face = nullptr, bool center = false);

// Rampagio: menu text the way Rampage draws it (scriptmenu.cpp): sized by
// _BG_SET_TEXT_SCALE instead of a SIZE tag. Center and Right need the game's
// text format struct (found by pattern on first use): Center puts the
// text's center at x, Right its right end at x + 0.5. Without the struct
// they fall back to an estimate. title also sets the format flag Rampage
// sets for its header text.
enum class TextAlign { Left, Center, Right };
void DrawMenuText(const std::string& text, float x, float y, float scale, ColorRgba color, const char* face = nullptr,
	TextAlign align = TextAlign::Left, bool title = false);

// Rampagio: a texture from a streamed dictionary (requested on first use,
// drawn once loaded), centered at x, y.
void DrawMenuSprite(const char* dict, const char* name, float x, float y, float width, float height, float heading, ColorRgba color);

// Rampagio addition: a number edited with NUMPAD 4/6, drawn as "<- value ->"
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
			DrawRowValue(this, lineTop, lineLeft, active, std::format("{:.2f}", *m_value));
		else
			DrawRowValue(this, lineTop, lineLeft, active, std::format("{}", *m_value));
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
// The selection skips it and the counter leaves it out, like Rampage's
// breaks. Drawn as a centered dimmed caption, or Rampage's divider line
// when the caption is empty.
class MenuItemSection : public MenuItemDefault
{
public:
	MenuItemSection(string caption) : MenuItemDefault(caption) {}
	virtual void OnDraw(float lineTop, float lineLeft, bool active) override;
	virtual bool IsSelectable() override { return false; }
};

class MenuBase
{
	MenuItemTitle *				m_itemTitle;
	vector<MenuItemBase *>		m_items;

	// Rampagio: one absolute index; the page comes from Style().linesPerScreen
	// at draw time, so changing it in Settings can't strand the selection.
	int		m_activeIndex;
	// The rows drawn this frame, for mouse hit-testing (OnDraw fills it).
	struct DrawnRow { int index; float left, top, width, height; };
	vector<DrawnRow>	m_drawnRows;
	float	m_lastCursorX = -1.0f, m_lastCursorY = -1.0f;
	int		OnMouse();
	// Rampagio: the next selectable row from `from` going `step` (+1/-1),
	// wrapping; `from` itself if none is.
	int		NextSelectable(int from, int step) const;
	void	SkipToSelectable();

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
	void Open() { if (m_onOpen) m_onOpen(this); SkipToSelectable(); }
	// Rebuilds the rows in place, keeping the selection where it still fits.
	void Reopen()
	{
		const int index = GetActiveItemIndex();
		Open();
		const int last = static_cast<int>(m_items.size()) - 1;
		m_activeIndex = index > last ? (last < 0 ? 0 : last) : index;
		SkipToSelectable();
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
	void SetActiveItemIndex(int index) { if (index >= 0 && index < static_cast<int>(m_items.size())) m_activeIndex = index; SkipToSelectable(); }
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
