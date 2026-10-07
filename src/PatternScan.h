/*
	Minimal AOB (array-of-bytes) pattern scanner for locating positions in
	RDR2.exe's loaded image by byte signature, since ScriptHookRDR2's SDK
	doesn't expose the internal engine pointers this mod needs (the live
	scrThread pool -- see GamePointers.h). Same general technique
	CollectorOffline's RpfMounter.cpp/ChangeSetActivator.cpp already use
	(their own scans are inline, not shared, hence this standalone version).

	Pattern syntax: space-separated hex bytes, "?" for a wildcard byte, e.g.
	"48 8D 0D ? ? ? ? E8 ? ? ? ? EB 0B 8B 0D" (this is HorseMenu's own
	"ScriptThreads&RunScriptThreads" signature -- see
	D:\Backup\Stuff\RDR2 Shit\HorseMenu\src\game\pointers\Pointers.cpp).
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace PatternScan
{
	// Scans the main module's (RDR2.exe's) full mapped image for the given
	// pattern. Returns the address of the first match, or nullopt.
	// startAddress (optional) skips every match that begins before it --
	// pass a previous match + 1 to look for a second one.
	std::optional<std::uintptr_t> FindInMainModule(std::string_view pattern, std::uintptr_t startAddress = 0);

	// Resolves a 4-byte RIP-relative operand at (matchAddress + operandOffset)
	// to the absolute address it targets -- i.e. matchAddress + operandOffset
	// + 4 (operand size) + the signed 32-bit displacement stored there. This
	// is the standard x64 `LEA reg, [rip+disp32]` / `CALL [rip+disp32]`
	// encoding used throughout RDR2.exe (and exactly how HorseMenu's
	// PointerCalculator::Rip() works).
	std::uintptr_t ResolveRip(std::uintptr_t matchAddress, int operandOffset);
}
