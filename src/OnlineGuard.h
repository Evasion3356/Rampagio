/*
	Online detection that a menu can't defeat by hooking natives
	(docs/ONLINE_DETECTION_PLAN.md). Reads engine memory directly:

	1. the net_main_online thread, found by walking the script threads
	2. a network component on that thread, or on any running thread
	3. the local ped's netObject
	4. CNetworkPlayerMgr's local player and player count
	5. the session-started flag and the CNetworkObjectMgr instance
	6. script global blocks only MP scripts allocate (to research: the
	   block list is empty until a live dump finds them; see the .cpp)
	7. a latch set by a ReceiveNetMessage detour
	8. the session natives' handlers, checked for hooks
	9. the old native check, cross-checked against the memory signals

	Any signal reading online counts as online, and the result latches for
	the rest of the session. A signal whose pointer didn't resolve sits
	out; if every memory signal sits out, the native check decides alone.
	Every signal change is logged, which is the live-verification data.

	Tick from the script thread; Latched() can be read from anywhere.
*/

#pragma once

#include <atomic>

namespace OnlineGuard
{
	namespace detail
	{
		inline std::atomic<bool> g_latched{ false };
	}

	// Evaluates the signals every 500 ms (the network latch at once). It
	// returns nothing on purpose: callers read Latched(), so patching Tick
	// to report "offline" isn't possible, only skipping it, and the
	// ReceiveNetMessage detour still sets the latch then.
	void Tick();

	// The latched state. Force-inlined (in Release; Debug inlines nothing)
	// so each caller reads the atomic itself: a menu would have to patch
	// every read site instead of one function. Clearing the atomic doesn't
	// stick either, since every pass sets it again while a signal is online.
	__forceinline bool Latched()
	{
		return detail::g_latched.load(std::memory_order_relaxed);
	}

	// Removes the ReceiveNetMessage detour (DllMain detach, before
	// MH_Uninitialize).
	void Shutdown();
}
