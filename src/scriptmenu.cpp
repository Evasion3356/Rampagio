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
	    _BG_SET_TEXT_COLOR, fed a `$Font5` ("Redemption", RDR2's own
	    western-stencil font) rich-text tag through Scaleform instead of
	    the legacy single-font text path. This is the menu's actual F9
	    surface, not a Debug-only diagnostic, so unlike those siblings'
	    own scriptmenu.cpp copies (still on the dead legacy path, since
	    their F-key menus are pure reversing tools that never needed a
	    real font), this one had to move.
	  - Styling (2026-09-19): recolored to match RDR2's own menu chrome --
	    solid black behind titles, a plain grey bar behind every item
	    whether selected or not, and the active item picked out with a
	    thin red border (DrawRectBorder(), below) instead of a color
	    swap -- the same "grey rows + one red-bordered row" look
	    Githubs/RDR2-Native-Menu-Base's DrawSelectionBox() uses (that
	    project achieves it with 4 stretched border sprites off
	    "menu_textures"; this just draws 4 solid GRAPHICS::DRAW_RECT
	    strips, since DrawRect() was already here and needed no new
	    texture-dict dependency).
*/

#include "scriptmenu.h"
#include <climits>

MenuStyle& Style()
{
	static MenuStyle style;
	return style;
}

// Wraps `str` in the Scaleform rich-text tags UIDEBUG::_BG_DISPLAY_TEXT
// needs to actually render it -- see this file's own header comment.
// Ported from DominoCheat/BlackjackCheat/PokerCheat's identical BgText()
// helper. Always left-aligned (RIGHTMARGIN/ALIGN fixed) -- every menu
// item in this file is; the one exception (the centered status-text
// popup) builds its own tag directly in MenuController::DrawStatusText().
void DrawTextAt(float x, float y, const char *str, int fontSize, ColorRgba color)
{
	std::string formatText = "<TEXTFORMAT RIGHTMARGIN='0'><P ALIGN='Left'><FONT FACE='$Font5' LETTERSPACING='0' SIZE='"
		+ std::to_string(fontSize) + "'>~s~" + str + "</FONT></P><TEXTFORMAT>";
	UIDEBUG::_BG_SET_TEXT_COLOR(color.r, color.g, color.b, color.a);
	UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(10, "LITERAL_STRING", formatText.c_str()), x, y);
}

void DrawRect(float lineLeft, float lineTop, float lineWidth, float lineHeight, int r, int g, int b, int a)
{
	GRAPHICS::DRAW_RECT((lineLeft + (lineWidth * 0.5f)), (lineTop + (lineHeight * 0.5f)), lineWidth, lineHeight, r, g, b, a, 0, 0);
}

// Thin rectangular outline (top/bottom/left/right strips, each `thickness`
// wide) traced around the given box -- used to pick out the active menu
// item with a red border instead of a fill/text color change, RDR2's own
// menu style (see this file's header comment).
void DrawRectBorder(float lineLeft, float lineTop, float lineWidth, float lineHeight, float thickness, int r, int g, int b, int a)
{
	DrawRect(lineLeft, lineTop, lineWidth, thickness, r, g, b, a);
	DrawRect(lineLeft, lineTop + lineHeight - thickness, lineWidth, thickness, r, g, b, a);
	DrawRect(lineLeft, lineTop, thickness, lineHeight, r, g, b, a);
	DrawRect(lineLeft + lineWidth - thickness, lineTop, thickness, lineHeight, r, g, b, a);
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

// Font size (the Scaleform $Font5 pipeline's point-size SIZE tag) scales
// off the item's own line height, same idea the old SET_TEXT_SCALE(0.0,
// m_lineHeight * 8.0f) call captured for the dead legacy pipeline -- a
// title row (taller line) gets visibly bigger text than a regular item
// row, with no separate per-class font-size constant needed.
constexpr float kMenuFontSizeScale = 500.0f;

void MenuItemBase::OnDraw(float lineTop, float lineLeft, bool active)
{
	// rect: plain grey behind every item, selected or not
	ColorRgba rectColor = active ? GetColorRectActive() : GetColorRect();
	DrawRect(lineLeft, lineTop, m_lineWidth, m_lineHeight, rectColor.r, rectColor.g, rectColor.b, rectColor.a);
	// red border: only around the active item
	const ColorRgba border = Style().border;
	if (active)
		DrawRectBorder(lineLeft, lineTop, m_lineWidth, m_lineHeight, MenuBase_activeBorderThickness, border.r, border.g, border.b, border.a);
	// text
	ColorRgba textColor = active ? GetColorTextActive() : GetColorText();
	int fontSize = static_cast<int>(m_lineHeight * kMenuFontSizeScale);
	DrawTextAt(lineLeft + m_textLeft, lineTop + m_lineHeight / 4.5f, GetCaption().c_str(), fontSize, textColor);
}

namespace
{
	// MenuItemParagraph layout. The wrap width is an ESTIMATE in "units" (a
	// Latin/Cyrillic character = 1, a CJK/Hangul/kana one = 2) since the
	// Scaleform text has no measuring call here -- the width is
	// Rampagio.ini [General] WrapWidth (Config.h), tunable in-game.
	constexpr int kParagraphFontSize = 21;
	constexpr float kParagraphLineStep = 0.034f;
	constexpr float kParagraphPadding = 0.016f;

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
		m_lines = WrapText(m_lastText, Config::Get().WrapWidth);
		if (m_lines.empty())
			m_lines.emplace_back();
	}
}

float MenuItemParagraph::GetLineHeight()
{
	Refresh();
	return static_cast<float>(m_lines.size()) * kParagraphLineStep + kParagraphPadding;
}

void MenuItemParagraph::OnDraw(float lineTop, float lineLeft, bool active)
{
	const float height = GetLineHeight();
	const float width = GetLineWidth();
	const ColorRgba rect = active ? GetColorRectActive() : GetColorRect();
	DrawRect(lineLeft, lineTop, width, height, rect.r, rect.g, rect.b, rect.a);
	if (active)
		DrawRectBorder(lineLeft, lineTop, width, height, MenuBase_activeBorderThickness,
			Style().border.r, Style().border.g, Style().border.b, Style().border.a);

	const ColorRgba color = active ? GetColorTextActive() : GetColorText();
	for (size_t line = 0; line < m_lines.size(); line++)
		DrawTextAt(lineLeft + MenuItemDefault_textLeft, lineTop + kParagraphPadding / 2.0f + static_cast<float>(line) * kParagraphLineStep,
			m_lines[line].c_str(), kParagraphFontSize, color);
}

void MenuItemSwitchable::OnDraw(float lineTop, float lineLeft, bool active)
{
	MenuItemDefault::OnDraw(lineTop, lineLeft, active);
	float lineWidth = GetLineWidth();
	float lineHeight = GetLineHeight();
	ColorRgba color = active ? GetColorTextActive() : GetColorText();
	color.a = static_cast<unsigned char>(color.a / 1.1f);
	int fontSize = static_cast<int>(lineHeight * kMenuFontSizeScale);
	DrawTextAt(lineLeft + lineWidth - lineWidth / 6.35f, lineTop + lineHeight / 4.8f, GetState() ? "[Y]" : "[N]", fontSize, color);
}

void MenuItemMenu::OnDraw(float lineTop, float lineLeft, bool active)
{
	MenuItemDefault::OnDraw(lineTop, lineLeft, active);
	float lineWidth = GetLineWidth();
	float lineHeight = GetLineHeight();
	ColorRgba color = active ? GetColorTextActive() : GetColorText();
	color.a = color.a / 2;
	int fontSize = static_cast<int>(lineHeight * kMenuFontSizeScale);
	DrawTextAt(lineLeft + lineWidth - lineWidth / 8, lineTop + lineHeight / 3.5f, "*", fontSize, color);
}

void DrawRowValue(MenuItemBase* item, float lineTop, float lineLeft, bool active, const std::string& text)
{
	const float lineWidth = item->GetLineWidth();
	const float lineHeight = item->GetLineHeight();
	const ColorRgba color = active ? item->GetColorTextActive() : item->GetColorText();
	const int fontSize = static_cast<int>(lineHeight * kMenuFontSizeScale * 0.8f);
	// No text measuring on the Scaleform path: back off ~0.0075 per char.
	const float x = lineLeft + lineWidth - 0.008f - 0.0075f * static_cast<float>(text.size());
	DrawTextAt(x, lineTop + lineHeight / 4.0f, text.c_str(), fontSize, color);
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
		DrawRowValue(this, lineTop, lineLeft, active, "< " + m_options[*m_index] + " >");
}

void MenuItemSection::OnDraw(float lineTop, float lineLeft, bool active)
{
	const float lineWidth = GetLineWidth();
	const float lineHeight = GetLineHeight();
	const MenuStyle& style = Style();
	DrawRect(lineLeft, lineTop, lineWidth, lineHeight, style.sectionRect.r, style.sectionRect.g, style.sectionRect.b, style.sectionRect.a);
	if (active)
		DrawRectBorder(lineLeft, lineTop, lineWidth, lineHeight, MenuBase_activeBorderThickness,
			style.border.r, style.border.g, style.border.b, style.border.a);
	DrawTextAt(lineLeft + MenuItemDefault_textLeft, lineTop + lineHeight / 4.5f, GetCaption().c_str(),
		static_cast<int>(lineHeight * kMenuFontSizeScale * 0.85f), style.sectionText);
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

void MenuBase::OnDraw()
{
	float lineTop = Style().top;
	float lineLeft = Style().left;
	const int lines = Style().linesPerScreen > 0 ? Style().linesPerScreen : 1;
	const int first = m_activeIndex / lines * lines;
	if (m_itemTitle->GetClass() == eMenuItemClass::ListTitle)
		reinterpret_cast<MenuItemListTitle *>(m_itemTitle)->
			SetCurrentItemInfo(GetActiveItemIndex() + 1, static_cast<int>(m_items.size()));
	m_itemTitle->OnDraw(lineTop, lineLeft, false);
	lineTop += m_itemTitle->GetLineHeight();
	for (int i = 0; i < lines; i++)
	{
		int itemIndex = first + i;
		if (itemIndex >= static_cast<int>(m_items.size()))
			break;
		MenuItemBase *item = m_items[itemIndex];
		item->OnDraw(lineTop, lineLeft, itemIndex == m_activeIndex);
		lineTop += item->GetLineHeight() - item->GetLineHeight() * MenuBase_lineOverlap;
	}
}

int MenuBase::OnInput()
{
	const int itemCount = static_cast<int>(m_items.size());
	if (m_activeIndex >= itemCount)
		m_activeIndex = itemCount ? itemCount - 1 : 0;

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
		m_activeIndex = (m_activeIndex + itemCount - 1) % itemCount;
	} else
	if (buttons.down)
	{
		m_activeIndex = (m_activeIndex + 1) % itemCount;
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
			+ m_statusText + "</FONT></P><TEXTFORMAT>";
		UIDEBUG::_BG_SET_TEXT_COLOR(255, 255, 255, 255);
		UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(10, "LITERAL_STRING", formatText.c_str()), -1.0f + (0.5f * 2.0f), 0.5f);
	}
}
