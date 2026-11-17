#!/usr/bin/env python3
"""The description record of a .cor (T104.1 D9, D12): TOML source -> flat binary record, and the checker that reads it back.

The layout is documented in coreapp/src/descrec.h (that file and this one are the two readers of it; tests/coreapp/test_descrec.cc and
tools/test_descrec.py compile the same sources and read them with both). Little endian, fixed entries, one string table, no pointers.

Source (TOML; every key is checked, an unknown one is an error - a typo must not silently become a menu without its entry):

    [[area]]                       # optional; without any, the default route stays (T104 5.8)
    key = "F5"                     # F1 .. F8
    name = "C64"                   # optional: the core's short title
    shifted = false
      [[area.tab]]
      id = "drives"                # lower case, [a-z0-9_]: what an anchor names
      name = "Drives"              # optional: none = named after the area (T205 D1)
      widget = "settings"          # info | browser | media | settings | tasks | system | nodes
      groups = ["drive_a"]         # settings groups the tab lists (widget settings)
      n = 3                        # the printed number; 0 or absent: the next free one

      layout = "settings"          # optional: how the tab is drawn (a screen of the UX concept): home | split | settings | status | library; the flat list when absent
      ui = { title = "TRX64 behavior" }      # optional: the layout's parameters (title, crumb); `ui = { sections = true }` alone (no layout) keeps the tab a list and
                                     # draws each of its groups under a heading made from the group's id (T228; not drawn when it repeats the tab's title)

    [[node]]                       # a button, a state line or a heading the core itself puts into a tab
    anchor = "F5/drives/drive_a"   # area/tab or area/tab/group
    kind = "op"                    # op | state | group   (setting and widget are refused when the menu is merged)
    label = "Reset Drive"
    op = "drive.reset"             # op: the operation; state: the state value shown
    bind = [ { arg = "slot", str = "drive_a" } ]     # every argument of the operation: file | int | str | ask | text
    order = 10
    when_state = "drive.drive_a.mode != empty"
    capability = "drive_a"
    id = "reset_a"                 # what hide / rename name; default: the operation or state
    closes_menu = false
    ui = { primary = true, goto = "F3", sub = "@drive.drive_a.image", chip = "@drive.drive_a.mode", tone = "cyan", off = "the reason it is greyed" }
                                   # optional, how the node is shown (coreapp/src/screens.h): primary (the violet button), link, goto (an area key or area/tab),
                                   # sub / chip (a second line, a state chip: "@state.name", "@info.Network", "link.Network" or plain text), tone, off (greyed, with its reason),
                                   # why (the reason shown while the node's when_state does not hold)

    [[file_action]]
    ext = [".d64", ".g64"]
    label = "Mount Disk"
    op = "drive.mount"
    bind = [ { arg = "path", file = true }, { arg = "mode", str = "rw" } ]
    size_in = [16384, 32768]
    closes_menu = false

    [[setting]]
    group = "drive_a"
    key = "Drive Bus ID"
    range = [8, 11]                # or values = ["Disabled", "Enabled"]
    default = 8                    # a value of the range, or the index into values
    apply = "drive.set_bus_id"

    [[tab]]                        # a tab this contributor adds to an area of the route (appended after the area's own, never renumbering them)
    area = "F5"
    id = "extras"
    name = "Extras"
    widget = "nodes"               # as for a tab of the route
    groups = ["g"]                 # widget settings: the groups it lists
    n = 0                          # 0: the next free number

    # T104.2 D10: the tables of the core's OWN menu (what C++ descriptors carry as Task / FileType / MediaSlot / Setting tables). A record that has
    # any of them takes the core's tasks, media slots, file types and settings from the record; the application's own settings (Picture, Persist, Home,
    # hotkeys) follow the record's, and the entries a library contributes are merged after them.
    [[task]]                       # a button of a tasks tab; `group` names the tab (the tab's groups list) it belongs on, none: the tabs that list no group
    group = "machine"
    label = "Reset"
    op = "machine.cold_reset"
    bind = [ ]                     # as for a node, plus ask: the typed text goes to the argument bound with `text = true`
    closes_menu = true
    ask = "Name of the new D64"    # optional: the prompt; ask_state: a state whose text prefills it; pick_dir: a folder is picked first (pick_label: its first row)
    ask_state = "drive.suggest.d64"
    pick_dir = true
    pick_label = "Create here"

    [[media]]                      # a slot of a media tab: what is in it (state), and how it is ejected
    group = "drives"
    name = "Drive A:"
    state = "drive.drive_a.image"
    eject_op = "drive.eject"
    eject_bind = [ { arg = "slot", str = "iec:drive_a" } ]
    exts = ".d64,.g64"             # the drive list (T212 D5/D11): what Mount from Library offers; mount = a file action's label, empty_tasks / mounted_tasks = task labels
    mount = "Mount Disk"           # (comma separated) listed while the slot is empty / mounted; each must name an existing file action or task (the pack fails otherwise)
    empty_tasks = "Insert blank D64"
    mounted_tasks = "Save disk"
    eject_closes = true            # optional: the Eject row closes the menu

    [[filetype]]                   # what the browser offers for files of an extension (and, for a disk image, for the files inside it)
    ext = ".d64"
    inner_ext = ".prg"             # optional; the actions below [[filetype.inner]] are for the files inside whose name ends so
      [[filetype.action]]
      label = "Mount Disk"
      op = "drive.mount"
      bind = [ { arg = "path", file = true } ]
      closes_menu = true
      ask = "Start address"        # optional: the prompt; the typed address goes to the argument bound with `ask = true`, ask_off: where its default is read
      ask_off = 0
      size_in = [16384]            # optional
      [[filetype.inner]]
      label = "Mount & Run"
      op = "drive.mount_run_file"

    [[core_setting]]               # a setting of the core itself: values (labels) and the index of the default, when it applies ("live" or "reset")
    key = "Drive Bus ID"
    group = "drives"
    values = ["8", "9", "10", "11"]
    default = 0
    when = "live"
    apply = "drive.set_bus_id"
    base = 0                       # optional: the operation gets base + index

    # a tab with widget "info" names the lines it shows (of the application's fixed ones): lines = ["Network", "Media"]; none: all six
    [poll]                         # the operation called every period_ms on a task of its own (T104.1 D6): no arguments, may answer `more`
    op = "drive.poll"
    period_ms = 10

    hide = ["some_node_id"]        # nodes of other contributors the description hides; a key at the top of the file, before the first table
    [[rename]]
    id = "some_node_id"
    label = "New label"
"""
import re
import struct

MAGIC = 0x44585254          # "TRXD"
VERSION = 4                 # 4 (T228) widens a media slot by the drive list's fields and gives the header's last word the machine-info line; 3 gave the two reserved words of 2 a use (a tab's layout, a node's attributes); 2 and 3 are read too
VERSION_MIN = 2
HEADER = 176
NONE = 0xFFFFFFFF
MAX_SIZE = 0x20000
LIMITS = dict(area=8, tab=32, node=320, action=24, setting=16, hide=16, rename=16, bind=700, ext=48, size=16, value=3200, ctab=16,
              task=64, media=4, ftype=32, fact=200, cset=330)
ESIZE = dict(area=20, tab=24, node=52, action=40, setting=32, hide=4, rename=8, bind=16, ext=4, size=4, value=4, ctab=24,
             task=36, media=44, ftype=24, fact=36, cset=36)
ESIZE_V3_MEDIA = 24
ORDER = ("area", "tab", "node", "action", "setting", "hide", "rename", "bind", "ext", "size", "value", "ctab", "task", "media", "ftype", "fact", "cset")
OWN = ("task", "media", "ftype", "fact", "cset")          # the tables of the core's own menu (flag bit 1)
INFO_LINES = ("Core", "Application", "Network", "Media", "ROMs", "Libraries")   # menutree.cc menu_info_labels, the application's fixed lines
WHEN = {"live": 0, "reset": 1}
WIDGETS = ("info", "browser", "media", "settings", "tasks", "system", "nodes")
NODE_KINDS = {"op": 0, "setting": 1, "state": 2, "widget": 3, "group": 4}
BIND_KINDS = {"file": 0, "int": 1, "str": 2, "ask": 3, "text": 4}
TABS_PER_AREA = 8                  # = MENU_TABS_MAX; the tab strip of the view has room for eight of them (TRX_TABS_MAX) (an application's hidden screens are tabs that are not on it)


class DescError(Exception):
    pass


# ---- a TOML subset ---------------------------------------------------------------------------------------------------------
# tomllib is 3.11; the build image has 3.10. This reads what the description uses: comments, [table] and [[array.of.tables]] with dotted
# names, key = value with strings (basic and literal), integers (decimal, 0x), booleans, arrays (also over several lines) and inline tables.

class _Toml:
    def __init__(self, text):
        self.s = text
        self.i = 0

    def err(self, msg):
        line = self.s.count("\n", 0, self.i) + 1
        raise DescError("TOML line %d: %s" % (line, msg))

    def ws(self, newlines=False):
        while self.i < len(self.s):
            c = self.s[self.i]
            if c in " \t\r" or (newlines and c == "\n"):
                self.i += 1
            elif c == "#":
                while self.i < len(self.s) and self.s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def key(self):
        m = re.compile(r'[A-Za-z0-9_-]+|"([^"\n]*)"').match(self.s, self.i)
        if not m:
            self.err("a key is expected")
        self.i = m.end()
        return m.group(1) if m.group(1) is not None else m.group(0)

    def dotted(self):
        parts = [self.key()]
        while True:
            self.ws()
            if self.i < len(self.s) and self.s[self.i] == ".":
                self.i += 1
                self.ws()
                parts.append(self.key())
            else:
                return parts

    def value(self):
        s = self.s
        if self.i >= len(s):
            self.err("a value is expected")
        c = s[self.i]
        if c == '"':
            return self.basic_string()
        if c == "'":
            j = s.find("'", self.i + 1)
            if j < 0 or "\n" in s[self.i:j]:
                self.err("unterminated string")
            v = s[self.i + 1:j]
            self.i = j + 1
            return v
        if c == "[":
            self.i += 1
            out = []
            while True:
                self.ws(True)
                if self.i < len(s) and s[self.i] == "]":
                    self.i += 1
                    return out
                out.append(self.value())
                self.ws(True)
                if self.i < len(s) and s[self.i] == ",":
                    self.i += 1
                elif self.i < len(s) and s[self.i] == "]":
                    self.i += 1
                    return out
                else:
                    self.err("',' or ']' is expected in an array")
        if c == "{":
            self.i += 1
            out = {}
            while True:
                self.ws()
                if self.i < len(s) and s[self.i] == "}":
                    self.i += 1
                    return out
                k = self.key()
                self.ws()
                if s[self.i:self.i + 1] != "=":
                    self.err("'=' is expected")
                self.i += 1
                self.ws()
                if k in out:
                    self.err("key %r twice" % k)
                out[k] = self.value()
                self.ws()
                if s[self.i:self.i + 1] == ",":
                    self.i += 1
                elif s[self.i:self.i + 1] != "}":
                    self.err("',' or '}' is expected in an inline table")
        if s.startswith("true", self.i):
            self.i += 4
            return True
        if s.startswith("false", self.i):
            self.i += 5
            return False
        m = re.compile(r"[+-]?(0x[0-9A-Fa-f_]+|[0-9][0-9_]*)").match(s, self.i)
        if not m:
            self.err("a value is expected")
        self.i = m.end()
        t = m.group(0).replace("_", "")
        return int(t, 16) if "0x" in t.lower() else int(t)

    def basic_string(self):
        s = self.s
        j = self.i + 1
        out = []
        esc = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
        while j < len(s) and s[j] != '"':
            if s[j] == "\n":
                self.err("unterminated string")
            if s[j] == "\\":
                if j + 1 >= len(s) or s[j + 1] not in esc:
                    self.err("an escape of a string is not known")
                out.append(esc[s[j + 1]])
                j += 2
            else:
                out.append(s[j])
                j += 1
        if j >= len(s):
            self.err("unterminated string")
        self.i = j + 1
        return "".join(out)

    def parse(self):
        root = {}
        cur = root
        while True:
            self.ws(True)
            if self.i >= len(self.s):
                return root
            if self.s.startswith("[[", self.i):
                self.i += 2
                self.ws()
                path = self.dotted()
                self.ws()
                if not self.s.startswith("]]", self.i):
                    self.err("']]' is expected")
                self.i += 2
                cur = self.array_table(root, path)
            elif self.s[self.i] == "[":
                self.i += 1
                self.ws()
                path = self.dotted()
                self.ws()
                if self.s[self.i:self.i + 1] != "]":
                    self.err("']' is expected")
                self.i += 1
                cur = root
                for p in path:
                    cur = cur.setdefault(p, {})
                    if isinstance(cur, list):
                        cur = cur[-1]
            else:
                k = self.key()
                self.ws()
                if self.s[self.i:self.i + 1] != "=":
                    self.err("'=' is expected")
                self.i += 1
                self.ws()
                if k in cur:
                    self.err("key %r twice" % k)
                cur[k] = self.value()
            self.ws()
            if self.i < len(self.s) and self.s[self.i] not in "\n#":
                self.err("unexpected text after a value")

    def array_table(self, root, path):
        cur = root
        for p in path[:-1]:
            nxt = cur.setdefault(p, {})
            cur = nxt[-1] if isinstance(nxt, list) else nxt
        lst = cur.setdefault(path[-1], [])
        if not isinstance(lst, list):
            self.err("%r is not an array of tables" % path[-1])
        d = {}
        lst.append(d)
        return d


def toml_loads(text):
    """tomllib's answer where there is one (3.11), the subset reader otherwise: the description is written in the subset, so both agree."""
    try:
        import tomllib
    except ImportError:
        return _Toml(text).parse()
    try:
        return tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise DescError("TOML: %s" % e)


# ---- the compiler -----------------------------------------------------------------------------------------------------------

def _only(d, allowed, what):
    for k in d:
        if k not in allowed:
            raise DescError("%s: unknown key %r (known: %s)" % (what, k, ", ".join(sorted(allowed))))


def _need(d, k, typ, what):
    if k not in d or not isinstance(d[k], typ) or isinstance(d[k], bool) != (typ is bool):
        raise DescError("%s: %r is missing or not a %s" % (what, k, typ.__name__))
    return d[k]


def _opt(d, k, typ, what, default=None):
    if k not in d:
        return default
    return _need(d, k, typ, what)


class _Strings:
    def __init__(self):
        self.buf = bytearray(b"\0")          # offset 0 is the empty string
        self.idx = {"": 0}

    def add(self, s):
        if s is None:
            return NONE
        if s not in self.idx:
            self.idx[s] = len(self.buf)
            self.buf += s.encode("utf-8") + b"\0"
        return self.idx[s]


UI_NODE_KEYS = {"primary": bool, "link": bool, "goto": str, "sub": str, "chip": str, "tone": str, "off": str, "why": str, "confirm": str, "label": str, "hide": str, "ask": str, "ask_state": str, "job": bool, "pick": str, "rename": bool, "view": str, "key": str, "none": str, "nokey": bool, "empty": str, "bkey": str, "legend": str}
UI_TONES = ("muted", "cyan", "violet", "amber", "coral")
LAYOUTS = ("home", "desk", "split", "settings", "status", "library", "timeline", "terminal", "app", "grid")
UI_TAB_KEYS = {"title": str, "crumb": str, "side": str, "chip": str, "sheet": str, "sheetx": str, "nodes_first": bool, "sections": bool, "left": str, "label": str, "frame": str, "bar": str, "hint": str, "marks": str, "pos": str, "info": str, "glyph": str, "track": str,
               # an application's own screens (T104.3 D0, layouts app and grid): which tab is shown, where a key goes, and what the screen draws of the application's state
               "hidden": bool, "lines": str, "under": str, "back": str, "follow": str, "lock": str, "keys": str, "enter": str, "rows": str, "input": str, "grid": str, "note": str,
               "status": str, "pct": str, "pct_label": str, "pct_note": str,
               "map1": str, "map1_s": str, "map1_d": str, "map1_t": str, "map2": str, "map2_s": str, "map2_d": str, "map2_t": str}


def _ui_word(what, k, v):
    if ";" in v or not v or v != v.strip():
        raise DescError("%s: ui %s is a text without ';' and without spaces around it" % (what, k))


def _ui_string(d, keys, what):
    """A ui inline table -> "key;key=value;..." (flags first in the order of keys, then values in the order of keys)."""
    if d is None:
        return None
    if not isinstance(d, dict):
        raise DescError("%s: ui is a table { key = value }" % what)
    _only(d, set(keys), what + " ui")
    words = []
    for k, typ in keys.items():
        if k not in d:
            continue
        v = d[k]
        if typ is bool:
            if v is not True:
                raise DescError("%s: ui %s is written 'true'" % (what, k))
            words.append(k)
        else:
            if not isinstance(v, str):
                raise DescError("%s: ui %s is a text" % (what, k))
            _ui_word(what, k, v)
            words.append("%s=%s" % (k, v))
    return ";".join(words) or None


def _ui_table(s, keys):
    """The inverse of _ui_string."""
    d = {}
    for w in (s or "").split(";"):
        if not w:
            continue
        k, _, v = w.partition("=")
        d[k] = True if keys.get(k) is bool else v
    return d


GROUP_WIDGETS = ("settings", "tasks", "media")      # the widgets whose entries name a group


def _tab_groups(tb, tid, widget):
    """The comma list a tab stores in its `groups` field: the settings / tasks / media groups it lists, or - for an info tab - the lines it shows."""
    groups = _opt(tb, "groups", list, "tab " + tid, None)
    lines = _opt(tb, "lines", list, "tab " + tid, None)
    if groups is not None and (not groups or not all(isinstance(g, str) and re.fullmatch(r"[A-Za-z0-9_]+", g) for g in groups)):
        raise DescError("tab %s: groups are names" % tid)
    if groups and widget not in GROUP_WIDGETS:
        raise DescError("tab %s: groups belong to a settings, tasks or media tab" % tid)
    if lines is not None:
        if widget != "info":
            raise DescError("tab %s: lines belong to an info tab" % tid)
        if not lines or not all(isinstance(g, str) and g in INFO_LINES for g in lines) or len(set(lines)) != len(lines):
            raise DescError("tab %s: lines are some of %s, each once" % (tid, ", ".join(INFO_LINES)))
        if groups:
            raise DescError("tab %s: groups and lines together" % tid)
        return lines
    return groups


def _expand_apps(doc):
    """T104.3 D6: an entry that opens an application says so as `app = "trex_crt"` and `action = "write"`; it is the operation app.open with the application, the action
    and - for a file action - the file as its bindings. A node, a task or a file action; the result is what the entry would have been written as."""
    def one(e, what, has_file):
        if "app" not in e:
            if "action" in e and what != "filetype action":
                raise DescError("%s: action without app" % what)
            return
        if "op" in e or "bind" in e:
            raise DescError("%s: app and action stand for op and bind; give one or the other" % what)
        app = _need(e, "app", str, what)
        if not re.fullmatch(r"[a-z0-9_]{1,15}", app):
            raise DescError("%s: app %r is 1..15 characters of [a-z0-9_]" % (what, app))
        action = _opt(e, "action", str, what, "")
        e["op"] = "app.open"
        e["bind"] = [{"arg": "app", "str": app}, {"arg": "action", "str": action}, ({"arg": "file", "file": True} if has_file else {"arg": "file", "str": ""})]
        del e["app"]
        e.pop("action", None)
    for e in doc.get("file_action", []):
        one(e, "file_action", True)
    for e in doc.get("node", []):
        one(e, "node", False)
    for e in doc.get("task", []):
        one(e, "task", False)
    for ft in doc.get("filetype", []):
        for e in ft.get("action", []):
            one(e, "filetype action", True)


def compile_desc(doc, merged=()):
    """doc: the parsed TOML (a dict). Returns the record's bytes. `merged`: the names of the libraries whose menus were merged into it (merge_lib_menus)."""
    _expand_apps(doc)
    _only(doc, {"area", "node", "file_action", "setting", "hide", "rename", "tab", "poll", "task", "media", "filetype", "core_setting", "lists", "machine_info"}, "description")
    st = _Strings()
    t = {k: [] for k in ORDER}

    def bind_list(binds, what):
        out = []
        for b in binds or []:
            if not isinstance(b, dict):
                raise DescError("%s: a binding is a table { arg = .., file|int|str|ask|text = .. }" % what)
            _only(b, {"arg"} | set(BIND_KINDS), what + " binding")
            arg = _need(b, "arg", str, what + " binding")
            kinds = [k for k in BIND_KINDS if k in b]
            if len(kinds) != 1:
                raise DescError("%s: binding %r needs exactly one of file, int, str, ask, text" % (what, arg))
            k = kinds[0]
            iv, sv = 0, NONE
            if k == "int":
                iv = _need(b, "int", int, what)
                if not -2 ** 31 <= iv < 2 ** 31:
                    raise DescError("%s: binding %r: the number does not fit 32 bits" % (what, arg))
            elif k == "str":
                sv = st.add(_need(b, "str", str, what))
            elif b[k] is not True:
                raise DescError("%s: binding %r: %s is written 'true'" % (what, arg, k))
            out.append((st.add(arg), BIND_KINDS[k], iv & 0xFFFFFFFF, sv))
        first = len(t["bind"])
        t["bind"].extend(out)
        return first, len(out)

    areas = _opt(doc, "area", list, "description", [])
    for a in areas:
        _only(a, {"key", "name", "shifted", "tab"}, "area")
        key = _need(a, "key", str, "area")
        if not re.fullmatch(r"F[1-8]|APP", key):
            raise DescError("area key %r: F1 .. F8, or APP (an application's own screens)" % key)
        tabs = _opt(a, "tab", list, "area %s" % key, [])
        if not tabs or len(tabs) > TABS_PER_AREA:
            raise DescError("area %s: 1 to %d tabs" % (key, TABS_PER_AREA))
        first = len(t["tab"])
        seen_id, seen_n = set(), set()
        for tb in tabs:
            _only(tb, {"id", "name", "widget", "groups", "lines", "n", "layout", "ui"}, "tab")
            tid = _need(tb, "id", str, "tab")
            if not re.fullmatch(r"[a-z0-9_]{1,31}", tid):
                raise DescError("tab id %r: lower case, digits and _, at most 31" % tid)
            widget = _need(tb, "widget", str, "tab " + tid)
            if widget not in WIDGETS:
                raise DescError("tab %s: widget %r (one of %s)" % (tid, widget, ", ".join(WIDGETS)))
            n = _opt(tb, "n", int, "tab " + tid, 0)
            if not 0 <= n <= 99:
                raise DescError("tab %s: n is 0 .. 99" % tid)
            if tid in seen_id or (n and n in seen_n):
                raise DescError("area %s: tab %r or its number twice" % (key, tid))
            seen_id.add(tid)
            seen_n.add(n)
            groups = _tab_groups(tb, tid, widget)
            layout = _opt(tb, "layout", str, "tab " + tid, None)
            if layout is None and "ui" in tb and set(tb["ui"]) != {"sections"}:
                raise DescError("tab %s: ui without a layout (only `sections` stands alone)" % tid)
            if layout is not None and layout not in LAYOUTS:
                raise DescError("tab %s: layout %r (one of %s)" % (tid, layout, ", ".join(LAYOUTS)))
            lay = None
            if layout is not None or "ui" in tb:                         # no layout: the name is empty, the tab stays a list (T228 `sections`)
                params = _ui_string(tb.get("ui"), UI_TAB_KEYS, "tab " + tid)
                lay = (layout or "") + (";" + params if params else "")
            t["tab"].append((st.add(tid), st.add(_opt(tb, "name", str, "tab " + tid, "")), n, WIDGETS.index(widget),
                             st.add(",".join(groups)) if groups else NONE, st.add(lay) if lay else 0))
        t["area"].append((st.add(key), st.add(_opt(a, "name", str, "area " + key, None)), 1 if _opt(a, "shifted", bool, "area", False) else 0,
                          first, len(tabs)))

    page_keys = {}
    for n in _opt(doc, "node", list, "description", []):
        _only(n, {"anchor", "kind", "label", "op", "state", "bind", "order", "when_state", "capability", "id", "closes_menu", "ui"}, "node")
        label = _need(n, "label", str, "node")
        kind = _need(n, "kind", str, "node " + label)
        if kind not in NODE_KINDS:
            raise DescError("node %s: kind %r (one of %s)" % (label, kind, ", ".join(NODE_KINDS)))
        anchor = _need(n, "anchor", str, "node " + label)
        if not re.fullmatch(r"(F[1-8]|APP)/[a-z0-9_]+(/[A-Za-z0-9_]+)?", anchor):
            raise DescError("node %s: anchor %r is area/tab or area/tab/group" % (label, anchor))
        order = _opt(n, "order", int, "node " + label, 0)
        if not -32768 <= order <= 32767:
            raise DescError("node %s: order does not fit 16 bits" % label)
        fb, nb = bind_list(_opt(n, "bind", list, "node " + label, []), "node " + label)
        ui = _ui_string(_opt(n, "ui", dict, "node " + label, None), UI_NODE_KEYS, "node " + label)
        if ui:
            tone = _ui_table(ui, UI_NODE_KEYS).get("tone")
            if tone is not None and tone not in UI_TONES:
                raise DescError("node %s: ui tone %r (one of %s)" % (label, tone, ", ".join(UI_TONES)))
            goto = _ui_table(ui, UI_NODE_KEYS).get("goto")
            if goto is not None and not re.fullmatch(r"(F[1-8]|APP)(/[a-z0-9_]+)?", goto):
                raise DescError("node %s: ui goto %r is an area key or area/tab" % (label, goto))
        if ui:                                                      # a letter belongs to its page: no two elements of a page claim one, none claims a digit or a key of the navigation
            uit = _ui_table(ui, UI_NODE_KEYS)
            page = "/".join(anchor.split("/")[:2])
            for kk in ("key", "bkey"):
                kv = uit.get(kk)
                if kv is None:
                    continue
                if not (len(kv) == 1 and kv.isalpha() and kv == kv.upper()):
                    raise DescError("node %s: ui %s is one upper case letter (digits and the F keys belong to the navigation)" % (label, kk))
                if kv in page_keys.setdefault(page, {}):
                    raise DescError("page %s: the letter %s is claimed by %s and by %s" % (page, kv, page_keys[page][kv], label))
                page_keys[page][kv] = label
        t["node"].append((st.add(anchor), NODE_KINDS[kind], st.add(label), st.add(_opt(n, "op", str, "node", None)),
                          st.add(_opt(n, "state", str, "node", None)), order & 0xFFFFFFFF, st.add(_opt(n, "when_state", str, "node", None)),
                          st.add(_opt(n, "capability", str, "node", None)), st.add(_opt(n, "id", str, "node", None)), fb, nb,
                          1 if _opt(n, "closes_menu", bool, "node", False) else 0, st.add(ui) if ui else 0))

    for f in _opt(doc, "file_action", list, "description", []):
        _only(f, {"ext", "label", "op", "bind", "size_in", "closes_menu"}, "file_action")
        label = _need(f, "label", str, "file_action")
        exts = _need(f, "ext", list, "file_action " + label)
        if not exts or not all(isinstance(e, str) and e.startswith(".") and len(e) > 1 for e in exts):
            raise DescError("file_action %s: ext is a list of extensions with a dot" % label)
        sizes = _opt(f, "size_in", list, "file_action " + label, [])
        if not all(isinstance(z, int) and not isinstance(z, bool) and 0 < z < 2 ** 32 for z in sizes):
            raise DescError("file_action %s: size_in are sizes in bytes" % label)
        fe = len(t["ext"])
        t["ext"].extend((st.add(e.lower()),) for e in exts)
        fz = len(t["size"])
        t["size"].extend((z,) for z in sizes)
        fb, nb = bind_list(_opt(f, "bind", list, "file_action " + label, []), "file_action " + label)
        t["action"].append((st.add(label), st.add(_need(f, "op", str, "file_action " + label)), fe, len(exts), fz, len(sizes), fb, nb,
                            1 if _opt(f, "closes_menu", bool, "file_action", False) else 0, 0))

    for s in _opt(doc, "setting", list, "description", []):
        _only(s, {"group", "key", "values", "range", "default", "apply"}, "setting")
        key = _need(s, "key", str, "setting")
        apply = _need(s, "apply", str, "setting " + key)
        group = _opt(s, "group", str, "setting " + key, None)
        if ("values" in s) == ("range" in s):
            raise DescError("setting %s: exactly one of values and range" % key)
        fv, nv, lo, hi = len(t["value"]), 0, 0, 0
        if "values" in s:
            vals = _need(s, "values", list, "setting " + key)
            if len(vals) < 2 or not all(isinstance(v, str) and v for v in vals):
                raise DescError("setting %s: values are at least two non-empty labels" % key)
            t["value"].extend((st.add(v),) for v in vals)
            nv = len(vals)
            default = _opt(s, "default", int, "setting " + key, 0)
            if not 0 <= default < nv:
                raise DescError("setting %s: default %d is not an index of values" % (key, default))
        else:
            rng = _need(s, "range", list, "setting " + key)
            if len(rng) != 2 or not all(isinstance(v, int) for v in rng) or rng[1] <= rng[0]:
                raise DescError("setting %s: range = [lo, hi] with hi above lo" % key)
            lo, hi = rng
            default = _opt(s, "default", int, "setting " + key, lo)
            if not lo <= default <= hi:
                raise DescError("setting %s: default %d is outside the range" % (key, default))
        t["setting"].append((st.add(group), st.add(key), fv, nv, lo & 0xFFFFFFFF, hi & 0xFFFFFFFF, default & 0xFFFFFFFF, st.add(apply)))

    for h in _opt(doc, "hide", list, "description", []):
        if not isinstance(h, str) or not h:
            raise DescError("hide: node ids")
        t["hide"].append((st.add(h),))
    for r in _opt(doc, "rename", list, "description", []):
        _only(r, {"id", "label"}, "rename")
        t["rename"].append((st.add(_need(r, "id", str, "rename")), st.add(_need(r, "label", str, "rename"))))

    for tb in _opt(doc, "tab", list, "description", []):
        _only(tb, {"area", "id", "name", "widget", "groups", "lines", "n"}, "tab")
        area = _need(tb, "area", str, "tab")
        if not re.fullmatch(r"F[1-8]", area):
            raise DescError("tab area %r: F1 .. F8" % area)
        tid = _need(tb, "id", str, "tab")
        if not re.fullmatch(r"[a-z0-9_]{1,31}", tid):
            raise DescError("tab id %r: lower case, digits and _, at most 31" % tid)
        widget = _need(tb, "widget", str, "tab " + tid)
        if widget not in WIDGETS:
            raise DescError("tab %s: widget %r (one of %s)" % (tid, widget, ", ".join(WIDGETS)))
        n = _opt(tb, "n", int, "tab " + tid, 0)
        if not 0 <= n <= 99:
            raise DescError("tab %s: n is 0 .. 99" % tid)
        groups = _tab_groups(tb, tid, widget)
        t["ctab"].append((st.add(area), st.add(tid), st.add(_need(tb, "name", str, "tab " + tid)), WIDGETS.index(widget), st.add(",".join(groups)) if groups else NONE, n))

    # ---- the core's own menu (T104.2 D10)
    def text_key(d, k, what, default=None):
        v = _opt(d, k, str, what, default)
        if v is not None and not v:
            raise DescError("%s: %r is empty" % (what, k))
        return v

    def group_name(d, what):
        g = _opt(d, "group", str, what, None)
        if g is not None and not re.fullmatch(r"[A-Za-z0-9_]+", g):
            raise DescError("%s: group %r is a name of letters, digits and _" % (what, g))
        return g

    for tk in _opt(doc, "task", list, "description", []):
        _only(tk, {"group", "label", "op", "bind", "closes_menu", "ask", "ask_state", "pick_dir", "pick_label"}, "task")
        label = _need(tk, "label", str, "task")
        what = "task " + label
        fb, nb = bind_list(_opt(tk, "bind", list, what, []), what)
        ask = text_key(tk, "ask", what)
        if ask is None and any(k in tk for k in ("ask_state", "pick_dir", "pick_label")):
            raise DescError("%s: ask_state, pick_dir and pick_label need an ask" % what)
        if "pick_label" in tk and not tk.get("pick_dir"):
            raise DescError("%s: pick_label needs pick_dir" % what)
        flags = (1 if _opt(tk, "closes_menu", bool, what, False) else 0) | (2 if _opt(tk, "pick_dir", bool, what, False) else 0)
        t["task"].append((st.add(label), st.add(_need(tk, "op", str, what)), fb, nb, flags, st.add(ask), st.add(text_key(tk, "ask_state", what)),
                          st.add(text_key(tk, "pick_label", what)), st.add(group_name(tk, what))))

    for m in _opt(doc, "media", list, "description", []):
        _only(m, {"group", "name", "state", "eject_op", "eject_bind", "exts", "mount", "empty_tasks", "mounted_tasks", "eject_closes"}, "media")
        name = _need(m, "name", str, "media")
        what = "media " + name
        if "eject_op" not in m and "eject_bind" in m:
            raise DescError("%s: eject_bind without eject_op" % what)
        fb, nb = bind_list(_opt(m, "eject_bind", list, what, []), what)
        t["media"].append((st.add(name), st.add(_need(m, "state", str, what)), st.add(text_key(m, "eject_op", what)), fb, nb, st.add(group_name(m, what)),
                           st.add(text_key(m, "exts", what)), st.add(text_key(m, "mount", what)), st.add(text_key(m, "empty_tasks", what)),
                           st.add(text_key(m, "mounted_tasks", what)), 1 if _opt(m, "eject_closes", bool, what, False) else 0))

    seen_ft = set()
    sheet_keys = {}
    fact_labels = set()
    def file_actions(lst, what):
        first = len(t["fact"])
        for f in lst:
            fact_labels.add(f.get("label"))
            _only(f, {"label", "op", "bind", "closes_menu", "ask", "ask_off", "size_in", "key"}, what)
            label = _need(f, "label", str, what)
            w2 = "%s action %s" % (what, label)
            ask = text_key(f, "ask", w2)
            if "ask_off" in f and ask is None:
                raise DescError("%s: ask_off needs an ask" % w2)
            off = _opt(f, "ask_off", int, w2, 0)
            if not -32768 <= off <= 32767:
                raise DescError("%s: ask_off does not fit 16 bits" % w2)
            sizes = _opt(f, "size_in", list, w2, [])
            if not all(isinstance(z, int) and not isinstance(z, bool) and 0 < z < 2 ** 32 for z in sizes):
                raise DescError("%s: size_in are sizes in bytes" % w2)
            fz = len(t["size"])
            t["size"].extend((z,) for z in sizes)
            fb, nb = bind_list(_opt(f, "bind", list, w2, []), w2)
            key = _opt(f, "key", str, w2, "")
            if key and key in sheet_keys.setdefault(what, {}):
                raise DescError("%s: the sheet letter %s is claimed by %s and by %s" % (what, key, sheet_keys[what][key], label))
            if key:
                sheet_keys[what][key] = label
            if key and not (len(key) == 1 and key.isalnum() and key == key.upper()):
                raise DescError("%s: key is one upper case letter or digit (the letter that runs the item in the sheet)" % w2)
            fl = (1 if _opt(f, "closes_menu", bool, w2, False) else 0) | (ord(key) << 8 if key else 0)      # bits 15:8 the key
            t["fact"].append((st.add(label), st.add(_need(f, "op", str, w2)), fb, nb, fl, st.add(ask),
                              off & 0xFFFFFFFF, fz, len(sizes)))
        return first, len(lst)

    for ft in _opt(doc, "filetype", list, "description", []):
        _only(ft, {"ext", "inner_ext", "action", "inner"}, "filetype")
        ext = _need(ft, "ext", str, "filetype")
        if not ext.startswith(".") or len(ext) < 2 or ext != ext.lower():
            raise DescError("filetype %r: a lower case extension with a dot" % ext)
        if ext in seen_ft:
            raise DescError("filetype %s twice" % ext)
        seen_ft.add(ext)
        acts = _opt(ft, "action", list, "filetype " + ext, [])
        inner = _opt(ft, "inner", list, "filetype " + ext, [])
        if not acts:
            raise DescError("filetype %s: at least one [[filetype.action]]" % ext)
        inner_ext = text_key(ft, "inner_ext", "filetype " + ext)
        if bool(inner) != (inner_ext is not None) or (inner_ext is not None and not inner_ext.startswith(".")):
            raise DescError("filetype %s: inner_ext (with a dot) and [[filetype.inner]] go together" % ext)
        fa, na = file_actions(acts, "filetype " + ext)
        fi, ni = file_actions(inner, "filetype " + ext + " inner")
        t["ftype"].append((st.add(ext), st.add(inner_ext), fa, na, fi, ni))

    # a media slot's drive list names its entries by label: a typo would silently drop the row, so every name has to exist
    task_labels = {tk.get("label") for tk in _opt(doc, "task", list, "description", [])}
    for m in _opt(doc, "media", list, "description", []):
        what = "media " + m["name"]
        if m.get("mount") is not None and m["mount"] not in fact_labels:
            raise DescError("%s: mount %r is not the label of a file action of any filetype" % (what, m["mount"]))
        for k_ in ("empty_tasks", "mounted_tasks"):
            for lab in (m.get(k_) or "").split(","):
                if lab and lab not in task_labels:
                    raise DescError("%s: %s names %r, which is not a task" % (what, k_, lab))

    seen_set = set()
    lists = _opt(doc, "lists", dict, "description", {})
    for cs in _opt(doc, "core_setting", list, "description", []):
        _only(cs, {"key", "group", "values", "list", "range", "unit", "default", "when", "apply", "base", "text", "secret", "maxlen", "show_state", "label", "empty"}, "core_setting")
        key = _need(cs, "key", str, "core_setting")
        what = "core_setting " + key
        if key.lower() in seen_set or key.lower() == "picture":
            raise DescError("%s: the key is taken (Picture belongs to the application)" % what)
        seen_set.add(key.lower())
        when = _opt(cs, "when", str, what, "live")
        if when not in WHEN:
            raise DescError("%s: when is live or reset" % what)
        base = _opt(cs, "base", int, what, 0)
        wcode = WHEN[when]
        if _opt(cs, "text", bool, what, False) or _opt(cs, "secret", bool, what, False):
            # a text the person types: values[0] is its default, base its longest length (T104.2 UI)
            vals = [_opt(cs, "default", str, what, "")]
            wcode = 3 if _opt(cs, "secret", bool, what, False) else 2
            base = _opt(cs, "maxlen", int, what, 40)
            default = 0
            if vals[0] == "":
                vals = [" "]                # the record has no empty strings; a blank is trimmed at the reader's use
            if _opt(cs, "empty", str, what, ""):
                vals.append(cs["empty"])    # what the row shows while the text is empty (values[1])
        else:
            if "list" in cs:
                vals = lists.get(cs["list"])
                if not isinstance(vals, list):
                    raise DescError("%s: no list named %s" % (what, cs["list"]))
            elif "range" in cs:
                rg = cs["range"]
                if not (isinstance(rg, list) and len(rg) == 2 and all(isinstance(x, int) for x in rg) and rg[0] < rg[1]):
                    raise DescError("%s: range is [lo, hi]" % what)
                unit = _opt(cs, "unit", str, what, "")
                vals = ["%d%s" % (x, unit) for x in range(rg[0], rg[1] + 1)]
                if "base" not in cs:
                    base = rg[0]
            else:
                vals = _need(cs, "values", list, what)
            if len(vals) < 2 or not all(isinstance(v, str) and v for v in vals):
                raise DescError("%s: values are at least two non-empty labels" % what)
            default = cs.get("default", 0)
            if isinstance(default, str):
                if default not in vals:
                    raise DescError("%s: default %s is not one of the values" % (what, default))
                default = vals.index(default)
            elif "range" in cs:
                default = default - cs["range"][0]
            if not isinstance(default, int) or not 0 <= default < len(vals):
                raise DescError("%s: default %s is not an index of values" % (what, default))
        if not -2 ** 31 <= base < 2 ** 31:
            raise DescError("%s: base does not fit 32 bits" % what)
        show = _opt(cs, "show_state", str, what, None)         # the row is listed only while this condition holds (contrib.h cond_holds); packed above the time code
        if show:
            wcode |= (st.add(show) + 1) << 8
        fv = len(t["value"])
        t["value"].extend((st.add(v),) for v in vals)
        t["cset"].append((st.add(key), st.add(group_name(cs, what)), fv, len(vals), default, wcode, st.add(_need(cs, "apply", str, what)), base & 0xFFFFFFFF, st.add(_opt(cs, "label", str, what, None))))

    poll_op, poll_ms = NONE, 0
    poll = _opt(doc, "poll", dict, "description", None)
    if poll is not None:
        _only(poll, {"op", "period_ms"}, "poll")
        poll_op = st.add(_need(poll, "op", str, "poll"))
        poll_ms = _need(poll, "period_ms", int, "poll")
        if not 1 <= poll_ms <= 60000:
            raise DescError("poll: period_ms is 1 .. 60000")
    for m_ in merged:
        if not re.fullmatch(r"[a-z0-9_]{1,15}", m_):
            raise DescError("merged library name %r: 1 to 15 characters of a-z, 0-9 and _" % m_)
    merged_off = st.add(",".join(merged)) if merged else NONE
    mi = _opt(doc, "machine_info", str, "description", None)
    mi_off = st.add(mi) if mi else NONE
    for k in ORDER:
        if len(t[k]) > LIMITS[k]:
            raise DescError("%d %s entries; the board takes %d" % (len(t[k]), k, LIMITS[k]))
    # layout: header, tables in ORDER, strings last
    pos = HEADER
    offs = {}
    for k in ORDER:
        offs[k] = pos if t[k] else 0
        pos += ESIZE[k] * len(t[k])
    str_off = pos
    total = str_off + len(st.buf)
    if total > MAX_SIZE:
        raise DescError("the record is %d bytes; the board takes %d" % (total, MAX_SIZE))
    out = bytearray(struct.pack("<IHHIIII", MAGIC, VERSION, HEADER, total, (1 if t["area"] else 0) | (2 if any(t[k] for k in OWN) else 0), str_off, len(st.buf)))
    for k in ORDER:
        out += struct.pack("<II", offs[k], len(t[k]))
    out += struct.pack("<IIII", poll_op, poll_ms, merged_off, mi_off)
    assert len(out) == HEADER, len(out)
    for k in ORDER:
        for e in t[k]:
            out += struct.pack("<%dI" % (ESIZE[k] // 4), *e)
    out += st.buf
    assert len(out) == total
    return bytes(out)


def compile_text(text):
    return compile_desc(toml_loads(text))


# ---- the reader (the same checks as descrec.cc) ----------------------------------------------------------------------------

def parse_desc(blob):
    """Reads and checks a record; returns a dict of the decoded lists. Raises DescError."""
    n = len(blob)
    if n < HEADER or n > MAX_SIZE:
        raise DescError("description record: %d bytes" % n)
    magic, ver, hsz, total, flags, so, ss = struct.unpack_from("<IHHIIII", blob, 0)
    if magic != MAGIC:
        raise DescError("description record: magic")
    if not VERSION_MIN <= ver <= VERSION:
        raise DescError("description record: version %d, this tool reads %d to %d" % (ver, VERSION_MIN, VERSION))
    esz = dict(ESIZE)
    if ver < 4:
        esz["media"] = ESIZE_V3_MEDIA
    if hsz != HEADER or total != n or flags & ~3:
        raise DescError("description record: header or length does not match")
    if so < HEADER or so + ss > n or ss == 0:
        raise DescError("description record: string table outside the record")
    tabs = {}
    for i, k in enumerate(ORDER):
        off, cnt = struct.unpack_from("<II", blob, 24 + 8 * i)
        if cnt > LIMITS[k]:
            raise DescError("description record: %d %s entries" % (cnt, k))
        if cnt and (off < HEADER or off + cnt * esz[k] > n):
            raise DescError("description record: table of %s outside the record" % k)
        tabs[k] = [struct.unpack_from("<%dI" % (esz[k] // 4), blob, off + j * esz[k]) for j in range(cnt)]

    def s(o):
        if o == NONE:
            return None
        if o >= ss:
            raise DescError("description record: a string lies outside the table")
        e = blob.find(b"\0", so + o, so + ss)
        if e < 0:
            raise DescError("description record: a string does not end in the table")
        try:
            return blob[so + o:e].decode("utf-8")
        except UnicodeDecodeError:
            raise DescError("description record: a string is not text")

    out = {"route": bool(flags & 1), "own": bool(flags & 2)}
    pop, pms, merged, r2 = struct.unpack_from("<IIII", blob, 24 + 8 * len(ORDER))
    if ver < 4 and r2:
        raise DescError("description record: a bad poll or merged-libraries list")
    out["machine_info"] = s(r2) if ver >= 4 and r2 != NONE else None
    if (pop == NONE) != (pms == 0) or pms > 60000 or (pop != NONE and not s(pop)) or (merged != NONE and not s(merged)):
        raise DescError("description record: a bad poll or merged-libraries list")
    out["merged"] = s(merged).split(",") if merged != NONE else []
    out["poll"] = {"op": s(pop), "period_ms": pms} if pop != NONE else None
    out["ctab"] = []
    for area, tid, name, w, groups, nn in tabs["ctab"]:
        if not re.fullmatch(r"F[1-8]", s(area) or "") or not re.fullmatch(r"[a-z0-9_]{1,31}", s(tid) or "") or not s(name) or w >= len(WIDGETS) or nn > 99:
            raise DescError("description record: a bad contributed tab")
        out["ctab"].append({"area": s(area), "id": s(tid), "name": s(name), "widget": WIDGETS[w], "groups": s(groups), "n": nn})
    out["bind"] = []
    for a, k, i, sv in tabs["bind"]:
        if k > 4 or not s(a) or (k == 2 and s(sv) is None):
            raise DescError("description record: a bad binding")
        out["bind"].append({"arg": s(a), "kind": k, "i": i - 2 ** 32 if i >= 2 ** 31 else i, "s": s(sv)})
    out["ext"] = [s(e[0]) for e in tabs["ext"]]
    out["size"] = [e[0] for e in tabs["size"]]
    out["value"] = [s(e[0]) for e in tabs["value"]]
    if any(not e or not e.startswith(".") for e in out["ext"]) or any(not v for v in out["value"]):
        raise DescError("description record: an extension without a dot or an empty value")
    out["hide"] = [s(e[0]) for e in tabs["hide"]]
    out["rename"] = [(s(a), s(b)) for a, b in tabs["rename"]]
    if any(not h for h in out["hide"]) or any(not a or not b for a, b in out["rename"]):
        raise DescError("description record: an empty hide or rename entry")
    out["tab"] = []
    for tid, name, nn, w, groups, lay in tabs["tab"]:
        layout = s(lay) if lay else None
        if not re.fullmatch(r"[a-z0-9_]{1,31}", s(tid) or "") or s(name) is None or nn > 99 or w >= len(WIDGETS) or (lay and not layout):
            raise DescError("description record: a bad tab")
        g = s(groups)
        if g == "":
            g = None
        isinfo = WIDGETS[w] == "info"
        if g and (not isinfo and WIDGETS[w] not in GROUP_WIDGETS):
            raise DescError("description record: tab %s lists groups but its widget takes none" % s(tid))
        if g and isinfo and any(x not in INFO_LINES for x in g.split(",")):
            raise DescError("description record: tab %s lists an information line that is not one" % s(tid))
        out["tab"].append({"id": s(tid), "name": s(name), "n": nn, "widget": WIDGETS[w], "groups": None if isinfo else g, "lines": g.split(",") if isinfo and g else None,
                           "layout": layout})
    out["area"] = []
    used = 0
    keys = set()
    for key, name, fl, first, cnt in tabs["area"]:
        k = s(key)
        if not k or not re.fullmatch(r"F[1-8]|APP", k) or fl & ~1 or k in keys:
            raise DescError("description record: a bad area")
        keys.add(k)
        if first != used or cnt == 0 or cnt > TABS_PER_AREA or first + cnt > len(out["tab"]):
            raise DescError("description record: the tabs of area %s are out of place" % k)
        used += cnt
        ids = [t["id"] for t in out["tab"][first:first + cnt]]
        ns = [t["n"] for t in out["tab"][first:first + cnt] if t["n"]]
        if len(set(ids)) != len(ids) or len(set(ns)) != len(ns):
            raise DescError("description record: area %s lists a tab or a number twice" % k)
        out["area"].append({"key": k, "name": s(name), "shifted": bool(fl & 1), "tabs": ids})
    if used != len(out["tab"]) or out["route"] != bool(out["area"]):
        raise DescError("description record: tabs without an area, or the route flag disagrees")
    out["node"] = []
    for anchor, kind, label, op, state, order, when, cap, nid, fb, nb, fl, attr in tabs["node"]:
        if not s(anchor) or not s(label) or kind > 4 or fl & ~1 or (attr and not s(attr)) or fb + nb > len(out["bind"]):
            raise DescError("description record: a bad node")
        out["node"].append({"anchor": s(anchor), "kind": kind, "label": s(label), "op": s(op), "state": s(state),
                            "order": order - 2 ** 32 if order >= 2 ** 31 else order, "when_state": s(when), "capability": s(cap),
                            "id": s(nid), "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1), "ui": s(attr) if attr else None})
    out["action"] = []
    for label, op, fe, ne, fz, nz, fb, nb, fl, rsv in tabs["action"]:
        if not s(label) or not s(op) or ne == 0 or fl & ~1 or rsv or fe + ne > len(out["ext"]) or fz + nz > len(out["size"]) \
                or fb + nb > len(out["bind"]):
            raise DescError("description record: a bad file action")
        out["action"].append({"label": s(label), "op": s(op), "ext": out["ext"][fe:fe + ne], "size_in": out["size"][fz:fz + nz],
                              "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1)})
    out["setting"] = []
    for group, key, fv, nv, lo, hi, df, apply in tabs["setting"]:
        lo, hi, df = [v - 2 ** 32 if v >= 2 ** 31 else v for v in (lo, hi, df)]
        if not s(key) or not s(apply) or fv + nv > len(out["value"]) or (nv == 0 and hi < lo):
            raise DescError("description record: a bad setting")
        out["setting"].append({"group": s(group), "key": s(key), "values": out["value"][fv:fv + nv], "lo": lo, "hi": hi, "default": df,
                               "apply": s(apply)})
    out["task"] = []
    for label, op, fb, nb, fl, ask, ask_state, pick_label, group in tabs["task"]:
        if not s(label) or not s(op) or fl & ~3 or fb + nb > len(out["bind"]) or (s(ask) is None and (ask_state != NONE or fl & 2 or pick_label != NONE)) or \
                (pick_label != NONE and not fl & 2):
            raise DescError("description record: a bad task")
        out["task"].append({"label": s(label), "op": s(op), "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1), "pick_dir": bool(fl & 2),
                            "ask": s(ask), "ask_state": s(ask_state), "pick_label": s(pick_label), "group": s(group)})
    out["media"] = []
    for me in tabs["media"]:
        name, state, eject, fb, nb, group = me[:6]
        ext_ = me[6:] if len(me) > 6 else (NONE, NONE, NONE, NONE, 0)
        if not s(name) or not s(state) or fb + nb > len(out["bind"]) or (eject == NONE and nb) or ext_[4] & ~1:
            raise DescError("description record: a bad media slot")
        out["media"].append({"name": s(name), "state": s(state), "eject_op": s(eject), "eject_bind": out["bind"][fb:fb + nb], "group": s(group),
                             "exts": s(ext_[0]), "mount": s(ext_[1]), "empty_tasks": s(ext_[2]), "mounted_tasks": s(ext_[3]), "eject_closes": bool(ext_[4] & 1)})
    out["fact"] = []
    for label, op, fb, nb, fl, ask, off, fz, nz in tabs["fact"]:
        off = off - 2 ** 32 if off >= 2 ** 31 else off
        if not s(label) or not s(op) or fl & ~0xFF01 or fb + nb > len(out["bind"]) or fz + nz > len(out["size"]) or not -32768 <= off <= 32767 or (s(ask) is None and off):
            raise DescError("description record: a bad file type action")
        out["fact"].append({"label": s(label), "op": s(op), "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1), "key": chr(fl >> 8) if fl >> 8 else "", "ask": s(ask), "ask_off": off,
                            "size_in": out["size"][fz:fz + nz]})
    out["ftype"] = []
    seen = set()
    for ext, inner_ext, fa, na, fi, ni in tabs["ftype"]:
        e = s(ext)
        if not e or not e.startswith(".") or e in seen or na == 0 or fa + na > len(out["fact"]) or fi + ni > len(out["fact"]) or \
                (ni == 0) != (inner_ext == NONE) or (inner_ext != NONE and not (s(inner_ext) or "").startswith(".")):
            raise DescError("description record: a bad file type")
        seen.add(e)
        out["ftype"].append({"ext": e, "inner_ext": s(inner_ext), "actions": out["fact"][fa:fa + na], "inner": out["fact"][fi:fi + ni]})
    out["cset"] = []
    for key, group, fv, nv, df, when, apply, base, label in tabs["cset"]:
        base = base - 2 ** 32 if base >= 2 ** 31 else base
        show = s((when >> 8) - 1) if when >> 8 else None
        when &= 0xFF
        if not s(key) or not s(apply) or nv < (1 if when >= 2 else 2) or fv + nv > len(out["value"]) or df >= nv or when > 3:
            raise DescError("description record: a bad core setting")
        out["cset"].append({"key": s(key), "group": s(group), "values": out["value"][fv:fv + nv], "default": df, "when": {0: "live", 1: "reset", 2: "text", 3: "secret"}[when],
                            "apply": s(apply), "base": base, "show_state": show, "label": s(label)})
    if out["own"] != bool(out["task"] or out["media"] or out["ftype"] or out["cset"]):
        raise DescError("description record: the own-menu flag and the tables of the own menu disagree")
    return out


# ---- the pack-time merge of the libraries' menus (T104.2 D10) -----------------------------------------------------------------------------

def _bind_doc(binds):
    out = []
    for b in binds:
        d = {"arg": b["arg"]}
        k = next(n for n, i in BIND_KINDS.items() if i == b["kind"])
        d[k] = b["i"] if k == "int" else (b["s"] if k == "str" else True)
        out.append(d)
    return out


def decoded_to_doc(rec):
    """The TOML document (a dict, as toml_loads makes it) that compiles to the record `rec` (parse_desc's answer): what the packer needs to put more
    into a record that is already compiled. compile_desc(decoded_to_doc(parse_desc(b)), merged) == b."""
    doc = {}
    if rec.get("machine_info"):
        doc["machine_info"] = rec["machine_info"]
    if rec["area"]:
        doc["area"], used = [], 0
        for a in rec["area"]:
            tabs = []
            for tb in rec["tab"][used:used + len(a["tabs"])]:
                d = {"id": tb["id"], "widget": tb["widget"], "n": tb["n"]}
                if tb["name"]:                                  # T205 D1: no name = named after the area
                    d["name"] = tb["name"]
                if tb["groups"]:
                    d["groups"] = tb["groups"].split(",")
                if tb.get("lines"):
                    d["lines"] = tb["lines"]
                if tb.get("layout"):
                    name, _, params = tb["layout"].partition(";")
                    if name:
                        d["layout"] = name
                    if params:
                        d["ui"] = _ui_table(params, UI_TAB_KEYS)
                tabs.append(d)
            used += len(a["tabs"])
            d = {"key": a["key"], "shifted": a["shifted"], "tab": tabs}
            if a["name"] is not None:
                d["name"] = a["name"]
            doc["area"].append(d)
    if rec["node"]:
        doc["node"] = []
        for n in rec["node"]:
            d = {"anchor": n["anchor"], "kind": next(k for k, i in NODE_KINDS.items() if i == n["kind"]), "label": n["label"], "order": n["order"],
                 "closes_menu": n["closes_menu"]}
            for k in ("op", "state", "when_state", "capability", "id"):
                if n[k] is not None:
                    d[k] = n[k]
            if n["bind"]:
                d["bind"] = _bind_doc(n["bind"])
            if n.get("ui"):
                d["ui"] = _ui_table(n["ui"], UI_NODE_KEYS)
            doc["node"].append(d)
    if rec["action"]:
        doc["file_action"] = []
        for a in rec["action"]:
            d = {"ext": a["ext"], "label": a["label"], "op": a["op"], "closes_menu": a["closes_menu"]}
            if a["bind"]:
                d["bind"] = _bind_doc(a["bind"])
            if a["size_in"]:
                d["size_in"] = a["size_in"]
            doc["file_action"].append(d)
    if rec["setting"]:
        doc["setting"] = []
        for x in rec["setting"]:
            d = {"key": x["key"], "apply": x["apply"], "default": x["default"]}
            if x["group"] is not None:
                d["group"] = x["group"]
            if x["values"]:
                d["values"] = x["values"]
            else:
                d["range"] = [x["lo"], x["hi"]]
            doc["setting"].append(d)
    if rec["hide"]:
        doc["hide"] = rec["hide"]
    if rec["rename"]:
        doc["rename"] = [{"id": a, "label": b} for a, b in rec["rename"]]
    if rec["ctab"]:
        doc["tab"] = []
        for c in rec["ctab"]:
            d = {"area": c["area"], "id": c["id"], "name": c["name"], "widget": c["widget"], "n": c["n"]}
            if c["groups"]:
                d["groups"] = c["groups"].split(",")
            doc["tab"].append(d)
    if rec["poll"]:
        doc["poll"] = dict(rec["poll"])
    if rec["task"]:
        doc["task"] = []
        for t in rec["task"]:
            d = {"label": t["label"], "op": t["op"], "closes_menu": t["closes_menu"]}
            if t["bind"]:
                d["bind"] = _bind_doc(t["bind"])
            for k in ("group", "ask", "ask_state", "pick_label"):
                if t[k] is not None:
                    d[k] = t[k]
            if t["pick_dir"]:
                d["pick_dir"] = True
            doc["task"].append(d)
    if rec["media"]:
        doc["media"] = []
        for m in rec["media"]:
            d = {"name": m["name"], "state": m["state"]}
            if m["group"] is not None:
                d["group"] = m["group"]
            for k_ in ("exts", "mount", "empty_tasks", "mounted_tasks"):
                if m[k_] is not None:
                    d[k_] = m[k_]
            if m["eject_closes"]:
                d["eject_closes"] = True
            if m["eject_op"] is not None:
                d["eject_op"] = m["eject_op"]
                if m["eject_bind"]:
                    d["eject_bind"] = _bind_doc(m["eject_bind"])
            doc["media"].append(d)
    if rec["ftype"]:
        doc["filetype"] = []

        def facts(lst):
            out = []
            for a in lst:
                d = {"label": a["label"], "op": a["op"], "closes_menu": a["closes_menu"]}
                if a["bind"]:
                    d["bind"] = _bind_doc(a["bind"])
                if a["ask"] is not None:
                    d["ask"] = a["ask"]
                    d["ask_off"] = a["ask_off"]
                if a["size_in"]:
                    d["size_in"] = a["size_in"]
                if a.get("key"):
                    d["key"] = a["key"]                  # the sheet letter (bits 15:8 of the flags)
                out.append(d)
            return out
        for f in rec["ftype"]:
            d = {"ext": f["ext"], "action": facts(f["actions"])}
            if f["inner_ext"] is not None:
                d["inner_ext"] = f["inner_ext"]
                d["inner"] = facts(f["inner"])
            doc["filetype"].append(d)
    if rec["cset"]:
        doc["core_setting"] = []
        for c in rec["cset"]:
            d = {"key": c["key"], "values": c["values"], "default": c["default"], "when": c["when"], "apply": c["apply"], "base": c["base"]}
            if c["when"] in ("text", "secret"):          # a text the person types: values[0] is its default (a blank stands for none), base its longest length, values[1] what an empty text shows
                d = {"key": c["key"], "apply": c["apply"], "default": c["values"][0].strip(), "maxlen": c["base"]}
                d["secret" if c["when"] == "secret" else "text"] = True
                if len(c["values"]) > 1:
                    d["empty"] = c["values"][1]
            if c["group"] is not None:
                d["group"] = c["group"]
            if c.get("label"):
                d["label"] = c["label"]
            if c.get("show_state"):
                d["show_state"] = c["show_state"]
            doc["core_setting"].append(d)
    return doc


def merge_lib_menus(desc_blob, libs):
    """The core's description with the menus of its libraries merged in: libs is a list of (name, record bytes). What a library contributes - nodes,
    file actions, settings and tabs - is appended after the description's own, the libraries in the order of their names; the record then lists them as
    merged, and the application does not add their menus again at run time. A library that cannot be merged stays for the run time and is named in the
    second result: one that declares a poll (a record has room for one), or one that carries more than contributions (a route, hide / rename of
    others, the core's own tables). Returns (blob, merged names, [(name, reason)])."""
    base = parse_desc(desc_blob) if desc_blob else None
    doc = decoded_to_doc(base) if base else {}
    merged = list(base["merged"]) if base else []
    kept = []
    for name, blob in sorted(libs, key=lambda x: x[0]):
        rec = parse_desc(blob)
        if rec["route"] or rec["hide"] or rec["rename"] or rec["own"]:
            kept.append((name, "its record gives a route, hides or renames what others contribute, or carries a core's own tables"))
            continue
        if rec["poll"]:
            kept.append((name, "it declares a poll, of which a description has one"))
            continue
        if name in merged:
            raise DescError("library %s is merged twice" % name)
        d = decoded_to_doc(rec)
        for k in ("node", "file_action", "setting", "tab"):
            if d.get(k):
                doc.setdefault(k, []).extend(d[k])
        merged.append(name)
    if len(merged) == (len(base["merged"]) if base else 0):
        return desc_blob, merged, kept
    return compile_desc(doc, merged), merged, kept


# ---- the check against what the application exports (T104.2 D10) -------------------------------------------------------------------
# The application exports its registry: one line per operation, one per state value, tab separated (tests/coreapp/ops_export.cc writes it):
#   op <TAB> name <TAB> arg:type:lo:hi:maxlen:label|label ...        type: int bool enum string path slot blob
#   state <TAB> name
# The checks are those of desc_check.cc / bindcheck.cc on the merged descriptor, made on the record, so a menu that names an operation the application
# does not have, binds an argument wrongly or leaves one unbound is refused when the .cor is packed, not when the board starts.

ARG_TYPES = ("int", "bool", "enum", "string", "path", "slot", "blob")


def load_ops(text):
    ops, states = {}, set()
    for ln, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        if f[0] == "state" and len(f) == 2:
            states.add(f[1])
        elif f[0] == "op" and len(f) >= 2:
            args = []
            for a in f[2:]:
                p = a.split(":")
                if len(p) != 6 or p[1] not in ARG_TYPES:
                    raise DescError("ops list line %d: a bad argument %r" % (ln, a))
                args.append({"name": p[0], "type": p[1], "lo": int(p[2]), "hi": int(p[3]), "maxlen": int(p[4]), "labels": p[5].split("|") if p[5] else []})
            ops[f[1]] = args
        else:
            raise DescError("ops list line %d: not an op or a state" % ln)
    return ops, states


def _bind_problem(ops, what, label, op, binds, file_ok, ask_ok, text_ok):
    if "$slot" in op or any("$slot" in (b.get("s") or "") for b in binds):
        return None                              # a declaration made once per drive slot: the names exist only with the core's capabilities
    if op not in ops:
        return '%s: "%s" calls operation "%s", which the application does not export' % (what, label, op)
    args = ops[op]
    seen = set()
    for b in binds:
        a = next((x for x in args if x["name"] == b["arg"]), None)
        if a is None:
            return '%s: "%s" binds "%s", which %s has no argument of' % (what, label, b["arg"], op)
        if b["arg"] in seen:
            return '%s: "%s" binds "%s" twice' % (what, label, b["arg"])
        seen.add(b["arg"])
        textual = a["type"] in ("string", "path", "slot")
        numeric = a["type"] in ("int", "bool", "enum")
        k = b["kind"]
        if k == BIND_KINDS["file"]:
            if not file_ok:
                return '%s: "%s" has no file to bind to "%s"' % (what, label, b["arg"])
            if not textual:
                return '%s: "%s" binds the file to "%s" of %s, which is not text' % (what, label, b["arg"], op)
        elif k == BIND_KINDS["int"]:
            if not numeric:
                return '%s: "%s" binds a number to "%s" of %s, which is not a number' % (what, label, b["arg"], op)
            if not a["lo"] <= b["i"] <= a["hi"]:
                return '%s: "%s" binds %d to "%s" of %s, which takes %d .. %d' % (what, label, b["i"], b["arg"], op, a["lo"], a["hi"])
        elif k == BIND_KINDS["str"]:
            lab = a["type"] == "enum" and any(l.lower() == b["s"].lower() for l in a["labels"])
            if not ((textual and len(b["s"]) <= a["maxlen"]) or lab):
                return '%s: "%s" binds "%s" to "%s" of %s, which does not take it' % (what, label, b["s"], b["arg"], op)
        elif k == BIND_KINDS["text"]:
            if not text_ok:
                return '%s: "%s" binds the typed text to "%s", but the entry asks for none (ask)' % (what, label, b["arg"])
            if a["type"] != "string":
                return '%s: "%s" binds the typed text to "%s" of %s, which is not a string' % (what, label, b["arg"], op)
        elif k == BIND_KINDS["ask"]:
            if not ask_ok:
                return '%s: "%s" binds "%s" to the typed address, but the action asks for none (ask)' % (what, label, b["arg"])
            if not numeric or a["lo"] > 0 or a["hi"] < 0xFFFF:
                return '%s: "%s" binds the typed address to "%s" of %s, which does not take 0 .. 65535' % (what, label, b["arg"], op)
    for a in args:
        if a["name"] not in seen:
            return '%s: "%s" leaves argument "%s" of %s unbound' % (what, label, a["name"], op)
    return None


def _setting_problem(ops, what, key, apply, nvalues, base):
    if "$slot" in apply:
        return None
    if apply not in ops:
        return '%s: "%s" is applied by operation "%s", which the application does not export' % (what, key, apply)
    a = ops[apply]
    if len(a) != 1 or a[0]["name"] != "value" or a[0]["type"] not in ("int", "enum"):
        return '%s: operation "%s" has to take exactly one number or enum named "value"' % (what, apply)
    if a[0]["lo"] > base or a[0]["hi"] < base + nvalues - 1:
        return '%s: "%s" has the values %d .. %d, operation "%s" takes %d .. %d' % (what, key, base, base + nvalues - 1, apply, a[0]["lo"], a[0]["hi"])
    return None


def check_ops(rec, ops, states):
    """rec: parse_desc's answer. Returns a list of sentences, empty when every name and binding is something the application exports."""
    bad = []

    def add(x):
        if x:
            bad.append(x)

    def state_ok(what, label, name):
        if name and "$slot" not in name and name not in states:
            bad.append('%s: "%s" reads state "%s", which the application does not export' % (what, label, name))

    for tk in rec["task"]:
        add(_bind_problem(ops, "task", tk["label"], tk["op"], tk["bind"], False, False, bool(tk["ask"])))
        if tk["ask"]:
            if sum(b["kind"] == BIND_KINDS["text"] for b in tk["bind"]) != 1:
                bad.append('task: "%s" asks for a name but binds it %d times (once)' % (tk["label"], sum(b["kind"] == BIND_KINDS["text"] for b in tk["bind"])))
            state_ok("task", tk["label"], tk["ask_state"])
    for m in rec["media"]:
        state_ok("media", m["name"], m["state"])
        if m["eject_op"]:
            add(_bind_problem(ops, "media", m["name"], m["eject_op"], m["eject_bind"], False, False, False))
    for ft in rec["ftype"]:
        for a in ft["actions"]:
            add(_bind_problem(ops, "filetype " + ft["ext"], a["label"], a["op"], a["bind"], True, bool(a["ask"]), False))
            if a["ask"] and sum(b["kind"] == BIND_KINDS["ask"] for b in a["bind"]) != 1:
                bad.append('filetype %s: "%s" asks for an address but binds it %d times (once)' % (ft["ext"], a["label"], sum(b["kind"] == BIND_KINDS["ask"] for b in a["bind"])))
        for a in ft["inner"]:
            add(_bind_problem(ops, "filetype %s inner" % ft["ext"], a["label"], a["op"], a["bind"], True, False, False))
    for cs in rec["cset"]:
        add(_setting_problem(ops, "core_setting", cs["key"], cs["apply"], len(cs["values"]), cs["base"]))
    for n in rec["node"]:
        ui = _ui_table(n.get("ui"), UI_NODE_KEYS)
        if n["kind"] == NODE_KINDS["op"]:
            if n["op"] or not ("goto" in ui or "off" in ui):
                add(_bind_problem(ops, "node", n["label"], n["op"] or "", n["bind"], False, False, "ask" in ui))     # ui ask: the node prompts for a name first (MenuCtl::begin_ask), the text goes to the argument bound with `text`
        elif n["kind"] == NODE_KINDS["state"]:
            if n["state"] or not ("sub" in ui or "chip" in ui):
                state_ok("node", n["label"], n["state"])
        for k in ("sub", "chip"):
            if ui.get(k, "").startswith("@") and not ui[k].startswith(("@info.", "@set.")):         # @set.<Key> is a setting of the core, not a state value
                state_ok("node", n["label"], ui[k][1:].split("|")[0])
        if n["when_state"]:
            state_ok("node", n["label"], n["when_state"].split()[0])
    for a in rec["action"]:
        add(_bind_problem(ops, "file_action", a["label"], a["op"], a["bind"], True, False, False))
    for st_ in rec["setting"]:
        add(_setting_problem(ops, "setting", st_["key"], st_["apply"], len(st_["values"]) or st_["hi"] - st_["lo"] + 1, st_["lo"] if not st_["values"] else 0))
    if rec["poll"]:
        a = ops.get(rec["poll"]["op"])
        if a is None or a:
            bad.append('poll: "%s" is not an exported operation without arguments' % rec["poll"]["op"])
    return bad


def compile_checked(text, ops_text=None, *, what="description"):
    """compile_text, and - given the application's export - check every name and binding of the result against it."""
    blob = compile_text(text)
    if ops_text is not None:
        ops, states = load_ops(ops_text)
        bad = check_ops(parse_desc(blob), ops, states)
        if bad:
            raise DescError("%s: %d problem(s) against the application's operations:\n  %s" % (what, len(bad), "\n  ".join(bad)))
    return blob


if __name__ == "__main__":
    import sys
    argv = sys.argv[1:]
    ops_file = None
    if "--ops" in argv:
        i = argv.index("--ops")
        ops_file = argv[i + 1] if i + 1 < len(argv) else None
        del argv[i:i + 2]
    if len(argv) not in (2, 3) or argv[0] not in ("compile", "dump") or (argv[0] == "compile" and ops_file is None and "--ops" in sys.argv):
        sys.exit("descrec.py compile desc.toml out.bin [--ops ops.txt] | descrec.py dump record.bin")
    try:
        if argv[0] == "compile":
            blob = compile_checked(open(argv[1]).read(), open(ops_file).read() if ops_file else None, what=argv[1])
            open(argv[2] if len(argv) > 2 else argv[1] + ".bin", "wb").write(blob)
        else:
            import json
            print(json.dumps(parse_desc(open(argv[1], "rb").read()), indent=1))
    except DescError as e:
        sys.exit("descrec: %s" % e)
