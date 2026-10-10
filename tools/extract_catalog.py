r"""Builds src/data/ItemCatalog.inc, Recovery > Add Items > Give Items' list,
from the game's SP item catalog (docs/COLLECTIBLES_AND_ITEMS_PLAN.md, step B1).
Regenerate rather than edit. Nothing here comes from Rampage.

    python tools/extract_catalog.py <input> src/data/ItemCatalog.inc [--names a.txt b.txt ...]
    python tools/extract_catalog.py <catalog_sp.ymt> --dump-json tools/data/catalog_sp_items.json [--names ...]
    python tools/extract_catalog.py <catalog_sp.ymt> --schema [--names ...]

<input> is either the extracted catalog_sp.ymt or its JSON dump,
tools/data/catalog_sp_items.json (checked in, so the .inc can be rebuilt
without the game files). --dump-json refreshes that JSON from a .ymt;
--schema prints the PSO structs.

Extracting the .ymt stays a manual step with the user's RDR2 RPF Tool, run
headless from ..\external-tools\RDR2-RPF-Tool\RDR2 RPF Tool\bin\Release\
(that folder has oo2core_5_win64.dll; app.publish doesn't):

    "RDR2 RPF Tool.exe" --extract <game>\update_4.rpf x64/data/itemdatabase/catalog_sp.rpf a.rpf
    "RDR2 RPF Tool.exe" --extract a.rpf catalog_sp.ymt catalog_sp.ymt

update_4's copy is a superset of data_0's and the mp004/mp005 dlcpacks'.

--names takes word lists (one name per line) that put names to hashes: the
scripts' joaat("...") names plus
..\external-tools\RAGE-StringsDatabase\RDR2\TextKeys\*.txt resolve about 70%
of the items. With the JSON, its own names are used and --names only fills
in hashes it lacks. The names are only the menu's fallback and search key:
an item hash is its own text label, so the menu shows the game's name
(GameUtil::ItemName).

PSO layout follows CodeWalker's CodeWalker.Core/GameFiles/MetaTypes/Pso.cs
(big-endian sections: PSIN data, PMAP blocks, PSCH schema). Item struct
0xEDD9A017, 208 bytes: key @8, category @12 (ci_category_*; the names
found so far, by brute force over item-name tokens, are in
tools/data/ci_categories.txt, which the default --names includes), item type @16, flags @24, model @28, tags @40, acquire costs @56.
Items live in the PMAP block whose wrapper struct (a map entry: hash @0,
struct @8, 216 bytes) points at 0xEDD9A017.

Tags: each item's tag array (item @40) holds 16-byte {tag @8, tag type @12}
entries (struct 0x5663E91C). The arrays' PSO pointers wrap past 1 MB, so they
are read in item order from that struct's block, which they exactly fill.
Tag types: TAG_ITEM_PROPERTY (CI_TAG_ITEM_*, CI_TAG_CATEGORY_* = the satchel
page, CI_TAG_SHOP_*) and TAG_SATCHEL_FOLDER (CI_TAG_FOLDER_* = the folder on
that page). Names found so far, by brute force over item-name tokens, are in
tools/data/ci_tags.txt. Acquire costs (@56) and sell prices (@72) are only
counted: an item with neither can't be bought or sold.

classify() sorts each item into the Give Items list it belongs in, from
those tags (see its comments); items it returns None for aren't listed.

Output lines are { key, "type", category, "NAME" or nullptr, "group" or
nullptr, "subgroup" or nullptr }, grouped by type in TYPE_ORDER, then by key.
Types are lowercased, and the catalog's one-offs (CURRENCY, Component)
become "other"; the root "character" item is left out.
"""
import argparse, collections, json, os, struct, sys

ITEM_STRUCT = 0xEDD9A017
TAG_STRUCT = 0x5663E91C
MAP_VALUE_FIELD = 0x063FA3F2

# Menu order. Types other menus own (clothing, weapon, horse) are still
# written, so the table is the whole catalog; Give Items skips them.
TYPE_ORDER = ["consumable", "provision", "document", "ammo", "kit", "upgrade",
              "core_item", "horse_equipment", "weapon_mod", "weapon_decoration",
              "money", "advert", "other", "clothing", "weapon", "horse"]
OTHER = {"currency", "component"}
SKIP = {"character"}


def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF
    h ^= h >> 11
    return (h + (h << 15)) & 0xFFFFFFFF


# ---- PSO reading ------------------------------------------------------------

def load_sections(path):
    b = open(path, "rb").read()
    sections, o = {}, 0
    while o + 8 <= len(b):
        tag = b[o:o + 4].decode("latin1")
        length = struct.unpack(">I", b[o + 4:o + 8])[0]
        sections[tag] = b[o:o + length]
        if length < 8:
            break
        o += length
    return sections


def parse(sections):
    pm = sections["PMAP"]
    root, count, _ = struct.unpack(">iHH", pm[8:16])
    o = 16
    if count == 0 or count >= 0x8000:  # newer header
        count = struct.unpack(">H", pm[16:18])[0]
        o = 24
    # (name hash, offset into PSIN, unknown, length)
    blocks = [struct.unpack(">Iiii", pm[o + 16 * i:o + 16 * i + 16]) for i in range(count)]
    ps = sections["PSCH"]
    n = struct.unpack(">I", ps[8:12])[0]
    schema = {}
    for i in range(n):
        name, off = struct.unpack(">Ii", ps[12 + 8 * i:20 + 8 * i])
        x = struct.unpack(">I", ps[off:off + 4])[0]
        if x >> 24 == 0:
            size = struct.unpack(">i", ps[off + 4:off + 8])[0]
            fields = [struct.unpack(">IBBHI", ps[off + 12 + 12 * j:off + 24 + 12 * j]) for j in range(x & 0xFFFF)]
            schema[name] = ("struct", size, fields)  # field: name, type, subtype, offset, ref
        else:
            schema[name] = ("enum", [struct.unpack(">Ii", ps[off + 4 + 8 * j:off + 12 + 8 * j]) for j in range(x & 0xFFFFFF)])
    return root, blocks, schema


def ymt_items(sections, blocks, schema):
    data = sections["PSIN"]
    wrapper = next(k for k, v in schema.items() if v[0] == "struct"
                   and any(f[0] == MAP_VALUE_FIELD and f[4] == ITEM_STRUCT for f in v[2]))
    size = schema[wrapper][1]
    block = next(b for b in blocks if b[0] == wrapper)
    tags_at = next(b for b in blocks if b[0] == TAG_STRUCT)[1]
    count = lambda e, off: struct.unpack(">H", e[off + 8:off + 10])[0]  # a PSO array's count
    for i in range(block[3] // size):
        e = data[block[1] + size * i:block[1] + size * (i + 1)]
        key, category, kind, _, flags, model = struct.unpack(">IIIIII", e[16:40])
        item = e[8:]
        n = count(item, 40)
        tags = [struct.unpack(">II", data[tags_at + 16 * j + 8:tags_at + 16 * j + 16]) for j in range(n)]
        tags_at += 16 * n
        yield key, category, kind, flags, model, tags, count(item, 56), count(item, 72)


# ---- names -------------------------------------------------------------------

def load_names(paths):
    names = {}
    for path in paths:
        for w in open(path, encoding="utf-8", errors="replace"):
            w = w.strip()
            if w:
                names.setdefault(joaat(w), w)
    return names


def is_hex(name):
    return name.startswith("0x")


def hex_name(h):
    return "0x%08X" % h


# ---- input -------------------------------------------------------------------

def rows_from_ymt(path, names):
    sections = load_sections(path)
    _, blocks, schema = parse(sections)
    name = lambda h: names.get(h, hex_name(h))
    return [dict(key=k, name=name(k), category=name(c), type=name(t), flags=f, model=name(m),
                 tags=[[name(tag), name(kind)] for tag, kind in tags], buy=buy, sell=sell)
            for k, c, t, f, m, tags, buy, sell in ymt_items(sections, blocks, schema)]


def rows_from_json(path, names):
    rows = json.load(open(path, encoding="utf-8"))
    named = lambda s: names.get(int(s, 16), s) if is_hex(s) else s
    for r in rows:
        if is_hex(r["name"]) and r["key"] in names:
            r["name"] = names[r["key"]]
        r["category"] = named(r["category"])
        r["tags"] = [[named(t), named(k)] for t, k in r.get("tags", [])]
    return rows


# ---- Give Items groups ---------------------------------------------------------

# The materials page: no CI_TAG_CATEGORY_* name found for its tag yet.
MATERIALS_PAGE = "0x85B3CEDE"
PAGES = {"CI_TAG_CATEGORY_PROVISION": "provisions", "CI_TAG_CATEGORY_REMEDY": "remedies",
         "CI_TAG_CATEGORY_INGREDIENT": "ingredients", "CI_TAG_CATEGORY_KIT": "kit",
         "CI_TAG_CATEGORY_VALUABLE": "valuables", "CI_TAG_CATEGORY_DOCUMENT": "documents",
         MATERIALS_PAGE: "materials"}
# Document folders (TAG_SATCHEL_FOLDER), merged into the Documents lists.
DOCUMENT_FOLDERS = {
    "CI_TAG_FOLDER_LETTERS": "letters", "CI_TAG_FOLDER_NOTES": "notes",
    "CI_TAG_FOLDER_NEWSPAPERS": "newspapers", "CI_TAG_FOLDER_NEWSPAPER_SCRAPS": "newspapers",
    "CI_TAG_FOLDER_RECIPE_PAMPHLETS": "recipes", "CI_TAG_FOLDER_BOOKS": "books", "0x575B70B5": "books",
    "CI_TAG_FOLDER_TREASURE_MAPS": "maps", "CI_TAG_FOLDER_MAPS": "maps",
    "CI_TAG_FOLDER_BOUNTY_POSTERS": "posters",
    "CI_TAG_FOLDER_PHOTOGRAPHS": "photos", "CI_TAG_FOLDER_DRAWINGS": "photos",
    "CI_TAG_FOLDER_DINOSAUR_NOTES": "collectors", "CI_TAG_FOLDER_ROCK_CARVING_NOTES": "collectors",
    "CI_TAG_FOLDER_TAXIDERMIST_ORDERS": "collectors", "CI_TAG_FOLDER_BUSINESS_CARDS": "collectors",
    "0x699AB030": "collectors",
}
# Containers the inventory keeps for itself.
INTERNAL = {"KIT_WARDROBE", "KIT_CAMP", "KIT_CAMP_SIMPLE"}
STORY_FOLDERS = {"CI_TAG_FOLDER_KIT_KEEPSAKES", "CI_TAG_FOLDER_KIT_KEYCHAIN"}
# Tags that explain why a provision can't be bought or sold other than it
# being a story item: rare orchids, used-up leftovers, crafting materials,
# collector loot.
NOT_STORY = {"CI_TAG_ITEM_RARE_ORCHID", "CI_TAG_ITEM_FLOWER", "CI_TAG_ITEM_COLLECTIBLE", "CI_TAG_ITEM_USED",
             "CI_TAG_ITEM_MATERIAL", "CI_TAG_FOLDER_SELL_CATCHER"}


def classify(r):
    """(group, subgroup) for Give Items, or None when the item isn't listed."""
    t = item_type(r["type"])
    name = "" if is_hex(r["name"]) else r["name"]
    tags = r.get("tags", [])
    props = {tag for tag, kind in tags if kind == "TAG_ITEM_PROPERTY"}
    folder = next((tag for tag, kind in tags if kind == "TAG_SATCHEL_FOLDER"), None)
    page = next((PAGES[p] for p in PAGES if p in props), None)
    used = "CI_TAG_ITEM_USED" in props or name.endswith("_USED")
    if t == "ammo":
        return None  # the ped's ammo, given from Weapon > Ammunition
    if t == "kit" and ("CI_TAG_ITEM_POUCH_UPGRADE" in props or "CI_TAG_ITEM_IS_SATCHEL" in props
                       or name.startswith("KIT_POUCH") or name == "CUSTOM_SATCHEL"):
        return ("satchel", None)
    if "CI_TAG_ITEM_FISHING_BAIT" in props or "CI_TAG_ITEM_FISHING_LURE" in props or name.startswith("UPGRADE_FSH_"):
        return None if name.endswith("_NONE") else ("fishing", None)  # the bait tins too
    if name in INTERNAL:
        return None
    if "CI_TAG_ITEM_TRINKET" in props or "CI_TAG_ITEM_TALISMAN" in props:
        return ("trinkets", None)
    # Story items: the game's Keepsakes and Keychain folders, plus
    # provisions that can't be bought or sold (the broken pistol, Mary's
    # ring, the companions' things) unless another tag explains it.
    if folder in STORY_FOLDERS:
        return ("story", None)
    if (t == "provision" and not r.get("buy") and not r.get("sell") and page in (None, "kit", "valuables")
            and not props & NOT_STORY and not used and folder != "CI_TAG_FOLDER_KIT_WATCHES"
            and not name.startswith("PROVISION_JEWELRY_BOX")):
        return ("story", None)
    if used or t not in ("consumable", "provision", "document", "kit", "upgrade"):
        return None  # clothing and weapons have their own menus
    if name == "PROVISION_GOLDTOOTH":
        return ("valuables", None)  # filed as a material, sold to the fence like jewelry
    if page == "documents":
        if (folder and folder.startswith("CI_TAG_FOLDER_CIG_CARD")) or name.startswith("DOCUMENT_CIG_CARD"):
            return ("documents", "cards")
        return ("documents", DOCUMENT_FOLDERS.get(folder, "other"))
    if page == "materials":
        if "CI_TAG_SHOP_ANIMAL_LEGENDARY" in props or "LEGENDARY" in name:
            return ("materials", "legendary")
        if "CI_TAG_ITEM_MEAT_FISH" in props or "CI_TAG_ITEM_FISH" in props or "_FISH_" in name:
            return ("materials", "fish")
        if "CI_TAG_ITEM_ANIMAL_CARCASS" in props or "CI_TAG_SHOP_ANIMAL_CARCASS" in props:
            return ("materials", "carcasses")
        if "CI_TAG_SHOP_ANIMAL_PELT" in props:
            return ("materials", "pelts")
        if "CI_TAG_ITEM_ANIMAL_FEATHER" in props or folder == "CI_TAG_FOLDER_CRAFT_FEATHERS":
            return ("materials", "feathers")
        return ("materials", "parts")
    return (page, None) if page else None


def print_schema(path, names):
    sections = load_sections(path)
    _, blocks, schema = parse(sections)
    name = lambda h: names.get(h, hex_name(h))
    for b in blocks:
        print("block", name(b[0]), b[1], b[3])
    for k, v in schema.items():
        if v[0] == "struct":
            print("STRUCT", name(k), "size", v[1])
            for f in v[2]:
                print("   ", name(f[0]), "type=%d sub=%d off=%d ref=%s" % (f[1], f[2], f[3], name(f[4])))
        else:
            print("ENUM", name(k), len(v[1]))


# ---- output ------------------------------------------------------------------

def item_type(raw):
    t = raw.lower()
    return "other" if t in OTHER else t


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def category_hash(name):
    return int(name, 16) if is_hex(name) else joaat(name)


def write_inc(rows, out, source):
    items = []
    for r in rows:
        t = item_type(r["type"])
        if t in SKIP:
            continue
        if t not in TYPE_ORDER:
            sys.exit("unknown item type %r (key 0x%08X): add it to TYPE_ORDER" % (r["type"], r["key"]))
        items.append((TYPE_ORDER.index(t), r["key"], t, category_hash(r["category"]),
                      None if is_hex(r["name"]) else r["name"], classify(r)))
    items.sort(key=lambda i: i[:2])
    lines = ["// Generated by tools/extract_catalog.py from %s;" % source,
             "// regenerate rather than edit. { item hash, item type, ci_category_* hash, internal name or nullptr,",
             "// Give Items group or nullptr, subgroup or nullptr }"]
    opt = lambda s: c_string(s) if s else "nullptr"
    for _, key, t, category, name, group in items:
        g, sub = group or (None, None)
        lines.append("{ 0x%08X, %s, 0x%08X, %s, %s, %s }," % (key, c_string(t), category, opt(name), opt(g), opt(sub)))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="") as f:
        f.write("\r\n".join(lines) + "\r\n")
    counts = collections.Counter(i[2] for i in items)
    print("items", len(items), "named", sum(i[4] is not None for i in items))
    print(", ".join("%s %d" % (t, counts[t]) for t in TYPE_ORDER if counts[t]))
    groups = collections.Counter("/".join(g for g in i[5] if g) for i in items if i[5])
    print("groups:", ", ".join("%s %d" % kv for kv in sorted(groups.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input", help="catalog_sp.ymt or its JSON dump")
    ap.add_argument("out", nargs="?", help="the .inc to write")
    ap.add_argument("--names", nargs="*", default=[], help="word lists naming hashes")
    ap.add_argument("--dump-json", metavar="PATH", help="write the items as JSON (from a .ymt)")
    ap.add_argument("--schema", action="store_true", help="print the PSO schema (from a .ymt)")
    args = ap.parse_args()

    data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
    names = load_names([os.path.join(data, "ci_categories.txt"), os.path.join(data, "ci_tags.txt")] + args.names)
    from_json = args.input.lower().endswith(".json")
    if args.schema:
        if from_json:
            sys.exit("--schema needs the .ymt")
        return print_schema(args.input, names)
    rows = rows_from_json(args.input, names) if from_json else rows_from_ymt(args.input, names)
    if args.dump_json:
        with open(args.dump_json, "w", encoding="utf-8") as f:
            json.dump(rows, f, indent=0)
        print("wrote", args.dump_json, len(rows), "items")
    if args.out:
        source = "the game's catalog_sp.ymt (update_4.rpf)"
        if from_json:
            source = "tools/data/catalog_sp_items.json (a dump of catalog_sp.ymt)"
        write_inc(rows, args.out, source)
    elif not args.dump_json:
        ap.error("nothing to do: give an output .inc, --dump-json or --schema")


if __name__ == "__main__":
    main()
