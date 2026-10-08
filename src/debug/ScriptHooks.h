/*
	The Script Monitor's hooks, Rampage's Script Patcher done properly.

	Function hooks make a script function return at once with a fixed value:
	right after its ENTER, a push of the value and a LEAVE with the
	function's own argument and return counts go into the script's private
	code copy (ScriptVM.h). The function is named the way the decompiled
	scripts name it (func_N or its Position), and its argument and return
	counts come from the bytecode, so the user only picks what to return:
	the choices are the ones whose size matches what the function returns
	(nothing, one slot, or a Vector3's three).

	A string return pushes a pointer to a copy of the text in memory below
	2 GB, because the VM's widest constant push is 32 bits. Scripts that
	write into or keep the string can misbehave; the UI says so.

	Native hooks replace a native for one script or all of them (through
	NativeHooks.h). Each takes one of a fixed pool of handlers, since a
	native handler gets no user pointer. They count calls and keep the last
	caller and arguments, and either skip the native and return a value, run
	it then override the return, or only log.

	Everything here runs on the script thread.
*/

#pragma once

#include "ScriptData.h"
#include "..\ScriptBytecode.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ScriptHooks
{
	enum class Value : std::uint8_t { Void, True, False, Int, Float, Vector3, String };

	const char* ValueName(Value value);
	// Script stack slots the value takes: 0, 1 or 3.
	int SlotCount(Value value);

	struct ReturnSpec
	{
		Value kind = Value::Void;
		std::int32_t i = 0;
		float f = 0;
		float v[3] = {};
		std::string s;
	};
	std::string Describe(const ReturnSpec& spec);

	struct FunctionHook
	{
		int id;
		std::uint32_t script;
		std::uint32_t index; // func_N
		std::uint32_t enter;
		std::uint8_t argCount;
		std::uint8_t returnCount;
		ReturnSpec returns;
	};

	// "" on success, else why not.
	std::string AddFunctionHook(std::uint32_t script, const ScriptBytecode::Function& function, const ReturnSpec& returns);
	void RemoveFunctionHook(int id);
	std::vector<FunctionHook> ListFunctionHooks();

	enum class NativeMode : std::uint8_t { Return, CallThenReturn, LogOnly };
	const char* NativeModeName(NativeMode mode);

	constexpr int kLoggedArgs = 8;

	struct NativeHook
	{
		int id;
		const ScriptData::Native* native;
		std::uint32_t script; // 0: every script
		NativeMode mode;
		ReturnSpec returns;
		std::uint64_t calls;
		std::uint32_t lastCaller; // script hash
		std::uint64_t lastArgs[kLoggedArgs];
	};

	std::string AddNativeHook(std::uint32_t script, const ScriptData::Native& native, NativeMode mode, const ReturnSpec& returns);
	void RemoveNativeHook(int id);
	void ResetNativeHookStats(int id);
	std::vector<NativeHook> ListNativeHooks();

	// Online kill switch.
	void RemoveAll();
}
