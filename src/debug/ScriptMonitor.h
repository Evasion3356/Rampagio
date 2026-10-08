/*
	Debug > Script Monitor: an ImGui window (src/overlay) listing the game's
	script threads with HorseMenu's thread details (its Debug > Scripts tab,
	HorseMenu\src\game\frontend\submenus\Debug\Scripts.cpp) plus VM time,
	the script's functions as the decompiled scripts number them, and
	function and native hooks (ScriptHooks.h). Starting a script works like
	HorseMenu's "New" group.

	Not translated, on purpose: it's a tool for people reading the
	decompiled scripts.

	Threads are read on the script thread into a snapshot each frame while
	the window is open; the window (render thread) only draws the snapshot
	and posts its actions back with MainThread::Post.
*/

#pragma once

namespace ScriptMonitor
{
	void Register();
	void SetOpen(bool open);
	bool IsOpen();
	// Every frame from the script loop.
	void Tick();
	// Online kill switch and eject: closes the window, resumes the threads
	// it paused and removes every hook.
	void Suspend();
}
