"""The step definitions (D5 of docs/specs/T153-ui-sdk-and-tests.md). A step is a regular expression and a function(ctx, *groups).

Since T192.3 (Alex, 2026-11-09) every step reads the UI service's snapshot (GET /v1/ui/state, contract 2: `rev`, `areas`, `tabs`, `items`, `dialog`, `topbar`,
`footer`, `status`, `path`, `sheet`, `medium`); there is no v1 state any more. The keys still go in as the board's keyboard (the emulator's control port), which is
the service's local key intent. The table old -> new is in docs/specs/T192.3-core-app-on-core.md section 5.

An assertion that is false raises AssertionFailed with the expected and the actual value; anything else that goes wrong (a key the menu did not take, a
state that never came, an emulator that ended) is a StepError, which is an error of the test and not a verdict on the UI.
"""
import json
import os
import struct
import zlib
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
        "ctrl+v": "ctrl+v", "ctrl+n": "ctrl+n", "+": "+", "-": "-"}


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
    def __init__(self, with_emulator=True, core="amiga1200"):
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
            if s['menu_open'] and self.emu.core in ("nes", "snes", "neogeo"):     # the NES with no cartridge loaded keeps its menu (the top bar says WAITING): that is the state the scenario starts from
                return self.settle()
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


@step(r'I wait (\d+) ms')
def i_wait_ms(c, n):
    """Emulated milliseconds without the UI service (a core that holds at boot with a message: no state to wait on)."""
    c.emu.wait(int(n))


@step(r'I save the picture "([^"]+)"')
def i_save_picture(c, name):
    """The emulator's screen as a PNG (tests/ui/out/NAME.png): for the person who looks at the page; nothing is asserted on pixels."""
    c.emu.wait(400)
    c.emu.png(name)


@step(r'I press RETURN and save the picture "([^"]+)" while it works')
def i_press_return_during(c, name):
    """T226 S1: RETURN starts a load that blocks the service; the picture is taken in the middle of it (emulated time), nothing is awaited."""
    c.emu.key("RETURN")
    c.emu.wait(150)
    c.emu.png(name)
    c.emu.wait(20000)


def _png_bright(path, x0, y0, x1, y1, lum=140):
    """The count of bright pixels in a rectangle of an 8-bit RGB/RGBA PNG (standard library only)."""
    return _png_rect(path, x0, y0, x1, y1, lambda r, g, b: max(r, g, b) >= lum)[0]


def _png_rect(path, x0, y0, x1, y1, pred):
    """(count of the pixels of the rectangle for which pred(r, g, b) holds, the sum of their brightness, the pixels in all, the width of the picture); the rectangle in picture pixels."""
    d = open(path, "rb").read()
    pos, idat, w, h, ct = 8, b"", 0, 0, 2
    while pos < len(d):
        ln, typ = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
            if bd != 8 or ct not in (2, 6):
                raise StepError("the picture is not an 8-bit RGB PNG")
        elif typ == b"IDAT":
            idat += body
        pos += 12 + ln
    bpp = 3 if ct == 2 else 4
    raw, stride, prev, rows = zlib.decompress(idat), w * bpp, bytearray(w * bpp), []
    for y in range(h):
        f = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                cur[i] = (cur[i] + a) & 255
            elif f == 2:
                cur[i] = (cur[i] + b) & 255
            elif f == 3:
                cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(cur)
        prev = cur
    n = tot = allp = 0
    for y in range(y0, min(y1, h)):
        r = rows[y]
        for x in range(x0, min(x1, w)):
            allp += 1
            px = (r[x * bpp], r[x * bpp + 1], r[x * bpp + 2])
            tot += px[0] + px[1] + px[2]
            if pred(*px):
                n += 1
    return n, tot, allp, w


def _canvas_rect(path, cx0, cy0, cx1, cy1, pred):
    """The same for a rectangle of the 960 x 540 canvas: the picture is scaled by its width over 960."""
    w = struct.unpack(">I", open(path, "rb").read()[16:20])[0]
    k = w / 960.0
    return _png_rect(path, int(cx0 * k), int(cy0 * k), int(cx1 * k), int(cy1 * k), pred)


@step(r'the picture "([^"]+)" has text in the box')
def then_picture_text(c, name):
    """T187 F1: the confirm panel (x 176-932, y 96-316 on the canvas) holds drawn text, not an empty box. The count of bright pixels, not a reading of them."""
    n = _png_bright(os.path.join(emu_mod.OUT, name + ".png"), 176, 96, 932, 316)
    if n < 300:
        raise AssertionFailed("the panel of %s shows %d bright pixels: the text is not drawn" % (name, n))


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


# ---- T256: the overlay service: the PAUSED tag, the still behind it, the open menu over the still, an element of a library (tests/ui/features*/overlay*.feature) -------------------
@step(r'the machine is paused with the menu closed')
def machine_paused(c):
    """The pause is set while the menu is open (machine.pause_toggle) and takes hold when the menu closes: the tag and the still come up."""
    c.open_menu()
    c.emu.op("machine.pause_toggle")
    c.press("RUN/STOP")
    expect_eq("menu_open", False, c.refresh()['menu_open'])
    c.emu.wait(600)


@step(r'the machine runs again')
def machine_resumes(c):
    c.open_menu()
    c.emu.op("machine.pause_toggle")
    c.press("RUN/STOP")
    expect_eq("menu_open", False, c.refresh()['menu_open'])
    c.emu.wait(600)


@step(r'the picture "([^"]+)" shows the PAUSED tag at its place')
def picture_tag(c, name):
    """The tag is the base UI TOML's element "paused": 126 x 30 canvas pixels from the top right corner, 14 pixels in, 14 down; its text is bright on the dark box."""
    n = _canvas_rect(os.path.join(emu_mod.OUT, name + ".png"), 820, 14, 946, 44, lambda r, g, b: max(r, g, b) >= 200)[0]
    if n < 80:
        raise AssertionFailed("the tag's place shows %d bright pixels: the text PAUSED is not drawn there" % n)


@step(r'the picture "([^"]+)" shows the machine\'s last frame behind the tag, not black')
def picture_still(c, name):
    """The still is the whole canvas (the base UI TOML's element "still"): below the tag the picture holds colour, not the black of a halted machine."""
    n, tot, allp, w = _canvas_rect(os.path.join(emu_mod.OUT, name + ".png"), 0, 120, 800, 500, lambda r, g, b: max(r, g, b) > 24)
    if allp == 0 or n * 10 < allp * 3:
        raise AssertionFailed("only %d of %d pixels behind the tag are not black: the still is not there" % (n, allp))


@step(r'the picture "([^"]+)" shows no tag')
def picture_no_tag(c, name):
    """With the menu closed: no bright pixel at the tag's place. With the menu open the top bar draws its own text there, so that picture is checked against a picture of the tag
    (the step below)."""
    n = _canvas_rect(os.path.join(emu_mod.OUT, name + ".png"), 820, 14, 946, 44, lambda r, g, b: max(r, g, b) >= 200)[0]
    if n >= 80:
        raise AssertionFailed("the tag's place shows %d bright pixels: a tag is drawn" % n)


@step(r'the picture "([^"]+)" does not hold the tag of "([^"]+)"')
def picture_not_tag_of(c, name, tagpic):
    """The text of the tag (bright pixels of PAUSED in `tagpic`, the picture of the tag with the menu closed) is not at the same pixels of `name`: the tag alone makes that text, a menu
    bar's own text does not land on it."""
    def pixels(pic):
        px = []
        _canvas_rect(os.path.join(emu_mod.OUT, pic + ".png"), 820, 14, 946, 44, lambda r, g, b: px.append((r, g, b)) or False)
        return px
    a, t = pixels(name), pixels(tagpic)
    if len(a) != len(t) or not t:
        raise AssertionFailed("%s and %s are not pictures of one size" % (name, tagpic))
    text = [i for i, (r, g, b) in enumerate(t) if max(r, g, b) >= 200]
    if len(text) < 80:
        raise AssertionFailed("%s shows no tag text at its place: it is no reference" % tagpic)
    same = sum(1 for i in text if max(a[i]) >= 200)
    if same * 10 >= len(text) * 6:
        raise AssertionFailed("%d of the %d pixels of the tag's text are bright in %s too: the tag is drawn over the open menu" % (same, len(text), name))


# ---- T256 S3, S5, S10: a library of our own (tests/ui/ovtest) calls the service through the SDK group; the emulator runs the SDK example's app it is linked into, on the profile vc20 (`# core: vc20+ovtest`) ------------------
def _lib_op(c, name):
    """The test library's operation over REST. A 404 is an app that did not link the library: the log of the emulator says why."""
    import urllib.error
    try:
        return c.emu.op("ovtest." + name)
    except urllib.error.HTTPError as e:
        raise StepError("ovtest.%s answered %s %s: the library is not linked (the emulator log, [lib] lines; tests/ui/ovtest/build.sh) or the call was refused" % (name, e.code, e.read().decode("utf-8", "replace")[:200]))


@step(r'the test library shows its element')
def lib_shows(c):
    _lib_op(c, "show")
    c.emu.wait(600)


@step(r'the test library takes its element away')
def lib_removes(c):
    _lib_op(c, "remove")
    c.emu.wait(600)


def _lib_pause(c, name):
    """Pause is a flag the menu's close turns into the stop (api.md 4.14.4): the library sets it while the menu is open, then the menu closes."""
    c.open_menu()
    _lib_op(c, name)
    c.press("RUN/STOP")
    expect_eq("menu_open", False, c.refresh()['menu_open'])
    c.emu.wait(600)


@step(r'the test library paints the machine\'s picture')
def lib_paints(c):
    _lib_op(c, "paint")
    c.emu.wait(600)


@step(r'the test library pauses the machine')
def lib_pauses(c):
    _lib_pause(c, "pause")


@step(r'the test library resumes the machine')
def lib_resumes(c):
    _lib_pause(c, "resume")


@step(r'the test library hides the menu')
def lib_hides(c):
    _lib_op(c, "hide")
    c.emu.wait(900)


@step(r'the test library shows the menu')
def lib_shows_menu(c):
    _lib_op(c, "show_menu")
    c.emu.wait(900)


# the element of tests/ui/ovtest/ovtest.toml: 160 x 40, bottom_mid, 40 above the bottom edge of the 960 x 540 canvas; the library paints it red with a white frame
OV_BOX = (400, 460, 560, 500)


def _ov_red(r, g, b):
    return r >= 200 and g <= 60 and b <= 60


@step(r'the picture "([^"]+)" shows the library\'s element at its place')
def picture_ov_element(c, name):
    n, tot, allp, w = _canvas_rect(os.path.join(emu_mod.OUT, name + ".png"), OV_BOX[0], OV_BOX[1], OV_BOX[2], OV_BOX[3], _ov_red)
    if allp == 0 or n * 2 < allp:
        raise AssertionFailed("%d of the %d pixels at the element's place are the element's red: it is not on the screen" % (n, allp))


@step(r'the picture "([^"]+)" shows no element of the library')
def picture_no_ov_element(c, name):
    n, tot, allp, w = _canvas_rect(os.path.join(emu_mod.OUT, name + ".png"), OV_BOX[0], OV_BOX[1], OV_BOX[2], OV_BOX[3], _ov_red)
    if n * 10 >= allp:
        raise AssertionFailed("%d of the %d pixels at the element's place are the element's red: it is on the screen" % (n, allp))


def _pixels(path, box):
    px = []
    _canvas_rect(path, box[0], box[1], box[2], box[3], lambda r, g, b: px.append((r, g, b)) or False)
    return px


def _moved(a, b):
    """The count of pixels above the element that differ between two saved pictures; the pictures have to be of one size."""
    box = (0, 100, 960, 440)
    pa, pb = _pixels(os.path.join(emu_mod.OUT, a + ".png"), box), _pixels(os.path.join(emu_mod.OUT, b + ".png"), box)
    if len(pa) != len(pb) or not pa:
        raise AssertionFailed("%s and %s are not pictures of one size" % (a, b))
    return sum(1 for x, y in zip(pa, pb) if abs(x[0] - y[0]) + abs(x[1] - y[1]) + abs(x[2] - y[2]) > 48)


@step(r'the picture "([^"]+)" shows another running picture than "([^"]+)"(?: or than "([^"]+)")? above the element')
def picture_moves(c, a, b, c2=None):
    """S3: the machine's picture runs under the element (the cursor of the C64's screen blinks about every 333 ms, so two looks 400 ms apart differ unless two toggles fall between
    them; with a third look the pair of the two gaps cannot both hold two). Only the area above the element is compared; one pair that differs is enough."""
    diffs = [_moved(a, b)] + ([_moved(b, c2)] if c2 else [])
    if max(diffs) < 40:
        raise AssertionFailed("%s pixels above the element differ between the pictures: the picture under the element does not move" % "/".join(str(d) for d in diffs))


def _blocks(path):
    """The picture as the canvas of 960 x 540 in blocks of 40 x 30 canvas pixels: {(bx, by): the mean (r, g, b)}."""
    w = struct.unpack(">I", open(path, "rb").read()[16:20])[0]
    k = w / 960.0
    px = _pixels(path, (0, 0, 960, 540))
    W = int(960 * k)
    out = {}
    for by in range(18):
        for bx in range(24):
            acc, n = [0, 0, 0], 0
            for y in range(int(by * 30 * k), int((by + 1) * 30 * k)):
                row = y * W
                for x in range(int(bx * 40 * k), int((bx + 1) * 40 * k)):
                    p = px[row + x]
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]
                    n += 1
            out[(bx, by)] = tuple(a / n for a in acc) if n else (0, 0, 0)
    return out


@step(r'the picture "([^"]+)" is the picture "([^"]+)" outside the library\'s element and the tag')
def picture_same_outside(c, still, before):
    """S5: the picture behind the element is the one the machine showed before it stopped: the blocks of 40 x 30 canvas pixels agree (the still is sampled from the frame buffer,
    the running picture is scaled by the layer, so a block mean has a tolerance), the blocks of the element and of the tag left out; and it is a picture, not black."""
    a, b = _blocks(os.path.join(emu_mod.OUT, still + ".png")), _blocks(os.path.join(emu_mod.OUT, before + ".png"))
    skip = lambda bx, by: (bx * 40 < OV_BOX[2] and (bx + 1) * 40 > OV_BOX[0] and by * 30 < OV_BOX[3] and (by + 1) * 30 > OV_BOX[1]) or (bx * 40 < 946 and (bx + 1) * 40 > 820 and by * 30 < 44 and (by + 1) * 30 > 14)
    keys = [k for k in a if not skip(*k)]
    bright = sum(1 for k in keys if max(b[k]) > 24)
    if bright * 4 < len(keys):
        raise AssertionFailed("only %d of %d blocks of %s are not black: the machine showed no picture to keep" % (bright, len(keys), before))
    bad = [k for k in keys if sum(abs(x - y) for x, y in zip(a[k], b[k])) > 90]
    if len(bad) * 10 > len(keys):
        raise AssertionFailed("%d of %d blocks of %s differ from %s (first at block %s): the still is not the picture that stopped" % (len(bad), len(keys), still, before, bad[0]))


@step(r'the emulator log has no line with "([^"]+)"')
def log_has_no(c, text):
    """S5: the base took the stopped machine's picture (a failed grab logs `no still`)."""
    lines = [l.rstrip() for l in open(c.emu.logpath, errors="replace") if text in l]
    if lines:
        raise AssertionFailed("the emulator log says: %s" % lines[0][:200])
