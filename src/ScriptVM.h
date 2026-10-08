/*
	A detour on the game's script VM (GamePointers::ScriptVM), installed on
	first use, for two things:

	Bytecode patches, the way HorseMenu's ScriptPatches does them
	(HorseMenu\src\game\backend\ScriptPatches.cpp): a script with patches
	gets a private copy of its code pages, the patches go into the copy, and
	the VM runs the copy (m_CodeBlocks is swapped for the length of each VM
	call). The game's own pages are never written, so removing the last
	patch, or ejecting, leaves the script exactly as it was. Copies are kept
	until Shutdown even after their last patch goes, because the VM may be
	running one further up the stack (a native that runs our fiber); a
	script that reloads gets a fresh copy with the patches re-applied.

	Timing: the time each script thread spends in the VM, per frame
	(EndFrame, once a frame from the script loop) and averaged, for the
	Script Monitor.

	Add, Remove and EndFrame run on the script thread, which is the game
	thread the VM runs on.
*/

#pragma once

#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ScriptVM
{
	using PatchId = int;

	// Writes `bytes` at code offset `pc` of `script`'s private copy. 0 if
	// the detour couldn't be installed or the bytes would leave the code or
	// cross a 0x4000 code page (error says why).
	PatchId AddPatch(rage::joaat_t script, std::uint32_t pc, std::vector<std::uint8_t> bytes, std::string* error = nullptr);
	void RemovePatch(PatchId id);

	struct Timing
	{
		double lastFrameMs = 0; // VM time in the last full frame
		double averageMs = 0;   // moving average over about a second
		double peakMs = 0;      // the most in one frame since the thread was first seen
		std::uint32_t callsLastFrame = 0;
	};

	// Installs the detour so timing starts; true once it's in.
	bool EnableTiming();
	// Closes the frame's timing totals. Script thread, once a frame.
	void EndFrame();
	Timing GetTiming(std::uint32_t threadId);

	// Removes the detour and frees the copies. DllMain, before MH_Uninitialize.
	void Shutdown();
}
