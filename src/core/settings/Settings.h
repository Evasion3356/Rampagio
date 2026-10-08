/*
	Rampagio.json: one JSON object made of IStateSerializer components
	("general", "style", "themes", "commands", "hotkeys"), lifted from
	HorseMenu's Settings. Differences from HorseMenu:

	- Initialize takes the read and write paths (LogFallback::ResolveSettings
	  can read the game folder's copy and write to the fallback folder).
	- A missing file starts from {}; a corrupt one is copied to
	  "<file>.bad", logged, and also starts from {}.
	- A component added after the first load is loaded on the next Tick or
	  Flush (HorseMenu's queue, which it marked broken: it called the
	  public LoadComponent from inside TickImpl's lock). It can't load from
	  AddComponent, which runs inside IStateSerializer's constructor.
	- Tick writes at most once per WriteInterval (default 1 s), so holding
	  NUMPAD 6 on a number doesn't rewrite the file every frame. Flush
	  writes now. TryFlush (DllMain detach) only writes the JSON Tick last
	  built, so it doesn't read component state from another thread.
	- Unknown keys (from a newer build, or removed commands) stay in the
	  file: components write into the loaded object instead of replacing it.
	- The file is written to "<file>.tmp" and then moved over the old one.
*/

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Rampagio
{
	class IStateSerializer;

	class Settings
	{
		std::wstring m_ReadPath;
		std::wstring m_WritePath;
		std::vector<IStateSerializer*> m_StateSerializers;
		std::vector<IStateSerializer*> m_LateLoaders;
		std::unique_ptr<nlohmann::json> m_Json;
		bool m_InitialLoadDone = false;
		bool m_Unwritten = false; // m_Json has changes the file doesn't
		unsigned long long m_LastWrite = 0;
		unsigned long long m_WriteInterval = 1000;
		std::recursive_mutex m_Mutex;

		Settings();
		~Settings();

		void ReadFile(const std::wstring& path);
		void LoadComponentImpl(IStateSerializer* serializer);
		void LoadLateComponents();
		// Saves the dirty (or all) components into m_Json.
		void SnapshotImpl(bool all);
		// Writes m_Json to the file.
		bool WriteFileImpl();

	public:
		static Settings& GetInstance();

		// Reads the file and loads every registered component. writePath
		// empty = same as readPath.
		static void Initialize(const std::wstring& readPath, const std::wstring& writePath = {});
		// Saves the dirty components into the JSON, and writes the file if the
		// last write was at least WriteInterval ago. Call every frame, from
		// the thread that changes the components' state.
		static void Tick();
		// Saves every component and writes the file now. False if the file
		// couldn't be written.
		static bool Flush();
		// Writes the JSON the last Tick or Flush built, if the file doesn't
		// have it yet. Runs no component code, so it never reads feature state
		// from another thread (DllMain detach); gives up if another thread
		// holds the lock (one killed at process exit may have died holding it).
		static bool TryFlush();
		// Re-reads the file (from the write path once it exists) and loads
		// every component again.
		static void Reload();
		static void AddComponent(IStateSerializer* serializer);
		static void RemoveComponent(IStateSerializer* serializer);
		static bool InitialLoadDone();
		static const std::wstring& GetWritePath();
		static void SetWriteInterval(unsigned long long ms);
	};
}
