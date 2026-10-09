#include "OnlineGuard.h"
#include "GamePointers.h"
#include "script.h"
#include "Log.h"
#include "debug/ScriptData.h"
#include "..\external\minhook\include\MinHook.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>

namespace
{
	enum class Reading : std::uint8_t
	{
		Unknown,
		Offline,
		Online,
	};

	const char* ReadingName(Reading r)
	{
		switch (r)
		{
		case Reading::Offline: return "offline";
		case Reading::Online: return "online";
		default: return "unknown";
		}
	}

	enum Signal
	{
		kNetMainThread, // 1
		kNetComponent,  // 2
		kPedNetObject,  // 3
		kPlayerMgr,     // 4
		kSessionFlag,   // 5
		kObjectMgr,     // 5
		kGlobalBlocks,  // 6
		kNetTraffic,    // 7
		kNativeHooks,   // 8
		kNativeCheck,   // 9
		kSignalCount,
	};

	constexpr const char* kSignalNames[kSignalCount] = {
		"net_main_online thread",
		"script net component",
		"local ped netObject",
		"CNetworkPlayerMgr",
		"session-started flag",
		"CNetworkObjectMgr",
		"MP global blocks",
		"network traffic",
		"native handler hooks",
		"native check",
	};

	// The memory signals: the native check is cross-checked against these.
	constexpr bool IsMemorySignal(int s)
	{
		return s != kNativeCheck && s != kNativeHooks;
	}

	// Global blocks (ScriptGlobals[0..63]) that only MP scripts allocate.
	// Empty until a live dump finds them: the startup and online-switch log
	// lines print the allocated blocks, so compare the two. While it's
	// empty, signal 6 sits out.
	constexpr std::uint64_t kOnlineBlockMask = 0;

	constexpr ULONGLONG kIntervalMs = 500;
	constexpr int kMaxLoggedThreads = 8;

	// The session natives a spoofing menu would hook (signal 8).
	constexpr std::uint64_t kSessionNatives[] = {
		0x1B89BC43B6E69107, // NETWORK_IS_SCRIPT_ACTIVE_BY_HASH
		0x9DE624D2FC4B603F, // NETWORK_IS_SESSION_STARTED
		0xCA97246103B63917, // NETWORK_IS_IN_SESSION
		0x10FAB35428CCC9D7, // NETWORK_IS_GAME_IN_PROGRESS
		0x8E34C953364A76DD, // GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH
	};

	// What one pass reads from engine memory. Plain data, so ReadRaw can
	// use SEH: a stale pointer makes the pass fault, not the game.
	struct Raw
	{
		bool threadsOk;
		bool netMainThread;
		int componentThreads;
		std::uint32_t componentHashes[kMaxLoggedThreads];

		bool pedOk;
		bool pedNetObject;

		bool playerMgrOk;
		bool playerMgrSession;
		void* playerMgr;
		void* localPlayer;
		std::uint16_t playerCount;

		bool sessionFlagOk;
		bool sessionStarted;

		bool objectMgrOk;
		void* objectMgr;

		bool globalsOk;
		std::uint64_t blockMask;

		bool handlersOk;
		int hookedHandler; // index into kSessionNatives, or -1
	};

	struct ImageRange
	{
		std::uintptr_t begin = 0;
		std::uintptr_t end = 0;

		bool Contains(std::uintptr_t a) const
		{
			return a >= begin && a < end;
		}
	};

	ImageRange MainImage()
	{
		ImageRange range;
		auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		range.begin = base;
		range.end = base + nt->OptionalHeader.SizeOfImage;
		return range;
	}

	// The target of a jump at `code`, or 0 if it doesn't start with one.
	// Covers the trampolines detour libraries write: jmp rel32/rel8,
	// jmp [rip+disp32], mov reg, imm64; jmp reg, and push imm32; ret.
	std::uintptr_t JumpTarget(std::uintptr_t code)
	{
		auto b = reinterpret_cast<const std::uint8_t*>(code);
		if (b[0] == 0xE9)
			return code + 5 + *reinterpret_cast<const std::int32_t*>(b + 1);
		if (b[0] == 0xEB)
			return code + 2 + static_cast<std::int8_t>(b[1]);
		if (b[0] == 0xFF && b[1] == 0x25)
			return *reinterpret_cast<const std::uintptr_t*>(code + 6 + *reinterpret_cast<const std::int32_t*>(b + 2));
		if ((b[0] == 0x48 || b[0] == 0x49) && b[1] >= 0xB8 && b[1] <= 0xBF)
		{
			// jmp reg is FF E0+r, with a 41 prefix for r8-r15.
			const std::uint8_t* j = b + 10;
			if (j[0] == 0x41)
				j++;
			if (j[0] == 0xFF && j[1] >= 0xE0 && j[1] <= 0xE7)
				return *reinterpret_cast<const std::uintptr_t*>(b + 2);
			return 0;
		}
		if (b[0] == 0x68)
		{
			std::uintptr_t target = *reinterpret_cast<const std::uint32_t*>(b + 1);
			if (b[5] == 0xC3)
				return target;
			// push lo; mov dword [rsp+4], hi; ret
			if (b[5] == 0xC7 && b[6] == 0x44 && b[7] == 0x24 && b[8] == 0x04 && b[13] == 0xC3)
				return target | (static_cast<std::uintptr_t>(*reinterpret_cast<const std::uint32_t*>(b + 9)) << 32);
		}
		return 0;
	}

	// A handler is hooked if it, or a jump it starts with, leads outside
	// RDR2.exe, or it starts with a breakpoint (a VEH hook). Jumps inside
	// the image are followed, since the game has its own thunks.
	bool IsHooked(std::uintptr_t handler, const ImageRange& image)
	{
		for (int hop = 0; hop < 4; hop++)
		{
			if (!image.Contains(handler))
				return true;
			if (*reinterpret_cast<const std::uint8_t*>(handler) == 0xCC)
				return true;
			const std::uintptr_t target = JumpTarget(handler);
			if (!target)
				return false;
			handler = target;
		}
		return !image.Contains(handler);
	}

	void ReadThreads(const GamePointers::Pointers* p, Raw& r)
	{
		const rage::joaat_t netMainOnline = rage::Joaat("net_main_online");
		for (auto thread : *p->ScriptThreads)
		{
			if (!thread || !thread->m_Context.m_ThreadId)
				continue;
			if (thread->m_Context.m_ScriptHash == netMainOnline)
				r.netMainThread = true;
			if (thread->m_HandlerNetComponent)
			{
				if (r.componentThreads < kMaxLoggedThreads)
					r.componentHashes[r.componentThreads] = thread->m_Context.m_ScriptHash;
				r.componentThreads++;
			}
		}
		r.threadsOk = true;
	}

	void ReadPed(const GamePointers::Pointers* p, Raw& r)
	{
		if (!p->GetLocalPed)
			return;
		auto ped = reinterpret_cast<std::uintptr_t>(p->GetLocalPed());
		if (!ped)
			return; // loading: no ped yet, so the signal sits out
		// CDynamicEntity::m_NetObject
		r.pedNetObject = *reinterpret_cast<void**>(ped + 0xE0) != nullptr;
		r.pedOk = true;
	}

	void ReadManagers(const GamePointers::Pointers* p, Raw& r)
	{
		if (p->NetworkPlayerMgr)
		{
			r.playerMgr = *p->NetworkPlayerMgr;
			if (r.playerMgr)
			{
				auto mgr = reinterpret_cast<std::uintptr_t>(r.playerMgr);
				// netPlayerMgrBase::m_LocalPlayer, m_PlayerCount
				r.localPlayer = *reinterpret_cast<void**>(mgr + 0xE8);
				r.playerCount = *reinterpret_cast<std::uint16_t*>(mgr + 0x28C);
			}
			// Not "count > 1": solo and invite-only sessions are online too.
			r.playerMgrSession = r.localPlayer && r.playerCount >= 1;
			r.playerMgrOk = true;
		}
		if (p->IsSessionStarted)
		{
			r.sessionStarted = *p->IsSessionStarted;
			r.sessionFlagOk = true;
		}
		if (p->NetworkObjectMgr)
		{
			r.objectMgr = *p->NetworkObjectMgr;
			r.objectMgrOk = true;
		}
		if (p->ScriptGlobals)
		{
			for (int i = 0; i < 64; i++)
			{
				if (p->ScriptGlobals[i])
					r.blockMask |= 1ull << i;
			}
			r.globalsOk = true;
		}
	}

	void ReadHandlers(const GamePointers::Pointers* p, const ImageRange& image, Raw& r)
	{
		if (!p->GetNativeHandler)
			return;
		r.hookedHandler = -1;
		for (int i = 0; i < static_cast<int>(std::size(kSessionNatives)); i++)
		{
			auto handler = reinterpret_cast<std::uintptr_t>(p->GetNativeHandler(static_cast<rage::scrNativeHash>(kSessionNatives[i])));
			// A missing handler is as good as a hooked one: the native can't
			// be trusted either way.
			if (!handler || IsHooked(handler, image))
			{
				r.hookedHandler = i;
				break;
			}
		}
		r.handlersOk = true;
	}

	// Each part is guarded on its own, so one faulting read only leaves its
	// own signals unknown. No C++ objects with destructors in here (SEH).
	void ReadRaw(const GamePointers::Pointers* p, const ImageRange& image, Raw& r)
	{
		__try { ReadThreads(p, r); }
		__except (EXCEPTION_EXECUTE_HANDLER) { r.threadsOk = false; }
		__try { ReadPed(p, r); }
		__except (EXCEPTION_EXECUTE_HANDLER) { r.pedOk = false; }
		__try { ReadManagers(p, r); }
		__except (EXCEPTION_EXECUTE_HANDLER) { r.playerMgrOk = r.sessionFlagOk = r.objectMgrOk = r.globalsOk = false; }
		__try { ReadHandlers(p, image, r); }
		__except (EXCEPTION_EXECUTE_HANDLER) { r.handlersOk = true; r.hookedHandler = 0; }
	}

	// Signal 7: set from the network thread.
	using ReceiveNetMessageFn = bool (*)(void*, void*, void*);
	ReceiveNetMessageFn g_receiveOriginal = nullptr;
	void* g_receiveTarget = nullptr;
	std::atomic<bool> g_sawNetTraffic{ false };
	std::atomic<std::uint32_t> g_netMessages{ 0 };

	// Kept trivial on purpose: counts, latches and passes everything on.
	bool ReceiveNetMessageDetour(void* a1, void* connectionMgr, void* frame)
	{
		g_netMessages.fetch_add(1, std::memory_order_relaxed);
		g_sawNetTraffic.store(true, std::memory_order_relaxed);
		OnlineGuard::detail::g_latched.store(true, std::memory_order_relaxed);
		return g_receiveOriginal(a1, connectionMgr, frame);
	}

	void InstallNetHook(const GamePointers::Pointers* p)
	{
		static bool tried = false;
		if (tried || !p->ReceiveNetMessage)
			return;
		tried = true;

		const MH_STATUS init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
		{
			Log::Write("OnlineGuard: MH_Initialize failed ({})", MH_StatusToString(init));
			return;
		}
		MH_STATUS status = MH_CreateHook(p->ReceiveNetMessage, reinterpret_cast<void*>(&ReceiveNetMessageDetour),
			reinterpret_cast<void**>(&g_receiveOriginal));
		if (status == MH_OK)
			status = MH_EnableHook(p->ReceiveNetMessage);
		if (status != MH_OK)
		{
			Log::Write("OnlineGuard: ReceiveNetMessage hook failed ({})", MH_StatusToString(status));
			MH_RemoveHook(p->ReceiveNetMessage);
			return;
		}
		g_receiveTarget = p->ReceiveNetMessage;
	}

	std::string ScriptLabel(std::uint32_t hash)
	{
		if (const char* name = ScriptData::FindScriptName(hash))
			return name;
		return std::format("{:#010x}", hash);
	}

	Reading FromBool(bool known, bool online)
	{
		if (!known)
			return Reading::Unknown;
		return online ? Reading::Online : Reading::Offline;
	}

	using Readings = std::array<Reading, kSignalCount>;

	Readings Evaluate(const Raw& r, bool nativeOnline)
	{
		Readings s;
		s[kNetMainThread] = FromBool(r.threadsOk, r.netMainThread);
		s[kNetComponent] = FromBool(r.threadsOk, r.componentThreads > 0);
		s[kPedNetObject] = FromBool(r.pedOk, r.pedNetObject);
		s[kPlayerMgr] = FromBool(r.playerMgrOk, r.playerMgrSession);
		s[kSessionFlag] = FromBool(r.sessionFlagOk, r.sessionStarted);
		s[kObjectMgr] = FromBool(r.objectMgrOk, r.objectMgr != nullptr);
		s[kGlobalBlocks] = FromBool(kOnlineBlockMask != 0 && r.globalsOk, (r.blockMask & kOnlineBlockMask) != 0);
		s[kNetTraffic] = FromBool(g_receiveTarget != nullptr, g_sawNetTraffic.load(std::memory_order_relaxed));
		s[kNativeHooks] = FromBool(r.handlersOk, r.hookedHandler >= 0);
		s[kNativeCheck] = FromBool(true, nativeOnline);
		return s;
	}

	// The raw values behind the readings, for the verification log.
	void LogDetails(const Raw& r)
	{
		std::string threads;
		for (int i = 0; i < r.componentThreads && i < kMaxLoggedThreads; i++)
			threads += (i ? ", " : "") + ScriptLabel(r.componentHashes[i]);
		if (r.componentThreads > kMaxLoggedThreads)
			threads += std::format(", +{} more", r.componentThreads - kMaxLoggedThreads);
		Log::Write("OnlineGuard: threads with a net component: {} ({})", r.componentThreads, threads.empty() ? "none" : threads);
		Log::Write("OnlineGuard: CNetworkPlayerMgr {} (local player {}, count {}), CNetworkObjectMgr {}",
			r.playerMgr, r.localPlayer, r.playerCount, r.objectMgr);
		Log::Write("OnlineGuard: allocated global blocks {:#018x}; ReceiveNetMessage calls so far {}",
			r.blockMask, g_netMessages.load(std::memory_order_relaxed));
		if (r.handlersOk && r.hookedHandler >= 0)
			Log::Write("OnlineGuard: native {:#018x} is hooked or missing", kSessionNatives[r.hookedHandler]);
	}

	void LogReadings(const Readings& s)
	{
		std::string line;
		for (int i = 0; i < kSignalCount; i++)
			line += std::format("{}{}: {}", i ? ", " : "", kSignalNames[i], ReadingName(s[i]));
		Log::Write("OnlineGuard: {}", line);
	}

	Readings g_last;
	bool g_haveLast = false;
	ULONGLONG g_nextEvalMs = 0;
	ULONGLONG g_netSeenLoggedAt = 0;
	bool g_reportedSwitch = false;
	bool g_reportedSpoof = false;
	bool g_loggedUnresolved = false;

	void LogUnresolvedOnce(const GamePointers::Pointers* p)
	{
		static bool loggedMissing = false;
		if (!p)
		{
			if (!loggedMissing)
				Log::Write("OnlineGuard: engine pointers not resolved; only the native check decides until they are");
			loggedMissing = true; // GamePointers retries every 5 s
			return;
		}
		if (g_loggedUnresolved)
			return;
		g_loggedUnresolved = true;
		const struct { const void* ptr; const char* signal; } optional[] = {
			{ reinterpret_cast<const void*>(p->GetLocalPed), kSignalNames[kPedNetObject] },
			{ p->NetworkPlayerMgr, kSignalNames[kPlayerMgr] },
			{ p->IsSessionStarted, kSignalNames[kSessionFlag] },
			{ p->NetworkObjectMgr, kSignalNames[kObjectMgr] },
			{ p->ReceiveNetMessage, kSignalNames[kNetTraffic] },
			{ reinterpret_cast<const void*>(p->GetNativeHandler), kSignalNames[kNativeHooks] },
		};
		for (const auto& o : optional)
		{
			if (!o.ptr)
				Log::Write("OnlineGuard: signal \"{}\" sits out (pattern not found)", o.signal);
		}
		if (kOnlineBlockMask == 0)
			Log::Write("OnlineGuard: signal \"{}\" sits out (no online-only blocks known yet)", kSignalNames[kGlobalBlocks]);
	}

	// One pass over every signal.
	void RunPass()
	{
		static const ImageRange image = MainImage();
		const GamePointers::Pointers* p = GamePointers::Get();
		LogUnresolvedOnce(p);

		Raw raw{};
		raw.hookedHandler = -1;
		if (p)
		{
			InstallNetHook(p);
			ReadRaw(p, image, raw);
		}
		const bool nativeOnline = NETWORK::NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(rage::Joaat("net_main_online"), -1, FALSE, 0) != FALSE;
		const Readings s = Evaluate(raw, nativeOnline);

		// Every change is logged: in story mode nothing should ever change.
		if (!g_haveLast)
		{
			LogReadings(s);
			LogDetails(raw);
		}
		else
		{
			for (int i = 0; i < kSignalCount; i++)
			{
				if (s[i] != g_last[i])
					Log::Write("OnlineGuard: {} {} -> {}", kSignalNames[i], ReadingName(g_last[i]), ReadingName(s[i]));
			}
		}
		g_last = s;
		g_haveLast = true;

		bool online = false;
		bool memoryOnline = false;
		for (int i = 0; i < kSignalCount; i++)
		{
			if (s[i] != Reading::Online)
				continue;
			online = true;
			if (IsMemorySignal(i))
				memoryOnline = true;
		}

		// Signal 9: the native says offline while memory says online.
		if (memoryOnline && !nativeOnline && !g_reportedSpoof)
		{
			g_reportedSpoof = true;
			Log::Write("OnlineGuard: the native check says offline while engine memory says online: it's being spoofed");
		}
		if (s[kNativeHooks] == Reading::Online && !g_reportedSpoof)
		{
			g_reportedSpoof = true;
			Log::Write("OnlineGuard: a session native is hooked, so the session is treated as online");
		}

		if (online)
			OnlineGuard::detail::g_latched.store(true, std::memory_order_relaxed);

		if (OnlineGuard::Latched() && !g_reportedSwitch)
		{
			g_reportedSwitch = true;
			std::string fired;
			for (int i = 0; i < kSignalCount; i++)
			{
				if (s[i] == Reading::Online)
					fired += std::format("{}{}", fired.empty() ? "" : ", ", kSignalNames[i]);
			}
			Log::Write("OnlineGuard: online (latched for the session). Signals: {}", fired.empty() ? "none yet" : fired);
			LogDetails(raw);
		}
	}
}

namespace OnlineGuard
{
	void Tick()
	{
		const ULONGLONG nowMs = GetTickCount64();
		// The network latch doesn't wait for the 500 ms pass, and after the
		// switch the log keeps recording how the other signals follow.
		const bool netNews = g_sawNetTraffic.load(std::memory_order_relaxed) && g_netSeenLoggedAt == 0;
		if (netNews)
			g_netSeenLoggedAt = nowMs;
		if (netNews || nowMs >= g_nextEvalMs)
		{
			g_nextEvalMs = nowMs + kIntervalMs;
			RunPass();
		}
	}

	void Shutdown()
	{
		if (!g_receiveTarget)
			return;
		MH_DisableHook(g_receiveTarget);
		MH_RemoveHook(g_receiveTarget);
		g_receiveTarget = nullptr;
	}
}
