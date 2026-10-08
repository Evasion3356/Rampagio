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

	std::vector<std::string> LoadLines(const std::wstring& fileName)
	{
		std::vector<std::string> lines;
		std::ifstream is(Paths(fileName).read);
		std::string line;
		while (std::getline(is, line))
		{
			const size_t first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos || line[first] == '#' || line.compare(first, 2, "//") == 0)
				continue;
			const size_t last = line.find_last_not_of(" \t\r");
			lines.push_back(line.substr(first, last - first + 1));
		}
		return lines;
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
