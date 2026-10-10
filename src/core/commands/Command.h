/*
	A named feature state or action (HorseMenu's Command). The name is a
	stable dotted id ("player.godmode") that keys the saved state and the
	hotkey; the label is the visible caption and can change freely.

	Rampagio's commands get their behaviour from std::function hooks given
	by the menu builders (see Menu.h's id overloads) instead of one
	subclass per feature; subclassing still works. Hooks run directly on
	the calling thread: every caller is the ScriptHook script thread, so
	there is no FiberPool.

	Constructing a command registers it with Commands.
*/

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>

namespace Rampagio
{
	class Command
	{
		std::string m_Name;
		std::string m_Label;
		std::string m_Description;
		std::uint32_t m_Hash;
		bool m_Hotkeyable = true;
		bool m_Registered = false;
		bool m_Transient = false;

	protected:
		virtual void OnCall() = 0;
		// Marks the state for saving.
		void MarkDirty();

	public:
		Command(std::string name, std::string label, std::string description = {});
		virtual ~Command() = default;

		// What a hotkey or a select does: flips a toggle, runs an action.
		void Call();

		// Whether this command has a value to save (actions don't).
		virtual bool HasState() const { return false; }
		virtual void SaveState(nlohmann::json& value) {}
		// Reads a saved value. It isn't applied until ApplyLoaded.
		virtual void LoadState(const nlohmann::json& value) {}
		// Applies what LoadState read, through the hooks: a toggle saved as
		// on comes back on.
		virtual void ApplyLoaded() {}
		// Back to the value it was built with, through its hooks, and saved.
		virtual void ResetToDefault() {}
		// Online kill switch: undo the feature without changing or saving
		// the state, and stop ticking. Resume re-applies it.
		virtual void Suspend() {}
		virtual void Resume() {}

		// Shown when a hotkey fires ("Godmode: on").
		virtual std::string StatusText() { return m_Label; }

		// False for commands that must not run from a hotkey with the menu
		// closed (anything that opens the on-screen keyboard, value rows
		// with nothing to apply).
		virtual bool Hotkeyable() const { return m_Hotkeyable; }
		Command* SetHotkeyable(bool hotkeyable)
		{
			m_Hotkeyable = hotkeyable;
			return this;
		}

		// Not saved: for states that are runs or mirrors of the game (a
		// drive task, the backup inventory flag), not settings.
		Command* SetTransient()
		{
			m_Transient = true;
			return this;
		}
		// Whether Rampagio.json holds this command's state.
		bool IsSaved() const { return HasState() && !m_Transient && m_Registered; }

		// False when another command already had this name; this one still
		// works for its row but isn't saved or bindable.
		bool IsRegistered() const { return m_Registered; }

		const std::string& GetName() const { return m_Name; }
		const std::string& GetLabel() const { return m_Label; }
		// For commands the user names (hotkey presets); row captions stay
		// fixed.
		void SetLabel(std::string label) { m_Label = std::move(label); }
		const std::string& GetDescription() const { return m_Description; }
		std::uint32_t GetHash() const { return m_Hash; }
	};
}
