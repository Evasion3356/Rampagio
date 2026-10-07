/*
	Recovery menu: ports Rampage's Submenus::SubRecoveryMoney,
	SubRecoveryHonor, SubRecoveryBounty and SubRecoveryCores. The "via Game Script" rows
	call the same script functions Rampage does (flow_controller func_688,
	short_update func_583 in the 1491.50 decompile) through our
	ScriptFunction.

	Money amounts are typed in dollars and passed to the game in cents, as
	Rampage does.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\ScriptFunction.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>

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
		return std::format("Added ${}", *dollars);
	}

	std::string RemoveMoney()
	{
		const auto dollars = PromptInt("Enter $ to Remove:", 9);
		if (!dollars)
			return "";
		MONEY::_MONEY_DECREMENT_CASH_BALANCE((std::max)(0, *dollars * 100));
		return std::format("Removed ${}", *dollars);
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
		struct { int hash; int pad; } statId = { static_cast<int>(GameUtil::Joaat("honor_current")), 0 };
		int value = 0;
		STATS::STAT_ID_GET_INT(reinterpret_cast<Any*>(&statId), &value);
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
	struct Core
	{
		const char* name;
		Hash tonic;   // consumable whose effect fills this core
		Hash tank;    // upgrade item that adds a core tank
	};
	const Core kCores[] = {
		{ "Health", GameUtil::Joaat("consumable_ginseng_elixier"), GameUtil::Joaat("UPGRADE_HEALTH_TANK_1") },
		{ "Stamina", GameUtil::Joaat("consumable_aged_pirate_rum"), GameUtil::Joaat("UPGRADE_STAMINA_TANK_1") },
		{ "Dead Eye", GameUtil::Joaat("consumable_valerian_root"), GameUtil::Joaat("UPGRADE_DEADEYE_TANK_1") },
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

	// flow_controller func_290(item, quantity, b2, b3, b4, hReason, i6, i7,
	// eEntity, b9): adds an inventory item. Position 0x766E.
	ScriptFunction g_addItemScript("flow_controller",
		"22 0A 32 00 00 66 00 2F 39 ? ? ? 05 8B 04 00 2F 50 0A 01 66 00 66 01 66 02 66 05 39 ? ? ? 05 8B 04 00 2F 50 0A 01");

	std::string AddTank(int core)
	{
		if (!g_addItemScript.Call(kCores[core].tank, 1, FALSE, FALSE, FALSE, kCashAddReason, 0, 0, 0, FALSE))
			return "flow_controller call failed (see log)";
		return "";
	}
}

namespace Menus
{
	void BuildRecovery(MenuBase* root)
	{
		MenuBase* recovery = Ui::Submenu(root, "Recovery");

		MenuBase* money = Ui::Submenu(recovery, "Money");
		Ui::Action(money, "Add Money", AddMoney);
		Ui::Action(money, "Remove Money", RemoveMoney);
		Ui::Action(money, "Add Money via Game Script", AddMoneyViaScript);
		Ui::Section(money, "Drop");
		Ui::Choice(money, "Model", kDropModelNames, &g_dropModel);
		Ui::Number(money, "Amount (cents)", &g_dropAmount, 0, 50000, 1000);
		Ui::Looped(money, "Drop Money", DropMoneyTick);
		money->AddItem(new MenuItemActionStatus(
			[] { return std::format("Total Money Dropped: ${}.{:02}", g_totalDropped / 100, g_totalDropped % 100); },
			[] { g_totalDropped = 0; return std::string("Counter reset"); }));

		MenuBase* honor = Ui::Submenu(recovery, "Honor");
		honor->AddItem(new MenuItemLabel([] { return std::format("Current Honor: {}", CurrentHonor()); }));
		Ui::Number(honor, "Amount", &g_honorAmount, 0, 320, 1);
		Ui::Action(honor, "Add Positive", [] { return ChangeHonorByKill(-g_honorAmount); });
		Ui::Action(honor, "Add Negative", [] { return ChangeHonorByKill(g_honorAmount); });
		Ui::Action(honor, "Custom Honor", CustomHonor);
		Ui::Action(honor, "Edit via Game Script", EditHonorViaScript);

		MenuBase* bounty = Ui::Submenu(recovery, "Bounty");
		bounty->AddItem(new MenuItemLabel([] { return "Current Bounty: " + Dollars(LAW::GET_BOUNTY(PLAYER::PLAYER_ID())); }));
		Ui::Number(bounty, "Bounty Value (cents)", &g_bountyAmount, 0, 10000, 100);
		Ui::Do(bounty, "Increase Bounty", [] { ChangeBounty(g_bountyAmount); });
		Ui::Do(bounty, "Decrease Bounty", [] { ChangeBounty(-g_bountyAmount); });
		Ui::Do(bounty, "Clear Bounty", ClearBounty);
		Ui::Section(bounty, "States");
		// Ours: the caption shows the state's bounty; selecting clears it.
		for (const State& state : kStates)
		{
			bounty->AddItem(new MenuItemActionStatus(
				[state] {
					const UINT64* value = StateBounty(state.index);
					return std::format("{}: {}", state.name, value ? Dollars(*reinterpret_cast<const int*>(value)) : "?");
				},
				[state] { return ClearStateBounty(state.index); }));
		}

		MenuBase* cores = Ui::Submenu(recovery, "Cores");
		cores->SetOnOpen(ReadCoreRanks);
		for (int core = 0; core < 3; core++)
			Ui::Do(cores, std::format("Add {} Points", kCores[core].name), [core] { UseTonic(core); });
		Ui::Section(cores, "Permanently");
		for (int core = 0; core < 3; core++)
			Ui::Do(cores, std::format("Max {}", kCores[core].name), [core] { MaxCore(core); });
		Ui::Section(cores, "Custom Temporary");
		// Ours: applied on every step instead of on select.
		for (int core = 0; core < 3; core++)
			Ui::Number(cores, std::format("{} Core", kCores[core].name), &g_coreRank[core], 0, 8, 1,
				[core] { ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(Me(), core, g_coreRank[core]); });
		Ui::Section(cores, "Tanks");
		for (int core = 0; core < 3; core++)
			Ui::Action(cores, std::format("Add {} Tank", kCores[core].name), [core] { return AddTank(core); });
	}
}
