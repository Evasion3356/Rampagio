#include "GamePointers.h"
#include "PatternScan.h"
#include "Log.h"

#include <windows.h>

#include <optional>

namespace
{
	constexpr int kScriptProgramSlots = 160;

	// Scans one signature; logs the miss (once per pattern) and the hit.
	std::optional<std::uintptr_t> Scan(const char* name, const char* pattern)
	{
		auto match = PatternScan::FindInMainModule(pattern);
		if (match)
			Log::Write("GamePointers: {} matched at {:#x}", name, static_cast<unsigned long long>(*match));
		else
			Log::Write("GamePointers: {} pattern not found", name);
		return match;
	}

	bool Resolve(GamePointers::Pointers& p)
	{
		auto threads = Scan("ScriptThreads", "48 8D 0D ? ? ? ? E8 ? ? ? ? EB 0B 8B 0D");
		auto programs = Scan("ScriptPrograms", "C1 EF 0E 85 FF 74 21");
		auto current = Scan("CurrentScriptThread&ScriptVM", "48 89 2D ? ? ? ? 48 89 2D ? ? ? ? 48 8B 04 F9");
		auto globals = Scan("ScriptGlobals", "48 8D 15 ? ? ? ? 48 8B 1D ? ? ? ? 8B 3D");
		if (!threads || !programs || !current || !globals)
			return false;

		p.ScriptThreads = reinterpret_cast<rage::atArray<rage::scrThread*>*>(PatternScan::ResolveRip(*threads, 3));
		// The LEA that loads the program table sits 0x16 bytes before the match.
		p.ScriptPrograms = reinterpret_cast<rage::scrProgram**>(PatternScan::ResolveRip(*programs - 0x16, 3) + 0xC8);
		p.CurrentScriptThread = reinterpret_cast<rage::scrThread**>(PatternScan::ResolveRip(*current, 3));
		p.ScriptVM = reinterpret_cast<GamePointers::ScriptVMFn>(PatternScan::ResolveRip(*current, 0x28));
		p.ScriptGlobals = reinterpret_cast<std::int64_t**>(PatternScan::ResolveRip(*globals, 3));

		if (auto handler = Scan("GetNativeHandler", "E8 ? ? ? ? 42 8B 9C FE"))
			p.GetNativeHandler = reinterpret_cast<GamePointers::GetNativeHandlerFn>(PatternScan::ResolveRip(*handler, 1));
		// The function starts 0x10 bytes before the match.
		if (auto init = Scan("InitNativeTables", "41 B0 01 44 39 51 2C 0F"))
			p.InitNativeTables = reinterpret_cast<void*>(*init - 0x10);

		// OnlineGuard's.
		if (auto ped = Scan("GetLocalPed", "8A 05 ? ? ? ? 33 D2 84 C0 74 39 48 8B 0D ? ? ? ? 4C 8B 05 ? ? ? ? 48 C1 C9 05 48 C1 C1 20 4C 33 C1 8B C1 83 E0 1F 49 C1 C0 20 FF C0 8A C8 8A 05 ? ? ? ? 49 D3 C0 84 C0 74 06 49 8B D0 48 F7 D2 48 8B 42"))
			p.GetLocalPed = reinterpret_cast<GamePointers::GetLocalPedFn>(*ped);
		if (auto mgr = Scan("NetworkPlayerMgr", "48 89 5C 24 08 57 48 83 EC 30 48 8B ? ? ? ? 01 8A D9 80 F9 20"))
			p.NetworkPlayerMgr = reinterpret_cast<void**>(PatternScan::ResolveRip(*mgr, 0xD));
		if (auto mgr = Scan("NetworkObjectMgr", "74 44 0F B7 56 40"))
			p.NetworkObjectMgr = reinterpret_cast<void**>(PatternScan::ResolveRip(*mgr, 0xC));
		if (auto started = Scan("IsSessionStarted", "40 38 35 ? ? ? ? 74 4D"))
			p.IsSessionStarted = reinterpret_cast<bool*>(PatternScan::ResolveRip(*started, 3));
		if (auto receive = Scan("ReceiveNetMessage", "E8 ? ? ? ? EB 24 48 8D B7 90 02 00 00"))
			p.ReceiveNetMessage = reinterpret_cast<void*>(PatternScan::ResolveRip(*receive, 1));
		return true;
	}
}

namespace GamePointers
{
	namespace
	{
		Pointers g_pointers;
		bool g_resolved = false;
	}

	const Pointers* Get()
	{
		static ULONGLONG nextAttemptMs = 0;
		if (g_resolved)
			return &g_pointers;

		const ULONGLONG nowMs = GetTickCount64();
		if (nowMs < nextAttemptMs)
			return nullptr;
		nextAttemptMs = nowMs + 5000;

		g_resolved = Resolve(g_pointers);
		return g_resolved ? &g_pointers : nullptr;
	}

	const Pointers* Cached()
	{
		return g_resolved ? &g_pointers : nullptr;
	}

	rage::scrThread* FindScriptThread(rage::joaat_t scriptHash)
	{
		const Pointers* p = Get();
		if (!p)
			return nullptr;

		for (auto thread : *p->ScriptThreads)
		{
			if (thread && thread->m_Context.m_ThreadId && thread->m_Context.m_ScriptHash == scriptHash)
				return thread;
		}
		return nullptr;
	}

	rage::scrProgram* FindScriptProgram(rage::joaat_t scriptHash)
	{
		const Pointers* p = Get();
		if (!p)
			return nullptr;

		for (int i = 0; i < kScriptProgramSlots; i++)
		{
			rage::scrProgram* program = p->ScriptPrograms[i];
			if (program && program->m_NameHash == scriptHash)
				return program;
		}
		return nullptr;
	}

	std::uint64_t* ScriptLocal(rage::scrThread* thread, std::uint32_t index)
	{
		if (!thread || !thread->m_Stack || index >= thread->m_Context.m_StackSize)
			return nullptr;
		return static_cast<std::uint64_t*>(thread->m_Stack) + index;
	}
}
