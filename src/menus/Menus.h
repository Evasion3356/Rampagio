/*
	One builder per top-level menu area, in Rampage's main-menu order. Each
	area's .cpp holds both its rows and their implementations. The Rampage
	submenu a block of rows ports is named in a comment above it
	(Submenus::SubXxx; see tools/rampage_inventory.md).
*/

#pragma once

#include "..\Menu.h"
#include "..\Localization.h"

#include <functional>
#include <span>
#include <string>

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
	void BuildPlayerFixes(MenuBase* self); // PlayerFixes.cpp
	void BuildHorse(MenuBase* root);
	// HorseLock.cpp: Horse Stats' Keep Cores Golden and Lock Stats rows,
	// holding the main horse's base ranks at the targets' values.
	namespace HorseLock
	{
		struct Target
		{
			int attribute;
			const int* value;
		};
		void Build(MenuBase* stats, std::vector<Target> targets);
		bool Locked();
	}
	void BuildWeapons(MenuBase* root);
	void BuildWeaponSubmenus(MenuBase* weapons); // WeaponSubmenus.cpp
	void BuildWeaponExtras(MenuBase* weapons, MenuBase* manage, MenuBase* ammo, MenuBase* mods); // WeaponSubmenus.cpp
	void BuildVehicle(MenuBase* root);
	void BuildTeleport(MenuBase* root);
	// Teleport.cpp: its location table (data/Teleports.inc), which the
	// Teleport Map (TeleportMap.h) shows too, and the teleports the map runs
	// (script thread): to a place's exact spot, or to the ground at x, y.
	// Both move the mount or vehicle along with the player.
	struct TeleportPlace
	{
		const char* menu;    // submenu of Teleport
		const char* nested;  // submenu inside it, or ""
		const char* section; // section header before the row, or ""
		const char* idPrefix;
		const char* name;
		float x, y, z;
	};
	std::span<const TeleportPlace> TeleportPlaces();
	void TeleportToPlace(float x, float y, float z);
	std::string TeleportToGround(float x, float y);
	void BuildSpawner(MenuBase* root);
	void BuildObjectSpawner(MenuBase* spawner); // ObjectSpawner.cpp
	void BuildWorld(MenuBase* root);
	void BuildWorldSubmenus(MenuBase* world); // WorldSubmenus.cpp
	void BuildRecovery(MenuBase* root);
	void BuildRecoveryUnlocks(MenuBase* recovery); // Unlocks.cpp
	void BuildRecoveryCollectibles(MenuBase* recovery); // Collectibles.cpp
	// Challenges.cpp: Recovery > Challenges; TickChallenges runs every frame
	// (main loop), ShutdownChallenges removes its hook on eject (DllMain).
	void BuildRecoveryChallenges(MenuBase* recovery);
	void TickChallenges();
	void ShutdownChallenges();
	void BuildMiscellaneous(MenuBase* root);
	// Minigames.cpp: the sibling minigame mods under Misc > Minigames.
	// ShutdownMinigames runs first thing on DLL_PROCESS_DETACH: it stops the
	// dominoes search worker and, on an eject, takes fillet_sp's patches out.
	void BuildMinigames(MenuBase* minigames);
	void ShutdownMinigames(bool processExit);
	void BuildScriptTools(MenuBase* root);
	// Debug.cpp: Debug > Script Monitor (src/debug/ScriptMonitor.h).
	void BuildDebug(MenuBase* root);
	void BuildSettings(MenuBase* root);
	// Settings.cpp. RegisterSettings creates the "general", "style"
	// and "themes" parts of Rampagio.json and calls RegisterHotkeys (before
	// Settings::Initialize); ApplyLoadedSettings applies the loaded command
	// states (ticked toggles come back on); TickSettings runs hotkeys and
	// overlays every frame.
	void RegisterSettings();
	void ApplyLoadedSettings();
	void TickSettings();
	// Hotkeys.cpp (docs/HOTKEYS_PLAN.md): Settings > Hotkeys, the F11 Hotkey
	// window (ImGui overlay) and Y-on-a-pad binding, and running the
	// bindings every frame (TickHotkeys, from TickSettings). RegisterHotkeys
	// creates the "hotkeys" and "presets" parts of Rampagio.json and the
	// window (from RegisterSettings); SuspendHotkeys closes the window and
	// ends a capture (online kill switch, eject). CaptureKey waits for one
	// keyboard key (the menu key row).
	inline constexpr DWORD kHotkeyBindKey = VK_F11;
	void BuildHotkeys(MenuBase* settings);
	void RegisterHotkeys();
	void TickHotkeys();
	void SuspendHotkeys();
	void CaptureKey(const std::string& prompt, std::function<void(int)> done);

	// The player's posse (Posse.cpp), shared with the spawners.
	namespace Posse
	{
		// Makes `ped` a mission entity and a bodyguard in the player's group.
		void Add(Ped ped);
		// Living members.
		const std::vector<Ped>& Members();
	}

	// The Ped Spawner's database and the vehicles the Vehicle Spawner made
	// (Spawner.cpp), saved and loaded with the Object Spawner's database.
	namespace SpawnerDb
	{
		struct Entry
		{
			Entity entity;
			std::string model;
		};
		// Entries whose entity still exists.
		std::vector<Entry> Peds();
		std::vector<Entry> Vehicles();
		void AddPed(Ped ped, const std::string& model);
		void AddVehicle(Vehicle vehicle, const std::string& model);
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
