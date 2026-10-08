/*
	One-byte patches of RDR2.exe's code, found by AOB pattern
	(PatternScan). A patch only applies when the pattern matches exactly
	once and the byte at the target is the expected original, so a game
	update that moves or changes the code turns the patch into a logged
	no-op instead of a corrupt write. Turning it off, and RestoreAll()
	(from DllMain on eject), put the original byte back.

		BytePatch g_hitmarker("Hitmarker", "74 2A 48 8D 4D 98", 0, 0x74, 0xEB);
		g_hitmarker.Set(true); // jz -> jmp
*/

#pragma once

#include <cstdint>
#include <string_view>

class BytePatch
{
public:
	BytePatch(const char* name, const char* pattern, int offset, std::uint8_t original, std::uint8_t patched);

	// Applies or restores the patch. False when it can't be applied
	// (pattern missing or not unique, unexpected byte); logged once.
	bool Set(bool on);
	bool IsApplied() const { return m_applied; }

	// Restores every applied patch.
	static void RestoreAll();

private:
	const char* m_name;
	const char* m_pattern;
	int m_offset;
	std::uint8_t m_original;
	std::uint8_t m_patched;
	std::uintptr_t m_address = 0;
	bool m_resolved = false;
	bool m_applied = false;

	bool Resolve();
	static bool Write(std::uintptr_t address, std::uint8_t value);
};
