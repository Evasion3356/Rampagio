#include "KeyNames.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>

namespace
{
	struct NamedKey
	{
		const char* name;
		DWORD vk;
	};

	// First row for a given VK is its canonical name (what Format() emits);
	// later rows for the same VK are accepted aliases only.
	constexpr NamedKey kNamedKeys[] = {
		{ "BACKSPACE", VK_BACK }, { "BACK", VK_BACK },
		{ "TAB", VK_TAB },
		{ "PAUSE", VK_PAUSE },
		{ "CAPSLOCK", VK_CAPITAL }, { "CAPITAL", VK_CAPITAL },
		{ "ESCAPE", VK_ESCAPE }, { "ESC", VK_ESCAPE },
		{ "SPACE", VK_SPACE },
		{ "PAGEUP", VK_PRIOR }, { "PGUP", VK_PRIOR }, { "PRIOR", VK_PRIOR },
		{ "PAGEDOWN", VK_NEXT }, { "PGDN", VK_NEXT }, { "NEXT", VK_NEXT },
		{ "END", VK_END },
		{ "HOME", VK_HOME },
		{ "PRINTSCREEN", VK_SNAPSHOT }, { "PRTSC", VK_SNAPSHOT }, { "SNAPSHOT", VK_SNAPSHOT },
		{ "INSERT", VK_INSERT }, { "INS", VK_INSERT },
		{ "DELETE", VK_DELETE }, { "DEL", VK_DELETE },
		{ "NUMLOCK", VK_NUMLOCK },
		{ "SCROLLLOCK", VK_SCROLL }, { "SCROLL", VK_SCROLL },
		{ "NUMPAD0", VK_NUMPAD0 }, { "NUMPAD1", VK_NUMPAD1 }, { "NUMPAD2", VK_NUMPAD2 },
		{ "NUMPAD3", VK_NUMPAD3 }, { "NUMPAD4", VK_NUMPAD4 }, { "NUMPAD5", VK_NUMPAD5 },
		{ "NUMPAD6", VK_NUMPAD6 }, { "NUMPAD7", VK_NUMPAD7 }, { "NUMPAD8", VK_NUMPAD8 },
		{ "NUMPAD9", VK_NUMPAD9 },
		{ "MULTIPLY", VK_MULTIPLY }, { "ADD", VK_ADD }, { "SUBTRACT", VK_SUBTRACT },
		{ "DECIMAL", VK_DECIMAL }, { "DIVIDE", VK_DIVIDE },
		// Arrows/Enter are named so a typo'd-but-valid name gets the
		// "reserved" log message rather than "unrecognized".
		{ "LEFT", VK_LEFT }, { "UP", VK_UP }, { "RIGHT", VK_RIGHT }, { "DOWN", VK_DOWN },
		{ "ENTER", VK_RETURN }, { "RETURN", VK_RETURN },
	};

	// Spelled-out punctuation names. Parse-only: Format() deliberately writes
	// the keycap character instead (see below), so these are kept out of
	// kNamedKeys. They map to the fixed VK_OEM_* codes (US-layout positions);
	// the literal character path via VkKeyScanW is the layout-aware one.
	constexpr NamedKey kPunctuationAliases[] = {
		{ "PERIOD", VK_OEM_PERIOD }, { "DOT", VK_OEM_PERIOD },
		{ "COMMA", VK_OEM_COMMA },
		{ "MINUS", VK_OEM_MINUS }, { "DASH", VK_OEM_MINUS }, { "HYPHEN", VK_OEM_MINUS },
		{ "EQUALS", VK_OEM_PLUS }, { "PLUS", VK_OEM_PLUS },
		{ "SEMICOLON", VK_OEM_1 },
		{ "SLASH", VK_OEM_2 }, { "FORWARDSLASH", VK_OEM_2 },
		{ "GRAVE", VK_OEM_3 }, { "TILDE", VK_OEM_3 }, { "BACKTICK", VK_OEM_3 },
		{ "LBRACKET", VK_OEM_4 }, { "LEFTBRACKET", VK_OEM_4 },
		{ "BACKSLASH", VK_OEM_5 },
		{ "RBRACKET", VK_OEM_6 }, { "RIGHTBRACKET", VK_OEM_6 },
		{ "QUOTE", VK_OEM_7 }, { "APOSTROPHE", VK_OEM_7 },
	};

	std::string Normalize(std::string_view text)
	{
		std::string out;
		for (char c : text)
		{
			if (c == ' ' || c == '\t' || c == '_')
				continue;
			out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
		}
		return out;
	}
}

namespace KeyNames
{
	bool IsReserved(DWORD vk)
	{
		switch (vk)
		{
		case VK_NUMPAD2:
		case VK_NUMPAD4:
		case VK_NUMPAD5:
		case VK_NUMPAD6:
		case VK_NUMPAD8:
		case VK_LEFT:
		case VK_UP:
		case VK_RIGHT:
		case VK_DOWN:
		case VK_RETURN:
			return true;
		default:
			return false;
		}
	}

	bool Parse(std::string_view text, DWORD& vk)
	{
		const std::string name = Normalize(text);
		if (name.empty())
			return false;

		DWORD result = 0;

		// Function keys: F1..F24.
		if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F' &&
			std::all_of(name.begin() + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; }))
		{
			const int n = std::stoi(name.substr(1));
			if (n >= 1 && n <= 24)
				result = VK_F1 + static_cast<DWORD>(n - 1);
		}
		else if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
		{
			// Main-row letters/digits: the VK code IS the ASCII code.
			result = static_cast<DWORD>(name[0]);
		}
		else
		{
			for (const NamedKey& key : kNamedKeys)
			{
				if (name == key.name)
				{
					result = key.vk;
					break;
				}
			}

			if (result == 0)
			{
				for (const NamedKey& key : kPunctuationAliases)
				{
					if (name == key.name)
					{
						result = key.vk;
						break;
					}
				}
			}

			// A single printable punctuation character, e.g. `;` or `[`.
			// Only accepted if the active layout produces it WITHOUT Shift,
			// i.e. it's a plain keycap the player can press by itself.
			if (result == 0 && name.size() == 1 && name[0] > ' ' && name[0] < 0x7F)
			{
				const SHORT scan = VkKeyScanW(static_cast<wchar_t>(name[0]));
				if (scan != -1 && (scan >> 8) == 0)
					result = static_cast<DWORD>(scan & 0xFF);
			}
		}

		if (result == 0 || IsReserved(result))
			return false;

		vk = result;
		return true;
	}

	std::string Format(DWORD vk)
	{
		if (vk >= VK_F1 && vk <= VK_F24)
			return "F" + std::to_string(vk - VK_F1 + 1);

		if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
			return std::string(1, static_cast<char>(vk));

		for (const NamedKey& key : kNamedKeys)
		{
			if (key.vk == vk)
				return key.name;
		}

		// Punctuation: ask the active layout what the key types.
		const UINT ch = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFFFFFF;
		if (ch > ' ' && ch < 0x7F)
			return std::string(1, static_cast<char>(ch));

		return "F9";
	}
}
