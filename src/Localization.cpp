#include "Localization.h"
#include "Log.h"
#include "script.h"

#include <unordered_map>

namespace
{
	using Localization::Language;

	struct Entry
	{
		const char* english;
		const char* text;
	};

	// One table per language, English -> translation; each ends with a
	// null entry so an empty table still compiles.
	constexpr Entry kFrench[] = {
#include "lang\fr.inc"
		{ nullptr, nullptr } };
	constexpr Entry kGerman[] = {
#include "lang\de.inc"
		{ nullptr, nullptr } };
	constexpr Entry kItalian[] = {
#include "lang\it.inc"
		{ nullptr, nullptr } };
	constexpr Entry kSpanish[] = {
#include "lang\es.inc"
		{ nullptr, nullptr } };
	constexpr Entry kPortuguese[] = {
#include "lang\pt-BR.inc"
		{ nullptr, nullptr } };
	constexpr Entry kPolish[] = {
#include "lang\pl.inc"
		{ nullptr, nullptr } };
	constexpr Entry kRussian[] = {
#include "lang\ru.inc"
		{ nullptr, nullptr } };
	constexpr Entry kKorean[] = {
#include "lang\ko.inc"
		{ nullptr, nullptr } };
	constexpr Entry kChineseTraditional[] = {
#include "lang\zh-TW.inc"
		{ nullptr, nullptr } };
	constexpr Entry kJapanese[] = {
#include "lang\ja.inc"
		{ nullptr, nullptr } };
	// Only where Mexican Spanish differs; everything else comes from es.inc.
	constexpr Entry kSpanishMexican[] = {
#include "lang\es-MX.inc"
		{ nullptr, nullptr } };
	constexpr Entry kChineseSimplified[] = {
#include "lang\zh-CN.inc"
		{ nullptr, nullptr } };

	struct LanguageInfo
	{
		const char* nativeName;
		const char* code;
		const Entry* table;
	};

	constexpr LanguageInfo kLanguages[] = {
		{ "English", "en-US", nullptr },
		{ "Français", "fr-FR", kFrench },
		{ "Deutsch", "de-DE", kGerman },
		{ "Italiano", "it-IT", kItalian },
		{ "Español", "es-ES", kSpanish },
		{ "Português (Brasil)", "pt-BR", kPortuguese },
		{ "Polski", "pl-PL", kPolish },
		{ "Русский", "ru-RU", kRussian },
		{ "한국어", "ko-KR", kKorean },
		{ "繁體中文", "zh-TW", kChineseTraditional },
		{ "日本語", "ja-JP", kJapanese },
		{ "Español (México)", "es-MX", kSpanishMexican },
		{ "简体中文", "zh-CN", kChineseSimplified },
	};
	static_assert(std::size(kLanguages) == static_cast<size_t>(Language::Count));

	Language g_game = Language::English;
	bool g_gameRead = false;

	using Map = std::unordered_map<std::string_view, std::string_view>;

	void Add(Map& map, const Entry* table)
	{
		for (const Entry* e = table; e && e->english; ++e)
			map.try_emplace(e->english, e->text);
	}

	const Map& MapFor(Language language)
	{
		static Map maps[static_cast<size_t>(Language::Count)];
		static bool built[static_cast<size_t>(Language::Count)] = {};
		const size_t i = static_cast<size_t>(language);
		if (!built[i])
		{
			built[i] = true;
			Add(maps[i], kLanguages[i].table);
			if (language == Language::SpanishMexican)
				Add(maps[i], kSpanish);
			Log::Write("[Localization] {} table: {} strings", kLanguages[i].code, maps[i].size());
		}
		return maps[i];
	}

	// Appends the UTF-8 encoding of cp.
	void PutUtf8(std::string& out, char32_t cp)
	{
		if (cp < 0x80)
			out.push_back(static_cast<char>(cp));
		else if (cp < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else if (cp < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	char32_t UpperCodePoint(char32_t cp)
	{
		if (cp >= U'a' && cp <= U'z')
			return cp - 0x20;
		if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) // Latin-1: à..þ except ÷
			return cp - 0x20;
		if (cp == 0xFF)
			return 0x178; // ÿ
		// Latin Extended-A pairs: upper even, lower odd, except where the
		// pairing shifts (Ĺ..Ň and Ź..Ž are odd/even).
		if ((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177))
			return (cp & 1) ? cp - 1 : cp;
		if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E))
			return (cp & 1) ? cp : cp - 1;
		if (cp >= 0x430 && cp <= 0x44F) // Cyrillic а..я
			return cp - 0x20;
		if (cp >= 0x450 && cp <= 0x45F) // ѐ..џ
			return cp - 0x50;
		return cp;
	}
}

namespace Localization
{
	int& Setting()
	{
		static int setting = 0;
		return setting;
	}

	std::string_view NativeName(Language language)
	{
		return kLanguages[static_cast<size_t>(language)].nativeName;
	}

	void Refresh()
	{
		const int raw = LOCALIZATION::GET_CURRENT_LANGUAGE();
		const Language game = raw >= 0 && raw < static_cast<int>(Language::Count) ? static_cast<Language>(raw) : Language::English;
		if (!g_gameRead || game != g_game)
			Log::Write("[Localization] Game language {} ({})", raw, kLanguages[static_cast<size_t>(game)].code);
		g_game = game;
		g_gameRead = true;
	}

	Language GameLanguage()
	{
		return g_game;
	}

	Language Current()
	{
		const int setting = Setting();
		if (setting > 0 && setting <= static_cast<int>(Language::Count))
			return static_cast<Language>(setting - 1);
		return g_game;
	}

	std::string_view Tr(std::string_view english)
	{
		const Language language = Current();
		if (language == Language::English || english.empty())
			return english;
		const Map& map = MapFor(language);
		const auto it = map.find(english);
		return it == map.end() ? english : it->second;
	}

	std::string Upper(std::string_view text)
	{
		std::string out;
		out.reserve(text.size());
		bool code = false; // leave ~COLOR_...~ codes and <tags> alone
		bool tag = false;
		for (size_t i = 0; i < text.size();)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
			if (i + len > text.size())
				len = 1;
			if (len == 1 && c >= 0x80) // malformed: copy as is
			{
				out.push_back(static_cast<char>(c));
				i++;
				continue;
			}
			char32_t cp = len == 1 ? c : c & (0xFF >> (len + 1));
			for (size_t k = 1; k < len; k++)
				cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
			if (cp == U'~')
				code = !code;
			else if (cp == U'<')
				tag = true;
			else if (cp == U'>')
				tag = false;
			PutUtf8(out, code || tag ? cp : UpperCodePoint(cp));
			i += len;
		}
		return out;
	}
}
