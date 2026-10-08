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
	void BuildWeaponSubmenus(MenuBase* weapons); // WeaponSubmenus.cpp
	void BuildWeaponExtras(MenuBase* weapons, MenuBase* manage, MenuBase* ammo, MenuBase* mods); // WeaponSubmenus.cpp
	void BuildVehicle(MenuBase* root);
	void BuildTeleport(MenuBase* root);
	void BuildSpawner(MenuBase* root);
	void BuildObjectSpawner(MenuBase* spawner); // ObjectSpawner.cpp
	void BuildWorld(MenuBase* root);
	void BuildWorldSubmenus(MenuBase* world); // WorldSubmenus.cpp
	void BuildRecovery(MenuBase* root);
	void BuildRecoveryUnlocks(MenuBase* recovery); // Unlocks.cpp
	void BuildRecoveryCollectibles(MenuBase* recovery); // Collectibles.cpp
	void BuildMiscellaneous(MenuBase* root);
	void BuildScriptTools(MenuBase* root);
	void BuildSettings(MenuBase* root);
	// Settings.cpp. RegisterSettings creates the "general", "style",
	// "themes" and "hotkeys" parts of Rampagio.json (before
	// Settings::Initialize); ApplyLoadedSettings applies the loaded command
	// states, honouring settings.restoretoggles unless restoreAll (Load
	// Settings restores everything); TickSettings runs hotkeys and overlays
	// every frame.
	void RegisterSettings();
	void ApplyLoadedSettings(bool restoreAll = false);
	void TickSettings();

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

	// The ped the shared ped submenus act on (PedEditor.cpp). The Player
	// submenus in PlayerSubmenus.cpp, PlayerActions.cpp and Wardrobe.cpp
	// call Target::Get() instead of PLAYER_PED_ID(); the Ped Editor and
	// the Horse menu link to the same menus and Bind themselves, so a row
	// reached through them acts on their ped. Per-frame ticks run with the
	// menu closed, so they fall back to the player.
	namespace Target
	{
		// Rows reached through `menu` act on getter() (while it exists).
		void Bind(MenuBase* menu, std::function<Ped()> getter);
		// The ped of the innermost bound menu on the open menu stack, else
		// the player.
		Ped Get();
	}

	// The Player submenus the Ped Editor and Horse menu link to, filled in
	// by their builders.
	struct SharedMenus
	{
		MenuBase* scenarios = nullptr;
		MenuBase* animations = nullptr;
		MenuBase* effects = nullptr;
		MenuBase* emotes = nullptr;
		MenuBase* speech = nullptr;
		MenuBase* voice = nullptr;
		MenuBase* moods = nullptr;
		MenuBase* walkStyles = nullptr;
		MenuBase* damagePacks = nullptr;
		MenuBase* outfits = nullptr;
		MenuBase* components = nullptr;
		MenuBase* metaTags = nullptr;
		MenuBase* metaExpressions = nullptr;
		MenuBase* overlays = nullptr;
	};
	SharedMenus& Shared();

	namespace PedEditor
	{
		void Build(); // once, after the Player menus
		// Opens the Ped Editor on `ped`.
		void Open(Ped ped);
	}
}
