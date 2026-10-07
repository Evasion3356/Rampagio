#include "NativeHooks.h"
#include "GamePointers.h"
#include "Log.h"

#include "..\external\minhook\include\MinHook.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr int kScriptProgramSlots = 160;
	// pgBase's vtable: the scalar deleting destructor and two more.
	constexpr int kVtableEntries = 3;

	using DtorFn = void* (*)(rage::scrProgram* program, unsigned int flags);

	struct Hook
	{
		NativeHooks::Id id;
		rage::joaat_t script;
		rage::scrNativeHandler original;
		rage::scrNativeHandler replacement;
	};

	struct Program
	{
		rage::scrProgram* program = nullptr;
		std::vector<rage::scrNativeHandler> originals; // the table as the game built it
		void** gameVtable = nullptr;
		std::unique_ptr<void*[]> vtable; // our copy, with the destructor swapped
	};

	// Programs register from the InitNativeTables detour, which may run
	// off the script thread.
	std::recursive_mutex g_mutex;
	bool g_initialized = false;
	GamePointers::InitNativeTablesFn g_initNativeTables = nullptr; // the original
	std::vector<Hook> g_hooks;
	std::unordered_map<rage::scrProgram*, Program> g_programs;
	NativeHooks::Id g_nextId = 1;

	bool Applies(const Hook& hook, const rage::scrProgram* program)
	{
		return hook.script == NativeHooks::kAllScripts || hook.script == program->m_NameHash;
	}

	// Rewrites every table slot that held `original`: back to the game's
	// handler, then to the newest hook on it that applies to this program.
	void Refresh(Program& entry, rage::scrNativeHandler original)
	{
		rage::scrNativeHandler handler = original;
		for (const Hook& hook : g_hooks)
		{
			if (hook.original == original && Applies(hook, entry.program))
				handler = hook.replacement;
		}
		for (std::size_t i = 0; i < entry.originals.size(); i++)
		{
			if (entry.originals[i] == original)
				entry.program->m_NativeEntrypoints[i] = handler;
		}
	}

	void ApplyAll(Program& entry)
	{
		for (const Hook& hook : g_hooks)
		{
			if (Applies(hook, entry.program))
				Refresh(entry, hook.original);
		}
	}

	void RestoreTable(Program& entry)
	{
		if (entry.program->m_NativeEntrypoints && entry.program->m_NativeCount == entry.originals.size())
			std::copy(entry.originals.begin(), entry.originals.end(), entry.program->m_NativeEntrypoints);
	}

	void RestoreVtable(Program& entry)
	{
		*reinterpret_cast<void***>(entry.program) = entry.gameVtable;
	}

	void* ProgramDtor(rage::scrProgram* program, unsigned int flags)
	{
		{
			std::lock_guard lock(g_mutex);
			if (auto it = g_programs.find(program); it != g_programs.end())
			{
				RestoreTable(it->second);
				RestoreVtable(it->second);
				g_programs.erase(it);
			}
		}
		// The game's vtable is back in place, so this is its destructor.
		return (*reinterpret_cast<DtorFn**>(program))[0](program, flags);
	}

	void RegisterProgram(rage::scrProgram* program)
	{
		if (!program || !program->m_NativeEntrypoints || program->m_NativeCount == 0)
			return;

		std::lock_guard lock(g_mutex);
		auto [it, added] = g_programs.try_emplace(program);
		Program& entry = it->second;
		// A program that runs InitNativeTables again gets a fresh table from
		// the game; take a new copy of it either way.
		entry.program = program;
		entry.originals.assign(program->m_NativeEntrypoints, program->m_NativeEntrypoints + program->m_NativeCount);
		if (added)
		{
			entry.gameVtable = *reinterpret_cast<void***>(program);
			entry.vtable = std::make_unique<void*[]>(kVtableEntries);
			std::copy(entry.gameVtable, entry.gameVtable + kVtableEntries, entry.vtable.get());
			entry.vtable[0] = reinterpret_cast<void*>(&ProgramDtor);
			*reinterpret_cast<void***>(program) = entry.vtable.get();
		}
		ApplyAll(entry);
	}

	bool InitNativeTablesDetour(rage::scrProgram* program)
	{
		const bool result = g_initNativeTables(program);
		if (result)
			RegisterProgram(program);
		return result;
	}

	// Detours InitNativeTables and registers the programs already loaded.
	// A failure is retried on the next Add (GamePointers rescans every 5 s).
	bool EnsureInitialized()
	{
		if (g_initialized)
			return true;

		const GamePointers::Pointers* p = GamePointers::Get();
		if (!p || !p->GetNativeHandler || !p->InitNativeTables)
		{
			Log::Write("NativeHooks: pointers not resolved");
			return false;
		}

		const MH_STATUS init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
		{
			Log::Write("NativeHooks: MH_Initialize failed ({})", MH_StatusToString(init));
			return false;
		}
		MH_STATUS status = MH_CreateHook(p->InitNativeTables, reinterpret_cast<void*>(&InitNativeTablesDetour),
			reinterpret_cast<void**>(&g_initNativeTables));
		if (status == MH_OK)
			status = MH_EnableHook(p->InitNativeTables);
		if (status != MH_OK)
		{
			Log::Write("NativeHooks: InitNativeTables hook failed ({})", MH_StatusToString(status));
			MH_RemoveHook(p->InitNativeTables);
			return false;
		}

		std::lock_guard lock(g_mutex);
		for (int i = 0; i < kScriptProgramSlots; i++)
			RegisterProgram(p->ScriptPrograms[i]);
		g_initialized = true;
		Log::Write("NativeHooks: ready, {} programs registered", g_programs.size());
		return true;
	}
}

namespace NativeHooks
{
	rage::scrNativeHandler Original(std::uint64_t nativeHash)
	{
		const GamePointers::Pointers* p = GamePointers::Get();
		if (!p || !p->GetNativeHandler)
			return nullptr;
		return p->GetNativeHandler(static_cast<rage::scrNativeHash>(nativeHash));
	}

	Id Add(rage::joaat_t script, std::uint64_t nativeHash, rage::scrNativeHandler replacement)
	{
		if (!EnsureInitialized())
			return 0;
		const rage::scrNativeHandler original = Original(nativeHash);
		if (!original)
		{
			Log::Write("NativeHooks: no handler for native {:#x}", nativeHash);
			return 0;
		}

		std::lock_guard lock(g_mutex);
		const Hook hook = { g_nextId++, script, original, replacement };
		g_hooks.push_back(hook);
		for (auto& [program, entry] : g_programs)
		{
			if (Applies(hook, program))
				Refresh(entry, original);
		}
		return hook.id;
	}

	void Remove(Id id)
	{
		if (id == 0)
			return;

		std::lock_guard lock(g_mutex);
		auto it = std::find_if(g_hooks.begin(), g_hooks.end(), [id](const Hook& hook) { return hook.id == id; });
		if (it == g_hooks.end())
			return;
		const Hook hook = *it;
		g_hooks.erase(it);
		for (auto& [program, entry] : g_programs)
		{
			if (Applies(hook, program))
				Refresh(entry, hook.original);
		}
	}

	void Shutdown()
	{
		if (!g_initialized)
			return;

		// Detour off first, so no program registers while we restore.
		const GamePointers::Pointers* p = GamePointers::Get();
		if (p && p->InitNativeTables)
		{
			MH_DisableHook(p->InitNativeTables);
			MH_RemoveHook(p->InitNativeTables);
		}

		std::lock_guard lock(g_mutex);
		for (auto& [program, entry] : g_programs)
		{
			RestoreTable(entry);
			RestoreVtable(entry);
		}
		g_programs.clear();
		g_hooks.clear();
		g_initialized = false;
		// Nothing else uses MinHook yet; whoever does next shares this.
		MH_Uninitialize();
	}
}
