/*
	Keycap-style key names <-> Windows virtual-key codes, for the INI's
	MenuKey setting. Deliberately does NOT just strip "VK_" off the Windows
	constant names: several of those don't match what's printed on the key
	(VK_PRIOR/VK_NEXT for PageUp/PageDown, VK_BACK, VK_CAPITAL, VK_SNAPSHOT,
	...), so the table below uses keycap names first, with the odd Windows
	names kept only as extra aliases.

	Accepted (case-insensitive, spaces and underscores ignored):
	  - F1..F24
	  - A..Z, 0..9 (the main-row keys, not the numpad)
	  - NUMPAD0..NUMPAD9, ADD/SUBTRACT/MULTIPLY/DIVIDE/DECIMAL
	  - named keys (PAGEUP/PGUP, PAGEDOWN/PGDN, INSERT/INS, HOME, END, ...)
	  - a single unshifted punctuation character, resolved against the
	    user's ACTIVE keyboard layout via VkKeyScanW, so `;` means whatever
	    key produces `;` on their layout
*/

#pragma once

#include <windows.h>
#include <string>
#include <string_view>

namespace KeyNames
{
	// Returns false (leaving `vk` untouched) if `text` isn't a recognized key
	// name, or names a key this mod's own menu navigation already uses
	// (see IsReserved).
	bool Parse(std::string_view text, DWORD& vk);

	// Canonical keycap-style name for a VK code that Parse() accepts; the
	// result always round-trips back through Parse().
	std::string Format(DWORD vk);

	// Keys the menu itself already consumes for navigation (numpad 2/4/5/6/8,
	// the arrows, Enter). Bound as the toggle they'd double as
	// select/navigate, so Parse() rejects them.
	bool IsReserved(DWORD vk);
}
