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

	Ours (docs/COLLECTIBLES_AND_ITEMS_PLAN.md, step A): "found" is
	_COLLECTABLE_GET_NUM_FOUND, as the world scripts decide it (Rampage's
	lists don't check); Show on Map re-reads it every few seconds and Hide
	Found leaves found ones off the map. The same category code adds
	legendary fish, gator eggs, Carolina parakeets, wilderness chests,
	treasure and herbs, and Legendary Animals lists the kill state the
	hunting zones keep (Legendaries.h).

	Card models follow the game's s_inv_cigcard_<set>_<NN>x naming (the
	same models Rampage's tables hold).

	Ours: Complete Set / Complete All Sets follow the game's own card
	tracking (from ..\CigCardTest and the 1491.50 scripts). A card counts
	as collected in three places: its inventory item, the collectable's
	"found" counter, and, on the set's twelfth card, the set document
	(DOCUMENT_CIG_CARD_*_SET) the player mails to the collector.
	flow_controller func_290 (the game's add-item function) does all three
	and updates the journal, so cards go through it. shop_post_office's
	journal objectives (func_1335) count a set as done when its document is
	held or its bit is set in the posted-sets mask.
*/

#include "Menus.h"
#include "Legendaries.h"
#include "..\GameUtil.h"
#include "..\Log.h"

#include <algorithm>
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

	// Ours: Recovery > Collectibles > Hide Found. Show on Map leaves out what
	// the player already has (Rampage blips everything); the Locations lists
	// keep every row, marked (Found).
	bool g_hideFound = true;

	// A location and whether the game counts it as found.
	struct Spot
	{
		Vector3 at;
		bool found;
	};

	bool HasLocation(const Vector3& at) { return at.x != 0.0f || at.y != 0.0f || at.z != 0.0f; }

	// A "Show on Map" toggle's blips, one per location. Ours: re-reads the
	// found state every few seconds while on, so picking one up removes its
	// blip without switching the toggle off and on.
	struct BlipSet
	{
		static constexpr int kRefreshMs = 3000;

		std::vector<Blip> blips;
		// Per spot, as last blipped: 0 none, 1 a blip, 2 a found one's blip.
		std::vector<int> shown;
		int nextRefresh = 0;

		// Show on Map's onChange.
		void Set(bool on, const std::function<std::vector<Spot>()>& source, const char* name)
		{
			if (!on)
				return Clear();
			Refresh(source, name, true);
		}

		// Show on Map's onTick.
		void Refresh(const std::function<std::vector<Spot>()>& source, const char* name, bool force = false)
		{
			const int now = MISC::GET_GAME_TIMER();
			if (!force && now < nextRefresh)
				return;
			nextRefresh = now + kRefreshMs;

			const std::vector<Spot> spots = source();
			std::vector<int> wanted;
			wanted.reserve(spots.size());
			for (const Spot& spot : spots)
				wanted.push_back(!HasLocation(spot.at) || (g_hideFound && spot.found) ? 0 : (spot.found ? 2 : 1));
			if (!force && wanted == shown)
				return;

			Clear();
			const std::string plain(Tr(name));
			const std::string found = TrFormat("{} (Found)", Tr(name));
			for (size_t i = 0; i < spots.size(); i++)
			{
				if (wanted[i] == 0)
					continue;
				const Vector3& at = spots[i].at;
				const Blip blip = MAP::BLIP_ADD_FOR_RADIUS(kBlipStyleArea, at.x, at.y, at.z, 5.0f);
				MAP::_SET_BLIP_NAME(blip, (wanted[i] == 2 ? found : plain).c_str());
				blips.push_back(blip);
			}
			shown = std::move(wanted);
		}

		void Clear()
		{
			for (Blip& blip : blips)
				if (MAP::DOES_BLIP_EXIST(blip))
					MAP::REMOVE_BLIP(&blip);
			blips.clear();
			shown.clear();
		}
	};

	std::string FoundSuffix(bool found) { return found ? std::string(Tr(" ~COLOR_GREEN~(Found)")) : ""; }

	// ---- SubCollectiblesDinoBones / SubCollectiblesRockCarvings ----
	// Ours: the same code covers the game's other collectable categories
	// (legendary fish, gator eggs, Carolina parakeets, wilderness chests,
	// treasure, herbs).

	struct Collectable
	{
		Hash item;
		Hash subcategory;
		Vector3 location;
		bool found;
	};

	// Found, the way the world scripts decide whether to spawn it again
	// (dino_bones func_16, rock_carvings): NUM_FOUND. TURNED_IN is the later
	// hand-in, so it counts too, but a found item isn't always turned in.
	bool CollectableFound(Hash item)
	{
		return COLLECTABLE::_COLLECTABLE_GET_NUM_FOUND(item) > 0 || COLLECTABLE::_COLLECTABLE_GET_NUM_TURNED_IN(item) > 0;
	}

	std::vector<Collectable> CategoryItems(Hash category)
	{
		std::vector<Collectable> items;
		const int count = COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_COLLECTABLES(category, 0);
		for (int i = 0; i < count; i++)
		{
			const Hash item = COLLECTABLE::_COLLECTABLE_GET_COLLECTABLE_ITEM_HASH(i, category, 0);
			if (item == 0)
				continue;
			items.push_back({ item, COLLECTABLE::_COLLECTABLE_GET_SUBCATEGORY(item),
				COLLECTABLE::_COLLECTABLE_GET_PLACEMENT_LOCATION(item), CollectableFound(item) });
		}
		return items;
	}

	// How a category's Locations rows are named.
	enum class Naming
	{
		Numbered,    // "<singular> <n>"
		Item,        // the collectable's own text label, else numbered
		Subcategory, // the subcategory's text label plus a number (herbs)
	};

	struct Category
	{
		const char* title;
		const char* id;        // collectibles.<id>.showonmap (never renamed)
		const char* singular;
		Hash hashes[2];        // the category; a second candidate where unsure
		Naming naming = Naming::Numbered;
		bool tracksFound = true; // false for things that respawn (herbs)
		BlipSet blips;

		// The first candidate the game has items for.
		Hash Resolve() const
		{
			for (Hash hash : hashes)
				if (hash != 0 && COLLECTABLE::_COLLECTABLE_CATEGORY_GET_NUM_COLLECTABLES(hash, 0) > 0)
					return hash;
			return hashes[0];
		}

		std::vector<Spot> Spots() const
		{
			std::vector<Spot> spots;
			for (const Collectable& c : CategoryItems(Resolve()))
				spots.push_back({ c.location, tracksFound && c.found });
			return spots;
		}
	};

	// Category hashes from the 1491.50 scripts (docs/COLLECTIBLES_AND_ITEMS_PLAN.md).
	// Only dino bones and rock carvings are known to have placement
	// locations; the others are untested (rows without one say so).
	Category g_categories[] = {
		{ "Dino Bones", "dino_bones", "Dino Bone", { GameUtil::Joaat("dino_bones") } },
		{ "Rock Carvings", "rock_carvings", "Rock Carving", { GameUtil::Joaat("rock_carvings") } },
		// rare_fish.ysc, rcm_collect_rare_fish1.ysc; the category's name is unknown.
		{ "Legendary Fish", "legendary_fish", "Legendary Fish", { 0xC7EEA672 }, Naming::Item },
		// gator_eggs.ysc: joaat("gator_eggs"), or 689918374 (joaat("gator_egg_nest")).
		{ "Gator Eggs", "gator_eggs", "Gator Egg", { GameUtil::Joaat("gator_eggs"), 0x291F51A6 } },
		{ "Carolina Parakeets", "carolina_parakeets", "Carolina Parakeet", { GameUtil::Joaat("carolina_parakeets") } },
		// wilderness_chest.ysc's -1129417850.
		{ "Wilderness Chests", "wilderness_chests", "Wilderness Chest", { GameUtil::Joaat("wilderness_chests") } },
		{ "Treasure", "treasure_hunter", "Treasure", { GameUtil::Joaat("treasure_hunter") }, Naming::Item },
		// herb_*.ysc's 1777389635, one subcategory per herb. Herbs grow
		// back, so found doesn't hide them.
		{ "Herb Pickups", "herbs", "Herb", { 0x69F0D043 }, Naming::Subcategory, false },
	};

	std::string RowName(const Category& category, const Collectable& c, int n)
	{
		const std::string numbered = std::format("{} {}", Tr(category.singular), n);
		switch (category.naming)
		{
		case Naming::Item:
			return GameUtil::ItemName(c.item, numbered);
		case Naming::Subcategory:
			return std::format("{} {}", GameUtil::ItemName(c.subcategory, Tr(category.singular)), n);
		default:
			return numbered;
		}
	}

	// "Show on Map", then one row per item that teleports to it.
	void BuildCategory(MenuBase* parent, Category& category)
	{
		MenuBase* menu = Ui::Submenu(parent, category.title);
		Category* c = &category;
		auto source = [c] { return c->Spots(); };
		Ui::Toggle(menu, std::string("collectibles.") + category.id + ".showonmap", "Show on Map",
			[c, source](bool on) { c->blips.Set(on, source, c->singular); },
			[c, source] { c->blips.Refresh(source, c->singular); });
		Ui::ListMenu(menu, "Locations", [c](MenuBase* list) {
			const std::vector<Collectable> items = CategoryItems(c->Resolve());
			if (items.empty())
			{
				Ui::Section(list, "The game has none of these");
				return;
			}
			if (c->tracksFound)
			{
				const auto found = std::count_if(items.begin(), items.end(), [](const Collectable& i) { return i.found; });
				Ui::Section(list, TrFormat("{} / {} found", found, items.size()));
			}
			int n = 0;
			for (const Collectable& item : items)
			{
				const Vector3 at = item.location;
				Ui::Action(list, RowName(*c, item, ++n) + FoundSuffix(c->tracksFound && item.found), [at] { return TeleportTo(at); });
			}
		});
	}

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

	std::vector<Spot> DreamcatcherSpots()
	{
		std::vector<Spot> spots;
		for (int i = 0; i < static_cast<int>(std::size(kDreamcatchers)); i++)
			spots.push_back({ kDreamcatchers[i], DreamcatcherFound(i) });
		return spots;
	}

	BlipSet g_dreamcatcherBlips;

	void BuildDreamcatchers(MenuBase* parent)
	{
		MenuBase* menu = Ui::Submenu(parent, "Dreamcatchers");
		Ui::Toggle(menu, "collectibles.showonmap", "Show on Map",
			[](bool on) { g_dreamcatcherBlips.Set(on, DreamcatcherSpots, "Dreamcatcher"); },
			[] { g_dreamcatcherBlips.Refresh(DreamcatcherSpots, "Dreamcatcher"); });
		Ui::ListMenu(menu, "Locations", [](MenuBase* list) {
			int found = 0;
			for (int i = 0; i < static_cast<int>(std::size(kDreamcatchers)); i++)
				found += DreamcatcherFound(i) ? 1 : 0;
			Ui::Section(list, TrFormat("{} / {} found", found, std::size(kDreamcatchers)));
			for (int i = 0; i < static_cast<int>(std::size(kDreamcatchers)); i++)
			{
				const Vector3 at = kDreamcatchers[i];
				Ui::Action(list, TrFormat("Dreamcatcher {}{}", i + 1, FoundSuffix(DreamcatcherFound(i))), [at] { return TeleportTo(at); });
			}
		});
	}

	// ---- Legendary Animals (ours) ----

	// Kill state only: the zone locations aren't known yet (Legendaries.h).
	void BuildLegendaryAnimals(MenuBase* parent)
	{
		Ui::ListMenu(parent, "Legendary Animals", [](MenuBase* list) {
			int killed = 0;
			for (int i = 0; i < Legendaries::kZoneCount; i++)
				killed += Legendaries::Killed(i) ? 1 : 0;
			Ui::Section(list, TrFormat("{} / {} killed", killed, Legendaries::kZoneCount));
			for (int i = 0; i < Legendaries::kZoneCount; i++)
			{
				const std::string caption = std::string(Legendaries::kZones[i])
					+ (Legendaries::Killed(i) ? std::string(Tr(" ~COLOR_RED~(Killed)")) : "");
				list->AddItem(new MenuItemLabel([caption] { return caption; }));
			}
		});
		Ui::Describe(parent, "The 16 legendary animals and which ones you've killed, as the game's hunting zones track it.");
	}

	// ---- SubCollectiblesCigaretteCards ----

	const Hash kCigaretteCards = GameUtil::Joaat("CIGARETTE_CARDS");
	constexpr int kCardsPerSet = 12;

	struct CardSet
	{
		const char* label;
		const char* subcategory; // the CARD_SET_* the scripts filter by
		const char* model;       // s_inv_cigcard_<model>_<NN>x
		int postedBit;           // in the posted-sets mask, by document (shop_post_office func_1875)
	};

	// In Rampage's spawn-row order.
	const CardSet kCardSets[] = {
		{ "Famous Gunslingers",     "CARD_SET_GUNSLINGERS", "gun",  32 },
		{ "Artists and Poets",      "CARD_SET_ARTISTS",     "art",  8 },
		{ "Vistas of America",      "CARD_SET_LANDMARKS",   "lnd",  256 },
		{ "Gems of Beauty",         "CARD_SET_GIRLS",       "grl",  16 },
		{ "Flora of North America", "CARD_SET_PLANTS",      "plt",  512 },
		{ "Stars of the Stage",     "CARD_SET_ACTRESSES",   "act",  1 },
		{ "Fauna of North America", "CARD_SET_ANIMALS",     "aml",  4 },
		{ "Marvels of Travel",      "CARD_SET_VEHICLES",    "veh",  2048 },
		{ "World's Champions",      "CARD_SET_SPORTS",      "spt",  1024 },
		{ "Amazing Inventions",     "CARD_SET_INVENTIONS",  "inv",  128 },
		{ "Breeds of Horses",       "CARD_SET_HORSES",      "hrs",  64 },
		{ "Prominent Americans",    "CARD_SET_AMERICANS",   "amer", 2 },
	};

	// Global_40.f_12019: sets mailed to the collector (shop_post_office
	// func_2380 sets the bits).
	constexpr int kPostedSets = 40 + 12019;

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

	// Where a set stands in the game's own tracking.
	struct SetStatus
	{
		std::vector<Hash> cards; // collectables, in the game's order
		int owned = 0;
		Hash document = 0;       // DOCUMENT_CIG_CARD_*_SET
		bool hasDocument = false;
		bool posted = false;     // its document was mailed to the collector
	};

	SetStatus GetStatus(const CardSet& set)
	{
		SetStatus status;
		status.cards = SetCards(set);
		for (Hash card : status.cards)
			if (OwnedCount(CardItem(card)) > 0)
				status.owned++;
		// flow_controller func_545: the document a full set earns.
		status.document = static_cast<Hash>(COLLECTABLE::_0x93F2E7B5DB85657B(kCigaretteCards, GameUtil::Joaat(set.subcategory)));
		status.hasDocument = OwnedCount(status.document) > 0;
		const UINT64* posted = GameUtil::Global(kPostedSets);
		status.posted = posted && (static_cast<int>(*posted) & set.postedBit);
		return status;
	}

	// Adds one card through the game's add function, unless `plain` (a
	// posted set, whose twelfth card would bring its document back). If the
	// game's function can't run: a plain add plus the found counter it
	// would have bumped.
	bool AddCard(Hash card, bool plain, std::string& error)
	{
		const Hash item = CardItem(card);
		if (!plain && GameUtil::AddInventoryItemViaScript(item, 1) && OwnedCount(item) > 0)
			return true;
		if (!GameUtil::AddInventoryItem(item, 1, error))
			return false;
		if (COLLECTABLE::_COLLECTABLE_GET_NUM_FOUND(card) == 0)
			COLLECTABLE::_COLLECTABLE_INCREMENT_NUM_FOUND(card, 1);
		return true;
	}

	struct CompleteResult
	{
		int added = 0;
		bool document = false; // the set's document was handed over
		bool alreadyDone = false;
		std::string error;
	};

	// Ours: completes a set as picking up its missing cards would. A set
	// held in full without its document (its cards added with a plain
	// inventory add, Rampage's way) has one card taken out and put back
	// through the game, so the game hands the document over itself. A
	// posted set only gets its missing cards.
	CompleteResult CompleteSet(const CardSet& set)
	{
		CompleteResult result;
		const SetStatus status = GetStatus(set);
		if (status.cards.empty())
		{
			result.error = "The game has no cards for this set";
			return result;
		}
		std::vector<Hash> missing;
		for (Hash card : status.cards)
			if (OwnedCount(CardItem(card)) == 0)
				missing.push_back(card);
		if (missing.empty() && (status.hasDocument || status.posted))
		{
			result.alreadyDone = true;
			return result;
		}

		Hash readded = 0;
		int restoreCopies = 0;
		if (missing.empty())
		{
			readded = status.cards.front();
			const Hash item = CardItem(readded);
			const int copies = OwnedCount(item);
			INVENTORY::_INVENTORY_REMOVE_INVENTORY_ITEM_WITH_ITEMID(GameUtil::kInventorySp, item, copies, GameUtil::kRemoveReasonDefault);
			if (OwnedCount(item) != 0)
			{
				result.error = "Couldn't take a card out to put it back";
				return result;
			}
			restoreCopies = copies - 1;
			missing.push_back(readded);
		}

		for (Hash card : missing)
		{
			std::string error;
			if (!AddCard(card, status.posted, error))
				result.error = error;
			else if (card != readded)
				result.added++;
		}
		std::string error;
		if (restoreCopies > 0)
			GameUtil::AddInventoryItem(CardItem(readded), restoreCopies, error);

		// The game's add hands the document over on the twelfth card; if it
		// couldn't run, give the document directly.
		if (!status.posted && OwnedCount(status.document) == 0 && GetStatus(set).owned == static_cast<int>(status.cards.size()))
			GameUtil::AddInventoryItem(status.document, 1, error);
		result.document = !status.posted && !status.hasDocument && OwnedCount(status.document) > 0;
		Log::Write("Complete Set {}: {} cards added, document {}, posted {}{}", set.label, result.added,
			result.document ? "handed over" : (status.hasDocument ? "already held" : "not held"), status.posted,
			result.error.empty() ? "" : ", error: " + result.error);
		return result;
	}

	std::string CompleteSetMessage(const CompleteResult& result)
	{
		if (!result.error.empty())
			return TrFormat("~COLOR_RED~Error:~s~ {}", Tr(result.error));
		if (result.alreadyDone)
			return std::string(Tr("Set already complete"));
		if (result.document)
			return TrFormat("Added {} cards and the set document", result.added);
		return TrFormat("Added {} cards", result.added);
	}

	std::string StatusLine(const SetStatus& status)
	{
		const int total = static_cast<int>(status.cards.size());
		if (status.posted)
			return TrFormat("{} / {} cards, posted to the collector", status.owned, total);
		if (status.hasDocument)
			return TrFormat("{} / {} cards, set document held", status.owned, total);
		return TrFormat("{} / {} cards", status.owned, total);
	}

	// SubCollectiblesCigaretteCardsSet: one row per card, adding it to the
	// inventory. Rampage's default is a plain inventory add (its Shift path
	// goes through flow_controller); ours always goes through the game, so
	// the twelfth card brings the set document as a real pick-up does.
	void BuildCardSet(MenuBase* list, const CardSet& set)
	{
		const SetStatus status = GetStatus(set);
		Ui::Section(list, StatusLine(status));
		Ui::Action(list, "Complete Set", [&set] {
			const std::string message = CompleteSetMessage(CompleteSet(set));
			Ui::Controller().ReopenActiveLater();
			return message;
		});
		int n = 0;
		for (Hash card : status.cards)
		{
			const Hash item = CardItem(card);
			const int owned = OwnedCount(item);
			const std::string name = GameUtil::ItemName(item, TrFormat("Card {}", ++n));
			const bool posted = status.posted;
			Ui::Action(list, owned > 0 ? std::format("{} ~COLOR_GREEN~({})", name, owned) : name, [card, item, posted] {
				std::string error;
				if (!AddCard(card, posted, error))
					return TrFormat("~COLOR_RED~Error:~s~ {}", Tr(error));
				Ui::Controller().ReopenActiveLater();
				return TrFormat("Added {}", GameUtil::ItemName(item, std::format("{:#x}", item)));
			});
		}
	}

	std::string CompleteAllSets()
	{
		int added = 0;
		int documents = 0;
		std::string error;
		for (const CardSet& set : kCardSets)
		{
			const CompleteResult result = CompleteSet(set);
			added += result.added;
			documents += result.document ? 1 : 0;
			if (!result.error.empty())
				error = result.error;
		}
		if (!error.empty())
			return TrFormat("~COLOR_RED~Error:~s~ {}", Tr(error));
		return TrFormat("Added {} cards and {} set documents", added, documents);
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
		Ui::Action(menu, "collectibles.completeallsets", "Complete All Sets", CompleteAllSets);
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
		// Ours: applies to every Show on Map below.
		Ui::Toggle(collectibles, "collectibles.hidefound", "Hide Found", [](bool on) { g_hideFound = on; })
			->SetDefault(true)
			->SetAlwaysRestore();
		Ui::Describe(collectibles, "Show on Map leaves out what you've already found. The location lists still show everything.");
		BuildCigaretteCards(collectibles);
		BuildCategory(collectibles, g_categories[0]); // Dino Bones
		BuildDreamcatchers(collectibles);
		BuildCategory(collectibles, g_categories[1]); // Rock Carvings
		Ui::Section(collectibles, "More Collectibles");
		for (size_t i = 2; i < std::size(g_categories); i++)
			BuildCategory(collectibles, g_categories[i]);
		BuildLegendaryAnimals(collectibles);
	}
}
