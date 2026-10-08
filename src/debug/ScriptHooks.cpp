#include "ScriptHooks.h"
#include "..\ScriptVM.h"
#include "..\NativeHooks.h"
#include "..\GamePointers.h"
#include "..\Log.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <utility>

namespace
{
	using namespace ScriptHooks;

	// Strings a hooked function returns live below 2 GB: PUSH_CONST_U32 is
	// the widest constant the VM has. Never freed, not even on eject, since
	// a script may still hold the pointer.
	const char* LowString(const std::string& text)
	{
		constexpr std::size_t kRegionSize = 0x10000;
		static char* region = nullptr;
		static std::size_t used = 0;
		if (!region)
		{
			for (std::uintptr_t address = 0x10000000; address < 0x7FFF0000 && !region; address += kRegionSize)
				region = static_cast<char*>(VirtualAlloc(reinterpret_cast<void*>(address), kRegionSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			if (!region)
				return nullptr;
		}
		if (used + text.size() + 1 > kRegionSize)
			return nullptr;
		char* copy = region + used;
		std::memcpy(copy, text.c_str(), text.size() + 1);
		used += text.size() + 1;
		return copy;
	}

	struct FunctionHookEntry
	{
		FunctionHook hook;
		ScriptVM::PatchId patch;
	};

	std::vector<FunctionHookEntry> g_functionHooks;
	int g_nextId = 1;

	// --- native hooks ---------------------------------------------------

	struct NativeHookEntry
	{
		NativeHook hook;
		rage::scrNativeHandler original;
		NativeHooks::Id hookId;
		int slot;
		std::unique_ptr<char[]> text; // a string return
	};

	constexpr std::size_t kSlots = 64;

	struct Slot
	{
		NativeHookEntry* entry = nullptr;
		rage::scrNativeHandler original = nullptr; // kept after removal, for a stale table entry
	};
	std::array<Slot, kSlots> g_slots;
	std::vector<std::unique_ptr<NativeHookEntry>> g_nativeHooks;

	void WriteReturn(rage::scrNativeCallContext* ctx, const ReturnSpec& spec, const char* text)
	{
		auto* slots = ctx->GetReturnValue<std::uint64_t>();
		if (!slots)
			return;
		switch (spec.kind)
		{
		case Value::Void: break;
		case Value::True: slots[0] = 1; break;
		case Value::False: slots[0] = 0; break;
		case Value::Int: slots[0] = static_cast<std::uint64_t>(static_cast<std::int64_t>(spec.i)); break;
		case Value::Float:
			slots[0] = 0;
			std::memcpy(&slots[0], &spec.f, sizeof(float));
			break;
		case Value::Vector3: // scrVector: each float in its own 8-byte slot
			for (int i = 0; i < 3; i++)
			{
				slots[i] = 0;
				std::memcpy(&slots[i], &spec.v[i], sizeof(float));
			}
			break;
		case Value::String: slots[0] = reinterpret_cast<std::uint64_t>(text); break;
		}
	}

	void RunSlot(Slot& slot, rage::scrNativeCallContext* ctx)
	{
		NativeHookEntry* entry = slot.entry;
		if (!entry)
		{
			if (slot.original)
				slot.original(ctx);
			return;
		}

		NativeHook& hook = entry->hook;
		hook.calls++;
		if (const GamePointers::Pointers* p = GamePointers::Cached(); p && *p->CurrentScriptThread)
			hook.lastCaller = (*p->CurrentScriptThread)->m_Context.m_ScriptHash;
		const int logged = (std::min)(static_cast<int>(hook.native->parameterCount), kLoggedArgs);
		for (int i = 0; i < logged; i++)
			hook.lastArgs[i] = ctx->GetArg<std::uint64_t>(i);

		if (hook.mode != NativeMode::Return)
			entry->original(ctx);
		if (hook.mode != NativeMode::LogOnly)
			WriteReturn(ctx, hook.returns, entry->text.get());
	}

	template <std::size_t N>
	void SlotHandler(rage::scrNativeCallContext* ctx)
	{
		RunSlot(g_slots[N], ctx);
	}

	template <std::size_t... I>
	constexpr std::array<rage::scrNativeHandler, sizeof...(I)> MakeHandlers(std::index_sequence<I...>)
	{
		return { &SlotHandler<I>... };
	}
	constexpr auto kHandlers = MakeHandlers(std::make_index_sequence<kSlots>{});
}

namespace ScriptHooks
{
	const char* ValueName(Value value)
	{
		switch (value)
		{
		case Value::Void: return "Return (void)";
		case Value::True: return "Return true";
		case Value::False: return "Return false";
		case Value::Int: return "Return INT";
		case Value::Float: return "Return FLOAT";
		case Value::Vector3: return "Return VECTOR3";
		case Value::String: return "Return char* (dangerous)";
		}
		return "?";
	}

	int SlotCount(Value value)
	{
		switch (value)
		{
		case Value::Void: return 0;
		case Value::Vector3: return 3;
		default: return 1;
		}
	}

	std::string Describe(const ReturnSpec& spec)
	{
		switch (spec.kind)
		{
		case Value::Void: return "void";
		case Value::True: return "true";
		case Value::False: return "false";
		case Value::Int: return std::format("{} (0x{:X})", spec.i, static_cast<std::uint32_t>(spec.i));
		case Value::Float: return std::format("{}f", spec.f);
		case Value::Vector3: return std::format("<<{}f, {}f, {}f>>", spec.v[0], spec.v[1], spec.v[2]);
		case Value::String: return std::format("\"{}\"", spec.s);
		}
		return "?";
	}

	std::string AddFunctionHook(std::uint32_t script, const ScriptBytecode::Function& function, const ReturnSpec& returns)
	{
		if (SlotCount(returns.kind) != function.returnCount)
			return std::format("func_{} returns {} slot(s); {} returns {}.", function.index, function.returnCount,
				ValueName(returns.kind), SlotCount(returns.kind));
		for (const auto& entry : g_functionHooks)
		{
			if (entry.hook.script == script && entry.hook.enter == function.enter)
				return std::format("func_{} is already hooked; remove that hook first.", function.index);
		}

		std::vector<std::uint8_t> bytes;
		switch (returns.kind)
		{
		case Value::Void: break;
		case Value::True: ScriptBytecode::PushInt(bytes, 1); break;
		case Value::False: ScriptBytecode::PushInt(bytes, 0); break;
		case Value::Int: ScriptBytecode::PushInt(bytes, returns.i); break;
		case Value::Float: ScriptBytecode::PushFloat(bytes, returns.f); break;
		case Value::Vector3:
			for (float component : returns.v)
				ScriptBytecode::PushFloat(bytes, component);
			break;
		case Value::String:
		{
			const char* text = LowString(returns.s);
			if (!text)
				return "No memory below 2 GB left for the string.";
			ScriptBytecode::PushInt(bytes, static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(text)));
			break;
		}
		}
		ScriptBytecode::Leave(bytes, function.argCount, function.returnCount);

		if (function.body + bytes.size() > function.end)
			return std::format("func_{} is only {} bytes long; this return needs {}. Try a value with a shorter encoding (-1, 0, 1, 2, 0..255, 0f).",
				function.index, function.end - function.body, bytes.size());

		std::string error;
		const ScriptVM::PatchId patch = ScriptVM::AddPatch(script, function.body, std::move(bytes), &error);
		if (!patch)
			return error;

		FunctionHook hook = { g_nextId++, script, function.index, function.enter, function.argCount, function.returnCount, returns };
		g_functionHooks.push_back({ hook, patch });
		Log::Write("Script Monitor: hooked func_{} of {:#010x} to {}", function.index, script, Describe(returns));
		return {};
	}

	void RemoveFunctionHook(int id)
	{
		auto it = std::find_if(g_functionHooks.begin(), g_functionHooks.end(), [id](const auto& entry) { return entry.hook.id == id; });
		if (it == g_functionHooks.end())
			return;
		ScriptVM::RemovePatch(it->patch);
		g_functionHooks.erase(it);
	}

	std::vector<FunctionHook> ListFunctionHooks()
	{
		std::vector<FunctionHook> hooks;
		for (const auto& entry : g_functionHooks)
			hooks.push_back(entry.hook);
		return hooks;
	}

	const char* NativeModeName(NativeMode mode)
	{
		switch (mode)
		{
		case NativeMode::Return: return "Skip native, return value";
		case NativeMode::CallThenReturn: return "Call native, override return";
		case NativeMode::LogOnly: return "Call native, log only";
		}
		return "?";
	}

	std::string AddNativeHook(std::uint32_t script, const ScriptData::Native& native, NativeMode mode, const ReturnSpec& returns)
	{
		int slot = -1;
		for (std::size_t i = 0; i < kSlots; i++)
		{
			if (!g_slots[i].entry)
			{
				slot = static_cast<int>(i);
				break;
			}
		}
		if (slot < 0)
			return std::format("All {} native hook slots are in use.", kSlots);

		const rage::scrNativeHandler original = NativeHooks::Original(native.hash);
		if (!original)
			return std::format("The game has no handler for {} (0x{:016X}).", native.name, native.hash);

		auto entry = std::make_unique<NativeHookEntry>();
		entry->hook = { g_nextId++, &native, script, mode, returns, 0, 0, {} };
		entry->original = original;
		entry->slot = slot;
		if (returns.kind == Value::String)
		{
			entry->text = std::make_unique<char[]>(returns.s.size() + 1);
			std::memcpy(entry->text.get(), returns.s.c_str(), returns.s.size() + 1);
		}

		g_slots[slot].entry = entry.get();
		g_slots[slot].original = original;
		entry->hookId = NativeHooks::Add(script, native.hash, kHandlers[slot]);
		if (!entry->hookId)
		{
			g_slots[slot].entry = nullptr;
			return "The native hook couldn't be installed (see Rampagio.log).";
		}
		Log::Write("Script Monitor: hooked {}::{} in {:#010x} ({})", native.nameSpace, native.name, script, NativeModeName(mode));
		g_nativeHooks.push_back(std::move(entry));
		return {};
	}

	void RemoveNativeHook(int id)
	{
		auto it = std::find_if(g_nativeHooks.begin(), g_nativeHooks.end(), [id](const auto& entry) { return entry->hook.id == id; });
		if (it == g_nativeHooks.end())
			return;
		NativeHooks::Remove((*it)->hookId);
		g_slots[(*it)->slot].entry = nullptr;
		g_nativeHooks.erase(it);
	}

	void ResetNativeHookStats(int id)
	{
		for (auto& entry : g_nativeHooks)
		{
			if (entry->hook.id == id)
			{
				entry->hook.calls = 0;
				entry->hook.lastCaller = 0;
				std::fill(std::begin(entry->hook.lastArgs), std::end(entry->hook.lastArgs), 0);
			}
		}
	}

	std::vector<NativeHook> ListNativeHooks()
	{
		std::vector<NativeHook> hooks;
		for (const auto& entry : g_nativeHooks)
			hooks.push_back(entry->hook);
		return hooks;
	}

	void RemoveAll()
	{
		while (!g_functionHooks.empty())
			RemoveFunctionHook(g_functionHooks.back().hook.id);
		while (!g_nativeHooks.empty())
			RemoveNativeHook(g_nativeHooks.back()->hook.id);
	}
}
