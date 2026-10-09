/*
	Recovery menu: ports Rampage's Submenus::SubRecoveryMoney,
	SubRecoveryHonor, SubRecoveryBounty, SubRecoveryCores and
	SubRecoveryAddItems. The "via Game Script" rows call the same script
	functions Rampage does (flow_controller func_688, func_290, func_299,
	short_update func_583 in the 1491.50 decompile) through our
	ScriptFunction. Unlimited/Max Items hook natives for game scripts
	through NativeHooks.

	Money amounts are typed in dollars and passed to the game in cents, as
	Rampage does.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\Log.h"
#include "..\NativeHooks.h"
#include "..\ScriptFunction.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>
#include <functional>
#include <span>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	// The add reason the game's own scripts pass with cash rewards (e.g.
	// every flow_controller call to func_688).
	constexpr Hash kCashAddReason = 752097756;

	// Prompts for a whole number; nullopt if cancelled or not a number.
	std::optional<int> PromptInt(const char* title, int maxLength)
	{
		std::string text;
		if (!GameUtil::PromptText(title, text, maxLength))
			return std::nullopt;
		int value = 0;
		const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (ec != std::errc() || end != text.data() + text.size())
			return std::nullopt;
		return value;
	}

	// ---- SubRecoveryMoney ----

	// flow_controller func_688(amount, bNoFeed, i2, i3, sLabel, i5, i6, hReason):
	// adds cash and shows the "money earned" feed toast. Position 0x1911E.
	ScriptFunction g_addCashScript("flow_controller",
		"22 08 0A 00 00 66 01 05 8B 0A 00 66 00 66 04 66 05 39 ? ? ? 66 00 66 01 66 02 66 03 66 07 39 ? ? ? 50 08 00");

	std::string AddMoney()
	{
		const auto dollars = PromptInt("Enter $ to Add:", 9);
		if (!dollars)
			return "";
		MONEY::_MONEY_INCREMENT_CASH_BALANCE((std::max)(0, *dollars * 100), kCashAddReason);
		return TrFormat("Added ${}", *dollars);
	}

	std::string RemoveMoney()
	{
		const auto dollars = PromptInt("Enter $ to Remove:", 9);
		if (!dollars)
			return "";
		MONEY::_MONEY_DECREMENT_CASH_BALANCE((std::max)(0, *dollars * 100));
		return TrFormat("Removed ${}", *dollars);
	}

	std::string AddMoneyViaScript()
	{
		const auto dollars = PromptInt("Enter Amount:", 9);
		if (!dollars || *dollars < 0)
			return "";
		if (!g_addCashScript.Call(*dollars * 100, FALSE, 0, 1, "", 0, 1, kCashAddReason))
			return "flow_controller call failed (see log)";
		return "";
	}

	// Drop: a pickup worth `g_dropAmount` cents near the player every
	// interval while on. Rampage drops one every pass of its feature loop;
	// the interval is ours, to keep the pickup count sane.
	constexpr Hash kPickupMoneyVariable = 0xFE18F3AF; // PICKUP_MONEY_VARIABLE
	constexpr ULONGLONG kDropIntervalMs = 250;
	const std::vector<std::string> kDropModelNames = { "Cash Bundle", "Gold Bar", "Jewelry Sack", "Money Bag" };
	const Hash kDropModels[] = {
		GameUtil::Joaat("p_foldedbills01x"),
		GameUtil::Joaat("s_pickup_goldbar01x"),
		GameUtil::Joaat("s_pickup_jewelrybag02x"),
		GameUtil::Joaat("p_moneybag02x"),
	};
	int g_dropModel = 0;
	int g_dropAmount = 5000;
	long long g_totalDropped = 0;

	void DropMoneyTick()
	{
		static ULONGLONG nextDropMs = 0;
		const ULONGLONG nowMs = GetTickCount64();
		if (nowMs < nextDropMs)
			return;
		nextDropMs = nowMs + kDropIntervalMs;

		const Hash model = kDropModels[g_dropModel];
		if (!GameUtil::LoadModel(model))
			return;
		const Vector3 pos = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		const float x = pos.x + MISC::GET_RANDOM_FLOAT_IN_RANGE(-2.0f, 2.0f);
		const float y = pos.y + MISC::GET_RANDOM_FLOAT_IN_RANGE(-2.0f, 2.0f);
		const Object pickup = OBJECT::CREATE_AMBIENT_PICKUP(kPickupMoneyVariable, x, y, pos.z, 0, g_dropAmount, model, FALSE, TRUE, 0, 0.0f);
		OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(pickup, FALSE);
		g_totalDropped += g_dropAmount;
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	}

	// ---- SubRecoveryHonor ----

	// short_update func_583(amount, bSilent, i2, hReason, sLabel, plPlayer, b6, b7):
	// applies an honor change with its feed message and saves honor_current.
	// Position 0x19210.
	ScriptFunction g_honorScript("short_update", "22 08 14 00 00 39 ? ? ? 67 0A 66 03 37 93 EE 11 44");
	constexpr Hash kHonorScriptReason = 0xBEF3D776; // what Rampage passes

	// The stat short_update writes after every honor change. Rampage reads a
	// different stat hash we couldn't name; this one is the game's own.
	int CurrentHonor()
	{
		GameUtil::StatId statId{ GameUtil::Joaat("honor_current") };
		int value = 0;
		STATS::STAT_ID_GET_INT(statId.Ptr(), &value);
		return value;
	}

	// Honor changes come from kills: the game reads an "honor_override"
	// decorator off the victim. So spawn an invisible ped carrying the
	// amount, make it attack the player, and kill it with an explosion the
	// player owns. A positive override costs honor, a negative one gains it.
	const Hash kHonorPedModel = GameUtil::Joaat("msp_gang2_males_01");
	int g_honorAmount = 50;

	std::string ChangeHonorByKill(int overrideValue)
	{
		if (!GameUtil::LoadModel(kHonorPedModel))
			return "Couldn't load the ped model";
		const Ped me = Me();
		constexpr float kX = 0.0f, kY = 0.0f, kZ = 98.0f;
		Ped ped = PED::CREATE_PED(kHonorPedModel, kX, kY, kZ, 0.0f, FALSE, FALSE, FALSE, FALSE);
		PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
		ENTITY::SET_ENTITY_ALPHA(ped, 0, FALSE);
		PED::SET_PED_CONFIG_FLAG(ped, 6, TRUE);
		DECORATOR::DECOR_SET_INT(ped, "honor_override", overrideValue);
		TASK::TASK_COMBAT_PED(ped, me, 0, 0);
		FIRE::ADD_OWNED_EXPLOSION(me, kX, kY, kZ, 22, 1.0f, FALSE, TRUE, 0.0f);
		ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(kHonorPedModel);
		return "";
	}

	std::string CustomHonor()
	{
		const auto value = PromptInt("Enter Value -320 / 320", 4);
		if (!value)
			return "";
		return ChangeHonorByKill(*value);
	}

	std::string EditHonorViaScript()
	{
		const auto value = PromptInt("Enter Value -320 / 320", 4);
		if (!value)
			return "";
		if (!g_honorScript.Call(*value, FALSE, 9, kHonorScriptReason, "", 0, FALSE, FALSE))
			return "short_update call failed (see log)";
		return "";
	}

	// ---- SubRecoveryBounty ----

	// Bounties are in cents, like money.
	std::string Dollars(long long cents)
	{
		return std::format("${}.{:02}", cents / 100, std::abs(cents % 100));
	}

	int g_bountyAmount = 1000;

	// Ends the wanted state the crime report below starts, keeping the
	// bounty.
	void ClearWanted()
	{
		const Player me = PLAYER::PLAYER_ID();
		PLAYER::_SET_MAX_WANTED_LEVEL_2(-1);
		LAW::CLEAR_WANTED_SCORE(me);
		LAW::_SET_BOUNTY_HUNTER_PURSUIT_CLEARED();
		LAW::SET_WANTED_SCORE(me, 0);
	}

	// A debug crime report first so the bounty attaches to the current
	// region, then the new bounty, then the wanted state is cleared half a
	// second later.
	void ChangeBounty(int delta)
	{
		const Player me = PLAYER::PLAYER_ID();
		LAW::_REPORT_CRIME(me, GameUtil::Joaat("CRIME_WANTED_LEVEL_UP_DEBUG_LOW"), 0, 0, TRUE);
		LAW::SET_BOUNTY(me, LAW::GET_BOUNTY(me) + delta);
		LAW::SET_WANTED_SCORE(me, 1);
		WAIT(500);
		ClearWanted();
	}

	void ClearBounty()
	{
		const Player me = PLAYER::PLAYER_ID();
		LAW::_REPORT_CRIME(me, GameUtil::Joaat("CRIME_WANTED_LEVEL_UP_DEBUG_LOW"), 0, 0, TRUE);
		PLAYER::_SET_MAX_WANTED_LEVEL_2(-1);
		LAW::CLEAR_WANTED_SCORE(me);
		LAW::_SET_BOUNTY_HUNTER_PURSUIT_CLEARED();
		LAW::SET_BOUNTY(me, 0);
		LAW::SET_WANTED_SCORE(me, 0);
	}

	// Each state's bounty is Global_40.f_358[state].f_0 (12-slot entries;
	// short_update's per-state bounty setter, which also mirrors it to the
	// StateBounty* compendium stats). Index order is the game's.
	struct State { const char* name; int index; };
	const State kStates[] = {
		{ "Lemoyne", 2 }, { "West Elizabeth", 3 }, { "New Hanover", 1 }, { "Ambarino", 0 }, { "New Austin", 4 },
	};

	UINT64* StateBounty(int index)
	{
		return GameUtil::Global(40 + 358 + 1 + 12 * index);
	}

	std::string ClearStateBounty(int index)
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return "";
		if (UINT64* bounty = StateBounty(index))
			*reinterpret_cast<int*>(bounty) = 0;
		return "";
	}

	// ---- SubRecoveryCores ----

	// Attribute indices 0..2 are health, stamina, dead eye.
	// The captions are whole literals so the menu can translate them.
	struct Core
	{
		const char* name;      // command ids only
		const char* addPoints;
		const char* max;
		const char* rank;
		const char* addTank;
		Hash tonic;   // consumable whose effect fills this core
		Hash tank;    // upgrade item that adds a core tank
	};
	const Core kCores[] = {
		{ "Health", "Add Health Points", "Max Health", "Health Core", "Add Health Tank",
			GameUtil::Joaat("consumable_ginseng_elixier"), GameUtil::Joaat("UPGRADE_HEALTH_TANK_1") },
		{ "Stamina", "Add Stamina Points", "Max Stamina", "Stamina Core", "Add Stamina Tank",
			GameUtil::Joaat("consumable_aged_pirate_rum"), GameUtil::Joaat("UPGRADE_STAMINA_TANK_1") },
		{ "Dead Eye", "Add Dead Eye Points", "Max Dead Eye", "Dead Eye Core", "Add Dead Eye Tank",
			GameUtil::Joaat("consumable_valerian_root"), GameUtil::Joaat("UPGRADE_DEADEYE_TANK_1") },
	};
	int g_coreRank[3] = {};

	// Plays the tonic's quick-use animation and applies its effect, without
	// needing the item.
	void UseTonic(int core)
	{
		TASK::START_TASK_ITEM_INTERACTION(Me(), kCores[core].tonic, GameUtil::Joaat("use_tonic_potent_satchel_unarmed_quick"), 1, 0, 0.0f);
	}

	// Sets the core's attribute points (Global_40.f_11095.f_11[core], the
	// float array short_update keeps in sync with SET_ATTRIBUTE_POINTS) to
	// 1600, flags short_update to save (Global_1347477.f_8), then uses the
	// tonic so the game applies it.
	void MaxCore(int core)
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		if (UINT64* points = GameUtil::Global(40 + 11095 + 11 + 1 + core))
			*reinterpret_cast<float*>(points) = 1600.0f;
		if (UINT64* save = GameUtil::Global(1347477 + 8))
			*reinterpret_cast<int*>(save) = 1;
		UseTonic(core);
	}

	void ReadCoreRanks(MenuBase*)
	{
		for (int core = 0; core < 3; core++)
			g_coreRank[core] = ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(Me(), core);
	}

	std::string AddTank(int core)
	{
		if (!GameUtil::AddInventoryItemViaScript(kCores[core].tank, 1))
			return "flow_controller call failed (see log)";
		return "";
	}

	// ---- SubRecoveryAddItems ----

	// flow_controller func_299(item, quantity, bNoStat, hReason, b4): removes
	// an inventory item (or ammo), keeping the game's stats in step.
	// Position 0x8D81.
	ScriptFunction g_removeItemScript("flow_controller",
		"22 05 0E 00 00 66 00 2F 39 ? ? ? 05 8B 04 00 2F 50 05 01 66 03 37 A3 E0 88 21 0B 67 07 66 03 37 82 B4 C4 76 0B 67 08");

	constexpr std::uint64_t kRemoveWithItemId = 0xB4158C8C9A3B5DCE; // _INVENTORY_REMOVE_INVENTORY_ITEM_WITH_ITEMID
	constexpr std::uint64_t kRemoveWithGuid = 0x3E4E811480B3AE79;   // _INVENTORY_REMOVE_INVENTORY_ITEM_WITH_GUID
	constexpr std::uint64_t kAddWithGuid = 0xCB5D11F9508A928D;      // _INVENTORY_ADD_ITEM_WITH_GUID
	constexpr Hash kAddReasonPurchased = 0x4A6726C9;                // ADD_REASON_PURCHASED
	constexpr Hash kAddReasonLooted = 0xCA806A55;                   // ADD_REASON_LOOTED
	constexpr int kMaxItemAmount = 99;

	// Prompts for an item name (or hash) and an amount of 1..99.
	bool PromptItem(Hash& item, int& amount)
	{
		std::string name;
		if (!GameUtil::PromptText("Enter Item Name:", name) || name.empty())
			return false;
		const auto value = PromptInt("Enter Amount:", 2);
		if (!value || *value < 1 || *value > kMaxItemAmount)
			return false;
		item = GameUtil::ParseHash(name);
		amount = *value;
		return true;
	}

	std::string ItemInvalid() { return "~COLOR_RED~Error:~s~ Item is invalid."; }

	bool ItemValid(Hash item) { return ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(item, 0) != FALSE; }

	std::string AddToInventory()
	{
		Hash item;
		int amount;
		if (!PromptItem(item, amount))
			return "";
		std::string error;
		if (!GameUtil::AddInventoryItem(item, amount, error))
			return TrFormat("~COLOR_RED~Error:~s~ {}", Tr(error));
		return TrFormat("Added {}x {}", amount, GameUtil::ItemName(item, std::format("{:#x}", item)));
	}

	std::string AddItemViaScript()
	{
		Hash item;
		int amount;
		if (!PromptItem(item, amount))
			return "";
		if (!ItemValid(item))
			return ItemInvalid();
		if (!GameUtil::AddInventoryItemViaScript(item, amount))
			return "flow_controller call failed (see log)";
		return "";
	}

	std::string RemoveFromInventory()
	{
		Hash item;
		int amount;
		if (!PromptItem(item, amount))
			return "";
		if (!ItemValid(item))
			return ItemInvalid();
		if (!INVENTORY::_INVENTORY_REMOVE_INVENTORY_ITEM_WITH_ITEMID(GameUtil::kInventorySp, item, amount, GameUtil::kRemoveReasonDefault))
			return "Nothing removed";
		return TrFormat("Removed {}x {}", amount, GameUtil::ItemName(item, std::format("{:#x}", item)));
	}

	std::string RemoveItemViaScript()
	{
		Hash item;
		int amount;
		if (!PromptItem(item, amount))
			return "";
		if (!ItemValid(item))
			return ItemInvalid();
		if (!g_removeItemScript.Call(item, amount, FALSE, GameUtil::kRemoveReasonDefault, TRUE))
			return "flow_controller call failed (see log)";
		return "";
	}

	// Unlimited Items: game scripts' removals succeed without removing
	// anything. Rampage blocks only the by-item-id removal; we also block
	// the by-GUID one, which the scripts use about three times as often
	// (ours). Our own Remove/Wipe rows go through ScriptHook and still work.
	void BlockRemoveWithItemId(rage::scrNativeCallContext* ctx)
	{
		Log::Write("[Inventory] Blocked removal of {}x {:#x}", ctx->GetArg<int>(2), ctx->GetArg<Hash>(1));
		ctx->SetReturnValue<BOOL>(TRUE);
	}

	void BlockRemoveWithGuid(rage::scrNativeCallContext* ctx)
	{
		Log::Write("[Inventory] Blocked removal of {}x (by GUID)", ctx->GetArg<int>(2));
		ctx->SetReturnValue<BOOL>(TRUE);
	}

	NativeHooks::Id g_blockRemoveIds[2] = {};

	void SetUnlimitedItems(bool on)
	{
		for (NativeHooks::Id& id : g_blockRemoveIds)
		{
			NativeHooks::Remove(id);
			id = 0;
		}
		if (!on)
			return;
		g_blockRemoveIds[0] = NativeHooks::Add(NativeHooks::kAllScripts, kRemoveWithItemId, BlockRemoveWithItemId);
		g_blockRemoveIds[1] = NativeHooks::Add(NativeHooks::kAllScripts, kRemoveWithGuid, BlockRemoveWithGuid);
	}

	// Max Items: a purchase or a looted pickup adds 99 instead of the
	// amount asked for. _INVENTORY_ADD_ITEM_WITH_GUID's arguments are
	// (inventory, itemGuid, slotGuid, item, slot, quantity, reason).
	rage::scrNativeHandler g_addWithGuid = nullptr;

	void MaxOnPurchase(rage::scrNativeCallContext* ctx)
	{
		const Hash reason = ctx->GetArg<Hash>(6);
		if (reason == kAddReasonPurchased || reason == kAddReasonLooted)
		{
			Log::Write("[Inventory] Add {:#x} x{} => {}", ctx->GetArg<Hash>(3), ctx->GetArg<int>(5), kMaxItemAmount);
			ctx->SetArg(5, kMaxItemAmount);
		}
		g_addWithGuid(ctx);
	}

	NativeHooks::Id g_maxItemsId = 0;

	void SetMaxItems(bool on)
	{
		NativeHooks::Remove(g_maxItemsId);
		g_maxItemsId = 0;
		if (!on)
			return;
		g_addWithGuid = NativeHooks::Original(kAddWithGuid);
		if (g_addWithGuid)
			g_maxItemsId = NativeHooks::Add(NativeHooks::kAllScripts, kAddWithGuid, MaxOnPurchase);
	}

	// Collectible: spawns the object in front of the player and has them
	// pick it up, which collects it the way finding it in the world does.
	std::string SpawnCollectible()
	{
		std::string name;
		if (!GameUtil::PromptText("Enter Object Name or Hash:", name) || name.empty())
			return "";
		const Hash model = GameUtil::ParseHash(name);
		if (!GameUtil::LoadModel(model))
			return "~COLOR_RED~Error:~s~ Model is invalid.";
		const Vector3 pos = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(Me(), 0.0f, 1.0f, 0.0f);
		const Object object = OBJECT::CREATE_OBJECT(model, pos.x, pos.y, pos.z, FALSE, FALSE, FALSE, FALSE, FALSE);
		TASK::_MAKE_OBJECT_CARRIABLE(object);
		GRAPHICS::SET_PICKUP_LIGHT(object, TRUE);
		ENTITY::FREEZE_ENTITY_POSITION(object, FALSE);
		WAIT(600);
		TASK::TASK_PICKUP_CARRIABLE_ENTITY(Me(), object);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return "";
	}

	std::string WipeInventory()
	{
		std::string text;
		if (!GameUtil::PromptText("To continue write \"Do as I say\"", text))
			return "";
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (text != "do as i say")
			return "Cancelled";
		INVENTORY::_INVENTORY_USE_BACKUP_INVENTORY(FALSE);
		INVENTORY::_INVENTORY_REMOVE_INVENTORY_ITEMS(GameUtil::kInventorySp, GameUtil::kRemoveReasonDefault);
		return "Inventory wiped";
	}

	// Copies inventory `from` over `to`, by the root character GUID.
	void CopyInventory(int from, int to)
	{
		GameUtil::ItemGuid character = GameUtil::CharacterGuid(from);
		INVENTORY::_INVENTORY_REMOVE_INVENTORY_ITEMS(to, GameUtil::kRemoveReasonDefault);
		INVENTORY::_INVENTORY_COPY_ITEM_TO_INVENTORY(from, to, character.Ptr(), 0);
	}

	// Rampage keeps its snapshot in inventory 6, which singleplayer
	// doesn't otherwise use.
	constexpr int kInventorySnapshot = 6;

	// ---- SubRecoveryGiveItemsList ----

	// Ours: the game's whole SP item catalog (catalog_sp.ymt, generated by
	// tools/extract_catalog.py), grouped by the catalog's own item types;
	// Rampage's is a hand-picked table in its binary. Rows show the game's
	// name for the item (its hash is its text label); the internal name is
	// the fallback and a search key.
	struct CatalogItem { Hash key; const char* type; Hash category; const char* name; };
	const CatalogItem kItemCatalog[] = {
#include "..\data\ItemCatalog.inc"
	};

	// A big item type's split by the catalog's ci_category_* (names in
	// tools/data/ci_categories.txt); items in none of its groups go under
	// Other.
	struct ItemGroup { const char* category; const char* caption; };
	const ItemGroup kConsumableGroups[] = {
		{ "ci_category_provision", "Food" },
		{ "ci_category_consumable", "Tonics and Medicine" },
		{ "ci_category_ingredient", "Ingredients" },
		{ "ci_category_herbs", "Herbs" },
		{ "ci_category_kit", "Kit" },
	};
	const ItemGroup kProvisionGroups[] = {
		{ "ci_category_materials", "Materials" },
		{ "ci_category_valuable", "Valuables" },
		{ "ci_category_kit", "Kit" },
		{ "ci_category_ingredient", "Ingredients" },
		{ "ci_category_watch", "Watches" },
	};
	const ItemGroup kHorseEquipmentGroups[] = {
		{ "ci_category_horse_saddle", "Saddles" },
		{ "ci_category_horse_blanket", "Blankets" },
		{ "ci_category_horse_horn", "Horns" },
		{ "ci_category_horse_saddlebag", "Saddlebags" },
		{ "ci_category_horse_stirrup", "Stirrups" },
		{ "ci_category_horse_bedroll", "Bedrolls" },
		{ "ci_category_horse_mane", "Manes" },
		{ "ci_category_horse_tail", "Tails" },
	};

	// The types Give Items lists, in menu order. Clothing, weapons and
	// horses are left to the Wardrobe, Weapon and Spawner menus.
	struct ItemType { const char* type; const char* caption; std::span<const ItemGroup> groups = {}; };
	const ItemType kItemTypes[] = {
		{ "consumable", "Consumables", kConsumableGroups },
		{ "provision", "Provisions", kProvisionGroups },
		{ "document", "Documents" },
		{ "ammo", "Ammo" },
		{ "kit", "Kits" },
		{ "upgrade", "Upgrades" },
		{ "core_item", "Core Items" },
		{ "horse_equipment", "Horse Equipment", kHorseEquipmentGroups },
		{ "weapon_mod", "Weapon Mods" },
		{ "weapon_decoration", "Weapon Decorations" },
		{ "money", "Money Items" },
		{ "advert", "Adverts" },
		{ "other", "Other" },
	};

	bool Giveable(const CatalogItem& item)
	{
		for (const ItemType& type : kItemTypes)
			if (std::string_view(item.type) == type.type)
				return true;
		return false;
	}

	std::string CatalogFallback(const CatalogItem& item)
	{
		return item.name ? std::string(item.name) : std::format("{:#010x}", item.key);
	}

	int g_giveAmount = 1;
	// Rampage picks the game-script path while Shift is held; a visible
	// choice instead (ours).
	int g_giveMethod = 0;
	const std::vector<std::string> kGiveMethods = { "Inventory", "Game Script" };

	std::string GiveItem(const CatalogItem& entry)
	{
		const Hash item = entry.key;
		if (g_giveMethod == 1)
		{
			if (!GameUtil::AddInventoryItemViaScript(item, g_giveAmount))
				return "flow_controller call failed (see log)";
			return "";
		}
		std::string error;
		if (!GameUtil::AddInventoryItem(item, g_giveAmount, error))
			return TrFormat("~COLOR_RED~Error:~s~ {}", Tr(error));
		return TrFormat("Added {}x {}", g_giveAmount, GameUtil::ItemName(item, CatalogFallback(entry)));
	}

	// One row per item the item database knows, sorted by the name shown;
	// items the game has no text for go last, under their internal name.
	void AddItemRows(MenuBase* list, const std::vector<const CatalogItem*>& items)
	{
		struct Row { std::string caption; bool named; const CatalogItem* item; };
		std::vector<Row> rows;
		rows.reserve(items.size());
		for (const CatalogItem* item : items)
		{
			if (!ItemValid(item->key))
				continue;
			std::string shown = GameUtil::ItemName(item->key, "");
			const bool named = !shown.empty();
			rows.push_back({ named ? std::move(shown) : CatalogFallback(*item), named, item });
		}
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
			if (a.named != b.named)
				return a.named;
			return a.caption < b.caption;
		});
		for (const Row& row : rows)
		{
			const CatalogItem* item = row.item;
			Ui::Action(list, row.caption, [item] { return GiveItem(*item); });
		}
		if (rows.empty())
			Ui::Section(list, "No matches");
	}

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// Search: part of a name, matched against the game's name and the
	// internal one, over every type Give Items lists.
	void SearchItems(MenuBase* results)
	{
		std::string text;
		if (!GameUtil::PromptText("Search:", text) || text.empty())
			return;
		text = Lower(text);
		std::vector<const CatalogItem*> matches;
		for (const CatalogItem& item : kItemCatalog)
		{
			if (!Giveable(item))
				continue;
			if ((item.name && Lower(item.name).find(text) != std::string::npos)
				|| Lower(GameUtil::ItemName(item.key, "")).find(text) != std::string::npos)
				matches.push_back(&item);
		}
		AddItemRows(results, matches);
	}

	bool ItemsOfType(const CatalogItem&) { return true; }

	// The rows for a type's items that pass `filter`.
	void AddTypeRows(MenuBase* list, std::string_view type, const std::function<bool(const CatalogItem&)>& filter)
	{
		std::vector<const CatalogItem*> items;
		for (const CatalogItem& item : kItemCatalog)
			if (type == item.type && filter(item))
				items.push_back(&item);
		AddItemRows(list, items);
	}

	void BuildGiveItems(MenuBase* items)
	{
		MenuBase* give = Ui::Submenu(items, "Give Items");
		Ui::Number(give, "recovery.giveitems.amount", "Amount", &g_giveAmount, 1, kMaxItemAmount, 1);
		Ui::Choice(give, "recovery.method", "Method", kGiveMethods, &g_giveMethod);
		Ui::ListMenu(give, "Search", SearchItems);
		Ui::Section(give, "Item Types");
		for (const ItemType& type : kItemTypes)
		{
			const std::string_view key = type.type;
			if (type.groups.empty())
			{
				Ui::ListMenu(give, type.caption, [key](MenuBase* list) { AddTypeRows(list, key, ItemsOfType); });
				continue;
			}
			// Ours: All, then one list per group, then Other.
			MenuBase* sub = Ui::Submenu(give, type.caption);
			Ui::ListMenu(sub, "All", [key](MenuBase* list) { AddTypeRows(list, key, ItemsOfType); });
			const std::span<const ItemGroup> groups = type.groups;
			for (const ItemGroup& group : groups)
			{
				const Hash category = GameUtil::Joaat(group.category);
				Ui::ListMenu(sub, group.caption, [key, category](MenuBase* list) {
					AddTypeRows(list, key, [category](const CatalogItem& item) { return item.category == category; });
				});
			}
			Ui::ListMenu(sub, "Other", [key, groups](MenuBase* list) {
				AddTypeRows(list, key, [groups](const CatalogItem& item) {
					return std::none_of(groups.begin(), groups.end(),
						[&item](const ItemGroup& g) { return item.category == GameUtil::Joaat(g.category); });
				});
			});
		}
	}

	std::string RestoreSnapshot()
	{
		// On the backup while main is cleared and refilled, as Rampage does.
		INVENTORY::_INVENTORY_USE_BACKUP_INVENTORY(TRUE);
		CopyInventory(kInventorySnapshot, GameUtil::kInventorySp);
		INVENTORY::_INVENTORY_USE_BACKUP_INVENTORY(FALSE);
		return "Snapshot restored";
	}
}

namespace Menus
{
	void BuildRecovery(MenuBase* root)
	{
		MenuBase* recovery = Ui::Submenu(root, "Recovery");

		MenuBase* money = Ui::Submenu(recovery, "Money");
		Ui::Action(money, "recovery.addmoney", "Add Money", AddMoney);
		Ui::Action(money, "recovery.removemoney", "Remove Money", RemoveMoney);
		Ui::Action(money, "recovery.addmoneyviagamescript", "Add Money via Game Script", AddMoneyViaScript);
		Ui::Section(money, "Drop");
		Ui::Choice(money, "recovery.model", "Model", kDropModelNames, &g_dropModel);
		Ui::Number(money, "recovery.amountcents", "Amount (cents)", &g_dropAmount, 0, 50000, 1000);
		Ui::Looped(money, "recovery.dropmoney", "Drop Money", DropMoneyTick);
		money->AddItem(new MenuItemActionStatus(
			[] { return TrFormat("Total Money Dropped: ${}.{:02}", g_totalDropped / 100, g_totalDropped % 100); },
			[] { g_totalDropped = 0; return std::string("Counter reset"); }));

		MenuBase* honor = Ui::Submenu(recovery, "Honor");
		// The label is drawn every frame the Honor menu is open, so it also
		// shows the game's honor meter then, as Rampage does.
		honor->AddItem(new MenuItemLabel([] {
			HUD::_ENABLE_HUD_CONTEXT_THIS_FRAME(GameUtil::Joaat("HUD_CTX_HONOR_SHOW"));
			return TrFormat("Current Honor: {}", CurrentHonor());
		}));
		Ui::Number(honor, "recovery.honor.amount", "Amount", &g_honorAmount, 0, 320, 1);
		Ui::Action(honor, "recovery.addpositive", "Add Positive", [] { return ChangeHonorByKill(-g_honorAmount); });
		Ui::Action(honor, "recovery.addnegative", "Add Negative", [] { return ChangeHonorByKill(g_honorAmount); });
		Ui::Action(honor, "recovery.customhonor", "Custom Honor", CustomHonor);
		Ui::Action(honor, "recovery.editviagamescript", "Edit via Game Script", EditHonorViaScript);

		MenuBase* bounty = Ui::Submenu(recovery, "Bounty");
		bounty->AddItem(new MenuItemLabel([] { return TrFormat("Current Bounty: {}", Dollars(LAW::GET_BOUNTY(PLAYER::PLAYER_ID()))); }));
		Ui::Number(bounty, "recovery.bountyvaluecents", "Bounty Value (cents)", &g_bountyAmount, 0, 10000, 100);
		Ui::Do(bounty, "recovery.increasebounty", "Increase Bounty", [] { ChangeBounty(g_bountyAmount); });
		Ui::Do(bounty, "recovery.decreasebounty", "Decrease Bounty", [] { ChangeBounty(-g_bountyAmount); });
		Ui::Do(bounty, "recovery.clearbounty", "Clear Bounty", ClearBounty);
		Ui::Section(bounty, "States");
		// Ours: the caption shows the state's bounty; selecting clears it.
		for (const State& state : kStates)
		{
			bounty->AddItem(new MenuItemActionStatus(
				[state] {
					const UINT64* value = StateBounty(state.index);
					return std::format("{}: {}", Tr(state.name), value ? Dollars(*reinterpret_cast<const int*>(value)) : "?");
				},
				[state] { return ClearStateBounty(state.index); }));
		}

		MenuBase* cores = Ui::Submenu(recovery, "Cores");
		cores->SetOnOpen(ReadCoreRanks);
		for (int core = 0; core < 3; core++)
			Ui::Do(cores, Ui::Id("recovery.addpoints", kCores[core].name), kCores[core].addPoints, [core] { UseTonic(core); });
		Ui::Section(cores, "Permanently");
		for (int core = 0; core < 3; core++)
			Ui::Do(cores, Ui::Id("recovery.maxcore", kCores[core].name), kCores[core].max, [core] { MaxCore(core); });
		Ui::Section(cores, "Custom Temporary");
		// Ours: applied on every step instead of on select.
		for (int core = 0; core < 3; core++)
			Ui::Number(cores, Ui::Id("recovery.corerank", kCores[core].name), kCores[core].rank, &g_coreRank[core], 0, 8, 1,
				[core] { ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(Me(), core, g_coreRank[core]); });
		Ui::Section(cores, "Tanks");
		for (int core = 0; core < 3; core++)
			Ui::Action(cores, Ui::Id("recovery.addtank", kCores[core].name), kCores[core].addTank, [core] { return AddTank(core); });
		Ui::Transient(cores); // the ranks are read from the game on open

		MenuBase* items = Ui::Submenu(recovery, "Add Items");
		Ui::Toggle(items, "recovery.unlimiteditems", "Unlimited Items", SetUnlimitedItems);
		Ui::Toggle(items, "recovery.maxitems", "Max Items", SetMaxItems);
		Ui::Action(items, "recovery.addtoinventory", "Add to Inventory", AddToInventory);
		Ui::Action(items, "recovery.additemviagamescript", "Add Item via Game Script", AddItemViaScript);
		Ui::Action(items, "recovery.removefrominventory", "Remove from Inventory", RemoveFromInventory);
		Ui::Action(items, "recovery.removeitemviagamescript", "Remove Item via Game Script", RemoveItemViaScript);
		Ui::Action(items, "recovery.collectible", "Collectible", SpawnCollectible)->SetHotkeyable(false);
		Ui::Action(items, "recovery.wipeinventory", "~COLOR_RED~Wipe Inventory", WipeInventory)->SetHotkeyable(false);
		// Shows the game's state, read each time the menu opens.
		Rampagio::BoolCommand* backup = Ui::Toggle(items, "recovery.usebackupinventory", "~COLOR_RED~Use Backup Inventory",
			[](bool on) { INVENTORY::_INVENTORY_USE_BACKUP_INVENTORY(on); });
		backup->SetTransient(); // the game's own state
		items->SetOnOpen([backup](MenuBase*) { backup->Sync(INVENTORY::_INVENTORY_IS_USING_BACKUP_INVENTORY() != FALSE); });
		Ui::Action(items, "recovery.copymaintobackup", "~COLOR_RED~Copy Main to Backup", [] {
			CopyInventory(GameUtil::kInventorySp, GameUtil::kInventorySpBackup);
			return std::string("Copied main to backup");
		});
		Ui::Action(items, "recovery.copybackuptomain", "~COLOR_RED~Copy Backup to Main", [] {
			CopyInventory(GameUtil::kInventorySpBackup, GameUtil::kInventorySp);
			return std::string("Copied backup to main");
		});
		Ui::Action(items, "recovery.snapshotinventory", "Snapshot Inventory", [] {
			CopyInventory(GameUtil::kInventorySp, kInventorySnapshot);
			return std::string("Snapshot saved");
		});
		Ui::Action(items, "recovery.restoresnapshot", "Restore Snapshot", RestoreSnapshot);
		BuildGiveItems(items);

		BuildRecoveryUnlocks(recovery);
		BuildRecoveryCollectibles(recovery);
		BuildRecoveryChallenges(recovery);
	}
}
