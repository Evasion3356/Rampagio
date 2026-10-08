/*
	The user's saved collections (custom teleports, outfits, horses, spooner
	sets), one JSON file each, plus plain list files the user drops in. Each
	lives where Rampagio.json does: next to the .asi, or in
	%LOCALAPPDATA%\RDR2ASIMods\ when the game folder isn't writable
	(LogFallback::ResolveSettings). Collections are content, not settings,
	so they aren't part of Rampagio.json.

		nlohmann::json file = DataFile::LoadJson(L"Rampagio_Teleports.json");
		file["Barn"] = { { "x", 123.4f }, { "y", 5.0f }, { "z", 67.8f } };
		DataFile::SaveJson(L"Rampagio_Teleports.json", file);

	LoadLines reads a plain list file (one entry per line) from the same
	place, for lists the user drops in (speech lines, animations, ...).
*/

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace DataFile
{
	// The file's top-level object; {} when it doesn't exist or isn't a JSON
	// object (logged; a corrupt file is kept as "<file>.bad").
	nlohmann::json LoadJson(const std::wstring& fileName);
	bool SaveJson(const std::wstring& fileName, const nlohmann::json& json);

	// Non-empty lines, trimmed, without '#' or "//" comment lines. Empty
	// when the file doesn't exist.
	std::vector<std::string> LoadLines(const std::wstring& fileName);

	// Names (without the extension) of the files with that extension in a
	// folder next to Rampagio.json or in the fallback directory, sorted.
	// Load one with LoadJson(folder + L"\\" + name + extension).
	std::vector<std::string> ListFiles(const std::wstring& folder, const std::wstring& extension);
}
