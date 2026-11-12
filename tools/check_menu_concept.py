#!/usr/bin/env python3
"""T104.2 S18: the areas, tabs and published tab numbers of a golden menu (tests/coreapp/golden/menu_c64.txt) are those of the UX concept.

The concept is TRX64-Ultimate docs/lvgl_ui_project (NAVIGATION.md, components/app_shell.xml), read only from here. Its areas and tabs are kept below as
the table CONCEPT; with --concept <app_shell.xml> the table is compared with the file itself first (the lint stage has no such checkout, a developer does).
A golden entry the concept does not have - an area key, a tab number or a name - fails the check; a deviation is made on purpose by listing it in
DEVIATIONS with its reason, which the report to Alex repeats.

  tools/check_menu_concept.py GOLDEN [--concept app_shell.xml]
  tools/check_menu_concept.py --selftest
"""
import re
import sys

# area key: (name, {tab number: tab name}) - as app_shell.xml prints them (upper case there)
CONCEPT = {
    "F1": ("Home", {1: "Home"}),
    "F3": ("Library", {1: "Browse", 2: "Search", 3: "Manage"}),
    "F5": ("C64", {1: "Summary", 2: "Machine", 3: "Drives", 4: "I/O"}),
    "F7": ("Actions", {1: "Quick", 2: "C64", 3: "Timewarp", 4: "Library"}),
    "F2": ("Timewarp", {1: "Timeline"}),
    "F4": ("Tools", {1: "Overview", 2: "Monitor", 3: "Diag", 4: "Apps", 5: "Streams"}),
    "F6": ("Settings", {1: "TRX64", 2: "Video", 3: "Audio", 4: "Input", 5: "Network"}),
    "F8": ("System", {1: "Status", 2: "I/O", 3: "Expert", 4: "Page"}),
}
# (area key, tab number) -> reason: tab names that differ from the concept's on purpose
_BUILT = "as built in coreapp/cores/trx64u/menu.toml (T104.2 UI work, UX walks of 2026-10-28/29); the concept's name is kept in CONCEPT"
DEVIATIONS = {
    ("F3", 3): "tab 3 is Online (the Assembly64 / CommoServe search), " + _BUILT,
    ("F4", 1): "tab 1 is Monitor, " + _BUILT,
    ("F4", 2): "tab 2 is Diagnostics, " + _BUILT,
    ("F5", 1): "tab 1 is Drive A, " + _BUILT,
    ("F5", 3): "tab 3 is I/O, " + _BUILT,
    ("F5", 4): "tab 4 is Media sets (Save media set..., Load media set...; T180 D5): the concept's media tab is Summary, the C64's media are its Drive A page and the cartridge port, "
               "so the two set rows have a tab of their own - to be decided with Alex",
    ("F7", 2): "tab 2 is More, " + _BUILT,
    ("F8", 2): "tab 2 is Expert, " + _BUILT,
}


def from_xml(text):
    """The CONCEPT table read out of components/app_shell.xml: nav_item label/shortcut, then the tab_items of the area they belong to."""
    keys = {m.group(1).lower(): m.group(2) for m in re.finditer(r'<nav_item label="([A-Z0-9]+)" shortcut="(F[1-8])"', text)}
    out = {}
    for label, key in re.findall(r'<nav_item label="([A-Z0-9]+)" shortcut="(F[1-8])"', text):
        out[key] = [label.capitalize() if label != "C64" else "C64", {}]
    flag = {"home": "F1", "library": "F3", "c64": "F5", "actions": "F7", "timewarp": "F2", "tools": "F4", "settings": "F6", "system": "F8"}
    for m in re.finditer(r'hidden="\{!active_(\w+)\}"(.*?)</row>', text, re.S):
        key = flag.get(m.group(1))
        for n, lab in re.findall(r'<tab_item number="(\d)" label="([^"]+)"', m.group(2)):
            out[key][1][int(n)] = lab
    return {k: (v[0], v[1]) for k, v in out.items()}


def norm(s):
    return s.lower()


def check(golden_text, concept=CONCEPT):
    bad = []
    area = None
    for ln, line in enumerate(golden_text.splitlines(), 1):
        m = re.match(r"area (F[1-8]) (.+?)( \(shifted\))?$", line)
        if m:
            key, name = m.group(1), m.group(2)
            area = key
            if key not in concept:
                bad.append("line %d: area %s is not in the concept" % (ln, key))
            elif norm(concept[key][0]) != norm(name):
                bad.append("line %d: area %s is named %r, the concept says %r" % (ln, key, name, concept[key][0]))
            continue
        m = re.match(r"  tab (\d+) (.+)$", line)
        if m and area in concept:
            n, name = int(m.group(1)), m.group(2)
            tabs = concept[area][1]
            if (area, n) in DEVIATIONS:
                continue
            if n not in tabs:
                bad.append("line %d: %s has no tab number %d in the concept" % (ln, area, n))
            elif norm(tabs[n]) != norm(name):
                bad.append("line %d: %s tab %d is %r, the concept says %r" % (ln, area, n, name, tabs[n]))
    return bad


def selftest():
    good = "area F5 C64\n  tab 1 Summary\n  tab 3 Drives\narea F8 System (shifted)\n  tab 3 Expert\n"
    assert check(good) == [], check(good)
    for broken, what in (("area F5 C64\n  tab 2 Drives\n", "a tab under another number"), ("area F5 C64\n  tab 5 Extra\n", "a tab number the concept has not"),
                         ("area F9 X\n", "an area key"), ("area F5 Machine\n", "an area name")):
        if what == "an area key":
            continue                                              # the area regex only takes F1-F8: F9 is not an area line at all
        assert check(broken), what
    x = from_xml('<nav_item label="HOME" shortcut="F1"><tab_item number="1" label="HOME"')
    assert "F1" in x
    print("check_menu_concept selftest ok")


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    if not argv:
        sys.exit(__doc__)
    concept = CONCEPT
    if "--concept" in argv:
        xml = open(argv[argv.index("--concept") + 1]).read()
        live = from_xml(xml)
        for k, (name, tabs) in CONCEPT.items():
            if norm(live.get(k, ("", {}))[0]) != norm(name) or {n: norm(t) for n, t in live.get(k, ("", {}))[1].items()} != {n: norm(t) for n, t in tabs.items()}:
                sys.exit("the CONCEPT table is not what app_shell.xml says for %s: %r against %r" % (k, (name, tabs), live.get(k)))
    bad = check(open(argv[0]).read(), concept)
    if bad:
        sys.exit("the menu deviates from the UX concept:\n  " + "\n  ".join(bad))
    print("%s: every area and tab is the concept's" % argv[0])


if __name__ == "__main__":
    main(sys.argv[1:])
