/*
	The command registry (HorseMenu's Commands), and the "commands" part of
	Rampagio.json: { "<name>": <state>, ... }.

	Rampagio additions: duplicate names are refused and logged (AddCommand
	returns false), lookup by name, ForEach in registration order (the
	Hotkey Manager and Search list them), ApplyLoaded (HorseMenu's
	EnableBoolCommands), RemoveCommand (for
	the hotkey presets) and Suspend/Resume
	for the online kill switch, which undo every feature without touching
	the saved states.
*/

#pragma once

#include "..\settings\IStateSerializer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Rampagio
{
	class Command;
	class LoopedCommand;

	// Lets m_ByName be searched by string_view without building a string.
	struct StringHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
	};

	class Commands : private IStateSerializer
	{
		std::unordered_map<std::uint32_t, Command*> m_ByHash;
		std::unordered_map<std::string, Command*, StringHash, std::equal_to<>> m_ByName;
		std::vector<Command*> m_Ordered;
		std::vector<LoopedCommand*> m_LoopedCommands;
		bool m_Suspended = false;

		Commands();

		void SaveStateImpl(nlohmann::json& state) override;
		void LoadStateImpl(nlohmann::json& state) override;

	public:
		static Commands& GetInstance();

		// False (and logged) if the name is taken or isn't a valid id.
		static bool AddCommand(Command* command);
		static void AddLoopedCommand(LoopedCommand* command);
		// For commands made and deleted at runtime (hotkey presets): a
		// command's own pointers elsewhere must be gone first.
		static void RemoveCommand(Command* command);

		template <typename T = Command>
		static T* GetCommand(std::uint32_t hash)
		{
			auto& map = GetInstance().m_ByHash;
			auto it = map.find(hash);
			return it == map.end() ? nullptr : dynamic_cast<T*>(it->second);
		}

		template <typename T = Command>
		static T* GetCommand(std::string_view name)
		{
			auto& map = GetInstance().m_ByName;
			auto it = map.find(name);
			return it == map.end() ? nullptr : dynamic_cast<T*>(it->second);
		}

		static void ForEach(const std::function<void(Command*)>& fn);

		// Ticks every looped command that's on (not while suspended).
		static void RunLoopedCommands();
		// Applies the states LoadState read; see Command::ApplyLoaded.
		static void ApplyLoaded();
		static void ResetToDefaults();

		static void Suspend();
		static void Resume();
		static bool IsSuspended() { return GetInstance().m_Suspended; }

		static void MarkDirty() { GetInstance().MarkStateDirty(); }
		static bool IsDirty() { return GetInstance().IsStateDirty(); }

		// "lowercase.dotted_id": a-z, 0-9, '.', '_' and '-', not empty.
		static bool IsValidName(std::string_view name);
	};
}
