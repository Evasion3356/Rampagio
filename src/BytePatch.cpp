#include "BytePatch.h"
#include "Log.h"
#include "PatternScan.h"

#include <windows.h>

#include <vector>

namespace
{
	std::vector<BytePatch*>& Registry()
	{
		static std::vector<BytePatch*> patches;
		return patches;
	}
}

BytePatch::BytePatch(const char* name, const char* pattern, int offset, std::uint8_t original, std::uint8_t patched)
	: m_name(name), m_pattern(pattern), m_offset(offset), m_original(original), m_patched(patched)
{
	Registry().push_back(this);
}

bool BytePatch::Resolve()
{
	if (m_resolved)
		return m_address != 0;
	m_resolved = true;
	const auto match = PatternScan::FindInMainModule(m_pattern);
	if (!match)
	{
		Log::Write("[BytePatch] {}: pattern not found", m_name);
		return false;
	}
	if (PatternScan::FindInMainModule(m_pattern, *match + 1))
	{
		Log::Write("[BytePatch] {}: pattern matches more than once", m_name);
		return false;
	}
	const std::uintptr_t address = *match + m_offset;
	const std::uint8_t current = *reinterpret_cast<const std::uint8_t*>(address);
	if (current != m_original)
	{
		Log::Write("[BytePatch] {}: expected 0x{:02X} at RDR2.exe+0x{:X}, found 0x{:02X}", m_name, m_original,
			address - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)), current);
		return false;
	}
	m_address = address;
	Log::Write("[BytePatch] {}: found at RDR2.exe+0x{:X}", m_name, address - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
	return true;
}

bool BytePatch::Write(std::uintptr_t address, std::uint8_t value)
{
	DWORD old = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE, &old))
		return false;
	*reinterpret_cast<std::uint8_t*>(address) = value;
	VirtualProtect(reinterpret_cast<void*>(address), 1, old, &old);
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 1);
	return true;
}

bool BytePatch::Set(bool on)
{
	if (on == m_applied)
		return true;
	if (!Resolve())
		return false;
	if (!Write(m_address, on ? m_patched : m_original))
		return false;
	m_applied = on;
	return true;
}

void BytePatch::RestoreAll()
{
	for (BytePatch* patch : Registry())
		patch->Set(false);
}
