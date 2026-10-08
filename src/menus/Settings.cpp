/*
	Settings: ports Rampage's Submenus::SubSettings and the parts of its
	submenus that have a Rampagio counterpart: SubSettingsCore,
	SubSettingsLoadSave, SubSettingsColor with SubSettingsPremadeThemes and
	SubSettingsCustomThemes, SubOverlaySettings, SubSettingsXUI's Max
	Display Options, plus Search and the Hotkey Manager. The About page is
	Rampagio's own (SubAbout).

	Saved data, all next to Rampagio.ini:
	- Rampagio_Settings.ini: the menu style (MenuStyle), toggle saving
	  options and hotkeys.
	- Rampagio_Toggles.ini: which toggles are on, by "Menu > Caption".
	- Rampagio_Themes.ini: saved custom themes.

	Hotkeys (ours): highlight any row and press F11, then the key to bind;
	the key then selects that row with the menu closed. The premade themes
	are our own presets, not Rampage's 26.

	Not ported: Rampage's plugins, language files, ImGui windows (Window
	Manager), fonts, teleport map and spawner previews, welcome/ToS/update
	screens and mouse control (still to come, see CLAUDE.md).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\DataFile.h"
#include "..\KeyNames.h"
#include "..\keyboard.h"
#include "..\Log.h"

#include <chrono>
#include <ctime>
#include <format>
#include <map>

namespace
{
	const std::wstring kSettingsFile = L"Rampagio_Settings.ini";
	const std::wstring kTogglesFile = L"Rampagio_Toggles.ini";
	const std::wstring kThemesFile = L"Rampagio_Themes.ini";

	constexpr DWORD kBindKey = VK_F11;

	bool g_toggleSaving = false;
	bool g_autoSaveToggles = false;
	std::map<std::string, DWORD> g_hotkeys; // row key -> virtual key

	// --- style <-> ini ---------------------------------------------------------------

	struct NamedColor
	{
		const char* name;
		ColorRgba MenuStyle::* field;
	};
	const NamedColor kColors[] = {
		{ "Title Background", &MenuStyle::titleRect },
		{ "Title Text", &MenuStyle::titleText },
		{ "Row Background", &MenuStyle::itemRect },
		{ "Row Text", &MenuStyle::itemText },
		{ "Selected Text", &MenuStyle::itemTextActive },
		{ "Selection Border", &MenuStyle::border },
		{ "Section Background", &MenuStyle::sectionRect },
		{ "Section Text", &MenuStyle::sectionText },
	};

	std::string ColorString(ColorRgba c) { return std::format("{},{},{},{}", c.r, c.g, c.b, c.a); }

	bool ParseColor(const std::string& text, ColorRgba& out)
	{
		int v[4];
		if (sscanf_s(text.c_str(), "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) != 4)
			return false;
		out = { static_cast<unsigned char>(v[0]), static_cast<unsigned char>(v[1]), static_cast<unsigned char>(v[2]), static_cast<unsigned char>(v[3]) };
		return true;
	}

	void WriteColors(DataFile::Ini::Section& sec, const MenuStyle& style)
	{
		for (const NamedColor& c : kColors)
			sec[c.name] = ColorString(style.*c.field);
	}

	void ReadColors(DataFile::Ini::Section& sec, MenuStyle& style)
	{
		for (const NamedColor& c : kColors)
			if (auto it = sec.find(c.name); it != sec.end())
				ParseColor(it->second, style.*c.field);
	}

	template <typename T>
	void ReadValue(DataFile::Ini::Section& sec, const char* key, T& value)
	{
		if (auto it = sec.find(key); it != sec.end())
			try
			{
				if constexpr (std::is_same_v<T, bool>) value = it->second == "1";
				else if constexpr (std::is_floating_point_v<T>) value = std::stof(it->second);
				else value = std::stoi(it->second);
			}
			catch (const std::exception&) {}
	}

	MenuItemToggle* g_soundsToggle = nullptr;
	MenuItemToggle* g_gamepadToggle = nullptr;
	MenuItemToggle* g_toggleSavingToggle = nullptr;
	MenuItemToggle* g_autoSaveToggle = nullptr;

	// Shows the loaded options on their toggles without running them.
	void SyncSettingToggles()
	{
		if (g_soundsToggle) g_soundsToggle->SetState(Style().sounds);
		if (g_gamepadToggle) g_gamepadToggle->SetState(Style().gamepad);
		if (g_toggleSavingToggle) g_toggleSavingToggle->SetState(g_toggleSaving);
		if (g_autoSaveToggle) g_autoSaveToggle->SetState(g_autoSaveToggles);
	}

	std::string SaveSettings()
	{
		DataFile::Ini ini;
		MenuStyle& style = Style();
		auto& s = ini.sections["Style"];
		WriteColors(s, style);
		s["Left"] = std::format("{:.3f}", style.left);
		s["Top"] = std::format("{:.3f}", style.top);
		s["Rows"] = std::to_string(style.linesPerScreen);
		s["Sounds"] = style.sounds ? "1" : "0";
		s["Gamepad"] = style.gamepad ? "1" : "0";
		s["GamepadOpen"] = std::to_string(style.gamepadOpen);
		auto& t = ini.sections["Toggles"];
		t["Enable"] = g_toggleSaving ? "1" : "0";
		t["AutoSave"] = g_autoSaveToggles ? "1" : "0";
		auto& h = ini.sections["Hotkeys"];
		for (const auto& [row, vk] : g_hotkeys)
			h[row] = KeyNames::Format(vk);
		return DataFile::Save(kSettingsFile, ini) ? "Settings saved" : "Couldn't write Rampagio_Settings.ini";
	}

	std::string LoadSettingsFile()
	{
		DataFile::Ini ini = DataFile::Load(kSettingsFile);
		if (ini.sections.empty())
			return "No saved settings";
		MenuStyle& style = Style();
		auto& s = ini.sections["Style"];
		ReadColors(s, style);
		ReadValue(s, "Left", style.left);
		ReadValue(s, "Top", style.top);
		ReadValue(s, "Rows", style.linesPerScreen);
		ReadValue(s, "Sounds", style.sounds);
		ReadValue(s, "Gamepad", style.gamepad);
		ReadValue(s, "GamepadOpen", style.gamepadOpen);
		style.linesPerScreen = std::clamp(style.linesPerScreen, 3, 25);
		auto& t = ini.sections["Toggles"];
		ReadValue(t, "Enable", g_toggleSaving);
		ReadValue(t, "AutoSave", g_autoSaveToggles);
		g_hotkeys.clear();
		for (const auto& [row, name] : ini.sections["Hotkeys"])
		{
			DWORD vk = 0;
			if (KeyNames::Parse(name, vk))
				g_hotkeys[row] = vk;
		}
		SyncSettingToggles();
		return "Settings loaded";
	}

	// --- toggles -------------------------------------------------------------------------

	std::map<std::string, bool> ToggleSnapshot()
	{
		std::map<std::string, bool> on;
		for (MenuItemToggle* t : Ui::AllToggles())
			if (t->Persist() && t != g_toggleSavingToggle && t != g_autoSaveToggle && t != g_soundsToggle && t != g_gamepadToggle)
				on[Ui::Key(t)] = t->GetState();
		return on;
	}

	std::string SaveToggles()
	{
		DataFile::Ini ini;
		auto& sec = ini.sections["Toggles"];
		for (const auto& [key, on] : ToggleSnapshot())
			if (on)
				sec[key] = "1";
		return DataFile::Save(kTogglesFile, ini) ? std::format("Saved {} toggles", sec.size()) : "Couldn't write Rampagio_Toggles.ini";
	}

	std::string LoadToggles()
	{
		DataFile::Ini ini = DataFile::Load(kTogglesFile);
		int count = 0;
		for (const auto& [key, value] : ini.sections["Toggles"])
		{
			if (value != "1")
				continue;
			for (MenuItemToggle* t : Ui::AllToggles())
				if (Ui::Key(t) == key)
				{
					t->SetOn();
					++count;
					break;
				}
		}
		return std::format("Turned on {} toggles", count);
	}

	// --- themes ----------------------------------------------------------------------------

	MenuStyle Preset(ColorRgba title, ColorRgba row, ColorRgba text, ColorRgba border, ColorRgba section)
	{
		MenuStyle s = Style(); // keeps position and input options
		s.titleRect = title;
		s.itemRect = row;
		s.itemText = { text.r, text.g, text.b, 200 };
		s.itemTextActive = text;
		s.border = border;
		s.sectionText = section;
		s.sectionRect = { static_cast<unsigned char>(row.r / 2), static_cast<unsigned char>(row.g / 2), static_cast<unsigned char>(row.b / 2), 200 };
		return s;
	}

	void ApplyColors(const MenuStyle& from)
	{
		for (const NamedColor& c : kColors)
			Style().*c.field = from.*c.field;
	}

	void BuildThemes(MenuBase* theme)
	{
		MenuBase* premade = Ui::Submenu(theme, "Premade Themes");
		const ColorRgba white{ 255, 255, 255, 255 };
		Ui::Do(premade, "Rampagio", [] { ApplyColors(MenuStyle{}); });
		Ui::Do(premade, "Blood Red", [white] { ApplyColors(Preset({ 120, 0, 0, 230 }, { 30, 10, 10, 190 }, white, { 230, 30, 30, 255 }, { 230, 120, 120, 255 })); });
		Ui::Do(premade, "Saint Denis Gold", [white] { ApplyColors(Preset({ 20, 15, 5, 235 }, { 45, 38, 25, 190 }, { 255, 235, 190, 255 }, { 212, 175, 55, 255 }, { 212, 175, 55, 255 })); });
		Ui::Do(premade, "Lagras Swamp", [white] { ApplyColors(Preset({ 20, 45, 30, 230 }, { 25, 40, 30, 185 }, white, { 110, 190, 90, 255 }, { 150, 210, 140, 255 })); });
		Ui::Do(premade, "Guarma Sea", [white] { ApplyColors(Preset({ 0, 50, 90, 230 }, { 15, 35, 55, 185 }, white, { 0, 170, 220, 255 }, { 120, 200, 240, 255 })); });
		Ui::Do(premade, "Ghost Train", [] { ApplyColors(Preset({ 220, 220, 220, 230 }, { 235, 235, 235, 200 }, { 20, 20, 20, 255 }, { 80, 80, 80, 255 }, { 60, 60, 60, 255 })); });
		Ui::Do(premade, "Night Folk", [white] { ApplyColors(Preset({ 10, 10, 10, 240 }, { 15, 15, 15, 200 }, { 200, 200, 200, 255 }, { 120, 0, 160, 255 }, { 170, 110, 210, 255 })); });

		Ui::ListMenu(theme, "Custom Themes", [](MenuBase* menu)
		{
			Ui::Action(menu, "Save Current", []() -> std::string
			{
				std::string name;
				if (!GameUtil::PromptText("Theme Name:", name) || name.empty())
					return "";
				DataFile::Ini ini = DataFile::Load(kThemesFile);
				WriteColors(ini.sections[name], Style());
				Ui::Controller().ReopenActiveLater();
				return DataFile::Save(kThemesFile, ini) ? "Saved " + name : "Couldn't write Rampagio_Themes.ini";
			});
			DataFile::Ini ini = DataFile::Load(kThemesFile);
			if (ini.sections.empty())
				Ui::Section(menu, "No Themes found");
			for (const auto& [name, sec] : ini.sections)
				Ui::Do(menu, name, [name]
				{
					DataFile::Ini file = DataFile::Load(kThemesFile);
					ReadColors(file.sections[name], Style());
				});
		});

		Ui::Section(theme, "Customize");
		static MenuBase* colorMenu = nullptr;
		static ColorRgba MenuStyle::* editing = nullptr;
		static int rgba[4];
		colorMenu = Ui::DetachedListMenu("Color", [](MenuBase* menu)
		{
			ColorRgba& c = Style().*editing;
			rgba[0] = c.r; rgba[1] = c.g; rgba[2] = c.b; rgba[3] = c.a;
			auto write = [] { ColorRgba& d = Style().*editing; d = { (unsigned char)rgba[0], (unsigned char)rgba[1], (unsigned char)rgba[2], (unsigned char)rgba[3] }; };
			Ui::Number(menu, "Red", &rgba[0], 0, 255, 5, write);
			Ui::Number(menu, "Green", &rgba[1], 0, 255, 5, write);
			Ui::Number(menu, "Blue", &rgba[2], 0, 255, 5, write);
			Ui::Number(menu, "Alpha", &rgba[3], 0, 255, 5, write);
		});
		for (const NamedColor& c : kColors)
		{
			auto field = c.field;
			Ui::Do(theme, c.name, [field] { editing = field; Ui::Push(colorMenu); });
		}
		Ui::Section(theme, "Position");
		Ui::Number(theme, "Menu X", &Style().left, 0.0f, 0.78f, 0.01f);
		Ui::Number(theme, "Menu Y", &Style().top, 0.0f, 0.5f, 0.01f);
		Ui::Number(theme, "Max Display Options", &Style().linesPerScreen, 3, 25, 1);
	}

	// --- search ----------------------------------------------------------------------------

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	void BuildSearch(MenuBase* results)
	{
		std::string text;
		if (!GameUtil::PromptText("Search:", text) || text.empty())
			return;
		text = Lower(text);
		int found = 0;
		for (MenuBase* menu : Ui::Controller().GetMenus())
		{
			if (menu == results)
				continue;
			const auto& items = menu->GetItems();
			for (size_t i = 0; i < items.size() && found < 200; ++i)
			{
				MenuItemBase* item = items[i];
				const std::string caption = item->GetCaption();
				if (caption.empty() || Lower(caption).find(text) == std::string::npos)
					continue;
				++found;
				const int index = static_cast<int>(i);
				Ui::Do(results, Ui::Key(item), [menu, index]
				{
					Ui::Controller().PushMenu(menu);
					menu->SetActiveItemIndex(index);
				});
			}
		}
		if (!found)
			Ui::Section(results, "No matches (lists only count once opened)");
	}

	// --- hotkeys ---------------------------------------------------------------------------

	std::string g_bindingRow; // row waiting for its key

	bool IsNavigationKey(DWORD vk)
	{
		return vk == VK_NUMPAD0 || vk == VK_NUMPAD2 || vk == VK_NUMPAD4 || vk == VK_NUMPAD5 || vk == VK_NUMPAD6 || vk == VK_NUMPAD8
			|| vk == VK_BACK || vk == VK_RETURN || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL
			|| vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == kBindKey || vk == Config::Get().MenuKey;
	}

	void HotkeyTick()
	{
		MenuController& menus = Ui::Controller();
		if (!g_bindingRow.empty())
		{
			if (IsKeyJustUp(VK_ESCAPE))
			{
				g_bindingRow.clear();
				menus.SetStatusText("Hotkey cancelled", 2000);
				return;
			}
			for (DWORD vk = 0x08; vk < 0xFF; ++vk)
				if (!IsNavigationKey(vk) && vk != VK_ESCAPE && IsKeyJustUp(vk, false))
				{
					std::erase_if(g_hotkeys, [vk](const auto& kv) { return kv.second == vk; });
					g_hotkeys[g_bindingRow] = vk;
					menus.SetStatusText(std::format("{} bound to {}", KeyNames::Format(vk), g_bindingRow), 3000);
					g_bindingRow.clear();
					SaveSettings();
					return;
				}
			return;
		}
		if (menus.HasActiveMenu())
		{
			if (IsKeyJustUp(kBindKey))
				if (MenuBase* menu = menus.GetTopMenu())
				{
					const int index = menu->GetActiveItemIndex();
					const auto& items = menu->GetItems();
					if (index >= 0 && index < static_cast<int>(items.size()))
					{
						g_bindingRow = Ui::Key(items[index]);
						menus.SetStatusText("Press a key for " + g_bindingRow + " (Esc cancels)", 10000);
					}
				}
			return;
		}
		for (const auto& [row, vk] : g_hotkeys)
			if (IsKeyJustUp(vk))
			{
				if (MenuItemBase* item = Ui::Find(row))
				{
					item->OnSelect();
					if (auto* t = dynamic_cast<MenuItemSwitchable*>(item))
						menus.SetStatusText(row + (t->GetState() ? ": on" : ": off"), 1500);
				}
				else
					menus.SetStatusText(row + " isn't built yet (open its menu once)", 2500);
				break;
			}
	}

	// --- overlays --------------------------------------------------------------------------

	std::vector<std::string> g_overlayLines;
	int g_fps = 0;
	int g_minimapZoom = 0;

	void Overlay(std::string line) { g_overlayLines.push_back(std::move(line)); }

	const char* const kDays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

	void RealTimeOverlay()
	{
		const std::time_t now = std::time(nullptr);
		std::tm tm{};
		localtime_s(&tm, &now);
		Overlay(std::format("{} {:02}:{:02}", kDays[tm.tm_wday], tm.tm_hour, tm.tm_min));
	}

	void GameTimeOverlay()
	{
		const int day = CLOCK::GET_CLOCK_DAY_OF_WEEK();
		Overlay(std::format("{} {:02}:{:02}", kDays[(day % 7 + 7) % 7], CLOCK::GET_CLOCK_HOURS(), CLOCK::GET_CLOCK_MINUTES()));
	}

	void CoordsOverlay()
	{
		const Ped me = PLAYER::PLAYER_PED_ID();
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(me, TRUE, FALSE);
		Overlay(std::format("X: {:.3f}  Y: {:.3f}  Z: {:.3f}  Heading: {:.1f}", p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(me)));
	}

	void TemperatureOverlay()
	{
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), FALSE, FALSE);
		const float c = MISC::_GET_TEMPERATURE_AT_COORDS(p.x, p.y, p.z);
		Overlay(MISC::_SHOULD_USE_METRIC_TEMPERATURE() ? std::format("{} °C", static_cast<int>(c)) : std::format("{} °F", static_cast<int>(c * 1.8f + 32.0f)));
	}

	void WaypointOverlay()
	{
		if (!MAP::IS_WAYPOINT_ACTIVE())
			return;
		const Vector3 w = MAP::_GET_WAYPOINT_COORDS();
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), FALSE, FALSE);
		const float d = MISC::GET_DISTANCE_BETWEEN_COORDS(p.x, p.y, p.z, w.x, w.y, p.z, FALSE);
		Overlay(d >= 1000.0f ? std::format("Waypoint: {:.2f} km", d / 1000.0f) : std::format("Waypoint: {} m", static_cast<int>(d)));
	}

	constexpr Hash HUD_CTX_HONOR_SHOW = 0x074132EF; // Rampage's Always Show Honor context

	void BuildOverlays(MenuBase* settings)
	{
		MenuBase* o = Ui::Submenu(settings, "Overlay Settings");
		Ui::Looped(o, "Display FPS", [] { Overlay(std::format("{} FPS", g_fps)); });
		Ui::Looped(o, "Display Day & Time (Real time)", RealTimeOverlay);
		Ui::Looped(o, "Display Time (Ingame)", GameTimeOverlay);
		Ui::Looped(o, "Display Coordinates", CoordsOverlay);
		Ui::Looped(o, "Display Temperature", TemperatureOverlay);
		Ui::Looped(o, "Display Waypoint Distance", WaypointOverlay);
		Ui::Toggle(o, "Always Show Player Cores", [](bool on) { HUD::_SHOW_PLAYER_CORES(on); });
		Ui::Toggle(o, "Always Show Horse Cores", [](bool on) { HUD::_SHOW_HORSE_CORES(on); });
		Ui::Looped(o, "Always Show Honor", [] { HUD::_ENABLE_HUD_CONTEXT_THIS_FRAME(HUD_CTX_HONOR_SHOW); });
		Ui::Looped(o, "Hide HUD", [] { HUD::HIDE_HUD_AND_RADAR_THIS_FRAME(); });
		Ui::Toggle(o, "Hide Radar", [](bool on) { MAP::DISPLAY_RADAR(!on); });
		Ui::Looped(o, "Hide POIs", [] { MAP::_HIDE_ACTIVE_POINTS_OF_INTEREST(); });
		Ui::Looped(o, "No Menu Slow-Down", [] { HUD::_DISABLE_REDUCED_MENU_TIME_SCALE(); });
		Ui::Looped(o, "Remove All Screen Effects", [] { GRAPHICS::ANIMPOSTFX_STOP_ALL(); });
		Ui::Looped(o, "Disable Notifications", [] { UIFEED::_UI_FEED_CLEAR_ALL_CHANNELS(); });
		Ui::Looped(o, "Disable Help Text", [] { HUD::CLEAR_ALL_HELP_MESSAGES(); });
		Ui::Number(o, "Minimap Zoom", &g_minimapZoom, 0, 1400, 50, [] { MAP::SET_RADAR_ZOOM(g_minimapZoom); });
	}

	void DrawOverlays()
	{
		static DWORD windowStart = GetTickCount();
		static int frames = 0;
		++frames;
		if (GetTickCount() - windowStart >= 500)
		{
			g_fps = frames * 2;
			frames = 0;
			windowStart = GetTickCount();
		}
		float y = 0.01f;
		for (const std::string& line : g_overlayLines)
		{
			DrawTextAt(0.01f, y, line.c_str(), 22, ColorRgba{ 255, 255, 255, 230 });
			y += 0.027f;
		}
		g_overlayLines.clear();
	}

	// --- about -----------------------------------------------------------------------------

	void BuildAbout(MenuBase* settings)
	{
		MenuBase* about = Ui::Submenu(settings, "About Rampagio");
		Ui::Action(about, "Rampagio", [] { return std::string("Singleplayer trainer for RDR2 build 1491.50"); });
		Ui::Action(about, "Built " __DATE__, [] { return std::string(__DATE__ " " __TIME__); });
		Ui::Section(about, "Thanks to");
		Ui::Action(about, "alloc8or", [] { return std::string("Native database (natives.h)"); });
		Ui::Action(about, "Alexander Blade", [] { return std::string("ScriptHookRDR2 and its SDK"); });
		Ui::Action(about, "HorseMenu", [] { return std::string("Script function caller, native hooks, pointers"); });
		Ui::Action(about, "Halen84", [] { return std::string("RDR3 native flags and enums"); });
		Ui::Section(about, "Libraries used");
		Ui::Action(about, "MinHook", [] { return std::string("Hooking library (Tsuda Kageyu)"); });
		Ui::Action(about, "inipp", [] { return std::string("INI parsing (Matthias C. M. Troffaes)"); });
		Ui::Action(about, "spdlog", [] { return std::string("Logging (Gabi Melman)"); });
	}

	void ShowControls()
	{
		Ui::Controller().SetStatusText(
			"F5 or RB + Left: open / close~n~NUMPAD 8/2 or d-pad: move~n~NUMPAD 5 or A: select~n~NUMPAD 4/6 or d-pad: change value~n~NUMPAD 0, Backspace or B: back~n~F11 on a row: bind a hotkey",
			8000);
	}
}

namespace Menus
{
	void BuildSettings(MenuBase* root)
	{
		MenuBase* settings = Ui::Submenu(root, "Settings");
		Ui::ListMenu(settings, "Search", BuildSearch);

		MenuBase* core = Ui::Submenu(settings, "Core");
		g_gamepadToggle = Ui::Toggle(core, "Gamepad Controls", [](bool on) { Style().gamepad = on; });
		g_soundsToggle = Ui::Toggle(core, "Menu Sounds", [](bool on) { Style().sounds = on; });
		static std::vector<std::string> openNames(std::begin(MenuInput::kGamepadOpenNames), std::end(MenuInput::kGamepadOpenNames));
		Ui::Choice(core, "Gamepad Open Key", openNames, &Style().gamepadOpen);
		Ui::Do(core, "Show Controller Screen", ShowControls);

		BuildThemes(Ui::Submenu(settings, "Theme"));

		Ui::ListMenu(settings, "Hotkey Manager", [](MenuBase* menu)
		{
			Ui::Section(menu, "F11 on any row binds it to a key");
			if (g_hotkeys.empty())
				Ui::Section(menu, "No hotkeys");
			for (const auto& [row, vk] : g_hotkeys)
			{
				const std::string key = row;
				Ui::Action(menu, KeyNames::Format(vk) + ": " + row, [key]
				{
					g_hotkeys.erase(key);
					Ui::Controller().ReopenActiveLater();
					SaveSettings();
					return "Removed " + key;
				});
			}
		});

		MenuBase* io = Ui::Submenu(settings, "Load / Save");
		Ui::Action(io, "Save Settings", SaveSettings);
		Ui::Action(io, "Load Settings", LoadSettingsFile);
		Ui::Action(io, "Restore Defaults", []
		{
			Style() = MenuStyle{};
			SyncSettingToggles();
			return std::string("Defaults restored (Save Settings to keep them)");
		});
		Ui::Section(io, "Toggles");
		g_toggleSavingToggle = Ui::Toggle(io, "Enable Toggle Saving", [](bool on) { g_toggleSaving = on; SaveSettings(); });
		g_autoSaveToggle = Ui::Toggle(io, "Auto Save Toggles", [](bool on) { g_autoSaveToggles = on; SaveSettings(); });
		Ui::Action(io, "Save Toggles", SaveToggles);
		Ui::Action(io, "Load Toggles", LoadToggles);

		BuildOverlays(settings);
		BuildAbout(settings);

		SyncSettingToggles();
	}

	void LoadSettings()
	{
		Log::Write("[Settings] {}", LoadSettingsFile());
		if (g_toggleSaving)
			Log::Write("[Settings] {}", LoadToggles());
	}

	void TickSettings()
	{
		HotkeyTick();
		DrawOverlays();
		if (g_toggleSaving && g_autoSaveToggles)
		{
			static DWORD last = GetTickCount();
			static std::map<std::string, bool> saved = ToggleSnapshot();
			if (GetTickCount() - last > 5000)
			{
				last = GetTickCount();
				auto now = ToggleSnapshot();
				if (now != saved)
				{
					SaveToggles();
					saved = std::move(now);
				}
			}
		}
	}
}
