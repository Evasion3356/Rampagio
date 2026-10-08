/*
	A named part of Rampagio.json (HorseMenu's IStateSerializer). Each
	component owns one top-level key: SaveStateImpl writes its state into
	that key's object, LoadStateImpl reads it back. MarkStateDirty asks
	Settings to write the file on its next Tick.

	Constructing one registers it with Settings, so components are usually
	static objects or singletons.
*/

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace Rampagio
{
	class IStateSerializer
	{
		std::string m_SerComponentName;
		bool m_IsDirty;

	public:
		IStateSerializer(const std::string& name);
		virtual ~IStateSerializer() = default;

		virtual void SaveStateImpl(nlohmann::json& state) = 0;
		virtual void LoadStateImpl(nlohmann::json& state) = 0;

		void SaveState(nlohmann::json& state)
		{
			SaveStateImpl(state);
			m_IsDirty = false;
		}

		void LoadState(nlohmann::json& state)
		{
			LoadStateImpl(state);
			m_IsDirty = false;
		}

		bool IsStateDirty() const { return m_IsDirty; }
		void MarkStateDirty() { m_IsDirty = true; }
		const std::string& GetSerializerComponentName() const { return m_SerComponentName; }
	};
}
