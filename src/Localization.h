/*
	The menu in the 13 languages RDR2 ships, as PokerCheat does it: the
	game's own language (LOCALIZATION::GET_CURRENT_LANGUAGE) unless
	Settings > Language (settings.language) picks one.

	Text is translated by its English wording. Tr() looks a string up in
	the current language's table (src/lang/<code>.inc, compiled in) and
	gives the English back when it has no entry, so untranslated text
	still shows. The menu translates what it draws (row captions, menu
	titles, choice values, sections, descriptions, status text, keyboard
	titles), so rows don't need anything; text built at runtime goes
	through Tr/TrFormat where it's built ("Spawned {} cards"), since only
	the template is in the tables.

	tools/lang_sync.py extracts the English strings and checks each table
	(missing and stale entries, {} placeholders and ~codes~ that don't
	match). The translations are written by hand per language, with the
	community Rampage translations as a reference where they exist.

	Chinese, Japanese and Korean only render while the game itself runs
	in one of those languages: the game loads those fonts only then
	(PokerCheat docs/PITFALLS.md). Settings > Language says so.
*/

#pragma once

#include <format>
#include <string>
#include <string_view>

namespace Localization
{
	// GET_CURRENT_LANGUAGE's values.
	enum class Language : int
	{
		English, French, German, Italian, Spanish, PortugueseBrazilian, Polish, Russian, Korean,
		ChineseTraditional, Japanese, SpanishMexican, ChineseSimplified,
		Count
	};

	// settings.language: 0 follows the game, else Language + 1.
	int& Setting();
	// Each language's name in itself, for Settings > Language.
	std::string_view NativeName(Language language);

	// Re-reads the game's language (when following it). Must run on the
	// script thread; the main loop calls it every few seconds.
	void Refresh();
	Language Current();
	// The game's own language, ignoring the setting.
	Language GameLanguage();

	// `english` in the current language, or `english` itself.
	std::string_view Tr(std::string_view english);

	// std::format with the translated template; falls back to the English
	// template if a translation's placeholders don't fit the arguments.
	template <typename... Args>
	std::string TrFormat(std::string_view english, Args&&... args)
	{
		try
		{
			return std::vformat(Tr(english), std::make_format_args(args...));
		}
		catch (const std::format_error&)
		{
			return std::vformat(english, std::make_format_args(args...));
		}
	}

	// Upper case for the menu's subheader: ASCII, Latin-1, Latin
	// Extended-A and Cyrillic, which covers every Latin and Cyrillic
	// language the game has. Other scripts have no case.
	std::string Upper(std::string_view text);
}

using Localization::Tr;
using Localization::TrFormat;
