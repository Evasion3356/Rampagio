/*
	Player submenus: ports Rampage's Submenus::SubSelfScenarios,
	SubSelfWardrobe (Walk Styles and Damage Packs only, via SubSelfWalkStyles
	and SubDamagePacks), SubTimecycleMod and SubAnimPostFx (Vision), SubMoods,
	SubAbilities, SubPlayerProofs and SubPlayerConfigFlags. The name lists
	come from the game scripts (tools/extract_player_lists.py), not from
	Rampage's tables.
*/

#include "Menus.h"
#include "..\GameUtil.h"

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Player MyPlayer() { return PLAYER::PLAYER_ID(); }

	template <size_t N>
	std::span<const char* const> Names(const char* const (&names)[N]) { return names; }

	const char* const kScenarios[] = {
#include "..\data\Scenarios.inc"
	};
	const char* const kWalkStyles[] = {
#include "..\data\WalkStyles.inc"
	};
	const char* const kDamagePacks[] = {
#include "..\data\DamagePacks.inc"
	};
	const char* const kTimecycles[] = {
#include "..\data\Timecycles.inc"
	};
	const char* const kPostFx[] = {
#include "..\data\PostFx.inc"
	};
	const char* const kMoods[] = {
#include "..\data\Moods.inc"
	};

	// --- Scenarios ------------------------------------------------------

	void PlayScenario(const std::string& name)
	{
		const Ped ped = Me();
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, FALSE, FALSE);
		TASK::TASK_START_SCENARIO_IN_PLACE_HASH(ped, GameUtil::Joaat(name), -1, TRUE, 0, -1.0f, FALSE);
	}

	// Rampage's values: the nearest scenario point within 50m, for 10 s.
	void UseNearestScenario()
	{
		const Ped ped = Me();
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, TRUE, FALSE);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, FALSE, FALSE);
		TASK::_TASK_USE_NEAREST_SCENARIO_TO_COORD(ped, p.x, p.y, p.z, 50.0f, 10000, FALSE, FALSE, FALSE, FALSE);
	}

	// --- Wardrobe -------------------------------------------------------

	void SetWalkStyle(const std::string& style)
	{
		const Ped ped = Me();
		PED::_SET_PED_DESIRED_LOCO_FOR_MODEL(ped, "DEFAULT");
		PED::_SET_PED_DESIRED_LOCO_MOTION_TYPE(ped, style.c_str());
	}

	void ClearDamage()
	{
		const Ped ped = Me();
		PED::CLEAR_PED_BLOOD_DAMAGE(ped);
		PED::_SET_PED_DIRT_CLEANED(ped, 0.0f, -1, TRUE, TRUE);
		PED::CLEAR_PED_DAMAGE_DECAL_BY_ZONE(ped, 10, "ALL");
		for (int zone : { 3, 0, 5, 7, 8, 9 })
			PED::CLEAR_PED_BLOOD_DAMAGE_BY_ZONE(ped, zone);
		PED::CLEAR_PED_ENV_DIRT(ped);
		PED::CLEAR_PED_WETNESS(ped);
	}

	// --- Vision ---------------------------------------------------------

	float g_timecycleStrength = 1.0f;

	// --- Abilities ------------------------------------------------------

	float g_healthRecharge = 1.0f;
	float g_staminaRecharge = 1.0f;
	int g_deadEyeLevel = 1;
	int g_eagleEyeMode = 0;

	void HealthRechargeTick()
	{
		if (PLAYER::_GET_PLAYER_HEALTH_RECHARGE_MULTIPLIER(MyPlayer()) != g_healthRecharge)
			PLAYER::SET_PLAYER_HEALTH_RECHARGE_MULTIPLIER(MyPlayer(), g_healthRecharge);
	}

	void StaminaRechargeTick()
	{
		if (PLAYER::_GET_PLAYER_STAMINA_RECHARGE_MULTIPLIER(MyPlayer()) != g_staminaRecharge)
			PLAYER::SET_PLAYER_STAMINA_RECHARGE_MULTIPLIER(MyPlayer(), g_staminaRecharge);
	}

	void SetDeadEyeLevel()
	{
		PLAYER::_SET_DEADEYE_ABILITY_LOCKED(MyPlayer(), g_deadEyeLevel, FALSE);
		PLAYER::_SET_DEADEYE_ABILITY_LEVEL(MyPlayer(), g_deadEyeLevel);
	}

	// Rampage's drain rates: 0.02 while on, 1/3 when switched off.
	void SetUnlimitedEagleEye(bool on)
	{
		PLAYER::_EAGLE_EYE_SET_DRAIN_RATE_MODIFIER(MyPlayer(), on ? 0.02f : 1.0f / 3.0f);
	}

	// The second argument picks the mode (0 default, 1 trail vision). The
	// struct is Rampage's: four 8-byte script slots { 1, 0, -1.0f, -1.0f }.
	void SetEagleEyeMode(int mode)
	{
		alignas(8) UINT64 color[4] = { 1, 0, 0xBF800000, 0xBF800000 };
		PLAYER::_EAGLE_EYE_SET_COLOR(MyPlayer(), mode, reinterpret_cast<Any*>(color));
	}

	// --- Player Proofs --------------------------------------------------

	// SET_ENTITY_PROOFS bits, in Rampage's row order (bullet = bit 0 ...
	// projectile = bit 8).
	int g_proofs = 0;

	void ApplyProofs() { ENTITY::SET_ENTITY_PROOFS(Me(), g_proofs, FALSE); }

	void SetProof(int bit, bool on)
	{
		if (on)
			g_proofs |= 1 << bit;
		else
			g_proofs &= ~(1 << bit);
		ApplyProofs();
	}

	// --- Config Flags ---------------------------------------------------

	int g_configFlag = 0;
	int g_configStatus = 0;

	// Ours: picking a flag shows its current state.
	void ReadConfigFlag() { g_configStatus = PED::GET_PED_CONFIG_FLAG(Me(), g_configFlag, TRUE) ? 1 : 0; }
}

namespace Menus
{
	void BuildPlayerSubmenus(MenuBase* self)
	{
		// SubSelfScenarios ("Reload List" reads Rampage's Scenarios.txt; ours
		// has the script list built in and Custom Input for anything else).
		Ui::NameList(self, "Scenarios", Names(kScenarios), PlayScenario, [](MenuBase* m) {
			Ui::Do(m, "Stop Playing", [] { TASK::CLEAR_PED_TASKS(Me(), TRUE, TRUE); });
			Ui::Do(m, "Stop Playing Immediately", [] { TASK::CLEAR_PED_TASKS_IMMEDIATELY(Me(), TRUE, TRUE); });
			Ui::Do(m, "Use Nearest", UseNearestScenario);
		});

		// SubSelfWardrobe (partial).
		MenuBase* wardrobe = Ui::Submenu(self, "Wardrobe");
		Ui::NameList(wardrobe, "Walk Styles", Names(kWalkStyles), SetWalkStyle, [](MenuBase* m) {
			Ui::Do(m, "Reset", [] { PED::_CLEAR_PED_DESIRED_LOCO_MOTION_TYPE(Me()); });
		});
		Ui::NameList(wardrobe, "Apply Damage Packs", Names(kDamagePacks),
			[](const std::string& pack) { PED::APPLY_PED_DAMAGE_PACK(Me(), pack.c_str(), 1.0f, 1.0f); },
			[](MenuBase* m) { Ui::Do(m, "Clear all", ClearDamage); });

		// Vision: SubTimecycleMod and SubAnimPostFx.
		MenuBase* vision = Ui::Submenu(self, "Vision");
		Ui::NameList(vision, "Timecycle Modifiers", Names(kTimecycles),
			[](const std::string& name) { GRAPHICS::SET_TRANSITION_TIMECYCLE_MODIFIER(name.c_str(), 0.0f); },
			[](MenuBase* m) {
				Ui::Do(m, "Clear all", [] { GRAPHICS::CLEAR_TIMECYCLE_MODIFIER(); });
				Ui::Number(m, "Strength", &g_timecycleStrength, 0.0f, 1.0f, 0.05f,
					[] { GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(g_timecycleStrength); });
			});
		Ui::NameList(vision, "Screen Effects", Names(kPostFx),
			[](const std::string& name) { GRAPHICS::ANIMPOSTFX_PLAY(name.c_str()); },
			[](MenuBase* m) { Ui::Do(m, "Clear all", [] { GRAPHICS::ANIMPOSTFX_STOP_ALL(); }); });

		// SubMoods: facial idle overrides from the ped's own facial dictionary.
		Ui::NameList(self, "Moods", Names(kMoods),
			[](const std::string& mood) { PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(Me(), mood.c_str(), nullptr); },
			[](MenuBase* m) { Ui::Do(m, "Reset", [] { PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(Me()); }); });

		// SubAbilities. Ours: the recharge toggles restore 1.0 when switched off.
		MenuBase* abilities = Ui::Submenu(self, "Abilities");
		Ui::Toggle(abilities, "Health Recharge",
			[](bool on) { if (!on) PLAYER::SET_PLAYER_HEALTH_RECHARGE_MULTIPLIER(MyPlayer(), 1.0f); }, HealthRechargeTick);
		Ui::Number(abilities, "Health Recharge Rate", &g_healthRecharge, 0.0f, 100.0f, 0.5f);
		Ui::Toggle(abilities, "Stamina Recharge",
			[](bool on) { if (!on) PLAYER::SET_PLAYER_STAMINA_RECHARGE_MULTIPLIER(MyPlayer(), 1.0f); }, StaminaRechargeTick);
		Ui::Number(abilities, "Stamina Recharge Rate", &g_staminaRecharge, 0.0f, 100.0f, 0.5f);
		Ui::Section(abilities, "Dead Eye");
		Ui::Toggle(abilities, "Disable Dead Eye", [](bool on) { PLAYER::_ENABLE_CUSTOM_DEADEYE_ABILITY(MyPlayer(), !on); });
		Ui::Looped(abilities, "Unlimited Dead Eye", [] { PLAYER::_SPECIAL_ABILITY_START_RESTORE(MyPlayer(), -1, TRUE); });
		Ui::Number(abilities, "Dead Eye Level", &g_deadEyeLevel, 0, 5, 1, SetDeadEyeLevel, true);
		Ui::Section(abilities, "Eagle Eye");
		Ui::Toggle(abilities, "Disable Eagle Eye", [](bool on) { PLAYER::_ENABLE_EAGLEEYE(MyPlayer(), !on); });
		Ui::Toggle(abilities, "Unlimited Eagle Eye", SetUnlimitedEagleEye);
		Ui::Toggle(abilities, "Eagle Eye Plus", [](bool on) { PLAYER::_EAGLE_EYE_SET_PLUS_FLAG_DISABLED(MyPlayer(), !on); });
		Ui::Choice(abilities, "Eagle Eye Mode", { "Default", "Trail Vision" }, &g_eagleEyeMode, SetEagleEyeMode);

		// SubPlayerProofs. Ours: the bits are re-applied every frame while any
		// is on, so they survive death and model changes.
		MenuBase* proofs = Ui::Submenu(self, "Player Proofs");
		const char* const kProofs[] = { "Bullets", "Flame", "Explosion", "Collision", "Melee", "Steam", "Smoke", "Headshots", "Projectiles" };
		for (int bit = 0; bit < 9; bit++)
			Ui::Toggle(proofs, kProofs[bit], [bit](bool on) { SetProof(bit, on); }, ApplyProofs);

		// SubPlayerConfigFlags. Rampage picks the flag from a named list; ours
		// takes the number (the game has no flag names to source).
		MenuBase* flags = Ui::Submenu(self, "Config Flags");
		Ui::Number(flags, "Flag", &g_configFlag, 0, 700, 1, ReadConfigFlag);
		Ui::Choice(flags, "Status", { "Disabled", "Enabled" }, &g_configStatus,
			[](int status) { PED::SET_PED_CONFIG_FLAG(Me(), g_configFlag, status != 0); });
		flags->SetOnOpen([](MenuBase*) { ReadConfigFlag(); });
	}
}
