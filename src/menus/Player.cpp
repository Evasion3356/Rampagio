/*
	Player menu: ports Rampage's Submenus::SubSelf rows. Our own code; the
	native choices and constants follow what Rampage's handlers do (see the
	notes on each), with improvements marked "ours".
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\keyboard.h"

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Player MyPlayer() { return PLAYER::PLAYER_ID(); }

	// Rampage controls (hashes of the game's input actions).
	constexpr Hash INPUT_JUMP = 0x6DB8C62F; // read from control group 2, as Rampage's fly mode does
	constexpr Hash INPUT_SPRINT = 0x8FFC75D6;
	constexpr Hash INPUT_MOVE_UP_ONLY = 0x8FD015D8;

	// Rampage's force calls all use these trailing flags.
	void Push(Entity e, float x, float y, float z, int forceType = 1)
	{
		ENTITY::APPLY_FORCE_TO_ENTITY(e, forceType, x, y, z, 0.0f, 0.0f, 0.0f, 0, TRUE, TRUE, TRUE, FALSE, TRUE);
	}

	// --- toggles --------------------------------------------------------

	void SetGodmode(bool on)
	{
		PLAYER::SET_PLAYER_INVINCIBLE(MyPlayer(), on);
		ENTITY::SET_ENTITY_INVINCIBLE(Me(), on);
	}

	// Rampage heals only after damage from a ped or vehicle; ours heals any
	// missing health (falls, fire, animals).
	void AutoHealTick()
	{
		const Ped ped = Me();
		const int max = ENTITY::GET_ENTITY_MAX_HEALTH(ped, FALSE);
		if (ENTITY::GET_ENTITY_HEALTH(ped) < max && !ENTITY::IS_ENTITY_DEAD(ped))
			ENTITY::SET_ENTITY_HEALTH(ped, max, 0);
	}

	void SetGhost(bool on) { ENTITY::SET_ENTITY_VISIBLE(Me(), !on); }

	void InfiniteStaminaTick() { PLAYER::RESTORE_PLAYER_STAMINA(MyPlayer(), 100.0f); }

	void ClearWanted()
	{
		const Player p = MyPlayer();
		LAW::SET_BOUNTY(p, 0);
		LAW::CLEAR_WANTED_SCORE(p);
		LAW::_SET_BOUNTY_HUNTER_PURSUIT_CLEARED();
	}

	void NeverWantedTick()
	{
		ClearWanted();
		PLAYER::SET_WANTED_LEVEL_MULTIPLIER(0.0f);
	}

	void NeverWantedOff() { PLAYER::SET_WANTED_LEVEL_MULTIPLIER(1.0f); }

	void SetEveryoneIgnore(bool on) { PLAYER::SET_EVERYONE_IGNORE_PLAYER(MyPlayer(), on); }

	void EveryoneIgnoreTick()
	{
		PLAYER::SET_EVERYONE_IGNORE_PLAYER(MyPlayer(), TRUE);
		EVENT::SUPPRESS_SHOCKING_EVENTS_NEXT_FRAME();
		EVENT::REMOVE_ALL_SHOCKING_EVENTS(TRUE);
	}

	void SetAnimalsIgnore(bool on) { PED::_SET_PED_ANIMAL_DETECTION_MODIFIER(Me(), on ? 0.0f : 1.0f); }

	void SetLawIgnore(bool on) { LAW::_SET_LAW_DISABLED(on); }

	void InfiniteSwimTick()
	{
		const Ped ped = Me();
		if (ENTITY::IS_ENTITY_IN_WATER(ped))
		{
			ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped, 1, 100);
			PED::_RESTORE_PED_STAMINA(ped, 100.0f);
		}
	}

	void SetNoNoise(bool on)
	{
		const float m = on ? 0.0f : 1.0f;
		PLAYER::SET_PLAYER_NOISE_MULTIPLIER(MyPlayer(), m);
		PLAYER::SET_PLAYER_SNEAKING_NOISE_MULTIPLIER(MyPlayer(), m);
	}

	void SuperJumpTick()
	{
		if (!INTERIOR::IS_INTERIOR_SCENE())
			MISC::SET_SUPER_JUMP_THIS_FRAME(MyPlayer());
	}

	void UltraJumpTick()
	{
		if (INTERIOR::IS_INTERIOR_SCENE())
			return;
		MISC::SET_SUPER_JUMP_THIS_FRAME(MyPlayer());
		if (PED::IS_PED_JUMPING(Me()))
			Push(Me(), 0.0f, 0.0f, 1.0f);
	}

	float g_scale = 1.0f;
	void ApplyScale() { PED::_SET_PED_SCALE(Me(), g_scale); }

	void SetNoRagdoll(bool on)
	{
		const Ped ped = Me();
		PED::SET_PED_CAN_RAGDOLL(ped, !on);
		PED::SET_PED_CAN_RAGDOLL_FROM_PLAYER_IMPACT(ped, !on);
		PED::SET_PED_RAGDOLL_ON_COLLISION(ped, !on, FALSE);
		PED::SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE(ped, on ? 1 : 0);
	}
	bool g_noRagdoll = false;

	void SuperRunTick()
	{
		const Ped ped = Me();
		if (ENTITY::IS_ENTITY_IN_AIR(ped, 0))
			return;
		if (PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_SPRINT))
			Push(ped, 0.0f, 30.0f, 0.0f);
		else if (PAD::IS_DISABLED_CONTROL_JUST_RELEASED(0, INPUT_SPRINT))
			ENTITY::SET_ENTITY_VELOCITY(ped, 0.0f, 0.0f, 0.0f);
	}

	// Rampage pushes harder the faster the gait: 6 walking, 10 running,
	// 14 sprinting, through the ped's pelvis ragdoll bone.
	void PassiveRunTick()
	{
		const Ped ped = Me();
		if (ENTITY::IS_ENTITY_IN_AIR(ped, 0) || PED::IS_PED_JUMPING(ped))
			return;
		float force = 0.0f;
		if (TASK::IS_PED_SPRINTING(ped))
			force = 14.0f;
		else if (TASK::IS_PED_RUNNING(ped))
			force = 10.0f;
		else if (TASK::IS_PED_WALKING(ped))
			force = 6.0f;
		if (force > 0.0f)
			ENTITY::APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS(ped, 1, 0.0f, force, 0.0f,
				PED::_GET_PED_RAGDOLL_BONE_INDEX(ped, 0x6928), TRUE, TRUE, FALSE);
	}

	int g_opacity = 4;
	void ApplyOpacity(int index)
	{
		constexpr int kAlpha[] = { 0, 50, 120, 160, 255 };
		ENTITY::SET_ENTITY_ALPHA(Me(), kAlpha[index], FALSE);
	}

	// Task 628 is the skinning/pickup interaction; clearing it skips the
	// animation, except while fishing_core runs (it uses the same task).
	void QuickSkinTick()
	{
		const Ped ped = Me();
		if (TASK::GET_IS_TASK_ACTIVE(ped, 628)
			&& SCRIPT::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(GameUtil::Joaat("fishing_core")) == 0)
			TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, TRUE, TRUE);
	}

	// Fly mode: SPACE (or jump) lifts, W (or forward) pushes ahead. The ped is
	// invincible and can't ragdoll while it's on, like in Rampage.
	bool g_godmode = false;
	void FlyTick()
	{
		const Ped ped = Me();
		ENTITY::SET_ENTITY_INVINCIBLE(ped, TRUE);
		SetNoRagdoll(true);
		const bool up = IsKeyDown(VK_SPACE) || PAD::IS_DISABLED_CONTROL_PRESSED(2, INPUT_JUMP);
		const bool forward = IsKeyDown('W') || PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_MOVE_UP_ONLY);
		if (up)
			Push(ped, 0.0f, 0.0f, 9.0f);
		if (up || forward)
			Push(ped, 0.0f, 9.0f, 0.0f);
	}

	void FlyOff()
	{
		if (!g_godmode)
			SetGodmode(false);
		if (!g_noRagdoll)
			SetNoRagdoll(false);
	}

	void SetNoCollision(bool on) { ENTITY::SET_ENTITY_COLLISION(Me(), !on, TRUE); }

	void ClipTick() { PED::SET_PED_CAPSULE(Me(), 1.0e-38f); }
	void ClipOff() { PED::SET_PED_CAPSULE(Me(), 1.0f); }

	// Pushes every ped and vehicle within 12m straight away from the player.
	void ForcefieldTick()
	{
		const Ped ped = Me();
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(ped, TRUE, FALSE);
		const Vehicle mine = PED::GET_VEHICLE_PED_IS_IN(ped, TRUE);
		const Ped mount = GameUtil::PlayerMount();
		std::vector<Entity> all = GameUtil::AllPeds();
		for (Vehicle v : GameUtil::AllVehicles())
			all.push_back(v);
		for (Entity e : GameUtil::Nearby(all, me, 12.0f))
		{
			if (e == ped || e == mine || e == mount)
				continue;
			const Vector3 p = ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE);
			ENTITY::APPLY_FORCE_TO_ENTITY(e, 3, p.x - me.x, p.y - me.y, p.z - me.z, 0.0f, 0.0f, 0.0f, 0, FALSE, TRUE, FALSE, TRUE, FALSE);
		}
	}

	void FillCores(Ped ped)
	{
		for (int core = 0; core < 3; core++)
			ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped, core, 100);
	}

	void CoresNeverDrainTick() { FillCores(Me()); }

	// Overpowered (gold) cores and attributes: 900 attribute overpower and
	// 100 core overpower on health, stamina and Dead Eye.
	void SetOverpower(bool sound)
	{
		const Ped ped = Me();
		for (int i = 0; i < 3; i++)
		{
			ATTRIBUTE::ENABLE_ATTRIBUTE_OVERPOWER(ped, i, 900.0f, sound);
			ATTRIBUTE::_ENABLE_ATTRIBUTE_CORE_OVERPOWER(ped, i, 100.0f, sound);
		}
	}

	void CleanPed()
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

	// Drunkenness is script global 1935436: slot 9 holds the level, slot 1
	// a timer Rampage zeroes so the level doesn't wear off.
	float g_drunk = 0.74f;
	void DrunkTick()
	{
		if (SCRIPT::IS_LOADING_SCREEN_VISIBLE())
			return;
		if (UINT64* g = GameUtil::Global(1935436))
		{
			*reinterpret_cast<float*>(g + 9) = g_drunk;
			*reinterpret_cast<int*>(g + 1) = 0;
		}
	}

	void DrunkOff()
	{
		if (UINT64* g = GameUtil::Global(1935436))
			*reinterpret_cast<float*>(g + 9) = 0.0f;
	}

	// --- actions --------------------------------------------------------

	void EndPursuit()
	{
		const Player p = MyPlayer();
		PLAYER::_SET_MAX_WANTED_LEVEL_2(-1);
		LAW::CLEAR_WANTED_SCORE(p);
		LAW::_SET_BOUNTY_HUNTER_PURSUIT_CLEARED();
		LAW::SET_BOUNTY(p, 0);
		LAW::SET_WANTED_SCORE(p, 0);
	}

	void FillPlayerCores()
	{
		const Ped ped = Me();
		FillCores(ped);
		PLAYER::RESTORE_PLAYER_STAMINA(MyPlayer(), 100.0f);
		PLAYER::_SPECIAL_ABILITY_START_RESTORE(MyPlayer(), -1, TRUE);
		ENTITY::SET_ENTITY_HEALTH(ped, ENTITY::GET_ENTITY_MAX_HEALTH(ped, FALSE), 0);
	}

	void Heal()
	{
		const Ped ped = Me();
		ENTITY::SET_ENTITY_HEALTH(ped, ENTITY::GET_ENTITY_MAX_HEALTH(ped, FALSE), 0);
	}

	void StartScenarioHere(const char* scenario)
	{
		const Ped ped = Me();
		const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, TRUE, FALSE);
		TASK::TASK_START_SCENARIO_AT_POSITION(ped, GameUtil::Joaat(scenario), p.x, p.y, p.z, ENTITY::GET_ENTITY_HEADING(ped), -1, FALSE, FALSE, "", 0.0f, FALSE);
	}

	void ItemInteraction(Hash item, Hash anim)
	{
		TASK::START_TASK_ITEM_INTERACTION(Me(), item, anim, 1, 0, 0.0f);
	}

	void Ragdoll()
	{
		if (g_noRagdoll)
			SetNoRagdoll(false);
		PED::SET_PED_TO_RAGDOLL(Me(), 2000, 2000, 0, TRUE, TRUE, "DraggedByCart");
	}
}

namespace Menus
{
	void BuildPlayer(MenuBase* root)
	{
		MenuBase* self = Ui::Submenu(root, "Player");

		Ui::Section(self, "Toggles");
		Ui::Toggle(self, "Godmode", [](bool on) { g_godmode = on; SetGodmode(on); }, [] { SetGodmode(true); });
		Ui::Looped(self, "Auto Heal", AutoHealTick);
		Ui::Toggle(self, "Ghostmode", SetGhost);
		Ui::Looped(self, "Infinite Stamina", InfiniteStaminaTick);
		Ui::Looped(self, "Never Wanted", NeverWantedTick, NeverWantedOff);
		Ui::Do(self, "End Pursuit", EndPursuit);
		Ui::Toggle(self, "Everyone Ignore", SetEveryoneIgnore, EveryoneIgnoreTick);
		Ui::Toggle(self, "Wild Animals Ignore", SetAnimalsIgnore);
		Ui::Toggle(self, "Law Ignore", SetLawIgnore);
		Ui::Looped(self, "Infinite Swim", InfiniteSwimTick);
		Ui::Toggle(self, "No Noise", SetNoNoise);
		Ui::Looped(self, "Super Jump", SuperJumpTick);
		Ui::Looped(self, "Ultra Jump", UltraJumpTick);
		Ui::Number(self, "Player Scale", &g_scale, 0.1f, 10.0f, 0.05f, ApplyScale);
		Ui::Toggle(self, "No Ragdoll", [](bool on) { g_noRagdoll = on; SetNoRagdoll(on); });
		Ui::Looped(self, "Super Run", SuperRunTick);
		Ui::Looped(self, "Passive Run", PassiveRunTick);
		Ui::Choice(self, "Player Opacity", { "0%", "25%", "50%", "75%", "100%" }, &g_opacity, ApplyOpacity);
		Ui::Looped(self, "Quick Skin", QuickSkinTick);
		Ui::Looped(self, "Fly Mode", FlyTick, FlyOff);
		Ui::Toggle(self, "No Collision", SetNoCollision);
		Ui::Looped(self, "Clip Through Objects", ClipTick, ClipOff);
		Ui::Looped(self, "Forcefield", ForcefieldTick);
		Ui::Looped(self, "Cores Never Drain", CoresNeverDrainTick);
		Ui::Looped(self, "Cores Overpower", [] { SetOverpower(false); });
		Ui::Looped(self, "Clean Ped", CleanPed);
		Ui::Looped(self, "Drunk Mode", DrunkTick, DrunkOff);
		Ui::Number(self, "Drunk Level", &g_drunk, 0.0f, 1.0f, 0.01f);

		Ui::Section(self, "Actions");
		Ui::Do(self, "Boost Cores", [] { SetOverpower(true); });
		Ui::Do(self, "Fill Player Cores", FillPlayerCores);
		Ui::Do(self, "Heal", Heal);
		Ui::Do(self, "Clean", CleanPed);
		Ui::Do(self, "Clone Player", [] { PED::CLONE_PED(Me(), TRUE, TRUE, TRUE); });
		Ui::Do(self, "Drink a Beer", [] { StartScenarioHere("WORLD_HUMAN_BOTTLE_PICKUP_BOX_TABLE_BEER"); });
		Ui::Do(self, "Drink a Whiskey", [] { ItemInteraction(0xEA0C25E5, 0xB434CA67); });
		Ui::Do(self, "Quick Health Cure", [] { ItemInteraction(0xBF9282E5, 0x685B9702); });
		Ui::Do(self, "Ragdoll", Ragdoll);
		Ui::Do(self, "Hard Fall", [] { TASK::SET_HIGH_FALL_TASK(Me(), 3000, 4000, 0); });
		Ui::Do(self, "Get Wanted", [] { LAW::_FORCE_LAW_ON_LOCAL_PLAYER_IMMEDIATELY(); });
		Ui::Do(self, "Suicide", [] { ENTITY::SET_ENTITY_HEALTH(Me(), 0, 0); });
	}
}
