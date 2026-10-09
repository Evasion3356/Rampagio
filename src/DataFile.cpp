#include "DataFile.h"
#include "Log.h"
#include "LogFallback.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

namespace DataFile
{
	namespace
	{
		LogFallback::SettingsPaths Paths(const std::wstring& fileName)
		{
			return LogFallback::ResolveSettings(LogFallback::ModuleDirectory(), fileName, LogFallback::FallbackDirectory());
		}

		// Where to read fileName from, without creating anything.
		// ResolveSettings tests the game folder by opening the file with
		// OPEN_ALWAYS, which leaves an empty file behind when there was none,
		// so it's only for writes. This picks the same file it would: the
		// game folder's copy while that's writable, else the fallback's.
		std::wstring ReadPath(const std::wstring& fileName)
		{
			const std::wstring preferred = LogFallback::ModuleDirectory() + fileName;
			if (LogFallback::FileExists(preferred) && LogFallback::CanAppend(preferred))
				return preferred;
			const std::wstring fallbackDir = LogFallback::FallbackDirectory();
			if (!fallbackDir.empty() && LogFallback::FileExists(fallbackDir + fileName))
				return fallbackDir + fileName;
			return preferred;
		}

		// Writes through "<path>.tmp" and a rename, so an eject or crash
		// mid-write can't leave a truncated file.
		bool WriteAtomic(const std::wstring& path, std::string_view text, const char* caller)
		{
			const std::wstring temp = path + L".tmp";
			{
				std::ofstream os(temp, std::ios::binary | std::ios::trunc);
				if (!os)
				{
					Log::Write("{} -- couldn't open {} for writing", caller, LogFallback::ToUtf8(temp));
					return false;
				}
				os << text;
				if (!os)
					return false;
			}
			if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				Log::Write("{} -- couldn't replace {} (error {})", caller, LogFallback::ToUtf8(path), GetLastError());
				return false;
			}
			return true;
		}
	}

	nlohmann::json LoadJson(const std::wstring& fileName)
	{
		const std::wstring path = ReadPath(fileName);
		std::ifstream is(path, std::ios::binary);
		if (!is)
			return nlohmann::json::object();
		std::stringstream text;
		text << is.rdbuf();
		is.close();
		// Empty (or blank) is "nothing saved yet", not corrupt.
		if (text.str().find_first_not_of(" \t\r\n") == std::string::npos)
			return nlohmann::json::object();
		nlohmann::json json = nlohmann::json::parse(text.str(), nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			const std::wstring backup = path + L".bad";
			CopyFileW(path.c_str(), backup.c_str(), FALSE);
			Log::Write("DataFile::LoadJson -- {} isn't a JSON object; starting empty (old file kept as {})",
				LogFallback::ToUtf8(path), LogFallback::ToUtf8(backup));
			return nlohmann::json::object();
		}
		return json;
	}

	bool SaveJson(const std::wstring& fileName, const nlohmann::json& json)
	{
		const std::wstring path = Paths(fileName).write;
		if (path.empty())
			return false;
		return WriteAtomic(path, json.dump(4), "DataFile::SaveJson");
	}

	std::vector<std::string> LoadLines(const std::wstring& fileName)
	{
		std::vector<std::string> lines;
		std::ifstream is(ReadPath(fileName));
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

	std::string LoadText(const std::wstring& fileName)
	{
		std::ifstream is(ReadPath(fileName), std::ios::binary);
		std::stringstream text;
		text << is.rdbuf();
		return text.str();
	}

	bool SaveText(const std::wstring& fileName, const std::string& text)
	{
		const std::filesystem::path parent = std::filesystem::path(fileName).parent_path();
		if (!parent.empty())
		{
			std::error_code ec;
			for (const std::wstring& dir : { LogFallback::ModuleDirectory(), LogFallback::FallbackDirectory() })
				if (!dir.empty())
					std::filesystem::create_directories(dir + parent.wstring(), ec);
		}
		const std::wstring path = Paths(fileName).write;
		if (path.empty())
			return false;
		return WriteAtomic(path, text, "DataFile::SaveText");
	}

	std::vector<std::string> ListFiles(const std::wstring& folder, const std::wstring& extension)
	{
		std::set<std::string> names;
		for (const std::wstring& dir : { LogFallback::ModuleDirectory(), LogFallback::FallbackDirectory() })
		{
			std::error_code ec;
			if (dir.empty())
				continue;
			for (const auto& entry : std::filesystem::directory_iterator(dir + folder, ec))
				if (entry.is_regular_file(ec) && _wcsicmp(entry.path().extension().c_str(), extension.c_str()) == 0)
					names.insert(LogFallback::ToUtf8(entry.path().stem().wstring()));
		}
		return { names.begin(), names.end() };
	}
}
