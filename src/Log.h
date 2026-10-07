/*
	Minimal file logger backed by spdlog (see external/spdlog), vendored
	unchanged in structure from PokerCheat/DominoCheat's own Log.h -- see
	either project's own copy for the full backstory (why header-only mode,
	why fmt {}-style placeholders instead of printf %-style, why synchronous
	logging in both configurations). Writes Rampagio.log next to the
	.asi.

	Where the file goes: next to the .asi when that folder is writable,
	otherwise %LOCALAPPDATA%\RDR2ASIMods\ (see LogFallback.h), with a first
	line saying which path was rejected. Creating the logger never throws --
	if nothing is writable, Log::Write silently does nothing. It used to throw
	spdlog_ex out of the first Log::Write, which runs early enough in game
	load to crash RDR2 when the install folder is read-only (e.g. a Rockstar
	Launcher install under C:\Program Files). tests/LogFallbackTests covers
	exactly that scenario.
*/

#pragma once

#define SPDLOG_HEADER_ONLY
#define SPDLOG_WCHAR_FILENAMES

#include "..\external\spdlog\include\spdlog\spdlog.h"
#include "..\external\spdlog\include\spdlog\sinks\basic_file_sink.h"

#include "LogFallback.h"

#include <memory>
#include <string>
#include <utility>

namespace Log
{
	namespace detail
	{
		// Logs to preferredDir + fileName, or to fallbackDir + fileName when
		// that can't be written. Never throws; nullptr if neither works.
		// Not registered with spdlog's global registry, so a second call with
		// the same name (tests, a hot-reload) can't throw "already exists".
		inline std::shared_ptr<spdlog::logger> CreateLogger(const std::string& name, const std::wstring& preferredDir,
			const std::wstring& fileName, const std::wstring& fallbackDir)
		{
			try
			{
				const LogFallback::Resolved resolved = LogFallback::Resolve(preferredDir, fileName, fallbackDir);

				// The preferred path can still fail inside spdlog after passing
				// Resolve()'s probe (e.g. locked in between) -- retry at the
				// fallback before giving up.
				const std::wstring candidates[2] = {
					resolved.path,
					resolved.usedFallback || fallbackDir.empty() ? std::wstring() : fallbackDir + fileName };
				for (int i = 0; i < 2; i++)
				{
					if (candidates[i].empty())
						continue;
					try
					{
						if (i == 1)
							LogFallback::EnsureDirectory(fallbackDir);
						auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(candidates[i], false);
						auto logger = std::make_shared<spdlog::logger>(name, std::move(sink));
						logger->set_pattern("[%H:%M:%S.%e] %v");
						logger->flush_on(spdlog::level::trace);
						if (resolved.usedFallback || i == 1)
							logger->info("Log redirected here: could not write {}",
								LogFallback::ToUtf8(resolved.usedFallback ? resolved.rejectedPath : preferredDir + fileName));
						return logger;
					}
					catch (...)
					{
					}
				}
			}
			catch (...)
			{
			}
			return nullptr;
		}

		inline const std::shared_ptr<spdlog::logger>& GetLogger()
		{
			static const std::shared_ptr<spdlog::logger> logger = CreateLogger(
				"Rampagio", LogFallback::ModuleDirectory(), L"Rampagio.log", LogFallback::FallbackDirectory());
			return logger;
		}
	}

	template <typename... Args>
	void Write(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		if (const auto& logger = detail::GetLogger())
			logger->info(fmt, std::forward<Args>(args)...);
	}
}
