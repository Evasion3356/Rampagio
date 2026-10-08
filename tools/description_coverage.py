"""Which of Rampagio's menu rows have a description, read from the source.

    python tools/description_coverage.py [--missing] [--unused]

Finds rows by their literal captions in src/menus/*.cpp and the menu they
go in (a variable assigned from Ui::Submenu/ListMenu/... with a literal
title, or Ui::Root() for Home), then looks each (menu, caption) up the way
src/Descriptions.cpp does: kOurs, then src/data/Descriptions.inc, then the
same under the menu's kMenuAliases title. Rows built in loops or
with computed captions aren't seen.

--missing lists rows with no description; --unused lists table entries no
row matched (usually a menu or caption named differently from Rampage's).
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
STR = r'"((?:[^"\\]|\\.)*)"'
ROW_FUNCS = "Action|Do|Toggle|Looped|Number|Choice|Text"
MENU_FUNCS = "Submenu|ListMenu|DetachedListMenu|NameList"


def unquote(s):
    return s.replace('\\"', '"').replace("\\\\", "\\").replace("\\n", "\n")


def norm(s):
    return re.sub(r"~[^~]*~", "", s).strip().lower()


def load_table(path):
    entries = {}
    if os.path.exists(path):
        for m in re.finditer(r"\{\s*" + STR + r",\s*" + STR + r",\s*" + STR + r"\s*\}", open(path, encoding="utf-8").read()):
            entries[(norm(unquote(m.group(1))), norm(unquote(m.group(2))))] = unquote(m.group(3))
    return entries


def main(args):
    table = load_table(os.path.join(SRC, "data", "Descriptions.inc"))
    ours = load_table(os.path.join(SRC, "Descriptions.cpp"))

    files = {p: open(p, encoding="utf-8").read() for p in sorted(glob.glob(os.path.join(SRC, "menus", "*.cpp")))}

    def local_menus(text):
        menus = {"root": "Home"}
        for m in re.finditer(r"(\w+)\s*=\s*Ui::(?:%s)\(\s*[^,()]*(?:\([^()]*\))?\s*,\s*%s" % (MENU_FUNCS, STR), text):
            menus[m.group(1)] = unquote(m.group(2))
        for m in re.finditer(r"(\w+)\s*=\s*Ui::(?:DetachedListMenu)\(\s*%s" % STR, text):
            menus[m.group(1)] = unquote(m.group(2))
        for m in re.finditer(r"(\w+)\s*=\s*Ui::Root\(\)", text):
            menus[m.group(1)] = "Home"
        return menus

    # A builder's MenuBase* parameter is the menu its callers pass.
    params = {}
    for _ in range(3):
        for text in files.values():
            menus = local_menus(text)
            menus.update({k: v for (f, k), v in params.items() if re.search(r"\b%s\s*\(\s*MenuBase\*\s*%s\b" % (f, k), text)})
            for m in re.finditer(r"\b(Build\w+)\(\s*(\w+)\s*\)", text):
                if m.group(2) in menus:
                    for t in files.values():
                        d = re.search(r"\b%s\(\s*MenuBase\*\s*(\w+)\s*\)" % m.group(1), t)
                        if d:
                            params[(m.group(1), d.group(1))] = menus[m.group(2)]

    rows = []
    for path, text in files.items():
        menus = local_menus(text)
        menus.update({k: v for (f, k), v in params.items() if re.search(r"\b%s\s*\(\s*MenuBase\*\s*%s\b" % (f, k), text)})
        # Submenus are rows of their parent too.
        for m in re.finditer(r"Ui::(?:%s|Link)\(\s*(\w+)\s*,\s*%s" % (MENU_FUNCS, STR), text):
            if m.group(1) in menus:
                rows.append((os.path.basename(path), menus[m.group(1)], unquote(m.group(2))))
        for m in re.finditer(r"Ui::(?:%s|Section)\(\s*(\w+)\s*,\s*(?:\"[a-z0-9_.]+\"\s*,\s*)?%s" % (ROW_FUNCS, STR), text):
            if m.group(1) in menus and not re.fullmatch(r"[a-z0-9_.]+", m.group(2)):
                rows.append((os.path.basename(path), menus[m.group(1)], unquote(m.group(2))))

    src = open(os.path.join(SRC, "Descriptions.cpp"), encoding="utf-8").read()
    aliases = {norm(unquote(a)): norm(unquote(b)) for a, b in re.findall(r"\{\s*%s,\s*%s\s*\}" % (STR, STR), src)}
    found, missing, used = 0, [], set()
    for f, menu, cap in rows:
        key = (norm(menu), norm(cap))
        alias = (aliases.get(key[0]), key[1])
        if key in ours or key in table:
            found += 1
            used.add(key)
        elif alias in table:
            found += 1
            used.add(alias)
        else:
            missing.append((f, menu, cap))
    print("%d rows seen, %d with a description, %d without" % (len(rows), found, len(missing)))
    print("%d table entries, %d matched" % (len(table), len(used & set(table))))
    if "--missing" in args:
        for f, menu, cap in missing:
            print("  missing  %-22s %-28s %s" % (f, menu, cap))
    if "--unused" in args:
        for (menu, cap) in sorted(set(table) - used):
            print("  unused   %-28s %-34s %s" % (menu, cap, table[(menu, cap)].split("\n")[0]))


if __name__ == "__main__":
    main(sys.argv[1:])
