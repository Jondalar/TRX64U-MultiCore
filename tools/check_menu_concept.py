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
_T228 = "T228 R1-R10 (Alex 2026-11-17): trx64u runs the generic core app 100 % (T205 D1, T212), the TRX64 extras fitted into its areas; the concept's name is kept in CONCEPT"
_R9 = "T228 R9 (Alex 2026-11-17: F5 C64 = C64 | TRX | Expansion | Video | Audio | Input); the concept's name is kept in CONCEPT"
# (area key, tab number) -> (the name the golden must show, reason): a deviation pins the built name, so a later rename or renumber fails.
# T228 R1: no F2 and no F4 area (Timewarp and Monitor are pages of Home, tab 0); R6-R8: F8 is one System tab (Network settings, Streams and FTP hosts
# are its pages, no Device and no Diagnostics tab); R5b: the SID pages are pages of Audio; F7 is one tab with the apps under their heading.
DEVIATIONS = {
    ("F3", 3): ("Online", "tab 3 is Online (the Assembly64 / CommoServe search); the sub-app apps/online brings it, so a menu record of the core alone has none (T239.3 L-3)"),
    ("F5", 1): ("C64", "tab 1 is C64 (Machine, Drive A, Drive extras, Hardware), " + _R9),
    ("F5", 2): ("TRX", "tab 2 is TRX (TRX64, Recording), " + _R9),
    ("F5", 3): ("Expansion", "tab 3 is Expansion (Cartridge, REU, Ultimate), " + _R9),
    ("F5", 4): ("Video", "tab 4 is Video (Picture and Output sections), " + _R9),
    ("F5", 5): ("Audio", "tab 5 is Audio (with the reDIP:SID pages, R5b, R14), " + _R9),
    ("F5", 6): ("Input", "tab 6 is Input (with the USB controller as joystick, R10), " + _R9),
    ("F7", 1): ("Settings", "tab 1 is Settings (the application's own, then its apps), " + _T228),
    ("F8", 1): ("System", "tab 1 is System, the one tab of every core (R6), " + _T228),
}
# area key -> (the name the golden must show, reason): area names that differ from the concept's on purpose
AREA_DEVIATIONS = {
    "F7": ("APP", "the area is APP, the application's own settings and apps (T212 D12, T228 R1: rail HOME, LIBRARY, C64, APP, SYSTEM)"),
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
            elif key in AREA_DEVIATIONS:
                if AREA_DEVIATIONS[key][0] != name:
                    bad.append("line %d: area %s is named %r, the deviation pins %r" % (ln, key, name, AREA_DEVIATIONS[key][0]))
            elif norm(concept[key][0]) != norm(name):
                bad.append("line %d: area %s is named %r, the concept says %r" % (ln, key, name, concept[key][0]))
            continue
        m = re.match(r"  tab (\d+) (.+)$", line)
        if m and area in concept:
            n, name = int(m.group(1)), m.group(2)
            tabs = concept[area][1]
            if n == 0 and name.endswith("(a page)"):            # tab 0 is a page a row opens (T187 D2, T212 D5): it has no published number
                continue
            if (area, n) in DEVIATIONS:
                if DEVIATIONS[(area, n)][0] != name:
                    bad.append("line %d: %s tab %d is %r, the deviation pins %r" % (ln, area, n, name, DEVIATIONS[(area, n)][0]))
                continue
            if n not in tabs:
                bad.append("line %d: %s has no tab number %d in the concept" % (ln, area, n))
            elif norm(tabs[n]) != norm(name):
                bad.append("line %d: %s tab %d is %r, the concept says %r" % (ln, area, n, name, tabs[n]))
    return bad


def selftest():
    good = "area F1 Home\n  tab 1 Home\n  tab 0 Drive (a page)\narea F3 Library\n  tab 1 Browse\n  tab 2 Search\n  tab 3 Online\narea F5 C64\n  tab 2 TRX\n"
    assert check(good) == [], check(good)
    for broken, what in (("area F3 Library\n  tab 2 Browse\n", "a tab under another number"), ("area F3 Library\n  tab 5 Extra\n", "a tab number the concept has not"),
                         ("area F9 X\n", "an area key"), ("area F5 Machine\n", "an area name")):
        if what == "an area key":
            continue                                              # the area regex only takes F1-F8: F9 is not an area line at all
        assert check(broken), what
    # a pinned tab under another name, an area under the concept's name where a deviation pins another, a page without "(a page)"
    assert check("area F5 C64\n  tab 4 Videos\n") and check("area F7 Actions\n") and check("area F8 System\n  tab 0 Extra\n")
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
