/*
	Recovery > Collectibles: ports Rampage's Submenus::SubCollectiblesCigaretteCards,
	SubCollectiblesCigaretteCardsSet, SubCollectiblesDinoBones,
	SubCollectiblesDreamcatchers and SubCollectiblesRockCarvings.

	Nothing here is Rampage's data. Cigarette cards, dino bones and rock
	carvings are read from the game's collectable categories at runtime
	(the same natives the rcm_collect_*, dino_bones and rock_carvings
	scripts use); their locations come from
	_COLLECTABLE_GET_PLACEMENT_LOCATION, which rock_carvings reads for its
	own placement. Dreamcatchers aren't a collectable category:
	discoverable_generic_location keeps their coordinates in a script
	function (extracted to Dreamcatchers.inc) and marks each found in a
	bit of Global_40.f_8863.f_148.

	Card models follow the game's s_inv_cigcard_<set>_<NN>x naming (the
	same models Rampage's tables hold).
*/

#include "Menus.h"
#include "..\GameUtil.h"
#include "..\Log.h"

#include <format>

namespace
{
	Ped Me() { return PLAYER::PLAYER_PED_ID(); }

	constexpr Hash kBlipStyleArea = 0x4A60B7C0; // BLIP_STYLE_AREA

	// The entity to move: the mount when riding, so the player comes along.
	Entity Mover()
	{
		if (const Ped mount = GameUtil::PlayerMount())
			return mount;
		return Me();
	}

	std::string TeleportTo(const Vector3& at)
	{
		if (at.x == 0.0f && at.y == 0.0f && at.z == 0.0f)
			return "~COLOR_RED~Error:~s~ The game has no location for this one.";
		STREAMING::REQUEST_COLLISION_AT_COORD(at.x, at.y, at.z);
		ENTITY::SET_ENTITY_COORDS_NO_OFFSET(Mover(), at.x, at.y, at.z + 1.0f, FALSE, FALSE, TRUE);
		return {};
	}

	// A "Show on Map" toggle's blips, one per location.
	struct BlipSet
	{
		std::vector<Blip> blips;

		void Show(const std::vector<Vector3>& locations, const char* name)
		{
			Clear();
			const std::string shown(Tr(name));
			for (const Vector3& at : locations)
			{
				if (at.x == 0.0f && at.y == 0.0f && at.z == 0.0f)
					continue;
				const Blip blip = MAP::BLIP_ADD_FOR_RADIUS(kBlipStyleArea, at.x, at.y, at.z, 5.0f);
				MAP::_SET_BLIP_NAME(blip, shown.c_str());
				blips.push_back(blip);
			}
		}

		void Clear()
		{
			for (Blip& blip : blips)
				if (MAP::DOES_BLIP_EXIST(blip))
					MAP::REMOVE_BLIP(&blip);
			blips.clear();
		}
	};

	// ---- SubCollectiblesDinoBones / SubCollectiblesRockCarvings ----

	struct Collectable
	{
		Hash item;
		Vector3 location;
		bool found; // turned in: the world script won't spawn it again
	};

	std::vector<Collectable> CategoryItems(Hash category)
	{
		std::vector<Collectable> items;
		const int count = COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_COLLECTABLES(category, 0);
		for (int i = 0; i < count; i++)
		{
			const Hash item = COLLECTABLE::_COLLECTABLE_GET_COLLECTABLE_ITEM_HASH(i, category, 0);
			if (item == 0)
				continue;
			items.push_back({ item, COLLECTABLE::_COLLECTABLE_GET_PLACEMENT_LOCATION(item),
				COLLECTABLE::_COLLECTABLE_GET_NUM_TURNED_IN(item) > 0 });
		}
		return items;
	}

	std::string FoundSuffix(bool found) { return found ? std::string(Tr(" ~COLOR_GREEN~(Found)")) : ""; }

	// "Show on Map", then one row per item that teleports to it.
	void BuildCategory(MenuBase* parent, const char* title, const char* category, const char* singular, BlipSet& blips)
	{
		MenuBase* menu = Ui::Submenu(parent, title);
		const Hash hash = GameUtil::Joaat(category);
		Ui::Toggle(menu, std::string("collectibles.") + category + ".showonmap", "Show on Map", [hash, singular, &blips](bool on) {
			if (!on)
				return blips.Clear();
			std::vector<Vector3> locations;
			for (const Collectable& c : CategoryItems(hash))
				locations.push_back(c.location);
			blips.Show(locations, singular);
		});
		Ui::ListMenu(menu, "Locations", [hash, singular](MenuBase* list) {
			int n = 0;
			for (const Collectable& c : CategoryItems(hash))
			{
				const Vector3 at = c.location;
				Ui::Action(list, std::format("{} {}{}", Tr(singular), ++n, FoundSuffix(c.found)), [at] { return TeleportTo(at); });
			}
		});
	}

	BlipSet g_dinoBlips, g_rockBlips, g_dreamcatcherBlips;

	// ---- SubCollectiblesDreamcatchers ----

	const Vector3 kDreamcatchers[] = {
#include "..\data\Dreamcatchers.inc"
	};

	// discoverable_generic_location's func_83/func_85: dreamcatcher i is
	// bit (2 << i) of Global_40.f_8863.f_148.
	constexpr int kDreamcatcherBits = 40 + 8863 + 148;

	bool DreamcatcherFound(int index)
	{
		const UINT64* bits = GameUtil::Global(kDreamcatcherBits);
		return bits && (static_cast<int>(*bits) & (2 << index));
	}

	void BuildDreamcatchers(MenuBase* parent)
	{
		MenuBase* menu = Ui::Submenu(parent, "Dreamcatchers");
		Ui::Toggle(menu, "collectibles.showonmap", "Show on Map", [](bool on) {
			if (!on)
				return g_dreamcatcherBlips.Clear();
			g_dreamcatcherBlips.Show({ std::begin(kDreamcatchers), std::end(kDreamcatchers) }, "Dreamcatcher");
		});
		Ui::ListMenu(menu, "Locations", [](MenuBase* list) {
			for (int i = 0; i < static_cast<int>(std::size(kDreamcatchers)); i++)
			{
				const Vector3 at = kDreamcatchers[i];
				Ui::Action(list, TrFormat("Dreamcatcher {}{}", i + 1, FoundSuffix(DreamcatcherFound(i))), [at] { return TeleportTo(at); });
			}
		});
	}

	// ---- SubCollectiblesCigaretteCards ----

	const Hash kCigaretteCards = GameUtil::Joaat("CIGARETTE_CARDS");
	constexpr int kCardsPerSet = 12;

	struct CardSet
	{
		const char* label;
		const char* subcategory; // the CARD_SET_* the scripts filter by
		const char* model;       // s_inv_cigcard_<model>_<NN>x
	};

	// In Rampage's spawn-row order.
	const CardSet kCardSets[] = {
		{ "Famous Gunslingers",     "CARD_SET_GUNSLINGERS", "gun" },
		{ "Artists and Poets",      "CARD_SET_ARTISTS",     "art" },
		{ "Vistas of America",      "CARD_SET_LANDMARKS",   "lnd" },
		{ "Gems of Beauty",         "CARD_SET_GIRLS",       "grl" },
		{ "Flora of North America", "CARD_SET_PLANTS",      "plt" },
		{ "Stars of the Stage",     "CARD_SET_ACTRESSES",   "act" },
		{ "Fauna of North America", "CARD_SET_ANIMALS",     "aml" },
		{ "Marvels of Travel",      "CARD_SET_VEHICLES",    "veh" },
		{ "World's Champions",      "CARD_SET_SPORTS",      "spt" },
		{ "Amazing Inventions",     "CARD_SET_INVENTIONS",  "inv" },
		{ "Breeds of Horses",       "CARD_SET_HORSES",      "hrs" },
		{ "Prominent Americans",    "CARD_SET_AMERICANS",   "amer" },
	};

	Hash CardModel(const CardSet& set, int card)
	{
		return GameUtil::Joaat(std::format("s_inv_cigcard_{}_{:02}x", set.model, card + 1));
	}

	// A card's inventory item. The scripts get it with this native (their
	// func_990), which alloc8or names as a setter but only returns the
	// item hash.
	Hash CardItem(Hash collectable) { return COLLECTABLE::_COLLECTABLE_SET_ITEM_HASH_DISCOVERED(collectable); }

	int OwnedCount(Hash item)
	{
		if (item == 0 || !ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(item, 0))
			return 0;
		return INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_COUNT_WITH_ITEMID(GameUtil::kInventorySp, item, FALSE);
	}

	// The set's cards, as collectable hashes, in the game's order.
	std::vector<Hash> SetCards(const CardSet& set)
	{
		std::vector<Hash> cards;
		const Hash subcategory = GameUtil::Joaat(set.subcategory);
		const int count = COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_COLLECTABLES(kCigaretteCards, 0);
		for (int i = 0; i < count; i++)
		{
			const Hash card = COLLECTABLE::_COLLECTABLE_GET_COLLECTABLE_ITEM_HASH(i, kCigaretteCards, 0);
			if (card != 0 && COLLECTABLE::_COLLECTABLE_GET_SUBCATEGORY(card) == subcategory)
				cards.push_back(card);
		}
		return cards;
	}

	std::string GiveCard(Hash item)
	{
		std::string error;
		if (!GameUtil::AddInventoryItem(item, 1, error))
			return TrFormat("~COLOR_RED~Error:~s~ {}", Tr(error));
		return TrFormat("Added {}", GameUtil::ItemName(item, std::format("{:#x}", item)));
	}

	// SubCollectiblesCigaretteCardsSet: one row per card, adding it to the
	// inventory (Rampage's default path; its Shift path through
	// flow_controller is Recovery > Add Items > Give Items' "Game Script").
	void BuildCardSet(MenuBase* list, const CardSet& set)
	{
		// Ours: the whole set at once, skipping cards already owned.
		Ui::Action(list, "Give Missing Cards", [&set] {
			int added = 0;
			for (Hash card : SetCards(set))
			{
				const Hash item = CardItem(card);
				std::string error;
				if (OwnedCount(item) == 0 && GameUtil::AddInventoryItem(item, 1, error))
					added++;
			}
			return TrFormat("Added {} cards", added);
		});
		int n = 0;
		for (Hash card : SetCards(set))
		{
			const Hash item = CardItem(card);
			const int owned = OwnedCount(item);
			const std::string name = GameUtil::ItemName(item, TrFormat("Card {}", ++n));
			Ui::Action(list, owned > 0 ? std::format("{} ~COLOR_GREEN~({})", name, owned) : name, [item] { return GiveCard(item); });
		}
	}

	// Spawns the set on a chest 5 m ahead, as Rampage does: the chest is
	// placed on the ground and the cards scattered on its lid.
	std::string SpawnCardSet(const CardSet& set)
	{
		const Hash chest = GameUtil::Joaat("p_chest03x");
		if (!GameUtil::LoadModel(chest))
			return "~COLOR_RED~Error:~s~ Couldn't load the chest model.";
		const Vector3 ahead = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(Me(), 0.0f, 5.0f, 0.0f);
		const Object table = OBJECT::CREATE_OBJECT_NO_OFFSET(chest, ahead.x, ahead.y, ahead.z, FALSE, FALSE, TRUE, FALSE);
		OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(table, FALSE);
		const Vector3 top = ENTITY::GET_ENTITY_COORDS(table, FALSE, FALSE);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(chest);
		int spawned = 0;
		for (int i = 0; i < kCardsPerSet; i++)
		{
			const Hash model = CardModel(set, i);
			if (!GameUtil::LoadModel(model))
				continue;
			const float x = top.x + MISC::GET_RANDOM_FLOAT_IN_RANGE(-0.27f, 0.27f);
			const float y = top.y + MISC::GET_RANDOM_FLOAT_IN_RANGE(-0.1f, 0.1f);
			const Object card = OBJECT::CREATE_OBJECT_NO_OFFSET(model, x, y, top.z + 0.23f, FALSE, FALSE, TRUE, FALSE);
			OBJECT::SET_OBJECT_TARGETTABLE(card, TRUE);
			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			spawned++;
		}
		return TrFormat("Spawned {} cards", spawned);
	}

	// Auto Collect All, as Rampage does it: each card spawns as a
	// carriable 1 m ahead, the player is tasked to pick it up (the game's
	// own pickup grants the card), then put back where they started. Ours
	// runs as a per-frame job rather than blocking the menu for the
	// ~7 minutes 144 cards take; switching the row off stops it.
	struct AutoCollect
	{
		enum class Step { Spawn, PickUp, Return };

		std::vector<Hash> models;
		size_t next = 0;
		Step step = Step::Spawn;
		int wakeAt = 0;
		Object card = 0;
		Vector3 origin{};
		Rampagio::BoolCommand* toggle = nullptr;

		void Start()
		{
			models.clear();
			for (const CardSet& set : kCardSets)
				for (int i = 0; i < kCardsPerSet; i++)
					models.push_back(CardModel(set, i));
			next = 0;
			step = Step::Spawn;
			origin = ENTITY::GET_ENTITY_COORDS(Me(), TRUE, FALSE);
		}

		void Tick()
		{
			const int now = MISC::GET_GAME_TIMER();
			if (now < wakeAt)
				return;
			const Ped me = Me();
			switch (step)
			{
			case Step::Spawn:
			{
				if (next >= models.size())
				{
					Log::Write("Auto Collect All: done, {} card models", models.size());
					toggle->SetState(false);
					return;
				}
				const Hash model = models[next];
				if (!GameUtil::LoadModel(model))
				{
					Log::Write("Auto Collect All: model {:#x} didn't load", model);
					next++;
					return;
				}
				const Vector3 at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(me, 0.0f, 1.0f, 0.0f);
				card = OBJECT::CREATE_OBJECT(model, at.x, at.y, at.z, FALSE, FALSE, FALSE, FALSE, FALSE);
				TASK::_MAKE_OBJECT_CARRIABLE(card);
				GRAPHICS::SET_PICKUP_LIGHT(card, TRUE);
				ENTITY::FREEZE_ENTITY_POSITION(card, FALSE);
				step = Step::PickUp;
				wakeAt = now + 600;
				return;
			}
			case Step::PickUp:
				TASK::TASK_PICKUP_CARRIABLE_ENTITY(me, card);
				STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(models[next]);
				step = Step::Return;
				wakeAt = now + 2500;
				return;
			case Step::Return:
				ENTITY::SET_ENTITY_COORDS_NO_OFFSET(me, origin.x, origin.y, origin.z, TRUE, FALSE, TRUE);
				next++;
				step = Step::Spawn;
				return;
			}
		}
	};
	AutoCollect g_autoCollect;

	void BuildCigaretteCards(MenuBase* parent)
	{
		MenuBase* menu = Ui::Submenu(parent, "Cigarette Cards");
		for (const CardSet& set : kCardSets)
			Ui::ListMenu(menu, set.label, [&set](MenuBase* list) { BuildCardSet(list, set); });
		Ui::Section(menu, "Spawn Card Sets");
		g_autoCollect.toggle = Ui::Toggle(menu, "collectibles.autocollectall", "Auto Collect All",
			[](bool on) { if (on) g_autoCollect.Start(); },
			[] { g_autoCollect.Tick(); });
		g_autoCollect.toggle->SetTransient(); // a run, not a setting
		for (const CardSet& set : kCardSets)
			Ui::Action(menu, Ui::Id("collectibles.spawncards", set.label), set.label, [&set] { return SpawnCardSet(set); });
	}
}

namespace Menus
{
	void BuildRecoveryCollectibles(MenuBase* recovery)
	{
		MenuBase* collectibles = Ui::Submenu(recovery, "Collectibles");
		BuildCigaretteCards(collectibles);
		BuildCategory(collectibles, "Dino Bones", "dino_bones", "Dino Bone", g_dinoBlips);
		BuildDreamcatchers(collectibles);
		BuildCategory(collectibles, "Rock Carvings", "rock_carvings", "Rock Carving", g_rockBlips);
	}
}
