"""The step definitions (D5 of docs/specs/T153-ui-sdk-and-tests.md). A step is a regular expression and a function(ctx, *groups).

Since T192.3 (Alex, 2026-11-09) every step reads the UI service's snapshot (GET /v1/ui/state, contract 2: `rev`, `areas`, `tabs`, `items`, `dialog`, `topbar`,
`footer`, `status`, `path`, `sheet`, `medium`); there is no v1 state any more. The keys still go in as the board's keyboard (the emulator's control port), which is
the service's local key intent. The table old -> new is in docs/specs/T192.3-core-app-on-core.md section 5.

An assertion that is false raises AssertionFailed with the expected and the actual value; anything else that goes wrong (a key the menu did not take, a
state that never came, an emulator that ended) is a StepError, which is an error of the test and not a verdict on the UI.
"""
import json
import os
import subprocess
import sys
import tempfile

import emu as emu_mod

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
STEPS = []


class AssertionFailed(Exception):
    pass


class StepError(Exception):
    pass


def step(pattern):
    import re

    def deco(fn):
        STEPS.append((re.compile(pattern), fn))
        return fn
    return deco


# The C64 keys the control port knows (emu.py), by the name a feature uses.
KEYS = {"left-arrow": "larrow", "up-arrow": "uparrow", "run/stop": "runstop", "return": "return", "space": "space", "cursor-left": "left",
        "cursor-right": "right", "cursor-up": "up", "cursor-down": "down", "home": "home", "del": "del", "ctrl+a": "ctrl+a", "ctrl+c": "ctrl+c",
        "ctrl+v": "ctrl+v", "ctrl+n": "ctrl+n"}


def key_of(name):
    n = name.strip()
    if n.lower() in KEYS:
        return KEYS[n.lower()]
    if len(n) == 2 and n[0] in "Ff" and n[1] in "12345678":
        return n.lower()
    if len(n) == 1 and n.isalnum():
        return n.lower()
    raise StepError("a key of that name is not known: %s" % n)


def plain(label):
    """A button's letter is marked in its label, "(S)ave": the name of the button is the label without the brackets."""
    return label.replace("(", "").replace(")", "")


class Ctx:
    def __init__(self, with_emulator=True, core="trx64u"):
        self.with_emulator = with_emulator
        self.core = core
        self.noted = None
        self.emu = None
        self.s = None
        self.scenario = "scenario"
        self.pack = {}
        self.pack_result = None

    def start(self):
        if self.with_emulator:
            self.emu = emu_mod.Emu(core=self.core)
            self.emu.start()

    def stop(self):
        if self.emu:
            self.emu.stop()

    # ---- the application ----------------------------------------------------------------------------------------------------------
    def refresh(self):
        self.s = self.emu.state()
        return self.s

    def settle(self):
        """The menu took its keys when the revision stays put for two looks in a row (a background change - the clock, a medium - may move it once)."""
        last = self.s['rev']
        still = 0
        for _ in range(30):
            self.emu.wait(120)
            s = self.refresh()
            if s['rev'] == last:
                still += 1
                if still >= 2:
                    return s
            else:
                still = 0
                last = s['rev']
        raise StepError("the menu does not come to rest")

    def press(self, name):
        k = key_of(name)
        s0 = self.refresh()['rev']
        self.emu.key(k)
        self.emu.wait(150)
        for _ in range(40):
            self.emu.wait(60)
            if self.refresh()['rev'] != s0:
                return self.settle()
        raise AssertionFailed("the menu did not take the key %s (the revision stayed at %d, menu_open=%s)" % (name, s0, self.s.get('menu_open')))

    def open_menu(self):
        s = self.refresh()
        if s['menu_open']:
            self.press("RUN/STOP")
            s = self.refresh()
            if s['menu_open']:
                raise AssertionFailed("RUN/STOP did not close the menu")
        for _ in range(12):
            self.emu.cmd("button")
            self.emu.wait(500)
            if self.refresh()['menu_open']:
                return self.settle()
        raise AssertionFailed("the menu button did not open the menu")

    def attach_failure(self):
        if not self.emu:
            return None
        try:
            return os.path.relpath(self.emu.png("fail_" + self.scenario), ROOT)
        except Exception:
            return None

    # ---- reading the state ------------------------------------------------------------------------------------------------------
    def items(self):
        return self.s.get('items', [])

    def find_item(self, label):
        want = plain(label).lower()
        for it in self.items():
            if plain(it['title']).lower() == want:
                return it
        raise AssertionFailed("there is no item \"%s\" on this page; the page has: %s" % (label, ", ".join(plain(i['title']) for i in self.items())))

    def focused_label(self):
        for it in self.items():
            if it.get('focused'):
                return it['title']
        return None

    def active(self, key):
        """The active entry of `areas` or `tabs`."""
        for e in self.s.get(key, []):
            if e.get('active'):
                return e
        raise AssertionFailed("the snapshot has no active entry in %s" % key)

    def lib_rows(self):
        """The Library's rows: the items that are files or folders."""
        return [i for i in self.items() if i.get('kind') in ('dir', 'file')]

    def in_library(self):
        return 'path' in self.s


def expect_eq(what, want, got):
    if want != got:
        raise AssertionFailed("%s: expected %r, got %r" % (what, want, got))


# ---- Given / When: getting somewhere ---------------------------------------------------------------------------------------------------
@step(r'the menu is open')
def menu_is_open(c):
    c.open_menu()


@step(r'the menu is open on (F[1-8])')
def menu_is_open_on(c, f):
    c.open_menu()
    c.press(f)


@step(r'the menu is open on (F[1-8]) tab (\d)')
def menu_is_open_on_tab(c, f, n):
    c.open_menu()
    c.press(f)
    c.press(n)


@step(r'the menu is closed')
def menu_is_closed(c):
    expect_eq("menu_open", False, c.refresh()['menu_open'])


@step(r'the menu is shown')
def menu_is_shown(c):
    expect_eq("menu_open", True, c.refresh()['menu_open'])


@step(r'I press ([^ ]+)')
def i_press(c, key):
    c.press(key)


@step(r'I press ([^ ]+) (\d+) times')
def i_press_n(c, key, n):
    for _ in range(int(n)):
        c.press(key)


@step(r'I move to "([^"]+)"')
def i_move_to(c, label):
    """The focus goes there by the cursor keys: HOME, then DOWN until the focused item is the one asked for."""
    want = plain(label).lower()
    c.press("home")
    prev = None
    for _ in range(len(c.items()) + 2):
        cur = c.focused_label()
        if cur and plain(cur).lower() == want:
            return
        if cur == prev:
            break                                              # the end of the list
        prev = cur
        c.press("cursor-down")
    raise AssertionFailed("the focus never reached \"%s\" (it stands on %r)" % (label, c.focused_label()))


@step(r'I set "([^"]+)" to "([^"]+)"')
def i_set_to(c, label, value):
    i_move_to(c, label)
    for key in ("cursor-right", "cursor-left"):
        prev = None
        for _ in range(40):
            it = c.find_item(label)
            if it.get('value') == value:
                return
            if it.get('value') == prev:
                break                                          # the end of the choices in this direction
            prev = it.get('value')
            c.press(key)
        i_move_to(c, label)
    raise AssertionFailed("\"%s\" never showed \"%s\" (it shows %r)" % (label, value, c.find_item(label).get('value')))


@step(r'I step "([^"]+)" (up|down) (\d+) times')
def i_step_value(c, label, direction, n):
    i_move_to(c, label)
    for _ in range(int(n)):
        c.press("cursor-right" if direction == "up" else "cursor-left")


# ---- Then: the page ---------------------------------------------------------------------------------------------------------------------
@step(r'the area is "([^"]+)"')
def then_area(c, name):
    c.refresh()
    expect_eq("area", name, c.active('areas')['title'])


@step(r'the area key is (F[1-8])')
def then_area_key(c, k):
    c.refresh()
    expect_eq("area key", k, c.active('areas')['id'])


@step(r'the tab is "([^"]+)"')
def then_tab(c, name):
    c.refresh()
    expect_eq("tab", name, c.active('tabs')['title'])


@step(r'the tab number is (\d)')
def then_tab_n(c, n):
    c.refresh()
    expect_eq("tab number", int(n), c.active('tabs')['n'])


@step(r'the page title is "([^"]+)"')
def then_title(c, t):
    expect_eq("page title", t, c.refresh()['title'])


@step(r'the layout is "([^"]+)"')
def then_layout(c, l):
    expect_eq("layout", l, c.refresh()['layout'])


@step(r'the focused item is "([^"]+)"')
def then_focus(c, label):
    c.refresh()
    got = c.focused_label()
    if got is None or plain(got).lower() != plain(label).lower():
        raise AssertionFailed("focused item: expected %r, got %r" % (label, got))


@step(r'the footer shows "([^"]+)"')
def then_footer(c, text):
    leg = c.refresh()['footer']['legend']
    if text not in leg:
        raise AssertionFailed("footer: %r is not in %r" % (text, leg))


@step(r'the footer on the left shows "([^"]+)"')
def then_footer_left(c, text):
    expect_eq("footer left", text, c.refresh()['footer']['left'])


@step(r'the status line is "([^"]*)"')
def then_status(c, text):
    expect_eq("status line", text, c.refresh()['status']['text'])


@step(r'the status line says "([^"]+)"')
def then_status_has(c, text):
    got = c.refresh()['status']['text']
    if text not in got:
        raise AssertionFailed("status line: %r is not in %r" % (text, got))


@step(r'the footer does not show "([^"]+)"')
def then_no_footer(c, text):
    leg = c.refresh()['footer']['legend']
    if text in leg:
        raise AssertionFailed("footer: %r is in %r" % (text, leg))


@step(r'I note the status line')
def when_note_status(c):
    c.noted = c.refresh()['status']['text']


@step(r'the status line is as noted')
def then_status_noted(c):
    expect_eq("status line", c.noted, c.refresh()['status']['text'])


@step(r'the top bar shows "([^"]+)"')
def then_chip(c, text):
    chips = c.refresh()['topbar']['chips']
    if text not in chips:
        raise AssertionFailed("top bar: %r is not among %r" % (text, chips))


@step(r'the top bar does not show "([^"]+)"')
def then_no_chip(c, text):
    chips = c.refresh()['topbar']['chips']
    if text in chips:
        raise AssertionFailed("top bar: %r is among %r" % (text, chips))


@step(r'the activity is "([^"]+)"')
def then_activity(c, text):
    expect_eq("activity", text, c.refresh()['topbar']['activity'])


@step(r'a question is open: "([^"]+)"')
def then_question(c, text):
    d = c.refresh()['dialog']
    if not d:
        raise AssertionFailed("no question is open (dialog is null)")
    expect_eq("question", text, d['text'])


@step(r'no dialog is open')
def then_no_dialog(c):
    s = c.refresh()
    expect_eq("dialog", None, s['dialog'])
    expect_eq("modal", None, s.get('modal'))


@step(r'the dialog is "([a-z]+)"')
def then_dialog_kind(c, kind):
    s = c.refresh()
    got = "confirm" if s['dialog'] else (s['modal']['kind'] if s.get('modal') else "none")
    expect_eq("dialog kind", kind, got)


# ---- Then: items, settings, buttons ---------------------------------------------------------------------------------------------------
@step(r'the setting "([^"]+)" is "([^"]*)"')
def then_setting(c, label, value):
    c.refresh()
    it = c.find_item(label)
    expect_eq('setting "%s"' % label, value, it.get('value', ''))


@step(r'the button "([^"]+)" is (enabled|greyed)')
def then_button_state(c, label, state):
    c.refresh()
    it = c.find_item(label)
    if it['kind'] != 'button':
        raise AssertionFailed('"%s" is a %s, not a button' % (label, it['kind']))
    expect_eq('button "%s"' % label, state == 'enabled', it['enabled'])


@step(r'the button "([^"]+)" has the letter ([A-Z])')
def then_button_letter(c, label, letter):
    c.refresh()
    it = c.find_item(label)
    expect_eq('letter of button "%s"' % label, letter, it.get('bkey', ''))


@step(r'the item "([^"]+)" is in slot "([^"]+)"')
def then_slot(c, label, slot):
    c.refresh()
    expect_eq('slot of "%s"' % label, slot, c.find_item(label).get('slot', ''))


@step(r'the item "([^"]+)" shows "([^"]*)"')
def then_item_shows(c, label, text):
    c.refresh()
    it = c.find_item(label)
    got = it.get('value') or it.get('sub') or ''
    expect_eq('item "%s"' % label, text, got)


@step(r'the page has the items "([^"]+)"')
def then_items(c, labels):
    c.refresh()
    for l in labels.split(","):
        c.find_item(l.strip())


@step(r'the page has no item "([^"]+)"')
def then_no_item(c, label):
    c.refresh()
    want = plain(label).lower()
    if any(plain(i['title']).lower() == want for i in c.items()):
        raise AssertionFailed('the page has an item "%s"' % label)


# ---- Then: the Library ------------------------------------------------------------------------------------------------------------------
@step(r'the library is at "([^"]+)"')
def then_lib_path(c, path):
    expect_eq("library path", path, c.refresh().get('path'))


@step(r'the library lists "([^"]+)"')
def then_lib_lists(c, names):
    c.refresh()
    rows = [r['title'] for r in c.lib_rows()]
    for n in names.split(","):
        if n.strip() not in rows:
            raise AssertionFailed("the library does not list %r; it lists %r" % (n.strip(), rows))


@step(r'the file "([^"]+)" is marked')
def then_marked(c, name):
    c.refresh()
    rows = {r['title']: r for r in c.lib_rows()}
    if name not in rows:
        raise AssertionFailed("the library does not list %r" % name)
    expect_eq('mark of "%s"' % name, True, bool(rows[name].get('marked')))


@step(r'the file "([^"]+)" is not marked')
def then_not_marked(c, name):
    c.refresh()
    rows = {r['title']: r for r in c.lib_rows()}
    if name not in rows:
        raise AssertionFailed("the library does not list %r" % name)
    expect_eq('mark of "%s"' % name, False, bool(rows[name].get('marked')))


@step(r'I move to the file "([^"]+)"')
def i_move_to_file(c, name):
    c.press("home")
    prev = None
    for _ in range(60):
        cur = c.focused_label()
        if cur == name:
            return
        if cur == prev:
            break                                              # the end of the list
        prev = cur
        c.press("cursor-down")
    raise AssertionFailed("the cursor never reached %r (it stands on %r)" % (name, c.focused_label()))


def _focus_row(c, medium):
    """The cursor on the row of that name (case does not matter): the media list names its media in the file system's own case."""
    want = medium.lower()
    c.press("home")
    prev = None
    for _ in range(40):
        cur = c.focused_label()
        if cur and cur.lower().rstrip("/") == want:
            return
        if cur == prev:
            break
        prev = cur
        c.press("cursor-down")
    raise AssertionFailed("the media list has no %r (the cursor stands on %r)" % (medium, c.focused_label()))


def _up(c):
    """One folder up: the left arrow, on every core."""
    c.press("left-arrow")


def _to_media_level(c):
    for _ in range(10):
        path = c.refresh().get('path')
        if path == "/":
            return
        if c.active('areas')['id'] != "F3":
            raise AssertionFailed("going up left the Library (now %s)" % c.active('areas')['title'])
        _up(c)
    raise AssertionFailed("the Library never came up to the list of the media (it is at %r)" % c.s.get('path'))


@step(r'I go up to the top of the medium "([^"]+)"')
def i_go_up(c, medium):
    """The Library keeps the folder it was in. The media list is above every medium (the left arrow, RETURN on ../): from there the medium's row opens it."""
    want = "/" + medium
    path = c.refresh().get('path')
    if path and path.lower() == want.lower():
        return
    _to_media_level(c)
    _focus_row(c, medium)
    c.press("RETURN")
    expect_eq("library path", want.lower(), c.refresh().get('path', '').lower())
    for _ in range(8):                                         # a folder it was in is not the top
        if c.refresh().get('path', '').lower() == want.lower():
            return
        _up(c)


@step(r'the media list is up')
def then_media_list(c):
    c.refresh()
    expect_eq("library path", "/", c.s.get('path'))
    rows = [r['title'] for r in c.lib_rows()]
    if ".." in rows:
        raise AssertionFailed("the media list has a ../ row: %r" % rows)


@step(r'the medium "([^"]+)" is listed with "([^"]*)"')
def then_medium_row(c, medium, text):
    for r in (c.refresh() and c.lib_rows()):
        if r['title'].lower().rstrip("/") == medium.lower():
            if text.lower() not in r.get('sub', '').lower():
                raise AssertionFailed("the row of %r says %r, not %r" % (medium, r.get('sub'), text))
            return
    raise AssertionFailed("the media list has no %r: %r" % (medium, [r['title'] for r in c.lib_rows()]))


@step(r'the info panel says of "([^"]+)": (Size|Contents|Access) "([^"]+)"')
def then_medium_panel(c, medium, field, text):
    """The host's File Browser panel at "/": the medium under the cursor, its Size, Contents and Access (snapshot `medium`)."""
    m = c.refresh().get('medium')
    if not m:
        raise AssertionFailed("the media level shows no panel for the row under the cursor (%r)" % c.focused_label())
    expect_eq("panel medium", medium.lower(), m.get('name', '').lower())
    got = m.get(field.lower(), '')
    if text.lower() not in got.lower():
        raise AssertionFailed("the panel's %s of %r says %r, not %r" % (field, medium, got, text))


@step(r'I move to the medium "([^"]+)"')
def i_move_medium(c, medium):
    _to_media_level(c)
    _focus_row(c, medium)


@step(r'I open the medium "([^"]+)"')
def i_open_medium(c, medium):
    _to_media_level(c)
    _focus_row(c, medium)
    c.press("RETURN")


@step(r'I go back to the media list with the parent row')
def i_parent_row(c):
    _focus_row(c, "..")
    c.press("RETURN")


@step(r'the sheet lists "([^"]+)" with the letter ([A-Z0-9])')
def then_sheet(c, label, letter):
    c.refresh()
    if 'sheet' not in c.s:
        raise AssertionFailed("no sheet is open (dialog: %s)" % c.s['dialog'])
    items = {i['title']: i.get('key', '') for i in c.items() if i.get('kind') == 'action'}
    if label not in items:
        raise AssertionFailed("the sheet does not list %r; it lists %r" % (label, list(items)))
    expect_eq('letter of "%s"' % label, letter, items[label])


@step(r'the sheet lists "([^"]+)"')
def then_sheet_lists(c, label):
    c.refresh()
    if 'sheet' not in c.s:
        raise AssertionFailed("no sheet is open (dialog: %s)" % c.s['dialog'])
    rows = [i['title'] for i in c.items() if i.get('kind') == 'action']
    if label not in rows:
        raise AssertionFailed("the sheet does not list %r; it lists %r" % (label, rows))


@step(r'the sheet is open for "([^"]+)"')
def then_sheet_open(c, name):
    c.refresh()
    if 'sheet' not in c.s:
        raise AssertionFailed("no sheet is open (dialog: %s)" % c.s['dialog'])
    expect_eq("sheet file", name, c.s['sheet'].rsplit("/", 1)[-1])


@step(r'no sheet is open')
def then_no_sheet(c):
    if 'sheet' in c.refresh():
        raise AssertionFailed("a sheet is open for %s" % c.s['sheet'])


# ---- pack-time checks (no emulator): tools/descrec.py on a description made here ---------------------------------------------------------
@step(r'a menu description with the page "(F[1-8]/[a-z0-9_]+)"')
def given_pack_page(c, page):
    c.pack = {"page": page, "buttons": []}
    c.pack_result = None


@step(r'the page has the button "([^"]+)" with the (key|bkey) ([^ ]+)')
def given_pack_button(c, label, kind, letter):
    c.pack["buttons"].append((c.pack["page"], label, kind, letter))


@step(r'the page "(F[1-8]/[a-z0-9_]+)" has the button "([^"]+)" with the (key|bkey) ([^ ]+)')
def given_pack_button_on(c, page, label, kind, letter):
    c.pack["buttons"].append((page, label, kind, letter))


def pack_toml(pack):
    pages = {}
    for page, *_ in pack["buttons"]:
        pages[page] = None
    pages.setdefault(pack["page"], None)
    areas = {}
    for page in pages:
        a, t = page.split("/")
        areas.setdefault(a, []).append(t)
    out = []
    for a, tabs in sorted(areas.items()):
        out.append('[[area]]\nkey = "%s"\nname = "A%s"' % (a, a))
        for t in tabs:
            out.append('  [[area.tab]]\n  id = "%s"\n  name = "%s"\n  widget = "nodes"\n  layout = "split"' % (t, t))
    for page, label, kind, letter in pack["buttons"]:
        out.append('[[node]]\nanchor = "%s/bar"\nkind = "op"\nlabel = "%s"\nop = "x.y"\nui = { %s = "%s" }' % (page, label, kind, letter))
    return "\n".join(out) + "\n"


def run_pack(c):
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "menu.toml")
        open(src, "w").write(pack_toml(c.pack))
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "descrec.py"), "compile", src, os.path.join(d, "menu.bin")],
                           capture_output=True, text=True, timeout=60)
    c.pack_result = (r.returncode, (r.stdout + r.stderr).strip())


@step(r'I pack it')
def when_pack(c):
    run_pack(c)


@step(r'packing is refused with "([^"]+)"')
def then_refused(c, text):
    code, out = c.pack_result
    if code == 0:
        raise AssertionFailed("packing was not refused; it should say %r" % text)
    if text not in out:
        raise AssertionFailed("packing was refused, but with %r (expected %r in it)" % (out, text))


@step(r'packing succeeds')
def then_packs(c):
    code, out = c.pack_result
    if code != 0:
        raise AssertionFailed("packing was refused: %s" % out)


# ---- the ABI list: what the image exports to a menu description -----------------------------------------------------------------------
@step(r'the build exports the operations and states of "([^"]+)"')
def then_exports(c, path):
    import difflib
    import export
    want = open(os.path.join(ROOT, path)).read().splitlines()
    got = export.export_text(c.emu).splitlines()
    if want != got:
        diff = [l for l in difflib.unified_diff(want, got, "golden", "build", lineterm="", n=0) if not l.startswith(("---", "+++", "@@"))]
        raise AssertionFailed("%s is not what the build exports (%d lines against %d); first differences: %s" % (path, len(want), len(got), " | ".join(diff[:6])))
