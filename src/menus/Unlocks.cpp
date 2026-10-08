/*
	Recovery > Unlocks: ports Rampage's Submenus::SubRecoveryUnlocks,
	SubUnlockCheats and SubMapDiscoverables.

	Cheats go through the game's own cheat state, the one cheat_ui (the
	pause menu's cheat list) and short_update share in Global_1425247:
	  [0..1]        bit per cheat: activation requested/active
	  .f_12[id]     state: 0 locked, 2 off, 3 on, 4 activate, 5 deactivate
	  .f_53         "cheats used" flag that blocks saving and missions
	and the saved unlock bits in Global_40.f_12000. short_update runs the
	cheat on state 4 and moves it to 3, and turns it off on 5. Unlock
	Outfits / Unlock Recipes request cheats 7 / 8 that way, as Rampage does.

	The map discovery, unlock and compendium lists come from the game
	scripts (tools/extract_unlocks.py), not Rampage's tables.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\NativeHooks.h"

#include <format>

namespace
{
	// ---- Game cheat state ----

	constexpr int kCheatGlobal = 1425247;
	constexpr int kCheatStates = kCheatGlobal + 12 + 1;   // .f_12[id]
	constexpr int kCheatUsed = kCheatGlobal + 53;         // .f_53
	constexpr int kCheatUnlockBits = 40 + 12000;          // Global_40.f_12000

	enum CheatState { kLocked = 0, kOff = 2, kOn = 3, kActivate = 4, kDeactivate = 5 };

	constexpr int kCheatOutfits = 7;
	constexpr int kCheatRecipes = 8;

	int* GlobalInt(int index)
	{
		return reinterpret_cast<int*>(GameUtil::Global(index));
	}

	// Script bitsets keep 31 bits per word.
	bool TestBit(int base, int bit)
	{
		const int* word = GlobalInt(base + bit / 31);
		return word && (*word & (1 << (bit % 31)));
	}

	void SetBit(int base, int bit, bool on)
	{
		if (int* word = GlobalInt(base + bit / 31))
			*word = on ? (*word | (1 << (bit % 31))) : (*word & ~(1 << (bit % 31)));
	}

	int GetCheatState(int id)
	{
		const int* state = GlobalInt(kCheatStates + id);
		return state ? *state : kLocked;
	}

	void SetCheatState(int id, int state)
	{
		// The scripts' own globals aren't safe to touch mid-load.
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		if (int* slot = GlobalInt(kCheatStates + id))
			*slot = state;
	}

	void ClearCheatUsed()
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		if (int* used = GlobalInt(kCheatUsed))
			*used = 0;
	}

	// ---- SubUnlockCheats ----

	// cheat_ui's names for cheat ids 0..36; each is also the text label of
	// the cheat's in-game name.
	const char* const kCheats[] = {
		"CHEAT_INFINITE_AMMO", "CHEAT_BASIC_WEAPONS", "CHEAT_HEAVY_WEAPONS", "CHEAT_STEALTH_WEAPONS",
		"CHEAT_GUNSLINGER", "CHEAT_REMOVE_FOG_OF_WAR", "CHEAT_ADD_MONEY", "CHEAT_UNLOCK_ALL_PLAYER_OUTFITS",
		"CHEAT_UNLOCK_ALL_RECIPES", "CHEAT_INCREASE_CAMP_UPGRADES_TO_MAX", "CHEAT_INCREASE_HONOR_TO_MAX",
		"CHEAT_DECREASE_HONOR_TO_MIN", "CHEAT_RESET_HONOR_TO_NEUTRAL", "CHEAT_INFINITE_DEADEYE",
		"CHEAT_INFINITE_STAMINA", "CHEAT_SET_DEADEYE_TO_LEVEL_1", "CHEAT_SET_DEADEYE_TO_LEVEL_2",
		"CHEAT_SET_DEADEYE_TO_LEVEL_3", "CHEAT_SET_DEADEYE_TO_LEVEL_4", "CHEAT_SET_DEADEYE_TO_LEVEL_5",
		"CHEAT_SET_RPG_TANKS_TO_FULL", "CHEAT_SET_RPG_TANK_STAT_LEVEL_TO_FULL",
		"CHEAT_SET_RPG_TANKS_TO_FULL_AND_OVERPOWERED", "CHEAT_INCREASE_HORSE_BOND_TO_MAX",
		"CHEAT_SET_HORSE_TO_SPAWN_NEAR_PLAYER_ON_WHISTLE", "CHEAT_BECOME_DRUNK", "CHEAT_SPAWN_RACE_HORSE",
		"CHEAT_SPAWN_WAR_HORSE", "CHEAT_SPAWN_SUPERIOR_HORSE", "CHEAT_SPAWN_RANDOM_HORSE",
		"CHEAT_SPAWN_STAGECOACH", "CHEAT_SPAWN_WAGON", "CHEAT_SPAWN_BUGGY", "CHEAT_SPAWN_CIRCUS_WAGON",
		"CHEAT_INCREASE_WANTED_LEVEL_BY_1", "CHEAT_DECREASE_WANTED_LEVEL_BY_1",
		"CHEAT_CLEAR_ALL_BOUNTIES_AND_LOCKDOWN_AREAS",
	};
	constexpr int kCheatCount = static_cast<int>(std::size(kCheats));

	std::string CheatStateName(int state)
	{
		switch (state)
		{
		case kLocked: return "Locked";
		case kOff: return "Off";
		case kOn: return "~COLOR_GREEN~On";
		case kActivate: return "Activating";
		case kDeactivate: return "Deactivating";
		default: return std::format("State {}", state);
		}
	}

	// What entering the cheat's phrase and picking it in the pause menu
	// does: unlock it (saved bit, state Off), set its activation bit, then
	// request it. An active cheat is turned off instead. Unlike the pause
	// menu we don't check cheat_ui's location rules (shops, camp, Guarma)
	// or switch off the other cheats in its group (dead eye levels, ...).
	std::string ToggleCheat(int id)
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return "";
		const int state = GetCheatState(id);
		if (state == kOn || state == kActivate)
		{
			SetBit(kCheatGlobal, id, false);
			SetCheatState(id, kDeactivate);
			return "Cheat deactivated";
		}
		SetBit(kCheatUnlockBits, id, true);
		SetBit(kCheatGlobal, id, true);
		SetCheatState(id, kActivate);
		return "Cheat activated";
	}

	// Bypass Cheat Restrictions: keeps the "cheats used" flag clear while
	// on, so saving and missions stay available; restores it when off.
	int g_savedCheatUsed = 0;

	void SetBypassCheatRestrictions(bool on)
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		int* used = GlobalInt(kCheatUsed);
		if (!used)
			return;
		if (on)
			g_savedCheatUsed = *used;
		else
			*used = g_savedCheatUsed;
	}

	void BuildCheats(MenuBase* cheats)
	{
		Ui::Toggle(cheats, "unlocks.bypasscheatrestrictions", "Bypass Cheat Restrictions", SetBypassCheatRestrictions, ClearCheatUsed);
		Ui::Section(cheats, "Cheats");
		for (int id = 0; id < kCheatCount; id++)
		{
			// The game's text, looked up on first draw.
			cheats->AddItem(new MenuItemActionStatus(
				[id, name = std::string()]() mutable {
					if (name.empty())
						name = GameUtil::ItemName(GameUtil::Joaat(kCheats[id]), kCheats[id]);
					return std::format("{}: {}", name, CheatStateName(GetCheatState(id)));
				},
				[id] { return ToggleCheat(id); }));
		}
	}

	// ---- SubRecoveryUnlocks ----

	std::string RevealMap()
	{
		MAP::SET_MINIMAP_HIDE_FOW(TRUE);
		MAP::_REVEAL_MINIMAP_FOW(0);
		return "Entire map revealed";
	}

	std::string ResetMap()
	{
		MAP::SET_MINIMAP_HIDE_FOW(FALSE);
		MAP::RESET_MINIMAP_FOW(0);
		return "Map reset";
	}

	// Requests a game cheat for a few seconds (short_update picks it up),
	// then puts its state back. Rampage sets it to Locked afterwards
	// instead.
	void RunCheatOnce(int id, int waitMs)
	{
		const int before = GetCheatState(id);
		SetCheatState(id, kActivate);
		WAIT(waitMs);
		SetCheatState(id, before);
	}

	void ShowSpinner(const char* text)
	{
		HUD::_BUSYSPINNER_SET_TEXT(MISC::VAR_STRING(10, "LITERAL_STRING", text));
	}

	// The outfits cheat gives one outfit, CLOTHING_OUTFIT_NEW_RHDSHOP_002_L
	// (0x843285E8), that Rampage swaps for 0x791E3638 on the first pass:
	// the item is removed first, and _INVENTORY_ADD_ITEM_WITH_GUID's item
	// argument is replaced while the cheat runs. A second pass without the
	// swap follows.
	constexpr std::uint64_t kAddWithGuid = 0xCB5D11F9508A928D; // _INVENTORY_ADD_ITEM_WITH_GUID
	constexpr Hash kOutfitSwapFrom = 0x843285E8;
	constexpr Hash kOutfitSwapTo = 0x791E3638;
	rage::scrNativeHandler g_addWithGuidOriginal = nullptr;

	void SwapOutfitOnAdd(rage::scrNativeCallContext* ctx)
	{
		if (ctx->GetArg<Hash>(3) == kOutfitSwapFrom)
			ctx->SetArg(3, kOutfitSwapTo);
		g_addWithGuidOriginal(ctx);
	}

	std::string UnlockOutfits()
	{
		ShowSpinner("Unlocking Outfits");
		INVENTORY::_INVENTORY_REMOVE_INVENTORY_ITEM_WITH_ITEMID(GameUtil::kInventorySp, kOutfitSwapFrom, 1, GameUtil::kRemoveReasonDefault);
		g_addWithGuidOriginal = NativeHooks::Original(kAddWithGuid);
		NativeHooks::Id swap = g_addWithGuidOriginal ? NativeHooks::Add(NativeHooks::kAllScripts, kAddWithGuid, SwapOutfitOnAdd) : 0;
		RunCheatOnce(kCheatOutfits, 3000);
		WAIT(2000);
		NativeHooks::Remove(swap);
		RunCheatOnce(kCheatOutfits, 3000);
		ClearCheatUsed();
		HUD::BUSYSPINNER_OFF();
		return "All outfits unlocked. ~COLOR_YELLOW~Save and reload~s~ your game.";
	}

	std::string UnlockRecipes()
	{
		RunCheatOnce(kCheatRecipes, 3000);
		ClearCheatUsed();
		return "All recipes unlocked. ~COLOR_YELLOW~Save and reload~s~ your game.";
	}

	// Every weapon's catalogue unlock, for the weapons in our Give Weapon
	// list.
	std::string UnlockWeapons()
	{
		int count = 0;
		for (const char* name : Menus::WeaponNames())
		{
			const Hash unlock = WEAPON::_GET_WEAPON_UNLOCK(GameUtil::Joaat(name));
			if (!unlock)
				continue;
			UNLOCK::UNLOCK_SET_UNLOCKED(unlock, TRUE);
			UNLOCK::UNLOCK_SET_VISIBLE(unlock, TRUE);
			count++;
		}
		return std::format("Unlocked {} weapons", count);
	}

	// A row that shows a game flag and flips it on select. Not a Ui::Toggle:
	// the online kill switch must not turn these off.
	void StateToggle(MenuBase* menu, const std::string& caption, bool state, std::function<void(bool)> onChange)
	{
		auto* toggle = new MenuItemToggle(caption, std::move(onChange));
		toggle->SetState(state);
		menu->AddItem(toggle);
	}

	struct NamedHash { const char* name; const char* label; };

	// "0x..." entries are raw hashes the scripts never name.
	Hash HashOf(const NamedHash& entry) { return GameUtil::ParseHash(entry.name); }

	const NamedHash kUnlocks[] = {
#include "..\data\UnlockNames.inc"
	};

	void SetUnlocked(Hash unlock, bool on)
	{
		UNLOCK::UNLOCK_SET_UNLOCKED(unlock, on);
		UNLOCK::UNLOCK_SET_VISIBLE(unlock, on);
	}

	// ---- Compendium ----

	struct CompendiumEntry { const char* kind; const char* name; };
	const CompendiumEntry kCompendium[] = {
#include "..\data\Compendium.inc"
	};

	template <typename F>
	void ForEachOfKind(std::string_view kind, F&& f)
	{
		for (const CompendiumEntry& entry : kCompendium)
			if (kind == entry.kind)
				f(GameUtil::Joaat(entry.name));
	}

	void IncrementStat(Hash stat, Hash item)
	{
		GameUtil::StatId id{ stat, item };
		STATS::_STAT_ID_INCREMENT_INT(id.Ptr(), 1);
	}

	// Spawns `model` 5 m in front of the player, hands it to `use`, then
	// deletes it. False if the model doesn't load.
	template <typename F>
	bool WithSpawnedPed(Hash model, F&& use)
	{
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !GameUtil::LoadModel(model))
			return false;
		const Ped me = PLAYER::PLAYER_PED_ID();
		const Vector3 pos = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(me, 0.0f, 5.0f, 0.0f);
		Ped ped = PED::CREATE_PED(model, pos.x, pos.y, pos.z, ENTITY::GET_ENTITY_HEADING(me), FALSE, FALSE, FALSE, FALSE);
		PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
		use(ped);
		PED::DELETE_PED(&ped);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return true;
	}

	// What herb.ysc does when a herb is picked, plus the used-in-recipe
	// stat Rampage also bumps.
	void DiscoverHerbs()
	{
		ShowSpinner("Unlocking Herbs");
		ForEachOfKind("Herb", [](Hash herb) {
			COMPENDIUM::COMPENDIUM_HERB_PICKED(herb, 0.0f, 0.0f, 0.0f);
			IncrementStat(GameUtil::Joaat("Pick"), herb);
			IncrementStat(GameUtil::Joaat("used_in_recipe"), herb);
		});
		HUD::BUSYSPINNER_OFF();
	}

	// Observed, plus one of each per-animal stat the compendium reads.
	void DiscoverAnimals()
	{
		ShowSpinner("Unlocking Animals");
		const Hash stats[] = {
			GameUtil::Joaat("Tracked"), GameUtil::Joaat("killed"), GameUtil::Joaat("skinned"),
			GameUtil::Joaat("plucked"), GameUtil::Joaat("COLLECTED"),
		};
		ForEachOfKind("Animal", [&stats](Hash animal) {
			COMPENDIUM::COMPENDIUM_ANIMAL_OBSERVED_BY_STAT_NAME(animal, FALSE);
			for (Hash stat : stats)
				IncrementStat(stat, animal);
		});
		HUD::BUSYSPINNER_OFF();
	}

	// Each horse model is spawned, observed, broken in and fully bonded.
	void DiscoverHorses()
	{
		ShowSpinner("Unlocking Horses");
		ForEachOfKind("Horse", [](Hash model) {
			WithSpawnedPed(model, [](Ped horse) {
				COMPENDIUM::COMPENDIUM_HORSE_OBSERVED(horse, FALSE);
				COMPENDIUM::COMPENDIUM_HORSE_WILD_BROKEN(horse);
				COMPENDIUM::COMPENDIUM_HORSE_BONDING(horse, 4);
			});
		});
		HUD::BUSYSPINNER_OFF();
	}

	// Each fish model is spawned and caught once with every bait, using
	// the stat category fishing_core gives a legendary lure catch.
	// Rampage also equips a per-fish outfit preset (legendary variants);
	// we spawn the model's default.
	void DiscoverFish()
	{
		ShowSpinner("Unlocking Fish");
		ForEachOfKind("Fish", [](Hash model) {
			WithSpawnedPed(model, [](Ped fish) {
				WAIT(150);
				STATS::_STAT_ITEM_FISH_CAUGHT(fish, 46.7f, GameUtil::Joaat("legendary_lure"), GameUtil::Joaat("potent_predator_bait"));
				ForEachOfKind("Bait", [fish](Hash bait) { COMPENDIUM::COMPENDIUM_FISH_CAUGHT(fish, bait); });
				WAIT(150);
			});
		});
		COMPENDIUM::COMPENDIUM_ANIMAL_OBSERVED_BY_STAT_NAME(GameUtil::Joaat("at_fchannel_legendary"), TRUE);
		HUD::BUSYSPINNER_OFF();
	}

	// The six compendium gangs, in the order the scripts' gang-id switch
	// lists them.
	const char* const kGangs[] = {
		"gang_odriscoll", "gang_inbred", "gang_exconfed", "gang_savages", "gang_ranchers", "gang_banditos",
	};

	void DiscoverGangs()
	{
		for (const char* name : kGangs)
		{
			const Hash gang = GameUtil::Joaat(name);
			COMPENDIUM::COMPENDIUM_GANG_CAMP_FOUND(gang, 0);
			COMPENDIUM::COMPENDIUM_GANG_AMBUSH_SURVIVED(gang);
			COMPENDIUM::COMPENDIUM_GANG_BOUNTY_CAPTURED(gang);
			COMPENDIUM::COMPENDIUM_GANG_HIDEOUT_FOUND(gang, 0);
			COMPENDIUM::COMPENDIUM_GANG_MEMBER_KILLED(gang);
		}
	}

	// Cigarette cards come from the game's own collectable list.
	const Hash kCigaretteCards = GameUtil::Joaat("CIGARETTE_CARDS");

	std::string DiscoverCigCards()
	{
		const int total = COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_COLLECTABLES(kCigaretteCards, 0);
		if (COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_FOUND(kCigaretteCards, 0) == total)
			return "~COLOR_RED~Error:~s~ You already discovered all Cigcards.";
		for (int i = 0; i < total; i++)
		{
			const Hash card = COLLECTABLE::_COLLECTABLE_GET_COLLECTABLE_ITEM_HASH(i, kCigaretteCards, 0);
			COLLECTABLE::_COLLECTABLE_SET_ITEM_HASH_DISCOVERED(card);
			COLLECTABLE::_COLLECTABLE_INCREMENT_NUM_FOUND(card, 1);
		}
		return "All Cigcards have been set to discovered";
	}

	// Writes every journal entry the game currently allows (Rampage writes
	// its own fixed list).
	std::string AddJournalEntries()
	{
		int count = 0;
		ForEachOfKind("Journal", [&count](Hash entry) {
			if (HUD::_JOURNAL_CAN_WRITE_ENTRY(entry))
			{
				HUD::_JOURNAL_WRITE_ENTRY(entry);
				count++;
			}
		});
		return std::format("Wrote {} journal entries", count);
	}

	// "Discover X (n)": n is the game's count for the compendium category,
	// the number Rampage shows against its own totals.
	void CompendiumRow(MenuBase* menu, const char* label, const char* category, std::function<void()> discover)
	{
		const Hash hash = GameUtil::Joaat(category);
		menu->AddItem(new MenuItemActionStatus(
			[label, hash] { return std::format("{} ({})", label, COMPENDIUM::_COMPENDIUM_GET_NUM_OF_ENTRIES_IN_CATEGORY(hash)); },
			[discover] { discover(); return std::string(); }));
	}

	// ---- SubMapDiscoverables ----

	const NamedHash kMapDiscoveries[] = {
#include "..\data\MapDiscoveries.inc"
	};

	// "Active" means discovered. alloc8or's _MAP_DISCOVERY_SET_ENABLED is
	// what Rampage (and the scripts) call to undiscover one.
	bool IsDiscovered(Hash discovery) { return MAP::_MAP_IS_DISCOVERY_ACTIVE(discovery) != FALSE; }

	void SetDiscovered(Hash discovery, bool on)
	{
		if (on == IsDiscovered(discovery))
			return;
		if (on)
			MAP::_MAP_DISCOVER_REGION(discovery);
		else
			MAP::_MAP_DISCOVERY_SET_ENABLED(discovery);
	}

	void BuildMapDiscoverables(MenuBase* menu)
	{
		Ui::Action(menu, "Enable All", [] {
			for (const NamedHash& entry : kMapDiscoveries)
				SetDiscovered(HashOf(entry), true);
			return std::string("All discovered");
		});
		Ui::Action(menu, "Disable All", [] {
			for (const NamedHash& entry : kMapDiscoveries)
				SetDiscovered(HashOf(entry), false);
			return std::string("All undiscovered");
		});
		Ui::Section(menu, "Discoveries");
		for (const NamedHash& entry : kMapDiscoveries)
		{
			const Hash discovery = HashOf(entry);
			StateToggle(menu, GameUtil::ItemName(discovery, entry.label), IsDiscovered(discovery),
				[discovery](bool on) { SetDiscovered(discovery, on); });
		}
	}
}

namespace Menus
{
	void BuildRecoveryUnlocks(MenuBase* recovery)
	{
		MenuBase* unlocks = Ui::Submenu(recovery, "Unlocks");
		// Ours: lists the cheats by their in-game names and activates them,
		// instead of showing each cheat's phrase.
		BuildCheats(Ui::Submenu(unlocks, "Cheat Codes"));
		Ui::Action(unlocks, "unlocks.revealmap", "Reveal Map", RevealMap);
		Ui::Action(unlocks, "unlocks.resetmap", "Reset Map", ResetMap);
		Ui::ListMenu(unlocks, "Map Discoverables", BuildMapDiscoverables);
		Ui::Action(unlocks, "unlocks.unlockoutfits", "Unlock Outfits", UnlockOutfits);
		Ui::Action(unlocks, "unlocks.unlockweapons", "Unlock Weapons", UnlockWeapons);
		Ui::Action(unlocks, "unlocks.unlockrecipes", "Unlock Recipes", UnlockRecipes);
		Ui::Section(unlocks, "Compendium");
		CompendiumRow(unlocks, "Discover Herbs", "herbs", DiscoverHerbs);
		CompendiumRow(unlocks, "Discover Horses", "horses", DiscoverHorses);
		CompendiumRow(unlocks, "Discover Animals", "ANIMALS", DiscoverAnimals);
		CompendiumRow(unlocks, "Discover Fish", "FISH", DiscoverFish);
		CompendiumRow(unlocks, "Discover Gangs", "GANGS", DiscoverGangs);
		unlocks->AddItem(new MenuItemActionStatus(
			[] {
				return std::format("Discover Cigcards ({}/{})",
					COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_FOUND(kCigaretteCards, 0),
					COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_COLLECTABLES(kCigaretteCards, 0));
			},
			DiscoverCigCards));
		Ui::Section(unlocks, "Journal");
		Ui::Action(unlocks, "unlocks.addentries", "Add Entries", AddJournalEntries);
		Ui::Action(unlocks, "unlocks.resetjournal", "~COLOR_RED~Reset Journal", [] {
			HUD::_JOURNAL_CLEAR_ALL_PROGRESS();
			return std::string("Journal reset");
		});
		// Ours: every unlock the scripts name, read when the list opens.
		Ui::ListMenu(unlocks, "Unlock Checks", [](MenuBase* list) {
			for (const NamedHash& entry : kUnlocks)
			{
				const Hash unlock = HashOf(entry);
				StateToggle(list, entry.label, UNLOCK::UNLOCK_IS_UNLOCKED(unlock) != FALSE,
					[unlock](bool on) { SetUnlocked(unlock, on); });
			}
		});
	}
}
