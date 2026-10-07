#include "DataFile.h"
#include "Log.h"
#include "LogFallback.h"

#include <fstream>

namespace DataFile
{
	namespace
	{
		LogFallback::SettingsPaths Paths(const std::wstring& fileName)
		{
			return LogFallback::ResolveSettings(LogFallback::ModuleDirectory(), fileName, LogFallback::FallbackDirectory());
		}
	}

	Ini Load(const std::wstring& fileName)
	{
		Ini ini;
		std::ifstream is(Paths(fileName).read);
		if (is)
			ini.parse(is);
		return ini;
	}

	bool Save(const std::wstring& fileName, Ini& ini)
	{
		const std::wstring path = Paths(fileName).write;
		if (path.empty())
			return false;
		std::ofstream os(path, std::ios::trunc);
		if (!os)
		{
			Log::Write("DataFile::Save -- couldn't open a data file for writing");
			return false;
		}
		ini.generate(os);
		return true;
	}
}
