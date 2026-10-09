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

	Rampagio addition: the logger also keeps its last lines in memory
	(Log::Recent), for Debug > Log (src/debug/LogWindow.h). That works even
	when no file could be written.
*/

#pragma once

#define SPDLOG_HEADER_ONLY
#define SPDLOG_WCHAR_FILENAMES

#include "..\external\spdlog\include\spdlog\spdlog.h"
#include "..\external\spdlog\include\spdlog\sinks\basic_file_sink.h"
#include "..\external\spdlog\include\spdlog\sinks\base_sink.h"

#include "LogFallback.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Log
{
	// The last kCapacity formatted lines, numbered from 0 in the order
	// written, so a reader can ask for only the ones it hasn't seen.
	class RecentSink : public spdlog::sinks::base_sink<std::mutex>
	{
		static constexpr size_t kCapacity = 2000;
		std::deque<std::string> m_lines;
		std::uint64_t m_total = 0;

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			spdlog::memory_buf_t formatted;
			formatter_->format(msg, formatted);
			std::string line(formatted.data(), formatted.size());
			while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
				line.pop_back();
			m_lines.push_back(std::move(line));
			if (m_lines.size() > kCapacity)
				m_lines.pop_front();
			++m_total;
		}
		void flush_() override {}

	public:
		// Appends the lines numbered `since` and later still kept to `out`;
		// returns the number the next line will get. Lines dropped from the
		// front before they were read are skipped.
		std::uint64_t CopySince(std::uint64_t since, std::vector<std::string>& out)
		{
			std::lock_guard lock(mutex_);
			const std::uint64_t first = m_total - m_lines.size();
			for (std::uint64_t i = (std::max)(since, first); i < m_total; ++i)
				out.push_back(m_lines[static_cast<size_t>(i - first)]);
			return m_total;
		}
	};

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

		inline const std::shared_ptr<RecentSink>& RecentLines()
		{
			static const std::shared_ptr<RecentSink> sink = []
			{
				auto made = std::make_shared<RecentSink>();
				made->set_pattern("[%H:%M:%S.%e] %v");
				return made;
			}();
			return sink;
		}

		inline const std::shared_ptr<spdlog::logger>& GetLogger()
		{
			static const std::shared_ptr<spdlog::logger> logger = []
			{
				std::shared_ptr<spdlog::logger> made;
				try
				{
					made = CreateLogger("Rampagio", LogFallback::ModuleDirectory(), L"Rampagio.log", LogFallback::FallbackDirectory());
					if (!made)
						made = std::make_shared<spdlog::logger>("Rampagio");
					made->sinks().push_back(RecentLines());
				}
				catch (...)
				{
				}
				return made;
			}();
			return logger;
		}
	}

	// The lines logged this session, newest last (Debug > Log).
	inline RecentSink& Recent() { return *detail::RecentLines(); }

	template <typename... Args>
	void Write(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		if (const auto& logger = detail::GetLogger())
			logger->info(fmt, std::forward<Args>(args)...);
	}
}
