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
	void BuildHorse(MenuBase* root);
	void BuildWeapons(MenuBase* root);
	void BuildVehicle(MenuBase* root);
	void BuildTeleport(MenuBase* root);
	void BuildSpawner(MenuBase* root);
	void BuildWorld(MenuBase* root);
	void BuildRecovery(MenuBase* root);
	void BuildRecoveryUnlocks(MenuBase* recovery); // Unlocks.cpp
	void BuildMiscellaneous(MenuBase* root);
	void BuildScriptTools(MenuBase* root);
	void BuildSettings(MenuBase* root);

	// The Give Weapon list's weapon names (Weapons.cpp).
	std::span<const char* const> WeaponNames();
}
