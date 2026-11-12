#!/usr/bin/env python3
"""T192.3 S1 and S6 (D23): the core menu controller (coreapp/src/menuctl.cc, menuctl.h) is navigation, dialogs and rows - and nothing of the domain.

What it refuses in menuctl.cc and menuctl.h (comments and string contents do not count: only the code is looked at):
  - an #include of pad.h writeback.h t64.h monitor.h volume.h wifinet.h partial.h home.h engines.h trx64u_ops.h video.h
  - a name that begins pad_ wb_ t64_ mon_ volume_ or wifi (a call, a member, a type)
  - ops_call / ops_call_direct / ops_state / ops_run_bound with a literal operation or state name (the name of an operation is data: the key table, the flow table, the
    descriptor - never a string in the controller)
  - a KEY_ constant outside the three raw-input handlers (text_key, pick_key, entry_key): the keys are the table's (menukeytab.cc)
  - the C64's magic left-arrow code 0x60 outside those handlers

    tools/check_menuctl_gate.py             check the tree
    tools/check_menuctl_gate.py --list      the same, one line per finding, exit 0 (work list)
    tools/check_menuctl_gate.py --selftest  plant each fault in a scratch copy: every one must be found
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ["coreapp/src/menuctl.cc", "coreapp/src/menuctl.h"]
RAW_HANDLERS = {"text_key", "pick_key", "entry_key"}
BAD_INCLUDES = {"pad.h", "writeback.h", "t64.h", "monitor.h", "volume.h", "wifinet.h", "partial.h", "home.h", "engines.h", "trx64u_ops.h", "video.h"}
BAD_PREFIX = re.compile(r"\b(?:pad_|wb_|t64_|mon_|volume_|wifi)\w*")
OPS_LITERAL = re.compile(r"\b(ops_call|ops_call_direct|ops_state|ops_run_bound)\s*\(\s*[^,()]+,\s*\"")
KEY_CONST = re.compile(r"\bKEY_[A-Z0-9_]+\b")
MAGIC_LEFT_ARROW = re.compile(r"\b0x60\b")
FUNC = re.compile(r"^[A-Za-z_][^;]*\bMenuCtl::(\w+)\s*\(")


def strip(text):
    """Comments and the inside of string and character literals become blanks (newlines stay), so the checks see code only."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            q = c
            out.append(q)
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else "_")
                i += 1
            out.append(q)
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def findings(rel, text):
    res = []
    raw_lines = text.split("\n")
    code = strip(text).split("\n")
    func = None
    for no, (raw, line) in enumerate(zip(raw_lines, code), 1):
        m = FUNC.match(line)
        if m:
            func = m.group(1)
        elif line.startswith("}"):
            func = None if not line.startswith("};") else func
        inc = re.match(r"\s*#\s*include\s+\"([^\"]+)\"", raw)
        if inc and inc.group(1) in BAD_INCLUDES:
            res.append((rel, no, "include of %s" % inc.group(1)))
        for m in BAD_PREFIX.finditer(line):
            res.append((rel, no, "domain name %s" % m.group(0)))
        m = OPS_LITERAL.search(raw) if "ops_" in line else None
        if m:
            # the literal must be in the code, not in a comment: check on the stripped line that the call is there, then on the raw text for the string
            if re.search(r"\b" + m.group(1) + r"\s*\(", line):
                res.append((rel, no, "%s with a literal name" % m.group(1)))
        in_raw = func in RAW_HANDLERS
        if not in_raw:
            for m in KEY_CONST.finditer(line):
                res.append((rel, no, "%s outside the raw-input handlers (in %s)" % (m.group(0), func or "the header")))
            if MAGIC_LEFT_ARROW.search(line):
                res.append((rel, no, "0x60 outside the raw-input handlers (in %s)" % (func or "the header")))
    return res


def run(root, files=FILES):
    res = []
    for rel in files:
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            res += findings(rel, f.read())
    return res


def selftest():
    faults = {
        "pad_state(": "void MenuCtl::begin_map(int p)\n{\n    uint32_t b; uint8_t d;\n    pad_state(0, &b, &d);\n}\n",
        "wb_answer": "void MenuCtl::x()\n{\n    int rc = wb_answer(c_, 1, 0, 0);\n}\n",
        "include pad.h": "#include \"pad.h\"\n",
        "include video.h": "#include \"video.h\"\n",
        "literal ops_call": "void MenuCtl::x()\n{\n    ops_call(c_, \"wifi.toggle\", 0, 0, &r);\n}\n",
        "literal ops_state": "void MenuCtl::x()\n{\n    const char *w = ops_state(c_, \"wifi.onoff\");\n}\n",
        "KEY_ in the controller": "MenuCtl::Result MenuCtl::run_intent(int k)\n{\n    if (k == KEY_RETURN) return R_NONE;\n}\n",
        "0x60": "MenuCtl::Result MenuCtl::run_intent(int k)\n{\n    if (k == 0x60) k = 1;\n}\n",
        "volume_ call": "void MenuCtl::x()\n{\n    volume_is_generic(s);\n}\n",
        "mon_ call": "void MenuCtl::x()\n{\n    mon_key(c_, 1);\n}\n",
        "t64_ call": "void MenuCtl::x()\n{\n    t64_list(0, 0, 0, 0);\n}\n",
        "wifi name": "void MenuCtl::x()\n{\n    wifi_toggle();\n}\n",
    }
    ok = True
    for name, code in faults.items():
        if not findings("scratch.cc", code):
            print("selftest: the gate did not see: %s" % name)
            ok = False
    clean = "void MenuCtl::x()\n{\n    // pad_state( KEY_RETURN ops_call(c, \"x\", 0, 0, 0) 0x60\n    const char *s = \"pad_state( KEY_RETURN\";\n}\n"
    if findings("scratch.cc", clean):
        print("selftest: the gate saw a fault in a comment or a string")
        ok = False
    allowed = "bool MenuCtl::text_key(int k, Result *out)\n{\n    if (k == KEY_RETURN) return true;\n}\n"
    if findings("scratch.cc", allowed):
        print("selftest: the gate refused a KEY_ constant inside a raw-input handler")
        ok = False
    if ok:
        print("check_menuctl_gate selftest: ok (%d faults planted, all found)" % len(faults))
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    res = run(ROOT)
    for rel, no, what in res:
        print("%s:%d: %s" % (rel, no, what))
    if "--list" in argv:
        print("check_menuctl_gate: %d finding(s)" % len(res))
        return 0
    if res:
        print("check_menuctl_gate: %d finding(s): the controller holds domain logic or a raw key (T192.3 S1, S6)" % len(res))
        return 1
    print("check_menuctl_gate: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
