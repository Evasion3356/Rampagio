r"""Headless IDA: builds src/data/Teleports.inc, the Teleport menu's location
lists, from Rampage's menu builders.

    idat.exe -A -S"tools\extract_rampage_teleports_ida.py src\data\Teleports.inc" -L"log.txt" <copy of Rampage.asi.i64>

Rampage adds each location row with one call to its location API
(LOCATION_API below: label, x, y, z), from one builder per region submenu
(the Common Locations sit on its Teleport menu itself). The builders are
found as the callers of that API whose title is in REGIONS. Labels come
from the Hex-Rays lines before each call: strcpy/std::string literals, or
short names packed into integer immediates. Shops and Services is a table
of { category, { town, x, y, z }... } filled by a static initializer,
found by its "Photostudio" string.

The names and coordinates are carried over on purpose (see CLAUDE.md);
nothing reads Rampage at runtime. Output rows:
    { "<submenu>", "<nested submenu or empty>", "<section or empty>", "<id prefix>", "<name>", x, y, z },
"""
import re
import struct

import ida_auto
import ida_bytes
import ida_funcs
import ida_hexrays
import ida_nalt
import ida_pro
import idautils
import idc

LOCATION_API = 0x1801F0970
TITLE_API = 0x1801ECEE0
SECTION_API = 0x1801EDB10

# Rampage builder title -> (our submenu caption, command id prefix).
REGIONS = {
    "Teleport": ("Common Locations", "teleport.town"),
    "Camps and Safe Houses": ("Camps and Safe Houses", "teleport.camp"),
    "Mid-Eastern Locations": ("Mid-Eastern Locations", "teleport.mideast"),
    "Saint Denis Locations": ("Saint Denis Locations", "teleport.saintdenis"),
    "North East Locations": ("North East Locations", "teleport.northeast"),
    "Northern Locations": ("Northern Locations", "teleport.north"),
    "North West Locations": ("North West Locations", "teleport.northwest"),
    "South West Locations": ("South West Locations", "teleport.southwest"),
    "Off-land Locations": ("Off-land Locations", "teleport.offland"),
    "Unfinished or Out-of-bounds": ("Unfinished or Out-of-bounds", "teleport.unfinished"),
    "Special & Secret": ("Special & Secret", "teleport.special"),
}
SHOPS = "Shops and Services"

STR = r'"([^"]*)"'


def strlit(name):
    ea = idc.get_name_ea_simple(name)
    raw = ida_bytes.get_strlit_contents(ea, -1, ida_nalt.STRTYPE_C)
    return raw.decode("utf-8") if raw else "{%s}" % name


def parse_builder(text):
    """(kind, label, xyz) for each title/section/location call, in order."""
    rows, buf = [], []
    api = "sub_%X" % LOCATION_API
    for s in (l.strip() for l in text.split("\n")):
        m = re.search(api + r"\(\w+(?:\[0\])?, ([-\d.e]+), ([-\d.e]+), ([-\d.e]+)\)", s)
        if m:
            rows.append(("loc", "".join(buf), [float(v) for v in m.groups()]))
            buf = []
            continue
        if "sub_%X(" % TITLE_API in s or "sub_%X(" % SECTION_API in s:
            rows.append(("title" if "%X" % TITLE_API in s else "section", "".join(buf), None))
            buf = []
            continue
        if re.search(r"sub_1801E[C-F]\w+\(|sub_1801F0\w+\(", s):
            buf = []  # another menu API call closes a row
            continue
        m = re.search(r"strcpy\([^,]+, " + STR + r"\)", s) or re.search(r"sub_\w+\(\w+, " + STR + r"\);", s)
        if m:
            buf = [m.group(1)]
            continue
        m = re.search(r"sub_\w+\(\w+, &(qword_\w+)\);", s)
        if m:
            buf = [strlit(m.group(1))]
            continue
        m = re.search(r"qmemcpy\([^,]+, " + STR + r", (\d+)\)", s)
        if m:
            buf.append(m.group(1)[:int(m.group(2))])
            continue
        m = re.search(r"= \*\([\w ]+\*\*?\)" + STR, s)
        if m:
            buf.append(m.group(1))
            continue
        m = re.search(r"= \(LPVOID\)0x([0-9A-F]+)LL;", s)
        if m:
            buf.append(bytes.fromhex(m.group(1).rjust(16, "0"))[::-1].rstrip(b"\0").decode())
    return rows


def regions():
    out = []
    builders = sorted({ida_funcs.get_func(x).start_ea for x in idautils.CodeRefsTo(LOCATION_API, 0)
                       if ida_funcs.get_func(x)})
    found = {}
    for f in builders:
        rows = parse_builder(str(ida_hexrays.decompile(f)))
        titles = [label for kind, label, _ in rows if kind == "title"]
        if not titles or titles[0] not in REGIONS:
            continue
        found[titles[0]] = rows
    missing = set(REGIONS) - set(found)
    if missing:
        raise RuntimeError("builders not found: %s" % sorted(missing))
    for title in REGIONS:  # REGIONS order is the menu order
        menu, prefix = REGIONS[title]
        section = ""
        for kind, label, xyz in found[title]:
            if kind == "section":
                section = label
            elif kind == "loc":
                if not label:
                    raise RuntimeError("unlabelled location in %s at %s" % (title, xyz))
                out.append((menu, "", "" if title == "Teleport" else section, prefix, label, xyz))
    return out


def shops():
    ea = next(idautils.DataRefsTo(next(s.ea for s in idautils.Strings() if str(s) == "Photostudio")))
    text = str(ida_hexrays.decompile(ida_funcs.get_func(ea).start_ea))
    lines = [l.strip() for l in text.split("\n") if re.match(r"\s*v\d+ = ", l)]
    as_float = lambda v: struct.unpack("<f", struct.pack("<i", int(v)))[0]
    out, category, i = [], None, 0
    while i < len(lines):
        m = re.match(r'v\d+ = "([^"]*)";', lines[i])
        if m:
            nums = [re.match(r"v\d+ = (-?\d+);", l) for l in lines[i + 1:i + 4]]
            if len(nums) == 3 and all(nums):
                prefix = "teleport.shop." + re.sub(r"[^a-z0-9]", "", category.lower())
                out.append((SHOPS, category, "", prefix, m.group(1), [as_float(n.group(1)) for n in nums]))
                i += 4
                continue
            category = m.group(1)
        i += 1
    return out


def c_str(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def main(dst):
    rows = regions() + shops()
    with open(dst, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// Generated by tools/extract_rampage_teleports_ida.py from Rampage's\n")
        f.write("// teleport menus. Regenerate rather than edit.\n")
        f.write("// { submenu, nested submenu, section, id prefix, name, x, y, z }\n")
        for menu, sub, section, prefix, name, (x, y, z) in rows:
            f.write("{ %s, %s, %s, %s, %s, %.4ff, %.4ff, %.4ff },\n"
                    % (c_str(menu), c_str(sub), c_str(section), c_str(prefix), c_str(name), x, y, z))
    print("Teleports.inc: %d" % len(rows))


ida_auto.auto_wait()
try:
    main(idc.ARGV[1])
finally:
    ida_pro.qexit(0)
