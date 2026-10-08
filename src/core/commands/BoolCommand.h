/*
	An on/off feature (HorseMenu's BoolCommand). onChange(true) runs when it
	turns on, onChange(false) when it turns off; subclasses can override
	OnEnable/OnDisable instead.
*/

#pragma once

#include "Command.h"

#include <functional>
#include <optional>

namespace Rampagio
{
	class BoolCommand : public Command
	{
	protected:
		bool m_State;
		bool m_Default;
		bool m_Suspended = false;
		std::optional<bool> m_Saved;
		std::function<void(bool)> m_OnChange;

		virtual void OnEnable()
		{
			if (m_OnChange)
				m_OnChange(true);
		}
		virtual void OnDisable()
		{
			if (m_OnChange)
				m_OnChange(false);
		}
		void OnCall() override { SetState(!m_State); }
		void Apply(bool on) { on ? OnEnable() : OnDisable(); }

	public:
		BoolCommand(std::string name, std::string label, std::string description = {},
			std::function<void(bool)> onChange = nullptr, bool defaultValue = false);

		bool GetState() const { return m_State; }
		// Runs the hooks (unless suspended) and marks the state for saving.
		void SetState(bool state);
		// Shows a state the game is already in, without running the hooks.
		void Sync(bool state);
		// At build time: the state this starts in, without running the
		// hooks (the feature's own globals already match it).
		BoolCommand* SetDefault(bool state);

		bool HasState() const override { return true; }
		void SaveState(nlohmann::json& value) override;
		void LoadState(const nlohmann::json& value) override;
		void ApplyLoaded(bool restoreFeatures) override;
		void ResetToDefault() override { SetState(m_Default); }
		void Suspend() override;
		void Resume() override;
		bool IsSuspended() const { return m_Suspended; }
		std::string StatusText() override { return GetLabel() + (m_State ? ": on" : ": off"); }
	};
}
