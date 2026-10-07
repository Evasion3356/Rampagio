/*
	Tests for LogFallback.h (log file and INI settings file) and, when LOGFALLBACK_TEST_SPDLOG is defined, the
	spdlog logger factory in Log.h (Log::detail::CreateLogger).

	The scenario: the game folder can't be written (C:\Program Files install,
	file locked). spdlog's file sink throws spdlog_ex there, and that used to
	escape from the first Log::Write -- which runs during game load -- and
	crash RDR2. These tests point the logger at C:\Windows\System32 (needs
	admin to write) and at a path that runs through a regular file (can't be
	written by anyone), and require that logging lands in the fallback folder
	instead of throwing.

	The System32 cases are skipped when the test runs elevated, since an
	admin CAN write there. The path-through-a-file cases cover the same code
	path either way.

	Vendored identically into every sibling ASI project (see LogFallback.h).
	Exits 0 and prints ALL PASS on success.
*/

#include "..\src\LogFallback.h"
#ifdef LOGFALLBACK_TEST_SPDLOG
#include "..\src\Log.h"
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	int g_failures = 0;
	int g_skipped = 0;

	void Check(bool condition, const char* name)
	{
		std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
		if (!condition)
			g_failures++;
	}

	void Skip(const char* name, const char* reason)
	{
		std::printf("[SKIP] %s (%s)\n", name, reason);
		g_skipped++;
	}

	bool IsElevated()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;
		TOKEN_ELEVATION elevation{};
		DWORD size = 0;
		const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
		CloseHandle(token);
		return ok && elevation.TokenIsElevated != 0;
	}

	bool FileExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::string ReadAll(const std::wstring& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	std::wstring AdminOnlyDirectory()
	{
		wchar_t windows[MAX_PATH] = {};
		GetWindowsDirectoryW(windows, MAX_PATH);
		return std::wstring(windows) + L"\\System32\\";
	}

	struct Scratch
	{
		std::wstring root;          // fresh empty directory under %TEMP%
		std::wstring fallback;      // root\fallback\ (not created: Resolve must create it)
		std::wstring throughFile;   // root\blocker.txt\ -- blocker.txt is a FILE, so nothing can be created under it
	};

	Scratch MakeScratch()
	{
		wchar_t temp[MAX_PATH + 1] = {};
		GetTempPathW(MAX_PATH + 1, temp);
		Scratch scratch;
		scratch.root = std::wstring(temp) + L"LogFallbackTests_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
		std::filesystem::remove_all(scratch.root);
		CreateDirectoryW(scratch.root.c_str(), nullptr);
		scratch.fallback = scratch.root + L"fallback\\";
		std::ofstream(scratch.root + L"blocker.txt") << "not a directory";
		scratch.throughFile = scratch.root + L"blocker.txt\\";
		return scratch;
	}

	const wchar_t* kFileName = L"LogFallbackTest.log";

	void TestFallbackDirectoryIsLocalAppData()
	{
		const std::wstring dir = LogFallback::FallbackDirectory();
		const std::wstring localAppData = LogFallback::EnvironmentDirectory(L"LOCALAPPDATA");
		Check(!localAppData.empty() && dir == localAppData + L"RDR2ASIMods\\",
			"FallbackDirectory() is %LOCALAPPDATA%\\RDR2ASIMods\\");
	}

	void TestModuleDirectoryIsThisExesFolder()
	{
		wchar_t exe[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring expected(exe);
		expected = expected.substr(0, expected.find_last_of(L'\\') + 1);
		Check(LogFallback::ModuleDirectory() == expected, "ModuleDirectory() is the containing module's folder");
	}

	void TestResolveKeepsWritablePreferred(const Scratch& scratch)
	{
		const auto resolved = LogFallback::Resolve(scratch.root, kFileName, scratch.fallback);
		Check(!resolved.usedFallback && resolved.path == scratch.root + kFileName && FileExists(resolved.path),
			"Resolve: writable preferred folder is used as-is");
	}

	void TestResolveFallsBackFromAdminOnlyFolder(const Scratch& scratch)
	{
		const char* name = "Resolve: C:\\Windows\\System32 (admin-only) falls back";
		if (IsElevated())
			return Skip(name, "running elevated, System32 is writable");

		const auto resolved = LogFallback::Resolve(AdminOnlyDirectory(), kFileName, scratch.fallback);
		Check(resolved.usedFallback && resolved.path == scratch.fallback + kFileName && FileExists(resolved.path)
			&& !FileExists(AdminOnlyDirectory() + kFileName), name);
	}

	void TestResolveFallsBackFromPathThroughFile(const Scratch& scratch)
	{
		const auto resolved = LogFallback::Resolve(scratch.throughFile, kFileName, scratch.fallback);
		Check(resolved.usedFallback && resolved.path == scratch.fallback + kFileName
			&& resolved.rejectedPath == scratch.throughFile + kFileName,
			"Resolve: unwritable preferred folder falls back and reports the rejected path");
	}

	void TestResolveNothingWritable(const Scratch& scratch)
	{
		const auto resolved = LogFallback::Resolve(scratch.throughFile, kFileName, scratch.throughFile + L"deeper\\");
		Check(resolved.usedFallback && resolved.path.empty(), "Resolve: nothing writable returns an empty path");
	}

	// Settings file (the mod's INI), which is both read and rewritten.
	const wchar_t* kSettingsName = L"LogFallbackTest.ini";

	void TestSettingsKeepWritablePreferred(const Scratch& scratch)
	{
		const auto paths = LogFallback::ResolveSettings(scratch.root, kSettingsName, scratch.fallback);
		Check(!paths.usedFallback && paths.read == scratch.root + kSettingsName && paths.write == paths.read,
			"ResolveSettings: writable folder reads and writes the INI in place");
	}

	// A read-only INI blocks writing even for an admin, so this case runs
	// elevated too. The player's existing settings must carry over: read
	// from the game-folder copy until the fallback copy exists, then from
	// the fallback copy.
	void TestSettingsFallBackAndCarryOverExistingIni(const Scratch& scratch)
	{
		const std::wstring gameDir = scratch.root + L"readonly_game\\";
		const std::wstring fallbackDir = scratch.root + L"settings_fallback\\";
		CreateDirectoryW(gameDir.c_str(), nullptr);
		const std::wstring existing = gameDir + kSettingsName;
		std::ofstream(existing) << "[General]\nKey=player-value\n";
		SetFileAttributesW(existing.c_str(), FILE_ATTRIBUTE_READONLY);

		const auto first = LogFallback::ResolveSettings(gameDir, kSettingsName, fallbackDir);
		Check(first.usedFallback && first.read == existing && first.write == fallbackDir + kSettingsName,
			"ResolveSettings: read-only INI is still read, and saving goes to the fallback folder");

		std::ofstream(first.write) << "[General]\nKey=saved-value\n"; // what Config::Reload's rewrite does
		const auto second = LogFallback::ResolveSettings(gameDir, kSettingsName, fallbackDir);
		Check(second.read == fallbackDir + kSettingsName && second.write == second.read,
			"ResolveSettings: once the fallback INI exists, it's the one read");

		SetFileAttributesW(existing.c_str(), FILE_ATTRIBUTE_NORMAL); // let cleanup delete it
	}

	void TestSettingsFallBackFromAdminOnlyFolder(const Scratch& scratch)
	{
		const char* name = "ResolveSettings: C:\\Windows\\System32 (admin-only) saves to the fallback";
		if (IsElevated())
			return Skip(name, "running elevated, System32 is writable");

		const std::wstring fallbackDir = scratch.root + L"settings_fallback_admin\\";
		const auto paths = LogFallback::ResolveSettings(AdminOnlyDirectory(), kSettingsName, fallbackDir);
		Check(paths.usedFallback && paths.write == fallbackDir + kSettingsName
			&& !FileExists(AdminOnlyDirectory() + kSettingsName), name);
	}

	void TestSettingsNothingWritable(const Scratch& scratch)
	{
		const auto paths = LogFallback::ResolveSettings(scratch.throughFile, kSettingsName, scratch.throughFile + L"deeper\\");
		Check(paths.usedFallback && paths.write.empty(), "ResolveSettings: nothing writable leaves the write path empty");
	}

#ifdef LOGFALLBACK_TEST_SPDLOG
	// The original failure, reproduced directly: without a fallback, spdlog
	// throws for a folder the user can't write.
	void TestSpdlogThrowsForAdminOnlyFolder()
	{
		const char* name = "Precondition: raw spdlog file sink throws for System32";
		if (IsElevated())
			return Skip(name, "running elevated, System32 is writable");

		bool threw = false;
		try
		{
			spdlog::sinks::basic_file_sink_mt sink(AdminOnlyDirectory() + kFileName, false);
		}
		catch (const spdlog::spdlog_ex&)
		{
			threw = true;
		}
		Check(threw, name);
	}

	bool LogsToFallback(const std::wstring& preferredDir, const Scratch& scratch, const char* message)
	{
		const std::wstring fallbackPath = scratch.fallback + kFileName;
		DeleteFileW(fallbackPath.c_str());
		{
			auto logger = Log::detail::CreateLogger("LogFallbackTests", preferredDir, kFileName, scratch.fallback);
			if (!logger)
				return false;
			logger->info("{}", message);
			logger->flush();
		}
		const std::string contents = ReadAll(fallbackPath);
		return contents.find("Log redirected here") != std::string::npos && contents.find(message) != std::string::npos;
	}

	void TestLoggerFallsBackFromAdminOnlyFolder(const Scratch& scratch)
	{
		const char* name = "CreateLogger: System32 (admin-only) logs to the fallback instead of throwing";
		if (IsElevated())
			return Skip(name, "running elevated, System32 is writable");

		bool ok = false;
		try
		{
			ok = LogsToFallback(AdminOnlyDirectory(), scratch, "hello from System32 test")
				&& !FileExists(AdminOnlyDirectory() + kFileName);
		}
		catch (...)
		{
			ok = false;
		}
		Check(ok, name);
	}

	void TestLoggerFallsBackFromPathThroughFile(const Scratch& scratch)
	{
		bool ok = false;
		try
		{
			ok = LogsToFallback(scratch.throughFile, scratch, "hello from path-through-file test");
		}
		catch (...)
		{
			ok = false;
		}
		Check(ok, "CreateLogger: unwritable preferred folder logs to the fallback instead of throwing");
	}

	void TestLoggerNothingWritable(const Scratch& scratch)
	{
		bool threw = false;
		std::shared_ptr<spdlog::logger> logger;
		try
		{
			logger = Log::detail::CreateLogger("LogFallbackTests", scratch.throughFile, kFileName, scratch.throughFile + L"deeper\\");
		}
		catch (...)
		{
			threw = true;
		}
		Check(!threw && !logger, "CreateLogger: nothing writable returns nullptr without throwing");
	}

	// The fallback lives under the user's profile, whose name can be
	// non-ASCII -- only works because Log.h builds spdlog with wide filenames.
	void TestLoggerHandlesNonAsciiFolder(const Scratch& scratch)
	{
		const std::wstring dir = scratch.root + L"\u00fcn\u00efc\u00f8d\u00e9 \u6d4b\u8bd5\\";
		CreateDirectoryW(dir.c_str(), nullptr);
		bool ok = false;
		try
		{
			auto logger = Log::detail::CreateLogger("LogFallbackTests", dir, kFileName, scratch.fallback);
			if (logger)
			{
				logger->info("non-ascii");
				logger->flush();
			}
			ok = logger && ReadAll(dir + kFileName).find("non-ascii") != std::string::npos;
		}
		catch (...)
		{
			ok = false;
		}
		Check(ok, "CreateLogger: non-ASCII folder name works");
	}
#endif
}

int main()
{
	const Scratch scratch = MakeScratch();

	TestFallbackDirectoryIsLocalAppData();
	TestModuleDirectoryIsThisExesFolder();
	TestResolveKeepsWritablePreferred(scratch);
	TestResolveFallsBackFromAdminOnlyFolder(scratch);
	TestResolveFallsBackFromPathThroughFile(scratch);
	TestResolveNothingWritable(scratch);
	TestSettingsKeepWritablePreferred(scratch);
	TestSettingsFallBackAndCarryOverExistingIni(scratch);
	TestSettingsFallBackFromAdminOnlyFolder(scratch);
	TestSettingsNothingWritable(scratch);
#ifdef LOGFALLBACK_TEST_SPDLOG
	TestSpdlogThrowsForAdminOnlyFolder();
	TestLoggerFallsBackFromAdminOnlyFolder(scratch);
	TestLoggerFallsBackFromPathThroughFile(scratch);
	TestLoggerNothingWritable(scratch);
	TestLoggerHandlesNonAsciiFolder(scratch);
#endif

	std::error_code ignored;
	std::filesystem::remove_all(scratch.root, ignored);

	if (g_failures == 0)
	{
		std::printf("ALL PASS%s\n", g_skipped ? " (some cases skipped, see above)" : "");
		return 0;
	}
	std::printf("%d FAILED\n", g_failures);
	return 1;
}
