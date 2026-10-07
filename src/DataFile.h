/*
	Named INI files for the menu's saved data (custom teleports, saved
	toggles, outfits, ...). Each lives where Rampagio.ini does: next to the
	.asi, or in %LOCALAPPDATA%\RDR2ASIMods\ when the game folder isn't
	writable (LogFallback::ResolveSettings).

		DataFile::Ini ini = DataFile::Load(L"Rampagio_Teleports.ini");
		ini.sections["Barn"]["x"] = "123.4";
		DataFile::Save(L"Rampagio_Teleports.ini", ini);
*/

#pragma once

#include "..\external\inipp\inipp\inipp.h"

#include <string>

namespace DataFile
{
	using Ini = inipp::Ini<char>;

	Ini Load(const std::wstring& fileName);
	bool Save(const std::wstring& fileName, Ini& ini);
}
