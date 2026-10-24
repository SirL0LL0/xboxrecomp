#!/usr/bin/env python3
"""i18n_check.py - the overlay's words and their translations (src/overlay/i18n).

    python3 tools/i18n_check.py                  check every table
    python3 tools/i18n_check.py --missing fr     the English a table lacks, one a line
    python3 tools/i18n_check.py --keys           every English text the overlay translates

The English texts are the ones the code passes through TR() (src/overlay,
and a title's own with --also <file>), the tables of names drawn through it
(the k_meta rows' labels and help, the n_* choices, the page names, the
Mixer page's lists) and the folder browser's places (xst_browse.c). A table
is i18n/<lang>.inc: one { "English", "translation" }, pair a line, UTF-8.

The check: no English twice in a table, a translation's printf conversions
the same as its English's (same order, same types), and none missing (a
missing one shows English; reported, not an error, with --strict an error).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
OV = os.path.join(ROOT, "src", "overlay")
LANGS = ("fr", "de", "it")

LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
CONV = re.compile(r"%[-+#0]*(\*|\d+)?(\.(\*|\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfFgGaAcspn%])")
PAIR = re.compile(r'^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\},\s*$')


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


def groups(text):
    """Literal groups: (start, end, value), adjacent literals merged."""
    out, cur = [], None
    for m in LIT.finditer(text):
        if cur and text[cur[1]:m.start()].strip() == "":
            cur[1] = m.end()
            cur[2] += m.group(1)
        else:
            if cur:
                out.append(tuple(cur))
            cur = [m.start(), m.end(), m.group(1)]
    if cur:
        out.append(tuple(cur))
    return out


def tr_keys(path, macro="TR"):
    t = strip_comments(open(path, encoding="utf-8").read())
    keys = []
    for s, e, v in groups(t):
        before = t[max(0, s - len(macro) - 1):s]
        after = t[e:e + 1]
        if before == macro + "(" and after == ")":
            keys.append(v)
    return keys


TABLES = re.compile(r"static\s+(?:constexpr\s+)?(?:const\s+)?(?:char\s*\*\s*const|Meta)\s+"
                    r"(n_\w+|k_page_name|k_spk_name|k_meta|k_mx_\w+|sn|tn|dn|keys)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};",
                    re.S)


def table_keys(path):
    t = strip_comments(open(path, encoding="utf-8").read())
    keys = []
    for m in TABLES.finditer(t):
        if m.group(1) == "k_spk_short":
            continue
        for _, _, v in groups(m.group(2)):
            keys.append(v)
    return keys


# The folder browser's places (src/soundtrack/xst_browse.c), drawn through
# browser_label in overlay_ui.cpp.
BROWSER = ["Use this folder", "Turn custom music off", "Music", "Home", "Downloads",
           "This app's Music folder", "Internal storage", "Drive: ", "Current folder: ",
           "The folder it is in: ", "Card: ", "Computer: ", "Drive "]


def all_keys(also):
    keys = []
    for f in ("overlay_ui.cpp", "overlay_mixer.inc", "xbox_update.c"):
        keys += tr_keys(os.path.join(OV, f))
        keys += table_keys(os.path.join(OV, f))
    for f in also:
        keys += tr_keys(f, "BO3_TR")
    keys += BROWSER
    seen, out = set(), []
    for k in keys:
        # Words, not a number, a unit or a format alone.
        if not re.search(r"[A-Za-z]{2}", CONV.sub("", k)):
            continue
        if k not in seen:
            seen.add(k)
            out.append(k)
    return out


def read_table(lang):
    path = os.path.join(OV, "i18n", lang + ".inc")
    pairs, errors = [], []
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        s = line.strip()
        if not s or s.startswith("/*") or s.startswith("*") or s.startswith("//"):
            continue
        m = PAIR.match(line)
        if not m:
            errors.append("%s:%d: not a pair" % (path, n))
            continue
        pairs.append((m.group(1), m.group(2), n))
    return path, pairs, errors


def convs(s):
    return [m.group(5) for m in CONV.finditer(s) if m.group(5) != "%"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--missing")
    ap.add_argument("--keys", action="store_true")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--also", action="append", default=[], help="a title's source with BO3_TR(...)")
    a = ap.parse_args()
    keys = all_keys(a.also)
    if a.keys:
        for k in keys:
            print(k)
        return 0
    bad = 0
    for lang in LANGS:
        path, pairs, errors = read_table(lang)
        for e in errors:
            print(e)
            bad += 1
        have = {}
        for en, tr, n in pairs:
            if en in have:
                print("%s:%d: %r twice (line %d)" % (path, n, en[:60], have[en]))
                bad += 1
            have[en] = n
            if convs(en) != convs(tr):
                print("%s:%d: conversions differ: %r / %r" % (path, n, en[:60], tr[:60]))
                bad += 1
        missing = [k for k in keys if k not in have]
        if a.missing == lang:
            for k in missing:
                print(k)
            continue
        if missing:
            print("%s: %d of %d texts not translated" % (lang, len(missing), len(keys)))
            if a.strict:
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
