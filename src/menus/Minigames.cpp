/*
	Miscellaneous > Minigames: the sibling minigame mods (ours, not in
	Rampage), linked as static libraries from their submodules (Goal B in
	CLAUDE.md). Each library holds the mechanism; the rows here own its
	options, so they save in Rampagio.json like any other row, and come
	back on start, the way the standalone mods' INIs work.

	Five Finger Fillet: external/FFFCheat (FFFCheatLib). Patches the
	fillet_sp script while it runs, each patch behind its own row; see that
	repo's CLAUDE.md for the bytecode and guards.

	Poker, Blackjack, Dominoes: external/PokerCheat, BlackjackCheat,
	DominoCheat. HUD advisors that read the minigame's script memory (every
	hand, the deck ahead) and draw over the table. Their rows are built
	from each library's Config::Options() table, so an option added
	upstream shows up here with only a submodule bump; ids never change.
	Their text follows the menu's language.
*/

#include "Menus.h"
#include "..\Log.h"
#include "..\..\external\FFFCheat\src\FFFCheat.h"
#include "..\..\external\FFFCheat\src\FFFCheatLog.h"
#include "..\..\external\PokerCheat\src\Config.h"
#include "..\..\external\PokerCheat\src\Localization.h"
#include "..\..\external\PokerCheat\src\PokerCheat.h"
#include "..\..\external\PokerCheat\src\PokerCheatLog.h"
#include "..\..\external\BlackjackCheat\src\BlackjackCheat.h"
#include "..\..\external\BlackjackCheat\src\BlackjackCheatLog.h"
#include "..\..\external\BlackjackCheat\src\Config.h"
#include "..\..\external\BlackjackCheat\src\Localization.h"
#include "..\..\external\DominoCheat\src\Config.h"
#include "..\..\external\DominoCheat\src\DominoCheat.h"
#include "..\..\external\DominoCheat\src\DominoCheatLog.h"
#include "..\..\external\DominoCheat\src\Localization.h"

#include <format>
#include <span>

namespace
{
	// ---- Five Finger Fillet ----

	FFFCheat::Options g_fillet;

	// Runs once per frame while any fillet row is on.
	void FilletTick()
	{
		static int lastFrame = -1;
		const int frame = MISC::GET_FRAME_COUNT();
		if (frame == lastFrame)
			return;
		lastFrame = frame;
		static const bool initialized = [] {
			FFFCheat::Log::SetSink([](std::string_view line) { Log::Write("[FFFCheat] {}", line); });
			FFFCheat::SetOptionsProvider([] { return g_fillet; });
			return true;
		}();
		FFFCheat::OnTick();
	}

	// A fillet option row: off until the user turns it on.
	void FilletToggle(MenuBase* menu, const char* id, const char* caption, bool FFFCheat::Options::* option)
	{
		g_fillet.*option = false;
		Ui::Toggle(menu, id, caption, [option](bool on) {
			g_fillet.*option = on;
			FFFCheat::Reapply();
		}, FilletTick);
	}

	void BuildFiveFingerFillet(MenuBase* minigames)
	{
		MenuBase* menu = Ui::Submenu(minigames, "Five Finger Fillet");
		FilletToggle(menu, "minigames.fff.anybuttoncounts", "Any Button Counts", &FFFCheat::Options::AnyButtonCounts);
		FilletToggle(menu, "minigames.fff.ignoreearlypresses", "Ignore Early Presses", &FFFCheat::Options::IgnoreEarlyPress);
		FilletToggle(menu, "minigames.fff.hidetimermessage", "Hide Timer Message", &FFFCheat::Options::HideTimerMessage);
	}

	// ---- the advisors ----

	// The advisors' language codes, in GET_CURRENT_LANGUAGE order (ours
	// too, see Localization::Language).
	constexpr const char* kLanguageCodes[] = { "en-US", "fr-FR", "de-DE", "it-IT", "es-ES", "pt-BR", "pl-PL",
		"ru-RU", "ko-KR", "zh-TW", "ja-JP", "es-MX", "zh-CN" };
	static_assert(std::size(kLanguageCodes) == static_cast<size_t>(Localization::Language::Count));

	// What Rampagio needs from an advisor library. The three share one
	// shape but live in their own namespaces, so this binds them.
	struct Advisor
	{
		bool* enabled;
		void (*setEnabled)(bool);
		void (*onTick)();
		std::string* language; // Config::Mutable().Language
		void (*refreshLanguage)();
		int languageShown = -1;
	};

	void AdvisorTick(Advisor& advisor)
	{
		const int language = static_cast<int>(Localization::Current());
		if (language != advisor.languageShown)
		{
			advisor.languageShown = language;
			*advisor.language = kLanguageCodes[language];
			advisor.refreshLanguage();
		}
		if (!*advisor.enabled)
			advisor.setEnabled(true);
		advisor.onTick();
	}

	// A row per entry of an advisor's Config::Options(), grouped by section.
	template <typename Option>
	void BuildOptionRows(MenuBase* menu, std::string_view prefix, std::span<const Option> options)
	{
		std::string_view section;
		for (const Option& option : options)
		{
			if (section != option.section)
			{
				section = option.section;
				Ui::Section(menu, std::string(section));
			}
			const std::string id = std::format("{}.{}", prefix, option.id);
			switch (option.kind)
			{
			case Option::Kind::Bool:
			{
				bool* value = static_cast<bool*>(option.value);
				Ui::Toggle(menu, id, option.label, [value](bool on) { *value = on; })->SetDefault(*value);
				break;
			}
			case Option::Kind::Int:
				Ui::Number(menu, id, option.label, static_cast<int*>(option.value), static_cast<int>(option.min),
					static_cast<int>(option.max), static_cast<int>(option.step));
				break;
			case Option::Kind::Float:
				Ui::Number(menu, id, option.label, static_cast<float*>(option.value), option.min, option.max, option.step);
				break;
			}
			Ui::Describe(menu, option.description);
		}
	}

	// The advisor's own row (off until the user turns it on), then its options.
	template <typename Option>
	void BuildAdvisor(MenuBase* minigames, const char* title, const char* prefix, Advisor& advisor, std::span<const Option> options)
	{
		MenuBase* menu = Ui::Submenu(minigames, title);
		Advisor* a = &advisor;
		Ui::Toggle(menu, std::format("{}.enabled", prefix), "Advisor", [a](bool on) { a->setEnabled(on); },
			[a] { AdvisorTick(*a); });
		Ui::Describe(menu, "Reads the table's cards from the game's memory and shows them, with advice, while you play.");
		BuildOptionRows(menu, prefix, options);
	}

	Advisor g_poker{ &PokerCheat::Enabled, PokerCheat::SetEnabled, PokerCheat::OnTick,
		&PokerCheat::Config::Mutable().Language, PokerCheat::Localization::Refresh };
	Advisor g_blackjack{ &BlackjackCheat::Enabled, BlackjackCheat::SetEnabled, BlackjackCheat::OnTick,
		&BlackjackCheat::Config::Mutable().Language, BlackjackCheat::Localization::Refresh };
	Advisor g_dominoes{ &DominoCheat::Enabled, DominoCheat::SetEnabled, DominoCheat::OnTick,
		&DominoCheat::Config::Mutable().Language, DominoCheat::Localization::Refresh };

	void BuildAdvisors(MenuBase* minigames)
	{
		PokerCheat::Log::SetSink([](std::string_view line) { Log::Write("[PokerCheat] {}", line); });
		BlackjackCheat::Log::SetSink([](std::string_view line) { Log::Write("[BlackjackCheat] {}", line); });
		DominoCheat::Log::SetSink([](std::string_view line) { Log::Write("[DominoCheat] {}", line); });
		BuildAdvisor(minigames, "Poker", "minigames.poker", g_poker, PokerCheat::Config::Options());
		BuildAdvisor(minigames, "Blackjack", "minigames.blackjack", g_blackjack, BlackjackCheat::Config::Options());
		BuildAdvisor(minigames, "Dominoes", "minigames.dominoes", g_dominoes, DominoCheat::Config::Options());
	}
}

namespace Menus
{
	void BuildMinigames(MenuBase* minigames)
	{
		BuildFiveFingerFillet(minigames);
		BuildAdvisors(minigames);
	}

	void ShutdownMinigames(bool processExit)
	{
		DominoCheat::OnProcessDetach(processExit);
		// At process exit the game's memory goes with it; restoring bytes
		// would only add risk.
		if (!processExit)
			FFFCheat::Shutdown();
	}
}
