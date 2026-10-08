#include "Commands.h"
#include "Command.h"
#include "LoopedCommand.h"
#include "..\..\Log.h"

#include <rage/joaat.hpp>
#include <nlohmann/json.hpp>

namespace Rampagio
{
	Command::Command(std::string name, std::string label, std::string description) :
	    m_Name(std::move(name)),
	    m_Label(std::move(label)),
	    m_Description(std::move(description)),
	    m_Hash(rage::Joaat(m_Name))
	{
		m_Registered = Commands::AddCommand(this);
	}

	void Command::Call()
	{
		OnCall();
	}

	void Command::MarkDirty()
	{
		if (m_Registered && HasState())
			Commands::MarkDirty();
	}

	Commands::Commands() :
	    IStateSerializer("commands")
	{
	}

	Commands& Commands::GetInstance()
	{
		static Commands instance;
		return instance;
	}

	bool Commands::IsValidName(std::string_view name)
	{
		if (name.empty())
			return false;
		for (char c : name)
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
				return false;
		return true;
	}

	bool Commands::AddCommand(Command* command)
	{
		Commands& self = GetInstance();
		const std::string& name = command->GetName();
		if (!IsValidName(name))
		{
			Log::Write("[Commands] \"{}\" isn't a valid command id; not saved", name);
			return false;
		}
		if (self.m_ByName.contains(name))
		{
			Log::Write("[Commands] Duplicate command id \"{}\" (\"{}\"); not saved", name, command->GetLabel());
			return false;
		}
		if (auto it = self.m_ByHash.find(command->GetHash()); it != self.m_ByHash.end())
		{
			Log::Write("[Commands] \"{}\" has the same hash as \"{}\"; not saved", name, it->second->GetName());
			return false;
		}
		self.m_ByName.emplace(name, command);
		self.m_ByHash.emplace(command->GetHash(), command);
		self.m_Ordered.push_back(command);
		return true;
	}

	void Commands::AddLoopedCommand(LoopedCommand* command)
	{
		GetInstance().m_LoopedCommands.push_back(command);
	}

	void Commands::ForEach(const std::function<void(Command*)>& fn)
	{
		for (Command* command : GetInstance().m_Ordered)
			fn(command);
	}

	void Commands::RunLoopedCommands()
	{
		Commands& self = GetInstance();
		if (self.m_Suspended)
			return;
		for (LoopedCommand* command : self.m_LoopedCommands)
			if (command->GetState())
				command->Tick();
	}

	void Commands::ApplyLoaded(bool restoreFeatures)
	{
		for (Command* command : GetInstance().m_Ordered)
			command->ApplyLoaded(restoreFeatures || command->AlwaysRestore());
	}

	void Commands::ResetToDefaults()
	{
		for (Command* command : GetInstance().m_Ordered)
			command->ResetToDefault();
	}

	void Commands::Suspend()
	{
		Commands& self = GetInstance();
		if (self.m_Suspended)
			return;
		self.m_Suspended = true;
		for (Command* command : self.m_Ordered)
			command->Suspend();
	}

	void Commands::Resume()
	{
		Commands& self = GetInstance();
		if (!self.m_Suspended)
			return;
		self.m_Suspended = false;
		for (Command* command : self.m_Ordered)
			command->Resume();
	}

	void Commands::SaveStateImpl(nlohmann::json& state)
	{
		for (Command* command : m_Ordered)
			if (command->HasState())
				command->SaveState(state[command->GetName()]);
	}

	void Commands::LoadStateImpl(nlohmann::json& state)
	{
		for (Command* command : m_Ordered)
		{
			auto it = state.find(command->GetName());
			if (it == state.end())
				continue;
			try
			{
				command->LoadState(*it);
			}
			catch (const std::exception& e)
			{
				Log::Write("[Commands] Ignoring the saved state of \"{}\": {}", command->GetName(), e.what());
			}
		}
	}
}
