/*
	Hotkeys (HorseMenu's HotkeySystem, grown; docs/HOTKEYS_PLAN.md) and the
	"hotkeys" part of Rampagio.json:
	{ "<command name>": [ { "keys": [id, ...], "gesture": "long", "action": "set", "value": 1 }, ... ] }.
	An old plain chain ([vk, ...]) still loads as one press binding.

	A binding is a chain of inputs held together (keyboard, mouse or pad,
	see InputId), a gesture (press, or long press) and what it does to the
	command (HotkeyAction). A command can have several bindings; a chain
	and gesture belong to one command at most. A chain held as part of a
	longer one completed on the same frame doesn't fire (Shift+G doesn't
	also fire G).

	Input polling is the host's: it polls WatchedInputs() and passes the
	held ones to Update, so this file has no game or keyboard dependency
	(tests\SettingsTests.cpp drives it). The capture flow and the Hotkey
	Manager are in src/menus/Settings.cpp, presets in HotkeyPresets.h.
*/

#pragma once

#include "..\settings\IStateSerializer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Rampagio
{
	class Command;

	// 0x01..0xFE: Windows virtual keys (mouse buttons included);
	// kPadBase + n: pad button n; kWheelUp/kWheelDown: one wheel notch.
	using InputId = std::uint32_t;
	constexpr InputId kPadBase = 0x10000;
	constexpr InputId kWheelUp = 0x20000;
	constexpr InputId kWheelDown = 0x20001;
	inline bool IsPadInput(InputId id) { return id >= kPadBase && id < kWheelUp; }
	inline bool IsWheelInput(InputId id) { return id == kWheelUp || id == kWheelDown; }

	enum class HotkeyMode : std::uint8_t
	{
		Press,    // Command::Call: flip a toggle, run an action
		Hold,     // a toggle is on while held, then back to its old state
		Next,     // a number or list one step up (repeats while held)
		Previous,
		Set,      // a toggle, number or list index to the value
		Count
	};

	enum class HotkeyGesture : std::uint8_t
	{
		Press, // fires when the chain completes (or on release, as a tap, when the chain also has a long press)
		Long,  // fires once the chain is held for the long-press time
		Count
	};

	struct HotkeyAction
	{
		HotkeyMode mode = HotkeyMode::Press;
		double value = 0;
	};

	struct Hotkey
	{
		std::vector<InputId> keys; // in the order pressed when bound (shown that way)
		HotkeyGesture gesture = HotkeyGesture::Press;
		HotkeyAction action;
	};

	class HotkeySystem : private IStateSerializer
	{
		// A Hold binding that's on: its command goes back to `previous` when
		// the chain ends.
		struct HoldState
		{
			std::string name;
			bool previous;
		};

		// A chain being held: its bindings fire or end from here.
		struct Active
		{
			std::vector<InputId> keys; // sorted
			std::uint32_t since;
			std::uint32_t nextRepeat;
			bool tapPending; // a press binding waiting to see if this is a tap
			bool longFired;
			std::optional<HotkeyGesture> repeating; // the Next/Previous binding that repeats while held
			std::vector<HoldState> holds;
		};

		std::map<std::string, std::vector<Hotkey>, std::less<>> m_Bindings;
		// Rebuilt when m_Bindings changes: input -> sorted chains using it.
		std::unordered_map<InputId, std::vector<std::vector<InputId>>> m_Index;
		std::vector<InputId> m_Watched;
		bool m_IndexDirty = true;

		std::vector<InputId> m_Down; // last frame's, sorted
		std::vector<Active> m_Active;
		bool m_Resync = true; // the next Update only learns what's held
		std::uint32_t m_LongPressMs = 500;
		Command* m_LastFired = nullptr;

		HotkeySystem();

		void SaveStateImpl(nlohmann::json& state) override;
		void LoadStateImpl(nlohmann::json& state) override;

		void RebuildIndex();
		// The binding for (sorted chain, gesture), if any.
		std::pair<const std::string*, const Hotkey*> Find(const std::vector<InputId>& sorted, HotkeyGesture gesture) const;
		void Fire(std::string name, Hotkey hotkey, Active& active);
		void EndChain(Active& active);

	public:
		static HotkeySystem& GetInstance();

		// Adds a binding to `name`. A binding of another command with the
		// same chain (as a set) and gesture is removed; the same one on
		// `name` is replaced.
		static void Bind(const std::string& name, Hotkey hotkey);
		// Old form: a press binding for `chain`.
		static void Bind(const std::string& name, std::vector<InputId> chain);
		// The command another binding with this chain and gesture belongs
		// to, other than `except`.
		static std::optional<std::string> FindConflict(const std::vector<InputId>& chain, HotkeyGesture gesture, std::string_view except = {});
		static void Remove(std::string_view name, size_t index);
		static void Clear(std::string_view name);
		// Replaces binding `index` of `name` (gesture or action changed).
		// False if that would clash with another binding.
		static bool Replace(std::string_view name, size_t index, const Hotkey& hotkey);
		static const std::map<std::string, std::vector<Hotkey>, std::less<>>& GetBindings() { return GetInstance().m_Bindings; }

		static void SetLongPressMs(std::uint32_t ms) { GetInstance().m_LongPressMs = ms; }

		// Every input some binding uses, sorted: what the host should poll.
		static std::span<const InputId> WatchedInputs();
		// Whether any binding uses `id` together with a pad button.
		static bool PadChainUses(InputId id);

		// Runs the bindings for this frame. `down` holds the watched inputs
		// held now (any order). Returns the last command fired, or nullptr,
		// so the caller can show its status.
		static Command* Update(std::span<const InputId> down, std::uint32_t nowMs);
		// Ends every held chain (hold bindings switch back off) and treats
		// whatever is held at the next Update as old, not pressed: for while
		// hotkeys don't run (menu open, window in the background).
		static void Release();

		// What `mode` does to `command` (down = the chain went down; Hold
		// also gets the release). Presets use it too. False if the command
		// can't do it.
		static bool Apply(Command* command, const HotkeyAction& action, bool down = true);
		// The modes `command` supports, for the binding menus.
		static bool Supports(Command* command, HotkeyMode mode);
		// Whether `command` can be bound at all.
		static bool Bindable(Command* command);

		// "Shift + G", "Mouse 4", "Pad RB": a name per input. Pad names come
		// from SetPadNames (the host knows the buttons).
		static std::string KeyLabel(InputId id);
		static std::string ChainLabel(const std::vector<InputId>& chain);
		static void SetPadNames(std::span<const char* const> names);

	};
}
