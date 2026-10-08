/*
	Hotkeys (HorseMenu's HotkeySystem) and the "hotkeys" part of
	Rampagio.json: { "<command name>": [vk, ...] }. A binding is a key chain:
	every key in it held together fires the command once.

	Rampagio differences: keyed by command name instead of hash (HorseMenu's
	"serialization isn't stable" TODO), bindings for names no command has
	are kept, a chain fires once per press instead of every 100 ms while
	held, and a chain held as part of a longer bound chain doesn't fire on
	its own (Shift+G doesn't also fire G). Key polling is passed in, so this
	file doesn't depend on the keyboard handler; the F11 binding flow and the
	Hotkey Manager are in src/menus/Settings.cpp.
*/

#pragma once

#include "..\settings\IStateSerializer.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Rampagio
{
	class Command;

	class HotkeySystem : private IStateSerializer
	{
		std::map<std::string, std::vector<int>> m_Bindings;
		std::map<std::string, bool> m_WasDown;

		HotkeySystem();

		void SaveStateImpl(nlohmann::json& state) override;
		void LoadStateImpl(nlohmann::json& state) override;

	public:
		static HotkeySystem& GetInstance();

		// Binds `chain` to `name`, replacing its old binding and removing the
		// same chain from any other command.
		static void Bind(const std::string& name, std::vector<int> chain);
		static void Clear(const std::string& name);
		static const std::map<std::string, std::vector<int>>& GetBindings() { return GetInstance().m_Bindings; }

		// Fires the commands whose chain was just completed; returns the last
		// one fired (or nullptr), so the caller can show its StatusText.
		static Command* Update(const std::function<bool(int)>& isKeyDown);

		// "Shift + G": a name per key, from the keyboard layout.
		static std::string KeyLabel(int vk);
		static std::string ChainLabel(const std::vector<int>& chain);
	};
}
