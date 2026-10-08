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
		bool m_KeepSaved = false;
		bool m_AlwaysRestore = false;

	protected:
		virtual void OnCall() = 0;
		// Marks the state for saving. A kept saved value (KeepSavedValue) is
		// dropped: the user changed the state, so the file follows it again.
		void MarkDirty();
		// ApplyLoaded didn't apply the saved value (settings.restoretoggles
		// off): leave it in the file instead of overwriting it with the
		// default, until the state is changed.
		void KeepSavedValue() { m_KeepSaved = true; }
		void DropSavedValue() { m_KeepSaved = false; }

	public:
		Command(std::string name, std::string label, std::string description = {});
		virtual ~Command() = default;

		// What a hotkey or a select does: flips a toggle, runs an action.
		void Call();

		// Whether this command has a value to save (actions don't).
		virtual bool HasState() const { return false; }
		virtual void SaveState(nlohmann::json& value) {}
		// Reads a saved value. It isn't applied until ApplyLoaded, so
		// Commands can decide which saved states to restore.
		virtual void LoadState(const nlohmann::json& value) {}
		// Applies what LoadState read. restoreFeatures is
		// settings.restoretoggles (always true for "settings." commands):
		// when false, toggles and values with a change hook keep their
		// defaults, and the file keeps the saved value so turning
		// restoretoggles on later still brings it back; plain values
		// (parameters for an action) still load.
		virtual void ApplyLoaded(bool restoreFeatures) {}
		// Back to the value it was built with, through its hooks, and saved
		// (a kept saved value is dropped).
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
		// Whether saving leaves the file's value alone (see KeepSavedValue).
		bool KeepsSavedValue() const { return m_KeepSaved; }

		// Restores the saved state on start even when
		// settings.restoretoggles is off, as "settings." commands do: for
		// toggles that are options rather than features (the sibling mods'
		// rows, which their standalone ASIs keep in an INI).
		Command* SetAlwaysRestore()
		{
			m_AlwaysRestore = true;
			return this;
		}
		bool AlwaysRestore() const { return m_AlwaysRestore || m_Name.rfind("settings.", 0) == 0; }

		// False when another command already had this name; this one still
		// works for its row but isn't saved or bindable.
		bool IsRegistered() const { return m_Registered; }

		const std::string& GetName() const { return m_Name; }
		const std::string& GetLabel() const { return m_Label; }
		const std::string& GetDescription() const { return m_Description; }
		std::uint32_t GetHash() const { return m_Hash; }
	};
}
