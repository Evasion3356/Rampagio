#include "ScriptData.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
	using ScriptData::NativeReturn;

	struct NativeRow
	{
		const char* nameSpace;
		const char* name;
		std::uint64_t hash;
		int returns;
		const char* returnType;
		const char* parameters;
		int parameterCount;
	};

	constexpr ScriptData::ScriptName kScriptNames[] = {
#include "..\data\ScriptNames.inc"
	};

	constexpr NativeRow kNatives[] = {
#include "..\data\NativeList.inc"
	};

	struct Tables
	{
		std::unordered_map<std::uint32_t, const char*> scriptByHash;
		std::vector<ScriptData::ScriptName> scriptsByName;
		std::vector<ScriptData::Native> natives;
		std::vector<const char*> namespaces;
		std::unordered_map<std::uint64_t, std::size_t> nativeByHash;

		Tables()
		{
			scriptByHash.reserve(std::size(kScriptNames));
			for (const auto& row : kScriptNames)
				scriptByHash.emplace(row.hash, row.name);
			scriptsByName.assign(std::begin(kScriptNames), std::end(kScriptNames));
			std::sort(scriptsByName.begin(), scriptsByName.end(),
				[](const auto& a, const auto& b) { return std::strcmp(a.name, b.name) < 0; });

			natives.reserve(std::size(kNatives));
			for (const NativeRow& row : kNatives)
			{
				if (namespaces.empty() || std::strcmp(namespaces.back(), row.nameSpace) != 0)
					namespaces.push_back(row.nameSpace);
				nativeByHash.emplace(row.hash, natives.size());
				natives.push_back({ row.nameSpace, row.name, row.hash, static_cast<NativeReturn>(row.returns),
					row.returnType, row.parameters, static_cast<std::uint8_t>(row.parameterCount) });
			}
		}
	};

	// Built on first use (the Script Monitor opening), not at load.
	const Tables& Get()
	{
		static const Tables tables;
		return tables;
	}
}

namespace ScriptData
{
	const char* FindScriptName(std::uint32_t hash)
	{
		const auto& map = Get().scriptByHash;
		auto it = map.find(hash);
		return it != map.end() ? it->second : nullptr;
	}

	std::span<const ScriptName> ScriptNames()
	{
		return Get().scriptsByName;
	}

	std::span<const Native> Natives()
	{
		return Get().natives;
	}

	std::span<const char* const> NativeNamespaces()
	{
		return Get().namespaces;
	}

	const Native* FindNative(std::uint64_t hash)
	{
		const Tables& tables = Get();
		auto it = tables.nativeByHash.find(hash);
		return it != tables.nativeByHash.end() ? &tables.natives[it->second] : nullptr;
	}
}
