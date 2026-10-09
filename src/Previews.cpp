#include "Previews.h"
#include "scriptmenu.h"
#include "GameUtil.h"

#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	struct Preview
	{
		const char* model;
		int variant;
		const char* dict;
		const char* texture;
	};
	const Preview kPreviews[] = {
#include "data\SpawnerPreviews.inc"
	};

	const char* const kHorseModels[] = {
#include "data\HorseModels.inc"
	};

	// a_c_horse_<breed>_<coat>: the breed's picture in cmpndm_horses.
	struct Breed
	{
		const char* prefix;
		const char* texture;
	};
	const Breed kBreeds[] = {
		{ "a_c_horse_americanpaint", "cmpndm_ampaint" },
		{ "a_c_horse_americanstandardbred", "cmpndm_amstdbred" },
		{ "a_c_horse_andalusian", "cmpndm_andalusian" },
		{ "a_c_horse_appaloosa", "cmpndm_appaloosa" },
		{ "a_c_horse_arabian", "cmpndm_arabian" },
		{ "a_c_horse_ardennes", "cmpndm_ardennes" },
		{ "a_c_horse_belgian", "cmpndm_beldraft" },
		{ "a_c_horse_dutchwarmblood", "cmpndm_dutchwm" },
		{ "a_c_horse_hungarianhalfbred", "cmpndm_hunghalf" },
		{ "a_c_horse_kentuckysaddle", "cmpndm_kysaddler" },
		{ "a_c_horse_missourifoxtrotter", "cmpndm_mofoxtrot" },
		{ "a_c_horse_morgan", "cmpndm_morgan" },
		{ "a_c_horse_mustang", "cmpndm_mustang" },
		{ "a_c_horse_nokota", "cmpndm_nokota" },
		{ "a_c_horse_shire", "cmpndm_shire" },
		{ "a_c_horse_suffolkpunch", "cmpndm_sufpunch" },
		{ "a_c_horse_tennesseewalker", "cmpndm_tnwalker" },
		{ "a_c_horse_thoroughbred", "cmpndm_thorobred" },
		{ "a_c_horse_turkoman", "cmpndm_turkoman" },
	};

	struct Picture
	{
		int variant;
		const char* dict;
		const char* texture;
	};

	std::string Lower(std::string_view s)
	{
		std::string out(s);
		for (char& c : out)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	// Model hash -> its pictures, in table order.
	const std::unordered_map<unsigned int, std::vector<Picture>>& Pictures()
	{
		static const auto table = [] {
			std::unordered_map<unsigned int, std::vector<Picture>> t;
			for (const Preview& p : kPreviews)
				t[GameUtil::Joaat(p.model)].push_back({ p.variant, p.dict, p.texture });
			for (const char* model : kHorseModels)
			{
				const std::string name = Lower(model);
				for (const Breed& b : kBreeds)
					if (name.starts_with(b.prefix))
						t[GameUtil::Joaat(model)].push_back({ -1, "cmpndm_horses", b.texture });
			}
			return t;
		}();
		return table;
	}

	const Picture* Find(unsigned int model, int variant)
	{
		const auto& table = Pictures();
		const auto it = table.find(model);
		if (it == table.end() || it->second.empty())
			return nullptr;
		for (const Picture& p : it->second)
			if (p.variant == variant)
				return &p;
		return &it->second.front();
	}
}

namespace Previews
{
	// Rampage's layout: a 0.13 square right of the menu (left of it when
	// the menu sits right of 0.57), 0.09 below its top.
	void Draw(unsigned int model, int variant, std::string_view caption)
	{
		const MenuStyle& style = Style();
		const float x = style.left + (style.left <= 0.57f ? 0.3095f : -0.0825f);
		const float y = style.top + 0.09f;
		constexpr float kSize = 0.13f;
		const Picture* picture = Find(model, variant);
		const ColorRgba white { 255, 255, 255, 255 };
		GRAPHICS::SET_SCRIPT_GFX_DRAW_ORDER(4);
		if (picture)
			DrawMenuSprite(picture->dict, picture->texture, x, y, kSize, kSize, 0.0f, white);
		else
			DrawMenuSprite("Social_Club", "missing_image", x, y, kSize, kSize, 0.0f, white);
		const std::string text = std::format("{} | 0x{:X}", caption, model);
		DrawMenuText(text, x, y + kSize / 2.0f + 0.004f, 0.25f, white, nullptr, TextAlign::Center);
	}
}
