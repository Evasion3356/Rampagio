"""Keep the translation tables (src/lang/<code>.inc) in step with the code.

    python tools/lang_sync.py                       report per language
    python tools/lang_sync.py --strings             print the English strings
    python tools/lang_sync.py --todo <code> [--ref <dir>] [--limit N]
                                                    missing strings as JSON
    python tools/lang_sync.py --merge <code> <batch.json>
                                                    add/replace translations
    python tools/lang_sync.py --prune <code>        drop stale entries

The English strings are the literals in the menu sources (src/menus,
Menu.cpp, GameUtil.cpp, Descriptions.cpp) that read as text rather than
identifiers, plus src/data/Descriptions.inc's descriptions, minus
src/lang/ignore.txt. A string that's never drawn costs nothing, so the
filter errs on keeping. Text built at runtime must use Tr/TrFormat with a
literal template (Localization.h) to be found.

The report flags entries whose {} placeholders or ~codes~ differ from the
English: TrFormat falls back to English when placeholders don't fit, and
a lost ~s~ leaves the colour on.

--todo prints [{"en": ..., "ref": {"es": ..., ...}}] for strings the
language lacks; --ref points at a folder of Rampage community language
files (<Translation Original=".." Replace=".."/>), whose wording for the
same English is shown as a reference only. They're never copied in
automatically; every table entry is written by hand. --merge takes
{"English": "translation", ...}.
"""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "src")
LANG = os.path.join(SRC, "lang")
CODES = ["fr", "de", "it", "es", "pt-BR", "pl", "ru", "ko", "zh-TW", "ja", "es-MX", "zh-CN"]
SOURCES = ["menus/*.cpp", "Menu.cpp", "GameUtil.cpp", "Descriptions.cpp"]

LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SKIP_LINE = re.compile(r"^\s*(//|#include)|Log::Write|Joaat\(|VAR_STRING|DataFile::|Ui::Id\(|assert\(|REQUEST_ANIM_DICT|TASK_PLAY_ANIM")
ENTRY = re.compile(r'^\{\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\},?\s*$')


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'"}.get(n, "\\" + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")


def looks_like_text(s):
    if not re.search(r"[A-Za-z]{2}", s):
        return False
    if re.fullmatch(r"[a-z0-9_.\-/\\:]+", s):        # ids, file names, dictionaries
        return False
    if re.fullmatch(r"[A-Z0-9_]+", s):               # game names (WEAPON_..., NAV_RIGHT)
        return False
    if " " not in s and re.search(r"[@_]|[a-z][A-Z]", s):  # anim dicts, CamelCase names
        return False
    if re.fullmatch(r"[\w.\-]+\.(json|xml|txt|ysc|ini|log|mp3|wav|ogg)", s, re.I):
        return False
    if s.startswith(("$", "<", "~s~<")) or "FONT FACE" in s or "&#" in s and len(s) < 12:
        return False
    if re.fullmatch(r"[\s{}:#x0-9A-F.,%()\-+/]*", s):  # format-only ("{} {}", "0x{:08X}")
        return False
    return True


def english_strings():
    ignore = set()
    path = os.path.join(LANG, "ignore.txt")
    if os.path.exists(path):
        ignore = {l.rstrip("\r\n").replace("\\n", "\n") for l in open(path, encoding="utf-8") if l.strip() and not l.startswith("#")}
    found = {}
    for pattern in SOURCES:
        for f in sorted(glob.glob(os.path.join(SRC, pattern))):
            for n, line in enumerate(open(f, encoding="utf-8"), 1):
                if SKIP_LINE.search(line):
                    continue
                for m in LITERAL.finditer(line):
                    s = unescape(m.group(1))
                    if looks_like_text(s) and s not in ignore:
                        found.setdefault(s, "%s:%d" % (os.path.relpath(f, SRC), n))
    inc = os.path.join(SRC, "data", "Descriptions.inc")
    for n, line in enumerate(open(inc, encoding="utf-8"), 1):
        m = re.match(r'\{\s*"(?:[^"\\]|\\.)*",\s*"(?:[^"\\]|\\.)*",\s*"((?:[^"\\]|\\.)*)"\s*\}', line)
        if m:
            found.setdefault(unescape(m.group(1)), "data/Descriptions.inc:%d" % n)
    return found


def load_table(code):
    table = {}
    path = os.path.join(LANG, code + ".inc")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            m = ENTRY.match(line.strip())
            if m:
                table[unescape(m.group(1))] = unescape(m.group(2))
    return table


def save_table(code, table):
    path = os.path.join(LANG, code + ".inc")
    head = "// %s: { English, translation }, sorted by English. Written by hand; tools/lang_sync.py checks it." % code
    lines = [head] + ['{ "%s", "%s" },' % (escape(k), escape(v)) for k, v in sorted(table.items(), key=lambda kv: kv[0].lower())]
    with open(path, "w", encoding="utf-8", newline="\r\n") as fp:
        fp.write("\n".join(lines) + "\n")


def placeholders(s):
    return sorted(re.findall(r"\{[^{}]*\}", s.replace("{{", "").replace("}}", "")))


def codes(s):
    return sorted(re.findall(r"~[^~\s]*~", s))


def problems(en, tr):
    out = []
    if placeholders(en) != placeholders(tr):
        out.append("placeholders %s vs %s" % (placeholders(en), placeholders(tr)))
    if codes(en) != codes(tr):
        out.append("codes %s vs %s" % (codes(en), codes(tr)))
    return out


def community(ref_dir):
    """Rampage community files: {lang: {English: translation}}, by folder name."""
    names = {"Spanish": "es", "French": "fr", "Korean": "ko", "Russian": "ru", "Chinese - Simplified": "zh-CN",
             "Chinese - Traditional": "zh-TW"}  # Thai and Turkish aren't game languages
    refs = {}
    for path in glob.glob(os.path.join(ref_dir, "**", "*.xml"), recursive=True):
        base = os.path.basename(path)
        lang = next((v for k, v in names.items() if k.lower() in path.lower()), None)
        if not lang or "IPL" in base:
            continue
        text = open(path, encoding="utf-8-sig", errors="replace").read()
        pairs = re.findall(r'<Translation\s+Original="([^"]*)"\s+Replace="([^"]*)"\s*/>', text)
        table = refs.setdefault(lang if lang != "ru" else "ru%d" % len([k for k in refs if k.startswith("ru")]), {})
        for o, r in pairs:
            o = o.replace("&amp;", "&").replace("&quot;", '"')
            table.setdefault(o, r.replace("&amp;", "&").replace("&quot;", '"'))
    return refs


def main(args):
    strings = english_strings()
    if "--strings" in args:
        for s, where in sorted(strings.items(), key=lambda kv: kv[1]):
            print("%-34s %s" % (where, json.dumps(s, ensure_ascii=False)))
        return
    if "--todo" in args:
        code = args[args.index("--todo") + 1]
        limit = int(args[args.index("--limit") + 1]) if "--limit" in args else 0
        refs = community(args[args.index("--ref") + 1]) if "--ref" in args else {}
        table = load_table(code)
        if code == "es-MX":
            table.update(load_table("es"))
        todo = []
        for s, where in sorted(strings.items(), key=lambda kv: kv[1]):
            if s in table:
                continue
            item = {"en": s, "at": where}
            r = {k: v[s] for k, v in refs.items() if s in v}
            if not r:
                low = s.lower()
                for k, v in refs.items():
                    hit = next((t for o, t in v.items() if o.lower() == low), None)
                    if hit:
                        r[k] = hit
            if r:
                item["ref"] = r
            todo.append(item)
            if limit and len(todo) >= limit:
                break
        print(json.dumps(todo, ensure_ascii=False, indent=0))
        return
    if "--batch" in args:
        # --batch <count> <out.json> [--ref dir]: the next strings some
        # language still lacks, numbered, with the references.
        count, out = int(args[args.index("--batch") + 1]), args[args.index("--batch") + 2]
        refs = community(args[args.index("--ref") + 1]) if "--ref" in args else {}
        tables = {c: load_table(c) for c in CODES}
        todo = []
        for s, where in sorted(strings.items(), key=lambda kv: kv[1]):
            if all(s in tables[c] or (c == "es-MX" and s in tables["es"]) for c in CODES):
                continue
            item = {"i": len(todo), "en": s, "at": where}
            r = {k: v[s] for k, v in refs.items() if s in v}
            if not r:
                low = s.lower()
                for k, v in refs.items():
                    hit = next((t for o, t in v.items() if o.lower() == low), None)
                    if hit:
                        r[k] = hit
            if r:
                item["ref"] = r
            todo.append(item)
            if len(todo) >= count:
                break
        with open(out, "w", encoding="utf-8") as fp:
            fp.write("[\n" + ",\n".join(json.dumps(x, ensure_ascii=False) for x in todo) + "\n]\n")
        print("%d strings -> %s" % (len(todo), out))
        return
    if "--merge-batch" in args:
        # --merge-batch <batch.json> <translations.json>: translations are
        # {"<i>": {"fr": ..., "de": ..., ...}}; null sends the string to
        # ignore.txt (not text). es-MX only where it differs from es.
        batch = json.load(open(args[args.index("--merge-batch") + 1], encoding="utf-8"))
        tr = json.load(open(args[args.index("--merge-batch") + 2], encoding="utf-8"))
        by_index = {str(b["i"]): b["en"] for b in batch}
        tables = {c: load_table(c) for c in CODES}
        ignored = []
        for i, langs in tr.items():
            en = by_index[i]
            if langs is None:
                ignored.append(en)
                continue
            for code, text in langs.items():
                if code not in tables:
                    print("unknown language %s for #%s" % (code, i))
                    continue
                if code == "es-MX" and text == langs.get("es", tables["es"].get(en)):
                    continue
                tables[code][en] = text
                for p in problems(en, text):
                    print("%s #%s %r: %s" % (code, i, en, p))
            missing = [c for c in CODES if c != "es-MX" and c not in langs and en not in tables[c]]
            if missing:
                print("#%s %r: no %s" % (i, en, ", ".join(missing)))
        for code, table in tables.items():
            save_table(code, table)
        if ignored:
            path = os.path.join(LANG, "ignore.txt")
            with open(path, "a", encoding="utf-8", newline="\r\n") as fp:
                for en in ignored:
                    fp.write(en.replace("\n", "\\n") + "\n")
        print("merged %d strings, ignored %d" % (len(tr) - len(ignored), len(ignored)))
        return
    if "--merge" in args:
        code, batch = args[args.index("--merge") + 1], args[args.index("--merge") + 2]
        table = load_table(code)
        new = json.load(open(batch, encoding="utf-8"))
        unknown = [k for k in new if k not in strings]
        for k, v in new.items():
            table[k] = v
        save_table(code, table)
        for k in unknown:
            print("not an English string (kept anyway): %r" % k)
        for k, v in new.items():
            for p in problems(k, v):
                print("%s: %r: %s" % (code, k, p))
        print("%s: merged %d, now %d entries" % (code, len(new), len(table)))
        return
    if "--prune" in args:
        code = args[args.index("--prune") + 1]
        table = load_table(code)
        kept = {k: v for k, v in table.items() if k in strings}
        save_table(code, kept)
        print("%s: dropped %d stale entries" % (code, len(table) - len(kept)))
        return
    print("%d English strings" % len(strings))
    for code in CODES:
        table = load_table(code)
        have = dict(table)
        if code == "es-MX":
            have.update({k: v for k, v in load_table("es").items() if k not in have})
        missing = [s for s in strings if s not in have]
        stale = [k for k in table if k not in strings]
        bad = [(k, p) for k, v in table.items() for p in problems(k, v)]
        print("%-6s %5d entries  %5d missing  %4d stale  %3d mismatched" % (code, len(table), len(missing), len(stale), len(bad)))
        for k, p in bad[:10]:
            print("         %r: %s" % (k, p))


if __name__ == "__main__":
    main(sys.argv[1:])
