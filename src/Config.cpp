// Same inipp-based load/rewrite pattern as ChallengeCheat's Config.cpp -- see
// Config.h's header comment.

#include "Config.h"
#include "KeyNames.h"
#include "Log.h"
#include "LogFallback.h"

#include "..\external\inipp\inipp\inipp.h"

#include <fstream>
#include <string>
#include <exception>

namespace
{
	using Section = inipp::Ini<char>::Section;

	Config::Values g_values;
	bool g_loaded = false;

	// Where Rampagio.ini is loaded from and saved to: next to the .asi, or
	// %LOCALAPPDATA%\RDR2ASIMods\Rampagio.ini when the game folder isn't
	// writable -- starting from the game folder's copy if there is one (see
	// LogFallback::ResolveSettings). Resolved once per session.
	const LogFallback::SettingsPaths& IniPaths()
	{
		static const LogFallback::SettingsPaths paths = LogFallback::ResolveSettings(
			LogFallback::ModuleDirectory(), L"Rampagio.ini", LogFallback::FallbackDirectory());
		return paths;
	}

	void ReloadImpl()
	{
		inipp::Ini<char> ini;
		{
			std::ifstream is(IniPaths().read);
			if (is)
				ini.parse(is);
		}

		const Config::Values defaults;
		Section& general = ini.sections["General"];

		g_values.MenuKey = defaults.MenuKey;
		auto it = general.find("MenuKey");
		if (it != general.end())
		{
			DWORD vk = 0;
			if (KeyNames::Parse(it->second, vk))
			{
				g_values.MenuKey = vk;
			}
			else
			{
				Log::Write("Config::Reload -- MenuKey '{}' is not a usable key name (unrecognized, or reserved for menu navigation) -- using {}",
					it->second, KeyNames::Format(defaults.MenuKey));
			}
		}
		general["MenuKey"] = KeyNames::Format(g_values.MenuKey);

		g_values.WrapWidth = defaults.WrapWidth;
		auto wrapIt = general.find("WrapWidth");
		if (wrapIt != general.end())
		{
			try
			{
				const int width = std::stoi(wrapIt->second);
				if (width == 0 || (width >= 10 && width <= 120))
					g_values.WrapWidth = width;
				else
					Log::Write("Config::Reload -- WrapWidth {} is not 0 or 10-120 -- using {}", width, defaults.WrapWidth);
			}
			catch (const std::exception&)
			{
				Log::Write("Config::Reload -- WrapWidth '{}' is not a number -- using {}", wrapIt->second, defaults.WrapWidth);
			}
		}
		general["WrapWidth"] = std::to_string(g_values.WrapWidth);

		if (IniPaths().usedFallback)
			Log::Write("Config::Reload -- the game folder isn't writable, so settings are saved to {}",
				LogFallback::ToUtf8(IniPaths().write));

		{
			std::ofstream os(IniPaths().write, std::ios::trunc);
			if (os)
				ini.generate(os);
			else
				Log::Write("Config::Reload -- failed to open Rampagio.ini for writing");
		}

		Log::Write("Config::Reload -- MenuKey={} (vk 0x{:02X})", general["MenuKey"], g_values.MenuKey);
	}
}

namespace Config
{
	void Reload()
	{
		try
		{
			ReloadImpl();
		}
		catch (const std::exception& e)
		{
			Log::Write("Config::Reload -- std::exception: {} -- keeping previous config values", e.what());
		}
		catch (...)
		{
			Log::Write("Config::Reload -- unknown non-std exception -- keeping previous config values");
		}

		g_loaded = true;
	}

	const Values& Get()
	{
		if (!g_loaded)
			Reload();

		return g_values;
	}
}
