r"""Prototype reader for the game's SP item catalog (catalog_sp.ymt, a binary
PSO file); the starting point for tools/extract_catalog.py
(docs/COLLECTIBLES_AND_ITEMS_PLAN.md, step B1).

    python tools/catalog_dump.py schema <catalog_sp.ymt> <names.txt>
    python tools/catalog_dump.py items  <catalog_sp.ymt> <names.txt> <out.json>

<names.txt> is any word list (one name per line) used to put names to
hashes: the scripts' joaat("...") names plus
..\external-tools\RAGE-StringsDatabase\RDR2\TextKeys\*.txt resolve about 70%
of the items. How to extract the .ymt from update_4.rpf is in the plan.

PSO layout follows CodeWalker's Pso.cs (big-endian sections PSIN data, PMAP
blocks, PSCH schema). Item struct 0xEDD9A017: key @8, category @12, item
type @16, flags @24, model @28. Items live in the PMAP block whose wrapper
struct (map entry: hash @0, struct @8) points at 0xEDD9A017.
"""
import collections, json, struct, sys

ITEM_STRUCT = 0xEDD9A017
MAP_VALUE_FIELD = 0x063FA3F2

def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF
    h ^= h >> 11
    return (h + (h << 15)) & 0xFFFFFFFF

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

def load_names(path):
    names = {}
    for w in open(path, encoding="utf-8", errors="replace"):
        w = w.strip()
        if w:
            names.setdefault(joaat(w), w)
    return names

def items(sections, blocks, schema):
    data = sections["PSIN"]
    wrapper = next(k for k, v in schema.items() if v[0] == "struct"
                   and any(f[0] == MAP_VALUE_FIELD and f[4] == ITEM_STRUCT for f in v[2]))
    size = schema[wrapper][1]
    block = next(b for b in blocks if b[0] == wrapper)
    for i in range(block[3] // size):
        e = data[block[1] + size * i:block[1] + size * (i + 1)]
        key, category, kind, _, flags, model = struct.unpack(">IIIIII", e[16:40])
        yield key, category, kind, flags, model

def main(mode, ymt, names_path, out=None):
    sections = load_sections(ymt)
    root, blocks, schema = parse(sections)
    names = load_names(names_path)
    name = lambda h: names.get(h, "0x%08X" % h)
    if mode == "schema":
        for b in blocks:
            print("block", name(b[0]), b[1], b[3])
        for k, v in schema.items():
            if v[0] == "struct":
                print("STRUCT", name(k), "size", v[1])
                for f in v[2]:
                    print("   ", name(f[0]), "type=%d sub=%d off=%d ref=%s" % (f[1], f[2], f[3], name(f[4])))
            else:
                print("ENUM", name(k), len(v[1]))
        return
    rows = [dict(key=k, name=name(k), category=name(c), type=name(t), flags=f, model=name(m))
            for k, c, t, f, m in items(sections, blocks, schema)]
    json.dump(rows, open(out, "w", encoding="utf-8"), indent=0)
    print("items", len(rows), "named", sum(not r["name"].startswith("0x") for r in rows))
    print(collections.Counter(r["type"] for r in rows).most_common())

if __name__ == "__main__":
    main(*sys.argv[1:])
