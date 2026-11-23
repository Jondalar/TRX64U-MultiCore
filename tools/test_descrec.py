#!/usr/bin/env python3
"""Host test for tools/descrec.py (T104.1 D9): the TOML subset reader agrees with tomllib where there is one, the compiler checks what it is given and
writes a record that its own reader (and the board's, tests/coreapp/test_descrec.cc) takes back, and the reader refuses damage.

  python3 tools/test_descrec.py        (also collected by python3 -m pytest tools/test_descrec.py)
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import descrec as D  # noqa: E402

GOLD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "coreapp", "golden")
FIXTURES = ("desc_test.toml", "desc_default.toml")


def src(name):
    return open(os.path.join(GOLD, name)).read()


MIN = """
[[area]]
key = "F5"
  [[area.tab]]
  id = "t"
  name = "T"
  widget = "nodes"
"""


class MiniToml(unittest.TestCase):
    def test_agrees_with_tomllib_on_the_fixtures(self):
        try:
            import tomllib
        except ImportError:
            self.skipTest("no tomllib (python before 3.11): the subset reader is the only reader here")
        for f in FIXTURES:
            self.assertEqual(D._Toml(src(f)).parse(), tomllib.loads(src(f)), f)

    def test_both_readers_compile_to_the_same_bytes(self):
        for f in FIXTURES:
            a = D.compile_desc(D._Toml(src(f)).parse())
            self.assertEqual(a, D.compile_text(src(f)))

    def test_the_subset(self):
        t = D._Toml('''
# a comment
a = 1
b = "x\\ty\\"z"
c = 'lit\\n'
d = [1, 2,
     3, ]   # an array over lines
e = { k = "v", n = -7, h = 0x10, t = true, f = false }
[[x]]
n = 1
  [[x.y]]
  m = 2
  [[x.y]]
  m = 3
[[x]]
n = 4
[tab]
z = 5
[tab.sub]
w = 6
''').parse()
        self.assertEqual(t["a"], 1)
        self.assertEqual(t["b"], 'x\ty"z')
        self.assertEqual(t["c"], "lit\\n")
        self.assertEqual(t["d"], [1, 2, 3])
        self.assertEqual(t["e"], {"k": "v", "n": -7, "h": 16, "t": True, "f": False})
        self.assertEqual(t["x"], [{"n": 1, "y": [{"m": 2}, {"m": 3}]}, {"n": 4}])
        self.assertEqual(t["tab"], {"z": 5, "sub": {"w": 6}})

    def test_errors_name_a_line(self):
        for bad in ("a = ", "a = [1, 2", "a = 'x", "a == 1", "[x", "a = 1 b = 2", "a = \"x\\q\"", "a = 1\na = 2", "a = {k = 1, k = 2}", "= 1"):
            with self.assertRaises(D.DescError) as e:
                D._Toml(bad).parse()
            self.assertIn("TOML line", str(e.exception), bad)


class Compiler(unittest.TestCase):
    def test_round_trip_of_the_fixture(self):
        blob = D.compile_text(src("desc_test.toml"))
        d = D.parse_desc(blob)
        self.assertTrue(d["route"])
        self.assertEqual([a["key"] for a in d["area"]], ["F1", "F3", "F5", "F7", "F8"])
        self.assertEqual(d["area"][2]["tabs"], ["summary", "settings", "drives", "tools"])
        self.assertEqual([t["n"] for t in d["tab"] if t["id"] in ("drives", "tools")], [4, 3])
        self.assertEqual([t["groups"] for t in d["tab"] if t["id"] == "drives"], ["drive_a,drive_b"])
        self.assertEqual([n["order"] for n in d["node"]], [1, 5, 10, -10])
        self.assertEqual(d["ctab"], [{"area": "F7", "id": "more", "name": "More", "widget": "nodes", "groups": None, "n": 0}])
        self.assertEqual(d["poll"], {"op": "test.task", "period_ms": 250})
        self.assertEqual(d["action"][0]["ext"], [".tst2", ".tst3"])
        self.assertEqual((d["setting"][0]["lo"], d["setting"][0]["hi"], d["setting"][0]["default"]), (8, 11, 9))
        self.assertEqual(d["setting"][1]["values"], ["Round", "Square", "Odd"])
        self.assertEqual(d["hide"], ["lib_hidden"])
        self.assertEqual(d["rename"], [("lib_renamed", "Renamed by the description")])

    def test_deterministic_and_small(self):
        a, b = D.compile_text(src("desc_test.toml")), D.compile_text(src("desc_test.toml"))
        self.assertEqual(a, b)
        self.assertLess(len(a), D.MAX_SIZE)
        self.assertEqual(D.compile_desc({}), D.compile_desc({}))          # an empty description is a record of its own

    def test_tab_and_poll_rules(self):
        self.refused(MIN + '[[tab]]\narea = "F9"\nid = "x"\nname = "X"\nwidget = "nodes"\n', "F1 .. F8")
        self.refused(MIN + '[[tab]]\narea = "F5"\nid = "X Y"\nname = "X"\nwidget = "nodes"\n', "tab id")
        self.refused(MIN + '[[tab]]\narea = "F5"\nid = "x"\nname = "X"\nwidget = "carousel"\n', "widget")
        self.refused(MIN + '[[tab]]\narea = "F5"\nid = "x"\nname = "X"\nwidget = "nodes"\ngroups = ["g"]\n', "groups belong")
        self.refused(MIN + '[[tab]]\narea = "F5"\nid = "x"\nname = "X"\nwidget = "nodes"\nn = 100\n', "0 .. 99")
        self.refused(MIN + '[poll]\nop = "a.b"\nperiod_ms = 0\n', "period_ms")
        self.refused(MIN + '[poll]\nperiod_ms = 5\n', "op")
        self.refused(MIN + '[poll]\nop = "a.b"\nperiod_ms = 5\nwhen_frozen = true\n', "unknown key")
        d = D.parse_desc(D.compile_text(MIN + '[poll]\nop = "a.b"\nperiod_ms = 5\n'))
        self.assertEqual(d["poll"], {"op": "a.b", "period_ms": 5})
        self.assertIsNone(D.parse_desc(D.compile_text(MIN))["poll"])

    def test_empty_description_has_no_route(self):
        d = D.parse_desc(D.compile_desc({}))
        self.assertFalse(d["route"])
        self.assertEqual((d["area"], d["node"], d["setting"]), ([], [], []))

    def test_strings_are_shared(self):
        t = src("desc_test.toml")
        once = D.compile_text(t)
        twice = D.compile_text(t + "\n[[rename]]\nid = \"a\"\nlabel = \"Home\"\n")
        self.assertLess(len(twice) - len(once), 20)                       # "Home" is in the table already

    def refused(self, text, part):
        with self.assertRaises(D.DescError) as e:
            D.compile_text(text)
        self.assertIn(part, str(e.exception))

    def test_refusals(self):
        self.refused(MIN + "[[nodes]]\n", "unknown key")                                    # a typo is not a menu without its entry
        self.refused(MIN.replace('"F5"', '"F9"'), "F1 .. F8")
        self.refused(MIN.replace('"nodes"', '"carousel"'), "widget")
        self.refused(MIN.replace('id = "t"', 'id = "Tab One"'), "tab id")
        self.refused(MIN.replace('widget = "nodes"', 'widget = "nodes"\n  groups = ["g"]'), "groups belong")
        self.refused(MIN + '  [[area.tab]]\n  id = "t"\n  name = "U"\n  widget = "nodes"\n', "twice")
        self.refused(MIN + '  [[area.tab]]\n  id = "u"\n  name = "U"\n  widget = "nodes"\n  n = 1\n  [[area.tab]]\n  id = "v"\n  name = "V"\n  widget = "nodes"\n  n = 1\n', "twice")
        self.refused("[[area]]\nkey = \"F5\"\n", "1 to")
        self.refused(MIN + '[[node]]\nanchor = "F5/t"\nkind = "op"\nlabel = "L"\norder = 99999\n', "16 bits")
        self.refused(MIN + '[[node]]\nanchor = "F5"\nkind = "op"\nlabel = "L"\n', "anchor")
        self.refused(MIN + '[[node]]\nanchor = "F5/t"\nkind = "toggle"\nlabel = "L"\n', "kind")
        self.refused(MIN + '[[node]]\nanchor = "F5/t"\nkind = "op"\nlabel = "L"\nbind = [ { arg = "a" } ]\n', "exactly one")
        self.refused(MIN + '[[node]]\nanchor = "F5/t"\nkind = "op"\nlabel = "L"\nbind = [ { arg = "a", int = 1, str = "x" } ]\n', "exactly one")
        self.refused(MIN + '[[node]]\nanchor = "F5/t"\nkind = "op"\nlabel = "L"\nbind = [ { arg = "a", file = 1 } ]\n', "true")
        self.refused(MIN + '[[node]]\nanchor = "F5/t"\nkind = "op"\nlabel = "L"\nbind = [ { arg = "a", int = 5000000000 } ]\n', "32 bits")
        self.refused(MIN + '[[file_action]]\next = ["tst"]\nlabel = "L"\nop = "x.y"\n', "extensions with a dot")
        self.refused(MIN + '[[file_action]]\next = [".a"]\nlabel = "L"\nop = "x.y"\nsize_in = [0]\n', "sizes")
        self.refused(MIN + '[[setting]]\nkey = "K"\napply = "x.y"\n', "exactly one of values and range")
        self.refused(MIN + '[[setting]]\nkey = "K"\napply = "x.y"\nvalues = ["only"]\n', "two")
        self.refused(MIN + '[[setting]]\nkey = "K"\napply = "x.y"\nvalues = ["a", "b"]\ndefault = 2\n', "default")
        self.refused(MIN + '[[setting]]\nkey = "K"\napply = "x.y"\nrange = [5, 5]\n', "hi above lo")
        self.refused(MIN + '[[setting]]\nkey = "K"\napply = "x.y"\nrange = [1, 4]\ndefault = 9\n', "outside the range")
        self.refused(MIN + '[[setting]]\nkey = "K"\napply = "x.y"\nrange = [1, 4]\nunit = "ms"\n', "unknown key")
        self.refused(MIN + '[[rename]]\nid = "a"\n', "label")
        # limits of the board
        many = MIN + "".join('[[node]]\nanchor = "F5/t"\nkind = "group"\nlabel = "L%d"\n' % i for i in range(D.LIMITS["node"] + 1))
        self.refused(many, "the board takes")
        big = MIN + '[[node]]\nanchor = "F5/t"\nkind = "group"\nlabel = "%s"\n' % ("x" * D.MAX_SIZE)
        self.refused(big, "the board takes")

    def test_types_are_checked(self):
        self.refused(MIN.replace('key = "F5"', 'key = 5'), "key")
        with self.assertRaises(D.DescError):
            D.compile_desc({"hide": "a"})
        with self.assertRaises(D.DescError):
            D.compile_desc({"hide": [""]})
        with self.assertRaises(D.DescError):
            D.compile_desc({"area": {}})
        with self.assertRaises(D.DescError):
            D.compile_desc({"node": [{"anchor": "F5/t", "kind": "op", "label": "L", "closes_menu": 1}]})


class Reader(unittest.TestCase):
    def setUp(self):
        self.good = D.compile_text(src("desc_test.toml"))

    def test_every_truncation_refused(self):
        for n in range(len(self.good)):
            with self.assertRaises(D.DescError):
                D.parse_desc(self.good[:n])
        with self.assertRaises(D.DescError):
            D.parse_desc(self.good + b"\0")

    def test_changed_bytes_never_crash(self):
        refused = ok = 0
        for i in range(len(self.good)):
            for x in (0x01, 0x80, 0xFF):
                b = bytearray(self.good)
                b[i] ^= x
                try:
                    D.parse_desc(bytes(b))
                    ok += 1
                except D.DescError:
                    refused += 1
        self.assertGreater(refused, 100)
        self.assertGreater(ok, 100)

    def test_header_fields(self):
        for off, val, part in ((0, b"TRXE", "magic"), (4, b"\x09\x00", "version"), (6, b"\x71\x00", "header or length")):
            b = bytearray(self.good)
            b[off:off + len(val)] = val
            with self.assertRaises(D.DescError) as e:
                D.parse_desc(bytes(b))
            self.assertIn(part, str(e.exception))


MENU = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "coreapp", "cores", "trx64u", "menu.toml")
OPS = os.path.join(GOLD, "ops_trx64u.txt")


def menu_text():
    with open(MENU) as f:
        return f.read()


def ops_text():
    with open(OPS) as f:
        return f.read()


class OwnMenu(unittest.TestCase):
    """T104.2 D10: the tables of the core's own menu (task, media, filetype, core_setting), the info lines, the groups of tasks and media."""

    def test_the_menu_of_trx64u_compiles_and_reads_back(self):
        r = D.parse_desc(D.compile_text(menu_text()))
        self.assertTrue(r["own"] and r["route"])
        # T228: 14 tasks (R2/R3 took Pause / resume, Reboot clear memory, Save C64 memory and Save REU memory; R1 added Timewarp... and Monitor...; R10
        # Map USB controller buttons; R21: Save .C64RE Dump, Save MP3 Drv A-D); 207 core settings (T239.3: the look and the mouse are the application's) and 24 file types after the R21 merge (R22: no Telnet row; no .g71, no .d71; R26: no WiFi address rows); R1: no F2 and no F4 area
        self.assertEqual((len(r["task"]), len(r["media"]), len(r["ftype"]), len(r["cset"])), (19, 3, 24, 207))
        self.assertEqual([a["key"] for a in r["area"]], ["F1", "F3", "F5", "F7", "F8"])
        self.assertEqual((r["tab"][0]["id"], r["tab"][0]["lines"]), ("home", None))        # the Home desk is a nodes tab: it names no information lines
        d64 = next(f for f in r["ftype"] if f["ext"] == ".d64")
        self.assertEqual((d64["inner_ext"], [a["label"] for a in d64["inner"]]), (".prg", ["Mount & run", "Real run"]))
        self.assertEqual(next(c for c in r["cset"] if c["key"] == "Freeze mode")["when"], "reset")

    def test_every_key_of_the_new_tables_is_checked(self):
        for table, extra in (("task", 'label = "A"\nop = "x.y"\n'), ("media", 'name = "M"\nstate = "s"\n'), ("core_setting", 'key = "K"\nvalues = ["a","b"]\napply = "x"\n')):
            with self.assertRaises(D.DescError) as e:
                D.compile_text("[[%s]]\n%sdangling = 1\n" % (table, extra))
            self.assertIn("unknown key 'dangling'", str(e.exception))
        with self.assertRaises(D.DescError):
            D.compile_text('[[filetype]]\next = ".x"\nbogus = 1\n  [[filetype.action]]\n  label = "a"\n  op = "b"\n')
        with self.assertRaises(D.DescError):
            D.compile_text('[[filetype]]\next = ".x"\n  [[filetype.action]]\n  label = "a"\n  op = "b"\n  bogus = 1\n')

    def test_what_a_description_may_not_say(self):
        bad = {
            "ask_state without ask": '[[task]]\nlabel="A"\nop="x"\nask_state="s"\n',
            "pick_label without pick_dir": '[[task]]\nlabel="A"\nop="x"\nask="n"\npick_label="l"\n',
            "empty ask": '[[task]]\nlabel="A"\nop="x"\nask=""\n',
            "bad group": '[[task]]\nlabel="A"\nop="x"\ngroup="a b"\n',
            "eject_bind without eject_op": '[[media]]\nname="M"\nstate="s"\neject_bind=[{arg="a",str="b"}]\n',
            "upper case extension": '[[filetype]]\next=".D64"\n  [[filetype.action]]\n  label="a"\n  op="b"\n',
            "an extension twice": '[[filetype]]\next=".x"\n  [[filetype.action]]\n  label="a"\n  op="b"\n[[filetype]]\next=".x"\n  [[filetype.action]]\n  label="a"\n  op="b"\n',
            "no action": '[[filetype]]\next=".x"\n',
            "inner_ext without inner": '[[filetype]]\next=".x"\ninner_ext=".y"\n  [[filetype.action]]\n  label="a"\n  op="b"\n',
            "ask_off without ask": '[[filetype]]\next=".x"\n  [[filetype.action]]\n  label="a"\n  op="b"\n  ask_off=4\n',
            "one value": '[[core_setting]]\nkey="K"\nvalues=["a"]\napply="x"\n',
            "default outside": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\ndefault=2\napply="x"\n',
            "when neither": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\nwhen="soon"\napply="x"\n',
            "Picture": '[[core_setting]]\nkey="picture"\nvalues=["a","b"]\napply="x"\n',
            "Scale": '[[core_setting]]\nkey="Scale"\nvalues=["a","b"]\napply="x"\n',
            "Smoothing": '[[core_setting]]\nkey="smoothing"\nvalues=["a","b"]\napply="x"\n',
            "picture.apply": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\napply="picture.apply"\n',
            "scale.apply": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\napply="scale.apply"\n',
            "smooth.apply": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\napply="smooth.apply"\n',
            "Frame follow": '[[core_setting]]\nkey="frame follow"\nvalues=["a","b"]\napply="x"\n',
            "follow.apply": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\napply="follow.apply"\n',
            "setting with picture.apply": '[[setting]]\nkey="K"\nvalues=["a","b"]\napply="picture.apply"\n',
            "setting named Scale": '[[setting]]\nkey="Scale"\nvalues=["a","b"]\napply="x"\n',
            "pages names a group the tab lacks": MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b"]\n  ui = { sections = true, pages = "c" }'),
            "a page group the tab lacks": MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b"]\n  ui = { sections = true, pages = "P=b+c" }'),
            "a group on two pages": MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b"]\n  ui = { sections = true, pages = "b,P=a+b" }'),
            "pages without sections": MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b"]\n  ui = { pages = "b" }'),
            "a system group that is no page": MIN.replace('"nodes"', '"system"\n  groups = ["a", "b"]\n  ui = { pages = "a" }'),
            "pages_head without pages": MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b"]\n  ui = { sections = true, pages_head = "H" }'),
            "groups on a browser tab named as pages": MIN.replace('"nodes"', '"browser"\n  ui = { pages = "a" }'),
            "key twice": '[[core_setting]]\nkey="K"\nvalues=["a","b"]\napply="x"\n[[core_setting]]\nkey="k"\nvalues=["a","b"]\napply="x"\n',
            "info lines on a browser": MIN.replace('"nodes"', '"browser"\n  lines = ["Core"]'),
            "an info line that is not one": MIN.replace('"nodes"', '"info"\n  lines = ["Moon"]'),
            "groups on an info tab": MIN.replace('"nodes"', '"info"\n  groups = ["g"]'),
            "groups on a browser": MIN.replace('"nodes"', '"browser"\n  groups = ["g"]'),
        }
        for what, text in bad.items():
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(text)

    def test_a_tab_may_name_pages(self):                      # T228 D18: a group of the tab that opens as a page
        b = D.compile_text(MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b"]\n  ui = { sections = true, pages = "b" }'))
        self.assertIn(b"pages=b", b)

    def test_pages_of_several_groups_and_of_a_system_tab(self):  # T228 R5/R6: "Name=g1+g2", a heading over the page rows, a System tab's pages
        b = D.compile_text(MIN.replace('"nodes"', '"settings"\n  groups = ["a", "b", "c"]\n  ui = { sections = true, pages = "P=b+c", pages_head = "H" }'))
        self.assertIn(b"pages=P=b+c;pages_head=H", b)
        r = D.parse_desc(D.compile_text(MIN.replace('"nodes"', '"system"\n  groups = ["a", "b"]\n  ui = { pages = "N=a,b" }')))
        self.assertEqual((r["tab"][0]["widget"], r["tab"][0]["groups"]), ("system", "a,b"))

    def test_groups_of_tasks_and_media_tabs(self):
        D.compile_text(MIN.replace('"nodes"', '"tasks"\n  groups = ["quick"]'))
        D.compile_text(MIN.replace('"nodes"', '"media"\n  groups = ["slots"]'))

    def test_the_reader_refuses_a_damaged_own_menu(self):
        good = D.compile_text(menu_text())
        refused = 0
        for i in range(0, len(good), 3):
            b = bytearray(good)
            b[i] ^= 0xFF
            try:
                D.parse_desc(bytes(b))
            except D.DescError:
                refused += 1
        self.assertGreater(refused, 100)
        for n in range(0, len(good), 7):
            with self.assertRaises(D.DescError):
                D.parse_desc(good[:n])

    def test_decoding_and_compiling_again_gives_the_same_bytes(self):
        for text in [menu_text()] + [src(f) for f in FIXTURES + ("menu_alpha.toml",)]:
            b = D.compile_text(text)
            self.assertEqual(D.compile_desc(D.decoded_to_doc(D.parse_desc(b))), b)


PAD = """
[pad]
output = "joystick"
per_pad = true
buttons = ["Up", "Down", "Left", "Right", "Fire"]
default = ["up", "down", "left", "right", "face_bottom"]
"""


class Pad(unittest.TestCase):
    """T228 R10: [pad], the core's USB pads through the pad service (pad.h PadSpec): version 5 and its six header words, only when there is a pad."""

    def test_a_pad_makes_version_5_and_reads_back(self):
        b = D.compile_text(MIN + PAD)
        self.assertEqual((b[4] | b[5] << 8, b[6] | b[7] << 8), (5, D.HEADER_V5))
        r = D.parse_desc(b)
        self.assertEqual(r["pad"], {"output": "joystick", "per_pad": True, "regs": [0, 1], "buttons": ["Up", "Down", "Left", "Right", "Fire"],
                                    "default": ["up", "down", "left", "right", "face_bottom"]})
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)

    def test_a_record_without_a_pad_stays_version_4(self):
        b = D.compile_text(MIN)
        self.assertEqual((b[4] | b[5] << 8, b[6] | b[7] << 8), (4, D.HEADER))
        self.assertIsNone(D.parse_desc(b)["pad"])

    def test_the_register_output_and_every_kind_of_source(self):
        text = MIN + '[pad]\nregs = [0x100, 0x102]\nbuttons = ["A", "B", "C", "D"]\ndefault = ["face_right", "button12", "none", "start"]\n'
        r = D.parse_desc(D.compile_text(text))
        self.assertEqual((r["pad"]["output"], r["pad"]["per_pad"], r["pad"]["regs"]), ("registers", False, [0x100, 0x102]))
        self.assertEqual(r["pad"]["default"], ["face_right", "button12", "none", "start"])

    def test_refusals(self):
        cases = [
            (PAD.replace('"joystick"', '"lines"'), "output"),
            (PAD.replace('"Fire"]', '"Fire", "Fire 2"]').replace('"face_bottom"]', '"face_bottom", "face_right"]'), "5 lines"),
            (PAD.replace('"face_bottom"', '"fire"'), "not a source"),
            (PAD.replace('"face_bottom"', '"button33"'), "not a source"),
            (PAD.replace(', "face_bottom"]', ']'), "one source per button"),
            (PAD.replace('per_pad = true', 'regs = [0, 1]'), "no regs"),
            (PAD.replace('output = "joystick"\n', ''), "regs"),
            (PAD + 'extra = 1\n', "extra"),
        ]
        for text, part in cases:
            with self.assertRaises(D.DescError, msg=part) as e:
                D.compile_text(MIN + text)
            self.assertIn(part, str(e.exception))

    def test_the_reader_refuses_a_damaged_pad(self):
        good = D.compile_text(MIN + PAD)
        for word, val in ((0, 0x200), (0, 0x2), (16, 0), (16, 17), (12, 0xFFFF)):
            b = bytearray(good)
            b[D.HEADER + word:D.HEADER + word + 4] = val.to_bytes(4, "little")
            with self.assertRaises(D.DescError):
                D.parse_desc(bytes(b))
        b = bytearray(good)
        i = b.index(b"2021222341")                                  # the default map's text: up down left right face_bottom
        b[i] = ord("x")
        with self.assertRaises(D.DescError):
            D.parse_desc(bytes(b))

    def test_the_menu_of_trx64u_declares_its_joystick_pad(self):
        r = D.parse_desc(D.compile_text(menu_text()))
        self.assertEqual((r["pad"]["output"], r["pad"]["per_pad"], len(r["pad"]["buttons"])), ("joystick", True, 5))
        self.assertEqual(r["poll"], {"op": "pad.poll", "period_ms": 16})
        port = next(c for c in r["cset"] if c["apply"] == "pad.port")
        self.assertEqual((port["group"], port["values"], port["default"]), ("input", ["Port 1", "Port 2"], 1))
        self.assertIn(("input", "menu.pad_map_port"), [(t["group"], t["op"]) for t in r["task"]])


RESTART = """
[restart]
clear_ram = 65536
ram_only = true
"""


class Restart(unittest.TestCase):
    """T228 R17: a core declares what F8 "Restart core" clears (record version 5, the two words behind the pad's)."""

    def test_a_restart_makes_version_5_without_a_pad_and_reads_back(self):
        b = D.compile_text(MIN + RESTART)
        self.assertEqual((b[4] | b[5] << 8, b[6] | b[7] << 8), (5, D.HEADER_V5))
        r = D.parse_desc(b)
        self.assertEqual((r["restart"], r["pad"]), ({"clear_ram": 65536, "ram_only": True}, None))
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)
        self.assertIsNone(D.parse_desc(D.compile_text(MIN + PAD))["restart"])

    def test_ram_only_is_optional(self):
        r = D.parse_desc(D.compile_text(MIN + RESTART.replace("ram_only = true\n", "")))
        self.assertEqual(r["restart"], {"clear_ram": 65536, "ram_only": False})

    def test_refusals(self):
        cases = [
            (RESTART.replace("65536", "1000"), "multiple of 256"),
            (RESTART.replace("65536", "0"), "clear_ram"),
            (RESTART.replace("65536", str(1 << 25)), "clear_ram"),
            (RESTART + "wipe = 1\n", "wipe"),
        ]
        for text, part in cases:
            with self.assertRaises(D.DescError, msg=part) as e:
                D.compile_text(MIN + text)
            self.assertIn(part, str(e.exception))

    def test_the_reader_refuses_a_damaged_restart(self):
        good = D.compile_text(MIN + RESTART)
        for word, val in ((24, 100), (24, 1 << 25), (28, 2)):
            b = bytearray(good)
            b[D.HEADER + word:D.HEADER + word + 4] = val.to_bytes(4, "little")
            with self.assertRaises(D.DescError):
                D.parse_desc(bytes(b))

    def test_the_menu_of_trx64u_clears_the_c64_ram_on_a_restart(self):
        self.assertEqual(D.parse_desc(D.compile_text(menu_text()))["restart"], {"clear_ram": 65536, "ram_only": True})


class AgainstTheApplication(unittest.TestCase):
    """T104.2 D10: every name and binding of a menu is checked against what the application exports (tests/coreapp/golden/ops_trx64u.txt)."""

    def check(self, text):
        ops, states = D.load_ops(ops_text())
        return D.check_ops(D.parse_desc(D.compile_text(text)), ops, states)

    def test_the_menu_of_trx64u_is_what_the_application_exports(self):
        self.assertEqual(self.check(menu_text()), [])
        D.compile_checked(menu_text(), ops_text())

    def test_it_refuses_what_the_application_does_not_have(self):
        text = menu_text()
        cases = [
            ("an operation that is not exported", 'op = "machine.cold_reset"', 'op = "machine.cold_resetx"', "does not export"),
            ("an argument the operation lacks", '{ arg = "slot", str = "iec:drive_a" }', '{ arg = "slotx", str = "iec:drive_a" }', "has no argument"),
            ("an argument left unbound", 'bind = [ { arg = "path", file = true } ]\n  closes_menu = true\n\n[[filetype]]\next = ".d64"', 'bind = [ ]\n  closes_menu = true\n\n[[filetype]]\next = ".d64"', "unbound"),
            ("a state that is not exported", 'state = "c64.program"', 'state = "c64.programx"', "state"),
            ("an ask_state that is not exported", 'ask_state = "drive.suggest.d64"', 'ask_state = "drive.suggest.d65"', "state"),
            ("a text that is no enum label", 'str = "unlinked" } ]\n  closes_menu = true\n  [[filetype.action]]\n  label = "Run disk"', 'str = "linked" } ]\n  closes_menu = true\n  [[filetype.action]]\n  label = "Run disk"', "does not take"),
            ("a setting applied by a missing operation", 'apply = "drive.set_ram"', 'apply = "drive.set_ramx"', "does not export"),
            ("an ask without the typed text", 'bind = [ { arg = "slot", str = "iec:drive_a" }, { arg = "path", text = true }, { arg = "format", str = "g64" } ]\nask = "Name of the G64"', 'bind = [ { arg = "slot", str = "iec:drive_a" }, { arg = "path", str = "x" }, { arg = "format", str = "g64" } ]\nask = "Name of the G64"', "binds it 0 times"),
        ]
        for what, old, new, word in cases:
            self.assertGreaterEqual(text.count(old), 1, what)
            bad = self.check(text.replace(old, new, 1))
            self.assertTrue(bad and any(word in b for b in bad), "%s: %r" % (what, bad))

    def test_the_packer_stops_at_the_first_such_menu(self):
        with self.assertRaises(D.DescError) as e:
            D.compile_checked(menu_text().replace('apply = "drive.set_ram"', 'apply = "nope"'), ops_text(), what="menu.toml")
        self.assertIn("menu.toml", str(e.exception))
        self.assertIn("nope", str(e.exception))

    def test_a_check_that_sees_nothing_is_no_check(self):
        # the export that names nothing refuses everything: the check is not vacuous
        bad = D.check_ops(D.parse_desc(D.compile_text(menu_text())), {}, set())
        self.assertGreater(len(bad), 15)


class LibraryMenus(unittest.TestCase):
    """T104.2 D10: the packer merges the menus of the libraries into the record; the record names them."""

    def gamma(self):
        return D.compile_text('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "Gamma"\nop = "gamma.go"\n[[file_action]]\next = [".gam"]\nlabel = "Play"\nop = "gamma.play"\n')

    def test_a_library_menu_is_appended_and_named(self):
        base = D.compile_text(menu_text())
        blob, merged, kept = D.merge_lib_menus(base, [("gamma", self.gamma())])
        r = D.parse_desc(blob)
        own = [n["label"] for n in D.parse_desc(base)["node"]]
        self.assertEqual((merged, kept, r["merged"]), (["gamma"], [], ["gamma"]))
        self.assertEqual([n["label"] for n in r["node"]], own + ["Gamma"])          # the menu's own nodes stay, the library's come after them
        self.assertEqual([a["label"] for a in r["action"]], ["Play"])
        self.assertEqual(len(r["task"]), len(D.parse_desc(base)["task"]))                       # the core's own menu is what it was
        self.assertEqual(D.parse_desc(base)["merged"], [])

    def test_libraries_are_taken_in_the_order_of_their_names(self):
        a = D.compile_text('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "A"\nop = "a.go"\n')
        b, _, _ = D.merge_lib_menus(D.compile_text(menu_text()), [("zed", self.gamma()), ("alpha", a)])
        own = [n["label"] for n in D.parse_desc(D.compile_text(menu_text()))["node"]]
        self.assertEqual([n["label"] for n in D.parse_desc(b)["node"]], own + ["A", "Gamma"])
        self.assertEqual(D.parse_desc(b)["merged"], ["alpha", "zed"])

    def test_what_a_library_may_not_bring_stays_for_the_run_time(self):
        poll = D.compile_text('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "P"\nop = "p.go"\n[poll]\nop = "p.go"\nperiod_ms = 10\n')
        route = D.compile_text(src("desc_default.toml"))
        blob, merged, kept = D.merge_lib_menus(D.compile_text(menu_text()), [("poller", poll), ("router", route), ("gamma", self.gamma())])
        self.assertEqual(merged, ["gamma"])
        self.assertEqual(sorted(n for n, _ in kept), ["poller", "router"])
        self.assertEqual(D.parse_desc(blob)["merged"], ["gamma"])

    def test_nothing_to_merge_leaves_the_bytes(self):
        base = D.compile_text(menu_text())
        self.assertEqual(D.merge_lib_menus(base, [])[0], base)


VC20 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cores", "vc20", "app", "desc.toml")


class CoreSections(unittest.TestCase):
    """T239.3 D17: the data sections of version 6, from the VIC-20's own description."""

    @classmethod
    def setUpClass(cls):
        with open(VC20) as f:
            cls.text = f.read()
        cls.blob = D.compile_text(cls.text)

    def test_the_vic20_description_is_a_version_6_record_that_reads_back(self):
        import struct
        b = self.blob
        self.assertEqual(struct.unpack_from("<HH", b, 4), (6, D.HEADER_V6))
        r = D.parse_desc(b)
        self.assertTrue(r["sections"])
        self.assertEqual((r["core"]["id"], r["core"]["name"], r["core"]["caps"]), ("VC20", "VIC-20", ["iec", "drive_a"]))
        self.assertEqual([x["role"] for x in r["rom"]], ["BASIC", "KERNAL", "CHAR", "1541 drive", "JiffyDOS VC-20 KERNAL", "JiffyDOS 1541 ROM"])
        self.assertEqual(r["video"]["fb_base"], 0x01D00000)
        self.assertEqual(r["freeze"], {"reg_status": 0xA4, "mask": 2, "timeout_ms": 200, "word": False})
        self.assertEqual(r["mount"], {"names": ["Drive A"]})
        self.assertEqual(r["hotkeys"]["mod"], [58, 255])
        bound = [c["key"] for c in r["cset"] if c.get("reg_bind")]
        self.assertEqual(bound, ["Memory Expansion (at next reset)", "Block 5 ($A000), when expanded", "Datasette port"])
        self.assertIsNone(r["cset"][0]["apply"])
        self.assertEqual(r["restart"], {"clear_ram": 65536, "ram_only": False})

    def test_the_record_compiles_back_to_the_same_bytes(self):
        self.assertEqual(D.compile_desc(D.decoded_to_doc(D.parse_desc(self.blob))), self.blob)

    def test_a_record_without_sections_stays_what_it_was(self):
        import struct
        b = D.compile_text(MIN)
        self.assertEqual(struct.unpack_from("<HH", b, 4), (4, D.HEADER))
        self.assertFalse(D.parse_desc(b)["sections"])

    def _sections(self):
        import struct
        soff = struct.unpack_from("<I", self.blob, D.HEADER_V5)[0]
        return soff

    def test_a_section_the_reader_does_not_know_is_refused_by_number(self):
        import struct
        b = bytearray(self.blob)
        struct.pack_into("<I", b, self._sections(), 99)
        with self.assertRaisesRegex(D.DescError, "section 99"):
            D.parse_desc(bytes(b))

    def test_a_damaged_section_is_refused_by_name(self):
        import struct
        b = bytearray(self.blob)
        soff = self._sections()
        struct.pack_into("<I", b, soff + 4, 68)                      # the core section's length: 72 bytes -> 68
        with self.assertRaisesRegex(D.DescError, "section"):
            D.parse_desc(bytes(b))
        b = bytearray(self.blob)
        struct.pack_into("<I", b, soff + 8 + 4 * 1, 0x7FFFFFF0)       # its name: a string outside the table
        with self.assertRaises(D.DescError):
            D.parse_desc(bytes(b))

    def test_the_sections_need_their_flag_and_their_place(self):
        import struct
        b = bytearray(self.blob)
        struct.pack_into("<I", b, 12, struct.unpack_from("<I", b, 12)[0] & ~4)
        with self.assertRaises(D.DescError):
            D.parse_desc(bytes(b))
        b = bytearray(self.blob)
        struct.pack_into("<I", b, D.HEADER_V5, 100)                   # the sections would start inside the tables
        with self.assertRaises(D.DescError):
            D.parse_desc(bytes(b))

    def test_a_rom_name_behind_a_gap_is_refused(self):
        import struct
        soff = self._sections()
        at, sec = soff, None
        while at < len(self.blob):                                              # walk the sections: id, length, payload
            sid, ln = struct.unpack_from("<II", self.blob, at)
            if sid == D.SEC_ROMS:
                sec = at + 8
                break
            at += 8 + ln
        self.assertIsNotNone(sec)
        nw = sec + 4 + 4 * (3 + D.ROM_SIZES_MAX)                                # the first entry's name words
        first = struct.unpack_from("<I", self.blob, nw)[0]
        self.assertNotEqual(first, D.NONE)
        b = bytearray(self.blob)
        struct.pack_into("<II", b, nw, D.NONE, first)                           # NONE, name: the name sits behind a gap
        with self.assertRaisesRegex(D.DescError, "name behind a gap"):
            D.parse_desc(bytes(b))

    def test_what_the_compiler_refuses(self):
        for what, text in (
                ("apply and reg_bind", self.text.replace('apply = "vc20.set_jiffy"', 'apply = "vc20.set_jiffy"\nreg_bind = { reg = 1, mask = 1 }')),
                ("neither", self.text.replace('apply = "vc20.set_jiffy"', "")),
                ("bytes per value", self.text.replace("bytes = [0x00, 0x10]", "bytes = [0x00]")),
                ("no core", self.text.replace("[core]", "[core_gone]")),
                ("a flag that is no key", self.text.replace('min_contract = 1', 'min_contract = 1\nmenu_last = true')),
                ("a rom without a size", self.text.replace("sizes = [4096]", "sizes = []")),
                ("a scale of 5", self.text.replace("scale = 3", "scale = 5"))):
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(text)

    def test_the_core_section_takes_eight_caps_and_the_reader_holds_the_same_limits(self):
        import struct
        caps9 = ", ".join('"c%d"' % i for i in range(9))
        with self.assertRaises(D.DescError):
            D.compile_text(self.text.replace('caps = ["iec", "drive_a"]', "caps = [%s]" % caps9))
        text = self.text.replace('caps = ["iec", "drive_a"]', "caps = [%s]" % caps9[:caps9.rindex(",")])        # eight: fine
        D.parse_desc(D.compile_text(text))
        saved = D.CAPS_MAX
        try:
            D.CAPS_MAX = 9                                                        # a writer that allows nine: the reader still refuses the record
            big = D.compile_text(self.text.replace('caps = ["iec", "drive_a"]', "caps = [%s]" % caps9))
        finally:
            D.CAPS_MAX = saved
        with self.assertRaisesRegex(D.DescError, "section core"):
            D.parse_desc(big)
        soff = self._sections()
        for word, value in ((6, 256), (7, 65536), (9, 60001), (16, 65536)):      # min_contract, reset_ms, service_period_ms, live_ms
            b = bytearray(self.blob)
            struct.pack_into("<I", b, soff + 8 + 4 * word, value)
            with self.assertRaisesRegex(D.DescError, "section core", msg=str(word)):
                D.parse_desc(bytes(b))
        b = bytearray(self.blob)                                                  # video: reg_latest (the ninth word) above 65535
        at = soff + 8 + 72 + 8
        struct.pack_into("<I", b, at + 4 * 8, 65536)
        with self.assertRaisesRegex(D.DescError, "section video"):
            D.parse_desc(bytes(b))

    def test_the_hooks_library_of_a_core_is_a_19th_word_and_the_reader_holds_the_same_limits(self):
        # T239.3 base 3.3 (core_hooks.h): [core] hooks = "<library name>" says the core's code is the export corehooks of that library of the bundle
        import struct
        self.assertNotIn("hooks", D.parse_desc(self.blob)["core"])
        plain = D.compile_text(self.text)
        b = D.compile_text(self.text.replace("[core]\n", '[core]\nhooks = "cmodel"\n', 1))
        r = D.parse_desc(b)
        self.assertEqual(r["core"]["hooks"], "cmodel")
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)

        def core_len(blob):                                                       # section 1 is the first: its header (id, length) stands at the sections' offset
            at = struct.unpack_from("<I", blob, D.HEADER_V5)[0]
            self.assertEqual(struct.unpack_from("<I", blob, at)[0], 1)
            return struct.unpack_from("<I", blob, at + 4)[0]
        self.assertEqual((core_len(plain), core_len(b)), (72, 76))                # 18 words without the name, 19 with it
        for what, text in (("empty", '""'), ("capitals", '"CModel"'), ("16 characters", '"0123456789abcdef"'), ("a dash", '"c-model"'), ("a number", "3")):
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(self.text.replace("[core]\n", "[core]\nhooks = %s\n" % text, 1))
        D.compile_text(self.text.replace("[core]\n", '[core]\nhooks = "0123456789abcde"\n', 1))      # 15 characters: the longest
        with self.assertRaisesRegex(D.DescError, "hooks library name"):          # the reader refuses what the writer would not have written
            D.parse_desc(b.replace(b"cmodel\0", b"cModel\0"))
        with self.assertRaisesRegex(D.DescError, "hooks library name|a text is missing"):
            D.parse_desc(b.replace(b"cmodel\0", b"\0model\0"))

    def test_a_file_setting_is_kind_3_and_reads_back(self):
        # T239.3 D17: a file the person selects (Setting::kind 3), live or reset, no values of its own
        text = self.text + '\n[[core_setting]]\nkey = "Kit file"\nfile = true\nwhen = "reset"\napply = "vc20.use_kit"\n'
        b = D.compile_text(text)
        r = D.parse_desc(b)
        c = [x for x in r["cset"] if x["key"] == "Kit file"][0]
        self.assertEqual((c["file"], c["when"], len(c["values"]), c["apply"]), (True, "reset", 1, "vc20.use_kit"))
        self.assertFalse(any(x["file"] for x in r["cset"] if x["key"] != "Kit file"))
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)
        for what, extra in (("values", 'values = ["a", "b"]'), ("default", 'default = "x"'), ("text", "text = true"), ("reg_bind", "reg_bind = { reg = 1, mask = 1 }"),
                            ("base", "base = 3"), ("empty", 'empty = "none"')):
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(text + extra + "\n")
        with self.assertRaises(D.DescError, msg="no apply"):
            D.compile_text(text.replace('apply = "vc20.use_kit"', ""))

    def test_register_bits_with_a_condition_and_up_to_64_bytes(self):
        # a reg_bind setting may have a show_state (its offset sits above the time code) and 64 bytes; a text or a file setting may not be register bits
        vals = ", ".join('"v%d"' % i for i in range(64))
        by = ", ".join(str(i) for i in range(64))
        head = '\n[[core_setting]]\nkey = "Wide"\nvalues = [%s]\nwhen = "reset"\nshow_state = "x.on == yes"\nreg_bind = { reg = 5, mask = 255, bytes = [%s] }\n'
        b = D.compile_text(self.text + head % (vals, by))
        r = D.parse_desc(b)
        c = [x for x in r["cset"] if x["key"] == "Wide"][0]
        self.assertEqual((len(c["reg_bind"]["bytes"]), c["show_state"]), (64, "x.on == yes"))
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)
        with self.assertRaises(D.DescError):
            D.compile_text(self.text + head % (vals + ', "v64"', by + ", 64"))
        with self.assertRaises(D.DescError):
            D.compile_text(self.text + '\n[[core_setting]]\nkey = "T"\ntext = true\nreg_bind = { reg = 5, mask = 1 }\n')

    def test_a_video_of_zeros_is_no_picture_of_the_cores_own(self):
        # the SuperCPU's picture goes through the host's scaler: scale 0 with every other number 0, and the freeze next to it
        zero = "\n".join("%s = 0" % k for k in D.VIDEO_INTS)
        sect = "[video]\n" + zero + "\n\n"
        i, j = self.text.index("[video]"), self.text.index("[freeze]")
        text = self.text[:i] + sect + self.text[j:]
        r = D.parse_desc(D.compile_text(text))
        self.assertEqual((r["video"]["scale"], r["video"]["w"], r["freeze"]["reg_status"]), (0, 0, 0x0A4))
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), D.compile_text(text))
        for what, t in (("a width", text.replace("w = 0", "w = 4")), ("a buffer count", text.replace("nbuf = 0", "nbuf = 2"))):
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(t)
        with self.assertRaises(D.DescError, msg="scale 4"):
            D.compile_text(self.text.replace("scale = 3", "scale = 4"))

    def test_a_core_with_no_picture_has_an_empty_video_and_still_a_freeze_timeout(self):
        head = '[core]\nid = "NOPI"\nname = "N"\ntitle = "N"\nshort_title = "N"\nfolder = "n"\nstore = "N"\n'
        r = D.parse_desc(D.compile_text(head + "[video]\n[freeze]\nreg_status = 0\nmask = 0\ntimeout_ms = 200\n"))
        self.assertEqual(r["video"]["scale"], 0)
        self.assertEqual(r["freeze"], {"reg_status": 0, "mask": 0, "timeout_ms": 200, "word": False})
        with self.assertRaises(D.DescError):                                      # one key of a picture is no picture: the scale is 1 to 3 then
            D.compile_text(head + "[video]\nw = 320\n[freeze]\nreg_status = 0\nmask = 0\ntimeout_ms = 200\n")

    def test_a_count_that_is_no_number_is_a_description_error(self):
        for sect, key, bad, more in (("window_media", "nslots", '"2"', ""), ("window_media", "ntracks", "1.5", "nslots = 1\n"), ("sector_media", "nunits", "true", "")):
            with self.assertRaisesRegex(D.DescError, "'%s' is a number" % key):
                D.compile_text(self.text + "\n[%s]\n%s%s = %s\n" % (sect, more, key, bad))

    def test_a_library_merged_into_it_keeps_the_sections(self):
        lib = D.compile_text('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "L"\nop = "l.go"\n')
        blob, merged, kept = D.merge_lib_menus(self.blob, [("lib", lib)])
        r = D.parse_desc(blob)
        self.assertEqual((merged, r["core"]["id"], len(r["rom"]), r["mount"]["names"]), (["lib"], "VC20", 6, ["Drive A"]))
        self.assertEqual([n["label"] for n in r["node"]], ["L"])


class MoreSections(unittest.TestCase):
    """T239.3 D17 part 2: sections 7 .. 17 (reset writes ... media replace) from the goldens the C++ test reads (tests/coreapp/test_descsec.cc)."""

    @classmethod
    def setUpClass(cls):
        cls.a_text, cls.x_text = src("desc_sections.toml"), src("desc_sections_extra.toml")
        cls.a, cls.x = D.compile_text(cls.a_text), D.compile_text(cls.x_text)

    def test_both_read_back_and_compile_to_the_same_bytes(self):
        for b in (self.a, self.x):
            self.assertEqual(D.compile_desc(D.decoded_to_doc(D.parse_desc(b))), b)

    def test_what_the_amiga_tables_hold(self):
        r = D.parse_desc(self.a)
        self.assertEqual([(x["reg"], x["value"]) for x in r["reset_write"]], [(0xA3, 0), (0xA6, 0x21)])
        self.assertEqual(r["id_feature"], {"reg": 0xC, "mask": 8, "value": 8})
        self.assertEqual(len(r["report_reg"]), 16)
        self.assertEqual((len(r["rawkeys"]["hid"]), len(r["rawkeys"]["mod"]), r["rawkeys"]["caps_code"]), (0x68, 8, 0x62))
        self.assertEqual((r["mouse"]["kind"], r["mouse"]["clamp"], r["mouse"]["step"]), ("counters", 4000, 127))
        self.assertEqual((r["fbwin"]["x_origin"], r["fbwin"]["src_w"], r["fbwin"]["def_cols"]), (182, 753, 90))
        self.assertEqual((r["romset"]["sizes"], r["romset"]["max"], r["romset"]["version"], r["romset"]["pick"]), ([262144, 524288], 8, True, True))
        self.assertEqual((r["window_media"]["prefix"], r["window_media"]["nslots"], r["sector_media"]["nunits"]), ("DF", 4, 2))
        self.assertEqual((r["pad"]["regs"], r["pad"]["per_pad"]), ([464, 0], True))

    def test_what_no_descriptor_has_together(self):
        r = D.parse_desc(self.x)
        self.assertTrue(r["rom"][0]["check"] and not r["rom"][1]["check"])
        self.assertEqual(r["alt_video"]["x0"], -8)
        self.assertEqual(r["rawkeys"], {"held": True, "caps_usage": 255})
        self.assertEqual([m.get("replace") for m in r["media"]], ["Replace Cartridge", None, "Replace Disk"])
        self.assertEqual((r["romset"]["version"], r["romset"]["pick"]), (False, False))

    CHOICE = '\n[[core_setting]]\nkey = "Kickstart file (at reload)"\nlabel = "Kickstart file"\nwhen = "reset"\nromset_choice = true\n'

    def test_the_rom_sets_choice_is_a_setting_with_neither_operation_nor_register_bits(self):
        text = self.a_text + self.CHOICE
        b = D.compile_text(text)
        r = D.parse_desc(b)
        c = [c for c in r["cset"] if c["key"] == "Kickstart file (at reload)"][0]
        self.assertEqual((c["apply"], c["values"], c["default"], c["when"], c["label"]), (None, ["Auto (3.1 first)"] + ["-"] * 8, 0, "reset", "Kickstart file"))
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)
        for what, bad in (("no rom set behind it", text.replace("[romset]", "[romset_gone]")),
                          ("another key than the set's choice", text.replace('key = "Kickstart file (at reload)"\nlabel', 'key = "Other"\nlabel')),
                          ("values of its own", text + 'values = ["a", "b"]\n'),
                          ("an operation as well", text + 'apply = "x.y"\n')):
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(bad)

    def test_six_media_slots_and_six_mount_names_are_taken_and_seven_are_not(self):
        def text(n):
            return self.x_text + "".join('\n[[media]]\nname = "Extra%d"\nstate = "extra.s%d"\n' % (i, i) for i in range(n - 3)) + \
                '\n[mount]\nnames = [%s]\n' % ", ".join('"S%d"' % i for i in range(n))
        r = D.parse_desc(D.compile_text(text(6)))
        self.assertEqual(len(r["media"]), 6)
        with self.assertRaises(D.DescError):
            D.compile_text(text(7))

    def test_what_the_compiler_refuses(self):
        a, x = self.a_text, self.x_text
        for what, text in (
                ("a reset write beyond a byte", a.replace("value = 0x21", "value = 0x121")),
                ("an id feature with no mask", a.replace("mask = 0x8", "mask = 0")),
                ("a report register of 3 bytes", a.replace("bytes = 1", "bytes = 3", 1)),
                ("a mouse kind that does not exist", a.replace('kind = "counters"', 'kind = "ball"')),
                ("a window media slot count of 5", a.replace("nslots = 4", "nslots = 5")),
                ("a sector media with no prefix", a.replace('prefix = "HD"\n', "")),
                ("a rom set of 9", a.replace("max = 8", "max = 9")),
                ("a rom set with no size", a.replace("sizes = [262144, 524288]", "sizes = []")),
                ("rawkeys with a short modifier table", a.replace("mod = [ 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 ]", "mod = [ 0x63 ]")),
                ("held rawkeys with a table", x.replace("held = true", "held = true\nreg = 1")),
                ("a fbwin number out of range", a.replace("src_w = 753", "src_w = 40000")),
                ("a field that is no key", a.replace("[mouse]", "[mouse]\nspeed = 1")),
                ("sections without a core", x.replace("[core]", "[core_gone]")),
                ("a replace that is empty", x.replace('replace = "Replace Disk"', 'replace = ""'))):
            with self.assertRaises(D.DescError, msg=what):
                D.compile_text(text)

    def test_a_damaged_section_is_refused_by_name(self):
        import struct
        soff = struct.unpack_from("<I", self.a, D.HEADER_V5)[0]
        at, found = soff, {}
        while at < len(self.a):
            sid, ln = struct.unpack_from("<II", self.a, at)
            found[sid] = at
            at += 8 + ln
        for sid, name in ((D.SEC_RESETW, "reset writes"), (D.SEC_IDFEAT, "id feature"), (D.SEC_REPORT, "report registers"), (D.SEC_RAWKEYS, "raw keys"), (D.SEC_FBWIN, "picture window"),
                          (D.SEC_MOUSE, "mouse"), (D.SEC_ROMSET, "rom set"), (D.SEC_WINMEDIA, "window media"), (D.SEC_SECMEDIA, "sector media")):
            b = bytearray(self.a)
            struct.pack_into("<I", b, found[sid] + 4, struct.unpack_from("<I", b, found[sid] + 4)[0] + 4)
            with self.assertRaisesRegex(D.DescError, name, msg=name):
                D.parse_desc(bytes(b))

    def test_the_pad_with_one_port_may_leave_the_second_register_zero(self):
        t = self.a_text
        D.compile_text(t)
        with self.assertRaises(D.DescError):
            D.compile_text(t.replace("per_pad = true\n", ""))                     # two ports: both registers are real
        with self.assertRaises(D.DescError):
            D.compile_text(t.replace("regs = [0x1D0, 0]", "regs = [0, 0x1D0]"))    # the first never is 0


class KeyTableWindowsAndJobs(unittest.TestCase):
    """T239.3 base 3.3: the key table of a core's UI TOML (section 18), the register windows of its API TOML (section 19) and a job task. The C++ reader of the same
    rules is tests/coreapp/test_deckeys.cc (fixture tests/coreapp/golden/desc_keys.toml)."""
    ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    FULL = """
key = [
  { layout = "terminal", key = "f10", do = "decline" },
  { layout = "terminal", key = "runstop", do = "decline", when = "!monitor.wants_stop" },
  { layout = "terminal", key = "any", op = "monitor.key", pass_key = true, directive = true, direct = true },
  { layout = "timeline", key = "left", op = "tw.step", value = -1, status = true },
  { layout = "timeline", key = "return", op = "tw.resume", status_ok = true, close = true },
  { layout = "timeline", key = "ctrl+o", op = "tw.monitor", directive = true },
  { layout = "timeline", codes = [32, 126], do = "swallow" },
  { node = "scope", key = "x", op = "tw.step", value = 7, when = "tw.on" },
]
"""
    WIN = """
[[window]]
name = "audio_mixer"
address = 0xA0000500
size = 0x14
"""

    def test_a_key_table_alone_is_a_version_6_record_that_needs_no_core(self):
        b = D.compile_text(self.FULL)
        self.assertEqual(int.from_bytes(b[4:6], "little"), 6)
        r = D.parse_desc(b)
        self.assertFalse(r["sections"])                                   # no core's data sections: a menu.toml with its key table
        self.assertNotIn("core", r)
        self.assertEqual(len(r["key"]), 8)
        self.assertEqual(r["key"][2], {"layout": "terminal", "key": "any", "do": "op", "op": "monitor.key", "pass_key": True, "directive": True, "direct": True})
        self.assertEqual(r["key"][3], {"layout": "timeline", "key": "left", "do": "op", "op": "tw.step", "value": -1, "status": True})
        self.assertEqual(r["key"][1]["when"], "!monitor.wants_stop")
        self.assertEqual(r["key"][6], {"layout": "timeline", "codes": [32, 126], "do": "swallow"})
        self.assertEqual(r["key"][7]["node"], "scope")
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)            # decoding and compiling again gives the same bytes

    def test_the_table_form_and_the_array_form_are_the_same_record(self):
        rows = [{"layout": "timeline", "key": "esc", "op": "tw.cancel", "close": True}, {"layout": "timeline", "codes": [32, 126], "do": "swallow"}]
        table = '[[key]]\nlayout = "timeline"\nkey = "esc"\nop = "tw.cancel"\nclose = true\n[[key]]\nlayout = "timeline"\ncodes = [32, 126]\ndo = "swallow"\n'
        array = 'key = [ { layout = "timeline", key = "esc", op = "tw.cancel", close = true }, { layout = "timeline", codes = [32, 126], do = "swallow" } ]\n'
        self.assertEqual(D.compile_text(table), D.compile_text(array))
        self.assertEqual(D.compile_desc({"key": rows}), D.compile_text(array))
        self.assertEqual(D._Toml(table).parse(), D._Toml(array).parse())

    def test_the_key_names_are_the_keyboards(self):
        with open(os.path.join(self.ROOT, "host-app", "software", "io", "c64", "keyboard.h")) as f:
            text = f.read()
        import re
        defs = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"#define\s+(KEY_\w+)\s+(0x[0-9A-Fa-f]+)", text)}
        want = {"up": "KEY_UP", "down": "KEY_DOWN", "left": "KEY_LEFT", "right": "KEY_RIGHT", "return": "KEY_RETURN", "esc": "KEY_ESCAPE", "del": "KEY_BACK", "runstop": "KEY_BREAK",
                "space": "KEY_SPACE", "home": "KEY_HOME", "pgup": "KEY_PAGEUP", "pgdn": "KEY_PAGEDOWN", "tab": "KEY_TAB", "f10": "KEY_F10", "scrlock": "KEY_SCRLOCK", "menu": "KEY_MENU"}
        for i in range(1, 9):
            want["f%d" % i] = "KEY_F%d" % i
        self.assertEqual(sorted(D.KEY_NAMES), sorted(want))
        for n, k in want.items():
            self.assertEqual(D.KEY_NAMES[n], defs[k], n)
        for letter, code in D.KEY_CTRL.items():
            self.assertEqual(code, defs["KEY_CTRL_" + letter.upper()], letter)

    def test_a_printable_character_and_the_codes_of_a_range(self):
        b = D.compile_text('key = [ { node = "t", key = "q", op = "a.b" }, { node = "t", key = "~", op = "a.b" }, { node = "t", codes = [7, 7], op = "a.b" } ]')
        r = D.parse_desc(b)["key"]
        self.assertEqual([x.get("key") or x.get("codes") for x in r], ["q", "~", [7, 7]])
        self.assertEqual(D.compile_desc(D.decoded_to_doc(D.parse_desc(b))), b)

    def test_what_the_compiler_refuses(self):
        def bad(row, msg):
            with self.assertRaises(D.DescError, msg=msg) as e:
                D.compile_text("key = [ %s ]" % row)
            return str(e.exception)
        bad('{ key = "any", op = "a.b" }', "neither layout nor node")
        bad('{ layout = "terminal", node = "t", key = "any", op = "a.b" }', "both")
        bad('{ layout = "terminal", op = "a.b" }', "no key")
        bad('{ layout = "terminal", key = "a", codes = [1, 2], op = "a.b" }', "key and codes")
        bad('{ layout = "terminal", key = "any" }', "no op and no do")
        bad('{ layout = "terminal", key = "any", do = "op" }', "do = op without an operation")
        bad('{ layout = "terminal", key = "any", do = "decline", op = "a.b" }', "a decline with an operation")
        bad('{ layout = "terminal", key = "any", do = "swallow", close = true }', "a swallow with a flag")
        bad('{ layout = "terminal", key = "any", do = "swallow", value = 1 }', "a swallow with a value")
        bad('{ layout = "terminal", key = "any", do = "maybe", op = "a.b" }', "an unknown action")
        bad('{ layout = "terminal", key = "any", op = "a.b", value = 1, pass_key = true }', "value and pass_key")
        bad('{ layout = "terminal", key = "any", op = "a.b", value = 2147483648 }', "a value that does not fit")
        bad('{ layout = "terminal", key = "any", op = "a.b", close = "yes" }', "a flag that is no boolean")
        bad('{ layout = "terminal", key = "any", op = "a.b", pass = true }', "an unknown field")
        bad('{ layout = "Terminal", key = "any", op = "a.b" }', "a layout name in capitals")
        bad('{ node = "a b", key = "any", op = "a.b" }', "a node id with a blank")
        bad('{ layout = "terminal", key = "nokey", op = "a.b" }', "a key name nobody has")
        bad('{ layout = "terminal", key = "ctrl+q", op = "a.b" }', "a ctrl letter the keyboard does not deliver")
        bad('{ layout = "terminal", key = " ", op = "a.b" }', "the blank is space")
        bad('{ layout = "terminal", codes = [5, 4], op = "a.b" }', "lo above hi")
        bad('{ layout = "terminal", codes = [0, 512], op = "a.b" }', "a code above 0x1FF")
        bad('{ layout = "terminal", codes = [1], op = "a.b" }', "one code")
        bad('{ layout = "terminal", key = "any", op = "a.b", when = "!" }', "a bare !")
        bad('{ layout = "terminal", key = "any", op = "a.b", when = "!!x" }', "a double !")
        bad('{ layout = "terminal", key = "any", op = "a.b", when = "" }', "an empty condition")
        with self.assertRaises(D.DescError):
            D.compile_text("key = []")
        with self.assertRaises(D.DescError):
            D.compile_text("key = [ %s ]" % ", ".join(['{ layout = "terminal", key = "a", op = "a.b" }'] * (D.KEYS_MAX + 1)))
        D.compile_text("key = [ %s ]" % ", ".join(['{ layout = "terminal", key = "a", op = "a.b" }'] * D.KEYS_MAX))
        with self.assertRaises(D.DescError):
            D.compile_text("key = 5")

    def test_the_reader_refuses_a_damaged_table(self):
        import struct
        b = D.compile_text(self.FULL)
        at = struct.unpack_from("<I", b, D.HEADER_V5)[0] + 8 + 4                   # the first row's words (the section is the only one)
        self.assertEqual(struct.unpack_from("<I", b, D.HEADER_V5 + 4)[0], 8 + 4 + 36 * 8)
        for what, row, word, value in (("do 3", 3, 4, 3), ("no operation", 3, 5, D.NONE), ("a decline with an operation", 0, 5, 0), ("an unknown flag", 3, 7, 1 | 4 | 16),
                                       ("value and key", 3, 7, 1 | 2), ("a decline with a flag", 0, 7, 4), ("lo above hi", 3, 2, 0x200), ("a key above 0x1FF", 3, 3, 0x200),
                                       ("lo any, hi not", 2, 3, 5), ("an empty condition", 1, 8, 0), ("neither scope", 3, 0, D.NONE), ("both scopes", 3, 1, 0)):
            x = bytearray(b)
            struct.pack_into("<I", x, at + 36 * row + 4 * word, value)
            with self.assertRaisesRegex(D.DescError, "key rows", msg=what):
                D.parse_desc(bytes(x))
        for n in range(len(b)):
            with self.assertRaises(D.DescError, msg=n):
                D.parse_desc(b[:n])

    def test_a_window_is_written_and_read_back_by_name(self):
        b = D.compile_text(self.WIN + '[[window]]\nname = "u64_userport"\naddress = 0xA0000405\nsize = 1\n')
        r = D.parse_desc(b)
        self.assertEqual(r["window"], [{"name": "audio_mixer", "address": 0xA0000500, "size": 0x14}, {"name": "u64_userport", "address": 0xA0000405, "size": 1}])
        self.assertFalse(r["sections"])
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)

    def test_the_windows_granted_to_trx64u_are_accepted(self):
        # the runs next to the base's own registers that the handover grants: u64_userport +0x05, u64_hdmi_connectors +0x08 and +0x09, u64_pwm_duty +0x0D of the U64 block, speaker_enable
        text = "".join('[[window]]\nname = "%s"\naddress = 0x%X\nsize = %d\n' % w for w in (("u64_userport", 0xA0000405, 1), ("u64_hdmi_connectors", 0xA0000408, 2), ("u64_pwm_duty", 0xA000040D, 1),
                                                                                      ("speaker_enable", 0xA000000C, 1), ("audio_mixer", 0xA0000500, 0x14), ("palette", 0xA0080800, 0x400)))
        self.assertEqual(len(D.parse_desc(D.compile_text(text))["window"]), 6)

    def test_a_window_over_a_register_the_base_drives_is_refused_by_name_and_reference(self):
        regs = D.base_registers()
        self.assertGreaterEqual(len(regs), 22)
        wanted = {"i2c_scl", "i2c_sda", "mdio_get", "mdio_mdc", "mdio_data", "usb_hub_reset", "hw_i2c", "power", "restore", "hdmi_pll_reset", "keyb_joy", "keyb_col", "keyb_row", "case_led_select",
                  "ulpi_reset", "hdmi_reg", "ethstream_ena", "matrix_keyb", "master_volume", "master_tone", "hotkey_mask", "bling_rx_flags"}
        self.assertEqual({r[0] for r in regs}, wanted)
        for name, address, nbytes, ref in regs:
            for i, (a, size) in enumerate(((address, 1), (address + nbytes - 1, 4), (address - 3, 4))):         # the register, a block that starts on its last byte, a block that runs into it
                with self.assertRaises(D.DescError, msg="%s %x" % (name, a)) as e:
                    D.compile_text('[[window]]\nname = "w"\naddress = %d\nsize = %d\n' % (a, size))
                if i == 0:                                      # the sentence names the first register of the table the block covers: the register itself, for one byte
                    self.assertIn(name, str(e.exception))
                    self.assertIn(ref, str(e.exception))
            other = D.window_conflict(address + nbytes, 1)
            if other is None:                                         # the byte behind it belongs to nobody: a run that starts there is allowed
                D.compile_text('[[window]]\nname = "w"\naddress = %d\nsize = 1\n' % (address + nbytes))
        with self.assertRaises(D.DescError):                          # a block that runs over the whole U64 base register run
            D.compile_text('[[window]]\nname = "w"\naddress = 0xA0000400\nsize = 0x100\n')

    def test_the_references_of_the_table_name_their_register(self):
        token = {"i2c_scl": "U2PIO_SCL", "i2c_sda": "U2PIO_SDA", "mdio_get": "GET_MDIO", "mdio_mdc": "SET_MDC", "mdio_data": "SET_MDIO", "usb_hub_reset": "U2PIO_HUB_RESET",
                 "hw_i2c": "U64II_HW_I2C_BASE", "power": "U64_POWER_REG", "restore": "U64_RESTORE_REG", "hdmi_pll_reset": "U64_HDMI_PLL_RESET", "keyb_joy": "U64II_KEYB_JOY",
                 "keyb_col": "U64II_KEYB_COL", "keyb_row": "U64II_KEYB_ROW", "case_led_select": "U64_CASELED_SELECT",
                 "ulpi_reset": "U2PIO_ULPI_RESET", "hdmi_reg": "U64_HDMI_REG", "ethstream_ena": "U64_ETHSTREAM_ENA", "matrix_keyb": "0x0300", "master_volume": "U64_AUDIO_MIXER + 0x50",
                 "master_tone": "U64_AUDIO_MIXER + 0x51", "hotkey_mask": "U64_AUDIO_MIXER + 0x60", "bling_rx_flags": "BLING_RX_FLAGS"}
        for name, address, nbytes, ref in D.base_registers():
            path, _, line = ref.rpartition(":")
            with open(os.path.join(self.ROOT, path)) as f:
                lines = f.read().splitlines()
            self.assertIn(token[name], lines[int(line) - 1], "%s: %s" % (name, ref))
        # the addresses are those of the headers
        defs = {}
        import re
        for h in ("u2p.h", "u64.h"):
            with open(os.path.join(self.ROOT, "host-app", "software", "system", h)) as f:
                for m in re.finditer(r"#define\s+(\w+)\s+\(U2P_IO_BASE\s*\+\s*(0x[0-9A-Fa-f]+)\)", f.read()):
                    defs[m.group(1)] = 0xA0000000 + int(m.group(2), 16)
        u64 = defs["U64_IO_BASE"]
        for name, off in (("power", 1), ("restore", 2), ("hdmi_pll_reset", 4), ("keyb_joy", 6), ("keyb_col", 0xA), ("keyb_row", 0xB), ("case_led_select", 0xE), ("hdmi_reg", 0), ("ethstream_ena", 0xF)):
            self.assertEqual({r[0]: r[1] for r in D.base_registers()}[name], u64 + off, name)
        self.assertEqual({r[0]: r[1] for r in D.base_registers()}["hw_i2c"], defs["U64II_HW_I2C_BASE"])
        self.assertEqual({r[0]: r[1] for r in D.base_registers()}["matrix_keyb"], defs["MATRIX_KEYB"])
        self.assertEqual({r[0]: r[1] for r in D.base_registers()}["ulpi_reset"], 0xA0000000 + 0xF)

    def test_what_a_window_must_be(self):
        def bad(text):
            with self.assertRaises(D.DescError):
                D.compile_text(text)
        bad('[[window]]\nname = "w"\naddress = 0xA0000500\nsize = 0\n')
        bad('[[window]]\nname = "w"\naddress = 0xA0000500\nsize = 0x100001\n')
        bad('[[window]]\nname = "w"\naddress = 0xFFFFFFF0\nsize = 0x20\n')
        bad('[[window]]\nname = "W"\naddress = 0xA0000500\nsize = 4\n')
        bad('[[window]]\nname = "w"\naddress = 0xA0000500\nsize = 4\n[[window]]\nname = "w"\naddress = 0xA0000600\nsize = 4\n')
        bad('[[window]]\nname = "w"\naddress = 0xA0000500\nsize = 4\nflags = 1\n')
        bad('[[window]]\naddress = 0xA0000500\nsize = 4\n')
        bad('[[window]]\nname = "w"\naddress = -1\nsize = 4\n')
        D.compile_text('[[window]]\nname = "w"\naddress = 0xA0100000\nsize = 0x100000\n')
        import struct
        b = bytearray(D.compile_text(self.WIN))
        at = struct.unpack_from("<I", b, D.HEADER_V5)[0] + 8 + 4
        struct.pack_into("<I", b, at + 4, 0xA0000008)                       # the reader holds the same rule: the window moved onto the I2C clock
        struct.pack_into("<I", b, at + 8, 1)
        with self.assertRaisesRegex(D.DescError, "i2c_scl"):
            D.parse_desc(bytes(b))
        for n in range(len(b)):
            with self.assertRaises(D.DescError):
                D.parse_desc(bytes(b[:n]))

    def test_a_data_section_still_needs_its_core(self):
        with self.assertRaisesRegex(D.DescError, "needs \\[core\\]"):
            D.compile_text('[[reset_write]]\nreg = 1\nvalue = 2\n' + self.WIN)
        b = D.compile_text(self.FULL + '[core]\nid = "TEST"\nname = "Test"\ntitle = "T"\nshort_title = "T"\nfolder = "test"\nstore = "test"\n' + self.WIN)
        r = D.parse_desc(b)
        self.assertTrue(r["sections"])
        self.assertEqual((r["core"]["id"], len(r["window"]), len(r["key"])), ("TEST", 1, 8))

    def test_a_job_task(self):
        b = D.compile_text('[[task]]\ngroup = "top"\nlabel = "Dump"\nop = "ring.dump"\njob = true\n[[task]]\nlabel = "Plain"\nop = "ring.plain"\n'
                           '[[task]]\nlabel = "Named"\nop = "ring.save"\nask = "Name"\nbind = [ { arg = "name", text = true } ]\njob = true\n')
        r = D.parse_desc(b)
        self.assertEqual([t["job"] for t in r["task"]], [True, False, True])
        self.assertEqual(D.compile_desc(D.decoded_to_doc(r)), b)
        self.assertEqual(int.from_bytes(b[4:6], "little"), 4)                          # a job needs no version of its own: a flag of the task
        head = '[[task]]\nlabel = "Dump"\nop = "ring.dump"\n'
        for text in (head + 'job = true\nhotkey = "J"\n', head + 'job = true\nask = "N"\npick_dir = true\n', head + 'job = "yes"\n'):
            with self.assertRaises(D.DescError, msg=text):
                D.compile_text(text)
        import struct
        t0 = struct.unpack_from("<I", b, 24 + 8 * D.ORDER.index("task"))[0]
        for what, add in (("a hotkey", ord("J") << 8), ("a folder", 2), ("an unknown flag", 8)):
            x = bytearray(b)
            struct.pack_into("<I", x, t0 + 16, struct.unpack_from("<I", x, t0 + 16)[0] | add)
            with self.assertRaises(D.DescError, msg=what):
                D.parse_desc(bytes(x))

    def test_the_key_rows_are_checked_against_what_the_application_exports(self):
        ops = "op\ttw.step\tvalue:int:-100:100:0:\nop\ttw.cancel\nop\tmon.key\tkey:int:0:8191:0:\nstate\tmon.stop\n"
        o, st = D.load_ops(ops)

        def check(row):
            return D.check_ops(D.parse_desc(D.compile_text("key = [ %s ]" % row)), o, st)
        self.assertEqual(check('{ layout = "a", key = "left", op = "tw.step", value = -1 }, { layout = "a", key = "esc", op = "tw.cancel" }, { layout = "a", key = "any", op = "mon.key", pass_key = true, when = "!mon.stop" }'), [])
        self.assertTrue(any("not exported" in b for b in check('{ layout = "a", key = "left", op = "tw.nope" }')))
        self.assertTrue(any("takes" in b for b in check('{ layout = "a", key = "left", op = "tw.step" }')))                       # the operation wants its value
        self.assertTrue(any("takes" in b for b in check('{ layout = "a", key = "left", op = "tw.cancel", value = 1 }')))          # and this one none
        self.assertTrue(any("outside" in b for b in check('{ layout = "a", key = "left", op = "tw.step", value = 1000 }')))
        self.assertTrue(any("state" in b for b in check('{ layout = "a", key = "left", op = "tw.cancel", when = "mon.nope" }')))

    def test_two_files_are_one_record(self):
        menu = 'machine_info = "A"\n' + self.FULL
        desc = self.WIN + '[core]\nid = "TEST"\nname = "Test"\ntitle = "T"\nshort_title = "T"\nfolder = "test"\nstore = "test"\n'
        both = D.compile_files([("menu.toml", menu), ("desc.toml", desc)])
        r = D.parse_desc(both)
        self.assertEqual((r["core"]["name"], len(r["key"]), len(r["window"]), r["machine_info"]), ("Test", 8, 1, "A"))
        self.assertEqual(D.compile_files([("desc.toml", desc), ("menu.toml", menu)]), both)       # the order of the files changes nothing
        self.assertEqual(D.compile_files([("one.toml", menu)]), D.compile_text(menu))                # one file is what it was
        with self.assertRaises(D.DescError) as e:
            D.compile_files([("menu.toml", self.FULL), ("desc.toml", self.FULL + self.WIN)])
        self.assertIn("menu.toml", str(e.exception))
        self.assertIn("desc.toml", str(e.exception))
        self.assertIn("'key'", str(e.exception))
        with self.assertRaises(D.DescError) as e:
            D.compile_files([("a.toml", '[[node]]\nanchor = "F5/x"\nkind = "op"\nlabel = "L"\nop = "o"\n'), ("b.toml", '[[node]]\nanchor = "F5/x"\nkind = "op"\nlabel = "M"\nop = "o"\n')])
        self.assertIn("a.toml", str(e.exception))
        self.assertIn("b.toml", str(e.exception))
        with self.assertRaisesRegex(D.DescError, "bad.toml"):
            D.compile_files([("menu.toml", menu), ("bad.toml", "key = [")])

    def test_a_library_that_brings_a_key_table_or_windows_stays_for_the_run_time(self):
        lib = D.compile_text(self.WIN)
        base = D.compile_text('[core]\nid = "TEST"\nname = "Test"\ntitle = "T"\nshort_title = "T"\nfolder = "test"\nstore = "test"\n')
        blob, merged, kept = D.merge_lib_menus(base, [("lib", lib)])
        self.assertEqual((blob, merged), (base, []))
        self.assertEqual([k[0] for k in kept], ["lib"])

    def test_a_record_without_them_is_what_it_was(self):
        for f in FIXTURES:
            b = D.compile_text(src(f))
            r = D.parse_desc(b)
            self.assertNotIn("key", r)
            self.assertNotIn("window", r)
            self.assertLess(int.from_bytes(b[4:6], "little"), 6)


class TomlA(unittest.TestCase):
    """T239.3 P1-1 (D30, D34): TOML A, record version 7. The C++ reader's side is tests/coreapp/test_desctoml.cc (same fixture)."""
    MAPS = os.path.join(os.path.dirname(GOLD), "..", "..", "app", "sdkpkg", "maps")
    BASE = ('[io]\niobase = 0x04000000\nu2p_io_base = 0xA0000000\n[identity]\nid = "TSTA"\nblock = "ident"\n'
            '[[block]]\nname = "ident"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0000800\nsize = 0x100\n'
            '[[block.property]]\nname = "id"\noffset = 0\nwidth = 32\ndir = "r"\n'
            '[[block.property]]\nname = "build"\noffset = 4\nwidth = 32\ndir = "r"\n')

    def fixture(self):
        return D.compile_checked(src("desc_toml_a.toml"), maps=D.load_maps([self.MAPS]))

    def test_the_fixture_is_version_7_and_reads_back_whole(self):
        b = self.fixture()
        self.assertEqual(int.from_bytes(b[4:6], "little"), 7)
        r = D.parse_desc(b)
        self.assertEqual(r["io"], {"iobase": 0x04000000, "u2p_io_base": 0xA0000000})
        self.assertEqual(r["identity"], {"id": "TSTA", "block": "ident", "build": 0x1234ABCD})
        self.assertEqual([x["name"] for x in r["block"]], ["ident", "cart_flash", "cmd_if", "mixer"])
        cart = r["block"][1]
        self.assertEqual(len(cart["property"]), 13)
        evt = next(p for p in cart["property"] if p["name"] == "evt")
        self.assertEqual((evt["width"], evt["dir"]), (32, "r"))
        op = next(f for f in evt["field"] if f["name"] == "op")
        self.assertEqual([(x["name"], x["value"]) for x in op["state"]], [("program", 1), ("sector_erase", 2), ("chip_erase", 3)])
        self.assertEqual(r["block"][3]["owner"], "core")

    def test_what_is_decoded_compiles_to_the_same_bytes(self):
        b = self.fixture()
        self.assertEqual(D.compile_desc(D.decoded_to_doc(D.parse_desc(b))), b)

    def test_a_module_that_serves_several_drivers_is_one_block_per_range(self):
        doc = self.BASE + ('[[block]]\nname = "page4"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0004000\nsize = 0x100\nmap = "c64cart"\n'
                           '[[block]]\nname = "page3"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0003000\nsize = 0x100\n')
        r = D.parse_desc(D.compile_checked(doc, maps=D.load_maps([self.MAPS])))
        self.assertEqual([x["module"] for x in r["block"]], ["trx_io"] * 3)

    def test_the_key_table_and_windows_records_stay_below_version_7(self):
        for f in FIXTURES:
            self.assertLess(int.from_bytes(D.compile_text(src(f))[4:6], "little"), 7)

    def test_a_map_comes_from_a_file(self):
        maps = D.load_maps([self.MAPS])
        self.assertEqual(sorted(maps), ["c64cart", "c64cart_regs", "cbm1541", "gcr_coder", "uci"])
        b = '[[block]]\nname = "u"\nmodule = "uci"\nowner = "base"\naddress = 0x04044000\nsize = 0x10\nmap = "%s"\n'
        self.assertTrue(D.compile_checked(self.BASE + b % "uci", maps=maps))


if __name__ == "__main__":
    unittest.main()
