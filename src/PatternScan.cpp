#include "PatternScan.h"
#include "Log.h"

#include <windows.h>
#include <array>
#include <vector>
#include <string>
#include <cstring>

namespace
{
	struct ParsedPattern
	{
		std::vector<std::uint8_t> bytes;
		std::vector<std::uint8_t> mask; // 1 = must match, 0 = wildcard
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
				parsed.mask.push_back(0);
				i++;
				// tolerate "??" as a single wildcard token
				if (i < pattern.size() && pattern[i] == '?')
					i++;
			}
			else
			{
				parsed.bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(pattern.substr(i, 2)), nullptr, 16)));
				parsed.mask.push_back(1);
				i += 2;
			}
		}

		return parsed;
	}

	// How often each byte value occurs in the image, from one 4 KB page in
	// every 16 (a few MB), counted once.
	std::array<std::uint32_t, 256> CountBytes(const std::uint8_t* base, std::size_t size)
	{
		constexpr std::size_t kPage = 0x1000;
		constexpr std::size_t kStride = 16 * kPage;
		std::array<std::uint32_t, 256> counts{};
		for (std::size_t offset = 0; offset + kPage <= size; offset += kStride)
		{
			const std::uint8_t* page = base + offset;
			for (std::size_t i = 0; i < kPage; i++)
				counts[page[i]]++;
		}
		return counts;
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
		const std::size_t patternLen = parsed.bytes.size();
		if (patternLen == 0 || imageSize < patternLen)
			return std::nullopt;
		// Raw pointers: vector indexing is checked, and slow, in Debug.
		const std::uint8_t* bytes = parsed.bytes.data();
		const std::uint8_t* mask = parsed.mask.data();

		// memchr jumps between occurrences of one concrete byte (the
		// anchor) and only those candidates are compared. The anchor is the
		// pattern's rarest byte in this image: patterns usually start with a
		// REX prefix (48, 4C) that occurs every few bytes of x64 code, and
		// anchoring on it made a scan check nearly every offset (about a
		// second per pattern, and the first menu open waited on one).
		static const std::array<std::uint32_t, 256> counts = CountBytes(base, imageSize);
		std::size_t anchor = patternLen;
		for (std::size_t i = 0; i < patternLen; i++)
			if (mask[i] && (anchor == patternLen || counts[bytes[i]] < counts[bytes[anchor]]))
				anchor = i;

		if (anchor == patternLen)
		{
			// Pattern is all wildcards -- degenerate, nothing to anchor on.
			// This is a bug in pattern generation, not normal usage.
			Log::Write("PatternScan::FindInMainModule: WARNING -- pattern is all wildcards, cannot scan");
			return std::nullopt;
		}

		// A match starts in [first, end - patternLen]; startAddress skips
		// every match that begins before it.
		const std::uint8_t* end = base + imageSize;
		const std::uint8_t* first = base;
		const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(base);
		if (startAddress > imageBase)
		{
			if (startAddress - imageBase > imageSize - patternLen)
				return std::nullopt;
			first = base + (startAddress - imageBase);
		}

		const std::uint8_t* next = first + anchor;            // next anchor position to search from
		const std::uint8_t* last = end - patternLen + anchor; // last possible anchor position
		while (next <= last)
		{
			const void* found = memchr(next, bytes[anchor], static_cast<std::size_t>(last - next) + 1);
			if (!found)
				break;

			const std::uint8_t* candidateAnchor = static_cast<const std::uint8_t*>(found);
			const std::uint8_t* candidateStart = candidateAnchor - anchor;

			bool matched = true;
			for (std::size_t j = 0; j < patternLen; j++)
			{
				if (mask[j] && candidateStart[j] != bytes[j])
				{
					matched = false;
					break;
				}
			}

			if (matched)
				return reinterpret_cast<std::uintptr_t>(candidateStart);

			next = candidateAnchor + 1;
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
