/*
	Player > Posse: ports Rampage's Submenus::SubPlayerPosse. Rampage's posse
	is the peds its Ped Spawner adds; ours is the same list (Posse::Add,
	for the spawner) plus Add Aimed Ped and Add Nearest Ped rows (ours) so
	it works before anything is spawned. Members join the player's group
	as bodyguards, which group formations need.

	Rampage runs the commands from bindable hotkeys while Posse Commands
	is on; ours are plain rows (the aimed target is whatever the player is
	aiming at when the row is selected) until Rampagio has hotkeys.
*/

#include "Menus.h"
#include "..\GameUtil.h"

#include <algorithm>
#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }
	Player MyPlayer() { return PLAYER::PLAYER_ID(); }

	constexpr Hash WEAPON_LASSO = 0x7A8A724A;
	constexpr Hash ADD_REASON_DEFAULT = 0x2CD419DC;

	struct EmoteName
	{
		const char* name;
		int type;
	};
	const EmoteName kEmotes[] = {
#include "..\data\Emotes.inc"
	};

	std::vector<Ped> g_members;
	std::string g_name = "My Posse";
	int g_formation = 0;

	int PlayerGroup() { return PLAYER::GET_PLAYER_GROUP(MyPlayer()); }

	void Prune()
	{
		std::erase_if(g_members, [](Ped p) { return !ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p); });
	}

	void ApplyName()
	{
		for (Ped p : g_members)
			PED::_SET_PED_PROMPT_NAME(p, TrFormat("Member ({})", g_name).c_str());
	}

	Entity AimedTarget()
	{
		Entity target = 0;
		if (!PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(MyPlayer(), &target) || !target)
			PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(MyPlayer(), &target, FALSE, FALSE);
		return target && ENTITY::DOES_ENTITY_EXIST(target) ? target : 0;
	}

	Ped ClosestMember(Entity to)
	{
		const Vector3 t = ENTITY::GET_ENTITY_COORDS(to, TRUE, FALSE);
		Ped best = 0;
		float bestDist = 3.4e38f;
		for (Ped p : g_members)
		{
			const float d = GameUtil::DistanceSq(ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE), t);
			if (d < bestDist)
			{
				bestDist = d;
				best = p;
			}
		}
		return best;
	}

	// --- commands ----------------------------------------------------------

	std::string AttackAimed()
	{
		Prune();
		const Entity target = AimedTarget();
		if (!target || !ENTITY::IS_ENTITY_A_PED(target))
			return "Aim at a ped first";
		for (Ped p : g_members)
		{
			TASK::CLEAR_PED_TASKS_IMMEDIATELY(p, FALSE, FALSE);
			TASK::TASK_COMBAT_PED(p, target, 0, 16);
		}
		return {};
	}

	std::string ClosestAttackAimed()
	{
		Prune();
		const Entity target = AimedTarget();
		if (!target || !ENTITY::IS_ENTITY_A_PED(target))
			return "Aim at a ped first";
		const Ped p = ClosestMember(target);
		if (!p)
			return "No members";
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(p, FALSE, FALSE);
		TASK::TASK_COMBAT_PED(p, target, 0, 16);
		return {};
	}

	std::string ClosestLassoAimed()
	{
		Prune();
		const Entity target = AimedTarget();
		if (!target || !ENTITY::IS_ENTITY_A_PED(target))
			return "Aim at a ped first";
		const Ped p = ClosestMember(target);
		if (!p)
			return "No members";
		WEAPON::GIVE_DELAYED_WEAPON_TO_PED(p, WEAPON_LASSO, 200, FALSE, ADD_REASON_DEFAULT);
		WEAPON::SET_PED_AMMO(p, WEAPON_LASSO, 200);
		WEAPON::SET_CURRENT_PED_WEAPON(p, WEAPON_LASSO, TRUE, 0, FALSE, FALSE);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(p, FALSE, FALSE);
		TASK::TASK_LASSO_PED(p, target);
		return {};
	}

	void KeepPosition()
	{
		Prune();
		for (Ped p : g_members)
		{
			TASK::CLEAR_PED_TASKS_IMMEDIATELY(p, FALSE, FALSE);
			TASK::TASK_STAND_STILL(p, -1);
		}
	}

	void ComeToMe()
	{
		Prune();
		for (Ped p : g_members)
		{
			TASK::CLEAR_PED_TASKS_IMMEDIATELY(p, FALSE, FALSE);
			TASK::TASK_GO_TO_ENTITY(p, Me(), -1, 5.0f, 100.0f, 1.0f, 0);
		}
	}

	void RandomEmote()
	{
		Prune();
		for (Ped p : g_members)
		{
			const EmoteName& e = kEmotes[MISC::GET_RANDOM_INT_IN_RANGE(0, static_cast<int>(std::size(kEmotes)))];
			TASK::TASK_PLAY_EMOTE_WITH_HASH(p, e.type, 2, GameUtil::Joaat(e.name), FALSE, FALSE, FALSE, FALSE, FALSE);
		}
	}

	void Cower()
	{
		Prune();
		for (Ped p : g_members)
			TASK::TASK_COWER(p, -1, 0, 0);
	}

	// --- membership (ours) ---------------------------------------------------

	std::string AddPed(Ped p)
	{
		if (!p || !ENTITY::DOES_ENTITY_EXIST(p) || !ENTITY::IS_ENTITY_A_PED(p) || p == Me() || PED::IS_PED_A_PLAYER(p))
			return "No ped";
		if (ENTITY::IS_ENTITY_DEAD(p))
			return "That ped is dead";
		Menus::Posse::Add(p);
		return TrFormat("{} member(s)", g_members.size());
	}

	std::string AddNearest()
	{
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		Ped best = 0;
		float bestDist = 30.0f * 30.0f;
		for (Ped p : GameUtil::AllPeds())
		{
			if (p == Me() || PED::IS_PED_A_PLAYER(p) || !PED::IS_PED_HUMAN(p) || ENTITY::IS_ENTITY_DEAD(p)
				|| std::find(g_members.begin(), g_members.end(), p) != g_members.end())
				continue;
			const float d = GameUtil::DistanceSq(ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE), me);
			if (d < bestDist)
			{
				bestDist = d;
				best = p;
			}
		}
		return best ? AddPed(best) : "No ped within 30m";
	}

	void DismissAll()
	{
		for (Ped p : g_members)
			if (ENTITY::DOES_ENTITY_EXIST(p))
			{
				PED::REMOVE_PED_FROM_GROUP(p);
				ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
			}
		g_members.clear();
	}

	void DeleteAll()
	{
		for (Ped p : g_members)
			if (ENTITY::DOES_ENTITY_EXIST(p))
			{
				if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(p))
					ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, TRUE, TRUE);
				PED::DELETE_PED(&p);
			}
		g_members.clear();
	}

	void TeleportToMe()
	{
		Prune();
		const Vector3 me = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		for (Ped p : g_members)
			ENTITY::SET_ENTITY_COORDS(p, me.x, me.y, me.z, FALSE, FALSE, FALSE, FALSE);
	}
}

namespace Menus::Posse
{
	void Add(Ped ped)
	{
		if (std::find(g_members.begin(), g_members.end(), ped) != g_members.end())
			return;
		if (!ENTITY::_DOES_THREAD_OWN_THIS_ENTITY(ped))
			ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, TRUE, TRUE);
		const int group = PlayerGroup();
		PED::SET_PED_AS_GROUP_MEMBER(ped, group);
		PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, PED::GET_PED_RELATIONSHIP_GROUP_HASH(Me()));
		PED::SET_GROUP_FORMATION(group, g_formation);
		g_members.push_back(ped);
		ApplyName();
	}

	const std::vector<Ped>& Members()
	{
		Prune();
		return g_members;
	}
}

namespace Menus
{
	void BuildPlayerPosse(MenuBase* self)
	{
		MenuBase* posse = Ui::Submenu(self, "Posse");
		Ui::Text(posse, "posse.name", "Name", &g_name, ApplyName);
		posse->AddItem(new MenuItemLabel([] { return TrFormat("Members: {}", Posse::Members().size()); }));
		Ui::Choice(posse, "posse.formation", "Formation", { "Default", "Circle Around Leader", "Alternative Circle", "Line" }, &g_formation,
			[](int f) { PED::SET_GROUP_FORMATION(PlayerGroup(), f); });

		Ui::Section(posse, "Commands");
		Ui::Action(posse, "posse.posseattackaimedtarget", "Posse Attack aimed target", AttackAimed);
		Ui::Action(posse, "posse.closestmemberattackaimedtarget", "Closest Member Attack aimed target", ClosestAttackAimed);
		Ui::Action(posse, "posse.closestmemberlassoaimedtarget", "Closest Member Lasso aimed target", ClosestLassoAimed);
		Ui::Do(posse, "posse.allmemberskeeptheirposition", "All Members keep their position", KeepPosition);
		Ui::Do(posse, "posse.allmemberswillcometoyou", "All Members will come to you", ComeToMe);
		Ui::Do(posse, "posse.allmembersplayarandomemote", "All Members play a random emote", RandomEmote);
		Ui::Do(posse, "posse.allmemberscowerinplace", "All Members cower in place", Cower);

		Ui::Section(posse, "Members");
		Ui::Action(posse, "posse.addaimedped", "Add Aimed Ped", [] { return AddPed(AimedTarget()); });
		Ui::Action(posse, "posse.addnearestped", "Add Nearest Ped", AddNearest);
		Ui::Do(posse, "posse.teleportmemberstome", "Teleport Members to Me", TeleportToMe);
		Ui::Do(posse, "posse.dismissall", "Dismiss All", DismissAll);
		Ui::Do(posse, "posse.deleteall", "Delete All", DeleteAll);
	}
}
