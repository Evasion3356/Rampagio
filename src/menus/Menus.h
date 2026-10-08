/*
	One builder per top-level menu area, in Rampage's main-menu order. Each
	area's .cpp holds both its rows and their implementations. The Rampage
	submenu a block of rows ports is named in a comment above it
	(Submenus::SubXxx; see tools/rampage_inventory.md).
*/

#pragma once

#include "..\Menu.h"

#include <span>

namespace Menus
{
	void BuildPlayer(MenuBase* root);
	void BuildPlayerSubmenus(MenuBase* self); // PlayerSubmenus.cpp
	// PlayerActions.cpp, called from BuildPlayerSubmenus in Rampage's order.
	void BuildPlayerAnimations(MenuBase* self);
	void BuildPlayerEffects(MenuBase* self);
	void BuildPlayerEmotes(MenuBase* self);
	void BuildPlayerSpeech(MenuBase* self);
	// Wardrobe.cpp: the Wardrobe rows before and after Walk Styles / Damage Packs.
	void BuildWardrobeTop(MenuBase* wardrobe);
	void BuildWardrobe(MenuBase* wardrobe);
	void BuildModelChanger(MenuBase* wardrobe); // ModelChanger.cpp
	void BuildPlayerPosse(MenuBase* self); // Posse.cpp
	void BuildHorse(MenuBase* root);
	void BuildWeapons(MenuBase* root);
	void BuildVehicle(MenuBase* root);
	void BuildTeleport(MenuBase* root);
	void BuildSpawner(MenuBase* root);
	void BuildWorld(MenuBase* root);
	void BuildRecovery(MenuBase* root);
	void BuildRecoveryUnlocks(MenuBase* recovery); // Unlocks.cpp
	void BuildRecoveryCollectibles(MenuBase* recovery); // Collectibles.cpp
	void BuildMiscellaneous(MenuBase* root);
	void BuildScriptTools(MenuBase* root);
	void BuildSettings(MenuBase* root);

	// The player's posse (Posse.cpp), shared with the spawners.
	namespace Posse
	{
		// Makes `ped` a mission entity and a bodyguard in the player's group.
		void Add(Ped ped);
		// Living members.
		const std::vector<Ped>& Members();
	}

	// The Give Weapon list's weapon names (Weapons.cpp).
	std::span<const char* const> WeaponNames();
}
