#include "HotkeyPresets.h"
#include "Command.h"
#include "Commands.h"
#include "..\..\Log.h"

#include <nlohmann/json.hpp>

#include <format>

namespace Rampagio
{
	PresetCommand::PresetCommand(std::string id, std::string name) :
	    Command(std::move(id), std::move(name))
	{
	}

	PresetCommand::~PresetCommand()
	{
		if (IsRegistered())
			Commands::RemoveCommand(this);
	}

	void PresetCommand::OnCall()
	{
		m_LastRan = HotkeyPresets::Run(GetName());
	}

	std::string PresetCommand::StatusText()
	{
		return std::format("{} ({} steps)", GetLabel(), m_LastRan);
	}

	namespace
	{
		const char* const kModeNames[] = { "press", "hold", "next", "previous", "set" };

		std::string Slug(std::string_view name)
		{
			std::string slug;
			for (char c : name)
			{
				if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c - 'A' + 'a');
				if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
					slug += c;
			}
			return slug.empty() ? "preset" : slug;
		}
	}

	HotkeyPresets::HotkeyPresets() :
	    IStateSerializer("presets")
	{
		// Constructed first, so destroyed after this: the presets' commands
		// unregister from it when they go.
		Commands::GetInstance();
	}

	HotkeyPresets& HotkeyPresets::GetInstance()
	{
		static HotkeyPresets instance;
		return instance;
	}

	HotkeyPresets::Preset& HotkeyPresets::Add(const std::string& id, std::string name)
	{
		Preset& preset = m_Presets[id];
		preset.name = std::move(name);
		if (!preset.command)
			preset.command = std::make_unique<PresetCommand>(id, preset.name);
		else
			preset.command->SetLabel(preset.name);
		return preset;
	}

	std::string HotkeyPresets::Create(std::string name)
	{
		HotkeyPresets& self = GetInstance();
		const std::string base = "preset." + Slug(name);
		std::string id = base;
		for (int n = 2; self.m_Presets.contains(id) || Commands::GetCommand(id); ++n)
			id = base + std::to_string(n);
		self.Add(id, std::move(name));
		self.MarkStateDirty();
		return id;
	}

	void HotkeyPresets::Delete(std::string_view id)
	{
		HotkeyPresets& self = GetInstance();
		if (auto it = self.m_Presets.find(id); it != self.m_Presets.end())
		{
			HotkeySystem::Clear(id);
			self.m_Presets.erase(it);
			self.MarkStateDirty();
		}
	}

	void HotkeyPresets::Rename(std::string_view id, std::string name)
	{
		if (Preset* preset = Get(id))
		{
			preset->name = std::move(name);
			preset->command->SetLabel(preset->name);
			GetInstance().MarkStateDirty();
		}
	}

	HotkeyPresets::Preset* HotkeyPresets::Get(std::string_view id)
	{
		auto& presets = GetInstance().m_Presets;
		auto it = presets.find(id);
		return it == presets.end() ? nullptr : &it->second;
	}

	void HotkeyPresets::AddStep(std::string_view id, PresetStep step)
	{
		if (Preset* preset = Get(id))
		{
			preset->steps.push_back(std::move(step));
			GetInstance().MarkStateDirty();
		}
	}

	void HotkeyPresets::RemoveStep(std::string_view id, size_t index)
	{
		if (Preset* preset = Get(id); preset && index < preset->steps.size())
		{
			preset->steps.erase(preset->steps.begin() + static_cast<std::ptrdiff_t>(index));
			GetInstance().MarkStateDirty();
		}
	}

	void HotkeyPresets::SetStep(std::string_view id, size_t index, PresetStep step)
	{
		if (Preset* preset = Get(id); preset && index < preset->steps.size())
		{
			preset->steps[index] = std::move(step);
			GetInstance().MarkStateDirty();
		}
	}

	void HotkeyPresets::MoveUp(std::string_view id, size_t index)
	{
		if (Preset* preset = Get(id); preset && index > 0 && index < preset->steps.size())
		{
			std::swap(preset->steps[index - 1], preset->steps[index]);
			GetInstance().MarkStateDirty();
		}
	}

	int HotkeyPresets::Run(std::string_view id)
	{
		HotkeyPresets& self = GetInstance();
		Preset* preset = Get(id);
		// A preset may run another; a loop of them stops here.
		if (!preset || self.m_Depth >= 4)
			return 0;
		++self.m_Depth;
		int ran = 0;
		const std::vector<PresetStep> steps = preset->steps; // a step may edit presets
		for (const PresetStep& step : steps)
			if (HotkeySystem::Apply(Commands::GetCommand(step.command), step.action))
				++ran;
		--self.m_Depth;
		return ran;
	}

	void HotkeyPresets::SaveStateImpl(nlohmann::json& state)
	{
		state = nlohmann::json::object();
		for (const auto& [id, preset] : m_Presets)
		{
			nlohmann::json steps = nlohmann::json::array();
			for (const PresetStep& step : preset.steps)
			{
				nlohmann::json entry = { { "command", step.command }, { "action", kModeNames[static_cast<int>(step.action.mode)] } };
				if (step.action.mode == HotkeyMode::Set)
					entry["value"] = step.action.value;
				steps.push_back(std::move(entry));
			}
			state[id] = { { "name", preset.name }, { "steps", std::move(steps) } };
		}
	}

	void HotkeyPresets::LoadStateImpl(nlohmann::json& state)
	{
		// Presets kept across a reload keep their command (and so the
		// pointers the menu or hotkeys hold); the rest go.
		std::map<std::string, Preset, std::less<>> old = std::move(m_Presets);
		m_Presets.clear();
		for (auto& [id, value] : state.items())
		{
			if (id.rfind("preset.", 0) != 0 || !value.is_object() || !Commands::IsValidName(id))
			{
				Log::Write("[Presets] Ignoring \"{}\"", id);
				continue;
			}
			try
			{
				if (auto it = old.find(id); it != old.end())
					m_Presets[id].command = std::move(it->second.command);
				Preset& preset = Add(id, value.value("name", id));
				for (const auto& entry : value.value("steps", nlohmann::json::array()))
				{
					PresetStep step;
					step.command = entry.at("command").get<std::string>();
					if (auto action = entry.find("action"); action != entry.end() && action->is_string())
						for (int i = 0; i < static_cast<int>(HotkeyMode::Count); ++i)
							if (*action == kModeNames[i])
								step.action.mode = static_cast<HotkeyMode>(i);
					if (auto v = entry.find("value"); v != entry.end() && v->is_number())
						step.action.value = v->get<double>();
					preset.steps.push_back(std::move(step));
				}
			}
			catch (const std::exception& e)
			{
				Log::Write("[Presets] Ignoring the steps of \"{}\": {}", id, e.what());
			}
		}
	}
}
