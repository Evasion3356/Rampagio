/*
	The menu's feature implementations. Each one-shot action returns a short
	status string for MenuItemActionStatus (empty = show nothing); each toggle
	has an OnChange(bool) and, if it has to be re-applied, an OnTick().

	Where Rampage has the same feature, the native sequence mirrors what its
	decompiled code does (see CLAUDE.md, "Porting a feature from Rampage").
	None of these are copied code -- they're re-implemented from which
	natives Rampage calls and in what order.
*/

#pragma once

#include <string>

namespace Features
{
	// Player
	std::string HealPlayer();
	std::string CleanPlayer();
	std::string ClearBounty();
	void InvinciblePlayer_OnChange(bool on);

	// Horse
	std::string HealHorse();
	void InvincibleHorse_OnChange(bool on);
	void InvincibleHorse_OnTick();

	// Teleport
	std::string TeleportToWaypoint();

	// World
	std::string AddClockHours(int hours);
}
