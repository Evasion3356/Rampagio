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

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
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
		Commands::ApplyLoaded(true);
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
		Commands::ApplyLoaded(true);
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
		Commands::ApplyLoaded(true);
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
		Commands::ApplyLoaded(true);
		Check(b->GetState(), "corrupt file: defaults kept");
		Check(fs::exists(path + L".bad"), "corrupt file copied to .bad");
		Check(Settings::Flush() && ReadJson(path).is_object(), "corrupt file replaced by valid JSON on save");

		const std::wstring wrongType = FilePath("wrongtype.json");
		WriteText(wrongType, R"({"commands":{"test.corrupt.bool":"yes"}})");
		Settings::Initialize(wrongType);
		Commands::ApplyLoaded(true);
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

	void RestoreOff()
	{
		static bool hook = false;
		static int liveValue = 0, plainValue = 0, changes = 0;
		auto* b = new BoolCommand("test.restore.bool", "Bool", "", [&](bool on) { hook = on; });
		auto* live = new IntCommand("test.restore.live", "Live", "", 0, 10, 1, 1, &liveValue, [&] { changes++; });
		auto* plain = new IntCommand("test.restore.plain", "Plain", "", 0, 10, 1, 1, &plainValue);
		auto* setting = new BoolCommand("settings.testrestore", "Setting");
		const std::wstring path = FilePath("restore.json");
		WriteText(path, R"({"commands":{"test.restore.bool":true,"test.restore.live":5,"test.restore.plain":6,"settings.testrestore":true}})");
		Settings::Initialize(path);
		Commands::ApplyLoaded(false);
		Check(!b->GetState() && !hook, "restore off: bool stays at default");
		Check(liveValue == 1 && changes == 0, "restore off: value with onChange stays at default");
		Check(plainValue == 6, "restore off: plain value loads");
		Check(setting->GetState(), "restore off: settings. commands still load");

		Settings::Reload();
		Commands::ApplyLoaded(true);
		Check(b->GetState() && hook, "restore on: bool restored through its hook");
		Check(liveValue == 5 && changes == 1, "restore on: value applied through onChange");
	}

	void Hotkeys()
	{
		static int calls = 0, shiftCalls = 0;
		new ActionCommand("test.hk.g", "G", "", [&] { calls++; return std::string(); });
		new ActionCommand("test.hk.shiftg", "Shift G", "", [&] { shiftCalls++; return std::string(); });
		const std::wstring path = FilePath("hotkeys.json");
		Settings::Initialize(path);
		HotkeySystem::Bind("test.hk.g", { 'G' });
		HotkeySystem::Bind("test.hk.shiftg", { VK_SHIFT, 'G' });
		HotkeySystem::Bind("test.hk.missing", { 'H' });

		std::set<int> down;
		auto isDown = [&down](int vk) { return down.contains(vk); };
		HotkeySystem::Update(isDown); // clears the "held while binding" state
		down = { 'G' };
		HotkeySystem::Update(isDown);
		HotkeySystem::Update(isDown);
		Check(calls == 1, "chain fires once per press");
		down = {};
		HotkeySystem::Update(isDown);
		down = { VK_SHIFT, 'G' };
		HotkeySystem::Update(isDown);
		Check(shiftCalls == 1 && calls == 1, "shorter chain inside a longer one doesn't fire");

		Settings::Flush();
		const nlohmann::json saved = ReadJson(path)["hotkeys"];
		Check(saved["test.hk.shiftg"] == nlohmann::json::array({ VK_SHIFT, 'G' }), "hotkeys saved by name");
		Check(saved.contains("test.hk.missing"), "binding for an unknown command kept");

		HotkeySystem::Bind("test.hk.shiftg", { 'G' });
		Check(!HotkeySystem::GetBindings().contains("test.hk.g"), "binding a used chain removes it from the other command");
		HotkeySystem::Clear("test.hk.shiftg");
		Settings::Reload();
		Check(HotkeySystem::GetBindings().size() == 3, "Reload reads the saved bindings back");
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
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	g_dir = fs::temp_directory_path() / ("RampagioSettingsTests_" + std::to_string(GetCurrentProcessId()));
	fs::create_directories(g_dir);
	HotkeySystem::GetInstance(); // components exist before the first load, as in ScriptMain

	RoundTrip();
	Clamping();
	UnknownKeysKept();
	CorruptFile();
	Duplicates();
	Suspend();
	RestoreOff();
	Hotkeys();
	Throttle();

	std::error_code ec;
	fs::remove_all(g_dir, ec);

	if (g_failures == 0)
		std::printf("ALL PASS\n");
	else
		std::printf("%d FAILED\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
