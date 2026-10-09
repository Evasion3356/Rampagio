/*
	Player > Wardrobe > Model Changer: ports Rampage's
	Submenus::SubSelfModelChanger, SubModelChangerPeds/PedsList,
	SubModelChangerHorses/HorsesList and SubModelChangerAnimal. The model
	lists come from the game scripts (tools/extract_models.py), filtered at
	runtime to models the game has.

	Changing model sets Global_1835009, which stops medium_update from
	checking (and restoring) the player's model; Reset clears it and the
	game puts Arthur or John back.

	Force Player Type makes the game treat the player as Arthur, sick
	Arthur or John without changing model, by rewriting the story state
	medium_update reads (see ForcePlayerTypeTick). Rampage writes it every
	frame and leaves it on toggle-off; ours puts the values it found back.
*/

#include "Menus.h"
#include "..\GameUtil.h"

#include <format>
#include <map>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Player MyPlayer() { return PLAYER::PLAYER_ID(); }

	constexpr Hash kArthur = 0x0D7114C9; // player_zero
	constexpr Hash kJohn = 0x00B69710;   // player_three

	struct PedModel
	{
		const char* group;
		const char* name;
	};
	const PedModel kPedModels[] = {
#include "..\data\PedModels.inc"
	};
	const char* const kHorseModels[] = {
#include "..\data\HorseModels.inc"
	};
	const char* const kAnimalModels[] = {
#include "..\data\AnimalModels.inc"
	};

	bool g_changed = false;
	bool g_copyModelOnly = false;
	int g_savedRanks[3] = {};

	Rampagio::LoopedCommand* g_birdControls = nullptr;
	Rampagio::LoopedCommand* g_attackControls = nullptr;
	Rampagio::LoopedCommand* g_reloadControls = nullptr;

	void SetControlToggles(bool bird, bool attack, bool reload)
	{
		g_birdControls->Sync(bird);
		g_attackControls->Sync(attack);
		g_reloadControls->Sync(reload);
	}

	void RefillCores()
	{
		const Ped ped = Me();
		for (int core = 0; core < 3; core++)
			ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped, core, 100);
		PLAYER::RESTORE_PLAYER_STAMINA(MyPlayer(), 100.0f);
		PLAYER::_SPECIAL_ABILITY_START_RESTORE(MyPlayer(), -1, TRUE);
		ENTITY::SET_ENTITY_HEALTH(ped, ENTITY::GET_ENTITY_MAX_HEALTH(ped, FALSE), 0);
	}

	void SetModelCheckSuspended(bool suspended)
	{
		if (UINT64* g = GameUtil::Global(1835009))
			*g = suspended ? 1 : 0;
	}

	void ResetModel()
	{
		if (!g_changed)
			return;
		SetModelCheckSuspended(false);
		WAIT(2000);
		const Ped ped = Me();
		for (int i = 0; i < 3; i++)
			ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(ped, i, g_savedRanks[i]);
		RefillCores();
		SetControlToggles(false, false, false);
		g_changed = false;
	}

	std::string ChangeModel(Hash model)
	{
		if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_A_PED(model))
			return "Not a ped model";
		if (!GameUtil::LoadModel(model))
			return "Model didn't load";
		Ped ped = Me();
		if (g_copyModelOnly)
		{
			// Clone a fresh ped's look onto the player, keeping the model.
			Ped donor = PED::CREATE_PED(model, 0.0f, 0.0f, 0.0f, 0.0f, FALSE, FALSE, FALSE, FALSE);
			PED::_SET_RANDOM_OUTFIT_VARIATION(donor, TRUE);
			PED::CLONE_PED_TO_TARGET(donor, ped);
			PED::DELETE_PED(&donor);
		}
		else
		{
			if (!g_changed)
				for (int i = 0; i < 3; i++)
					g_savedRanks[i] = ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(ped, i);
			SetModelCheckSuspended(true);
			PED::SET_PED_CONFIG_FLAG(ped, 265, FALSE);
			PLAYER::SET_PLAYER_MODEL(MyPlayer(), model, TRUE);
			ped = PLAYER::GET_PLAYER_PED(MyPlayer());
			PED::_SET_RANDOM_OUTFIT_VARIATION(ped, TRUE);
			// Animals get controls that let them do something.
			if (!PED::IS_PED_HUMAN(ped) || model == kArthur || model == kJohn)
			{
				const bool bird = ENTITY::_GET_IS_BIRD(ped);
				SetControlToggles(bird, !bird, false);
			}
			else
				SetControlToggles(false, false, true);
			g_changed = true;
		}
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return {};
	}

	void BirdControlsTick()
	{
		PAD::_SET_CONTROL_CONTEXT(5, 0x7B4D1CFB);
		PAD::_SET_CONTROL_CONTEXT(5, 0xF5A638B9);
	}

	void AttackControlsTick()
	{
		PAD::_SET_CONTROL_CONTEXT(5, 0x374A4FC8);
		Entity target = 0;
		PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(MyPlayer(), &target, FALSE, FALSE);
		if (!PAD::IS_DISABLED_CONTROL_PRESSED(0, 0x07CE1E61) && !PAD::IS_DISABLED_CONTROL_PRESSED(0, 0xB2F377E8))
			return;
		if (!target || !ENTITY::DOES_ENTITY_EXIST(target))
			return;
		const Ped ped = Me();
		for (int attribute : { 5, 58, 93 })
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, attribute, TRUE);
		TASK::TASK_COMBAT_PED(ped, target, 0, 16);
	}

	void ReloadControlsTick()
	{
		if (!PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, 0xE30CD707))
			return;
		const Ped ped = Me();
		TASK::TASK_RELOAD_WEAPON(ped, TRUE);
		WEAPON::_ENABLE_WEAPON_RESTORE(ped);
	}

	// Rampage's Auto-Reset: back to the story character after dying or
	// being arrested in another model.
	void AutoResetTick()
	{
		if (!g_changed)
			return;
		const Ped ped = Me();
		if (!ENTITY::DOES_ENTITY_EXIST(ped))
			return;
		const Hash model = ENTITY::GET_ENTITY_MODEL(ped);
		if (model == kArthur || model == kJohn)
			return;
		if (ENTITY::IS_ENTITY_DEAD(ped) || PLAYER::IS_PLAYER_BEING_ARRESTED(MyPlayer(), FALSE))
		{
			WAIT(1000);
			ResetModel();
		}
	}

	// Force Player Type. medium_update keeps the story character in
	// Global_1946054.f_1 (func_889: Arthur, sick Arthur or John), the
	// player model in Global_1935630.f_2, and per-character state in
	// Global_12106[Global_1347702.f_2858 /*7*/].f_1 (sick Arthur is 5, as
	// Rampage writes); _SET_PED_ACTIVE_PLAYER_TYPE takes joaat("Arthur") or
	// joaat("John") (func_887).
	struct PlayerType
	{
		int character;
		Hash model;
		int state; // -1: John's entry is left alone, as Rampage does
		Hash type;
	};
	const PlayerType kPlayerTypes[] = {
		{ -2125499975, kArthur, 0, 0xE1B02E5B },
		{ -449205311, kArthur, 5, 0xE1B02E5B },
		{ 1160113249, kJohn, -1, 0xD7AE28D0 },
	};
	int g_playerType = 0;

	struct PlayerTypeSlots
	{
		UINT64* character;
		UINT64* model;
		UINT64* state;
	};

	bool FindPlayerTypeSlots(PlayerTypeSlots& out)
	{
		UINT64* character = GameUtil::Global(1946054 + 1);
		UINT64* model = GameUtil::Global(1935630 + 2);
		UINT64* index = GameUtil::Global(1347702 + 2858);
		UINT64* size = GameUtil::Global(12106); // the array's size slot
		if (!character || !model || !index || !size)
			return false;
		const int i = static_cast<int>(*index);
		if (i < 0 || i >= static_cast<int>(*size))
			return false;
		out = { character, model, GameUtil::Global(12106 + 1 + i * 7 + 1) };
		return out.state != nullptr;
	}

	// What the globals held before the first forced frame, put back on off.
	bool g_savedPlayerType = false;
	UINT64 g_savedCharacter = 0, g_savedModel = 0, g_savedState = 0;

	void ForcePlayerTypeTick()
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		PlayerTypeSlots slots;
		if (!FindPlayerTypeSlots(slots))
			return;
		if (!g_savedPlayerType)
		{
			g_savedCharacter = *slots.character;
			g_savedModel = *slots.model;
			g_savedState = *slots.state;
			g_savedPlayerType = true;
		}
		const PlayerType& t = kPlayerTypes[g_playerType];
		*reinterpret_cast<int*>(slots.character) = t.character;
		*reinterpret_cast<int*>(slots.model) = static_cast<int>(t.model);
		if (t.state >= 0)
			*reinterpret_cast<int*>(slots.state) = t.state;
		const Ped ped = Me();
		PED::_SET_PED_ACTIVE_PLAYER_TYPE(ped, t.type);
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
	}

	void ForcePlayerTypeOff()
	{
		if (!g_savedPlayerType)
			return;
		g_savedPlayerType = false;
		PlayerTypeSlots slots;
		if (!FindPlayerTypeSlots(slots))
			return;
		*slots.character = g_savedCharacter;
		*slots.model = g_savedModel;
		*slots.state = g_savedState;
		const Ped ped = Me();
		const Hash model = ENTITY::GET_ENTITY_MODEL(ped);
		if (model == kArthur || model == kJohn)
			PED::_SET_PED_ACTIVE_PLAYER_TYPE(ped, model == kJohn ? 0xD7AE28D0 : 0xE1B02E5B);
		PED::_UPDATE_PED_VARIATION(ped, FALSE, TRUE, TRUE, TRUE, FALSE);
	}

	bool Available(const char* name)
	{
		const Hash h = GameUtil::Joaat(name);
		return STREAMING::IS_MODEL_IN_CDIMAGE(h) && STREAMING::IS_MODEL_A_PED(h);
	}

	void AddModelRow(MenuBase* m, const char* name)
	{
		const std::string n = name;
		Ui::Action(m, n, [n] { return ChangeModel(GameUtil::Joaat(n)); });
	}
}

namespace Menus
{
	void BuildModelChanger(MenuBase* wardrobe)
	{
		MenuBase* changer = Ui::Submenu(wardrobe, "Model Changer");
		Ui::Choice(changer, "modelchanger.playertype", "Player Type", { "Arthur", "Arthur Sick", "John" }, &g_playerType);
		Ui::Looped(changer, "modelchanger.forceplayertype", "Force Player Type", ForcePlayerTypeTick, ForcePlayerTypeOff);
		Ui::Do(changer, "modelchanger.reset", "Reset", ResetModel);
		Ui::Looped(changer, "modelchanger.autoreset", "Auto-Reset", AutoResetTick);
		Ui::Toggle(changer, "modelchanger.copymodelonly", "Copy Model Only", [](bool on) { g_copyModelOnly = on; });
		g_birdControls = Ui::Looped(changer, "modelchanger.enablebirdcontrols", "Enable Bird Controls", BirdControlsTick);
		g_attackControls = Ui::Looped(changer, "modelchanger.enableattackcontrols", "Enable Attack Controls", AttackControlsTick);
		g_reloadControls = Ui::Looped(changer, "modelchanger.enablereloadcontrols", "Enable Reload Controls", ReloadControlsTick);
		// Set from the model on every change, so not saved.
		for (Rampagio::Command* command : { g_birdControls, g_attackControls, g_reloadControls })
			command->SetTransient();

		Ui::Section(changer, "Models");
		MenuBase* humans = Ui::Submenu(changer, "Humans");
		std::map<std::string, std::vector<const char*>> groups;
		for (const PedModel& p : kPedModels)
			groups[p.group].push_back(p.name);
		for (auto& [group, names] : groups)
			Ui::ListMenu(humans, group, [names](MenuBase* m) {
				for (const char* name : names)
					if (Available(name))
						AddModelRow(m, name);
			});
		Ui::ListMenu(changer, "Horses", [](MenuBase* m) {
			for (const char* name : kHorseModels)
				if (Available(name))
					AddModelRow(m, name);
		});
		Ui::ListMenu(changer, "Animals", [](MenuBase* m) {
			for (const char* name : kAnimalModels)
				if (Available(name))
					AddModelRow(m, name);
		});
		Ui::ListMenu(changer, "Search Peds", [](MenuBase* m) {
			std::string text;
			if (!GameUtil::PromptText("Search:", text) || text.empty())
				return;
			for (auto& c : text)
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			auto match = [&](const char* name) { return std::string_view(name).find(text) != std::string_view::npos && Available(name); };
			for (const PedModel& p : kPedModels)
				if (match(p.name))
					AddModelRow(m, p.name);
			for (const char* name : kHorseModels)
				if (match(name))
					AddModelRow(m, name);
			for (const char* name : kAnimalModels)
				if (match(name))
					AddModelRow(m, name);
		});
		Ui::Action(changer, "modelchanger.custominput", "Custom Input", [] {
			std::string text;
			if (!GameUtil::PromptText("Enter Model Name or Hash:", text) || text.empty())
				return std::string();
			return ChangeModel(GameUtil::ParseHash(text));
		})->SetHotkeyable(false);
	}
}
