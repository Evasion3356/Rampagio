#include "HotkeySystem.h"
#include "Command.h"
#include "Commands.h"
#include "BoolCommand.h"
#include "ValueCommands.h"
#include "..\..\Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <windows.h>

namespace Rampagio
{
	namespace
	{
		constexpr std::uint32_t kRepeatDelayMs = 400;
		constexpr std::uint32_t kRepeatMs = 100;

		const char* const kModeNames[] = { "press", "hold", "next", "previous", "set" };
		const char* const kGestureNames[] = { "press", "long" };

		std::vector<const char*> g_padNames;

		std::vector<InputId> Sorted(std::vector<InputId> chain)
		{
			std::sort(chain.begin(), chain.end());
			chain.erase(std::unique(chain.begin(), chain.end()), chain.end());
			return chain;
		}

		bool Contains(const std::vector<InputId>& sortedSet, const std::vector<InputId>& sortedSub)
		{
			return std::includes(sortedSet.begin(), sortedSet.end(), sortedSub.begin(), sortedSub.end());
		}

		bool Repeats(const Hotkey& hotkey)
		{
			return hotkey.action.mode == HotkeyMode::Next || hotkey.action.mode == HotkeyMode::Previous;
		}

		template <typename E, size_t N>
		E Parse(const nlohmann::json& j, const char* key, const char* const (&names)[N], E fallback)
		{
			auto it = j.find(key);
			if (it == j.end() || !it->is_string())
				return fallback;
			for (size_t i = 0; i < N; ++i)
				if (*it == names[i])
					return static_cast<E>(i);
			return fallback;
		}
	}

	HotkeySystem::HotkeySystem() :
	    IStateSerializer("hotkeys")
	{
	}

	HotkeySystem& HotkeySystem::GetInstance()
	{
		static HotkeySystem instance;
		return instance;
	}

	// --- bindings ----------------------------------------------------------------------------

	void HotkeySystem::Bind(const std::string& name, Hotkey hotkey)
	{
		HotkeySystem& self = GetInstance();
		if (hotkey.keys.empty())
			return;
		const std::vector<InputId> sorted = Sorted(hotkey.keys);
		for (auto it = self.m_Bindings.begin(); it != self.m_Bindings.end();)
		{
			std::erase_if(it->second, [&](const Hotkey& other)
			{
				return other.gesture == hotkey.gesture && Sorted(other.keys) == sorted;
			});
			it = it->second.empty() ? self.m_Bindings.erase(it) : std::next(it);
		}
		self.m_Bindings[name].push_back(std::move(hotkey));
		self.m_IndexDirty = true;
		self.m_Resync = true; // the keys may still be held from binding
		self.MarkStateDirty();
	}

	void HotkeySystem::Bind(const std::string& name, std::vector<InputId> chain)
	{
		Bind(name, Hotkey{ std::move(chain) });
	}

	std::optional<std::string> HotkeySystem::FindConflict(const std::vector<InputId>& chain, HotkeyGesture gesture, std::string_view except)
	{
		const std::vector<InputId> sorted = Sorted(chain);
		for (const auto& [name, hotkeys] : GetInstance().m_Bindings)
			if (name != except)
				for (const Hotkey& hotkey : hotkeys)
					if (hotkey.gesture == gesture && Sorted(hotkey.keys) == sorted)
						return name;
		return std::nullopt;
	}

	void HotkeySystem::Remove(std::string_view name, size_t index)
	{
		HotkeySystem& self = GetInstance();
		auto it = self.m_Bindings.find(name);
		if (it == self.m_Bindings.end() || index >= it->second.size())
			return;
		it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
		if (it->second.empty())
			self.m_Bindings.erase(it);
		self.m_IndexDirty = true;
		self.MarkStateDirty();
	}

	void HotkeySystem::Clear(std::string_view name)
	{
		HotkeySystem& self = GetInstance();
		if (auto it = self.m_Bindings.find(name); it != self.m_Bindings.end())
		{
			self.m_Bindings.erase(it);
			self.m_IndexDirty = true;
			self.MarkStateDirty();
		}
	}

	bool HotkeySystem::Replace(std::string_view name, size_t index, const Hotkey& hotkey)
	{
		HotkeySystem& self = GetInstance();
		auto it = self.m_Bindings.find(name);
		if (it == self.m_Bindings.end() || index >= it->second.size() || hotkey.keys.empty())
			return false;
		const std::vector<InputId> sorted = Sorted(hotkey.keys);
		for (const auto& [other, hotkeys] : self.m_Bindings)
			for (size_t i = 0; i < hotkeys.size(); ++i)
				if (!(other == name && i == index) && hotkeys[i].gesture == hotkey.gesture && Sorted(hotkeys[i].keys) == sorted)
					return false;
		it->second[index] = hotkey;
		self.m_IndexDirty = true;
		self.MarkStateDirty();
		return true;
	}

	void HotkeySystem::RebuildIndex()
	{
		m_Index.clear();
		m_Watched.clear();
		for (const auto& [name, hotkeys] : m_Bindings)
			for (const Hotkey& hotkey : hotkeys)
			{
				const std::vector<InputId> sorted = Sorted(hotkey.keys);
				for (InputId id : sorted)
				{
					auto& chains = m_Index[id];
					if (std::find(chains.begin(), chains.end(), sorted) == chains.end())
						chains.push_back(sorted);
					m_Watched.push_back(id);
				}
			}
		m_Watched = Sorted(std::move(m_Watched));
		m_IndexDirty = false;
	}

	std::span<const InputId> HotkeySystem::WatchedInputs()
	{
		HotkeySystem& self = GetInstance();
		if (self.m_IndexDirty)
			self.RebuildIndex();
		return self.m_Watched;
	}

	bool HotkeySystem::PadChainUses(InputId id)
	{
		HotkeySystem& self = GetInstance();
		if (self.m_IndexDirty)
			self.RebuildIndex();
		auto it = self.m_Index.find(id);
		if (it == self.m_Index.end())
			return false;
		for (const auto& chain : it->second)
			if (std::any_of(chain.begin(), chain.end(), [id](InputId other) { return other != id && IsPadInput(other); }))
				return true;
		return false;
	}

	std::pair<const std::string*, const Hotkey*> HotkeySystem::Find(const std::vector<InputId>& sorted, HotkeyGesture gesture) const
	{
		for (const auto& [name, hotkeys] : m_Bindings)
			for (const Hotkey& hotkey : hotkeys)
				if (hotkey.gesture == gesture && Sorted(hotkey.keys) == sorted)
					return { &name, &hotkey };
		return { nullptr, nullptr };
	}

	// --- running -----------------------------------------------------------------------------

	bool HotkeySystem::Supports(Command* command, HotkeyMode mode)
	{
		if (!command)
			return false;
		const bool isBool = dynamic_cast<BoolCommand*>(command) != nullptr;
		const bool isStep = dynamic_cast<IntCommand*>(command) || dynamic_cast<FloatCommand*>(command) || dynamic_cast<ListCommand*>(command);
		switch (mode)
		{
		case HotkeyMode::Press: return command->Hotkeyable();
		case HotkeyMode::Hold: return isBool;
		case HotkeyMode::Next:
		case HotkeyMode::Previous: return isStep;
		case HotkeyMode::Set: return isBool || isStep;
		default: return false;
		}
	}

	bool HotkeySystem::Bindable(Command* command)
	{
		if (!command || !command->IsRegistered())
			return false;
		for (int mode = 0; mode < static_cast<int>(HotkeyMode::Count); ++mode)
			if (Supports(command, static_cast<HotkeyMode>(mode)))
				return true;
		return false;
	}

	bool HotkeySystem::Apply(Command* command, const HotkeyAction& action, bool down)
	{
		if (!command || !down || !Supports(command, action.mode))
			return false;
		const int direction = action.mode == HotkeyMode::Previous ? -1 : 1;
		switch (action.mode)
		{
		case HotkeyMode::Press:
			command->Call();
			return true;
		case HotkeyMode::Hold: // only Fire holds; elsewhere (presets) it's a press
			command->Call();
			return true;
		case HotkeyMode::Next:
		case HotkeyMode::Previous:
			if (auto* i = dynamic_cast<IntCommand*>(command))
				i->Step(direction);
			else if (auto* f = dynamic_cast<FloatCommand*>(command))
				f->Step(direction);
			else if (auto* l = dynamic_cast<ListCommand*>(command))
				l->Step(direction);
			return true;
		case HotkeyMode::Set:
		{
			// Setting the value it already has re-applies it, as a select does.
			auto setOrApply = [command](auto* typed, auto value)
			{
				if (typed->GetState() == value)
				{
					if (typed->HasOnChange())
						command->Call();
				}
				else
					typed->SetState(value);
			};
			if (auto* b = dynamic_cast<BoolCommand*>(command))
				b->SetState(action.value != 0);
			else if (auto* i = dynamic_cast<IntCommand*>(command))
				setOrApply(i, static_cast<int>(std::lround(action.value)));
			else if (auto* f = dynamic_cast<FloatCommand*>(command))
				setOrApply(f, static_cast<float>(action.value));
			else if (auto* l = dynamic_cast<ListCommand*>(command))
				setOrApply(l, static_cast<int>(std::lround(action.value)));
			return true;
		}
		default:
			return false;
		}
	}

	void HotkeySystem::Fire(std::string name, Hotkey hotkey, Active& active)
	{
		Command* command = Commands::GetCommand(name);
		if (!command)
			return;
		if (hotkey.action.mode == HotkeyMode::Hold)
		{
			auto* toggle = dynamic_cast<BoolCommand*>(command);
			if (!toggle)
				return;
			active.holds.push_back({ name, toggle->GetState() });
			toggle->SetState(true);
		}
		else if (!Apply(command, hotkey.action))
			return;
		m_LastFired = command;
	}

	void HotkeySystem::EndChain(Active& active)
	{
		if (active.tapPending)
			if (auto [name, hotkey] = Find(active.keys, HotkeyGesture::Press); name && hotkey->action.mode != HotkeyMode::Hold)
				Fire(*name, *hotkey, active);
		for (const HoldState& hold : active.holds)
			if (auto* toggle = Commands::GetCommand<BoolCommand>(hold.name))
			{
				toggle->SetState(hold.previous);
				m_LastFired = toggle;
			}
		active.holds.clear();
	}

	void HotkeySystem::Release()
	{
		HotkeySystem& self = GetInstance();
		for (Active& active : self.m_Active)
		{
			active.tapPending = false; // cut short: not a tap
			self.EndChain(active);
		}
		self.m_Active.clear();
		self.m_Resync = true;
	}

	Command* HotkeySystem::Update(std::span<const InputId> held, std::uint32_t now)
	{
		HotkeySystem& self = GetInstance();
		if (self.m_IndexDirty)
			self.RebuildIndex();
		std::vector<InputId> down = Sorted(std::vector<InputId>(held.begin(), held.end()));
		if (self.m_Resync)
		{
			self.m_Resync = false;
			self.m_Down = std::move(down);
			return nullptr;
		}
		// Nothing changed and nothing waiting on time: the usual frame.
		if (down == self.m_Down && self.m_Active.empty())
			return nullptr;
		self.m_LastFired = nullptr;

		// Chains let go of.
		for (size_t i = 0; i < self.m_Active.size();)
		{
			if (Contains(down, self.m_Active[i].keys))
			{
				++i;
				continue;
			}
			Active ended = std::move(self.m_Active[i]);
			self.m_Active.erase(self.m_Active.begin() + static_cast<std::ptrdiff_t>(i));
			self.EndChain(ended);
		}

		// Chains completed by an input pressed this frame. Of two completed
		// together, one containing the other, only the longer counts.
		std::vector<InputId> pressed;
		std::set_difference(down.begin(), down.end(), self.m_Down.begin(), self.m_Down.end(), std::back_inserter(pressed));
		// Copies, as Fire gets them: a command run from here may change the
		// bindings, which rebuilds the index.
		std::vector<std::vector<InputId>> completed;
		for (InputId id : pressed)
			if (auto it = self.m_Index.find(id); it != self.m_Index.end())
				for (const auto& chain : it->second)
					if (Contains(down, chain) && std::find(completed.begin(), completed.end(), chain) == completed.end())
						completed.push_back(chain);
		for (const auto& keys : completed)
		{
			const bool inLonger = std::any_of(completed.begin(), completed.end(), [&keys](const auto& other)
			{
				return other.size() > keys.size() && Contains(other, keys);
			});
			if (inLonger)
				continue;
			auto [pressName, press] = self.Find(keys, HotkeyGesture::Press);
			auto [longName, longPress] = self.Find(keys, HotkeyGesture::Long);
			Active active{ keys, now, now + kRepeatDelayMs, press && longPress, false, std::nullopt, {} };
			if (press && !longPress)
			{
				if (Repeats(*press))
					active.repeating = HotkeyGesture::Press;
				self.Fire(*pressName, *press, active);
			}
			self.m_Active.push_back(std::move(active));
		}

		// Held chains: long presses and repeats.
		for (Active& active : self.m_Active)
		{
			if (!active.longFired && now - active.since >= self.m_LongPressMs)
				if (auto [name, hotkey] = self.Find(active.keys, HotkeyGesture::Long); name)
				{
					active.longFired = true;
					active.tapPending = false;
					active.nextRepeat = now + kRepeatDelayMs;
					if (Repeats(*hotkey))
						active.repeating = HotkeyGesture::Long;
					self.Fire(*name, *hotkey, active);
					continue;
				}
			if (active.repeating && static_cast<std::int32_t>(now - active.nextRepeat) >= 0)
			{
				active.nextRepeat = now + kRepeatMs;
				if (auto [name, hotkey] = self.Find(active.keys, *active.repeating); name && Repeats(*hotkey))
					self.Fire(*name, *hotkey, active);
			}
		}

		self.m_Down = std::move(down);
		return self.m_LastFired;
	}

	// --- names -------------------------------------------------------------------------------

	void HotkeySystem::SetPadNames(std::span<const char* const> names)
	{
		g_padNames.assign(names.begin(), names.end());
	}

	std::string HotkeySystem::KeyLabel(InputId id)
	{
		if (IsPadInput(id))
		{
			const size_t button = id - kPadBase;
			return button < g_padNames.size() ? std::string("Pad ") + g_padNames[button] : "Pad " + std::to_string(button);
		}
		switch (id)
		{
		case kWheelUp: return "Wheel Up";
		case kWheelDown: return "Wheel Down";
		case VK_LBUTTON: return "Mouse Left";
		case VK_RBUTTON: return "Mouse Right";
		case VK_MBUTTON: return "Mouse Middle";
		case VK_XBUTTON1: return "Mouse 4";
		case VK_XBUTTON2: return "Mouse 5";
		}
		char name[32] = {};
		LONG lParam = static_cast<LONG>(MapVirtualKeyA(id, MAPVK_VK_TO_VSC) << 16);
		// Keys that share a scan code with the numpad need the extended bit.
		switch (id)
		{
		case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
		case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
			lParam |= 1 << 24;
			break;
		}
		if (GetKeyNameTextA(lParam, name, sizeof(name)) > 0)
			return name;
		return std::to_string(id);
	}

	std::string HotkeySystem::ChainLabel(const std::vector<InputId>& chain)
	{
		std::string label;
		for (InputId id : chain)
			label += (label.empty() ? "" : " + ") + KeyLabel(id);
		return label;
	}


	// --- Rampagio.json -----------------------------------------------------------------------

	// A plain press binding is saved as its chain alone ([71]); others as
	// an object.
	void HotkeySystem::SaveStateImpl(nlohmann::json& state)
	{
		state = nlohmann::json::object();
		for (const auto& [name, hotkeys] : m_Bindings)
		{
			nlohmann::json& list = state[name] = nlohmann::json::array();
			for (const Hotkey& hotkey : hotkeys)
			{
				if (hotkey.gesture == HotkeyGesture::Press && hotkey.action.mode == HotkeyMode::Press)
				{
					list.push_back(hotkey.keys);
					continue;
				}
				nlohmann::json entry = { { "keys", hotkey.keys } };
				if (hotkey.gesture != HotkeyGesture::Press)
					entry["gesture"] = kGestureNames[static_cast<int>(hotkey.gesture)];
				if (hotkey.action.mode != HotkeyMode::Press)
					entry["action"] = kModeNames[static_cast<int>(hotkey.action.mode)];
				if (hotkey.action.mode == HotkeyMode::Set)
					entry["value"] = hotkey.action.value;
				list.push_back(std::move(entry));
			}
		}
	}

	void HotkeySystem::LoadStateImpl(nlohmann::json& state)
	{
		m_Bindings.clear();
		for (auto& [name, value] : state.items())
		{
			try
			{
				std::vector<Hotkey> hotkeys;
				// Before 2026-10-09: one chain, [vk, ...].
				if (value.is_array() && !value.empty() && value.front().is_number())
					hotkeys.push_back({ value.get<std::vector<InputId>>() });
				else if (value.is_array())
					for (const auto& entry : value)
					{
						Hotkey hotkey;
						if (entry.is_array())
							hotkey.keys = entry.get<std::vector<InputId>>();
						else if (entry.is_object())
						{
							hotkey.keys = entry.at("keys").get<std::vector<InputId>>();
							hotkey.gesture = Parse(entry, "gesture", kGestureNames, HotkeyGesture::Press);
							hotkey.action.mode = Parse(entry, "action", kModeNames, HotkeyMode::Press);
							if (auto it = entry.find("value"); it != entry.end() && it->is_number())
								hotkey.action.value = it->get<double>();
						}
						if (!hotkey.keys.empty())
							hotkeys.push_back(std::move(hotkey));
					}
				if (!hotkeys.empty())
					m_Bindings[name] = std::move(hotkeys);
			}
			catch (const std::exception& e)
			{
				Log::Write("[Hotkeys] Ignoring the binding of \"{}\": {}", name, e.what());
			}
		}
		m_IndexDirty = true;
		Release(); // don't fire for keys already held at load
	}
}
