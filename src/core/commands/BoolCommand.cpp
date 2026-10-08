#include "BoolCommand.h"
#include "LoopedCommand.h"
#include "Commands.h"

#include <nlohmann/json.hpp>

namespace Rampagio
{
	BoolCommand::BoolCommand(std::string name, std::string label, std::string description,
		std::function<void(bool)> onChange, bool defaultValue) :
	    Command(std::move(name), std::move(label), std::move(description)),
	    m_State(defaultValue),
	    m_Default(defaultValue),
	    m_OnChange(std::move(onChange))
	{
	}

	void BoolCommand::SetState(bool state)
	{
		if (state == m_State)
			return;
		m_State = state;
		if (!m_Suspended)
			Apply(state);
		MarkDirty();
	}

	void BoolCommand::Sync(bool state)
	{
		if (state == m_State)
			return;
		m_State = state;
		MarkDirty();
	}

	BoolCommand* BoolCommand::SetDefault(bool state)
	{
		m_State = m_Default = state;
		return this;
	}

	void BoolCommand::SaveState(nlohmann::json& value)
	{
		value = m_State;
	}

	void BoolCommand::LoadState(const nlohmann::json& value)
	{
		m_Saved = value.get<bool>();
	}

	void BoolCommand::ApplyLoaded(bool restoreFeatures)
	{
		if (!m_Saved)
			return;
		const bool saved = *m_Saved;
		m_Saved.reset();
		if (!restoreFeatures)
		{
			if (saved != m_State)
				KeepSavedValue();
			return;
		}
		DropSavedValue();
		if (saved == m_State)
			return;
		m_State = saved;
		if (!m_Suspended)
			Apply(saved);
	}

	void BoolCommand::Suspend()
	{
		if (m_Suspended)
			return;
		m_Suspended = true;
		if (m_State)
			OnDisable();
	}

	void BoolCommand::Resume()
	{
		if (!m_Suspended)
			return;
		m_Suspended = false;
		if (m_State)
			OnEnable();
	}

	LoopedCommand::LoopedCommand(std::string name, std::string label, std::string description,
		std::function<void()> onTick, std::function<void(bool)> onChange, bool defaultValue) :
	    BoolCommand(std::move(name), std::move(label), std::move(description), std::move(onChange), defaultValue),
	    m_OnTick(std::move(onTick))
	{
		Commands::AddLoopedCommand(this); // ticks even if its name was a duplicate
	}
}
