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
		return true;
	}
}

namespace GamePointers
{
	const Pointers* Get()
	{
		static Pointers pointers;
		static bool resolved = false;
		static ULONGLONG nextAttemptMs = 0;
		if (resolved)
			return &pointers;

		const ULONGLONG nowMs = GetTickCount64();
		if (nowMs < nextAttemptMs)
			return nullptr;
		nextAttemptMs = nowMs + 5000;

		resolved = Resolve(pointers);
		return resolved ? &pointers : nullptr;
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
