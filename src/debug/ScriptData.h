/*
	Lookup tables for the Script Monitor, compiled in from src/data:
	script names by name hash (ScriptNames.inc, tools/extract_script_names.py)
	and alloc8or's natives (NativeList.inc, tools/extract_native_list.py).
*/

#pragma once

#include <cstdint>
#include <span>

namespace ScriptData
{
	struct ScriptName
	{
		std::uint32_t hash;
		const char* name;
	};

	// The script's name, or nullptr if it isn't a 1491.50 singleplayer script. O(1).
	const char* FindScriptName(std::uint32_t hash);
	// Every known script, sorted by name.
	std::span<const ScriptName> ScriptNames();
	// The stack size Rampage starts the script with (ScriptStackSizes.inc,
	// tools/extract_rampage_tables.py), or 0 if unknown.
	int StackSize(std::uint32_t hash);
	// The force-cleanup flags the script checks (ForceCleanupFlags.inc,
	// tools/extract_cleanup_flags.py), or 0 if none.
	std::uint32_t CleanupFlags(std::uint32_t hash);

	// The return-value kinds a hook can produce, matching NativeList.inc's kind column.
	enum class NativeReturn : std::uint8_t { Void, Int, Float, Vector3, String, Pointer };

	struct Native
	{
		const char* nameSpace;
		const char* name;
		std::uint64_t hash;
		NativeReturn returns;
		const char* returnType;
		const char* parameters;
		std::uint8_t parameterCount;
	};

	// natives.h's order: grouped by namespace.
	std::span<const Native> Natives();
	std::span<const char* const> NativeNamespaces();
	const Native* FindNative(std::uint64_t hash);
}
