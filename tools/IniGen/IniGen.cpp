/*
	Writes the default Rampagio.ini for this build configuration next to
	IniGen.exe, by running the mod's own Config code against a folder with no
	INI in it: Config::Reload() fills in every missing key with its default
	and saves the result. Debug builds include the Debug-only keys, Release
	builds don't, exactly as the .asi of the same configuration would write.

	Built and run by Rampagio.vcxproj's GenerateDefaultIni target, which
	copies the result to bin\<Configuration>\ for packaging. It is not
	deployed to the game folder, so it never replaces the INI you're testing
	with.
*/

#include "..\..\src\Config.h"

int main()
{
	Config::Reload();
	return 0;
}
