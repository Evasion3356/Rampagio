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

	Hardening: no single patch should turn the guard off. There are no
	self-checks, just no single point of failure:
	- IsOnline() is force-inlined, so every read site has its own copy of
	  the check; there's no one function to patch to "xor eax, eax; ret".
	- The latch isn't a bool. It's three copies of a per-run random token,
	  offline only while every copy still holds its expected value, so
	  writing 0 or false anywhere reads as online.
	- Each read site also checks two signals itself (session flag, object
	  manager) through addresses stored encoded with the same token, so
	  skipping Tick doesn't leave the read sites blind.
	- The pass exists as two separate copies of the code (Tick, TickAlt,
	  called from different places), and each signal reader latches on its
	  own, so patching the pass or one reader leaves the rest working.
	- script.cpp enforces the kill switch every frame while online, not
	  once on the transition.
	Release only: Debug builds don't inline anything.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <intrin.h>

namespace OnlineGuard
{
	namespace detail
	{
		constexpr int kStateCopies = 3;
		constexpr int kQuickSlots = 2; // IsSessionStarted, NetworkObjectMgr

		inline std::atomic<std::uint64_t> g_key{ 0 };
		inline std::atomic<std::uint64_t> g_state[kStateCopies];
		inline std::atomic<std::uint64_t> g_quick[kQuickSlots];

		// What state copy i holds while offline; for i >= kStateCopies, the
		// key quick slot i - kStateCopies is XORed with. The key is drawn at
		// load (OnlineGuard.cpp) so none of these is 0, and a wiped key gives
		// the salts, which zeroed storage doesn't match either.
		__forceinline std::uint64_t Expected(int i)
		{
			constexpr std::uint64_t kSalt[kStateCopies + kQuickSlots] = {
				0x9E3779B97F4A7C15, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
				0xD6E8FEB86659FD93, 0xA0761D6478BD642F,
			};
			return _rotl64(g_key.load(std::memory_order_relaxed), 7 + 11 * i) ^ kSalt[i];
		}

		// Marks every copy online. The order only differs so the two pass
		// copies don't compile to identical (linker-foldable) code.
		template <int Order = 0>
		__forceinline void Latch()
		{
			for (int n = 0; n < kStateCopies; n++)
			{
				const int i = Order ? kStateCopies - 1 - n : n;
				g_state[i].store(~Expected(i), std::memory_order_relaxed);
			}
		}

		// The address in quick slot j: 0 while unresolved. Anything else
		// outside RDR2.exe means the slot was overwritten.
		__forceinline std::uintptr_t QuickAddress(int j)
		{
			return static_cast<std::uintptr_t>(g_quick[j].load(std::memory_order_relaxed) ^ Expected(kStateCopies + j));
		}

		__forceinline bool QuickSignals()
		{
			// RDR2.exe's range from the PEB (ImageBaseAddress) and its PE
			// header (e_lfanew, OptionalHeader.SizeOfImage): nothing of ours
			// to overwrite.
			const std::uintptr_t base = *reinterpret_cast<const std::uintptr_t*>(__readgsqword(0x60) + 0x10);
			const std::uintptr_t size = *reinterpret_cast<const std::uint32_t*>(base + *reinterpret_cast<const std::int32_t*>(base + 0x3C) + 0x50);

			const std::uintptr_t sessionStarted = QuickAddress(0);
			if (sessionStarted && (sessionStarted - base >= size || *reinterpret_cast<const volatile bool*>(sessionStarted)))
				return true;
			const std::uintptr_t objectMgr = QuickAddress(1);
			if (objectMgr && (objectMgr - base >= size || *reinterpret_cast<void* const volatile*>(objectMgr)))
				return true;
			return false;
		}
	}

	// True once Red Dead Online has been detected this session. A few loads,
	// so read it wherever a path should stop rather than caching it.
	__forceinline bool IsOnline()
	{
		using namespace detail;
		bool online = false;
		for (int i = 0; i < kStateCopies; i++)
			online |= g_state[i].load(std::memory_order_relaxed) != Expected(i);
		if (!online && QuickSignals())
		{
			Latch();
			online = true;
		}
		return online;
	}

	// One copy of the signal pass each, at most every 500 ms (the network
	// latch at once). Tick logs; TickAlt is the silent second copy. Neither
	// returns anything: callers read IsOnline().
	void Tick();
	void TickAlt();

	// Removes the ReceiveNetMessage detour (DllMain detach, before
	// MH_Uninitialize).
	void Shutdown();
}
