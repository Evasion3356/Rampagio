/*
	Hotkeys (ours, docs/HOTKEYS_PLAN.md): Settings > Hotkeys, the binding
	flow and the Hotkey window. The bindings themselves are
	src/core/commands/HotkeySystem.h, presets HotkeyPresets.h, and the
	input reading src/HotkeyInput.h.

	- F11 on a command row opens the Hotkey window (ImGui overlay): the
	  row's bindings, each with its gesture and action drop-downs, its value
	  and a Clear button, plus Add Key. With no binding yet, it starts
	  listening at once. English only, like the other overlay tools (the
	  overlay font has no Cyrillic or CJK glyphs); tools/lang_sync.py skips
	  ImGui lines and lines marked "// overlay text".
	- Y on a pad (the overlay has no pad input) binds the row from the native
	  menu directly; Settings > Hotkeys > Hotkey Manager edits bindings
	  there too.
	- While Settings > Hotkeys > Presets > Add Step waits, F11 or Y on a row
	  adds it to the preset instead.

	A capture waits until everything is let go, then collects what's held
	and finishes when it's all released; Esc or 10 s cancels. Left click is
	left out while the window listens, since it clicks the window.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\HotkeyInput.h"
#include "..\MainThread.h"
#include "..\keyboard.h"
#include "..\overlay\Overlay.h"
#include "..\core\commands\Commands.h"
#include "..\core\commands\HotkeyPresets.h"
#include "..\core\commands\HotkeySystem.h"

#include "imgui.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <functional>
#include <mutex>

using Rampagio::Commands;
using Rampagio::Hotkey;
using Rampagio::HotkeyAction;
using Rampagio::HotkeyGesture;
using Rampagio::HotkeyMode;
using Rampagio::HotkeyPresets;
using Rampagio::HotkeySystem;
using Rampagio::InputId;

namespace
{
	constexpr DWORD kCaptureTimeoutMs = 10000;
	constexpr Hash INPUT_FRONTEND_Y = 0x7C0162C0; // binds the selected row on a pad

	Rampagio::BoolCommand* g_hotkeysOn = nullptr;
	Rampagio::BoolCommand* g_padHotkeys = nullptr;
	int g_longPressMs = 500;
	int g_layerIndex = 2;
	const std::vector<std::string> kLayerNames = { "None", "LB", "RB", "LT", "RT" };
	// HotkeyMode's names, here so tools/lang_sync.py finds them.
	const char* const kModeNames[] = { "Press", "Hold", "Next", "Previous", "Set" };
	const char* ModeName(HotkeyMode mode) { return kModeNames[static_cast<int>(mode)]; }

	InputId LayerButton()
	{
		return g_layerIndex > 0 && g_layerIndex < static_cast<int>(kLayerNames.size()) ? HotkeyInput::PadByName(kLayerNames[g_layerIndex].c_str()) : 0;
	}

	std::vector<InputId> SortedChain(std::vector<InputId> chain)
	{
		std::sort(chain.begin(), chain.end());
		return chain;
	}

	// The pad combo that opens the menu (Settings > Core > Gamepad Open Key).
	std::vector<InputId> GamepadOpenChain()
	{
		using HotkeyInput::PadByName;
		switch (Style().gamepadOpen)
		{
		case 1: return SortedChain({ PadByName("RB"), PadByName("Left") });
		case 2: return SortedChain({ PadByName("LB"), PadByName("RB") });
		case 3: return SortedChain({ PadByName("RB"), PadByName("X") });
		default: return {};
		}
	}

	// The command's current value, as a Set action's value.
	double CurrentValue(Rampagio::Command* command)
	{
		if (auto* b = dynamic_cast<Rampagio::BoolCommand*>(command))
			return b->GetState() ? 1 : 0;
		if (auto* i = dynamic_cast<Rampagio::IntCommand*>(command))
			return i->GetState();
		if (auto* f = dynamic_cast<Rampagio::FloatCommand*>(command))
			return f->GetState();
		if (auto* l = dynamic_cast<Rampagio::ListCommand*>(command))
			return l->GetState();
		return 0;
	}

	// The action a new binding starts with: what selecting the row does,
	// else a step up, else hold.
	HotkeyAction DefaultAction(Rampagio::Command* command)
	{
		if (HotkeySystem::Supports(command, HotkeyMode::Press))
			return {};
		if (HotkeySystem::Supports(command, HotkeyMode::Next))
			return { HotkeyMode::Next };
		return { HotkeyMode::Hold };
	}

	// --- the Hotkey window's state (script thread) ------------------------------------------

	Overlay::Tool g_window = { "Hotkey", nullptr };
	std::string g_windowCommand;
	std::string g_windowStatus;
	// A chain another command has, waiting for Replace or Keep.
	std::vector<InputId> g_windowPending;

	// --- capture -----------------------------------------------------------------------------

	enum class Capture
	{
		None,
		Hotkey, // a chain for g_bindingCommand
		Key,    // one keyboard key for g_keyDone (the menu key)
	};
	Capture g_capture = Capture::None;
	bool g_captureInWindow = false;
	Rampagio::Command* g_bindingCommand = nullptr;
	std::function<void(int)> g_keyDone;
	std::vector<InputId> g_chain;
	bool g_captureArmed = false; // everything was let go since the capture started
	DWORD g_captureStart = 0;
	// Native flow: a chain another command has is bound only if the same one
	// is bound again.
	std::string g_pendingName;
	std::vector<InputId> g_pendingChain;
	// Settings > Hotkeys > Presets > Add Step: the next F11/Y on a row adds it here.
	std::string g_pickPreset;

	// A capture's outcome: in the window (English, like the overlay) or on
	// the native menu (translated).
	void Report(std::string windowText, const std::string& menuText, int ms = 3000)
	{
		if (g_captureInWindow)
			g_windowStatus = std::move(windowText);
		else
			Ui::Controller().SetStatusText(menuText, ms);
	}

	void StartCapture(Capture capture, Rampagio::Command* command, const std::string& prompt, bool inWindow = false)
	{
		g_capture = capture;
		g_captureInWindow = inWindow;
		g_bindingCommand = command;
		g_chain.clear();
		g_captureArmed = false;
		g_captureStart = GetTickCount();
		Ui::Controller().BlockInput(true);
		if (inWindow)
		{
			g_windowStatus.clear();
			g_windowPending.clear();
		}
		else
			Ui::Controller().SetStatusText(TrFormat("{} (Esc cancels)", Tr(prompt)), kCaptureTimeoutMs);
	}

	void EndCapture()
	{
		g_capture = Capture::None;
		g_chain.clear();
		Ui::Controller().BlockInput(false);
	}

	void FinishCapture()
	{
		if (g_capture == Capture::Hotkey)
		{
			const std::string name = g_bindingCommand->GetName();
			const std::string label = g_captureInWindow ? g_bindingCommand->GetLabel() : std::string(Tr(g_bindingCommand->GetLabel()));
			const std::string chainLabel = HotkeySystem::ChainLabel(g_chain);
			const std::vector<InputId> sorted = SortedChain(g_chain);
			const auto conflict = HotkeySystem::FindConflict(g_chain, HotkeyGesture::Press, name);
			const auto& bindings = HotkeySystem::GetBindings();
			auto own = bindings.find(name);
			const bool already = own != bindings.end() && std::any_of(own->second.begin(), own->second.end(), [&sorted](const Hotkey& h)
			{
				return h.gesture == HotkeyGesture::Press && SortedChain(h.keys) == sorted;
			});
			if (!GamepadOpenChain().empty() && sorted == GamepadOpenChain())
				Report(chainLabel + " opens the menu.", // overlay text
					TrFormat("{} opens the menu", chainLabel));
			else if (already)
				Report(chainLabel + " is already bound to this.", // overlay text
					TrFormat("{} is already bound to {}", chainLabel, label));
			else if (conflict && g_captureInWindow)
				g_windowPending = g_chain; // the window asks: Replace or Keep
			else if (conflict && !(g_pendingName == name && g_pendingChain == sorted))
			{
				Rampagio::Command* other = Commands::GetCommand(*conflict);
				g_pendingName = name;
				g_pendingChain = sorted;
				Ui::Controller().SetStatusText(TrFormat("{} already runs {}; bind it again to replace", chainLabel, other ? std::string(Tr(other->GetLabel())) : *conflict), 5000);
			}
			else
			{
				g_pendingName.clear();
				HotkeySystem::Bind(name, Hotkey{ g_chain, HotkeyGesture::Press, DefaultAction(g_bindingCommand) });
				Report("Bound " + chainLabel + ".", // overlay text
					TrFormat("{} bound to {}", chainLabel, label));
			}
		}
		else if (g_capture == Capture::Key && g_keyDone)
			g_keyDone(static_cast<int>(g_chain.front()));
		EndCapture();
	}

	// Waits until everything is let go, then collects what's held; the
	// chain is done once every input in it is released.
	void CaptureTick()
	{
		if (g_captureInWindow && !g_window.open)
		{
			EndCapture();
			return;
		}
		// The game shouldn't jump or shoot while the buttons go to the binding.
		PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
		if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) || GetTickCount() - g_captureStart > kCaptureTimeoutMs)
		{
			Report("Cancelled.", // overlay text
				std::string(Tr("Cancelled")), 2000);
			EndCapture();
			return;
		}
		static std::vector<InputId> held;
		HotkeyInput::PollAll(held);
		std::erase_if(held, [](InputId id)
		{
			return id == VK_ESCAPE || id == Menus::kHotkeyBindKey || id == static_cast<InputId>(MenuKey())
				|| (g_captureInWindow && id == VK_LBUTTON)
				|| (g_capture == Capture::Key && (id < 0x08 || id >= Rampagio::kPadBase));
		});
		if (!g_captureArmed)
		{
			g_captureArmed = held.empty();
			return;
		}
		for (InputId id : held)
			if (std::find(g_chain.begin(), g_chain.end(), id) == g_chain.end())
				if (g_capture == Capture::Hotkey || g_chain.empty())
					g_chain.push_back(id);
		// A wheel notch is never held, so it doesn't keep the chain open.
		const bool anyHeld = std::any_of(g_chain.begin(), g_chain.end(), [](InputId id)
		{
			return !Rampagio::IsWheelInput(id) && std::find(held.begin(), held.end(), id) != held.end();
		});
		if (!g_chain.empty() && !anyHeld)
			FinishCapture();
	}

	// A fired hotkey's status text, translated (Command::StatusText is
	// English: src/core doesn't depend on the game).
	std::string HotkeyStatus(Rampagio::Command* command)
	{
		if (auto* toggle = dynamic_cast<Rampagio::BoolCommand*>(command))
			return TrFormat(toggle->GetState() ? "{}: on" : "{}: off", Tr(toggle->GetLabel()));
		if (auto* list = dynamic_cast<Rampagio::ListCommand*>(command))
			return std::format("{}: {}", Tr(list->GetLabel()), Tr(list->GetSelected()));
		if (auto* number = dynamic_cast<Rampagio::IntCommand*>(command))
			return std::format("{}: {}", Tr(number->GetLabel()), number->GetState());
		if (auto* number = dynamic_cast<Rampagio::FloatCommand*>(command))
			return std::format("{}: {:.2f}", Tr(number->GetLabel()), number->GetState());
		if (auto* preset = dynamic_cast<Rampagio::PresetCommand*>(command))
			return TrFormat("{}: {} steps", preset->GetLabel(), preset->LastRan());
		return command->StatusText();
	}

	// --- the Hotkey window -------------------------------------------------------------------

	// What the window draws: copied on the script thread each frame it's
	// open, read on the render thread.
	struct BindingView
	{
		std::string keys;
		int gesture = 0;
		HotkeyMode mode = HotkeyMode::Press;
		double value = 0;
	};
	enum class ValueKind { None, Bool, Int, Float, List };
	struct WindowView
	{
		std::string name;
		std::string label;
		bool found = false;
		std::vector<BindingView> bindings;
		std::vector<HotkeyMode> modes;
		ValueKind kind = ValueKind::None;
		std::vector<std::string> listNames;
		double min = 0, max = 0, step = 1;
		bool capturing = false;
		std::string chain;
		std::string pendingKeys;
		std::string pendingOwner;
		std::string status;
	};
	std::mutex g_viewMutex;
	WindowView g_view;

	// Script thread.
	void UpdateWindowView()
	{
		WindowView view;
		view.name = g_windowCommand;
		Rampagio::Command* command = Commands::GetCommand(g_windowCommand);
		view.found = command != nullptr;
		view.label = command ? command->GetLabel() : g_windowCommand;
		const auto& bindings = HotkeySystem::GetBindings();
		if (auto it = bindings.find(g_windowCommand); it != bindings.end())
			for (const Hotkey& hotkey : it->second)
				view.bindings.push_back({ HotkeySystem::ChainLabel(hotkey.keys), static_cast<int>(hotkey.gesture), hotkey.action.mode, hotkey.action.value });
		for (int m = 0; m < static_cast<int>(HotkeyMode::Count); ++m)
			if (HotkeySystem::Supports(command, static_cast<HotkeyMode>(m)))
				view.modes.push_back(static_cast<HotkeyMode>(m));
		if (dynamic_cast<Rampagio::BoolCommand*>(command))
			view.kind = ValueKind::Bool;
		else if (auto* list = dynamic_cast<Rampagio::ListCommand*>(command))
		{
			view.kind = ValueKind::List;
			view.listNames = list->GetList();
		}
		else if (auto* i = dynamic_cast<Rampagio::IntCommand*>(command))
		{
			view.kind = ValueKind::Int;
			view.min = i->GetMinimum().value_or(INT_MIN);
			view.max = i->GetMaximum().value_or(INT_MAX);
			view.step = i->GetStep();
		}
		else if (auto* f = dynamic_cast<Rampagio::FloatCommand*>(command))
		{
			view.kind = ValueKind::Float;
			view.min = f->GetMinimum().value_or(-1e9f);
			view.max = f->GetMaximum().value_or(1e9f);
			view.step = f->GetStep();
		}
		view.capturing = g_capture == Capture::Hotkey && g_captureInWindow;
		if (view.capturing)
			view.chain = HotkeySystem::ChainLabel(g_chain);
		if (!g_windowPending.empty())
		{
			view.pendingKeys = HotkeySystem::ChainLabel(g_windowPending);
			if (auto owner = HotkeySystem::FindConflict(g_windowPending, HotkeyGesture::Press, g_windowCommand))
			{
				Rampagio::Command* other = Commands::GetCommand(*owner);
				view.pendingOwner = other ? other->GetLabel() : *owner;
			}
		}
		view.status = g_windowStatus;
		std::lock_guard lock(g_viewMutex);
		g_view = std::move(view);
	}

	// Script thread: edits binding `index` of the window's command, if it's
	// still the one the window showed.
	void EditBinding(const std::string& name, size_t index, const std::function<void(Hotkey&)>& edit)
	{
		if (name != g_windowCommand)
			return;
		const auto& bindings = HotkeySystem::GetBindings();
		auto it = bindings.find(name);
		if (it == bindings.end() || index >= it->second.size())
			return;
		Hotkey hotkey = it->second[index];
		edit(hotkey);
		if (!HotkeySystem::Replace(name, index, hotkey))
			g_windowStatus = "Another binding already uses that key and gesture."; // overlay text
		else
			g_windowStatus.clear();
	}

	void OpenWindow(Rampagio::Command* command)
	{
		g_windowCommand = command->GetName();
		g_windowStatus.clear();
		g_windowPending.clear();
		Overlay::SetOpen(g_window, true);
		const auto& bindings = HotkeySystem::GetBindings();
		if (!bindings.contains(g_windowCommand))
			StartCapture(Capture::Hotkey, command, "", true);
		UpdateWindowView();
	}

	// Render thread from here on: no natives, only g_view and MainThread::Post.

	const ImVec4 kYellow = { 1.0f, 0.85f, 0.3f, 1.0f };
	const ImVec4 kRed = { 1.0f, 0.4f, 0.4f, 1.0f };

	// The value widget of a Set binding; true when it changed.
	bool ValueWidget(const WindowView& view, double& value)
	{
		const float em = ImGui::GetFontSize();
		ImGui::SetNextItemWidth(em * 9);
		switch (view.kind)
		{
		case ValueKind::Bool:
		{
			int on = value != 0 ? 1 : 0;
			const char* names[] = { "Off", "On" };
			if (!ImGui::Combo("##value", &on, names, 2))
				return false;
			value = on;
			return true;
		}
		case ValueKind::List:
		{
			const int current = static_cast<int>(std::lround(value));
			const char* preview = current >= 0 && current < static_cast<int>(view.listNames.size()) ? view.listNames[current].c_str() : "";
			bool changed = false;
			if (ImGui::BeginCombo("##value", preview))
			{
				for (int i = 0; i < static_cast<int>(view.listNames.size()); ++i)
					if (ImGui::Selectable(view.listNames[i].c_str(), i == current))
					{
						value = i;
						changed = true;
					}
				ImGui::EndCombo();
			}
			return changed;
		}
		case ValueKind::Int:
		{
			int v = static_cast<int>(std::lround(value));
			if (!ImGui::InputInt("##value", &v, static_cast<int>(view.step)))
				return false;
			value = std::clamp(static_cast<double>(v), view.min, view.max);
			return true;
		}
		case ValueKind::Float:
		{
			float v = static_cast<float>(value);
			if (!ImGui::InputFloat("##value", &v, static_cast<float>(view.step), 0, "%.2f"))
				return false;
			value = std::clamp(static_cast<double>(v), view.min, view.max);
			return true;
		}
		default:
			ImGui::TextDisabled("-");
			return false;
		}
	}

	void DrawWindow(bool* open)
	{
		WindowView view;
		{
			std::lock_guard lock(g_viewMutex);
			view = g_view;
		}
		const float em = ImGui::GetFontSize();
		ImGui::SetNextWindowSize(ImVec2(em * 36, 0), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Hotkey###RampagioHotkey", open, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}
		ImGui::TextUnformatted(view.label.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", view.name.c_str());
		ImGui::Separator();

		const std::string name = view.name;
		if (view.bindings.empty())
			ImGui::TextDisabled("No hotkey bound.");
		else if (ImGui::BeginTable("bindings", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
		{
			ImGui::TableSetupColumn("Keys");
			ImGui::TableSetupColumn("Gesture");
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();
			for (size_t i = 0; i < view.bindings.size(); ++i)
			{
				const BindingView& binding = view.bindings[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(binding.keys.c_str());

				ImGui::TableNextColumn();
				int gesture = binding.gesture;
				const char* gestures[] = { "Press", "Long Press" };
				ImGui::SetNextItemWidth(em * 7);
				if (ImGui::Combo("##gesture", &gesture, gestures, 2))
					MainThread::Post([name, i, gesture] { EditBinding(name, i, [gesture](Hotkey& h) { h.gesture = static_cast<HotkeyGesture>(gesture); }); });
				ImGui::SetItemTooltip("Long Press fires once held for the Long Press Time\n(Settings > Hotkeys). A key with both runs its Press\nbinding when tapped.");

				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(em * 7);
				if (ImGui::BeginCombo("##action", ModeName(binding.mode)))
				{
					for (HotkeyMode mode : view.modes)
						if (ImGui::Selectable(ModeName(mode), mode == binding.mode) && mode != binding.mode)
							MainThread::Post([name, i, mode]
							{
								Rampagio::Command* command = Commands::GetCommand(name);
								EditBinding(name, i, [mode, command](Hotkey& h)
								{
									if (mode == HotkeyMode::Set && h.action.mode != HotkeyMode::Set)
										h.action.value = CurrentValue(command);
									h.action.mode = mode;
								});
							});
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Press: what selecting the row does\nHold: on only while held\nNext / Previous: one step, repeating while held\nSet: to the value beside it");

				ImGui::TableNextColumn();
				double value = binding.value;
				if (binding.mode == HotkeyMode::Set)
				{
					if (ValueWidget(view, value))
						MainThread::Post([name, i, value] { EditBinding(name, i, [value](Hotkey& h) { h.action.value = value; }); });
				}
				else
					ImGui::TextDisabled("-");

				ImGui::TableNextColumn();
				if (ImGui::Button("Clear"))
					MainThread::Post([name, i]
					{
						if (name == g_windowCommand)
							HotkeySystem::Remove(name, i);
					});
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::Separator();
		if (view.capturing)
		{
			ImGui::TextColored(kYellow, "Listening: hold the keys, mouse or pad buttons, then let go.");
			ImGui::TextDisabled("%s", view.chain.empty() ? "(Esc cancels; left click isn't bound here)" : view.chain.c_str());
			if (ImGui::Button("Cancel"))
				MainThread::Post([]
				{
					if (!g_captureInWindow)
						return;
					EndCapture();
					g_windowStatus = "Cancelled."; // overlay text
				});
		}
		else if (!view.pendingKeys.empty())
		{
			ImGui::TextColored(kRed, "%s already runs %s.", view.pendingKeys.c_str(), view.pendingOwner.c_str());
			if (ImGui::Button("Replace"))
				MainThread::Post([name]
				{
					if (name != g_windowCommand || g_windowPending.empty())
						return;
					if (Rampagio::Command* command = Commands::GetCommand(name))
						HotkeySystem::Bind(name, Hotkey{ g_windowPending, HotkeyGesture::Press, DefaultAction(command) });
					g_windowStatus = "Bound " + HotkeySystem::ChainLabel(g_windowPending) + "."; // overlay text
					g_windowPending.clear();
				});
			ImGui::SameLine();
			if (ImGui::Button("Keep"))
				MainThread::Post([] { g_windowPending.clear(); g_windowStatus.clear(); });
		}
		else
		{
			ImGui::BeginDisabled(!view.found);
			if (ImGui::Button(view.bindings.empty() ? "Bind Key" : "Add Key"))
				MainThread::Post([name]
				{
					if (Rampagio::Command* command = Commands::GetCommand(name); command && name == g_windowCommand && g_capture == Capture::None)
						StartCapture(Capture::Hotkey, command, "", true);
				});
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Another combination for the same row; a row can have several.");
			if (!view.bindings.empty())
			{
				ImGui::SameLine();
				if (ImGui::Button("Clear All"))
					MainThread::Post([name]
					{
						if (name == g_windowCommand)
							HotkeySystem::Clear(name);
					});
			}
		}
		if (!view.status.empty())
			ImGui::TextDisabled("%s", view.status.c_str());
		ImGui::Spacing();
		ImGui::TextDisabled("The menu key closes this window.");
		ImGui::End();
	}

	// --- the native menu flow ----------------------------------------------------------------

	// The highlighted row's command, or nullptr.
	Rampagio::Command* SelectedCommand()
	{
		MenuBase* menu = Ui::Controller().GetTopMenu();
		if (!menu)
			return nullptr;
		const int index = menu->GetActiveItemIndex();
		const auto& items = menu->GetItems();
		return index >= 0 && index < static_cast<int>(items.size()) ? items[index]->GetCommand() : nullptr;
	}

	// Adds the highlighted row to the preset Add Step is waiting for.
	void AddSelectedToPreset()
	{
		MenuController& menus = Ui::Controller();
		Rampagio::Command* command = SelectedCommand();
		if (!command || !command->IsRegistered() || command->GetName() == g_pickPreset)
		{
			menus.SetStatusText("This row can't be added", 2500);
			return;
		}
		// A preset sets things: toggles on, values to what they are now.
		HotkeyAction action;
		if (dynamic_cast<Rampagio::BoolCommand*>(command))
			action = { HotkeyMode::Set, 1 };
		else if (dynamic_cast<Rampagio::IntCommand*>(command) || dynamic_cast<Rampagio::FloatCommand*>(command)
			|| dynamic_cast<Rampagio::ListCommand*>(command))
			action = { HotkeyMode::Set, CurrentValue(command) };
		else if (!HotkeySystem::Supports(command, HotkeyMode::Press))
		{
			menus.SetStatusText("This row can't be added", 2500);
			return;
		}
		HotkeyPresets::AddStep(g_pickPreset, { command->GetName(), action });
		const auto* preset = HotkeyPresets::Get(g_pickPreset);
		menus.SetStatusText(TrFormat("Added {} to {}", Tr(command->GetLabel()), preset ? preset->name : g_pickPreset), 3000);
		g_pickPreset.clear();
	}

	// F11 (window) or Y on a pad (native) on a row.
	void BindSelectedRow(bool window)
	{
		if (!g_pickPreset.empty())
			return AddSelectedToPreset();
		Rampagio::Command* command = SelectedCommand();
		if (!HotkeySystem::Bindable(command))
			Ui::Controller().SetStatusText("This row can't be bound", 2500);
		else if (window)
			OpenWindow(command);
		else
			StartCapture(Capture::Hotkey, command, TrFormat("Press the key(s) for {}", Tr(command->GetLabel())));
	}

	// --- Settings > Hotkeys: the binding and preset step editors ---------------------------

	// "Set 5", "Set On", "Next": what an action does to `command`.
	std::string ActionText(Rampagio::Command* command, const HotkeyAction& action)
	{
		if (action.mode != HotkeyMode::Set)
			return std::string(Tr(ModeName(action.mode)));
		if (dynamic_cast<Rampagio::BoolCommand*>(command))
			return std::string(Tr(action.value != 0 ? "Set On" : "Set Off"));
		if (auto* list = dynamic_cast<Rampagio::ListCommand*>(command))
		{
			const int i = static_cast<int>(std::lround(action.value));
			if (i >= 0 && i < static_cast<int>(list->GetList().size()))
				return TrFormat("Set {}", Tr(list->GetList()[i]));
		}
		if (dynamic_cast<Rampagio::FloatCommand*>(command))
			return TrFormat("Set {}", std::format("{:.2f}", action.value));
		return TrFormat("Set {}", std::lround(action.value));
	}

	// Rows editing `action` for `command`: the action and, for Set, the
	// value. commit() saves the edit; false means it was refused, and the
	// edit is undone.
	void ActionRows(MenuBase* menu, Rampagio::Command* command, HotkeyAction* action, bool allowHold, bool (*commit)())
	{
		static std::vector<HotkeyMode> modes;
		static std::vector<std::string> modeNames;
		static int modeIndex = 0;
		modes.clear();
		modeNames.clear();
		modeIndex = 0;
		for (int m = 0; m < static_cast<int>(HotkeyMode::Count); ++m)
		{
			const auto mode = static_cast<HotkeyMode>(m);
			if ((mode != HotkeyMode::Hold || allowHold) && HotkeySystem::Supports(command, mode))
			{
				if (mode == action->mode)
					modeIndex = static_cast<int>(modes.size());
				modes.push_back(mode);
				modeNames.push_back(ModeName(mode));
			}
		}
		if (modes.empty())
			return;
		Ui::Choice(menu, "Action", modeNames, &modeIndex, [action, command, commit](int i)
		{
			const HotkeyAction old = *action;
			action->mode = modes[i];
			if (action->mode == HotkeyMode::Set && old.mode != HotkeyMode::Set)
				action->value = CurrentValue(command);
			if (!commit())
				*action = old;
			Ui::Controller().ReopenActiveLater();
		});
		Ui::Describe(menu, "Press: what selecting the row does\nHold: on while held\nNext / Previous: one step, repeating while held\nSet: to the value below");
		if (action->mode != HotkeyMode::Set)
			return;

		static int intValue = 0;
		static float floatValue = 0;
		auto write = [action, commit](double value)
		{
			const double old = action->value;
			action->value = value;
			if (!commit())
				action->value = old;
		};
		if (dynamic_cast<Rampagio::BoolCommand*>(command))
		{
			static const std::vector<std::string> onOff = { "Off", "On" };
			intValue = action->value != 0 ? 1 : 0;
			Ui::Choice(menu, "Value", onOff, &intValue, [write](int i) { write(i); });
		}
		else if (auto* list = dynamic_cast<Rampagio::ListCommand*>(command))
		{
			intValue = std::clamp(static_cast<int>(std::lround(action->value)), 0, (std::max)(0, static_cast<int>(list->GetList().size()) - 1));
			Ui::Choice(menu, "Value", list->GetList(), &intValue, [write](int i) { write(i); });
		}
		else if (auto* i = dynamic_cast<Rampagio::IntCommand*>(command))
		{
			intValue = static_cast<int>(std::lround(action->value));
			Ui::Number(menu, "Value", &intValue, i->GetMinimum().value_or(INT_MIN), i->GetMaximum().value_or(INT_MAX), i->GetStep(), [write] { write(intValue); });
		}
		else if (auto* f = dynamic_cast<Rampagio::FloatCommand*>(command))
		{
			floatValue = static_cast<float>(action->value);
			Ui::Number(menu, "Value", &floatValue, f->GetMinimum().value_or(-1e9f), f->GetMaximum().value_or(1e9f), f->GetStep(), [write] { write(floatValue); });
		}
	}

	// The binding the Hotkey Manager opened.
	std::string g_editName;
	size_t g_editIndex = 0;
	Hotkey g_editHotkey;

	bool CommitHotkey()
	{
		if (HotkeySystem::Replace(g_editName, g_editIndex, g_editHotkey))
			return true;
		Ui::Controller().SetStatusText("Another binding already uses that key and gesture", 3000);
		return false;
	}

	MenuBase* BuildBindingEditor()
	{
		return Ui::DetachedListMenu("Hotkey", [](MenuBase* menu)
		{
			Rampagio::Command* command = Commands::GetCommand(g_editName);
			Ui::Section(menu, HotkeySystem::ChainLabel(g_editHotkey.keys) + ": " + (command ? std::string(Tr(command->GetLabel())) : g_editName));
			if (command)
				ActionRows(menu, command, &g_editHotkey.action, true, CommitHotkey);
			static const std::vector<std::string> gestures = { "Press", "Long Press" };
			static int gesture = 0;
			gesture = static_cast<int>(g_editHotkey.gesture);
			Ui::Choice(menu, "Gesture", gestures, &gesture, [](int i)
			{
				const HotkeyGesture old = g_editHotkey.gesture;
				g_editHotkey.gesture = static_cast<HotkeyGesture>(i);
				if (!CommitHotkey())
				{
					g_editHotkey.gesture = old;
					gesture = static_cast<int>(old);
				}
			});
			Ui::Describe(menu, "Long Press: fires once held for the Long Press Time.\nA key with both runs its Press binding when tapped");
			Ui::Action(menu, "Remove", []
			{
				HotkeySystem::Remove(g_editName, g_editIndex);
				Ui::Controller().PopMenu();
				Ui::Controller().ReopenActiveLater();
				return std::string(Tr("Removed"));
			});
		});
	}

	// The preset (and step) the Presets menu opened.
	std::string g_editPreset;
	size_t g_editStep = 0;
	Rampagio::PresetStep g_editStepData;

	bool CommitStep()
	{
		HotkeyPresets::SetStep(g_editPreset, g_editStep, g_editStepData);
		return true;
	}

	MenuBase* BuildStepEditor()
	{
		return Ui::DetachedListMenu("Step", [](MenuBase* menu)
		{
			Rampagio::Command* command = Commands::GetCommand(g_editStepData.command);
			Ui::Section(menu, command ? std::string(Tr(command->GetLabel())) : TrFormat("{} (not found)", g_editStepData.command));
			if (command)
				ActionRows(menu, command, &g_editStepData.action, false, CommitStep);
			Ui::Do(menu, "Move Up", []
			{
				if (g_editStep == 0)
					return;
				HotkeyPresets::MoveUp(g_editPreset, g_editStep);
				Ui::Controller().PopMenu();
				Ui::Controller().ReopenActiveLater();
			});
			Ui::Do(menu, "Remove", []
			{
				HotkeyPresets::RemoveStep(g_editPreset, g_editStep);
				Ui::Controller().PopMenu();
				Ui::Controller().ReopenActiveLater();
			});
		});
	}

	MenuBase* BuildPresetEditor()
	{
		static MenuBase* stepEditor = BuildStepEditor();
		return Ui::DetachedListMenu("Preset", [](MenuBase* menu)
		{
			const auto* preset = HotkeyPresets::Get(g_editPreset);
			if (!preset)
				return;
			Ui::Section(menu, preset->name);
			Ui::Action(menu, "Run", []
			{
				auto* preset = HotkeyPresets::Get(g_editPreset);
				if (!preset)
					return std::string();
				preset->command->Call();
				return HotkeyStatus(preset->command.get());
			});
			Ui::Do(menu, "Bind Hotkey", []
			{
				if (auto* preset = HotkeyPresets::Get(g_editPreset); preset && preset->command->IsRegistered())
					StartCapture(Capture::Hotkey, preset->command.get(), TrFormat("Press the key(s) for {}", preset->name));
			});
			Ui::Action(menu, "Rename", []
			{
				auto* preset = HotkeyPresets::Get(g_editPreset);
				std::string name = preset ? preset->name : "";
				if (!preset || !GameUtil::PromptText("Preset Name:", name) || name.empty())
					return std::string();
				HotkeyPresets::Rename(g_editPreset, name);
				Ui::Controller().ReopenActiveLater();
				return TrFormat("Renamed to {}", name);
			});
			Ui::Action(menu, "Add Step", []
			{
				g_pickPreset = g_editPreset;
				return std::string(Tr("Highlight any row and press F11 (or Y on a pad) to add it"));
			});
			Ui::Section(menu, "Steps");
			if (preset->steps.empty())
				Ui::Section(menu, "No steps");
			for (size_t i = 0; i < preset->steps.size(); ++i)
			{
				const Rampagio::PresetStep& step = preset->steps[i];
				Rampagio::Command* command = Commands::GetCommand(step.command);
				const std::string caption = command
					? std::format("{}. {}: {}", i + 1, Tr(command->GetLabel()), ActionText(command, step.action))
					: std::format("{}. {}", i + 1, TrFormat("{} (not found)", step.command));
				Ui::Do(menu, caption, [i]
				{
					auto* preset = HotkeyPresets::Get(g_editPreset);
					if (!preset || i >= preset->steps.size())
						return;
					g_editStep = i;
					g_editStepData = preset->steps[i];
					Ui::Push(stepEditor);
				});
			}
			Ui::Section(menu, "Manage");
			Ui::Do(menu, "Delete Preset", []
			{
				HotkeyPresets::Delete(g_editPreset);
				g_pickPreset.clear();
				Ui::Controller().PopMenu();
				Ui::Controller().ReopenActiveLater();
			});
		});
	}
}

namespace Menus
{
	void BuildHotkeys(MenuBase* settings)
	{
		MenuBase* hotkeys = Ui::Submenu(settings, "Hotkeys");
		g_hotkeysOn = Ui::Toggle(hotkeys, "settings.hotkeys.enabled", "Hotkeys Enabled", nullptr)->SetDefault(true);
		Ui::Number(hotkeys, "settings.hotkeys.longpress", "Long Press Time (ms)", &g_longPressMs, 200, 2000, 50);
		g_padHotkeys = Ui::Toggle(hotkeys, "settings.hotkeys.gamepad", "Gamepad Hotkeys", nullptr)->SetDefault(true);
		Ui::Choice(hotkeys, "settings.hotkeys.layer", "Gamepad Layer Button", kLayerNames, &g_layerIndex);
		Ui::Describe(hotkeys, "While held, the game ignores the other pad buttons\n(moving and looking still work), so combos with it only run hotkeys");

		static MenuBase* bindingEditor = BuildBindingEditor();
		Ui::ListMenu(hotkeys, "Hotkey Manager", [](MenuBase* menu)
		{
			Ui::Section(menu, "F11 (or Y on a pad) on a row binds it");
			if (HotkeySystem::GetBindings().empty())
				Ui::Section(menu, "No hotkeys");
			for (const auto& [name, list] : HotkeySystem::GetBindings())
			{
				Rampagio::Command* command = Commands::GetCommand(name);
				const std::string label = command ? std::string(Tr(command->GetLabel())) : TrFormat("{} (not found)", name);
				for (size_t i = 0; i < list.size(); ++i)
				{
					const Hotkey& hotkey = list[i];
					std::string caption = HotkeySystem::ChainLabel(hotkey.keys);
					if (hotkey.gesture == HotkeyGesture::Long)
						caption += " (" + std::string(Tr("Long Press")) + ")";
					caption += ": " + label;
					if (command && hotkey.action.mode != HotkeyMode::Press)
						caption += " - " + ActionText(command, hotkey.action);
					Ui::Do(menu, caption, [name, i]
					{
						const auto& bindings = HotkeySystem::GetBindings();
						auto it = bindings.find(name);
						if (it == bindings.end() || i >= it->second.size())
							return;
						g_editName = name;
						g_editIndex = i;
						g_editHotkey = it->second[i];
						Ui::Push(bindingEditor);
					});
				}
			}
		});

		static MenuBase* presetEditor = BuildPresetEditor();
		Ui::ListMenu(hotkeys, "Presets", [](MenuBase* menu)
		{
			Ui::Action(menu, "Create Preset", []
			{
				std::string name;
				if (!GameUtil::PromptText("Preset Name:", name) || name.empty())
					return std::string();
				g_editPreset = HotkeyPresets::Create(name);
				Ui::Push(presetEditor);
				return TrFormat("Created {}", name);
			});
			Ui::Describe(menu, "A preset runs several rows at once (Godmode on,\nInfinite Ammo on, ...) and binds like any row");
			if (HotkeyPresets::All().empty())
				Ui::Section(menu, "No presets");
			for (const auto& [id, preset] : HotkeyPresets::All())
				Ui::Do(menu, TrFormat("{} ({} steps)", preset.name, preset.steps.size()), [id]
				{
					g_editPreset = id;
					Ui::Push(presetEditor);
				});
		});
	}

	void RegisterHotkeys()
	{
		HotkeySystem::GetInstance();
		HotkeySystem::SetPadNames({ HotkeyInput::kPadNames, static_cast<size_t>(HotkeyInput::kPadCount) });
		HotkeyPresets::GetInstance();
		g_window.draw = DrawWindow;
		Overlay::Register(g_window);
	}

	void CaptureKey(const std::string& prompt, std::function<void(int)> done)
	{
		g_keyDone = std::move(done);
		StartCapture(Capture::Key, nullptr, prompt);
	}

	void SuspendHotkeys()
	{
		Overlay::SetOpen(g_window, false);
		if (g_capture != Capture::None)
			EndCapture();
		HotkeySystem::Release();
	}

	void TickHotkeys()
	{
		MenuController& menus = Ui::Controller();
		HotkeySystem::SetLongPressMs(static_cast<std::uint32_t>(g_longPressMs));
		if (g_window.open)
			UpdateWindowView();
		if (g_capture != Capture::None)
		{
			HotkeySystem::Release();
			return CaptureTick();
		}
		if (Overlay::AnyOpen())
		{
			HotkeySystem::Release();
			return;
		}
		if (menus.HasActiveMenu())
		{
			HotkeySystem::Release();
			const bool padBind = Style().gamepad && HotkeyInput::PadInUse() && PAD::IS_DISABLED_CONTROL_JUST_PRESSED(2, INPUT_FRONTEND_Y);
			if (IsKeyJustUp(kHotkeyBindKey))
				BindSelectedRow(true);
			else if (padBind)
				BindSelectedRow(false);
			return;
		}
		if (!g_hotkeysOn->GetState() || HUD::IS_PAUSE_MENU_ACTIVE() || !HotkeyInput::GameInFront())
		{
			HotkeySystem::Release();
			return;
		}

		static std::vector<InputId> held;
		HotkeyInput::Poll(HotkeySystem::WatchedInputs(), held);
		if (!g_padHotkeys->GetState())
			std::erase_if(held, Rampagio::IsPadInput);
		else if (const InputId layer = LayerButton(); layer && std::find(held.begin(), held.end(), layer) != held.end()
			&& HotkeySystem::PadChainUses(layer))
			HotkeyInput::SuppressGameControls();

		if (Rampagio::Command* fired = HotkeySystem::Update(held, GetTickCount()))
			menus.SetStatusText(HotkeyStatus(fired), 1500);
	}
}
