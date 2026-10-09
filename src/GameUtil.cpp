#include "GameUtil.h"
#include "Localization.h"
#include "ScriptFunction.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <iterator>

namespace GameUtil
{
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
		const std::string shown(Tr(title));
		MISC::DISPLAY_ONSCREEN_KEYBOARD(0, shown.c_str(), "", text.c_str(), "", "", "", maxLength);
		int state;
		while ((state = MISC::UPDATE_ONSCREEN_KEYBOARD()) == 0)
			WAIT(0);
		if (state != 1)
			return false;
		const char* result = MISC::GET_ONSCREEN_KEYBOARD_RESULT();
		text = result ? result : "";
		return true;
	}

	Hash ParseHash(std::string_view text)
	{
		if (text.empty())
			return 0;
		// The whole text must be the number, else it's a name.
		const auto number = [](std::string_view digits, int base, Hash& out)
		{
			const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), out, base);
			return ec == std::errc() && end == digits.data() + digits.size();
		};
		Hash value = 0;
		if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
		{
			if (number(text.substr(2), 16, value))
				return value;
		}
		else if (std::isdigit(static_cast<unsigned char>(text[0])))
		{
			if (number(text, 10, value))
				return value;
		}
		return Joaat(text);
	}

	ItemGuid CharacterGuid(int inventoryId)
	{
		ItemGuid root;
		ItemGuid character;
		INVENTORY::INVENTORY_GET_GUID_FROM_ITEMID(inventoryId, root.Ptr(), Joaat("character"), Joaat("SLOTID_NONE"), character.Ptr());
		return character;
	}

	std::string ItemName(Hash item, std::string_view fallback)
	{
		// Rampage takes 3..37 characters as a real name; empty or NULL
		// means the game has no label for the hash.
		const char* text = HUD::GET_STRING_FROM_HASH_KEY(item);
		if (!text)
			return std::string(fallback);
		const std::string_view name(text);
		if (name.size() < 3 || name == "NULL")
			return std::string(fallback);
		return std::string(name);
	}

	bool AddInventoryItem(Hash item, int quantity, std::string& error)
	{
		if (item == 0 || !ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(item, 0))
		{
			error = "Item is invalid";
			return false;
		}

		// Parent: the character, in the first slot the item fits.
		SlotGuid slot;
		const ItemGuid character = CharacterGuid(kInventorySp);
		std::copy(std::begin(character.w), std::end(character.w), slot.w);
		if (INVENTORY::_INVENTORY_FITS_SLOT_ID(item, Joaat("SLOTID_SATCHEL")))
			slot.w[4] = Joaat("SLOTID_SATCHEL");
		else if (INVENTORY::_INVENTORY_FITS_SLOT_ID(item, Joaat("SLOTID_WARDROBE")))
			slot.w[4] = Joaat("SLOTID_WARDROBE");
		else
			slot.w[4] = INVENTORY::_GET_DEFAULT_ITEM_SLOT_INFO(item, Joaat("character"));
		if (!INVENTORY::_INVENTORY_IS_GUID_VALID(slot.Ptr()))
		{
			error = "Couldn't build the slot GUID";
			return false;
		}

		// The item's own GUID within that slot.
		ItemGuid itemGuid;
		INVENTORY::INVENTORY_GET_GUID_FROM_ITEMID(kInventorySp, slot.Ptr(), item, slot.Slot(), itemGuid.Ptr());

		// The reason the game's scripts pass with their own grants.
		constexpr Hash kAddReason = 752097756;
		if (!INVENTORY::_INVENTORY_ADD_ITEM_WITH_GUID(kInventorySp, itemGuid.Ptr(), slot.Ptr(), item, slot.Slot(), quantity, kAddReason))
		{
			error = "The game refused the item";
			return false;
		}
		return true;
	}

	// flow_controller func_290(item, quantity, b2, b3, b4, hReason, i6, i7,
	// eEntity, b9). Position 0x766E.
	namespace
	{
		ScriptFunction g_addItemScript("flow_controller",
			"22 0A 32 00 00 66 00 2F 39 ? ? ? 05 8B 04 00 2F 50 0A 01 66 00 66 01 66 02 66 05 39 ? ? ? 05 8B 04 00 2F 50 0A 01");
	}

	bool AddInventoryItemViaScript(Hash item, int quantity)
	{
		// The reason the game's scripts pass with their own grants.
		constexpr Hash kAddReason = 752097756;
		return g_addItemScript.Call(item, quantity, FALSE, FALSE, FALSE, kAddReason, 0, 0, 0, FALSE);
	}
}
