/*
	Tests for src/core: Settings (Rampagio.json), the command types,
	Commands and HotkeySystem. Built against the real sources; no game
	needed. Exits 0 and prints ALL PASS on success.

	Settings, Commands and HotkeySystem are process-wide singletons, so the
	cases share them: each uses its own command names and re-initializes
	Settings on its own file in a temp folder.
*/

#include "..\src\core\settings\Settings.h"
#include "..\src\core\commands\Commands.h"
#include "..\src\core\commands\BoolCommand.h"
#include "..\src\core\commands\LoopedCommand.h"
#include "..\src\core\commands\ValueCommands.h"
#include "..\src\core\commands\ActionCommand.h"
#include "..\src\core\commands\HotkeySystem.h"
#include "..\src\core\commands\HotkeyPresets.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <windows.h>

using namespace Rampagio;
namespace fs = std::filesystem;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* name)
	{
		std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
		if (!condition)
			g_failures++;
	}

	fs::path g_dir;

	std::wstring FilePath(const char* name)
	{
		return (g_dir / name).wstring();
	}

	void WriteText(const std::wstring& path, const std::string& text)
	{
		std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
	}

	nlohmann::json ReadJson(const std::wstring& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream text;
		text << file.rdbuf();
		return nlohmann::json::parse(text.str(), nullptr, false);
	}

	void RoundTrip()
	{
		// Static: later cases' Suspend/ResetToDefaults still reach these hooks.
		static bool hookOn = false;
		static int ticks = 0;
		static int intStorage = 0;
		auto* b = new BoolCommand("test.rt.bool", "Bool", "", [&](bool on) { hookOn = on; });
		auto* l = new LoopedCommand("test.rt.looped", "Looped", "", [&] { ticks++; });
		auto* i = new IntCommand("test.rt.int", "Int", "", 0, 100, 5, 10, &intStorage);
		auto* f = new FloatCommand("test.rt.float", "Float", "", 0.0f, 2.0f, 0.1f, 1.0f);
		auto* li = new ListCommand("test.rt.list", "List", "", { "a", "b", "c" });
		auto* s = new StringCommand("test.rt.string", "String", "", "default");
		auto* c = new ColorCommand("test.rt.color", "Color", "", { 1, 2, 3, 4 });
		auto* a = new ActionCommand("test.rt.action", "Action", "", [] { return std::string("done"); });

		const std::wstring path = FilePath("roundtrip.json");
		Settings::Initialize(path);
		Check(intStorage == 10, "storage starts at the default");

		b->SetState(true);
		l->SetState(true);
		i->SetState(35);
		f->SetState(1.5f);
		li->SetState(2);
		s->SetState("hello");
		c->SetState({ 10, 20, 30, 40 });
		Check(hookOn, "BoolCommand::SetState runs onChange");
		Commands::RunLoopedCommands();
		Check(ticks == 1, "RunLoopedCommands ticks a looped command that's on");
		Check(intStorage == 35, "IntCommand writes through storage");
		Check(a->Run() == "done" && a->StatusText() == "done", "ActionCommand returns its status line");

		Check(Settings::Flush(), "Flush writes the file");
		const nlohmann::json saved = ReadJson(path);
		Check(saved["commands"]["test.rt.int"] == 35 && saved["commands"]["test.rt.string"] == "hello", "file holds the values");
		Check(saved["commands"]["test.rt.color"] == nlohmann::json::array({ 10, 20, 30, 40 }), "color saved as [r,g,b,a]");
		Check(!saved["commands"].contains("test.rt.action"), "actions aren't saved");

		Commands::ResetToDefaults();
		Check(!hookOn && intStorage == 10 && s->GetState() == "default", "ResetToDefaults restores defaults through the hooks");

		Settings::Reload();
		Commands::ApplyLoaded();
		Check(b->GetState() && hookOn, "bool restored and its hook ran");
		Check(l->GetState(), "looped restored");
		Check(intStorage == 35, "int restored");
		Check(f->GetState() == 1.5f, "float restored");
		Check(li->GetState() == 2, "list restored");
		Check(s->GetState() == "hello", "string restored");
		Check(c->GetState() == ColorRgba{ 10, 20, 30, 40 }, "color restored");
	}

	void Clamping()
	{
		auto* i = new IntCommand("test.clamp.int", "Int", "", 0, 10, 1, 5);
		auto* li = new ListCommand("test.clamp.list", "List", "", { "a", "b" });
		const std::wstring path = FilePath("clamp.json");
		WriteText(path, R"({"commands":{"test.clamp.int":999,"test.clamp.list":-4}})");
		Settings::Initialize(path);
		Commands::ApplyLoaded();
		Check(i->GetState() == 10, "int above max clamped on load");
		Check(li->GetState() == 0, "list index below 0 clamped on load");
		i->SetState(-3);
		Check(i->GetState() == 0, "SetState clamps");
		li->Step(-1);
		Check(li->GetState() == 1, "ListCommand::Step wraps");
	}

	void UnknownKeysKept()
	{
		auto* i = new IntCommand("test.unknown.int", "Int", "", 0, 10, 1, 5);
		const std::wstring path = FilePath("unknown.json");
		WriteText(path, R"({"future":{"x":1},"commands":{"gone.command":true,"test.unknown.int":3}})");
		Settings::Initialize(path);
		Commands::ApplyLoaded();
		i->SetState(7);
		Settings::Flush();
		const nlohmann::json saved = ReadJson(path);
		Check(saved.contains("future") && saved["future"]["x"] == 1, "unknown component kept");
		Check(saved["commands"].contains("gone.command"), "unknown command kept");
		Check(saved["commands"]["test.unknown.int"] == 7, "known command updated");
	}

	void CorruptFile()
	{
		auto* b = new BoolCommand("test.corrupt.bool", "Bool", "", nullptr, true);
		const std::wstring path = FilePath("corrupt.json");
		WriteText(path, "{\"commands\": {\"test.corrupt.bool\": fal");
		Settings::Initialize(path);
		Commands::ApplyLoaded();
		Check(b->GetState(), "corrupt file: defaults kept");
		Check(fs::exists(path + L".bad"), "corrupt file copied to .bad");
		Check(Settings::Flush() && ReadJson(path).is_object(), "corrupt file replaced by valid JSON on save");

		const std::wstring wrongType = FilePath("wrongtype.json");
		WriteText(wrongType, R"({"commands":{"test.corrupt.bool":"yes"}})");
		Settings::Initialize(wrongType);
		Commands::ApplyLoaded();
		Check(b->GetState(), "wrong value type ignored");
	}

	void Duplicates()
	{
		auto* first = new BoolCommand("test.dup", "First");
		auto* second = new BoolCommand("test.dup", "Second");
		auto* invalid = new BoolCommand("Test Dup", "Invalid");
		Check(first->IsRegistered() && !second->IsRegistered(), "duplicate id refused");
		Check(!invalid->IsRegistered(), "invalid id refused");
		Check(Commands::GetCommand("test.dup") == first, "lookup finds the first");
		Check(Commands::GetCommand<BoolCommand>(first->GetHash()) == first, "lookup by hash");
	}

	void Suspend()
	{
		static int disables = 0, ticks = 0;
		auto* l = new LoopedCommand("test.suspend.looped", "Looped", "", [&] { ticks++; }, [&](bool on) { if (!on) disables++; });
		const std::wstring path = FilePath("suspend.json");
		Settings::Initialize(path);
		l->SetState(true);
		Settings::Flush();
		Check(!Commands::IsDirty(), "clean after flush");
		Commands::Suspend();
		Check(disables == 1, "Suspend runs the disable hook");
		Check(l->GetState(), "Suspend keeps the state");
		Check(!Commands::IsDirty(), "Suspend doesn't mark dirty");
		Commands::RunLoopedCommands();
		Check(ticks == 0, "no ticks while suspended");
		Settings::Flush();
		Check(ReadJson(path)["commands"]["test.suspend.looped"] == true, "file keeps the pre-suspend state");
		Commands::Resume();
		Commands::RunLoopedCommands();
		Check(ticks == 1, "ticks again after Resume");
	}

	void Restore()
	{
		static bool hook = false;
		static int liveValue = 0, plainValue = 0, changes = 0;
		auto* b = new BoolCommand("test.restore.bool", "Bool", "", [&](bool on) { hook = on; });
		auto* looped = new LoopedCommand("test.restore.looped", "Looped");
		auto* live = new IntCommand("test.restore.live", "Live", "", 0, 10, 1, 1, &liveValue, [&] { changes++; });
		auto* plain = new IntCommand("test.restore.plain", "Plain", "", 0, 10, 1, 1, &plainValue);
		const std::wstring path = FilePath("restore.json");
		WriteText(path, R"({"commands":{"test.restore.bool":true,"test.restore.looped":true,"test.restore.live":5,"test.restore.plain":6}})");
		Settings::Initialize(path);
		Commands::ApplyLoaded();
		Check(b->GetState() && hook, "ticked toggle comes back on through its hook");
		Check(looped->GetState(), "ticked looped toggle comes back on");
		Check(liveValue == 5 && changes == 1, "value with onChange applied through it");
		Check(plainValue == 6, "plain value loads");

		// A toggle turned on in this session is written, and read back on the next start.
		b->SetState(false);
		looped->SetState(false);
		looped->SetState(true);
		Settings::Flush();
		nlohmann::json saved = ReadJson(path)["commands"];
		Check(saved["test.restore.bool"] == false && saved["test.restore.looped"] == true, "changed toggles are saved");
		hook = false;
		b->SetState(true);
		Settings::Flush();
		Settings::Reload();
		b->SetState(false);
		Settings::Reload();
		Commands::ApplyLoaded();
		Check(b->GetState() && hook, "a toggle ticked and saved comes back after a reload");

		Commands::ResetToDefaults();
		Settings::Flush();
		saved = ReadJson(path)["commands"];
		Check(saved["test.restore.bool"] == false && saved["test.restore.live"] == 1, "ResetToDefaults saves the defaults");
	}

	void Hotkeys()
	{
		static int calls = 0, shiftCalls = 0;
		new ActionCommand("test.hk.g", "G", "", [&] { calls++; return std::string(); });
		new ActionCommand("test.hk.shiftg", "Shift G", "", [&] { shiftCalls++; return std::string(); });
		const std::wstring path = FilePath("hotkeys.json");
		Settings::Initialize(path);
		HotkeySystem::Bind("test.hk.g", std::vector<InputId>{ 'G' });
		HotkeySystem::Bind("test.hk.shiftg", std::vector<InputId>{ VK_SHIFT, 'G' });
		HotkeySystem::Bind("test.hk.missing", std::vector<InputId>{ 'H' });

		std::uint32_t now = 1000;
		auto update = [&now](std::vector<InputId> down) { now += 16; return HotkeySystem::Update(down, now); };
		update({}); // clears the "held while binding" state
		update({ 'G' });
		update({ 'G' });
		Check(calls == 1, "chain fires once per press");
		update({});
		update({ VK_SHIFT, 'G' });
		Check(shiftCalls == 1 && calls == 1, "shorter chain inside a longer one doesn't fire");
		update({});
		update({ VK_SHIFT });
		update({ VK_SHIFT, 'G' });
		Check(shiftCalls == 2 && calls == 1, "chain completes in any order");

		Settings::Flush();
		const nlohmann::json saved = ReadJson(path)["hotkeys"];
		Check(saved["test.hk.shiftg"] == nlohmann::json::array({ nlohmann::json::array({ VK_SHIFT, 'G' }) }), "plain hotkeys saved as their chain");
		Check(saved.contains("test.hk.missing"), "binding for an unknown command kept");

		HotkeySystem::Bind("test.hk.shiftg", std::vector<InputId>{ 'G' });
		Check(!HotkeySystem::GetBindings().contains("test.hk.g"), "binding a used chain removes it from the other command");
		Check(HotkeySystem::GetBindings().at("test.hk.shiftg").size() == 2, "a command keeps several bindings");
		HotkeySystem::Clear("test.hk.shiftg");
		Settings::Reload();
		Check(HotkeySystem::GetBindings().size() == 3, "Reload reads the saved bindings back");

		// Old files: one flat chain per command.
		WriteText(path, R"({"hotkeys":{"test.hk.g":[71]}})");
		Settings::Reload();
		Check(HotkeySystem::GetBindings().at("test.hk.g").front().keys == std::vector<InputId>{ 'G' }, "old flat chain loads");
	}

	void HotkeyModes()
	{
		auto* toggle = new BoolCommand("test.hk.toggle", "Toggle");
		auto* list = new ListCommand("test.hk.list", "List", "", { "a", "b", "c" });
		auto* number = new IntCommand("test.hk.int", "Int", "", 0, 100, 5, 0);
		static int taps = 0, longs = 0;
		new ActionCommand("test.hk.tap", "Tap", "", [&] { taps++; return std::string(); });
		new ActionCommand("test.hk.long", "Long", "", [&] { longs++; return std::string(); });
		const std::wstring path = FilePath("hotkeymodes.json");
		WriteText(path, "{}");
		Settings::Initialize(path);
		HotkeySystem::SetLongPressMs(500);
		const InputId pad = kPadBase + 3;
		HotkeySystem::Bind("test.hk.toggle", Hotkey{ { 'Q' }, HotkeyGesture::Press, { HotkeyMode::Hold } });
		HotkeySystem::Bind("test.hk.list", Hotkey{ { pad, 'W' }, HotkeyGesture::Press, { HotkeyMode::Next } });
		HotkeySystem::Bind("test.hk.int", Hotkey{ { 'E' }, HotkeyGesture::Press, { HotkeyMode::Set, 40 } });
		HotkeySystem::Bind("test.hk.tap", Hotkey{ { 'R' } });
		HotkeySystem::Bind("test.hk.long", Hotkey{ { 'R' }, HotkeyGesture::Long });

		std::uint32_t now = 5000;
		auto at = [&now](std::uint32_t t, std::vector<InputId> down) { now = t; HotkeySystem::Update(down, now); };
		at(5000, {});
		at(5016, { 'Q' });
		Check(toggle->GetState(), "hold: on while held");
		at(5032, {});
		Check(!toggle->GetState(), "hold: back off on release");

		at(6000, { pad, 'W' });
		Check(list->GetState() == 1, "next steps a list");
		at(6300, { pad, 'W' });
		Check(list->GetState() == 1, "no repeat before the delay");
		at(6400, { pad, 'W' });
		Check(list->GetState() == 2, "next repeats while held");
		at(6500, {});

		at(7000, { 'E' });
		Check(number->GetState() == 40, "set puts a number to the value");
		at(7016, {});

		at(8000, { 'R' });
		at(8100, {});
		Check(taps == 1 && longs == 0, "tap: the press binding fires on a quick release");
		at(9000, { 'R' });
		at(9600, { 'R' });
		at(9700, {});
		Check(taps == 1 && longs == 1, "long press: only the long binding fires");

		Check(HotkeySystem::FindConflict({ 'R' }, HotkeyGesture::Long, "test.hk.tap") == std::optional<std::string>("test.hk.long"), "conflict found by chain and gesture");
		Check(HotkeySystem::PadChainUses(pad) == false && HotkeySystem::PadChainUses('W') == true, "pad chain lookup");

		Settings::Flush();
		const nlohmann::json saved = ReadJson(path)["hotkeys"];
		Check(saved["test.hk.int"][0]["action"] == "set" && saved["test.hk.int"][0]["value"] == 40, "set binding saved with its value");
		Check(saved["test.hk.long"][0]["gesture"] == "long", "long press saved");
		Settings::Reload();
		const Hotkey& loaded = HotkeySystem::GetBindings().at("test.hk.list").front();
		Check(loaded.action.mode == HotkeyMode::Next && loaded.keys.size() == 2, "modes load back");
	}

	void Presets()
	{
		auto* a = new BoolCommand("test.preset.a", "A");
		auto* n = new IntCommand("test.preset.n", "N", "", 0, 10, 1, 0);
		const std::wstring path = FilePath("presets.json");
		WriteText(path, "{}");
		Settings::Initialize(path);
		const std::string id = HotkeyPresets::Create("Combat Kit");
		Check(id == "preset.combatkit", "preset id from its name");
		HotkeyPresets::AddStep(id, { "test.preset.a", { HotkeyMode::Set, 1 } });
		HotkeyPresets::AddStep(id, { "test.preset.n", { HotkeyMode::Set, 7 } });
		HotkeyPresets::AddStep(id, { "test.preset.missing", {} });
		Command* command = Commands::GetCommand(id);
		Check(command != nullptr, "preset registers a command");
		command->Call();
		Check(a->GetState() && n->GetState() == 7, "preset runs its steps");
		Check(HotkeyPresets::Get(id)->command->LastRan() == 2, "missing step skipped");

		Settings::Flush();
		Settings::Reload();
		Check(HotkeyPresets::Get(id) && HotkeyPresets::Get(id)->steps.size() == 3 && Commands::GetCommand(id) == command, "preset reloads, keeping its command");
		HotkeyPresets::Delete(id);
		Check(!Commands::GetCommand(id), "deleting a preset removes its command");
	}

	void Transient()
	{
		auto* t = new BoolCommand("test.transient", "Transient");
		t->SetTransient();
		const std::wstring path = FilePath("transient.json");
		WriteText(path, R"({"commands":{"test.transient":true}})");
		Settings::Initialize(path);
		Commands::ApplyLoaded();
		Check(!t->GetState(), "transient command ignores its saved state");
		Settings::Flush();
		t->SetState(true);
		Check(!Commands::IsDirty(), "transient command doesn't mark dirty");
	}

	void Throttle()
	{
		auto* i = new IntCommand("test.throttle.int", "Int", "", 0, 100, 1, 0);
		const std::wstring path = FilePath("throttle.json");
		Settings::SetWriteInterval(60000);
		Settings::Initialize(path);
		Settings::Flush(); // starts the interval
		i->SetState(1);
		Settings::Tick();
		Check(ReadJson(path)["commands"]["test.throttle.int"] == 0, "Tick doesn't write inside the interval");
		Settings::SetWriteInterval(0);
		Settings::Tick();
		Check(ReadJson(path)["commands"]["test.throttle.int"] == 1, "Tick writes once the interval passed");
		Settings::SetWriteInterval(1000);
	}

	void TryFlushSnapshot()
	{
		auto* i = new IntCommand("test.tryflush.int", "Int", "", 0, 100, 1, 0);
		const std::wstring path = FilePath("tryflush.json");
		Settings::SetWriteInterval(60000);
		Settings::Initialize(path);
		Settings::Flush();
		i->SetState(1);
		Settings::Tick(); // inside the interval: snapshot only
		i->SetState(2); // not snapshotted yet
		Check(Settings::TryFlush(), "TryFlush writes");
		Check(ReadJson(path)["commands"]["test.tryflush.int"] == 1, "TryFlush writes the last Tick's snapshot, not live state");
		Settings::SetWriteInterval(1000);
	}
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	g_dir = fs::temp_directory_path() / ("RampagioSettingsTests_" + std::to_string(GetCurrentProcessId()));
	fs::create_directories(g_dir);
	HotkeySystem::GetInstance(); // components exist before the first load, as in ScriptMain
	HotkeyPresets::GetInstance();

	RoundTrip();
	Clamping();
	UnknownKeysKept();
	CorruptFile();
	Duplicates();
	Suspend();
	Restore();
	Hotkeys();
	HotkeyModes();
	Presets();
	Transient();
	Throttle();
	TryFlushSnapshot();

	std::error_code ec;
	fs::remove_all(g_dir, ec);

	if (g_failures == 0)
		std::printf("ALL PASS\n");
	else
		std::printf("%d FAILED\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
