/*
	Replaces natives for game scripts, adapted from HorseMenu's NativeHooks
	(D:\Backup\Stuff\RDR2 Shit\HorseMenu\src\game\backend\NativeHooks.cpp).

	Each loaded scrProgram has its own table of native handlers
	(m_NativeEntrypoints). A hook swaps the native's entry in that table,
	for one script or for all of them, so only game scripts see the
	replacement: our own calls through ScriptHookRDR2 still reach the real
	native. (Rampage instead detours the handler function itself, which
	also catches its own calls.) Programs that load later get the hooks
	from a MinHook detour on the game's InitNativeTables, and a vtable swap
	on each program's destructor drops it from the registry when it
	unloads.

	Unlike HorseMenu, hooks can be removed again, so a toggle can add its
	hook when switched on and remove it when switched off. Several hooks on
	the same native stack; the newest one wins.

	A replacement gets the script's call context: read and change
	arguments with GetArg/SetArg, then call Original(hash)(ctx) to run the
	real native, or set the return value itself.

	Call Add/Remove from the script thread. Shutdown restores every table
	and is called on unload.
*/

#pragma once

#include "..\external\RDR-Classes\script\scrNativeHandler.hpp"
#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <cstdint>

namespace NativeHooks
{
	using Id = int;

	// Pass as `script` to hook the native in every script.
	constexpr rage::joaat_t kAllScripts = 0;

	// Hooks `nativeHash` (alloc8or's 64-bit hash) in `script`'s native
	// table. Returns 0 if it couldn't (logged): the pointers didn't
	// resolve or the game has no such native.
	Id Add(rage::joaat_t script, std::uint64_t nativeHash, rage::scrNativeHandler replacement);

	// Removes a hook from Add; 0 is ignored.
	void Remove(Id id);

	// The game's own handler for a native, or nullptr.
	rage::scrNativeHandler Original(std::uint64_t nativeHash);

	// Restores every native table and removes the InitNativeTables detour.
	void Shutdown();
}
