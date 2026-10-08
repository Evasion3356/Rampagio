"""Turn rampage_inventory_ida.py's JSON into a feature checklist.

    python tools/rampage_inventory.py <inventory.json> <rampage_natives.csv> <prefix>

Writes <prefix>.csv (one row per menu option) and <prefix>.md (grouped by
submenu, for reading). For each option it lists the natives reached from
its handler and, for toggles, from the functions that read the toggle's
global, following the call graph a few levels down. Both outputs are
derived from Rampage's binary: keep them out of git.
"""
import bisect
import collections
import csv
import json
import re
import sys

DEPTH = 3
# Callees with more callers than this are shared plumbing (strings,
# allocation, the menu framework), not feature code.
MAX_CALLERS = 25
# Functions that read more toggle globals than this are config load/save
# or hotkey tables, not the feature's tick.
MAX_GLOBALS_READ = 6
SCRIPT_CALL = 0x18001C900   # look up a script function by (script hash, index)

INTERACTIVE = {"action", "toggle", "toggle_plain", "toggle_value", "toggle_cb", "choice",
               "slider_float", "slider_int", "number_int", "value_hex", "value_pick",
               "color", "action_icon", "component_pick", "posse_item", "location"}

# Top-level menu areas, from the builder name prefix. Longest match wins.
AREAS = [
    ("SubSelfHorse", "Horse"), ("SubHorse", "Horse"), ("SubMobileStable", "Horse"),
    ("SubSelf", "Player"), ("SubPlayer", "Player"), ("SubAbilities", "Player"),
    ("SubEmotes", "Player"), ("SubMoods", "Player"), ("SubPlaySpeech", "Player"),
    ("SubVoiceChanger", "Player"), ("SubDamagePacks", "Player"), ("SubEffects", "Player"),
    ("SubTimecycleMod", "Player"), ("SubAnimPostFx", "Player"), ("SubAnimationDicts", "Player"),
    ("SubModelChanger", "Player"),
    ("SubWeapon", "Weapons"),
    ("SubVehicle", "Vehicles"), ("SubTrain", "Vehicles"),
    ("SubTeleport", "Teleport"),
    ("SubWorld", "World"), ("SubIMAP", "World"),
    ("SubRecovery", "Recovery"), ("SubCollectibles", "Recovery"), ("SubStatEditor", "Recovery"),
    ("SubUnlockCheats", "Recovery"), ("SubMapDiscoverables", "Recovery"),
    ("SubPedSpawner", "Spawners"), ("SubObjectSpawner", "Spawners"), ("SubObjSpawner", "Spawners"),
    ("SubPlantSpawner", "Spawners"),
    ("SubPedEditor", "Ped Editor"), ("SubPed", "Ped Editor"),
    ("SubObjEditor", "Object Editor"), ("SubObj", "Object Editor"), ("SubObjectFinder", "Object Editor"),
    ("SubScript", "Script Tools"), ("SubGlobalEditor", "Script Tools"),
    ("SubSettings", "Settings"), ("SubOverlay", "Settings"), ("SubWindowManager", "Settings"),
    ("SubLanguage", "Settings"), ("SubPluginManager", "Settings"), ("SubTrainerSearch", "Settings"),
    ("SubCreator", "Settings"), ("SubAbout", "Settings"),
    ("SubMiscellaneous", "Misc"), ("SubMobileTheater", "Misc"), ("SubMusicPlayer", "Misc"),
    ("SubGameMusic", "Misc"), ("SubCutscenePlayer", "Misc"), ("SubMinigames", "Misc"),
    ("SubEditVolume", "Misc"), ("SubVolumeEditor", "Misc"), ("SubFriendlist", "Misc"),
    ("SubDebug", "Debug"),
]


def area_of(sub):
    best = max((p for p, _ in AREAS if sub.startswith(p)), key=len, default=None)
    return dict(AREAS)[best] if best else "Other"


# Sub-slot writes Hex-Rays uses for packed immediates: macro -> (byte offset, width).
PARTS = {"LOBYTE": (0, 1), "LOWORD": (0, 2), "LODWORD": (0, 4), "HIBYTE": (7, 1),
         "HIWORD": (6, 2), "HIDWORD": (4, 4), "WORD1": (2, 2), "WORD2": (4, 2)}
PARTS.update({"BYTE%d" % i: (i, 1) for i in range(1, 7)})
# Texture dictionaries passed to icon rows; never a label.
TEXTURE_RX = re.compile(r"[a-z0-9]+(?:_[a-z0-9]+)+")
INT_RX = r"(-?(?:0x[0-9A-Fa-f]+|\d+))(?:u?i?64|LL|uLL|u)?"


def first_arg(call_line):
    m = re.search(r"sub_[0-9A-F]+\((.*)\);", call_line)
    if not m:
        return None
    arg = m.group(1).split(",")[0]
    arg = re.sub(r"\([^()]*\)", "", arg).replace("&", "").strip()
    m = re.match(r"(\w+)", arg)
    return m.group(1) if m else None


def label_from_pseudocode(pc):
    """Recover the label the row's first argument holds, from the lines
    that fill it in: a literal copy, or 8-byte immediates per slot."""
    if not pc:
        return None
    var = first_arg(pc[-1])
    if not var:
        return None
    v = re.escape(var)
    literal = None
    buf = bytearray(32)
    used = False
    for line in pc[:-1]:
        if not re.search(r"\b%s\b" % v, line):
            continue
        m = re.search(r'(?:strcpy|qmemcpy)\([^,]*\b%s\b[^,]*,\s*"((?:[^"\\]|\\.)*)"' % v, line)
        if m:
            literal = m.group(1)
            continue
        m = re.match(r'\s*\*?\(?[\w\s*]*\)?\s*\(?%s(?:\[(\d)\])?\)?\s*=\s*\*\([\w\s*]+\)"((?:[^"\\]|\\.)*)"' % v, line)
        if m:
            literal = m.group(2)
            continue
        m = re.match(r"\s*(?:(\w+)\()?%s\[(\d)\]\)?\s*=\s*(?:\([^()]*\))?%s;" % (v, INT_RX), line)
        if m:
            macro, slot, val = m.group(1), int(m.group(2)), int(m.group(3), 0)
            off, width = PARTS.get(macro, (0, 8)) if macro else (0, 8)
            if slot > 3 or (macro and macro not in PARTS):
                continue
            pos = slot * 8 + off
            buf[pos:pos + width] = (val & ((1 << (8 * width)) - 1)).to_bytes(width, "little")
            used = True
    if literal is not None:
        return literal.encode().decode("unicode_escape")
    if used:
        text = bytes(buf).split(b"\0")[0]
        if text and all(32 <= c < 127 for c in text):
            return text.decode()
    return None


def row_label(r):
    """The row's label: from the pseudocode that fills the API call's first
    argument, else the last string the instruction walk saw."""
    desc = set(row_description(r))
    strs = [s for s in r["strs"] if s.strip() and not TEXTURE_RX.fullmatch(s) and s not in desc]
    label = label_from_pseudocode(r.get("pc"))
    if label is None:
        label = strs[-1] if strs else ""
    if TEXTURE_RX.fullmatch(label):
        label = ""
    # Packed immediates can cover only the start of a label; Hex-Rays
    # fills the rest from the literal, which the instruction walk saw.
    label = next((s for s in strs if label and s.startswith(label) and len(s) > len(label)), label)
    # A label copied in pieces (qmemcpy of its start, then a word read of
    # its tail) leaves only the tail as the last literal.
    return next((s for s in strs if label and s.endswith(label) and len(s) > len(label)), label)


LITERAL = r'"((?:[^"\\]|\\.)*)"'
# Builds a description vector: from a range of std::strings, or (other
# builders) from an array of n strings filled just before.
VECTOR_FILL = re.compile(r"sub_180218810\(|sub_18024E950\(")


def unescape(s):
    return s.encode("latin-1", "backslashreplace").decode("unicode_escape").encode("latin-1").decode("utf-8", "replace")


def row_description(r):
    """The row's description lines (Rampage passes a vector<string> to every
    menu API call; the selected row's is drawn under the menu). They're the
    strings the pseudocode builds between the previous vector fill and the
    last one before the call: std::string constructions, strcpy/qmemcpy
    copies and dword reads of a literal. A copy of only a literal's start
    is completed from the strings the instruction walk saw."""
    strs = r["strs"]
    events = []
    for line in r.get("pc") or []:
        if VECTOR_FILL.search(line):
            events.append(None)
            continue
        for m in re.finditer(r"(?:sub_18002FED0|strcpy|qmemcpy)\([^\"]*" + LITERAL, line):
            s = unescape(m.group(1))
            if "qmemcpy" in m.group(0):
                s = next((x for x in strs if x.startswith(s) and len(x) > len(s)), s)
            events.append(s)
        for m in re.finditer(r"\*\([\w ]+ \*\)" + LITERAL, line):
            s = unescape(m.group(1))
            if events and events[-1] and events[-1].endswith(s) and events[-1] != s:
                continue  # the tail of the string copied just before
            events.append(next((x for x in strs if x.startswith(s)), s))
    fills = [i for i, e in enumerate(events) if e is None]
    if not fills:
        return []
    start = fills[-2] + 1 if len(fills) > 1 else 0
    return [e for e in events[start:fills[-1]] if e and not TEXTURE_RX.fullmatch(e)]


def main(inv_path, natives_csv, prefix):
    inv = json.load(open(inv_path))
    cg = {int(k): v for k, v in inv["callgraph"].items()}
    vft = {int(k): v for k, v in inv["vft"].items()}
    readers = {int(k): v for k, v in inv["readers"].items()}
    builders = {int(k) for k in inv["builders"]}

    callers = collections.Counter(c for cs in cg.values() for c in cs)
    starts = [c[0] for c in inv["chunks"]]

    def owner(ea):
        i = bisect.bisect_right(starts, ea) - 1
        if i >= 0 and ea < inv["chunks"][i][1]:
            return inv["chunks"][i][2]
        return None

    natives_in = collections.defaultdict(list)
    for r in csv.DictReader(open(natives_csv)):
        if r["native"] or r["hash"]:
            natives_in[owner(int(r["site"], 16))].append(r["native"] or r["hash"])

    globals_read = collections.Counter(f for fs in readers.values() for f in fs)

    def closure(roots):
        seen, frontier = set(), [f for f in roots if f]
        for _ in range(DEPTH + 1):
            nxt = []
            for f in frontier:
                if f in seen or f in builders:
                    continue
                seen.add(f)
                nxt += [c for c in cg.get(f, ()) if callers[c] <= MAX_CALLERS or c == SCRIPT_CALL]
            frontier = nxt
        return seen

    out = []
    for r in inv["rows"]:
        if r["api"] in ("title",):
            continue
        label = row_label(r)
        desc = " / ".join(row_description(r))
        roots = []
        for kind, ea in r["handlers"]:
            if kind == "lambda" and ea in vft:
                roots.append(vft[ea]["docall"])
            elif kind == "fn" and callers[ea] <= MAX_CALLERS:
                roots.append(ea)
        if r["api"].startswith("toggle"):
            for g in r["globals"]:
                roots += [f for f in readers.get(g, ()) if globals_read[f] <= MAX_GLOBALS_READ]
        funcs = closure(roots)
        nat = sorted({n for f in funcs for n in natives_in.get(f, ())})
        out.append(dict(
            area=area_of(r["sub"]), submenu=r["sub"], order=r["order"], kind=r["api"],
            label=label, description=desc,
            data_driven="yes" if not label and r["api"] in INTERACTIVE | {"list_item", "list_item_icon", "submenu"} else "",
            script_call="yes" if SCRIPT_CALL in funcs else "",
            handlers=" ".join(hex(f) for f in roots[:4]),
            natives=" ".join(nat),
            site=hex(r["site"]),
        ))
    out.sort(key=lambda x: (x["area"], x["submenu"], x["order"]))

    with open(prefix + ".csv", "w", newline="", encoding="utf-8") as fp:
        w = csv.DictWriter(fp, fieldnames=list(out[0]))
        w.writeheader()
        w.writerows(out)

    by_sub = collections.defaultdict(list)
    for o in out:
        by_sub[(o["area"], o["submenu"])].append(o)
    lines = ["# Rampage feature inventory", "",
             "Generated by tools/rampage_inventory.py. One section per Rampage submenu builder.",
             "`port` column: tick it when Rampagio has the option and it's live-tested.", ""]
    stats = collections.Counter()
    area = None
    for (a, sub), opts in sorted(by_sub.items()):
        if a != area:
            area = a
            lines += ["", "## " + a, ""]
        lines += ["### " + sub, "", "| port | kind | label | description | natives |", "|---|---|---|---|---|"]
        for o in opts:
            if o["kind"] == "section":
                lines.append("| | — | **%s** | | |" % o["label"])
                continue
            if o["data_driven"]:
                stats["data_driven"] += 1
            elif o["kind"] in INTERACTIVE:
                stats[a] += 1
            nat = o["natives"].split()
            shown = ", ".join("`%s`" % n.split("::")[-1] for n in nat[:8]) + (" +%d" % (len(nat) - 8) if len(nat) > 8 else "")
            if o["script_call"]:
                shown = "**script call** " + shown
            lines.append("| [ ] | %s | %s | %s | %s |" % (
                o["kind"], (o["label"] or "*(data-driven)*").replace("|", "/"),
                o["description"].replace("|", "/"), shown))
        lines.append("")
    summary = ["", "## Counts", "", "Static options per area (sections, titles and data-driven list rows excluded):", ""]
    summary += ["- %s: %d" % (k, v) for k, v in sorted(stats.items()) if k != "data_driven"]
    summary += ["- data-driven rows (lists filled at runtime): %d" % stats["data_driven"],
                "- submenus: %d" % len(by_sub), ""]
    with open(prefix + ".md", "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines[:5] + summary + lines[5:]) + "\n")
    print("%d rows, %d submenus -> %s.csv / %s.md" % (len(out), len(by_sub), prefix, prefix))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
