/*
	Named INI files for the menu's saved data (custom teleports, saved
	toggles, outfits, ...). Each lives where Rampagio.ini does: next to the
	.asi, or in %LOCALAPPDATA%\RDR2ASIMods\ when the game folder isn't
	writable (LogFallback::ResolveSettings).

		DataFile::Ini ini = DataFile::Load(L"Rampagio_Teleports.ini");
		ini.sections["Barn"]["x"] = "123.4";
		DataFile::Save(L"Rampagio_Teleports.ini", ini);

	LoadLines reads a plain list file (one entry per line) from the same
	place, for lists the user drops in (speech lines, animations, ...).
*/

#pragma once

#include "..\external\inipp\inipp\inipp.h"

#include <string>
#include <vector>

namespace DataFile
{
	using Ini = inipp::Ini<char>;

	Ini Load(const std::wstring& fileName);
	bool Save(const std::wstring& fileName, Ini& ini);

	// Non-empty lines, trimmed, without '#' or "//" comment lines. Empty
	// when the file doesn't exist.
	std::vector<std::string> LoadLines(const std::wstring& fileName);
}
