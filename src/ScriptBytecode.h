/*
	Reads a loaded script's bytecode (scrProgram code pages) the way the
	decompiler does, so the Script Monitor can name functions as the
	decompiled scripts in ..\Scripts\1491.50 do.

	The decompiler names functions by the order of their ENTER instructions:
	index 0 is __EntryFunction__, index N is func_N. Each decompiled
	function's "// Position - 0x..." comment is either its ENTER offset or
	the end of the previous function (the byte after its LEAVE); the two
	differ only where padding sits between functions. Checked offline
	against all 1,638 decompiled 1491.50 singleplayer scripts: every func_N
	and Position matched (tools/check_script_functions.py).

	Opcodes are the decompiler's RDR1355 set, except PUSH_CONST_U32 (55) and
	PUSH_CONST_F (134), which it has swapped: the bytes of
	aberdeenpigfarm's func_140 (`return fParam0 * 1.8f + 32f`) show
	134 + 1.8f. Both take 4 operand bytes, so the walk was right either way.

	Instructions never straddle a 0x4000 code page (the compiler pads with
	NOPs), so reading by offset through GetCodeAddress is safe.
*/

#pragma once

#include "..\external\RDR-Classes\script\scrProgram.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ScriptBytecode
{
	// Only the opcodes this code walks or emits. Every push the encoders
	// emit was checked against bytecode whose decompiled source is known
	// (tiny `return <const>;` functions): -1, 0, 1, 2, U8, U32, 0f and F.
	namespace Op
	{
		constexpr std::uint8_t PUSH_CONST_M1 = 8;
		constexpr std::uint8_t PUSH_CONST_1 = 9;
		constexpr std::uint8_t PUSH_CONST_2 = 17;
		constexpr std::uint8_t ENTER = 34;
		constexpr std::uint8_t PUSH_CONST_0 = 47;
		constexpr std::uint8_t PUSH_CONST_U32 = 55;
		constexpr std::uint8_t SWITCH = 60;
		constexpr std::uint8_t LEAVE = 80;
		constexpr std::uint8_t PUSH_CONST_U8 = 109;
		constexpr std::uint8_t PUSH_CONST_F0 = 115;
		constexpr std::uint8_t PUSH_CONST_F = 134;
	}

	struct Function
	{
		std::uint32_t index;       // func_N
		std::uint32_t start;       // end of the previous function
		std::uint32_t enter;       // the ENTER instruction
		std::uint32_t body;        // first byte after ENTER
		std::uint32_t end;         // start of the next function (or the code size)
		std::uint8_t argCount;
		std::uint16_t frameSize;
		std::uint8_t returnCount;  // from the first LEAVE, as the decompiler reads it
	};

	// Every function in the program, in func_N order. Empty if the program
	// has no code.
	std::vector<Function> ListFunctions(const rage::scrProgram* program);

	// "func_12", "12", "__EntryFunction__" or a Position ("0x3A7"), as the
	// decompiled scripts write them; nullopt if it isn't one of those.
	// A position comes back with isPosition set and value = the offset.
	struct FunctionRef
	{
		bool isPosition;
		std::uint32_t value;
	};
	std::optional<FunctionRef> ParseFunctionRef(std::string_view text);

	// The function a reference names, or nullptr.
	const Function* Find(const std::vector<Function>& functions, FunctionRef ref);

	// Encoders for hook payloads: the shortest instruction pushing the value.
	void PushInt(std::vector<std::uint8_t>& out, std::int32_t value);
	void PushFloat(std::vector<std::uint8_t>& out, float value);
	void Leave(std::vector<std::uint8_t>& out, std::uint8_t argCount, std::uint8_t returnCount);
}
