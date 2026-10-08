/*
	World menu: ports Rampage's Submenus::SubWorld, SubWorldTime and
	SubWorldWeather rows; the other World submenus are in WorldSubmenus.cpp.
*/

#include "Menus.h"
#include "..\GameUtil.h"

#include <ctime>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Vector3 MyPos() { return ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE); }

	// --- main -----------------------------------------------------------

	void SetInteriorLights(bool off)
	{
		const Vector3 p = MyPos();
		const Interior interior = INTERIOR::GET_INTERIOR_AT_COORDS(p.x, p.y, p.z);
		if (INTERIOR::IS_VALID_INTERIOR(interior))
			GRAPHICS::_SET_PROXY_INTERIOR_INDEX_ARTIFICIAL_LIGHTS_STATE(GRAPHICS::_GET_PROXY_INTERIOR_INDEX(interior), !off);
	}

	// Fireworks: one looped "scr_ind1_firework" effect (the Saint Denis
	// industry mission's) kept running at the player.
	int g_firework = 0;
	void FireworkTick()
	{
		if (g_firework)
			return;
		AUDIO::REQUEST_SCRIPT_AUDIO_BANK("Industry_01");
		STREAMING::REQUEST_NAMED_PTFX_ASSET(GameUtil::Joaat("scr_ind1"));
		if (!STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(GameUtil::Joaat("scr_ind1")))
			return;
		const Vector3 p = MyPos();
		GRAPHICS::USE_PARTICLE_FX_ASSET("scr_ind1");
		g_firework = GRAPHICS::START_PARTICLE_FX_LOOPED_AT_COORD("scr_ind1_firework", p.x, p.y, p.z, 0.0f, 0.0f, 0.0f, 1.0f, FALSE, FALSE, FALSE, FALSE);
	}

	void FireworkOff()
	{
		if (g_firework)
		{
			GRAPHICS::STOP_PARTICLE_FX_LOOPED(g_firework, FALSE);
			GRAPHICS::REMOVE_PARTICLE_FX(g_firework, FALSE);
			g_firework = 0;
		}
		AUDIO::RELEASE_NAMED_SCRIPT_AUDIO_BANK("Industry_01");
		STREAMING::REMOVE_NAMED_PTFX_ASSET(GameUtil::Joaat("scr_ind1"));
	}

	// Every ped but the player and their horse.
	template <typename Fn>
	void ForOtherPeds(Fn fn)
	{
		const Ped me = Me();
		const Ped horse = GameUtil::PlayerHorse();
		for (Ped p : GameUtil::AllPeds())
			if (p != me && p != horse)
				fn(p);
	}

	float g_giantScale = 3.0f;
	void GiantTick() { ForOtherPeds([](Ped p) { PED::_SET_PED_SCALE(p, g_giantScale); }); }
	void GiantOff() { ForOtherPeds([](Ped p) { PED::_SET_PED_SCALE(p, 1.0f); }); }

	void SetGravity(bool has)
	{
		ENTITY::SET_ENTITY_HAS_GRAVITY(Me(), has);
		ForOtherPeds([has](Ped p) { ENTITY::SET_ENTITY_HAS_GRAVITY(p, has); });
	}

	float g_areaRadius = 50.0f;
	void ClearArea()
	{
		const Vector3 p = MyPos();
		MISC::CLEAR_AREA(p.x, p.y, p.z, g_areaRadius, 0x25438A);
		PERSISTENCE::PERSISTENCE_REMOVE_ALL_ENTITIES_IN_AREA(p.x, p.y, p.z, g_areaRadius);
	}

	void CleanArea()
	{
		const Vector3 p = MyPos();
		GRAPHICS::REMOVE_DECALS_IN_RANGE(p.x, p.y, p.z, g_areaRadius);
	}

	void DeleteAllPickups()
	{
		const Vector3 p = MyPos();
		MISC::CLEAR_AREA(p.x, p.y, p.z, 1000.0f, 2);
	}

	// Starts a game script by name if it isn't already running.
	std::string StartScript(const char* name, int stackSize)
	{
		if (SCRIPT::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(GameUtil::Joaat(name)) > 0)
			return std::string(name) + " is already running";
		if (!SCRIPT::DOES_SCRIPT_EXIST(name))
			return std::string(name) + " doesn't exist";
		SCRIPT::REQUEST_SCRIPT(name);
		for (int i = 0; i < 300 && !SCRIPT::HAS_SCRIPT_LOADED(name); i++)
			WAIT(10);
		if (!SCRIPT::HAS_SCRIPT_LOADED(name))
			return std::string(name) + " didn't load";
		SCRIPT::START_NEW_SCRIPT(name, stackSize);
		SCRIPT::SET_SCRIPT_AS_NO_LONGER_NEEDED(name);
		return {};
	}

	std::string QuickCamp()
	{
		if (MISC::GET_MISSION_FLAG())
			return "Not during a mission";
		return StartScript("player_camp", 6096);
	}

	// --- time -----------------------------------------------------------

	int g_hour = 12, g_minute = 0, g_second = 0;
	int g_day = 1, g_month = 1, g_year = 1899;
	int g_msPerMinute = 2000;
	float g_timeScale = 1.0f;

	void ApplyTime() { CLOCK::SET_CLOCK_TIME(g_hour, g_minute, g_second); }
	void ApplyDate() { CLOCK::SET_CLOCK_DATE(g_day, g_month - 1, g_year); }

	void SetTimeOfDay(int hour, int minute)
	{
		g_hour = hour;
		g_minute = minute;
		g_second = 0;
		ApplyTime();
	}

	void SyncSystemTimeTick()
	{
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);
		CLOCK::SET_CLOCK_TIME(local.tm_hour, local.tm_min, local.tm_sec);
		CLOCK::SET_CLOCK_DATE(local.tm_mday, local.tm_mon, local.tm_year + 1900);
	}

	// Timelapse: advance the clock a minute every N ms of real time.
	int g_timelapseSpeed = 1; // 0 slow, 1 normal, 2 fast
	DWORD g_timelapseNext = 0;
	void TimelapseTick()
	{
		constexpr DWORD kStep[] = { 100, 30, 5 };
		const DWORD now = GetTickCount();
		if (now < g_timelapseNext)
			return;
		g_timelapseNext = now + kStep[g_timelapseSpeed];
		CLOCK::ADD_TO_CLOCK_TIME(0, 1, 0);
	}

	// --- weather --------------------------------------------------------

	struct Weather { const char* label; const char* name; };
	constexpr Weather kWeathers[] = {
		{ "Sunny", "SUNNY" }, { "Misty", "MISTY" }, { "Fog", "FOG" }, { "Clouds", "CLOUDS" },
		{ "Overcast", "OVERCAST" }, { "Overcast Dark", "OVERCASTDARK" }, { "Drizzle", "DRIZZLE" },
		{ "Rain", "RAIN" }, { "Thunder", "THUNDER" }, { "Thunderstorm", "THUNDERSTORM" },
		{ "High Pressure", "HIGHPRESSURE" }, { "Hurricane", "HURRICANE" }, { "Shower", "SHOWER" },
		{ "Hail", "HAIL" }, { "Sleet", "SLEET" }, { "Light Snow", "SNOWLIGHT" }, { "Blizzard", "BLIZZARD" },
		{ "Snow", "SNOW" }, { "Ground Blizzard", "GROUNDBLIZZARD" }, { "Whiteout", "WHITEOUT" },
		{ "Sandstorm", "SANDSTORM" }, { "Snow Clearing", "SNOWCLEARING" },
	};

	bool g_smoothWeather = false;
	bool g_freezeWeather = false;
	void SetWeather(Hash weather)
	{
		MISC::CLEAR_OVERRIDE_WEATHER();
		MISC::CLEAR_WEATHER_TYPE_PERSIST();
		MISC::SET_WEATHER_TYPE(weather, TRUE, TRUE, TRUE, g_smoothWeather ? 15.0f : 0.0f, FALSE);
		MISC::_SET_OVERRIDE_WEATHER(weather);
		MISC::_SET_WEATHER_TYPE_FROZEN(g_freezeWeather);
	}

	void ResetWeather()
	{
		MISC::CLEAR_WEATHER_TYPE_PERSIST();
		MISC::CLEAR_OVERRIDE_WEATHER();
		MISC::SET_WIND_SPEED(0.0f);
		SetWeather(GameUtil::Joaat("SUNNY"));
		MISC::_SET_WEATHER_TYPE_FROZEN(FALSE);
	}

	float g_wind = 0.0f, g_rain = 0.0f, g_snow = 0.0f, g_moon = 0.0f;

	void SetHalloween(bool on)
	{
		const char* types[] = { "FOG", "MISTY", "SHOWER", "THUNDERSTORM" };
		const char* variations[] = { "fog_MP_Pred", "misty_MP_Pred", "shower_MP_Pred", "thunderstorm_MP_Pred" };
		MISC::CLEAR_OVERRIDE_WEATHER();
		for (int i = 0; i < 4; i++)
		{
			if (on)
				MISC::_SET_WEATHER_VARIATION(types[i], variations[i]);
			else
				MISC::_CLEAR_WEATHER_VARIATION(types[i], TRUE);
		}
	}

	void SetSnowyMap(bool on)
	{
		const Hash snow = GameUtil::Joaat("SNOW");
		MISC::CLEAR_OVERRIDE_WEATHER();
		if (on)
		{
			MISC::SET_WEATHER_TYPE(snow, TRUE, TRUE, FALSE, 0.0f, FALSE);
			MISC::_SET_OVERRIDE_WEATHER(snow);
			MISC::_SET_WEATHER_TYPE_FROZEN(TRUE);
			GRAPHICS::_SET_SNOW_COVERAGE_TYPE(3);
			MISC::_SET_SNOW_LEVEL(100.0f);
		}
		else
		{
			MISC::_SET_SNOW_LEVEL(0.0f);
			MISC::CLEAR_WEATHER_TYPE_PERSIST();
			MISC::_SET_WEATHER_TYPE_FROZEN(FALSE);
			GRAPHICS::_SET_SNOW_COVERAGE_TYPE(0);
		}
	}

	int g_coverage = 0;
}

namespace Menus
{
	void BuildWorld(MenuBase* root)
	{
		MenuBase* world = Ui::Submenu(root, "World");

		MenuBase* time = Ui::Submenu(world, "Time");
		Ui::Number(time, "Hour", &g_hour, 0, 23, 1, ApplyTime);
		Ui::Number(time, "Minute", &g_minute, 0, 59, 1, ApplyTime);
		Ui::Number(time, "Second", &g_second, 0, 59, 1, ApplyTime);
		Ui::Number(time, "Day", &g_day, 1, 31, 1, ApplyDate);
		Ui::Number(time, "Month", &g_month, 1, 12, 1, ApplyDate);
		Ui::Number(time, "Year", &g_year, 1800, 1950, 1, ApplyDate);
		Ui::Number(time, "Game Minute Milliseconds", &g_msPerMinute, 100, 60000, 100, [] { CLOCK::_SET_MILLISECONDS_PER_GAME_MINUTE(g_msPerMinute); });
		Ui::Looped(time, "Stop Time", [] { CLOCK::_PAUSE_CLOCK_THIS_FRAME(TRUE); });
		Ui::Number(time, "Time Scale", &g_timeScale, 0.0f, 1.0f, 0.05f, [] { MISC::SET_TIME_SCALE(g_timeScale); });
		Ui::Do(time, "Sunrise", [] { SetTimeOfDay(6, 1); });
		Ui::Do(time, "Midday", [] { SetTimeOfDay(12, 15); });
		Ui::Do(time, "Sunset", [] { SetTimeOfDay(19, 19); });
		Ui::Do(time, "Midnight", [] { SetTimeOfDay(23, 23); });
		Ui::Do(time, "Add 1 Hour", [] { CLOCK::ADD_TO_CLOCK_TIME(1, 0, 0); });
		Ui::Looped(time, "Sync System Time", SyncSystemTimeTick);
		Ui::Section(time, "Timelapse");
		Ui::Choice(time, "Progression", { "Slow", "Normal", "Fast" }, &g_timelapseSpeed);
		Ui::Looped(time, "Enable Timelapse", TimelapseTick);

		MenuBase* weather = Ui::Submenu(world, "Weather");
		Ui::Do(weather, "Reset Weather", ResetWeather);
		Ui::Do(weather, "Randomize", [] { MISC::CLEAR_WEATHER_TYPE_PERSIST(); MISC::SET_RANDOM_WEATHER_TYPE(FALSE, TRUE); });
		Ui::Toggle(weather, "Smooth Transition", [](bool on) { g_smoothWeather = on; });
		Ui::Toggle(weather, "Freeze", [](bool on) { g_freezeWeather = on; MISC::_SET_WEATHER_TYPE_FROZEN(on); });
		Ui::Section(weather, "Weather Type");
		for (const Weather& w : kWeathers)
		{
			const Hash hash = GameUtil::Joaat(w.name);
			Ui::Do(weather, w.label, [hash] { SetWeather(hash); });
		}
		Ui::Section(weather, "Levels");
		Ui::Number(weather, "Wind Speed", &g_wind, 0.0f, 50.0f, 0.5f, [] { MISC::SET_WIND_SPEED(g_wind); });
		Ui::Number(weather, "Rain Level", &g_rain, 0.0f, 1.0f, 0.05f, [] { MISC::SET_RAIN(g_rain); });
		Ui::Number(weather, "Snow Level", &g_snow, 0.0f, 1.0f, 0.05f, [] { MISC::_SET_SNOW_LEVEL(g_snow); });
		Ui::Number(weather, "Moonlight", &g_moon, 0.0f, 1.0f, 0.05f, [] { GRAPHICS::ENABLE_MOON_CYCLE_OVERRIDE(g_moon); });
		Ui::Section(weather, "Extras");
		Ui::Do(weather, "Lightning Flash", [] { MISC::FORCE_LIGHTNING_FLASH(); });
		Ui::Toggle(weather, "Guarma Horizon", [](bool on) { STREAMING::_SET_GUARMA_WORLDHORIZON_ACTIVE(on); });
		Ui::Toggle(weather, "Halloween Override", SetHalloween);
		Ui::Toggle(weather, "Snowy Map", SetSnowyMap);
		Ui::Choice(weather, "Coverage Type", { "0", "1", "2", "3" }, &g_coverage, [](int i) { GRAPHICS::_SET_SNOW_COVERAGE_TYPE(i); });

		BuildWorldSubmenus(world);

		Ui::Section(world, "Toggles");
		Ui::Toggle(world, "Disable Interior Lightning", SetInteriorLights);
		Ui::Toggle(world, "Disable Distant Lights", [](bool on) { GRAPHICS::_DISABLE_FAR_ARTIFICIAL_LIGHTS(on); });
		Ui::Looped(world, "Firework Mode", FireworkTick, FireworkOff);
		Ui::Looped(world, "Giant Mode", GiantTick, GiantOff);
		Ui::Number(world, "Giant Scale", &g_giantScale, 1.0f, 10.0f, 0.5f);
		Ui::Toggle(world, "No Gravity", [](bool on) { SetGravity(!on); });

		Ui::Section(world, "Actions");
		Ui::Do(world, "Populate Area", [] { MISC::POPULATE_NOW(); PED::INSTANTLY_FILL_PED_POPULATION(); });
		Ui::Number(world, "Area Scale", &g_areaRadius, 5.0f, 1000.0f, 5.0f);
		Ui::Do(world, "Clear Area", ClearArea);
		Ui::Do(world, "Clean Area", CleanArea);
		Ui::Do(world, "Delete All Trains", [] { VEHICLE::DELETE_ALL_TRAINS(); });
		Ui::Do(world, "Delete All Pickups", DeleteAllPickups);
		Ui::Action(world, "Door Unlocker", [] {
			std::string text;
			if (!GameUtil::PromptText("Enter Door Hash", text) || text.empty())
				return std::string();
			OBJECT::DOOR_SYSTEM_SET_DOOR_STATE(GameUtil::ParseHash(text), 0);
			return std::string();
		});
		Ui::Action(world, "Quick Camp", QuickCamp);
	}
}
