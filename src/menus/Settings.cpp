/*
	Settings: ports Rampage's Submenus::SubSettings and the parts of its
	submenus that have a Rampagio counterpart: SubSettingsCore,
	SubSettingsLoadSave, SubSettingsColor with SubSettingsPremadeThemes and
	SubSettingsCustomThemes, SubOverlaySettings, SubSettingsXUI's Max
	Display Options, Invert Colors, Centered Title, Smooth Scroller,
	Scroll Smoothness, Ink Rendering, Spawner Previews (Previews.h) and Teleport
	Map (TeleportMap.h), plus Search and the Hotkey Manager. The About page is
	Rampagio's own (SubAbout).

	Saved data is all in Rampagio.json (src/core/settings): this file owns
	its "general", "style" (MenuStyle) and "themes" (saved custom themes)
	parts, and the settings.* commands (menu key, wrap width, restore
	toggles). Command states and hotkeys are saved by src/core/commands.

	Settings > Hotkeys and the F11 binding flow are in Hotkeys.cpp. The
	premade themes are our own presets, not Rampage's 26.

	Not ported: Rampage's plugins, language files, ImGui windows (Window
	Manager), fonts, teleport map, welcome/ToS/update
	screens and mouse control (still to come, see CLAUDE.md).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\KeyNames.h"
#include "..\Localization.h"
#include "..\keyboard.h"
#include "..\Log.h"
#include "..\OnlineGuard.h"
#include "..\core\settings\Settings.h"
#include "..\core\settings\IStateSerializer.h"
#include "..\core\commands\Commands.h"
#include "..\core\commands\HotkeySystem.h"

// The only menu file that needs the full JSON header: it implements the
// style and themes components.
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <format>
#include <map>
#include <span>

using Rampagio::Commands;
using Rampagio::HotkeySystem;

namespace
{
	// --- style and themes ------------------------------------------------------------

	struct NamedColor
	{
		const char* name;
		const char* key; // in Rampagio.json
		ColorRgba MenuStyle::* field;
	};
	// Rampage's SubSettingsColor rows, plus Text and Section Text (ours).
	// The keys are new with the Rampage look, so colors saved for the old
	// one don't carry over.
	const NamedColor kColors[] = {
		{ "Main Color", "mainColor", &MenuStyle::main },
		{ "Title Text Color", "titleTextColor", &MenuStyle::titleText },
		{ "Header Color", "headerColor", &MenuStyle::header },
		{ "Subheader Color", "subheaderColor", &MenuStyle::subheader },
		{ "Base Color", "baseColor", &MenuStyle::base },
		{ "Scroller Color", "scrollerColor", &MenuStyle::scroller },
		{ "Text Color", "textColor", &MenuStyle::text },
		{ "Selected Text Color", "selectedTextColor", &MenuStyle::selectedText },
		{ "Footer Color", "footerColor", &MenuStyle::footer },
		{ "Section Text Color", "sectionTextColor", &MenuStyle::sectionText },
	};

	nlohmann::json ColorJson(ColorRgba c) { return nlohmann::json::array({ c.r, c.g, c.b, c.a }); }

	void ReadColor(const nlohmann::json& j, ColorRgba& out)
	{
		if (!j.is_array() || j.size() != 4)
			return;
		auto channel = [&j](size_t i) { return static_cast<unsigned char>(std::clamp(j[i].is_number() ? j[i].get<int>() : 0, 0, 255)); };
		out = { channel(0), channel(1), channel(2), channel(3) };
	}

	void WriteColors(nlohmann::json& j, const MenuStyle& style)
	{
		for (const NamedColor& c : kColors)
			j[c.key] = ColorJson(style.*c.field);
	}

	void ReadColors(const nlohmann::json& j, MenuStyle& style)
	{
		for (const NamedColor& c : kColors)
			if (auto it = j.find(c.key); it != j.end())
				ReadColor(*it, style.*c.field);
	}

	template <typename T>
	void ReadValue(const nlohmann::json& j, const char* key, T& value)
	{
		auto it = j.find(key);
		if (it == j.end())
			return;
		if constexpr (std::is_same_v<T, bool>)
		{
			if (it->is_boolean())
				value = it->get<bool>();
		}
		else if (it->is_number())
			value = it->get<T>();
	}

	// "style": the menu's look and input options (MenuStyle).
	class StyleComponent : public Rampagio::IStateSerializer
	{
	public:
		StyleComponent() : IStateSerializer("style") {}
		void SaveStateImpl(nlohmann::json& j) override
		{
			const MenuStyle& style = Style();
			WriteColors(j, style);
			j["left"] = style.left;
			j["top"] = style.top;
			j["linesPerScreen"] = style.linesPerScreen;
			j["sounds"] = style.sounds;
			j["gamepad"] = style.gamepad;
			j["gamepadOpen"] = style.gamepadOpen;
			j["titleFont"] = style.titleFont;
			j["bodyFont"] = style.bodyFont;
			j["title"] = style.title;
			j["invertColors"] = style.invertColors;
			j["centeredTitle"] = style.centeredTitle;
			j["smoothScroll"] = style.smoothScroll;
			j["scrollSmoothness"] = style.scrollSmoothness;
			j["mouse"] = style.mouse;
			j["inkRendering"] = style.inkRendering;
			j["spawnerPreviews"] = style.spawnerPreviews;
			j["teleportMap"] = style.teleportMap;
		}
		void LoadStateImpl(nlohmann::json& j) override
		{
			MenuStyle& style = Style();
			ReadColors(j, style);
			ReadValue(j, "left", style.left);
			ReadValue(j, "top", style.top);
			ReadValue(j, "linesPerScreen", style.linesPerScreen);
			ReadValue(j, "sounds", style.sounds);
			ReadValue(j, "gamepad", style.gamepad);
			ReadValue(j, "gamepadOpen", style.gamepadOpen);
			ReadValue(j, "titleFont", style.titleFont);
			ReadValue(j, "bodyFont", style.bodyFont);
			ReadValue(j, "invertColors", style.invertColors);
			ReadValue(j, "centeredTitle", style.centeredTitle);
			ReadValue(j, "smoothScroll", style.smoothScroll);
			ReadValue(j, "scrollSmoothness", style.scrollSmoothness);
			ReadValue(j, "mouse", style.mouse);
			ReadValue(j, "inkRendering", style.inkRendering);
			ReadValue(j, "spawnerPreviews", style.spawnerPreviews);
			ReadValue(j, "teleportMap", style.teleportMap);
			if (auto it = j.find("title"); it != j.end() && it->is_string())
				style.title = it->get<std::string>();
			style.titleFont = std::clamp(style.titleFont, 0, static_cast<int>(std::size(kTitleFonts)) - 1);
			style.bodyFont = std::clamp(style.bodyFont, 0, static_cast<int>(std::size(kBodyFonts)) - 1);
			style.left = std::clamp(style.left, 0.0f, 0.78f);
			style.top = std::clamp(style.top, 0.0f, 0.5f);
			style.linesPerScreen = std::clamp(style.linesPerScreen, 3, 25);
			style.scrollSmoothness = std::clamp(style.scrollSmoothness, 1, 20);
			style.gamepadOpen = std::clamp(style.gamepadOpen, 0, static_cast<int>(std::size(MenuInput::kGamepadOpenNames)) - 1);
		}
	};

	// "themes": { "<name>": { colors } }, the saved custom themes.
	class ThemesComponent : public Rampagio::IStateSerializer
	{
	public:
		std::map<std::string, MenuStyle> themes; // only the colors are used
		ThemesComponent() : IStateSerializer("themes") {}
		void SaveStateImpl(nlohmann::json& j) override
		{
			j = nlohmann::json::object();
			for (const auto& [name, style] : themes)
				WriteColors(j[name], style);
		}
		void LoadStateImpl(nlohmann::json& j) override
		{
			themes.clear();
			for (auto& [name, value] : j.items())
				if (value.is_object())
					ReadColors(value, themes[name]);
		}
	};

	// "general": a format version, for future changes to the file.
	class GeneralComponent : public Rampagio::IStateSerializer
	{
	public:
		GeneralComponent() : IStateSerializer("general") {}
		void SaveStateImpl(nlohmann::json& j) override { j["version"] = 1; }
		void LoadStateImpl(nlohmann::json&) override {}
	};

	StyleComponent* g_style = nullptr;
	ThemesComponent* g_themes = nullptr;

	void StyleChanged()
	{
		if (g_style)
			g_style->MarkStateDirty();
	}

	// A toggle row bound to one of MenuStyle's flags.
	class StyleFlagItem : public MenuItemSwitchable
	{
		bool MenuStyle::* m_field;
	public:
		StyleFlagItem(string caption, bool MenuStyle::* field) : MenuItemSwitchable(caption), m_field(field) {}
		void OnSelect() override
		{
			Style().*m_field = !(Style().*m_field);
			StyleChanged();
		}
		void OnDraw(float lineTop, float lineLeft, bool active) override
		{
			SetState(Style().*m_field);
			MenuItemSwitchable::OnDraw(lineTop, lineLeft, active);
		}
	};

	// header: the title box; base: behind the rows (the subheader and
	// footer are darker, more opaque versions); accent: the scroller.
	MenuStyle Preset(ColorRgba header, ColorRgba base, ColorRgba text, ColorRgba accent, ColorRgba section)
	{
		MenuStyle s = Style(); // keeps position and input options
		const auto darker = [](ColorRgba c, unsigned char a)
		{
			return ColorRgba{ static_cast<unsigned char>(c.r / 2), static_cast<unsigned char>(c.g / 2), static_cast<unsigned char>(c.b / 2), a };
		};
		s.main = accent;
		s.header = header;
		s.titleText = text;
		s.subheader = darker(base, 255);
		s.base = base;
		s.scroller = accent;
		s.text = text;
		s.selectedText = text;
		s.footer = darker(base, 220);
		s.sectionText = section;
		return s;
	}

	void ApplyColors(const MenuStyle& from)
	{
		for (const NamedColor& c : kColors)
			Style().*c.field = from.*c.field;
		StyleChanged();
	}

	void BuildThemes(MenuBase* theme)
	{
		MenuBase* premade = Ui::Submenu(theme, "Premade Themes");
		const ColorRgba white{ 255, 255, 255, 255 };
		Ui::Do(premade, "settings.theme.rampagio", "Rampagio", [] { ApplyColors(MenuStyle{}); });
		Ui::Do(premade, "settings.theme.bloodred", "Blood Red", [white] { ApplyColors(Preset({ 120, 0, 0, 230 }, { 30, 10, 10, 190 }, white, { 230, 30, 30, 255 }, { 230, 120, 120, 255 })); });
		Ui::Do(premade, "settings.theme.saintdenisgold", "Saint Denis Gold", [white] { ApplyColors(Preset({ 20, 15, 5, 235 }, { 45, 38, 25, 190 }, { 255, 235, 190, 255 }, { 212, 175, 55, 255 }, { 212, 175, 55, 255 })); });
		Ui::Do(premade, "settings.theme.lagrasswamp", "Lagras Swamp", [white] { ApplyColors(Preset({ 20, 45, 30, 230 }, { 25, 40, 30, 185 }, white, { 110, 190, 90, 255 }, { 150, 210, 140, 255 })); });
		Ui::Do(premade, "settings.theme.guarmasea", "Guarma Sea", [white] { ApplyColors(Preset({ 0, 50, 90, 230 }, { 15, 35, 55, 185 }, white, { 0, 170, 220, 255 }, { 120, 200, 240, 255 })); });
		Ui::Do(premade, "settings.theme.ghosttrain", "Ghost Train", [] { ApplyColors(Preset({ 220, 220, 220, 230 }, { 235, 235, 235, 200 }, { 20, 20, 20, 255 }, { 150, 150, 150, 255 }, { 60, 60, 60, 255 })); });
		Ui::Do(premade, "settings.theme.nightfolk", "Night Folk", [white] { ApplyColors(Preset({ 10, 10, 10, 240 }, { 15, 15, 15, 200 }, { 200, 200, 200, 255 }, { 120, 0, 160, 255 }, { 170, 110, 210, 255 })); });

		Ui::ListMenu(theme, "Custom Themes", [](MenuBase* menu)
		{
			Ui::Action(menu, "Save Current", []() -> std::string
			{
				std::string name;
				if (!GameUtil::PromptText("Theme Name:", name) || name.empty())
					return "";
				g_themes->themes[name] = Style();
				g_themes->MarkStateDirty();
				Ui::Controller().ReopenActiveLater();
				return TrFormat("Saved {}", name);
			});
			if (g_themes->themes.empty())
				Ui::Section(menu, "No Themes found");
			for (const auto& [name, style] : g_themes->themes)
				Ui::Do(menu, name, [name] { ApplyColors(g_themes->themes[name]); });
		});

		Ui::Section(theme, "Customize");
		// Font faces as Rampage lists them, without the '$'.
		const auto faces = [](std::span<const char* const> fonts)
		{
			std::vector<std::string> names;
			for (const char* f : fonts)
				names.push_back(f + 1);
			return names;
		};
		Ui::Choice(theme, "Main Font", faces(kTitleFonts), &Style().titleFont, [](int) { StyleChanged(); });
		Ui::Choice(theme, "Body Font", faces(kBodyFonts), &Style().bodyFont, [](int) { StyleChanged(); });
		Ui::Text(theme, "Menu Title", &Style().title, StyleChanged);
		static MenuBase* colorMenu = nullptr;
		static ColorRgba MenuStyle::* editing = nullptr;
		static int rgba[4];
		colorMenu = Ui::DetachedListMenu("Color", [](MenuBase* menu)
		{
			ColorRgba& c = Style().*editing;
			rgba[0] = c.r; rgba[1] = c.g; rgba[2] = c.b; rgba[3] = c.a;
			auto write = []
			{
				Style().*editing = { (unsigned char)rgba[0], (unsigned char)rgba[1], (unsigned char)rgba[2], (unsigned char)rgba[3] };
				StyleChanged();
			};
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
		Ui::Number(theme, "Menu X", &Style().left, 0.0f, 0.78f, 0.01f, StyleChanged);
		Ui::Number(theme, "Menu Y", &Style().top, 0.0f, 0.5f, 0.01f, StyleChanged);
		Ui::Number(theme, "Max Display Options", &Style().linesPerScreen, 3, 25, 1, StyleChanged);
		// SubSettingsXUI's native-menu rows.
		MenuItemToggle* invert = Ui::Toggle(theme, "Invert Colors", [](bool on) { Style().invertColors = on; StyleChanged(); });
		MenuItemToggle* centered = Ui::Toggle(theme, "Centered Title", [](bool on) { Style().centeredTitle = on; StyleChanged(); });
		MenuItemToggle* smooth = Ui::Toggle(theme, "Smooth Scroller", [](bool on) { Style().smoothScroll = on; StyleChanged(); });
		Ui::Number(theme, "Scroll Smoothness", &Style().scrollSmoothness, 1, 20, 1, StyleChanged);
		MenuItemToggle* ink = Ui::Toggle(theme, "Ink Rendering", [](bool on) { Style().inkRendering = on; StyleChanged(); });
		MenuItemToggle* previews = Ui::Toggle(theme, "Spawner Previews", [](bool on) { Style().spawnerPreviews = on; StyleChanged(); });
		MenuItemToggle* teleportMap = Ui::Toggle(theme, "Teleport Map", [](bool on) { Style().teleportMap = on; StyleChanged(); });
		// The style loads after the menus are built: show its values on open.
		theme->SetOnOpen([invert, centered, smooth, ink, previews, teleportMap](MenuBase*)
		{
			invert->SetState(Style().invertColors);
			centered->SetState(Style().centeredTitle);
			smooth->SetState(Style().smoothScroll);
			ink->SetState(Style().inkRendering);
			previews->SetState(Style().spawnerPreviews);
			teleportMap->SetState(Style().teleportMap);
		});
	}

	// --- settings commands -------------------------------------------------------------

	// A key the menu can open with: one KeyNames round-trips that isn't used
	// for navigation or binding.
	bool IsUsableMenuKey(int vk)
	{
		DWORD parsed = 0;
		return vk > 0 && vk < 0xFF && vk != static_cast<int>(Menus::kHotkeyBindKey) && vk != VK_ESCAPE
			&& KeyNames::Parse(KeyNames::Format(static_cast<DWORD>(vk)), parsed) && parsed == static_cast<DWORD>(vk);
	}

	// settings.menukey: MenuKey() as a VK code; an unusable saved key falls
	// back to F5 (logged).
	class MenuKeyCommand : public Rampagio::IntCommand
	{
	protected:
		int Clamp(const int& value) const override
		{
			if (IsUsableMenuKey(value))
				return value;
			Log::Write("[Settings] Menu key {} isn't usable (reserved for navigation, or unknown); using F5", value);
			return VK_F5;
		}
	public:
		MenuKeyCommand() : Rampagio::IntCommand("settings.menukey", "Menu Key", "", 1, 0xFE, 1, VK_F5, &MenuKey()) {}
	};

	MenuKeyCommand* g_menuKey = nullptr;

	// --- search ----------------------------------------------------------------------------

	// "Menu Title > Caption", how a search result names a row.
	std::string RowPath(MenuItemBase* item)
	{
		MenuBase* menu = item->GetMenu();
		const std::string title = menu ? menu->GetTitle()->MenuItemTitle::GetCaption() : "";
		return std::string(Tr(title)) + " > " + std::string(Tr(item->GetCaption()));
	}

	// Every static menu (and so every command row) is built at start, so
	// searching the built menus also finds rows in menus never opened.
	// ListMenu rows only exist once their list has been opened.
	void BuildSearch(MenuBase* results)
	{
		std::string text;
		if (!GameUtil::PromptText("Search:", text) || text.empty())
			return;
		// Matched against the captions as shown, so in the menu's language;
		// Upper folds Latin and Cyrillic case.
		text = Localization::Upper(text);
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
				if (caption.empty() || Localization::Upper(Tr(caption)).find(text) == std::string::npos)
					continue;
				++found;
				const int index = static_cast<int>(i);
				Ui::Do(results, RowPath(item), [menu, index]
				{
					Ui::Controller().PushMenu(menu);
					menu->SetActiveItemIndex(index);
				});
			}
		}
		if (!found)
			Ui::Section(results, "No matches (list menus only count once opened)");
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
		Overlay(std::format("{} {:02}:{:02}", Tr(kDays[tm.tm_wday]), tm.tm_hour, tm.tm_min));
	}

	void GameTimeOverlay()
	{
		const int day = CLOCK::GET_CLOCK_DAY_OF_WEEK();
		Overlay(std::format("{} {:02}:{:02}", Tr(kDays[(day % 7 + 7) % 7]), CLOCK::GET_CLOCK_HOURS(), CLOCK::GET_CLOCK_MINUTES()));
	}

	void CoordsOverlay()
	{
		const Ped me = PLAYER::PLAYER_PED_ID();
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(me, TRUE, FALSE);
		Overlay(TrFormat("X: {:.3f}  Y: {:.3f}  Z: {:.3f}  Heading: {:.1f}", p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(me)));
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
		Overlay(d >= 1000.0f ? TrFormat("Waypoint: {:.2f} km", d / 1000.0f) : TrFormat("Waypoint: {} m", static_cast<int>(d)));
	}

	constexpr Hash HUD_CTX_HONOR_SHOW = 0x074132EF; // Rampage's Always Show Honor context

	void BuildOverlays(MenuBase* settings)
	{
		MenuBase* o = Ui::Submenu(settings, "Overlay Settings");
		Ui::Looped(o, "settings.overlay.fps", "Display FPS", [] { Overlay(TrFormat("{} FPS", g_fps)); });
		Ui::Looped(o, "settings.overlay.realtime", "Display Day & Time (Real time)", RealTimeOverlay);
		Ui::Looped(o, "settings.overlay.gametime", "Display Time (Ingame)", GameTimeOverlay);
		Ui::Looped(o, "settings.overlay.coords", "Display Coordinates", CoordsOverlay);
		Ui::Looped(o, "settings.overlay.temperature", "Display Temperature", TemperatureOverlay);
		Ui::Looped(o, "settings.overlay.waypoint", "Display Waypoint Distance", WaypointOverlay);
		// These act on the game, so they're feature states (overlay.*, not
		// settings.*).
		Ui::Toggle(o, "overlay.playercores", "Always Show Player Cores", [](bool on) { HUD::_SHOW_PLAYER_CORES(on); });
		Ui::Toggle(o, "overlay.horsecores", "Always Show Horse Cores", [](bool on) { HUD::_SHOW_HORSE_CORES(on); });
		Ui::Looped(o, "overlay.honor", "Always Show Honor", [] { HUD::_ENABLE_HUD_CONTEXT_THIS_FRAME(HUD_CTX_HONOR_SHOW); });
		Ui::Looped(o, "overlay.hidehud", "Hide HUD", [] { HUD::HIDE_HUD_AND_RADAR_THIS_FRAME(); });
		Ui::Toggle(o, "overlay.hideradar", "Hide Radar", [](bool on) { MAP::DISPLAY_RADAR(!on); });
		Ui::Looped(o, "overlay.hidepois", "Hide POIs", [] { MAP::_HIDE_ACTIVE_POINTS_OF_INTEREST(); });
		Ui::Looped(o, "overlay.nomenuslowdown", "No Menu Slow-Down", [] { HUD::_DISABLE_REDUCED_MENU_TIME_SCALE(); });
		Ui::Looped(o, "overlay.noscreeneffects", "Remove All Screen Effects", [] { GRAPHICS::ANIMPOSTFX_STOP_ALL(); });
		Ui::Looped(o, "overlay.nonotifications", "Disable Notifications", [] { UIFEED::_UI_FEED_CLEAR_ALL_CHANNELS(); });
		Ui::Looped(o, "overlay.nohelptext", "Disable Help Text", [] { HUD::CLEAR_ALL_HELP_MESSAGES(); });
		Ui::Number(o, "overlay.minimapzoom", "Minimap Zoom", &g_minimapZoom, 0, 1400, 50, [] { MAP::SET_RADAR_ZOOM(g_minimapZoom); });
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
		Ui::Action(about, "HorseMenu", [] { return std::string("Settings and commands, script function caller, native hooks, pointers"); });
		Ui::Action(about, "Halen84", [] { return std::string("RDR3 native flags and enums"); });
		Ui::Section(about, "Libraries used");
		Ui::Action(about, "MinHook", [] { return std::string("Hooking library (Tsuda Kageyu)"); });
		Ui::Action(about, "nlohmann/json", [] { return std::string("JSON for Modern C++ (Niels Lohmann)"); });
		Ui::Action(about, "spdlog", [] { return std::string("Logging (Gabi Melman)"); });
	}

	// Settings > Language: the game's language (default) or one picked.
	// Chinese, Japanese and Korean need the game itself in one of them for
	// their fonts (Localization.h).
	void BuildLanguage(MenuBase* settings)
	{
		using Localization::Language;
		static std::vector<std::string> names;
		names.push_back("Game Language");
		for (int i = 0; i < static_cast<int>(Language::Count); ++i)
			names.emplace_back(Localization::NativeName(static_cast<Language>(i)));
		Ui::Choice(settings, "settings.language", "Language", names, &Localization::Setting(), [](int)
		{
			const Language picked = Localization::Current();
			const bool cjk = picked == Language::Korean || picked == Language::Japanese
				|| picked == Language::ChineseTraditional || picked == Language::ChineseSimplified;
			const Language game = Localization::GameLanguage();
			const bool gameCjk = game == Language::Korean || game == Language::Japanese
				|| game == Language::ChineseTraditional || game == Language::ChineseSimplified;
			if (cjk && !gameCjk)
				Ui::Controller().SetStatusText("This language only shows while the game itself is set to Chinese, Japanese or Korean", 5000);
		});
	}

	void ShowControls()
	{
		Ui::Controller().SetStatusText(
			TrFormat("{} or RB + Left: open / close~n~NUMPAD 8/2 or d-pad: move~n~NUMPAD 5 or A: select~n~NUMPAD 4/6 or d-pad: change value~n~NUMPAD 0, Backspace or B: back~n~F11 on a row: bind a hotkey",
				KeyNames::Format(static_cast<DWORD>(MenuKey()))),
			8000);
	}
}

namespace Menus
{
	void BuildSettings(MenuBase* root)
	{
		MenuBase* settings = Ui::Submenu(root, "Settings");
		Ui::ListMenu(settings, "Search", BuildSearch);

		BuildLanguage(settings);

		MenuBase* core = Ui::Submenu(settings, "Core");
		g_menuKey = new MenuKeyCommand();
		core->AddItem(new MenuItemActionStatus(
			[] { return TrFormat("Menu Key: {}", KeyNames::Format(static_cast<DWORD>(MenuKey()))); },
			[]
			{
				CaptureKey("Press the new menu key", [](int vk)
				{
					if (IsUsableMenuKey(vk) && !KeyNames::IsReserved(static_cast<DWORD>(vk)))
					{
						g_menuKey->SetState(vk);
						Ui::Controller().SetStatusText(TrFormat("Menu key: {}", KeyNames::Format(static_cast<DWORD>(vk))), 3000);
					}
					else
						Ui::Controller().SetStatusText(TrFormat("{} can't open the menu", HotkeySystem::KeyLabel(static_cast<Rampagio::InputId>(vk))), 3000);
				});
				return std::string();
			}));
		core->AddItem(new StyleFlagItem("Gamepad Controls", &MenuStyle::gamepad));
		core->AddItem(new StyleFlagItem("Menu Sounds", &MenuStyle::sounds));
		core->AddItem(new StyleFlagItem("Mouse Controls", &MenuStyle::mouse));
		static std::vector<std::string> openNames(std::begin(MenuInput::kGamepadOpenNames), std::end(MenuInput::kGamepadOpenNames));
		Ui::Choice(core, "Gamepad Open Key", openNames, &Style().gamepadOpen, [](int) { StyleChanged(); });
		Ui::Number(core, "settings.wrapwidth", "Text Wrap Width", &WrapWidth(), 0, 120, 10);
		Ui::Do(core, "settings.showcontrols", "Show Controller Screen", ShowControls);

		BuildThemes(Ui::Submenu(settings, "Theme"));
		BuildHotkeys(settings);

		MenuBase* io = Ui::Submenu(settings, "Load / Save");
		Ui::Section(io, "Saved to Rampagio.json as you change things");
		Ui::Action(io, "Save Settings", []
		{
			return std::string(Rampagio::Settings::Flush() ? "Settings saved" : "Couldn't write Rampagio.json (see Rampagio.log)");
		});
		Ui::Action(io, "Load Settings", []
		{
			Rampagio::Settings::Reload();
			ApplyLoadedSettings();
			return std::string("Settings loaded");
		});
		Ui::Action(io, "Restore Defaults", []
		{
			Commands::ResetToDefaults();
			Style() = MenuStyle{};
			StyleChanged();
			return std::string("Defaults restored");
		});

		BuildOverlays(settings);
		BuildAbout(settings);
	}

	void RegisterSettings()
	{
		static GeneralComponent general;
		static StyleComponent style;
		static ThemesComponent themes;
		g_style = &style;
		g_themes = &themes;
		RegisterHotkeys();
		Commands::GetInstance();
	}

	void ApplyLoadedSettings()
	{
		Commands::ApplyLoaded();
	}

	void TickSettings()
	{
		// The guard's second pass copy, called from here rather than the main
		// loop so the two sit in different functions (OnlineGuard.h).
		OnlineGuard::TickAlt();
		// Hotkeys run commands: check here as well as in the main loop.
		if (OnlineGuard::IsOnline())
			return;
		TickHotkeys();
		DrawOverlays();
	}
}
