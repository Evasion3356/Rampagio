/*
	Ped Editor: ports Rampage's Submenus::SubPedEditor and its submenus
	SubPedEditorGeneral, SubPedEditorCombat / CombatAttributes,
	SubPedEditorWeapons / WeaponsGive, SubPedEditorWardrobe,
	SubPedPositioning and SubPedSelectAttachment. Its Effects, Emotes,
	Scenarios, Animations, Play Speech (and their lists), Walk Styles,
	Damage Packs, Meta Ped Tags / Expressions and Components rows open the
	Player's own menus, which act on the edited ped when reached from here
	(Menus::Target, also defined here).

	Opened from Spawner > Ped Database and World > Ped Manager. Combat
	styles and mods are the ones the game scripts name
	(tools/extract_peds.py); relationship groups come from
	tools/extract_misc.py.

	Ours: Attach To Something offers the player, their horse and the
	nearest ped, vehicle or object (Rampage lists its own spawn databases);
	labels from Rename / Force Name live here instead of in Rampage's
	database entry.
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\Log.h"

#include <cmath>
#include <format>
#include <map>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	const char* const kRelGroups[] = {
#include "..\data\RelGroups.inc"
	};
	const char* const kCombatStyles[] = {
#include "..\data\CombatStyles.inc"
	};
	const char* const kCombatStyleMods[] = {
#include "..\data\CombatStyleMods.inc"
	};
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

	constexpr Hash REL_COMPANION_GROUP = 0xB5A1D680;
	constexpr Hash REL_PLAYER_ENEMY = 0x4BAD542C;
	constexpr Hash FIRING_PATTERN_FULL_AUTO = 0xC6EE6B4C;
	constexpr Hash BLIP_STYLE_COMPANION = 0x19365607;
	constexpr Hash BLIP_STYLE_ENEMY = 0x318C617C;
	constexpr Hash BLIP_STYLE_OBJECTIVE = 0x97B6F06C; // Rampage's Add Blip style
	constexpr Hash REMOVE_REASON_DEFAULT = 0xF77DE93D;
	constexpr Hash ADD_REASON_DEFAULT = 0x2CD419DC;

	// --- target binding ------------------------------------------------------------

	struct Binding
	{
		MenuBase* menu;
		std::function<Ped()> getter;
	};
	std::vector<Binding> g_bindings;
	Menus::SharedMenus g_shared;

	// --- the edited ped -------------------------------------------------------------

	Ped g_ped = 0;
	MenuBase* g_editor = nullptr;
	std::map<Ped, std::string> g_labels;
	std::vector<MenuItemToggle*> g_editorToggles;

	bool Valid() { return g_ped && ENTITY::DOES_ENTITY_EXIST(g_ped); }

	template <typename Fn>
	void WithPed(Fn fn)
	{
		if (Valid())
			fn(g_ped);
	}

	MenuItemToggle* EditorToggle(MenuBase* menu, const std::string& caption, std::function<void(Ped, bool)> onChange, std::function<void(Ped)> onTick = nullptr)
	{
		MenuItemToggle* t = Ui::Toggle(menu, caption,
			[onChange](bool on) { WithPed([&](Ped p) { onChange(p, on); }); },
			onTick ? std::function<void()>([onTick] { WithPed(onTick); }) : nullptr);
		t->SetPersist(false); // per ped, never saved
		g_editorToggles.push_back(t);
		return t;
	}

	void TakeControl(Entity e)
	{
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(e))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, TRUE, TRUE);
	}

	std::string ModelName(Hash model)
	{
		for (const PedModel& m : kPedModels)
			if (GameUtil::Joaat(m.name) == model)
				return m.name;
		for (const char* m : kHorseModels)
			if (GameUtil::Joaat(m) == model)
				return m;
		for (const char* m : kAnimalModels)
			if (GameUtil::Joaat(m) == model)
				return m;
		return std::format("0x{:08X}", model);
	}

	std::string Label(Ped p)
	{
		auto it = g_labels.find(p);
		return it != g_labels.end() ? it->second : ModelName(ENTITY::GET_ENTITY_MODEL(p));
	}

	// --- main rows -------------------------------------------------------------------

	void SetBodyguard(Ped p, bool on)
	{
		if (ENTITY::IS_ENTITY_DEAD(p))
			return;
		const int group = PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID());
		if (on)
		{
			PED::SET_PED_COMBAT_ABILITY(p, 2);
			PED::SET_PED_COMBAT_MOVEMENT(p, 2);
			PED::SET_PED_COMBAT_ATTRIBUTES(p, 17, FALSE);
			PED::SET_PED_COMBAT_ATTRIBUTES(p, 113, TRUE);
			PED::SET_PED_COMBAT_ATTRIBUTES(p, 46, TRUE);
			PED::SET_PED_COMBAT_ATTRIBUTES(p, 58, TRUE);
			PED::SET_PED_ACCURACY(p, 100);
			PED::SET_PED_FIRING_PATTERN(p, FIRING_PATTERN_FULL_AUTO);
			PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, REL_COMPANION_GROUP);
			PED::SET_PED_AS_GROUP_LEADER(Me(), group, TRUE);
			PED::SET_PED_CONFIG_FLAG(p, 279, TRUE);
			PED::SET_GROUP_SEPARATION_RANGE(group, 400.0f);
			PED::SET_GROUP_FORMATION_SPACING(group, 1.5f, -1.0f, -1.0f);
			PED::SET_PED_CAN_PLAY_AMBIENT_ANIMS(p, TRUE);
			PED::SET_PED_CAN_TELEPORT_TO_GROUP_LEADER(p, group, TRUE);
			PED::SET_PED_AS_GROUP_MEMBER(p, group);
			MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_COMPANION, p);
		}
		else
		{
			PED::SET_PED_COMBAT_ABILITY(p, 1);
			PED::SET_PED_COMBAT_MOVEMENT(p, 2);
			PED::SET_PED_COMBAT_ATTRIBUTES(p, 17, FALSE);
			PED::SET_PED_COMBAT_ATTRIBUTES(p, 58, FALSE);
			PED::SET_PED_ACCURACY(p, 70);
			PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, PED::GET_PED_RELATIONSHIP_GROUP_DEFAULT_HASH(p));
			PED::REMOVE_PED_FROM_GROUP(p);
			PED::SET_PED_CONFIG_FLAG(p, 279, FALSE);
			Blip blip = MAP::GET_BLIP_FROM_ENTITY(p);
			if (blip)
				MAP::REMOVE_BLIP(&blip);
		}
	}

	void SetAsEnemy(Ped p)
	{
		if (ENTITY::IS_ENTITY_DEAD(p))
			return;
		PED::SET_PED_COMBAT_ATTRIBUTES(p, 17, FALSE);
		PED::SET_PED_COMBAT_ATTRIBUTES(p, 5, TRUE);
		PED::SET_PED_COMBAT_ATTRIBUTES(p, 46, TRUE);
		PED::SET_PED_COMBAT_ABILITY(p, 1);
		PED::SET_PED_COMBAT_MOVEMENT(p, 2);
		PED::SET_PED_FIRING_PATTERN(p, FIRING_PATTERN_FULL_AUTO);
		PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, REL_PLAYER_ENEMY);
		TASK::TASK_COMBAT_PED(p, Me(), 0, 16);
		PED::SET_PED_KEEP_TASK(p, TRUE);
		MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_ENEMY, p);
	}

	void MountMyHorse(Ped p)
	{
		if (!PED::IS_PED_HUMAN(p))
			return;
		if (PED::IS_PED_ON_MOUNT(p))
		{
			PED::_REMOVE_PED_FROM_MOUNT(p, FALSE, FALSE);
			return;
		}
		if (PED::IS_PED_ON_MOUNT(Me()))
			PED::SET_PED_ONTO_MOUNT(p, PED::GET_MOUNT(Me()), 0, TRUE); // behind the player
	}

	void Teleport(Entity what, Entity to)
	{
		const Vector3 v = ENTITY::GET_ENTITY_COORDS(to, TRUE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(what, v.x, v.y, v.z, FALSE, FALSE, FALSE);
	}

	void AddBlip(Ped p)
	{
		if (MAP::GET_BLIP_FROM_ENTITY(p))
			return;
		const Blip blip = MAP::BLIP_ADD_FOR_ENTITY(BLIP_STYLE_OBJECTIVE, p);
		MAP::_SET_BLIP_NAME(blip, MISC::VAR_STRING(10, "LITERAL_STRING", Label(p).c_str()));
	}

	void Revive(Ped p)
	{
		PED::SET_PED_CONFIG_FLAG(p, 11, FALSE);
		PED::SET_PED_CONFIG_FLAG(p, 580, FALSE);
		PED::_INCAPACITATED_REVIVE(p, 0);
		PED::REVIVE_INJURED_PED(p);
		PED::RESURRECT_PED(p);
		ENTITY::SET_ENTITY_HEALTH(p, ENTITY::GET_ENTITY_MAX_HEALTH(p, TRUE), 0);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(p, FALSE, FALSE);
		const Vector3 v = ENTITY::GET_ENTITY_COORDS(p, FALSE, FALSE);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(p, v.x, v.y, v.z, FALSE, FALSE, FALSE);
	}

	void Clean(Ped p)
	{
		PED::CLEAR_PED_BLOOD_DAMAGE(p);
		PED::CLEAR_PED_DAMAGE_DECAL_BY_ZONE(p, 10, "ALL");
		PED::CLEAR_PED_ENV_DIRT(p);
		PED::CLEAR_PED_WETNESS(p);
	}

	void Delete(Ped p)
	{
		TakeControl(p);
		Entity e = p;
		ENTITY::DELETE_ENTITY(&e);
		g_labels.erase(p);
		g_ped = 0;
		Ui::Controller().PopMenu();
	}

	// Duel: the player draws on the ped, which fires back a few seconds
	// later (Rampage's sequence).
	std::string Duel(Ped p)
	{
		if (!PED::IS_PED_HUMAN(p))
			return "Only humans can duel";
		const Ped me = Me();
		const Vector3 a = ENTITY::GET_ENTITY_COORDS(me, TRUE, FALSE);
		const Vector3 b = ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE);
		TASK::TASK_TURN_PED_TO_FACE_ENTITY(p, me, 0, -1.0f, -1.0f, -1.0f);
		WEAPON::_HIDE_PED_WEAPONS(me, 2, TRUE);
		WEAPON::_HIDE_PED_WEAPONS(p, 2, TRUE);
		PED::SET_PED_SHOOT_RATE(p, 200);
		const float heading = std::atan2(b.y - a.y, b.x - a.x) * 180.0f / 3.14159265f - 90.0f;
		TASK::TASK_DUEL(me, 0, 0.0f, p, 0.2f, 0, b.x, b.y, b.z, heading, 1);
		WAIT(MISC::GET_RANDOM_INT_IN_RANGE(3000, 3500));
		TASK::TASK_COMBAT_PED(p, me, 0, 0);
		PED::SET_PED_KEEP_TASK(p, TRUE);
		return "";
	}

	std::string MakeCopy(Ped p)
	{
		const Ped copy = PED::CLONE_PED(p, FALSE, TRUE, TRUE);
		if (!ENTITY::DOES_ENTITY_EXIST(copy))
			return "Couldn't copy";
		g_labels[copy] = Label(p) + " (copy)";
		return "Copied";
	}

	// --- general -----------------------------------------------------------------------

	int g_relationship = 0;
	int g_alpha = 0; // index into kAlphas
	const int kAlphas[] = { 255, 204, 153, 102, 51, 0 };
	int g_health = 100;
	float g_scale = 1.0f;
	int g_accuracy = 50;

	void MakeInteractable(Ped p)
	{
		MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(p);
		PED::SET_PED_CONFIG_FLAG(p, 178, TRUE);
		for (int flag : { 315, 331, 130, 301 })
			PED::SET_PED_CONFIG_FLAG(p, flag, FALSE);
		MISC::REGISTER_INTERACTION_LOCKON_PROMPT(p, "INTERACT_LOCKON", 7.0f, 0.0f, 0, 0.0f, 0.0f, 0, FALSE, -1);
	}

	std::string SetFlag(Ped p)
	{
		std::string text;
		if (!GameUtil::PromptText("Config Flag:", text) || text.empty())
			return "";
		const int flag = std::atoi(text.c_str());
		const bool on = !PED::GET_PED_CONFIG_FLAG(p, flag, FALSE);
		PED::SET_PED_CONFIG_FLAG(p, flag, on);
		return std::format("Flag {} {}", flag, on ? "on" : "off");
	}

	void BuildCombatStyle(MenuBase* combat)
	{
		static MenuBase* attributes = nullptr;
		static int values[128];
		attributes = Ui::ListMenu(combat, "Combat Attributes", [](MenuBase* m)
		{
			if (!Valid())
				return;
			for (int i = 0; i < 128; i++)
			{
				values[i] = PED::_GET_PED_COMBAT_ATTRIBUTE(g_ped, i) ? 1 : 0;
				Ui::Choice(m, std::format("Attribute {}", i), { "Off", "On" }, &values[i],
					[i](int v) { WithPed([&](Ped p) { PED::SET_PED_COMBAT_ATTRIBUTES(p, i, v != 0); }); });
			}
		});
		Ui::Do(combat, "pededitor.clearcombatstyle", "Clear Combat Style", [] { WithPed([](Ped p) { PED::_CLEAR_PED_COMBAT_STYLE(p, 1); }); });
		Ui::Do(combat, "pededitor.clearcombatmods", "Clear Combat Mods", []
		{
			WithPed([](Ped p) {
				for (const char* mod : kCombatStyleMods)
					PED::_CLEAR_PED_COMBAT_STYLE_MOD(p, GameUtil::Joaat(mod));
			});
		});
		Ui::Section(combat, "Combat Styles");
		for (const char* style : kCombatStyles)
			Ui::Do(combat, Ui::Id("pededitor.combatstyle", style), style, [style] { WithPed([&](Ped p) { PED::_SET_PED_COMBAT_STYLE(p, GameUtil::Joaat(style), 1, -1.0f); }); });
		Ui::Section(combat, "Combat Mods");
		for (const char* mod : kCombatStyleMods)
			Ui::Do(combat, Ui::Id("pededitor.combatmod", mod), mod, [mod] { WithPed([&](Ped p) { PED::_SET_PED_COMBAT_STYLE_MOD(p, GameUtil::Joaat(mod), -1.0f); }); });
	}

	void BuildGeneral(MenuBase* editor)
	{
		MenuBase* general = Ui::Submenu(editor, "General");
		std::vector<std::string> groups(std::begin(kRelGroups), std::end(kRelGroups));
		Ui::Choice(general, "pededitor.relationship", "Relationship", groups, &g_relationship,
			[](int i) { WithPed([&](Ped p) { PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, GameUtil::Joaat(kRelGroups[i])); }); });
		Ui::Do(general, "pededitor.makeinteractable", "Make Interactable", [] { WithPed(MakeInteractable); });
		Ui::Action(general, "pededitor.rename", "Rename", []() -> std::string
		{
			std::string name = Valid() ? Label(g_ped) : "";
			if (!Valid() || !GameUtil::PromptText("Name:", name) || name.empty())
				return "";
			g_labels[g_ped] = name;
			return "Renamed";
		})->SetHotkeyable(false);
		Ui::Action(general, "pededitor.setprompt", "Set Prompt", []() -> std::string
		{
			std::string name;
			if (!Valid() || !GameUtil::PromptText("Prompt Name:", name) || name.empty())
				return "";
			PED::_SET_PED_PROMPT_NAME(g_ped, name.c_str());
			return "";
		})->SetHotkeyable(false);
		Ui::Action(general, "pededitor.setflag", "Set Flag", [] { return Valid() ? SetFlag(g_ped) : std::string(); });
		BuildCombatStyle(Ui::Submenu(general, "Combat Style"));
		EditorToggle(general, "Block Fleeing", [](Ped p, bool on) { PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, on); });
		EditorToggle(general, "Invincibility", [](Ped p, bool on) { ENTITY::SET_ENTITY_INVINCIBLE(p, on); });
		Ui::Choice(general, "pededitor.visibility", "Visibility", { "100%", "80%", "60%", "40%", "20%", "0%" }, &g_alpha,
			[](int i) { WithPed([&](Ped p) { ENTITY::SET_ENTITY_ALPHA(p, kAlphas[i], FALSE); }); });
		EditorToggle(general, "Freeze Entity", [](Ped p, bool on) { ENTITY::FREEZE_ENTITY_POSITION(p, on); });
		EditorToggle(general, "Handcuffs", [](Ped p, bool on) { if (PED::IS_PED_HUMAN(p)) PED::SET_ENABLE_HANDCUFFS(p, on, FALSE); });
		Ui::Number(general, "pededitor.pedhealth", "Ped Health", &g_health, 0, 10000, 50, [] { WithPed([](Ped p) { ENTITY::SET_ENTITY_HEALTH(p, g_health, 0); }); }, true);
		Ui::Number(general, "pededitor.pedscale", "Ped Scale", &g_scale, 0.1f, 10.0f, 0.1f, [] { WithPed([](Ped p) { PED::_SET_PED_SCALE(p, g_scale); }); }, true);
		EditorToggle(general, "Is Wild", [](Ped p, bool on)
		{
			if (PED::IS_PED_HUMAN(p))
				return;
			WEAPON::_HIDE_PED_WEAPONS(p, 2, TRUE);
			FLOCK::_SET_ANIMAL_IS_WILD(p, on);
		});
		general->SetOnOpen([](MenuBase*)
		{
			if (!Valid())
				return;
			g_health = ENTITY::GET_ENTITY_HEALTH(g_ped);
		});
	}

	// --- weapons -----------------------------------------------------------------------

	int g_removeMode = 0; // all, current
	float g_weaponScale = 2.0f;
	Object g_scaledWeapon = 0;

	void GiveWeapon(Ped p, Hash weapon)
	{
		WEAPON::GIVE_WEAPON_TO_PED(p, weapon, 100, TRUE, FALSE, 0, TRUE, 0.5f, 1.0f, ADD_REASON_DEFAULT, TRUE, 0.0f, FALSE);
		WEAPON::SET_CURRENT_PED_WEAPON(p, weapon, TRUE, 0, FALSE, FALSE);
	}

	// Weapon Scale: a scaled copy of the current weapon over the real one,
	// which is hidden (Rampage's approach).
	void RemoveScaledWeapon(Ped p)
	{
		if (const Entity real = WEAPON::GET_CURRENT_PED_WEAPON_ENTITY_INDEX(p, 0))
			ENTITY::SET_ENTITY_ALPHA(real, 255, FALSE);
		if (ENTITY::DOES_ENTITY_EXIST(g_scaledWeapon))
		{
			ENTITY::DETACH_ENTITY(g_scaledWeapon, TRUE, TRUE);
			OBJECT::DELETE_OBJECT(&g_scaledWeapon);
		}
		g_scaledWeapon = 0;
	}

	void ApplyWeaponScale(Ped p)
	{
		RemoveScaledWeapon(p);
		Hash weapon = 0;
		if (!WEAPON::GET_CURRENT_PED_WEAPON(p, &weapon, TRUE, 0, FALSE) || !WEAPON::IS_WEAPON_VALID(weapon))
			return;
		const Entity real = WEAPON::GET_CURRENT_PED_WEAPON_ENTITY_INDEX(p, 0);
		if (!real)
			return;
		const Vector3 v = ENTITY::GET_ENTITY_COORDS(p, FALSE, FALSE);
		g_scaledWeapon = WEAPON::_CREATE_WEAPON_OBJECT(weapon, 200, v.x, v.y, v.z, TRUE, g_weaponScale);
		if (!ENTITY::DOES_ENTITY_EXIST(g_scaledWeapon))
			return;
		ENTITY::ATTACH_ENTITY_TO_ENTITY(g_scaledWeapon, real, 0, 0, 0, 0, 0, 0, 0, FALSE, FALSE, FALSE, TRUE, 2, FALSE, FALSE, FALSE);
		ENTITY::SET_ENTITY_ALPHA(real, 0, FALSE);
	}

	void BuildWeapons(MenuBase* editor)
	{
		MenuBase* weapons = Ui::Submenu(editor, "Weapons");
		Ui::Number(weapons, "pededitor.weaponaccuracy", "Weapon Accuracy", &g_accuracy, 0, 100, 5, [] { WithPed([](Ped p) { PED::SET_PED_ACCURACY(p, g_accuracy); }); });
		Ui::Choice(weapons, "pededitor.remove", "Remove", { "All Weapons", "Current Weapon" }, &g_removeMode, [](int mode)
		{
			WithPed([mode](Ped p) {
				if (mode == 0)
				{
					WEAPON::REMOVE_ALL_PED_WEAPONS(p, TRUE, TRUE);
					WEAPON::_HIDE_PED_WEAPONS(p, 2, TRUE);
				}
				else
				{
					Hash current = 0;
					if (WEAPON::GET_CURRENT_PED_WEAPON(p, &current, TRUE, 0, FALSE))
						WEAPON::REMOVE_WEAPON_FROM_PED(p, current, TRUE, REMOVE_REASON_DEFAULT);
				}
			});
		});
		Ui::Do(weapons, "pededitor.dropweapon", "Drop Weapon", [] { WithPed([](Ped p) { WEAPON::MAKE_PED_DROP_WEAPON(p, TRUE, 0, TRUE, FALSE); }); });
		Ui::Do(weapons, "pededitor.givedefaultweapons", "Give Default Weapons", []
		{
			WithPed([](Ped p) { WEAPON::_GIVE_WEAPON_COLLECTION_TO_PED(p, WEAPON::_GET_DEFAULT_PED_WEAPON_COLLECTION(ENTITY::GET_ENTITY_MODEL(p))); });
		});
		Ui::Action(weapons, "pededitor.givecustom", "Give Custom", []() -> std::string
		{
			std::string name;
			if (!Valid() || !GameUtil::PromptText("Weapon Name:", name) || name.empty())
				return "";
			GiveWeapon(g_ped, GameUtil::ParseHash(name));
			return "";
		})->SetHotkeyable(false);
		Ui::Do(weapons, "pededitor.fillammo", "Fill Ammo", []
		{
			WithPed([](Ped p) {
				Hash current = 0;
				if (WEAPON::GET_CURRENT_PED_WEAPON(p, &current, TRUE, 0, FALSE))
					WEAPON::SET_PED_AMMO(p, current, 200);
			});
		});
		EditorToggle(weapons, "Weapon Scale", [](Ped p, bool on) { if (on) ApplyWeaponScale(p); else RemoveScaledWeapon(p); });
		Ui::Number(weapons, "pededitor.weaponscalesize", "Weapon Scale Size", &g_weaponScale, 0.1f, 10.0f, 0.1f, [] { WithPed([](Ped p) { if (g_scaledWeapon) ApplyWeaponScale(p); }); });
		Ui::NameList(weapons, "Give Weapon", Menus::WeaponNames(), [](const std::string& name)
		{
			WithPed([&](Ped p) { GiveWeapon(p, GameUtil::Joaat(name)); });
		});
	}

	// --- wardrobe ----------------------------------------------------------------------

	int g_outfitPreset = 0;

	std::string PromptHash(const char* title, Hash& out)
	{
		std::string text;
		if (!GameUtil::PromptText(title, text) || text.empty())
			return "cancel";
		out = GameUtil::ParseHash(text);
		return "";
	}

	void BuildWardrobe(MenuBase* editor)
	{
		const Menus::SharedMenus& s = g_shared;
		MenuBase* wardrobe = Ui::Submenu(editor, "Wardrobe");
		Ui::Link(wardrobe, "Apply Damage Packs", s.damagePacks);
		Ui::Link(wardrobe, "Walk Styles", s.walkStyles);
		Ui::Link(wardrobe, "Meta Ped Tags", s.metaTags);
		Ui::Link(wardrobe, "Meta Ped Expressions", s.metaExpressions);
		Ui::Link(wardrobe, "Outfits", s.outfits);
		Ui::Do(wardrobe, "pededitor.cloneoutfittome", "Clone Outfit to Me", [] { WithPed([](Ped p) { PED::CLONE_PED_TO_TARGET(p, Me()); }); });
		Ui::Number(wardrobe, "pededitor.outfitpreset", "Outfit Preset", &g_outfitPreset, 0, 200, 1,
			[] { WithPed([](Ped p) { PED::_EQUIP_META_PED_OUTFIT_PRESET(p, g_outfitPreset, FALSE); }); }, true);
		Ui::Do(wardrobe, "pededitor.randomoutfit", "Random Outfit", [] { WithPed([](Ped p) { PED::_SET_RANDOM_OUTFIT_VARIATION(p, TRUE); }); });
		Ui::Do(wardrobe, "pededitor.removeallcomponents", "Remove all Components", []
		{
			WithPed([](Ped p) {
				for (int i = PED::_GET_NUM_COMPONENTS_IN_PED(p) - 1; i >= 0; i--)
					if (const Hash category = PED::_GET_PED_COMPONENT_CATEGORY_BY_INDEX(p, i))
						PED::REMOVE_TAG_FROM_META_PED(p, category, 1);
				PED::_UPDATE_PED_VARIATION(p, FALSE, TRUE, TRUE, TRUE, FALSE);
			});
		});
		Ui::Do(wardrobe, "pededitor.drophat", "Drop Hat", [] { WithPed([](Ped p) { PED::KNOCK_OFF_PED_PROP(p, FALSE, FALSE, FALSE, TRUE); }); });
		Ui::Link(wardrobe, "Components", s.components);
		Ui::Section(wardrobe, "Custom");
		Ui::Action(wardrobe, "pededitor.enablepedcomponent", "Enable Ped Component", []() -> std::string
		{
			Hash h = 0;
			if (!Valid() || !PromptHash("Component Hash:", h).empty())
				return "";
			PED::_APPLY_SHOP_ITEM_TO_PED(g_ped, h, TRUE, TRUE, FALSE);
			PED::_UPDATE_PED_VARIATION(g_ped, FALSE, TRUE, TRUE, TRUE, FALSE);
			return "";
		});
		Ui::Action(wardrobe, "pededitor.disablepedcomponent", "Disable Ped Component", []() -> std::string
		{
			Hash h = 0;
			if (!Valid() || !PromptHash("Category Hash:", h).empty())
				return "";
			PED::REMOVE_TAG_FROM_META_PED(g_ped, h, 1);
			PED::_UPDATE_PED_VARIATION(g_ped, FALSE, TRUE, TRUE, TRUE, FALSE);
			return "";
		});
		Ui::Action(wardrobe, "pededitor.setbodycomponent", "Set Body Component", []() -> std::string
		{
			Hash h = 0;
			if (!Valid() || !PromptHash("Body Component Hash:", h).empty())
				return "";
			PED::_SET_META_PED_TAG(g_ped, h, 0, 0, 0, 0, 0, 0, 0);
			PED::_UPDATE_PED_VARIATION(g_ped, FALSE, TRUE, TRUE, TRUE, FALSE);
			return "";
		});
	}

	// --- positioning and attaching ----------------------------------------------------

	int g_precision = 1;
	const float kPrecision[] = { 0.01f, 0.1f, 1.0f, 10.0f };
	float g_pos[3] = {};
	float g_heading = 0.0f;
	Entity g_attachedTo = 0;
	float g_offset[3] = {};
	float g_rotation[3] = {};

	void Reattach()
	{
		WithPed([](Ped p) {
			if (!g_attachedTo || !ENTITY::DOES_ENTITY_EXIST(g_attachedTo))
				return;
			ENTITY::ATTACH_ENTITY_TO_ENTITY(p, g_attachedTo, 0, g_offset[0], g_offset[1], g_offset[2],
				g_rotation[0], g_rotation[1], g_rotation[2], FALSE, FALSE, TRUE, FALSE, 2, TRUE, FALSE, FALSE);
		});
	}

	void BuildPositioning(MenuBase* m)
	{
		if (!Valid())
			return;
		const Vector3 v = ENTITY::GET_ENTITY_COORDS(g_ped, FALSE, FALSE);
		g_pos[0] = v.x; g_pos[1] = v.y; g_pos[2] = v.z;
		g_heading = ENTITY::GET_ENTITY_HEADING(g_ped);
		Ui::Choice(m, "Precision", { "0.01", "0.1", "1", "10" }, &g_precision, [](int) { Ui::Controller().ReopenActiveLater(); });
		const float step = kPrecision[g_precision];
		if (g_attachedTo && ENTITY::IS_ENTITY_ATTACHED(g_ped))
		{
			Ui::Section(m, "Attachment");
			const char* const kAxis[] = { "X Axis", "Y Axis", "Z Axis" };
			const char* const kRot[] = { "Pitch", "Roll", "Yaw" };
			for (int i = 0; i < 3; i++)
				Ui::Number(m, kAxis[i], &g_offset[i], -100.0f, 100.0f, step, Reattach);
			for (int i = 0; i < 3; i++)
				Ui::Number(m, kRot[i], &g_rotation[i], -180.0f, 180.0f, step * 10.0f, Reattach);
			return;
		}
		Ui::Section(m, "Position");
		auto move = [] { WithPed([](Ped p) { ENTITY::SET_ENTITY_COORDS_NO_OFFSET(p, g_pos[0], g_pos[1], g_pos[2], FALSE, FALSE, FALSE); }); };
		Ui::Number(m, "X Axis", &g_pos[0], -10000.0f, 10000.0f, step, move);
		Ui::Number(m, "Y Axis", &g_pos[1], -10000.0f, 10000.0f, step, move);
		Ui::Number(m, "Z Axis", &g_pos[2], -1000.0f, 3000.0f, step, move);
		Ui::Number(m, "Heading", &g_heading, 0.0f, 360.0f, step * 10.0f, [] { WithPed([](Ped p) { ENTITY::SET_ENTITY_HEADING(p, g_heading); }); });
	}

	Entity Nearest(const std::vector<Entity>& all, Entity skip)
	{
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		Entity best = 0;
		float bestD = 1e12f;
		for (Entity e : all)
		{
			if (e == skip || e == Me())
				continue;
			const float d = GameUtil::DistanceSq(me, ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE));
			if (d < bestD)
			{
				bestD = d;
				best = e;
			}
		}
		return best;
	}

	void AttachTo(Entity target)
	{
		if (!Valid() || !target || target == g_ped)
			return;
		g_attachedTo = target;
		g_offset[0] = g_offset[1] = 0.0f;
		g_offset[2] = 1.0f;
		g_rotation[0] = g_rotation[1] = g_rotation[2] = 0.0f;
		Reattach();
		Ui::Controller().PopMenu();
	}

	void BuildAttach(MenuBase* m)
	{
		Ui::Do(m, "Me", [] { AttachTo(Me()); });
		Ui::Do(m, "My Horse", [] { AttachTo(GameUtil::PlayerHorse()); });
		Ui::Do(m, "Nearest Ped", [] { AttachTo(Nearest(GameUtil::AllPeds(), g_ped)); });
		Ui::Do(m, "Nearest Vehicle", [] { AttachTo(Nearest(GameUtil::AllVehicles(), g_ped)); });
		Ui::Do(m, "Nearest Object", [] { AttachTo(Nearest(GameUtil::AllObjects(), g_ped)); });
	}

	// --- spectate ----------------------------------------------------------------------

	Cam g_spectateCam = 0;

	void SpectateTick(Ped p)
	{
		if (!CAMERA::DOES_CAM_EXIST(g_spectateCam))
		{
			g_spectateCam = CAMERA::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", TRUE);
			CAMERA::SET_CAM_ACTIVE(g_spectateCam, TRUE);
			CAMERA::RENDER_SCRIPT_CAMS(TRUE, TRUE, 1000, TRUE, FALSE, 0);
		}
		CAMERA::ATTACH_CAM_TO_ENTITY(g_spectateCam, p, 0.0f, -4.0f, 1.5f, TRUE);
		CAMERA::POINT_CAM_AT_ENTITY(g_spectateCam, p, 0.0f, 0.0f, 0.5f, TRUE);
		STREAMING::SET_FOCUS_ENTITY(p);
	}

	void StopSpectating()
	{
		if (CAMERA::DOES_CAM_EXIST(g_spectateCam))
		{
			CAMERA::SET_CAM_ACTIVE(g_spectateCam, FALSE);
			CAMERA::DESTROY_CAM(g_spectateCam, FALSE);
			CAMERA::RENDER_SCRIPT_CAMS(FALSE, TRUE, 1000, TRUE, FALSE, 0);
		}
		g_spectateCam = 0;
		STREAMING::CLEAR_FOCUS();
	}
}

namespace Menus
{
	namespace Target
	{
		void Bind(MenuBase* menu, std::function<Ped()> getter)
		{
			g_bindings.push_back({ menu, std::move(getter) });
		}

		Ped Get()
		{
			const auto& stack = Ui::Controller().GetStack();
			for (auto it = stack.rbegin(); it != stack.rend(); ++it)
				for (const Binding& b : g_bindings)
					if (b.menu == *it)
					{
						const Ped p = b.getter();
						return p && ENTITY::DOES_ENTITY_EXIST(p) ? p : PLAYER::PLAYER_PED_ID();
					}
			return PLAYER::PLAYER_PED_ID();
		}
	}

	SharedMenus& Shared() { return g_shared; }

	namespace PedEditor
	{
		void Build()
		{
			g_editor = Ui::Submenu(nullptr, "Ped Editor");
			Target::Bind(g_editor, [] { return g_ped; });
			MenuBase* e = g_editor;
			const SharedMenus& s = g_shared;

			e->AddItem(new MenuItemLabel([] { return Valid() ? "Editing: " + Label(g_ped) : std::string("The ped is gone"); }));
			BuildGeneral(e);
			BuildWardrobe(e);
			BuildWeapons(e);
			Ui::Link(e, "Effects", s.effects);
			Ui::Link(e, "Emotes", s.emotes);
			Ui::Link(e, "Scenarios", s.scenarios);
			Ui::Link(e, "Animations", s.animations);
			Ui::Link(e, "Play Speech", s.speech);
			Ui::ListMenu(e, "Positioning", BuildPositioning);
			EditorToggle(e, "Spectate", [](Ped, bool on) { if (!on) StopSpectating(); }, SpectateTick);
			EditorToggle(e, "Bodyguard", SetBodyguard);
			Ui::Do(e, "pededitor.setasenemy", "Set as Enemy", [] { WithPed(SetAsEnemy); });
			Ui::Do(e, "pededitor.mountdismountmyhorse", "Mount / Dismount my Horse", [] { WithPed(MountMyHorse); });
			Ui::Do(e, "pededitor.teleporttome", "Teleport to Me", [] { WithPed([](Ped p) { Teleport(p, Me()); }); });
			Ui::Do(e, "pededitor.teleporttoped", "Teleport to Ped", [] { WithPed([](Ped p) { Teleport(Me(), p); }); });
			Ui::Do(e, "pededitor.addblip", "Add Blip", [] { WithPed(AddBlip); });
			Ui::Do(e, "pededitor.restoreloot", "Restore Loot", [] { WithPed([](Ped p) { ENTITY::_SET_ENTITY_FULLY_LOOTED(p, FALSE); }); });
			Ui::Do(e, "pededitor.ragdoll", "Ragdoll", [] { WithPed([](Ped p) { PED::SET_PED_TO_RAGDOLL(p, 2000, 2000, 0, TRUE, TRUE, "DraggedByCart"); }); });
			Ui::Do(e, "pededitor.revive", "Revive", [] { WithPed(Revive); });
			Ui::Do(e, "pededitor.clean", "Clean", [] { WithPed(Clean); });
			Ui::Do(e, "pededitor.bleedout", "Bleed out", [] { WithPed([](Ped p) { TASK::_TASK_ANIMAL_BLEED_OUT(p, 0, FALSE, 0, 0, 0); }); });
			Ui::Do(e, "pededitor.kill", "Kill", [] { WithPed([](Ped p) { ENTITY::SET_ENTITY_HEALTH(p, 0, 0); }); });
			Ui::Do(e, "pededitor.delete", "Delete", [] { WithPed(Delete); });
			Ui::Action(e, "pededitor.duel", "Duel", [] { return Valid() ? Duel(g_ped) : std::string(); });
			Ui::ListMenu(e, "Attach To Something", BuildAttach);
			Ui::Do(e, "pededitor.detach", "Detach", []
			{
				WithPed([](Ped p) { ENTITY::DETACH_ENTITY(p, FALSE, TRUE); });
				g_attachedTo = 0;
			});
			Ui::Action(e, "pededitor.makecopy", "Make Copy", [] { return Valid() ? MakeCopy(g_ped) : std::string(); });
			Ui::Do(e, "pededitor.forcename", "Force Name", [] { WithPed([](Ped p) { g_labels[p] = ModelName(ENTITY::GET_ENTITY_MODEL(p)); }); });
		}

		void Open(Ped ped)
		{
			if (!ENTITY::DOES_ENTITY_EXIST(ped))
				return;
			if (ped != g_ped)
			{
				if (g_spectateCam)
					StopSpectating();
				if (g_scaledWeapon)
					RemoveScaledWeapon(g_ped);
				for (MenuItemToggle* t : g_editorToggles)
					t->SetState(false);
				g_attachedTo = 0;
			}
			g_ped = ped;
			TakeControl(ped);
			g_scale = 1.0f;
			g_alpha = 0;
			g_accuracy = PED::GET_PED_ACCURACY(ped);
			for (MenuItemToggle* t : g_editorToggles)
				if (t->GetCaption() == "Bodyguard")
					t->SetState(PED::IS_PED_GROUP_MEMBER(ped, PLAYER::GET_PLAYER_GROUP(PLAYER::PLAYER_ID()), FALSE));
				else if (t->GetCaption() == "Is Wild")
					t->SetState(!PED::IS_PED_HUMAN(ped) && FLOCK::_GET_ANIMAL_IS_WILD(ped));
			Ui::Push(g_editor);
		}
	}
}
