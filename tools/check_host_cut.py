#!/usr/bin/env python3
"""T192 D23 / T192.2 (P1 the cut, P4 the key map as a table): the host's screen is a client of host_core. Red when hostui.cc

  K1  acts on a key itself: a KEY_ name, a character literal or `k ==` inside the key handlers (the body of on_key only). The keys are host_core.cc's
      table (HcKey); the handlers act on the intents it names.
  K2  carries a hand-written footer legend: a string literal that starts with "UP/DOWN" (the legends are generated from the table: hc_foot). 
  K3  acts on an intent itself: an HCA_ / HCX_ name in hostui.cc. The intents are host_core_ui.cc's; the screen forwards the input (on_key) and draws.
  S1  changes the state of the host outside host_core: a call of a function that writes boot.cfg, the picture, the flash lock, the FTP switch, or a
      redefinition of `cfg` that is not the read-only view of the core. These calls live in host_core.cc (hc_*).
  N1  holds or writes the navigation state (T192.2 P1): the view, the cursors, the browser's folder and rows, the modes of the browser and of the settings pages, the notes. That state
      is host_core_nav.h's; hostui.cc reads it as `nv` (= hc_nav(), const) and changes it through hc_nav_* / hc_browse_* / hc_note_* / hc_cm_*. Red: a file-scope variable of such a name,
      a write to `nv.`, its address taken, a macro that hides it, the writable header or accessor (host_core_nav_rw.h, hc_nav_rw), a const_cast of it.
  R2  copies a refusal text of a module (T192.2-S14): a string literal of cor_result_str, cfw_result_str, txu_result_str (the producing modules) appears in hostui.cc or a
      host_core* file. The pages show the module's own words (the function), never a copy of them.
  C1  host_core*.h / host_core*.cc name LVGL, trx_*, FreeRTOS, a task delay or a key wait: the core builds and runs without a screen (T192.2-S1).
  S2  calls a mutator of the world (the start sequence, the flash writers, the file operations, the WiFi module) from a function that is not in the
      ledger (tools/host_cut_ledger.txt). The ledger is what is NOT cut yet: each line is a (function, mutator) pair of hostui.cc. A pair that is not in the
      ledger is a new path around the service: red. A ledger line that no longer occurs is stale: red (it is deleted when the work moves).

Usage: tools/check_host_cut.py [hostapp dir]        exit 1 and a line per breach.
       tools/check_host_cut.py --selftest           plants every breach in a copy; each must be reported, the real tree must be green.
       tools/check_host_cut.py --ledger [dir]       prints the pairs found, in the ledger's format.
"""
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DIR = os.path.join(HERE, "..", "host-app", "software", "hostapp")
LEDGER = os.path.join(HERE, "host_cut_ledger.txt")

KEY_HANDLERS = ["on_key"]
LEGEND_ALLOWED = set()                                  # no legend is written by hand any more

# S1: functions that write the state the core owns. hostui.cc calls hc_* instead.
STATE_WRITERS = [
    r"bootcfg_(?:init|parse|add|remove|rename|autostart|move)", r"startcheck_migrate",
    r"hv_(?:user_set|keep|start|tick|init|load_default)", r"hv_saved_store", r"flashprot_set", r"ftp_state_set",
]
# N1: the names of the navigation state (host_core_nav.h)
NAV_NAMES = ["view", "sel", "ssel", "nsel", "bsel", "mfirst", "efirst", "bpath", "bl", "bn", "bup", "br_mode", "wm_mode", "cm_mode", "cm_top", "cpat", "note", "nnote",
             "cm_nlines", "cm_rep", "cm_lines", "cm_msg", "del_arm", "wf_forget_arm"]
# S2: mutators of the world that the flows still call from hostui.cc (the ledger names who may).
WORLD_MUTATORS = [
    r"core_stage", r"core_swap", r"core_jump", r"legacy_stage", r"legacy_jump", r"flashprot_before_handover",
    r"fw_write\w*", r"fw_flash_write\w*", r"fw_flash_stamp\w*", r"txu_stage", r"txu_write\w*", r"esp_flash\w*", r"espflash_\w+", r"esp_reboot\w*",
    r"fo_run", r"cm_delete", r"hostfs_(?:save|remove|rename|mkdir)\w*", r"fmt_(?:run|format|write)\w*", r"exfat\w*",
    r"wifi_net_(?:cfg_set|radio|forget|disconnect|connect|autoconnect|rescan)", r"hostreport_write", r"fw_restore\w*", r"fwr_\w+", r"fwc_apply\w*",
]


def strip(src):
    """Comments and string/char contents blanked (same length, newlines kept), so a regex never sees text inside either."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + "".join(ch if ch == "\n" else "x" for ch in src[i + 1:j - 1]) + c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def functions(clean):
    """name -> (start offset, end offset) of the body of each function defined at column 0 ('type name(args) {' possibly over lines)."""
    res = {}
    for m in re.finditer(r"^(?:static\s+|inline\s+)*[A-Za-z_][\w\s\*&:<>,]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?\{", clean, re.M):
        name = m.group(1)
        if name in ("if", "for", "while", "switch"):
            continue
        i = m.end() - 1
        depth = 0
        j = i
        while j < len(clean):
            if clean[j] == "{":
                depth += 1
            elif clean[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        res.setdefault(name, (i, j + 1))
    return res


def line_of(src, off):
    return src.count("\n", 0, off) + 1


def func_at(funcs, off):
    best = None
    for name, (a, b) in funcs.items():
        if a <= off < b and (best is None or a > funcs[best][0]):
            best = name
    return best


CORE_FORBIDDEN = [r"\blv_\w+", r"\bLV_\w+", r"\btrx_\w+", r"\bvTaskDelay\b", r"\bxTask\w+", r"\bhost_getch\w*", r"\bFreeRTOS\b", r"\blvgl\b", r"\bfm_(?:draw|service)\b"]


REFUSE_MODULES = [("cor.cc", "cor_result_str"), ("cfw.cc", "cfw_result_str"), ("txu.cc", "txu_result_str")]


def refuse_literals(d):
    """The string literals (ten characters or more) of the refusal-text functions of the producing modules."""
    lits = set()
    for fname, fn in REFUSE_MODULES:
        path = os.path.join(d, fname)
        if not os.path.exists(path):
            path = os.path.join(DEFAULT_DIR, fname)               # the selftest's copies hold the screen and the core only
        if not os.path.exists(path):
            continue
        src = open(path).read()
        m = re.search(r"\b%s\s*\([^)]*\)\s*\{" % fn, src)
        if not m:
            continue
        depth, j = 0, m.end() - 1
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        for lit in re.findall(r'"((?:[^"\\\n]|\\.)*)"', src[m.end():j]):
            if len(lit) >= 10:
                lits.add(lit)
    return lits


def core_files(d):
    return sorted(f for f in os.listdir(d) if re.match(r"host_core\w*\.(?:h|cc)$", f))


def read_ledger(path):
    pairs = set()
    if os.path.exists(path):
        for ln in open(path):
            ln = ln.split("#")[0].strip()
            if ln:
                f, m = ln.split()
                pairs.add((f, m))
    return pairs


def scan(d, ledger_path=LEDGER, want_ledger=False):
    raw = open(os.path.join(d, "hostui.cc")).read()
    clean = strip(raw)
    funcs = functions(clean)
    bad = []
    # K1
    for h in KEY_HANDLERS:
        if h not in funcs:
            bad.append("K1: key handler %s not found in hostui.cc (the gate reads the handlers by name)" % h)
            continue
        a, b = funcs[h]
        body = clean[a:b]
        for pat, what in ((r"\bKEY_[A-Z0-9_]+\b", "a KEY_ name"), (r"'x'", "a character literal"), (r"\bk\s*(?:==|!=)", "a comparison of the key")):
            for m in re.finditer(pat, body):
                bad.append("K1: hostui.cc:%d: %s acts on %s (%s): the keys are host_core.cc's table, act on the intent" % (line_of(raw, a + m.start()), h, what, m.group(0)))
    # K3
    for m in re.finditer(r"\b(?:HCA|HCX)_[A-Z0-9_]+\b", clean):
        bad.append("K3: hostui.cc:%d: %s: the intents are the core's (host_core_ui.cc); the screen forwards the input" % (line_of(raw, m.start()), m.group(0)))
    # K2: string literals that start with UP/DOWN
    for m in re.finditer(r'"UP/DOWN[^"\n]*"', raw):
        ln = line_of(raw, m.start())
        text = raw.splitlines()[ln - 1]
        if not any(a in text for a in LEGEND_ALLOWED):
            bad.append('K2: hostui.cc:%d: a hand-written footer legend %s: generate it from the key map (hc_foot)' % (ln, m.group(0)[:40] + '..."'))
    # S1
    pat = re.compile(r"\b(?:%s)\s*\(" % "|".join(STATE_WRITERS))
    for m in pat.finditer(clean):
        bad.append("S1: hostui.cc:%d: %s changes state the core owns: call the hc_* command" % (line_of(raw, m.start()), m.group(0).rstrip("( ")))
    for m in re.finditer(r"#\s*define\s+cfg\b(.*)", raw):
        if m.group(1).strip().split("//")[0].strip() != "(hc_cfg())":
            bad.append("S1: hostui.cc:%d: `cfg` is redefined as something other than the core's read-only view" % line_of(raw, m.start()))
    if re.search(r"\bconst_cast\s*<\s*(?:BootCfg|BootEntry)\b", clean):
        bad.append("S1: hostui.cc: const_cast of the boot menu: the core's read-only views are not to be cast away")
    # N1
    for m in re.finditer(r'#\s*include\s*"(host_core\w*_rw\.h)"', raw):
        bad.append("N1: hostui.cc:%d: includes %s: the screen reads the navigation state, it does not hold the writable view" % (line_of(raw, m.start()), m.group(1)))
    for m in re.finditer(r"\bhc_\w*_rw\s*\(", clean):
        bad.append("N1: hostui.cc:%d: %s: the writable accessors are the core's own" % (line_of(raw, m.start()), m.group(0)))
    names = "|".join(NAV_NAMES)
    for m in re.finditer(r"^(?:static\s+)?(?!const\b|enum\b|struct\b|class\b|return\b)[A-Za-z_][\w\s\*:<>]*?\b(%s)\b\s*(?:\[[^\]\n]*\]\s*)*(?:=[^;\n(]*)?(?:,[^;\n(]*)?;" % names, clean, re.M):
        bad.append("N1: hostui.cc:%d: a file-scope variable `%s`: the navigation state is host_core_nav.h's" % (line_of(raw, m.start()), m.group(1)))
    for m in re.finditer(r"#\s*define\s+(%s)\b" % names, raw):
        bad.append("N1: hostui.cc:%d: a macro `%s` hides the navigation state" % (line_of(raw, m.start()), m.group(1)))
    for m in re.finditer(r"#\s*define\s+nv\b(.*)", raw):
        if m.group(1).strip().split("//")[0].strip() != "(hc_nav())":
            bad.append("N1: hostui.cc:%d: `nv` is redefined as something other than the core's read-only view" % line_of(raw, m.start()))
    wpat = [r"\bnv\.\w+(?:\[[^\]]*\])*\s*(?:=(?!=)|\+\+|--|\+=|-=|\|=|&=|\^=)", r"(?:\+\+|--)\s*nv\.", r"\b(?:strcpy|strncpy|strcat|memcpy|memset|snprintf|sprintf)\s*\(\s*nv\."]
    for pat in wpat:
        for m in re.finditer(pat, clean):
            bad.append("N1: hostui.cc:%d: writes the navigation state (%s): use the core's hc_nav_* / hc_browse_* / hc_note_* commands" % (line_of(raw, m.start()), m.group(0).strip()))
    if re.search(r"\bconst_cast\s*<[^>]*(?:HcNav|nv\b)", clean):
        bad.append("N1: hostui.cc: const_cast of the navigation state")
    # R2
    lits = refuse_literals(d)
    for fname in ["hostui.cc"] + core_files(d):
        text = raw if fname == "hostui.cc" else open(os.path.join(d, fname)).read()
        ctext = strip(text)
        for lit in sorted(lits):
            for m in re.finditer(re.escape('"' + lit + '"'), text):
                if ctext[m.start():m.start() + 2] == '"x':        # a literal in code (strip() blanks the text of comments away: not here)
                    bad.append("R2: %s:%d: a copy of the module's refusal text \"%s\": show the module's own words (the *_result_str function)" % (fname, line_of(text, m.start()), lit))
    # C1
    for f in core_files(d):
        src = open(os.path.join(d, f)).read()
        cl = strip(src)
        inc = re.findall(r'#\s*include\s*[<"]([^>"]+)[>"]', src)
        for pat in CORE_FORBIDDEN:
            for m in re.finditer(pat, cl):
                bad.append("C1: %s:%d: %s: the core does not know the screen or the scheduler" % (f, line_of(src, m.start()), m.group(0)))
        for i in inc:
            if re.search(r"lvgl|lv_|trx_lv|trxview|FreeRTOS|task\.h|trxcanvas", i):
                bad.append("C1: %s: includes %s: the core does not know the screen or the scheduler" % (f, i))
    # S2
    found = set()
    mpat = re.compile(r"\b(%s)\s*\(" % "|".join(WORLD_MUTATORS))
    for m in mpat.finditer(clean):
        f = func_at(funcs, m.start())
        if f is None:
            continue
        found.add((f, m.group(1)))
    if want_ledger:
        return sorted(found)
    ledger = read_ledger(ledger_path)
    for f, mu in sorted(found - ledger):
        bad.append("S2: hostui.cc: %s calls %s and is not in tools/host_cut_ledger.txt: a new path around the service (move it behind host_core, or put it in the ledger with the spec item that removes it)" % (f, mu))
    for f, mu in sorted(ledger - found):
        bad.append("S2: tools/host_cut_ledger.txt: %s %s no longer occurs in hostui.cc: delete the line (the ledger only shrinks)" % (f, mu))
    return bad, len(found)


def selftest():
    rc = 0
    real = os.path.abspath(DEFAULT_DIR)
    bad, n = scan(real)
    if bad:
        print("selftest: the real tree is not green:")
        for b in bad:
            print("  " + b)
        return 1
    src = open(os.path.join(real, "hostui.cc")).read()
    plants = [
        ("K1 KEY_ name", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    if (k == KEY_F1) { }", 1), "K1:"),
        ("K1 character literal", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    if (k == 'c') { }", 1), "K1:"),
        ("K3 intent", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    int zz = HCA_ENTRY_START;", 1), "K3:"),
        ("K2 legend", lambda s: s + '\nstatic const char *zz_legend = "UP/DOWN select    RETURN open";\n', "K2:"),
        ("S1 boot.cfg writer", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    bootcfg_remove(0, 0);", 1), "S1:"),
        ("S1 picture", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    hv_keep();", 1), "S1:"),
        ("S1 cfg redefined", lambda s: s.replace("#define cfg (hc_cfg())", "#define cfg (zz_cfg)", 1), "S1:"),
        ("N1 file-scope state", lambda s: s.replace("#define nv (hc_nav())", "#define nv (hc_nav())\nstatic int bsel;", 1), "N1:"),
        ("N1 write", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    nv.sel = 0;", 1), "N1:"),
        ("N1 write by function", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    strcpy(nv.bpath, \"/\");", 1), "N1:"),
        ("N1 hidden by macro", lambda s: s.replace("#define nv (hc_nav())", "#define nv (hc_nav())\n#define sel (hc_nav().sel)", 1), "N1:"),
        ("N1 writable header", lambda s: s.replace('#include "host_core_nav.h"', '#include "host_core_nav.h"\n#include "host_core_nav_rw.h"', 1), "N1:"),
        ("N1 nv redefined", lambda s: s.replace("#define nv (hc_nav())", "#define nv (zz_nav)", 1), "N1:"),
        ("R2 copied refusal text", lambda s: s + '\nstatic const char *zz_refuse = "SHA-256 mismatch: the file is damaged";\n', "R2:"),
        ("S2 new mutator path", lambda s: s.replace("static void on_key(int k)\n{", "static void on_key(int k)\n{\n    core_jump(0);", 1), "S2:"),
    ]
    for name, fn, tag in plants:
        t = tempfile.mkdtemp()
        try:
            new = fn(src)
            if new == src:
                print("selftest: %s: the plant changed nothing" % name)
                rc = 1
                continue
            open(os.path.join(t, "hostui.cc"), "w").write(new)
            b2, _ = scan(t)
            if not any(x.startswith(tag) for x in b2):
                print("selftest: %s: NOT reported" % name)
                rc = 1
            else:
                print("selftest: %s: reported" % name)
        finally:
            shutil.rmtree(t)
    # the core must not name the screen
    t = tempfile.mkdtemp()
    try:
        for f in os.listdir(real):
            if f.startswith("host_core") or f == "hostui.cc":
                shutil.copy(os.path.join(real, f), os.path.join(t, f))
        cc = open(os.path.join(t, "host_core.cc")).read()
        open(os.path.join(t, "host_core.cc"), "w").write(cc + "\nstatic void zz(void) { vTaskDelay(1); lv_obj_t *o = 0; }\n")
        b2, _ = scan(t)
        ok = any(x.startswith("C1:") for x in b2)
        print("selftest: C1 the core names the scheduler: %s" % ("reported" if ok else "NOT reported"))
        rc = rc or (0 if ok else 1)
    finally:
        shutil.rmtree(t)
    # a stale ledger line
    t = tempfile.mkdtemp()
    try:
        lp = os.path.join(t, "ledger.txt")
        shutil.copy(LEDGER, lp)
        with open(lp, "a") as f:
            f.write("net_key core_jump\n")
        b2, _ = scan(real, lp)
        print("selftest: stale ledger line: %s" % ("reported" if any(x.startswith("S2:") and "no longer occurs" in x for x in b2) else "NOT reported"))
        rc = rc or (0 if any("no longer occurs" in x for x in b2) else 1)
    finally:
        shutil.rmtree(t)
    return rc


def main():
    args = sys.argv[1:]
    if args[:1] == ["--selftest"]:
        return selftest()
    if args[:1] == ["--ledger"]:
        d = args[1] if len(args) > 1 else DEFAULT_DIR
        for f, m in scan(d, want_ledger=True):
            print("%s %s" % (f, m))
        return 0
    d = args[0] if args else DEFAULT_DIR
    bad, n = scan(d)
    for b in bad:
        print(b)
    if bad:
        return 1
    print("check_host_cut: green; %d (function, mutator) pairs still in hostui.cc (the ledger: what is not cut yet)" % n)
    return 0


sys.exit(main())
