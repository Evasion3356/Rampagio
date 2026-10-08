#include "ScriptFunction.h"
#include "GamePointers.h"
#include "Log.h"

#include "..\external\RDR-Classes\rage\tlsContext.hpp"

#include <algorithm>
#include <charconv>
#include <memory>

namespace
{
	struct Pattern
	{
		std::vector<std::uint8_t> bytes;
		std::vector<bool> mask; // true = must match
	};

	Pattern ParsePattern(std::string_view text)
	{
		Pattern p;
		for (size_t i = 0; i < text.size();)
		{
			if (text[i] == ' ')
			{
				i++;
			}
			else if (text[i] == '?')
			{
				p.bytes.push_back(0);
				p.mask.push_back(false);
				while (i < text.size() && text[i] == '?')
					i++;
			}
			else
			{
				std::uint8_t byte = 0;
				std::from_chars(text.data() + i, text.data() + std::min(i + 2, text.size()), byte, 16);
				p.bytes.push_back(byte);
				p.mask.push_back(true);
				i += 2;
			}
		}
		return p;
	}

	// Code is stored in 0x4000-byte pages; GetCodeAddress handles the split.
	std::optional<std::uint32_t> FindInCode(rage::scrProgram* program, const Pattern& pattern)
	{
		const std::uint32_t size = program->m_CodeSize;
		const std::uint32_t len = static_cast<std::uint32_t>(pattern.bytes.size());
		if (len == 0 || size < len)
			return std::nullopt;

		for (std::uint32_t i = 0; i <= size - len; i++)
		{
			bool match = true;
			for (std::uint32_t j = 0; j < len && match; j++)
				match = !pattern.mask[j] || *program->GetCodeAddress(i + j) == pattern.bytes[j];
			if (match)
				return i;
		}
		return std::nullopt;
	}

	// Runs from `pc` on `thread`'s stack, above whatever it has pushed. The
	// thread's own context is copied, not modified, so a live script picks
	// up where it left off.
	void RunScript(const GamePointers::Pointers& p, rage::scrThread* thread, rage::scrProgram* program, std::uint32_t pc,
		const std::vector<std::uint64_t>& args, void* returnValue, std::uint32_t returnSize)
	{
		// The VM's per-block "globals loaded" flags; HorseMenu passes 50 trues.
		std::uint8_t globalsEnabled[50];
		std::memset(globalsEnabled, 1, sizeof(globalsEnabled));

		rage::scrThread* oldThread = *p.CurrentScriptThread;
		const bool oldRunning = rage::tlsContext::Get()->m_RunningScript;

		auto stack = static_cast<std::uint64_t*>(thread->m_Stack);
		rage::scrThreadContext context = thread->m_Context;
		const std::uint32_t top = context.m_StackPointer;

		for (std::uint64_t arg : args)
			stack[context.m_StackPointer++] = arg;
		stack[context.m_StackPointer++] = 0; // return address
		context.m_ProgramCounter = pc;
		context.m_State = rage::eThreadState::idle;

		p.ScriptVM(stack, p.ScriptGlobals, reinterpret_cast<bool*>(globalsEnabled), program, &context);

		rage::tlsContext::Get()->m_RunningScript = oldRunning;
		*p.CurrentScriptThread = oldThread;

		if (returnValue)
			std::memcpy(returnValue, stack + top, returnSize);
	}
}

ScriptFunction::ScriptFunction(const char* scriptName, const char* pattern)
	: m_name(scriptName),
	m_hash(rage::Joaat(scriptName)),
	m_pattern(pattern)
{
}

bool ScriptFunction::CallImpl(const std::vector<std::uint64_t>& args, void* returnValue, std::uint32_t returnSize)
{
	const GamePointers::Pointers* p = GamePointers::Get();
	if (!p)
		return false;

	rage::scrProgram* program = GamePointers::FindScriptProgram(m_hash);
	if (!program)
	{
		Log::Write("ScriptFunction: {} isn't loaded", m_name);
		return false;
	}

	if (!m_pc)
	{
		m_pc = FindInCode(program, ParsePattern(m_pattern));
		if (!m_pc)
		{
			Log::Write("ScriptFunction: pattern not found in {}: {}", m_name, m_pattern);
			return false;
		}
		Log::Write("ScriptFunction: {} function at pc {:#x}", m_name, *m_pc);
	}

	if (rage::scrThread* live = GamePointers::FindScriptThread(m_hash))
	{
		RunScript(*p, live, program, *m_pc, args, returnValue, returnSize);
		return true;
	}

	// Not running: borrow a copy of the current thread with its own stack.
	rage::scrThread* current = *p->CurrentScriptThread;
	if (!current)
	{
		Log::Write("ScriptFunction: no current script thread to copy for {}", m_name);
		return false;
	}

	constexpr std::uint32_t kStackSlots = 25000;
	auto threadBytes = std::make_unique<std::uint8_t[]>(sizeof(rage::scrThread));
	auto stack = std::make_unique<std::uint64_t[]>(kStackSlots);
	auto thread = reinterpret_cast<rage::scrThread*>(threadBytes.get());
	std::memcpy(thread, current, sizeof(rage::scrThread));
	thread->m_Stack = stack.get();
	thread->m_Context.m_StackSize = kStackSlots;
	thread->m_Context.m_StackPointer = 1;

	RunScript(*p, thread, program, *m_pc, args, returnValue, returnSize);
	return true;
}
