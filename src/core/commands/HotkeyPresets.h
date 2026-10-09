/*
	Hotkey presets (ours; docs/HOTKEYS_PLAN.md) and the "presets" part of
	Rampagio.json:
	{ "preset.<slug>": { "name": "Combat", "steps": [ { "command": "player.godmode", "action": "set", "value": 1 }, ... ] } }.

	A preset is a command ("preset.<slug>", the slug from its first name)
	whose call runs its steps in order, each a HotkeyAction on another
	command (HotkeySystem::Apply). It's bound like any row, so one key can
	do several things while a chain still runs one command. Steps naming a
	command that doesn't exist (yet) are kept and skipped.
*/

#pragma once

#include "Command.h"
#include "HotkeySystem.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Rampagio
{
	struct PresetStep
	{
		std::string command;
		HotkeyAction action;
	};

	// A preset's own command: calling it runs the steps.
	class PresetCommand : public Command
	{
		int m_LastRan = 0;

	protected:
		void OnCall() override;

	public:
		PresetCommand(std::string id, std::string name);
		~PresetCommand() override;
		std::string StatusText() override;
		int LastRan() const { return m_LastRan; }
	};

	class HotkeyPresets : private IStateSerializer
	{
	public:
		struct Preset
		{
			std::string name;
			std::vector<PresetStep> steps;
			std::unique_ptr<PresetCommand> command;
		};

	private:
		std::map<std::string, Preset, std::less<>> m_Presets; // by command id
		int m_Depth = 0; // presets running presets

		HotkeyPresets();
		void SaveStateImpl(nlohmann::json& state) override;
		void LoadStateImpl(nlohmann::json& state) override;
		Preset& Add(const std::string& id, std::string name);

	public:
		static HotkeyPresets& GetInstance();

		// Creates an empty preset; returns its command id.
		static std::string Create(std::string name);
		static void Delete(std::string_view id);
		static void Rename(std::string_view id, std::string name);
		static Preset* Get(std::string_view id);
		static const std::map<std::string, Preset, std::less<>>& All() { return GetInstance().m_Presets; }

		static void AddStep(std::string_view id, PresetStep step);
		static void RemoveStep(std::string_view id, size_t index);
		static void SetStep(std::string_view id, size_t index, PresetStep step);
		// Swaps step `index` with the one before it.
		static void MoveUp(std::string_view id, size_t index);

		// Runs the steps; returns how many ran.
		static int Run(std::string_view id);
	};
}
