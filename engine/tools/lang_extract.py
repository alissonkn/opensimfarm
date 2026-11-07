#!/usr/bin/env python3
# OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
# Licensed under the GNU General Public License version 3 (see LICENSE.txt).
"""The list of texts to translate, and the state of each language.

    python3 engine/tools/lang_extract.py            # writes engine/lang/opensimfarm.pot from the sources
    python3 engine/tools/lang_extract.py --update   # also adds the new texts to every engine/lang/*.po (empty
                                                    # translations) and moves texts no longer used to the end
    python3 engine/tools/lang_extract.py --check    # reports, per language, texts not translated, texts no longer
                                                    # used and translations whose {0} {1} ... differ; exit 1 if any

The texts are the English ones in the code inside Tr("..."), TrF("...") and N_("..."). A new language: copy
opensimfarm.pot to engine/lang/<code>.po (for example es.po, fr.po, pt_BR.po), fill in the header's Language and
X-Language-Name (the language's name in its own words) and the msgstr lines, then build. The file can also go into the
"languages" folder of the settings folder, where the game reads it without a rebuild.

Texts of the player's game files are translated by id; they are not in the code, so this script keeps them in the
.po files as they are:
    crop:<file>        a crop's name (crop:corn.crp)
    crophelp:<file>    a crop's help card (BUY window, examined seed bag)
    event:<n>          message box n of events.dat; '~' starts a new line, a first line ended by '~' is a centred title
    item:<n>           item text n of itemtext.dat: the name, '~', the description
    card:<card>:<n>    text box n (counting from 0) of the Farm Expert's card <card>
A crop's information card is translated by its parts (the cropinfo:... texts in the code).
"""
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ENGINE, "src")
LANG = os.path.join(ENGINE, "lang")
CALL = re.compile(r'\b(?:Tr|TrF|N_)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
HOLES = re.compile(r"\{\d\}")


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            e = s[i + 1]
            out.append({"n": "\n", "t": "\t"}.get(e, e))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")


def extract():
    """English text -> list of files using it, in the order first seen."""
    found = {}
    for root, _, files in sorted(os.walk(SRC)):
        for name in sorted(files):
            if not name.endswith((".cpp", ".h")):
                continue
            path = os.path.join(root, name)
            rel = os.path.relpath(path, ENGINE)
            text = open(path, encoding="utf-8").read()
            for m in CALL.finditer(text):
                s = unescape("".join(LIT.findall(m.group(1))))
                if not s or s.startswith("#define"):
                    continue
                found.setdefault(s, [])
                if rel not in found[s]:
                    found[s].append(rel)
    return found


def entry(msgid, msgstr="", refs=(), comment=None):
    lines = []
    if comment:
        lines += ["# " + c for c in comment]
    for r in refs:
        lines.append("#: " + r)
    for key, val in (("msgid", msgid), ("msgstr", msgstr)):
        if "\n" in val:
            parts = val.split("\n")
            lines.append(f'{key} ""')
            for k, p in enumerate(parts):
                lines.append('"' + escape(p + ("\n" if k < len(parts) - 1 else "")) + '"')
        else:
            lines.append(f'{key} "{escape(val)}"')
    return "\n".join(lines) + "\n"


def parse_po(path):
    """(header text, [(msgid, msgstr, comment lines)]) of a .po file."""
    header, entries = "", []
    msgid = msgstr = None
    cur = None
    comments = []

    def flush():
        nonlocal msgid, msgstr, header, comments
        if msgid is not None:
            if msgid == "":
                header = msgstr
            else:
                entries.append((msgid, msgstr or "", [c for c in comments if not c.startswith("#:")]))
        msgid = msgstr = None
        comments = []

    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n").rstrip("\r")
        s = line.strip()
        if not s:
            continue
        if s.startswith("#"):
            if msgstr is not None:
                flush()
            comments.append(s)
            continue
        if s.startswith("msgid "):
            flush()
            msgid = unescape("".join(LIT.findall(s)))
            cur = "id"
        elif s.startswith("msgstr "):
            msgstr = unescape("".join(LIT.findall(s)))
            cur = "str"
        elif s.startswith('"'):
            if cur == "id":
                msgid += unescape("".join(LIT.findall(s)))
            elif cur == "str":
                msgstr += unescape("".join(LIT.findall(s)))
    flush()
    return header, entries


POT_HEADER = (
    "Project-Id-Version: OpenSimFarm\n"
    "MIME-Version: 1.0\n"
    "Content-Type: text/plain; charset=UTF-8\n"
    "Content-Transfer-Encoding: 8bit\n"
    "Language: \n"
    "X-Language-Name: \n"
)


def write_pot(found):
    out = ["# OpenSimFarm: the texts to translate (made by engine/tools/lang_extract.py - do not edit).\n"
           "# Copy this file to <language code>.po and fill in the msgstr lines. {0}, {1} ... stand for numbers and\n"
           "# names filled in by the game; a translation may put them in another order. \\n starts a new line.\n",
           entry("", POT_HEADER)]
    for s, refs in found.items():
        out.append("\n" + entry(s, "", refs))
    open(os.path.join(LANG, "opensimfarm.pot"), "w", encoding="utf-8").write("".join(out))


ID_KINDS = ("crop:", "crophelp:", "event:", "item:", "card:")


def is_id(s):
    return s.startswith(ID_KINDS)


def update_po(path, found):
    header, entries = parse_po(path)
    have = {e[0]: e for e in entries}
    out = [entry("", header)]
    for s, refs in found.items():
        tr, comment = (have[s][1], have[s][2]) if s in have else ("", [])
        out.append("\n" + entry(s, tr, refs, [c.lstrip("# ") for c in comment]))
    ids = [e for e in entries if is_id(e[0])]
    if ids:
        out.append("\n# Texts of the player's game files, by id\n")
        for s, tr, comment in ids:
            out.append("\n" + entry(s, tr))
    gone = [e for e in entries if e[0] not in found and not is_id(e[0])]
    if gone:
        out.append("\n# No longer used by the game (kept for reference)\n")
        for s, tr, comment in gone:
            out.append("\n" + "\n".join("#~ " + l for l in entry(s, tr).rstrip("\n").split("\n")) + "\n")
    open(path, "w", encoding="utf-8").write("".join(out))


def check(found):
    bad = False
    for name in sorted(os.listdir(LANG)):
        if not name.endswith(".po"):
            continue
        header, entries = parse_po(os.path.join(LANG, name))
        have = {e[0]: e[1] for e in entries}
        missing = [s for s in found if not have.get(s)]
        unused = [s for s in have if s not in found and not is_id(s)]
        holes = [s for s, t in have.items() if t and sorted(HOLES.findall(s)) != sorted(HOLES.findall(t))]
        print(f"{name}: {len(found) - len(missing)} of {len(found)} texts translated, {len(unused)} no longer used, "
              f"{len(holes)} with different {{0}} {{1}} ...")
        for s in missing[:20]:
            print("   not translated:", repr(s))
        for s in holes:
            print("   placeholders differ:", repr(s))
        bad = bad or bool(missing or unused or holes)
    return bad


def main():
    found = extract()
    os.makedirs(LANG, exist_ok=True)
    write_pot(found)
    print(f"{len(found)} texts -> engine/lang/opensimfarm.pot")
    if "--update" in sys.argv:
        for name in sorted(os.listdir(LANG)):
            if name.endswith(".po"):
                update_po(os.path.join(LANG, name), found)
                print("updated", name)
    if "--check" in sys.argv:
        sys.exit(1 if check(found) else 0)


if __name__ == "__main__":
    main()
