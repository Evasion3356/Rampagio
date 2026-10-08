#include "HotkeySystem.h"
#include "Command.h"
#include "Commands.h"
#include "..\..\Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <windows.h>

namespace Rampagio
{
	HotkeySystem::HotkeySystem() :
	    IStateSerializer("hotkeys")
	{
	}

	HotkeySystem& HotkeySystem::GetInstance()
	{
		static HotkeySystem instance;
		return instance;
	}

	void HotkeySystem::Bind(const std::string& name, std::vector<int> chain)
	{
		HotkeySystem& self = GetInstance();
		if (chain.empty())
			return Clear(name);
		std::vector<int> sorted = chain;
		std::sort(sorted.begin(), sorted.end());
		std::erase_if(self.m_Bindings, [&sorted](const auto& binding)
		{
			std::vector<int> other = binding.second;
			std::sort(other.begin(), other.end());
			return other == sorted;
		});
		self.m_Bindings[name] = std::move(chain);
		self.m_WasDown[name] = true; // the keys are still held from binding
		self.MarkStateDirty();
	}

	void HotkeySystem::Clear(const std::string& name)
	{
		HotkeySystem& self = GetInstance();
		if (self.m_Bindings.erase(name))
			self.MarkStateDirty();
	}

	Command* HotkeySystem::Update(const std::function<bool(int)>& isKeyDown)
	{
		HotkeySystem& self = GetInstance();
		std::vector<const std::pair<const std::string, std::vector<int>>*> down;
		for (const auto& binding : self.m_Bindings)
			if (!binding.second.empty() && std::all_of(binding.second.begin(), binding.second.end(), isKeyDown))
				down.push_back(&binding);

		auto partOfLonger = [&down](const std::vector<int>& chain)
		{
			for (const auto* other : down)
				if (other->second.size() > chain.size()
					&& std::all_of(chain.begin(), chain.end(), [other](int vk) { return std::find(other->second.begin(), other->second.end(), vk) != other->second.end(); }))
					return true;
			return false;
		};

		Command* fired = nullptr;
		std::map<std::string, bool> wasDown;
		for (const auto* binding : down)
		{
			wasDown[binding->first] = true;
			if (self.m_WasDown[binding->first] || partOfLonger(binding->second))
				continue;
			Command* command = Commands::GetCommand(binding->first);
			if (command && command->Hotkeyable())
			{
				command->Call();
				fired = command;
			}
		}
		self.m_WasDown = std::move(wasDown);
		return fired;
	}

	std::string HotkeySystem::KeyLabel(int vk)
	{
		char name[32] = {};
		LONG lParam = static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16);
		// Keys that share a scan code with the numpad need the extended bit.
		switch (vk)
		{
		case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
		case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
			lParam |= 1 << 24;
			break;
		}
		if (GetKeyNameTextA(lParam, name, sizeof(name)) > 0)
			return name;
		return std::to_string(vk);
	}

	std::string HotkeySystem::ChainLabel(const std::vector<int>& chain)
	{
		std::string label;
		for (int vk : chain)
			label += (label.empty() ? "" : " + ") + KeyLabel(vk);
		return label;
	}

	void HotkeySystem::SaveStateImpl(nlohmann::json& state)
	{
		state = nlohmann::json::object();
		for (const auto& [name, chain] : m_Bindings)
			state[name] = chain;
	}

	void HotkeySystem::LoadStateImpl(nlohmann::json& state)
	{
		m_Bindings.clear();
		for (auto& [name, value] : state.items())
		{
			try
			{
				std::vector<int> chain = value.get<std::vector<int>>();
				if (!chain.empty())
					m_Bindings[name] = std::move(chain);
			}
			catch (const std::exception& e)
			{
				Log::Write("[Hotkeys] Ignoring the binding of \"{}\": {}", name, e.what());
			}
		}
		for (const auto& [name, chain] : m_Bindings)
			m_WasDown[name] = true; // don't fire for keys already held at load
	}
}
