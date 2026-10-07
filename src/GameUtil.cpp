#include "GameUtil.h"

namespace GameUtil
{
	bool IsOnline()
	{
		return NETWORK::NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(rage::Joaat("net_main_online"), -1, FALSE, 0) != FALSE;
	}

	Ped PlayerMount()
	{
		const Ped ped = PLAYER::PLAYER_PED_ID();
		return PED::IS_PED_ON_MOUNT(ped) ? PED::GET_MOUNT(ped) : 0;
	}

	bool TeleportToGround(Entity entity, float x, float y)
	{
		// Collision far from the player isn't loaded yet, so the ground
		// probe fails until it streams in: park the entity high above the
		// target, request collision, and retry from the top down for ~2s.
		constexpr float kProbeHeights[] = { 1000.0f, 800.0f, 600.0f, 400.0f, 300.0f, 200.0f, 150.0f, 100.0f, 50.0f, 25.0f, 0.0f };
		for (int attempt = 0; attempt < 20; attempt++)
		{
			for (float height : kProbeHeights)
			{
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, x, y, height, FALSE, FALSE, FALSE);
				STREAMING::REQUEST_COLLISION_AT_COORD(x, y, height);
				float groundZ = 0.0f;
				if (MISC::GET_GROUND_Z_FOR_3D_COORD(x, y, height, &groundZ, FALSE))
				{
					ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, x, y, groundZ + 1.0f, FALSE, FALSE, FALSE);
					return true;
				}
			}
			WAIT(100);
		}
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, x, y, 1000.0f, FALSE, FALSE, FALSE);
		return false;
	}

	namespace
	{
		template <typename T>
		std::vector<T> Pool(int (*walk)(int*, int))
		{
			std::vector<int> buf(1024);
			const int n = walk(buf.data(), static_cast<int>(buf.size()));
			buf.resize(n > 0 ? n : 0);
			return std::vector<T>(buf.begin(), buf.end());
		}
	}

	std::vector<Ped> AllPeds() { return Pool<Ped>(worldGetAllPeds); }
	std::vector<Vehicle> AllVehicles() { return Pool<Vehicle>(worldGetAllVehicles); }
	std::vector<Object> AllObjects() { return Pool<Object>(worldGetAllObjects); }

	float DistanceSq(const Vector3& a, const Vector3& b)
	{
		const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
		return dx * dx + dy * dy + dz * dz;
	}

	std::vector<Entity> Nearby(const std::vector<Entity>& entities, const Vector3& center, float radius)
	{
		std::vector<Entity> result;
		for (Entity e : entities)
			if (ENTITY::DOES_ENTITY_EXIST(e) && DistanceSq(ENTITY::GET_ENTITY_COORDS(e, TRUE, FALSE), center) < radius * radius)
				result.push_back(e);
		return result;
	}

	UINT64* Global(int index)
	{
		return getGlobalPtr(index);
	}

	bool LoadModel(Hash model)
	{
		if (!STREAMING::IS_MODEL_VALID(model))
			return false;
		STREAMING::REQUEST_MODEL(model, FALSE);
		for (int i = 0; i < 200 && !STREAMING::HAS_MODEL_LOADED(model); i++)
			WAIT(10);
		return STREAMING::HAS_MODEL_LOADED(model) != FALSE;
	}

	bool LoadAnimDict(const char* dict)
	{
		if (!STREAMING::DOES_ANIM_DICT_EXIST(dict))
			return false;
		STREAMING::REQUEST_ANIM_DICT(dict);
		for (int i = 0; i < 200 && !STREAMING::HAS_ANIM_DICT_LOADED(dict); i++)
			WAIT(10);
		return STREAMING::HAS_ANIM_DICT_LOADED(dict) != FALSE;
	}

	Ped PlayerHorse()
	{
		const Ped ped = PLAYER::PLAYER_PED_ID();
		if (PED::IS_PED_ON_MOUNT(ped))
			return PED::GET_MOUNT(ped);
		const Ped last = PED::_GET_LAST_MOUNT(ped);
		if (last && ENTITY::DOES_ENTITY_EXIST(last))
			return last;
		const Ped saddle = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
		return saddle && ENTITY::DOES_ENTITY_EXIST(saddle) ? saddle : 0;
	}

	bool PromptText(const char* title, std::string& text, int maxLength)
	{
		MISC::DISPLAY_ONSCREEN_KEYBOARD(0, title, "", text.c_str(), "", "", "", maxLength);
		int state;
		while ((state = MISC::UPDATE_ONSCREEN_KEYBOARD()) == 0)
			WAIT(0);
		if (state != 1)
			return false;
		const char* result = MISC::GET_ONSCREEN_KEYBOARD_RESULT();
		text = result ? result : "";
		return true;
	}
}
