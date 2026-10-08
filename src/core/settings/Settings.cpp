#include "Settings.h"
#include "IStateSerializer.h"
#include "..\..\Log.h"
#include "..\..\LogFallback.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace Rampagio
{
	IStateSerializer::IStateSerializer(const std::string& name) :
	    m_SerComponentName(name),
	    m_IsDirty(false)
	{
		Settings::AddComponent(this);
	}

	Settings::Settings() :
	    m_Json(std::make_unique<nlohmann::json>(nlohmann::json::object()))
	{
	}

	Settings::~Settings() = default;

	Settings& Settings::GetInstance()
	{
		static Settings instance;
		return instance;
	}

	void Settings::ReadFile(const std::wstring& path)
	{
		*m_Json = nlohmann::json::object();
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			Log::Write("[Settings] No {} yet, starting from defaults", LogFallback::ToUtf8(path));
			return;
		}
		std::stringstream text;
		text << file.rdbuf();
		file.close();

		nlohmann::json parsed = nlohmann::json::parse(text.str(), nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object())
		{
			const std::wstring backup = path + L".bad";
			CopyFileW(path.c_str(), backup.c_str(), FALSE);
			Log::Write("[Settings] {} is corrupt, starting from defaults (old file kept as {})",
				LogFallback::ToUtf8(path), LogFallback::ToUtf8(backup));
			return;
		}
		*m_Json = std::move(parsed);
	}

	void Settings::LoadComponentImpl(IStateSerializer* serializer)
	{
		const std::string& name = serializer->GetSerializerComponentName();
		nlohmann::json& json = *m_Json;
		if (!json.contains(name) || !json[name].is_object())
			json[name] = nlohmann::json::object();
		try
		{
			serializer->LoadState(json[name]);
		}
		catch (const std::exception& e)
		{
			Log::Write("[Settings] Couldn't load \"{}\": {}", name, e.what());
		}
	}

	void Settings::LoadLateComponents()
	{
		std::vector<IStateSerializer*> late;
		late.swap(m_LateLoaders);
		for (IStateSerializer* serializer : late)
			LoadComponentImpl(serializer);
	}

	bool Settings::WriteImpl(bool all)
	{
		for (IStateSerializer* serializer : m_StateSerializers)
			if (all || serializer->IsStateDirty())
				serializer->SaveState((*m_Json)[serializer->GetSerializerComponentName()]);
		m_LastWrite = GetTickCount64();
		if (m_WritePath.empty())
			return false;

		const std::wstring temp = m_WritePath + L".tmp";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				Log::Write("[Settings] Couldn't write {}", LogFallback::ToUtf8(temp));
				return false;
			}
			file << m_Json->dump(4);
			if (!file)
				return false;
		}
		if (!MoveFileExW(temp.c_str(), m_WritePath.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			Log::Write("[Settings] Couldn't replace {} (error {})", LogFallback::ToUtf8(m_WritePath), GetLastError());
			return false;
		}
		return true;
	}

	void Settings::Initialize(const std::wstring& readPath, const std::wstring& writePath)
	{
		Settings& self = GetInstance();
		std::lock_guard lock(self.m_Mutex);
		self.m_ReadPath = readPath;
		self.m_WritePath = writePath.empty() ? readPath : writePath;
		self.ReadFile(readPath);
		self.m_LateLoaders.clear();
		for (IStateSerializer* serializer : self.m_StateSerializers)
			self.LoadComponentImpl(serializer);
		self.m_InitialLoadDone = true;
		Log::Write("[Settings] Loaded {} components from {}", self.m_StateSerializers.size(), LogFallback::ToUtf8(readPath));
	}

	void Settings::Tick()
	{
		Settings& self = GetInstance();
		std::lock_guard lock(self.m_Mutex);
		if (!self.m_InitialLoadDone)
			return;
		self.LoadLateComponents();
		if (GetTickCount64() - self.m_LastWrite < self.m_WriteInterval)
			return;
		if (std::any_of(self.m_StateSerializers.begin(), self.m_StateSerializers.end(), [](IStateSerializer* s) { return s->IsStateDirty(); }))
			self.WriteImpl(false);
	}

	bool Settings::Flush()
	{
		Settings& self = GetInstance();
		std::lock_guard lock(self.m_Mutex);
		if (!self.m_InitialLoadDone)
			return false;
		self.LoadLateComponents();
		return self.WriteImpl(true);
	}

	void Settings::Reload()
	{
		Settings& self = GetInstance();
		std::lock_guard lock(self.m_Mutex);
		self.ReadFile(LogFallback::FileExists(self.m_WritePath) ? self.m_WritePath : self.m_ReadPath);
		self.m_LateLoaders.clear();
		for (IStateSerializer* serializer : self.m_StateSerializers)
			self.LoadComponentImpl(serializer);
		self.m_InitialLoadDone = true;
	}

	void Settings::AddComponent(IStateSerializer* serializer)
	{
		Settings& self = GetInstance();
		std::lock_guard lock(self.m_Mutex);
		self.m_StateSerializers.push_back(serializer);
		// Not loaded here: this runs from IStateSerializer's constructor,
		// before the derived class (and its LoadStateImpl) exists.
		if (self.m_InitialLoadDone)
			self.m_LateLoaders.push_back(serializer);
	}

	void Settings::RemoveComponent(IStateSerializer* serializer)
	{
		Settings& self = GetInstance();
		std::lock_guard lock(self.m_Mutex);
		std::erase(self.m_StateSerializers, serializer);
		std::erase(self.m_LateLoaders, serializer);
	}

	bool Settings::InitialLoadDone()
	{
		return GetInstance().m_InitialLoadDone;
	}

	const std::wstring& Settings::GetWritePath()
	{
		return GetInstance().m_WritePath;
	}

	void Settings::SetWriteInterval(unsigned long long ms)
	{
		GetInstance().m_WriteInterval = ms;
	}
}
