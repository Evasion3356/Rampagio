#include "PatternScan.h"
#include "Log.h"

#include <windows.h>
#include <vector>
#include <string>
#include <cstring>

namespace
{
	struct ParsedPattern
	{
		std::vector<std::uint8_t> bytes;
		std::vector<bool> mask; // true = must match, false = wildcard
	};

	ParsedPattern Parse(std::string_view pattern)
	{
		ParsedPattern parsed;

		size_t i = 0;
		while (i < pattern.size())
		{
			while (i < pattern.size() && pattern[i] == ' ')
				i++;
			if (i >= pattern.size())
				break;

			if (pattern[i] == '?')
			{
				parsed.bytes.push_back(0);
				parsed.mask.push_back(false);
				i++;
				// tolerate "??" as a single wildcard token
				if (i < pattern.size() && pattern[i] == '?')
					i++;
			}
			else
			{
				parsed.bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(pattern.substr(i, 2)), nullptr, 16)));
				parsed.mask.push_back(true);
				i += 2;
			}
		}

		return parsed;
	}
}

namespace PatternScan
{
	std::optional<std::uintptr_t> FindInMainModule(std::string_view pattern, std::uintptr_t startAddress)
	{
		auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandle(nullptr));
		if (!base)
			return std::nullopt;

		auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		auto ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
		std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;

		ParsedPattern parsed = Parse(pattern);
		if (parsed.bytes.empty())
			return std::nullopt;

		std::size_t patternLen = parsed.bytes.size();
		if (imageSize < patternLen)
			return std::nullopt;

		// Anchor on the first non-wildcard byte and use memchr (a
		// well-optimized, typically SIMD-backed CRT routine) to jump
		// straight to each candidate occurrence of it, instead of
		// testing every single byte offset in the image by hand. The
		// original version here did a naive brute-force scan -- for
		// every one of a ~100MB+ game executable's byte offsets, an
		// inner-loop comparison -- which turned out to be the real
		// cause of a one-time hitch on the first "Toggle Poker Cheat"
		// press each session (this function's result is cached, but the
		// cache only gets populated on that first call). Every
		// signature used in this project so far starts with a concrete
		// byte (not a wildcard), so this covers the real case; an
		// all-wildcard pattern is handled as a degenerate no-match below
		// rather than falling back to a slow scan.
		std::size_t firstConcrete = 0;
		while (firstConcrete < patternLen && !parsed.mask[firstConcrete])
			firstConcrete++;

		if (firstConcrete == patternLen)
		{
			// Pattern is all wildcards -- degenerate, nothing to anchor on.
			// This is a bug in pattern generation, not normal usage.
			// Log a warning to catch accidental all-wildcard patterns early.
			Log::Write("PatternScan::FindInMainModule: WARNING -- pattern is all wildcards, cannot scan");
			return std::nullopt;
		}

		// Start searching at base+firstConcrete, not base -- candidateStart
		// (below) is computed as candidateAnchor - firstConcrete, so
		// starting any earlier could let memchr find an anchor byte
		// close enough to the very start of the image that
		// candidateStart would point before base, reading out of bounds.
		std::uint8_t* searchStart = base + firstConcrete;
		std::size_t remaining = imageSize - firstConcrete;

		// Honor startAddress by starting the anchor search that much later.
		const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(base);
		if (startAddress > imageBase)
		{
			const std::size_t skip = static_cast<std::size_t>(startAddress - imageBase);
			if (skip >= remaining)
				return std::nullopt;
			searchStart += skip;
			remaining -= skip;
		}

		for (;;)
		{
			// Only the region that could still contain a full match
			// (patternLen - firstConcrete bytes after the anchor) needs
			// to be searched for the anchor byte.
			if (remaining < patternLen - firstConcrete)
				break;

			std::size_t searchableForAnchor = remaining - (patternLen - firstConcrete - 1);
			void* found = memchr(searchStart, parsed.bytes[firstConcrete], searchableForAnchor);
			if (!found)
				break;

			std::uint8_t* candidateAnchor = static_cast<std::uint8_t*>(found);
			std::uint8_t* candidateStart = candidateAnchor - firstConcrete;

			bool matched = true;
			for (std::size_t j = 0; j < patternLen; j++)
			{
				if (parsed.mask[j] && candidateStart[j] != parsed.bytes[j])
				{
					matched = false;
					break;
				}
			}

			if (matched)
				return reinterpret_cast<std::uintptr_t>(candidateStart);

			// Advance past this anchor candidate and keep scanning.
			std::size_t advanced = static_cast<std::size_t>(candidateAnchor - searchStart) + 1;
			searchStart += advanced;
			remaining -= advanced;
		}

		return std::nullopt;
	}

	std::uintptr_t ResolveRip(std::uintptr_t matchAddress, int operandOffset)
	{
		auto operandAddr = matchAddress + operandOffset;
		std::int32_t displacement = *reinterpret_cast<std::int32_t*>(operandAddr);
		return operandAddr + sizeof(std::int32_t) + displacement;
	}
}
