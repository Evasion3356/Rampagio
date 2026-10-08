/*
	Recovery > Challenges (ours, not in Rampage): the ChallengeCheat
	submodule (external/ChallengeCheat, ChallengeCheatLib.vcxproj), whose
	CLAUDE.md and ChallengeCheat.cpp header have the research. Each of the
	nine singleplayer challenges shows its rank and the game's objective
	for the next one, with:
	- Advance: one real step toward the current rank (one more kill, one
	  more herb, ...), the granularity a playthrough would give.
	- Complete: the current rank's goals to their full target, one rank
	  per click.
	Both apply their writes and return; the game confirms the rank on its
	own pass a few seconds later. Some goals need a live condition (on
	horseback, on a train, Dead Eye); the row then says which.

	The library's text (challenge names, objectives, messages) is the
	game's own wording per language, so it isn't run through our tables.
*/

#include "Menus.h"
#include "..\Log.h"
#include "..\..\external\ChallengeCheat\src\ChallengeCheat.h"
#include "..\..\external\ChallengeCheat\src\ChallengeCheatLog.h"
#include "..\..\external\ChallengeCheat\src\Localization.h"
#include "..\..\external\ChallengeCheat\src\TimedRideHook.h"

#include <format>

namespace
{
	using ChallengeCheat::Category;

	void Init()
	{
		static bool initialized = false;
		if (initialized)
			return;
		initialized = true;
		ChallengeCheat::Log::SetSink([](std::string_view line) { Log::Write("[ChallengeCheat] {}", line); });
		// Keeps the menu drawn while an action waits on the game.
		ChallengeCheat::SetWaitDrawCallback([] { Ui::Controller().OnDraw(); });
	}

	int NextRank(Category category)
	{
		const auto rank = ChallengeCheat::GetRankInfo(category);
		return rank.completed + 1 > rank.max ? rank.max : rank.completed + 1;
	}

	std::string Failure(Category category)
	{
		return std::format("{}: {}", ChallengeCheat::GetDisplayName(category), ChallengeCheat::GetLastAdvanceFailureReason());
	}

	void BuildChallenges(MenuBase* list)
	{
		Init();
		ChallengeCheat::Localization::SetLanguage(static_cast<int>(Localization::Current()));
		namespace L = ChallengeCheat::Localization;
		for (int i = 0; i < static_cast<int>(Category::Count); i++)
		{
			const auto category = static_cast<Category>(i);
			const auto rank = ChallengeCheat::GetRankInfo(category);
			const std::string name(ChallengeCheat::GetDisplayName(category));
			const std::string objective(L::RankObjective(i, NextRank(category)));
			Ui::Section(list, std::format("{} {} / {}", name, rank.completed, rank.max));
			Ui::Action(list, std::format("{} {} {}", L::Text(L::Msg::Advance), name, NextRank(category)), [category] {
				const std::string message = ChallengeCheat::AdvanceRank(category) ? "" : Failure(category);
				Ui::Controller().ReopenActiveLater();
				return message;
			});
			Ui::Describe(list, objective);
			Ui::Action(list, std::format("{} {} {}", L::Text(L::Msg::Complete), name, NextRank(category)), [category] {
				const std::string message = ChallengeCheat::CompleteChallenge(category) > 0 ? "" : Failure(category);
				Ui::Controller().ReopenActiveLater();
				return message;
			});
			Ui::Describe(list, objective);
		}
	}
}

namespace Menus
{
	void BuildRecoveryChallenges(MenuBase* recovery)
	{
		Ui::ListMenu(recovery, "Challenges", BuildChallenges);
	}

	void TickChallenges()
	{
		// A loading screen means another save or a new game: the Advance
		// step counters describe the save loaded before it.
		static bool wasLoading = false;
		const bool loading = DLC::GET_IS_LOADING_SCREEN_ACTIVE() != FALSE;
		if (loading && !wasLoading)
			ChallengeCheat::ResetSessionState();
		wasLoading = loading;
		// Disarms the timed-ride hook once it has fired; a no-op otherwise.
		ChallengeCheat::TimedRideHook::Update();
	}

	void ShutdownChallenges()
	{
		ChallengeCheat::TimedRideHook::Uninstall();
	}
}
