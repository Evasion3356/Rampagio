#include "HotkeyInput.h"
#include "script.h"

#include <windows.h>
#include <algorithm>
#include <cstring>

namespace HotkeyInput
{
	namespace
	{
		using Rampagio::kPadBase;
		using Rampagio::kWheelDown;
		using Rampagio::kWheelUp;

		// Frontend (group 2) controls per pad button; joaat of the input
		// names. Which control is which button (LS/RS as the stick clicks,
		// Select as View/Share) is unverified: docs/HOTKEYS_PLAN.md's live
		// checklist.
		constexpr Hash kPadControls[] = {
			0xC7B5340A, // INPUT_FRONTEND_ACCEPT
			0x156F7119, // INPUT_FRONTEND_CANCEL
			0x6DB8C62F, // INPUT_FRONTEND_X
			0x7C0162C0, // INPUT_FRONTEND_Y
			0xE885EF16, // INPUT_FRONTEND_LB
			0x17BEC168, // INPUT_FRONTEND_RB
			0x51104035, // INPUT_FRONTEND_LT
			0x6FED71BC, // INPUT_FRONTEND_RT
			0x43CDA5B0, // INPUT_FRONTEND_LS
			0x7DA48D2A, // INPUT_FRONTEND_RS
			0x6319DB71, // INPUT_FRONTEND_UP
			0x05CA7C52, // INPUT_FRONTEND_DOWN
			0xA65EBAB4, // INPUT_FRONTEND_LEFT
			0xDEB34313, // INPUT_FRONTEND_RIGHT
			0x171910DC, // INPUT_FRONTEND_SELECT
		};

		constexpr Hash INPUT_CURSOR_SCROLL_UP = 0x62800C92, INPUT_CURSOR_SCROLL_DOWN = 0x8BDE7443;

		// Still on while the layer button is held.
		constexpr Hash kKeptControls[] = {
			0x4D8FB4C1, // INPUT_MOVE_LR
			0xFDA83190, // INPUT_MOVE_UD
			0xA987235F, // INPUT_LOOK_LR
			0xD2047988, // INPUT_LOOK_UD
			0x126796EB, // INPUT_HORSE_MOVE_LR
			0x3BBDEFEF, // INPUT_HORSE_MOVE_UD
		};

		bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
	}

	const char* const kPadNames[] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Up", "Down", "Left", "Right", "Select" };
	const int kPadCount = static_cast<int>(std::size(kPadControls));
	static_assert(std::size(kPadControls) == 15, "kPadNames and kPadControls must match");

	InputId Pad(int button) { return kPadBase + static_cast<InputId>(button); }

	InputId PadByName(const char* name)
	{
		for (int i = 0; i < kPadCount; ++i)
			if (std::strcmp(kPadNames[i], name) == 0)
				return Pad(i);
		return 0;
	}

	bool GameInFront()
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		return pid == GetCurrentProcessId();
	}

	bool PadInUse()
	{
		return !PAD::IS_USING_KEYBOARD_AND_MOUSE(2);
	}

	bool IsDown(InputId id)
	{
		if (Rampagio::IsPadInput(id))
		{
			const InputId button = id - kPadBase;
			return button < static_cast<InputId>(kPadCount) && PAD::IS_DISABLED_CONTROL_PRESSED(2, kPadControls[button]) != 0;
		}
		if (id == kWheelUp)
			return PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_CURSOR_SCROLL_UP) != 0;
		if (id == kWheelDown)
			return PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_CURSOR_SCROLL_DOWN) != 0;
		return id > 0 && id < 0xFF && KeyDown(static_cast<int>(id));
	}

	void Poll(std::span<const InputId> ids, std::vector<InputId>& out)
	{
		out.clear();
		if (ids.empty())
			return;
		const bool front = GameInFront();
		bool pad = false, padChecked = false;
		for (InputId id : ids)
		{
			if (Rampagio::IsPadInput(id))
			{
				if (!padChecked)
				{
					pad = PadInUse();
					padChecked = true;
				}
				if (!pad)
					continue;
			}
			else if (!front)
				continue;
			if (IsDown(id))
				out.push_back(id);
		}
	}

	void PollAll(std::vector<InputId>& out)
	{
		out.clear();
		if (GameInFront())
		{
			for (int vk = 0x01; vk < 0xFF; ++vk)
			{
				// The generic VK_SHIFT/VK_CONTROL/VK_MENU stand for both sides.
				if (vk >= VK_LSHIFT && vk <= VK_RMENU)
					continue;
				if (KeyDown(vk))
					out.push_back(static_cast<InputId>(vk));
			}
			if (IsDown(kWheelUp))
				out.push_back(kWheelUp);
			if (IsDown(kWheelDown))
				out.push_back(kWheelDown);
		}
		if (PadInUse())
			for (int i = 0; i < kPadCount; ++i)
				if (IsDown(Pad(i)))
					out.push_back(Pad(i));
	}

	void SuppressGameControls()
	{
		PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
		for (Hash control : kKeptControls)
			PAD::ENABLE_CONTROL_ACTION(0, control, FALSE);
	}
}
