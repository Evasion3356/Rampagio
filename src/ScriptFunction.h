/*
	Calls a function inside a game script (a .ysc), the way HorseMenu's
	ScriptFunction does (HorseMenu\src\game\rdr\ScriptFunction.cpp): find the
	function's first instruction in the script's bytecode by pattern, push the
	arguments onto a script thread's stack, and run the game's script VM from
	that program counter until the function returns.

	Rampage does the same job differently (it retargets the "audiotest"
	thread with scrThread::Reset and runs it), and finds functions by index
	(the decompiler's func_N). A bytecode pattern survives game updates that
	renumber functions, so we use HorseMenu's way. Build a pattern from the
	function's ENTER (0x22) onward and wildcard CALL (0x39) targets; the
	decompiled scripts' "Position - 0x..." comment gives the offset.

	Call() runs on the script's live thread when it's running, else on a
	temporary copy of the current thread (StaticCall in HorseMenu). Script
	thread only. Every argument is one 8-byte stack slot.
*/

#pragma once

#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

class ScriptFunction
{
public:
	ScriptFunction(const char* scriptName, const char* pattern);

	// Runs the function; false if the script, pattern or pointers weren't
	// found (logged).
	template <typename... Args>
	bool Call(Args... args)
	{
		return CallImpl({ ToSlot(args)... }, nullptr, 0);
	}

	// Same, reading a return value of type Ret.
	template <typename Ret, typename... Args>
	std::optional<Ret> CallReturning(Args... args)
	{
		Ret value{};
		if (!CallImpl({ ToSlot(args)... }, &value, sizeof(Ret)))
			return std::nullopt;
		return value;
	}

private:
	std::string m_name;
	rage::joaat_t m_hash;
	std::string m_pattern;
	std::optional<std::uint32_t> m_pc;

	// Zero-extends into a full slot (HorseMenu reads 8 bytes from every
	// argument, which picks up garbage above a 4-byte int).
	template <typename T>
	static std::uint64_t ToSlot(T value)
	{
		static_assert(sizeof(T) <= sizeof(std::uint64_t) && std::is_trivially_copyable_v<T>);
		std::uint64_t slot = 0;
		std::memcpy(&slot, &value, sizeof(T));
		return slot;
	}

	bool CallImpl(const std::vector<std::uint64_t>& args, void* returnValue, std::uint32_t returnSize);
};
