/*
	Settings > Theme > Spawner Previews (Rampage's SubSettingsXUI row of the
	same name): the game's compendium picture of the selected animal, fish
	or horse, drawn beside the menu with the row's caption and model hash.
	Rows opt in with Ui::Preview (Menu.h).

	Pictures come from the game's own texture dictionaries: animals and
	fish from Rampage's tables (data/SpawnerPreviews.inc, by model and
	outfit preset), horses one per breed from cmpndm_horses. A row with no
	picture shows Social_Club's missing_image, as Rampage's does.
*/

#pragma once

#include <string_view>

namespace Previews
{
	// variant: the row's outfit preset, -1 for any (the model's first entry).
	void Draw(unsigned int model, int variant, std::string_view caption);
}
