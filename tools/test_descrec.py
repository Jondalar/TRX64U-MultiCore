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
        # Map USB controller buttons; R21: Save .C64RE Dump, Save MP3 Drv A-D); 216 core settings and 24 file types after the R21 merge (R22: no Telnet row; no .g71, no .d71; R26: no WiFi address rows); R1: no F2 and no F4 area
        self.assertEqual((len(r["task"]), len(r["media"]), len(r["ftype"]), len(r["cset"])), (19, 3, 24, 216))
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


if __name__ == "__main__":
    unittest.main()
