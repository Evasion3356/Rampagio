/*
	Lightweight INI-backed config, same inipp-based load/rewrite pattern as
	ChallengeCheat's own Config (this file started as a copy of it). The INI
	is a user-facing setting in Release too, since the F5 menu is the mod's
	whole interface.

	Rampagio.ini lives next to the .asi, or in
	%LOCALAPPDATA%\RDR2ASIMods\ when the game folder isn't writable (see
	LogFallback::ResolveSettings). Missing/invalid values fall
	back to defaults and the file is rewritten with the resolved values, so
	it always reflects what the mod is actually using.

	Loaded lazily on the first Get() (from the script thread), NOT from
	DllMain -- MenuKey parsing calls into user32 (VkKeyScanW), which has no
	business running under the loader lock.
*/

#pragma once

#include <windows.h>
#include <string>

namespace Config
{
	struct Values
	{
		// Virtual-key code that opens/closes the menu. INI key: [General]
		// MenuKey, a keycap-style name (see KeyNames.h).
		// Default F5, the key Rampage uses, so Rampagio can stand in for
		// it. Don't load both with the defaults.
		DWORD MenuKey = VK_F5;

		// How many "units" of text fit on one line of the wrapped rank
		// objective (a Latin/Cyrillic character = 1, a CJK/Hangul/kana one =
		// 2). An estimate -- there is no text-measuring call -- so raise it if
		// lines wrap too early, lower it if they overflow the box. 0 = never
		// wrap (one line per objective; the longest run off the box). INI key:
		// [General] WrapWidth (0, or 10-120).
		int WrapWidth = 50;
	};

	void Reload();
	const Values& Get();
}
