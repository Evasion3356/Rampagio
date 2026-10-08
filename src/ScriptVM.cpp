#include "ScriptVM.h"
#include "GamePointers.h"
#include "Log.h"

#include "..\external\minhook\include\MinHook.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
	constexpr std::uint32_t kPageSize = 0x4000;

	struct Patch
	{
		ScriptVM::PatchId id;
		rage::joaat_t script;
		std::uint32_t pc;
		std::vector<std::uint8_t> bytes;
	};

	// A private copy of one script's code pages.
	struct Shadow
	{
		rage::scrProgram* program = nullptr; // the program it was copied from
		std::uint32_t codeSize = 0;
		std::vector<std::unique_ptr<std::uint8_t[]>> pages;
		std::vector<std::uint8_t*> table; // what m_CodeBlocks points at while the copy runs
		std::vector<std::uint8_t> original; // the game's bytes, in one piece, for restoring
		int patchCount = 0;

		std::uint8_t* At(std::uint32_t pc) { return pages[pc / kPageSize].get() + pc % kPageSize; }
	};

	struct ThreadTiming
	{
		std::int64_t frameTicks = 0;
		std::uint32_t frameCalls = 0;
		std::uint32_t idleFrames = 0;
		ScriptVM::Timing timing;
	};

	// The VM runs on the game thread, as do Add/Remove/EndFrame (the script
	// fiber); the lock only covers the rare off-thread VM call.
	std::mutex g_mutex;
	bool g_hooked = false;
	GamePointers::ScriptVMFn g_original = nullptr;
	void* g_target = nullptr;
	std::vector<Patch> g_patches;
	std::unordered_map<rage::joaat_t, Shadow> g_shadows;
	std::unordered_map<std::uint32_t, ThreadTiming> g_timings;
	ScriptVM::PatchId g_nextId = 1;
	double g_ticksToMs = 0;

	void Copy(Shadow& shadow, rage::scrProgram* program)
	{
		shadow.program = program;
		shadow.codeSize = program->m_CodeSize;
		shadow.pages.clear();
		shadow.table.clear();
		shadow.original.resize(program->m_CodeSize);
		for (std::uint32_t page = 0; page < program->GetNumCodePages(); page++)
		{
			const std::uint32_t size = page + 1 < program->GetNumCodePages() ? kPageSize : program->m_CodeSize - page * kPageSize;
			auto copy = std::make_unique<std::uint8_t[]>(kPageSize);
			std::memcpy(copy.get(), program->GetCodePage(page), size);
			std::memcpy(shadow.original.data() + page * kPageSize, program->GetCodePage(page), size);
			shadow.table.push_back(copy.get());
			shadow.pages.push_back(std::move(copy));
		}
	}

	void Write(Shadow& shadow, const Patch& patch, bool apply)
	{
		const std::uint8_t* source = apply ? patch.bytes.data() : shadow.original.data() + patch.pc;
		std::memcpy(shadow.At(patch.pc), source, patch.bytes.size());
	}

	// Copies the program's code if the copy is missing or from an older load
	// of the script, and applies the script's patches to a fresh copy.
	Shadow* Prepare(rage::scrProgram* program)
	{
		auto it = g_shadows.find(program->m_NameHash);
		if (it == g_shadows.end() || it->second.patchCount == 0)
			return nullptr;
		Shadow& shadow = it->second;
		if (shadow.program != program || shadow.codeSize != program->m_CodeSize)
		{
			Copy(shadow, program);
			for (const Patch& patch : g_patches)
			{
				if (patch.script == program->m_NameHash && patch.pc + patch.bytes.size() <= shadow.codeSize)
					Write(shadow, patch, true);
			}
		}
		return &shadow;
	}

	rage::eThreadState Detour(void* stack, std::int64_t** globals, bool* globalsEnabled, rage::scrProgram* program, rage::scrThreadContext* ctx)
	{
		std::uint8_t** codeBlocks = nullptr;
		{
			std::lock_guard lock(g_mutex);
			if (Shadow* shadow = Prepare(program))
			{
				codeBlocks = program->m_CodeBlocks;
				program->m_CodeBlocks = shadow->table.data();
			}
		}

		LARGE_INTEGER before, after;
		QueryPerformanceCounter(&before);
		const rage::eThreadState state = g_original(stack, globals, globalsEnabled, program, ctx);
		QueryPerformanceCounter(&after);

		std::lock_guard lock(g_mutex);
		if (codeBlocks)
			program->m_CodeBlocks = codeBlocks;
		if (ctx)
		{
			ThreadTiming& timing = g_timings[ctx->m_ThreadId];
			timing.frameTicks += after.QuadPart - before.QuadPart;
			timing.frameCalls++;
		}
		return state;
	}

	bool EnsureHooked()
	{
		if (g_hooked)
			return true;
		const GamePointers::Pointers* p = GamePointers::Get();
		if (!p || !p->ScriptVM)
			return false;

		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		g_ticksToMs = 1000.0 / static_cast<double>(frequency.QuadPart);

		const MH_STATUS init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
		{
			Log::Write("ScriptVM: MH_Initialize failed ({})", MH_StatusToString(init));
			return false;
		}
		g_target = reinterpret_cast<void*>(p->ScriptVM);
		MH_STATUS status = MH_CreateHook(g_target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_original));
		if (status == MH_OK)
			status = MH_EnableHook(g_target);
		if (status != MH_OK)
		{
			Log::Write("ScriptVM: hook failed ({})", MH_StatusToString(status));
			MH_RemoveHook(g_target);
			return false;
		}
		g_hooked = true;
		Log::Write("ScriptVM: hooked");
		return true;
	}
}

namespace ScriptVM
{
	PatchId AddPatch(rage::joaat_t script, std::uint32_t pc, std::vector<std::uint8_t> bytes, std::string* error)
	{
		auto fail = [error](const char* why) {
			if (error)
				*error = why;
			return 0;
		};
		if (bytes.empty())
			return fail("Nothing to write.");
		if (pc / kPageSize != (pc + bytes.size() - 1) / kPageSize)
			return fail("The patch would cross a code page boundary.");
		if (!EnsureHooked())
			return fail("The script VM hook couldn't be installed (see Rampagio.log).");
		rage::scrProgram* program = GamePointers::FindScriptProgram(script);
		if (!program)
			return fail("The script isn't loaded.");
		if (pc + bytes.size() > program->m_CodeSize)
			return fail("The patch would run past the end of the code.");

		std::lock_guard lock(g_mutex);
		Shadow& shadow = g_shadows[script];
		if (shadow.program != program || shadow.codeSize != program->m_CodeSize)
		{
			Copy(shadow, program);
			for (const Patch& patch : g_patches)
			{
				if (patch.script == script)
					Write(shadow, patch, true);
			}
		}
		const Patch patch = { g_nextId++, script, pc, std::move(bytes) };
		Write(shadow, patch, true);
		shadow.patchCount++;
		g_patches.push_back(patch);
		return patch.id;
	}

	void RemovePatch(PatchId id)
	{
		std::lock_guard lock(g_mutex);
		auto it = std::find_if(g_patches.begin(), g_patches.end(), [id](const Patch& patch) { return patch.id == id; });
		if (it == g_patches.end())
			return;
		const Patch patch = *it;
		g_patches.erase(it);
		auto shadow = g_shadows.find(patch.script);
		if (shadow == g_shadows.end())
			return;
		if (patch.pc + patch.bytes.size() <= shadow->second.codeSize)
			Write(shadow->second, patch, false);
		// Patches may overlap: put back the ones still there.
		for (const Patch& other : g_patches)
		{
			if (other.script == patch.script && other.pc + other.bytes.size() <= shadow->second.codeSize)
				Write(shadow->second, other, true);
		}
		shadow->second.patchCount--;
	}

	bool EnableTiming()
	{
		return EnsureHooked();
	}

	void EndFrame()
	{
		if (!g_hooked)
			return;
		std::lock_guard lock(g_mutex);
		// Threads that stopped running (ids aren't reused) drop out after ~10 s.
		std::erase_if(g_timings, [](const auto& item) { return item.second.idleFrames > 600; });
		for (auto& [id, entry] : g_timings)
		{
			entry.idleFrames = entry.frameCalls ? 0 : entry.idleFrames + 1;
			const double ms = static_cast<double>(entry.frameTicks) * g_ticksToMs;
			entry.timing.lastFrameMs = ms;
			entry.timing.callsLastFrame = entry.frameCalls;
			entry.timing.averageMs += (ms - entry.timing.averageMs) * 0.05; // about 60 frames
			entry.timing.peakMs = (std::max)(entry.timing.peakMs, ms);
			entry.frameTicks = 0;
			entry.frameCalls = 0;
		}
	}

	Timing GetTiming(std::uint32_t threadId)
	{
		std::lock_guard lock(g_mutex);
		auto it = g_timings.find(threadId);
		return it != g_timings.end() ? it->second.timing : Timing{};
	}

	void Shutdown()
	{
		if (!g_hooked)
			return;
		MH_DisableHook(g_target);
		MH_RemoveHook(g_target);
		g_hooked = false;
		std::lock_guard lock(g_mutex);
		g_patches.clear();
		g_shadows.clear();
		g_timings.clear();
	}
}
