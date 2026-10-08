/*
	Work posted from other threads (the overlay's render and window threads)
	to run on the script thread, where natives and game state are safe.
	script.cpp's loop calls Run once per frame. A job that has to wait for
	something (a script to load) posts itself again; it runs next frame.
*/

#pragma once

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace MainThread
{
	namespace detail
	{
		inline std::mutex g_mutex;
		inline std::vector<std::function<void()>> g_jobs;
	}

	inline void Post(std::function<void()> job)
	{
		std::lock_guard lock(detail::g_mutex);
		detail::g_jobs.push_back(std::move(job));
	}

	// Runs the jobs posted before this call (jobs they post wait a frame).
	inline void Run()
	{
		std::vector<std::function<void()>> jobs;
		{
			std::lock_guard lock(detail::g_mutex);
			jobs.swap(detail::g_jobs);
		}
		for (auto& job : jobs)
			job();
	}

	// Drops pending jobs (online kill switch).
	inline void Clear()
	{
		std::lock_guard lock(detail::g_mutex);
		detail::g_jobs.clear();
	}
}
