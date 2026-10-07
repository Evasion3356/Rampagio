/*
	Picks writable locations for this mod's log file and, via
	ResolveSettings(), its INI settings file. Vendored identically
	into every ScriptHookRDR2 ASI project in this repo family (PokerCheat,
	BlackjackCheat, DominoCheat, ChallengeCheat, FFFCheat, FishingFix,
	YEEAHSM) -- each is its own git repo, so if you fix a bug here, port the
	same fix to the other copies too.

	The log normally goes next to the .asi (the game folder). That folder is
	not always writable: a Rockstar Launcher install under C:\Program Files
	needs admin rights, and the file can be locked by another process. The
	spdlog-based loggers used to throw spdlog_ex in that case, and because
	the first log line could run from DllMain, that exception took the game
	down during the loading screen with no log to show for it. The INI never
	crashed (plain file streams don't throw), but in an unwritable folder a
	player couldn't create or edit it and was stuck on default settings.

	Fallback is %LOCALAPPDATA%\RDR2ASIMods\ (then %TEMP%), not Documents:
	LocalAppData is always per-user writable and is never redirected to
	OneDrive or guarded by Windows Defender's Controlled Folder Access, both
	of which can block RDR2.exe from writing into Documents.

	Kernel32 only (no shell32/ole32), so it's safe to call while the loader
	lock is held.
*/

#pragma once

#include <windows.h>

#include <string>

namespace LogFallback
{
	constexpr const wchar_t* kFallbackFolderName = L"RDR2ASIMods";

	// Directory (with a trailing backslash) of the module containing this
	// code, i.e. the .asi itself. Empty on failure.
	inline std::wstring ModuleDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module))
			return {};

		std::wstring path(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0)
				return {};
			if (length < path.size())
			{
				path.resize(length);
				break;
			}
			path.resize(path.size() * 2); // truncated -- long path, grow and retry
		}

		const std::size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
	}

	inline std::wstring EnvironmentDirectory(const wchar_t* name)
	{
		const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
		if (size == 0)
			return {};

		std::wstring value(size, L'\0');
		const DWORD length = GetEnvironmentVariableW(name, value.data(), size);
		if (length == 0 || length >= size)
			return {};
		value.resize(length);
		if (value.back() != L'\\' && value.back() != L'/')
			value.push_back(L'\\');
		return value;
	}

	// %LOCALAPPDATA%\RDR2ASIMods\, else %TEMP%. Empty if neither is set.
	// Not created here -- Resolve() creates it only if it's actually used.
	inline std::wstring FallbackDirectory()
	{
		const std::wstring localAppData = EnvironmentDirectory(L"LOCALAPPDATA");
		if (!localAppData.empty())
			return localAppData + kFallbackFolderName + L"\\";

		wchar_t temp[MAX_PATH + 1] = {};
		const DWORD length = GetTempPathW(MAX_PATH + 1, temp);
		if (length > 0 && length <= MAX_PATH)
			return temp;
		return {};
	}

	// Creates `dir` (one level) if it doesn't exist yet.
	inline void EnsureDirectory(const std::wstring& dir)
	{
		if (!dir.empty())
			CreateDirectoryW(dir.c_str(), nullptr);
	}

	// True if `path` can be opened for appending. Creates the file if it
	// doesn't exist, the same thing the logger does next anyway.
	inline bool CanAppend(const std::wstring& path)
	{
		const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		CloseHandle(file);
		return true;
	}

	struct Resolved
	{
		std::wstring path;         // where to log; empty if nothing was writable
		bool usedFallback = false; // true if preferredDir was rejected
		std::wstring rejectedPath; // the preferred path that couldn't be written
	};

	// preferredDir + fileName if writable, else fallbackDir + fileName.
	// Both directories must end in a backslash (or be empty).
	inline Resolved Resolve(const std::wstring& preferredDir, const std::wstring& fileName, const std::wstring& fallbackDir)
	{
		Resolved result;
		const std::wstring preferred = preferredDir + fileName;
		if (!preferredDir.empty() && CanAppend(preferred))
		{
			result.path = preferred;
			return result;
		}

		result.usedFallback = true;
		result.rejectedPath = preferred;
		if (!fallbackDir.empty())
		{
			EnsureDirectory(fallbackDir);
			const std::wstring fallback = fallbackDir + fileName;
			if (CanAppend(fallback))
				result.path = fallback;
		}
		return result;
	}

	inline bool FileExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	struct SettingsPaths
	{
		std::wstring read;         // where to load settings from
		std::wstring write;        // where to save them; empty if nothing was writable
		bool usedFallback = false; // true if preferredDir + fileName couldn't be written
	};

	// Same idea as Resolve(), for a settings file (the mod's INI) that is
	// both read and rewritten. If preferredDir + fileName is writable, both
	// paths are that. Otherwise settings are saved to fallbackDir + fileName
	// instead, and loaded from there once it exists -- until then from the
	// preferred file, so a player's existing settings in an unwritable game
	// folder carry over instead of being reset to defaults.
	inline SettingsPaths ResolveSettings(const std::wstring& preferredDir, const std::wstring& fileName, const std::wstring& fallbackDir)
	{
		SettingsPaths result;
		const std::wstring preferred = preferredDir + fileName;
		if (!preferredDir.empty() && CanAppend(preferred))
		{
			result.read = preferred;
			result.write = preferred;
			return result;
		}

		result.usedFallback = true;
		result.read = preferred;
		if (!fallbackDir.empty())
		{
			EnsureDirectory(fallbackDir);
			const std::wstring fallback = fallbackDir + fileName;
			if (FileExists(fallback))
				result.read = fallback;
			if (CanAppend(fallback))
				result.write = fallback;
		}
		return result;
	}

	inline std::string ToUtf8(const std::wstring& wide)
	{
		if (wide.empty())
			return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		if (size <= 0)
			return {};
		std::string narrow(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), size, nullptr, nullptr);
		return narrow;
	}
}
