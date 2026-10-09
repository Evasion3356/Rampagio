/*
	ImGui overlay for desk tools the native menu can't carry (the Script
	Monitor first). Adapted from the user's HerbSpawner overlay
	(GoldenHorseCores\HerbSpawner\overlay): hooks Vulkan's present first and
	DX12's as the fallback, whichever the game actually presents with, plus
	the game window's WndProc and SetCursorPos/ClipCursor so the mouse is
	free while a tool is open.

	Nothing is hooked until the first tool opens (Start), so a user who never
	opens one never gets renderer hooks. While any tool is open the overlay
	takes all keyboard and mouse input; the menu key (MenuKey(), F5 by
	default) closes every tool and gives input back to the game and the
	native menu.

	Tools draw on the render thread. They must not call natives or touch
	game state there: read a snapshot the script thread made and send
	actions back through MainThread::Post.

	A passive tool only draws (the Teleport Map beside the native menu): it
	takes no input, the menu keeps running, and the close key leaves it.
*/

#pragma once

#include <atomic>

namespace Overlay
{
	struct Tool
	{
		const char* name;
		// Draws the tool's windows; set *open to false to close it.
		void (*draw)(bool* open);
		std::atomic<bool> open = false;
		bool passive = false; // draws only; set before Register
	};

	// Registers a tool (static storage). Script thread, before Start.
	void Register(Tool& tool);

	// Opens or closes a tool; opening one starts the overlay. Script thread.
	void SetOpen(Tool& tool, bool open);

	// Whether any interactive tool is open (input goes to the overlay).
	// Passive tools don't count.
	bool AnyOpen();

	// The key that closes every tool, read from the window thread.
	void SetCloseKey(int virtualKey);

	// "DX12", "Vulkan" or "None".
	const char* ActiveBackendName();

	// Removes every hook, waits for the render and window threads to leave
	// our code, then releases ImGui and the renderer objects. From DllMain,
	// before MH_Uninitialize.
	void Shutdown();
}
