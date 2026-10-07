/*
	Engine pointers ScriptHookRDR2's SDK doesn't expose, found by AOB scan
	(PatternScan.h). The signatures and offsets are HorseMenu's
	(D:\Backup\Stuff\RDR2 Shit\HorseMenu\src\game\pointers\Pointers.cpp),
	the same ones PokerCheat/BlackjackCheat use for ScriptThreads.

	Everything resolves together on first use. A failed scan is retried at
	most every 5 s rather than cached, so a scan that runs before RDR2.exe
	has finished unpacking can't leave the feature dead for the session.
	Call from the script thread only.
*/

#pragma once

#include "..\external\RDR-Classes\script\scrThread.hpp"
#include "..\external\RDR-Classes\script\scrProgram.hpp"
#include "..\external\RDR-Classes\rage\atArray.hpp"
#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <cstdint>

namespace GamePointers
{
	using ScriptVMFn = rage::eThreadState (*)(void* stack, std::int64_t** globals, bool* globalsEnabled, rage::scrProgram* program, rage::scrThreadContext* ctx);
	using GetNativeHandlerFn = rage::scrNativeHandler (*)(rage::scrNativeHash hash);
	using InitNativeTablesFn = bool (*)(rage::scrProgram* program);

	struct Pointers
	{
		rage::atArray<rage::scrThread*>* ScriptThreads = nullptr;
		rage::scrProgram** ScriptPrograms = nullptr; // 160 slots
		rage::scrThread** CurrentScriptThread = nullptr;
		ScriptVMFn ScriptVM = nullptr;
		std::int64_t** ScriptGlobals = nullptr;

		// Only NativeHooks uses these, so a miss leaves them nullptr
		// instead of failing everything else.
		GetNativeHandlerFn GetNativeHandler = nullptr;
		void* InitNativeTables = nullptr; // an InitNativeTablesFn, for MinHook
	};

	// All pointers, or nullptr if any signature didn't match (logged).
	const Pointers* Get();

	// The running thread / loaded program for a script name hash, or nullptr.
	rage::scrThread* FindScriptThread(rage::joaat_t scriptHash);
	rage::scrProgram* FindScriptProgram(rage::joaat_t scriptHash);

	// Address of script-local slot `index` (slots are 8 bytes), or nullptr
	// if the thread has no stack or the index is past its stack size.
	std::uint64_t* ScriptLocal(rage::scrThread* thread, std::uint32_t index);
}
