#include "ScriptBytecode.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>

namespace
{
	// Operand bytes per opcode (the decompiler's ScriptFile.GetFunctions,
	// RDR1355 numbering); SWITCH and ENTER are variable and handled apart.
	constexpr std::array<std::uint8_t, 256> MakeOperandLengths()
	{
		std::array<std::uint8_t, 256> lengths{};
		// U8 operand: PUSH_CONST_U8, the *_U8 array/local/static/offset ops, IADD_U8,
		// IMUL_U8, and the four TEXT_LABEL_* ops.
		for (int op : { 109, 92, 20, 128, 39, 108, 99, 23, 100, 75, 102, 103, 137, 84, 78, 31, 121, 94, 41 })
			lengths[op] = 1;
		// Two bytes: PUSH_CONST_U8_U8, the S16/U16 ops and the jumps.
		for (int op : { 111, 37, 59, 127, 24, 120, 140, 64, 2, 10, 88, 1, 68, 70, 58, 95, 135, 112, 74, 104, 139, 21, 114, 46, 117, 138, 35 })
			lengths[op] = 2;
		// Three: PUSH_CONST_U8_U8_U8, NATIVE, CALL and the U24 ops.
		for (int op : { 123, 3, 57, 62, 107, 40, 93, 133, 38, 33 })
			lengths[op] = 3;
		// Four: PUSH_CONST_U32 and PUSH_CONST_F.
		for (int op : { 55, 134 })
			lengths[op] = 4;
		lengths[ScriptBytecode::Op::LEAVE] = 2;
		return lengths;
	}
	constexpr auto kOperandLengths = MakeOperandLengths();

	std::uint8_t ByteAt(const rage::scrProgram* program, std::uint32_t pc)
	{
		const std::uint8_t* address = program->GetCodeAddress(pc);
		return address ? *address : 0;
	}
}

namespace ScriptBytecode
{
	std::vector<Function> ListFunctions(const rage::scrProgram* program)
	{
		std::vector<Function> functions;
		if (!program || !program->m_CodeBlocks || program->m_CodeSize == 0)
			return functions;

		const std::uint32_t size = program->m_CodeSize;
		std::uint32_t pc = 0;
		std::uint32_t afterLeave = 0; // where the next function starts
		bool needReturnCount = false;
		while (pc < size)
		{
			const std::uint8_t op = ByteAt(program, pc);
			if (op == Op::ENTER)
			{
				const std::uint8_t nameLength = ByteAt(program, pc + 4);
				Function function{};
				function.index = static_cast<std::uint32_t>(functions.size());
				function.start = afterLeave;
				function.enter = pc;
				function.body = pc + 5 + nameLength;
				function.argCount = ByteAt(program, pc + 1);
				function.frameSize = static_cast<std::uint16_t>(ByteAt(program, pc + 2) | ByteAt(program, pc + 3) << 8);
				if (!functions.empty())
					functions.back().end = function.start;
				functions.push_back(function);
				needReturnCount = true;
				pc += 5 + nameLength;
				continue;
			}
			if (op == Op::LEAVE)
			{
				if (needReturnCount && !functions.empty())
				{
					functions.back().returnCount = ByteAt(program, pc + 2);
					needReturnCount = false;
				}
				afterLeave = pc + 3;
			}
			if (op == Op::SWITCH)
			{
				const std::uint32_t cases = ByteAt(program, pc + 1) | ByteAt(program, pc + 2) << 8;
				pc += 3 + 6 * cases;
				continue;
			}
			pc += 1 + kOperandLengths[op];
		}
		if (!functions.empty())
			functions.back().end = size;
		return functions;
	}

	std::optional<FunctionRef> ParseFunctionRef(std::string_view text)
	{
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			text.remove_prefix(1);
		while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '(' || text.back() == ')'))
			text.remove_suffix(1);
		if (text == "__EntryFunction__")
			return FunctionRef{ false, 0 };

		bool isPosition = false;
		int base = 10;
		if (text.starts_with("func_"))
			text.remove_prefix(5);
		else if (text.starts_with("0x") || text.starts_with("0X"))
		{
			text.remove_prefix(2);
			isPosition = true;
			base = 16;
		}
		if (text.empty())
			return std::nullopt;

		std::uint32_t value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
		if (error != std::errc() || end != text.data() + text.size())
			return std::nullopt;
		return FunctionRef{ isPosition, value };
	}

	const Function* Find(const std::vector<Function>& functions, FunctionRef ref)
	{
		if (!ref.isPosition)
			return ref.value < functions.size() ? &functions[ref.value] : nullptr;
		for (const Function& function : functions)
		{
			if (ref.value >= function.start && ref.value < function.end)
				return &function;
		}
		return nullptr;
	}

	void PushInt(std::vector<std::uint8_t>& out, std::int32_t value)
	{
		switch (value)
		{
		case -1: out.push_back(Op::PUSH_CONST_M1); return;
		case 0: out.push_back(Op::PUSH_CONST_0); return;
		case 1: out.push_back(Op::PUSH_CONST_1); return;
		case 2: out.push_back(Op::PUSH_CONST_2); return;
		}
		if (value > 0 && value <= 0xFF)
		{
			out.push_back(Op::PUSH_CONST_U8);
			out.push_back(static_cast<std::uint8_t>(value));
			return;
		}
		const auto bits = static_cast<std::uint32_t>(value);
		out.push_back(Op::PUSH_CONST_U32);
		for (int i = 0; i < 4; i++)
			out.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
	}

	void PushFloat(std::vector<std::uint8_t>& out, float value)
	{
		const auto bits = std::bit_cast<std::uint32_t>(value);
		if (bits == 0) // +0.0f only; -0.0f keeps its sign bit
		{
			out.push_back(Op::PUSH_CONST_F0);
			return;
		}
		out.push_back(Op::PUSH_CONST_F);
		for (int i = 0; i < 4; i++)
			out.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
	}

	void Leave(std::vector<std::uint8_t>& out, std::uint8_t argCount, std::uint8_t returnCount)
	{
		out.push_back(Op::LEAVE);
		out.push_back(argCount);
		out.push_back(returnCount);
	}
}
