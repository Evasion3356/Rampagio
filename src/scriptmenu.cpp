/*
	Adapted from the ScriptHookRDR2 SDK's NativeTrainer sample (Alexander
	Blade, http://dev-c.com). Changes from that 2019 sample, on top of the
	two PokerCheat/BlackjackCheat/DominoCheat already made (forced by
	building against a newer C++ standard) -- the F9 toggle key itself
	lives in scriptmenu.h's MenuInput::MenuSwitchPressed(), not here:
	  - DrawText renamed DrawTextAt: windows.h #defines DrawText to
	    DrawTextA (Win32's own function) when built with the MultiByte
	    character set, which silently swallowed this function's own
	    definition/calls into calling the wrong (Win32) function entirely.
	  - const_cast at the one call site that hands a literal to a stock
	    native declared char* (not const char*) -- invoke<> doesn't care
	    about constness, it's a pass-by-value template, so this is safe.
	  - Font (2026-09-19): DrawTextAt's original plain UI::DRAW_TEXT/
	    SET_TEXT_COLOR_RGBA are documented nullsub since game build 1436
	    (see this SDK's own natives.h comments on those two natives, and
	    Poker/Blackjack/DominoCheat's independent live confirmation of the
	    same thing) -- this game is build 1491.50, long past that. Ported
	    the working replacement those sibling projects already use for
	    every piece of real user-facing text: UIDEBUG::_BG_DISPLAY_TEXT/
	    _BG_SET_TEXT_COLOR, fed a Scaleform rich-text font tag.
	  - Look (2026-10-08): Rampage's menu, re-implemented from its draw
	    code (Rampage.asi 2026-01-04: sub_1801F6A60 draws the frame,
	    sub_1801ED140 a row's label, sub_1801EE020 a toggle). A red header
	    box with the title in $title1, a black subheader with the submenu
	    name and an "n/total" counter, the rows over translucent black with
	    a red scroller bar behind the selected one (gliding to it), a
	    footer with up/down arrows, toggles as checkbox sprites, submenus
	    with an arrow sprite and values as "<- value ->". Text is sized
	    with _BG_SET_TEXT_SCALE and aligned through the game's text format
	    struct, both as Rampage does (DrawMenuText). Every size and offset
	    below is Rampage's; MenuLayout (scriptmenu.h) names the shared ones.
	    Replaces the ChallengeCheat look (grey rows, red border).
*/

#include "scriptmenu.h"
#include "Descriptions.h"
#include "Localization.h"
#include "PatternScan.h"
#include "core\commands\Command.h"
#include "Log.h"
#include <algorithm>
#include <climits>
#include <cmath>

using namespace MenuLayout;

int& MenuKey()
{
	static int key = VK_F5;
	return key;
}

int& WrapWidth()
{
	// How many "units" of text fit on one line (a Latin/Cyrillic character
	// = 1, a CJK/Hangul/kana one = 2). An estimate, since there's no
	// text-measuring call: raise it if lines wrap too early.
	static int width = 50;
	return width;
}

MenuStyle& Style()
{
	static MenuStyle style;
	return style;
}

namespace
{
	// Set while MenuBase::OnDraw draws, so Invert Colors only touches the
	// menu, not the overlays that share DrawTextAt.
	bool g_drawingMenu = false;

	// Settings > Theme > Invert Colors: every menu color drawn inverted.
	ColorRgba Shown(ColorRgba c)
	{
		if (!g_drawingMenu || !Style().invertColors)
			return c;
		return { static_cast<unsigned char>(255 - c.r), static_cast<unsigned char>(255 - c.g), static_cast<unsigned char>(255 - c.b), c.a };
	}

	const char* BodyFace()
	{
		const int body = Style().bodyFont;
		return body >= 0 && body < static_cast<int>(std::size(kBodyFonts)) ? kBodyFonts[body] : "$body";
	}

	const char* TitleFace()
	{
		const int title = Style().titleFont;
		return title >= 0 && title < static_cast<int>(std::size(kTitleFonts)) ? kTitleFonts[title] : "$title1";
	}

	// Rampage gives sprite sizes in pixels of a 1280x720 screen.
	constexpr float PxW(float px) { return px / 1280.0f; }
	constexpr float PxH(float px) { return px / 720.0f; }

	constexpr ColorRgba kWhite{ 255, 255, 255, 255 };
	constexpr ColorRgba kCheckbox{ 220, 220, 220, 255 }; // Rampage's unselected checkbox tint

	// The game's format for the next _BG_DISPLAY_TEXT, found the way
	// Rampage finds it (its Pointers::Run, lambda 1): a LEA of the struct.
	// Rampage clears byte 28 before text it centers or right-aligns, which
	// makes the text field centered on x, 1.0 wide (so Right ends at
	// x + 0.5), and sets byte 30 for its header title. The game resets
	// them after each display: Rampage never sets them back.
	constexpr const char* kTextFormatPattern = "48 8D 05 ? ? ? ? 48 89 44 24 ? 8B 05 ? ? ? ? 89 44 24 28 8B 05 ? ? ? ? 89 44 24 20";
	constexpr size_t kFormatAlignByte = 28;
	constexpr size_t kFormatTitleByte = 30;

	unsigned char* TextFormat()
	{
		static unsigned char* format = nullptr;
		static bool scanned = false;
		if (!scanned)
		{
			scanned = true;
			if (const auto match = PatternScan::FindInMainModule(kTextFormatPattern))
				format = reinterpret_cast<unsigned char*>(PatternScan::ResolveRip(*match, 3));
			if (format)
				Log::Write("[Menu] Text format struct at RDR2.exe+0x{:X}", reinterpret_cast<uintptr_t>(format) - reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
			else
				Log::Write("[Menu] Text format pattern not found: centered and right-aligned menu text use an estimate");
		}
		return format;
	}

	// Characters shown, for the no-struct fallback's width estimate: skips
	// markup tags and counts an entity (&#8592;) as one.
	size_t VisibleLength(const std::string& text)
	{
		size_t n = 0;
		for (size_t i = 0; i < text.size(); i++)
		{
			const char c = text[i];
			if (c == '<')
				i = (std::min)(text.find('>', i), text.size());
			else if (c == '&')
			{
				i = (std::min)(text.find(';', i), text.size());
				n++;
			}
			else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
				n++;
		}
		return n;
	}

	// A box centered at x, y, as Rampage draws its menu boxes.
	void DrawBox(float x, float y, float width, float height, ColorRgba color)
	{
		color = Shown(color);
		GRAPHICS::DRAW_RECT(x, y, width, height, color.r, color.g, color.b, color.a, FALSE, TRUE);
	}
}

void DrawTextAt(float x, float y, const char *str, int fontSize, ColorRgba color, const char* face, bool center)
{
	if (!face)
		face = BodyFace();
	// A Center-aligned field's x is a -1..1 offset from the screen center
	// (see MenuController::DrawStatusText).
	std::string formatText = std::string("<TEXTFORMAT RIGHTMARGIN='0'><P ALIGN='") + (center ? "Center" : "Left") + "'><FONT FACE='"
		+ face + "' LETTERSPACING='0' SIZE='" + std::to_string(fontSize) + "'>~s~" + str + "</FONT></P><TEXTFORMAT>";
	color = Shown(color);
	UIDEBUG::_BG_SET_TEXT_COLOR(color.r, color.g, color.b, color.a);
	UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(10, "LITERAL_STRING", formatText.c_str()), center ? -1.0f + x * 2.0f : x, y);
}

void DrawMenuText(const std::string& text, float x, float y, float scale, ColorRgba color, const char* face, TextAlign align, bool title)
{
	if (!face)
		face = BodyFace();
	unsigned char* format = TextFormat();
	std::string markup;
	if (format || align == TextAlign::Left)
	{
		if (format && align != TextAlign::Left)
			format[kFormatAlignByte] = 0;
		markup = std::string("~s~<FONT FACE='") + face + "'>"
			+ (align == TextAlign::Right ? "<P ALIGN='RIGHT'>" + text + "</P>" : text) + "</FONT>";
	}
	else if (align == TextAlign::Center)
	{
		markup = std::string("<P ALIGN='Center'>~s~<FONT FACE='") + face + "'>" + text + "</FONT></P>";
		x = -1.0f + x * 2.0f;
	}
	else
	{
		// Right without the struct: back off from the right end by an
		// estimated width (~0.0065 per character at Rampage's 0.32).
		markup = std::string("~s~<FONT FACE='") + face + "'>" + text + "</FONT>";
		x = x + 0.5f - 0.0065f * (scale / kTextScale) * static_cast<float>(VisibleLength(text));
	}
	color = Shown(color);
	UIDEBUG::_BG_SET_TEXT_COLOR(color.r, color.g, color.b, color.a);
	UIDEBUG::_BG_SET_TEXT_SCALE(0.0f, scale);
	if (format && title)
		format[kFormatTitleByte] = 1;
	UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(10, "LITERAL_STRING", markup.c_str()), x, y);
}

void DrawMenuSprite(const char* dict, const char* name, float x, float y, float width, float height, float heading, ColorRgba color)
{
	if (!TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(dict))
	{
		TXD::REQUEST_STREAMED_TEXTURE_DICT(dict, FALSE);
		return;
	}
	color = Shown(color);
	GRAPHICS::DRAW_SPRITE(dict, name, x, y, width, height, heading, color.r, color.g, color.b, color.a, FALSE);
}

std::string_view MenuItemBase::GetDescription()
{
	if (!m_description.empty())
		return m_description;
	if (Rampagio::Command* command = GetCommand(); command && !command->GetDescription().empty())
		return command->GetDescription();
	MenuItemTitle* title = m_menu ? m_menu->GetTitle() : nullptr;
	return title ? Descriptions::Find(title->GetCaption(), GetCaption()) : std::string_view();
}

void MenuItemBase::WaitAndDraw(int ms)
{
	DWORD time = GetTickCount() + ms;
	bool waited = false;
	while (GetTickCount() < time || !waited)
	{
		WAIT(0);
		waited = true;
		if (auto menu = GetMenu())
			menu->OnDraw();
	}
}

void MenuItemBase::SetStatusText(string text, int ms)
{
	MenuController *controller;
	if (m_menu && (controller = m_menu->GetController()))
		controller->SetStatusText(text, ms);
}

// A row's label (Rampage's sub_1801ED140). lineLeft is the box's left edge
// and lineTop the row's top; MenuBase::OnDraw draws the row's background.
void MenuItemBase::OnDraw(float lineTop, float lineLeft, bool active)
{
	DrawMenuText(std::string(Tr(GetCaption())), lineLeft + m_textLeft, lineTop + kTextDrop, kTextScale, GetTextColor(active));
}

namespace
{
	// MenuItemParagraph layout. The wrap width is an ESTIMATE in "units" (a
	// Latin/Cyrillic character = 1, a CJK/Hangul/kana one = 2) since the
	// Scaleform text has no measuring call here -- the width is
	// WrapWidth() (settings.wrapwidth), tunable in-game.
	constexpr float kParagraphScale = 0.28f;
	constexpr float kParagraphLineStep = 0.024f;
	constexpr float kParagraphPadding = 0.009f;
	constexpr float kDescriptionScale = 0.25f; // the text under the menu (MenuBase::OnDraw)

	// Decodes the UTF-8 code point at s[i]; returns its byte length (>= 1,
	// so malformed input still makes progress).
	size_t DecodeUtf8(const std::string& s, size_t i, char32_t& cp)
	{
		const unsigned char c = static_cast<unsigned char>(s[i]);
		size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
		if (i + len > s.size())
			len = 1;
		cp = len == 1 ? c : c & (0xFF >> (len + 1));
		for (size_t k = 1; k < len; k++)
			cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
		return len;
	}

	bool IsWide(char32_t cp)
	{
		return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7AF)
			|| (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60)
			|| (cp >= 0xFFE0 && cp <= 0xFFE6);
	}

	// Closing punctuation never starts a line (it may overhang instead).
	bool IsClosingPunctuation(char32_t cp)
	{
		switch (cp)
		{
		case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF01: case 0xFF1F: case 0xFF1A: case 0xFF1B:
		case 0xFF09: case 0x300D: case 0x300F: case 0x3011: case 0x300B: case 0x201D: case 0x2019:
			return true;
		default:
			return false;
		}
	}

	// Greedy wrap: break at spaces, or after any wide (CJK) character.
	std::vector<std::string> WrapText(const std::string& text, int maxUnits)
	{
		if (maxUnits <= 0)
			maxUnits = INT_MAX; // 0 = never wrap (explicit newlines still break)

		std::vector<std::string> lines;
		size_t lineStart = 0, i = 0;
		int units = 0;
		size_t breakEnd = std::string::npos, breakNext = std::string::npos; // last break opportunity on this line

		while (i < text.size())
		{
			char32_t cp;
			const size_t n = DecodeUtf8(text, i, cp);

			if (cp == 0x0A) // newline
			{
				lines.push_back(text.substr(lineStart, i - lineStart));
				lineStart = i = i + n;
				units = 0;
				breakEnd = breakNext = std::string::npos;
				continue;
			}
			if (cp == U' ' && units == 0)
			{
				lineStart = i = i + n; // no leading spaces
				continue;
			}

			const bool wide = IsWide(cp);
			const int width = IsClosingPunctuation(cp) ? 0 : (wide ? 2 : 1);
			if (units > maxUnits - width && units > 0)
			{
				const size_t end = breakEnd != std::string::npos && breakEnd > lineStart ? breakEnd : i;
				const size_t next = breakEnd != std::string::npos && breakEnd > lineStart ? breakNext : i;
				lines.push_back(text.substr(lineStart, end - lineStart));
				lineStart = i = next; // rescan the overflow from the new line start
				units = 0;
				breakEnd = breakNext = std::string::npos;
				continue;
			}

			units += width;
			if (cp == U' ')
			{
				breakEnd = i;
				breakNext = i + n;
			}
			else if (wide)
			{
				breakEnd = breakNext = i + n;
			}
			i += n;
		}

		if (lineStart < text.size())
			lines.push_back(text.substr(lineStart));
		return lines;
	}
}

void MenuItemParagraph::Refresh()
{
	const std::string_view text = m_textFn ? m_textFn() : std::string_view();
	if (m_lines.empty() || text != m_lastText)
	{
		m_lastText.assign(text);
		m_lines = WrapText(m_lastText, WrapWidth());
		if (m_lines.empty())
			m_lines.emplace_back();
	}
}

float MenuItemParagraph::GetLineHeight()
{
	Refresh();
	return (std::max)(kRowHeight, static_cast<float>(m_lines.size()) * kParagraphLineStep + kParagraphPadding);
}

void MenuItemParagraph::OnDraw(float lineTop, float lineLeft, bool active)
{
	Refresh();
	const ColorRgba color = GetTextColor(active);
	for (size_t line = 0; line < m_lines.size(); line++)
		DrawMenuText(m_lines[line], lineLeft + kTextPad, lineTop + kParagraphPadding / 2.0f + static_cast<float>(line) * kParagraphLineStep,
			kParagraphScale, color);
}

// Rampage's checkbox: commonmenu's shop_box_blank, with generic_textures'
// tick inside while on, at the row's right end.
void MenuItemSwitchable::OnDraw(float lineTop, float lineLeft, bool active)
{
	MenuItemDefault::OnDraw(lineTop, lineLeft, active);
	const float x = lineLeft + 0.218f, y = lineTop + kRowHeight / 2.0f;
	DrawMenuSprite("commonmenu", "shop_box_blank", x, y, PxW(34), PxH(34), 0.0f, active ? Style().selectedText : kCheckbox);
	if (GetState())
		DrawMenuSprite("generic_textures", "tick", x, y, PxW(14), PxH(14), 0.0f, GetTextColor(active));
}

// Rampage's submenu row: menu_textures' selection_arrow_right at the right end.
void MenuItemMenu::OnDraw(float lineTop, float lineLeft, bool active)
{
	MenuItemDefault::OnDraw(lineTop, lineLeft, active);
	DrawMenuSprite("menu_textures", "selection_arrow_right", lineLeft + 0.221f, lineTop + 0.015f, PxW(9), PxH(9), 0.0f, GetTextColor(active));
}

// Rampage's value rows (sub_1802049E0 and friends): right-aligned at
// left - 0.275, with $Font5 arrows around the value while selected.
void DrawRowValue(MenuItemBase* item, float lineTop, float lineLeft, bool active, const std::string& value)
{
	const std::string text = active
		? "<FONT FACE='$Font5'>&#8592;</FONT> " + value + " <FONT FACE='$Font5'>&#8594;</FONT>"
		: value;
	DrawMenuText(text, lineLeft - 0.274f, lineTop + kTextDrop, kTextScale, item->GetTextColor(active), nullptr, TextAlign::Right);
}

void MenuItemChoice::OnLeft()
{
	if (m_options.empty())
		return;
	*m_index = (*m_index + static_cast<int>(m_options.size()) - 1) % static_cast<int>(m_options.size());
	if (m_onChange)
		m_onChange(*m_index);
}

void MenuItemChoice::OnRight()
{
	if (m_options.empty())
		return;
	*m_index = (*m_index + 1) % static_cast<int>(m_options.size());
	if (m_onChange)
		m_onChange(*m_index);
}

void MenuItemChoice::OnDraw(float lineTop, float lineLeft, bool active)
{
	MenuItemDefault::OnDraw(lineTop, lineLeft, active);
	if (*m_index >= 0 && *m_index < static_cast<int>(m_options.size()))
		DrawRowValue(this, lineTop, lineLeft, active, std::string(Tr(m_options[*m_index])));
}

void MenuItemSection::OnDraw(float lineTop, float lineLeft, bool active)
{
	const std::string caption = GetCaption();
	if (caption.empty())
		DrawMenuSprite("menu_textures", "divider_line", lineLeft + kWidth / 2.0f, lineTop + 0.015f, 0.2f, 0.0011f, 0.0f, kWhite);
	else
		DrawMenuText(std::string(Tr(caption)), lineLeft + kWidth / 2.0f, lineTop + kTextDrop, kTextScale, Style().sectionText, nullptr, TextAlign::Center);
}

void MenuItemMenu::OnSelect()
{
	if (auto parentMenu = GetMenu())
		if (auto controller = parentMenu->GetController())
		{
			m_menu->Open();
			controller->PushMenu(m_menu);
		}
}

int MenuBase::NextSelectable(int from, int step) const
{
	const int count = static_cast<int>(m_items.size());
	for (int k = 1; k <= count; k++)
	{
		const int i = ((from + step * k) % count + count) % count;
		if (m_items[i]->IsSelectable())
			return i;
	}
	return from;
}

void MenuBase::SkipToSelectable()
{
	const int count = static_cast<int>(m_items.size());
	if (m_activeIndex >= count)
		m_activeIndex = count ? count - 1 : 0;
	if (count && !m_items[m_activeIndex]->IsSelectable())
		m_activeIndex = NextSelectable(m_activeIndex, 1);
}

// Rampage's frame (sub_1801F6A60 and sub_1801F7640). x, y below are
// Style().left/top; every box is centered at x + 0.114.
void MenuBase::OnDraw()
{
	g_drawingMenu = true;
	struct Done { ~Done() { g_drawingMenu = false; } } done;
	const MenuStyle& style = Style();
	const float x = style.left, y = style.top;
	const float centerX = x + 0.114f, boxLeft = centerX - kWidth / 2.0f;
	GRAPHICS::SET_SCRIPT_GFX_DRAW_ORDER(0);

	// Header: the title over the header color.
	DrawBox(centerX, y + 0.046f, kWidth, 0.105f, style.header);
	const std::string title = style.title.empty() ? "Rampagio" : style.title;
	if (style.centeredTitle)
		DrawMenuText(title, centerX, y + 0.0135f, 0.95f, style.titleText, TitleFace(), TextAlign::Center, true);
	else
		DrawMenuText(title, x + 0.004f, y + 0.0135f, 0.95f, style.titleText, TitleFace(), TextAlign::Left, true);

	// Subheader: the menu's name in capitals, and "position/total" over the
	// selectable rows.
	DrawBox(centerX, y + 0.1115f, kWidth, 0.035f, style.subheader);
	const std::string name = Localization::Upper(Tr(m_itemTitle->GetCaption()));
	DrawMenuText(name, x + 0.0041f, y + 0.099f, kTextScale, style.text);
	const int count = static_cast<int>(m_items.size());
	int position = 0, total = 0;
	for (int i = 0; i < count; i++)
		if (m_items[i]->IsSelectable())
		{
			total++;
			if (i <= m_activeIndex)
				position = total;
		}
	DrawMenuText(std::format("{}/{}", position, total), x - 0.275f, y + 0.099f, kTextScale, style.text, nullptr, TextAlign::Right);

	// Rows: a window of linesPerScreen ending at the selection, scrolling
	// one row at a time, over the base color.
	const int lines = style.linesPerScreen > 0 ? style.linesPerScreen : 1;
	const int first = (std::max)(0, m_activeIndex - lines + 1);
	const int last = (std::min)(count, first + lines);
	const float rowsTop = y + 0.128f;
	float rowsHeight = 0.0f, activeTop = 0.0f, activeHeight = kRowHeight;
	for (int i = first; i < last; i++)
	{
		if (i == m_activeIndex)
			activeTop = rowsHeight, activeHeight = m_items[i]->GetLineHeight();
		rowsHeight += m_items[i]->GetLineHeight();
	}
	if (rowsHeight > 0.0f)
		DrawBox(centerX, rowsTop + rowsHeight / 2.0f, kWidth, rowsHeight, style.base);

	// Scroller: glides a 1/scrollSmoothness of the way each frame.
	static float s_scrollerTop = -1.0f; // from rowsTop
	if (style.smoothScroll && s_scrollerTop >= 0.0f && style.scrollSmoothness > 1)
	{
		const float step = (activeTop - s_scrollerTop) / static_cast<float>(style.scrollSmoothness);
		s_scrollerTop = std::fabs(s_scrollerTop - activeTop) > std::fabs(step) + 0.00005f ? s_scrollerTop + step : activeTop;
	}
	else
		s_scrollerTop = activeTop;
	if (count && m_items[m_activeIndex]->IsSelectable())
		DrawBox(centerX, rowsTop + s_scrollerTop + activeHeight / 2.0f, kWidth, activeHeight, style.scroller);

	m_drawnRows.clear();
	float lineTop = rowsTop;
	for (int i = first; i < last; i++)
	{
		MenuItemBase* item = m_items[i];
		const float height = item->GetLineHeight();
		item->OnDraw(lineTop, boxLeft, i == m_activeIndex);
		m_drawnRows.push_back({ i, boxLeft, lineTop, kWidth, height });
		lineTop += height;
	}

	// Footer, with Rampage's arrows (selection_arrow_left/right turned 270
	// degrees): one at the first or last row, both in between.
	const float footerY = rowsTop - 0.001f + rowsHeight + kRowHeight / 2.0f;
	DrawBox(centerX, footerY, kWidth, kRowHeight, style.footer);
	const int firstSelectable = count ? NextSelectable(count - 1, 1) : 0;
	const int lastSelectable = count ? NextSelectable(0, -1) : 0;
	const float arrowW = PxW(10), arrowH = PxH(10);
	if (m_activeIndex == firstSelectable)
		DrawMenuSprite("menu_textures", "selection_arrow_left", centerX, footerY - 0.0005f, arrowW, arrowH, 270.0f, kWhite);
	else if (m_activeIndex == lastSelectable)
		DrawMenuSprite("menu_textures", "selection_arrow_right", centerX, footerY - 0.0005f, arrowW, arrowH, 270.0f, kWhite);
	else
	{
		DrawMenuSprite("menu_textures", "selection_arrow_right", centerX, footerY - 0.0065f, arrowW, arrowH, 270.0f, kWhite);
		DrawMenuSprite("menu_textures", "selection_arrow_left", centerX, footerY + 0.0065f, arrowW, arrowH, 270.0f, kWhite);
	}

	// The selected row's description in a box under the footer, with a
	// main-color line along its top: 0.022 per line, text at 0.25 spaced
	// 0.02 (Rampage's sub_1801F6A60, after it sets draw order 4).
	if (count && m_items[m_activeIndex]->IsSelectable())
	{
		const std::string_view description = Tr(m_items[m_activeIndex]->GetDescription());
		if (!description.empty())
		{
			static std::string s_text;
			static std::vector<std::string> s_lines;
			static int s_units = 0;
			const int units = WrapWidth() > 0 ? WrapWidth() * 6 / 5 : 0; // the text is smaller than a paragraph's
			if (description != s_text || units != s_units)
			{
				s_text.assign(description);
				s_units = units;
				s_lines = WrapText(s_text, units);
			}
			const float lines = static_cast<float>(s_lines.size());
			const float boxTop = footerY + kRowHeight / 2.0f + 0.005f;
			GRAPHICS::SET_SCRIPT_GFX_DRAW_ORDER(4);
			DrawBox(centerX, boxTop + lines * 0.011f, kWidth, lines * 0.022f, style.base);
			DrawBox(centerX, boxTop, kWidth, 0.0025f, style.main);
			for (size_t i = 0; i < s_lines.size(); i++)
				DrawMenuText(s_lines[i], x + 0.004f, boxTop + 0.001f + static_cast<float>(i) * 0.02f, kDescriptionScale, style.text);
		}
	}
}

// Settings > Core > Mouse Controls: shows the cursor while the menu is
// open, keeps the camera and weapons still, and maps the cursor onto the
// rows drawn this frame: hover selects, left click runs the row, right
// click goes back, the wheel moves the selection (or changes a value row
// under the cursor with Shift held). Returns the input wait, 0 if unused.
int MenuBase::OnMouse()
{
	constexpr Hash INPUT_CURSOR_X = 0xD6C4ECDC, INPUT_CURSOR_Y = 0xE4130778;
	constexpr Hash INPUT_CURSOR_ACCEPT = 0x9D2AEA88, INPUT_CURSOR_CANCEL = 0x27568539;
	constexpr Hash INPUT_CURSOR_SCROLL_UP = 0x62800C92, INPUT_CURSOR_SCROLL_DOWN = 0x8BDE7443;
	constexpr Hash INPUT_LOOK_LR = 0xA987235F, INPUT_LOOK_UD = 0xD2047988, INPUT_ATTACK = 0x07CE1E61, INPUT_AIM = 0xF84FA74F;
	if (!Style().mouse || !PAD::IS_USING_KEYBOARD_AND_MOUSE(0))
		return 0;
	INTERACTION::SET_MOUSE_CURSOR_THIS_FRAME();
	for (Hash input : { INPUT_LOOK_LR, INPUT_LOOK_UD, INPUT_ATTACK, INPUT_AIM, INPUT_CURSOR_ACCEPT, INPUT_CURSOR_CANCEL,
		INPUT_CURSOR_SCROLL_UP, INPUT_CURSOR_SCROLL_DOWN })
		PAD::DISABLE_CONTROL_ACTION(0, input, TRUE);

	const float x = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_CURSOR_X);
	const float y = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_CURSOR_Y);
	const DrawnRow* hovered = nullptr;
	for (const DrawnRow& row : m_drawnRows)
		if (x >= row.left && x <= row.left + row.width && y >= row.top && y < row.top + row.height
			&& m_items[row.index]->IsSelectable())
			hovered = &row;
	// Hover only moves the selection when the cursor moves, so the
	// keyboard and gamepad keep working with the cursor parked on a row.
	const bool moved = x != m_lastCursorX || y != m_lastCursorY;
	m_lastCursorX = x;
	m_lastCursorY = y;
	if (hovered && moved)
		m_activeIndex = hovered->index;

	const int itemCount = static_cast<int>(m_items.size());
	if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_CURSOR_ACCEPT) && hovered)
	{
		MenuInput::MenuInputBeep();
		m_activeIndex = hovered->index;
		m_items[m_activeIndex]->OnSelect();
		return 150;
	}
	if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_CURSOR_CANCEL))
	{
		MenuInput::MenuInputBeep();
		if (auto controller = GetController())
			controller->PopMenu();
		return 200;
	}
	const bool up = PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_CURSOR_SCROLL_UP) != 0;
	const bool down = PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_CURSOR_SCROLL_DOWN) != 0;
	if ((up || down) && itemCount)
	{
		if (IsKeyDownLong(VK_SHIFT) && hovered)
		{
			m_activeIndex = hovered->index;
			if (up)
				m_items[m_activeIndex]->OnRight();
			else
				m_items[m_activeIndex]->OnLeft();
		}
		else
			m_activeIndex = NextSelectable(m_activeIndex, up ? -1 : 1);
		MenuInput::MenuInputBeep();
		return 50;
	}
	return 0;
}

int MenuBase::OnInput()
{
	const int itemCount = static_cast<int>(m_items.size());
	SkipToSelectable();
	if (const int mouseWait = OnMouse())
		return mouseWait;

	auto buttons = MenuInput::GetButtonState();

	int waitTime = 0;

	if (itemCount == 0 && !buttons.b)
		return 0;

	if (buttons.a || buttons.b || buttons.up || buttons.down || buttons.l || buttons.r)
	{
		MenuInput::MenuInputBeep();
		waitTime = buttons.b ? 200 : (buttons.l || buttons.r) ? 100 : 150;
	}

	if (buttons.a)
	{
		int activeItemIndex = GetActiveItemIndex();
		m_items[activeItemIndex]->OnSelect();
	} else
	if (buttons.l)
	{
		m_items[GetActiveItemIndex()]->OnLeft();
	} else
	if (buttons.r)
	{
		m_items[GetActiveItemIndex()]->OnRight();
	} else
	if (buttons.b)
	{
		if (auto controller = GetController())
			controller->PopMenu();
	} else
	if (buttons.up)
	{
		m_activeIndex = NextSelectable(m_activeIndex, -1);
	} else
	if (buttons.down)
	{
		m_activeIndex = NextSelectable(m_activeIndex, 1);
	}

	return waitTime;
}

void MenuController::DrawStatusText()
{
	if (GetTickCount() < m_statusTextMaxTicks)
	{
		// Center-aligned instead of DrawTextAt's fixed left align -- the
		// $Font5 pipeline has no SET_TEXT_CENTRE equivalent, alignment
		// comes from the <P ALIGN='Center'> tag itself, and (per
		// Githubs/RDR2-Native-Menu-Base's own DrawFormattedText(), the
		// confirmed-working reference for this pipeline's Center mode)
		// a Center-aligned field's x parameter is a -1..1 offset from
		// screen center rather than DrawTextAt's normal 0..1 left-edge
		// position -- 0.5 (screen-center in 0..1) maps to 0.0 here.
		std::string formatText = "<TEXTFORMAT RIGHTMARGIN='0'><P ALIGN='Center'><FONT FACE='$Font5' LETTERSPACING='0' SIZE='28'>~s~"
			+ std::string(Tr(m_statusText)) + "</FONT></P><TEXTFORMAT>";
		UIDEBUG::_BG_SET_TEXT_COLOR(255, 255, 255, 255);
		UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(10, "LITERAL_STRING", formatText.c_str()), -1.0f + (0.5f * 2.0f), 0.5f);
	}
}
