/*
	Reads the inputs hotkeys bind (docs/HOTKEYS_PLAN.md), as the InputIds
	of src/core/commands/HotkeySystem.h:
	- keyboard keys and mouse buttons: GetAsyncKeyState, only while the
	  game window is in front;
	- the wheel: the game's cursor scroll controls (one frame per notch);
	- pad buttons: the frontend controls (group 2), only while the game
	  says the pad is in use (any pad the game supports).
	Only what's asked for is read: HotkeySystem::WatchedInputs() per frame,
	everything while binding.
*/

#pragma once

#include "core\commands\HotkeySystem.h"

#include <span>
#include <vector>

namespace HotkeyInput
{
	using Rampagio::InputId;

	// Pad buttons by index (kPadBase + index): names for the menu.
	extern const char* const kPadNames[];
	extern const int kPadCount;
	InputId Pad(int button);
	// Pad buttons Settings > Hotkeys > Gamepad Layer offers, by name.
	InputId PadByName(const char* name);

	// The game window is the foreground window.
	bool GameInFront();
	// The game reads the pad now, not keyboard and mouse.
	bool PadInUse();

	bool IsDown(InputId id);
	// The held ones of `ids` into `out` (cleared first).
	void Poll(std::span<const InputId> ids, std::vector<InputId>& out);
	// Every held input, for binding: keys (generic Shift/Ctrl/Alt, not the
	// left/right copies), mouse buttons, wheel, pad buttons.
	void PollAll(std::vector<InputId>& out);

	// For the pad layer button: the player's game controls are off this
	// frame, except moving and looking, so pad buttons only run hotkeys.
	void SuppressGameControls();
}
