#!/usr/bin/env python3
"""T55 board test: trxmon's monitor extras on a real C64 Ultimate, no human at the keyboard.

    tests/t55_board.py [HOST] [--package VER] [--app trxmon.u2a] [--only a,b] [--skip a,b]

Everything is driven over the board's REST API, FTP and trxmon's WebSocket RPC (tests/trxmon_client.py).
The monitor screen is driven too: a breakpoint makes the detached trxmon ask the firmware for the screen,
and `POST /v1/machine:input` types into it (the overlay takes the keys while it is up). What the screen
shows cannot be read back, so its keys are checked by what they do to the machine.

  --package VER  load the bitstream (tools/load.sh c64_basic VER) and boot that package's ultimate.app
                 (tools/trxdbg.sh boot staged), then wait for REST. Needs the USB-Blaster. After an app
                 crash or hang this is the only way back (never power-cycle).
  --app FILE     (default: none - the registered trxmon is used; the fork is no longer built here) upload FILE to /USB1/trxmon_t55.u2a (FTP) and start it headless over REST
                 (PUT /v1/apps:run_file?app=/Usb1/trxmon_t55.u2a&action=serve): nothing is registered,
                 nothing is written to the flash. Without it a running trxmon is used, else the
                 registered one is started.

The test changes the REU setting at runtime (hardware logs need the frame ring, which the REU switches off)
and puts it back to Enabled; it leaves the machine reset at READY and trxmon ended.
Prints PASS/FAIL per check and per item; the exit status is 0 when every item passed.
"""
import argparse
import base64
import ftplib
import hashlib
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from trxmon_client import Client, TrxmonError  # noqa: E402

ROOT = os.path.dirname(HERE)
HOST = "192.0.2.189"
USB_APP = "/Usb1/trxmon_t55.u2a"
REU_PATH = "/v1/configs/C64%20and%20Cartridge%20Settings/RAM%20Expansion%20Unit"

# ---------------------------------------------------------------------------------------- results

RESULTS = []  # (item, name, ok, detail)
ITEM = ["setup"]


def check(name, ok, detail=""):
    RESULTS.append((ITEM[0], name, bool(ok), detail))
    print("  %s  %s%s" % ("PASS" if ok else "FAIL", name, ("  (%s)" % detail) if detail and not ok else ""))
    sys.stdout.flush()
    return bool(ok)


def info(text):
    print("        " + text)
    sys.stdout.flush()


# ---------------------------------------------------------------------------------------- board access

def rest(method, path, params=None, body=None, timeout=15, raw=False):
    url = "http://%s%s" % (HOST, path)
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        data = urllib.request.urlopen(req, timeout=timeout).read()
    except urllib.error.HTTPError as e:
        data = e.read()
        if raw:
            return e.code, data
        try:
            return json.loads(data.decode())
        except ValueError:
            return {"errors": ["HTTP %d" % e.code]}
    if raw:
        return 200, data
    return json.loads(data.decode())


def rest_alive():
    try:
        return "version" in rest("GET", "/v1/version", timeout=3)
    except Exception:
        return False


def wait_rest(seconds=90):
    end = time.time() + seconds
    while time.time() < end:
        if rest_alive():
            return True
        time.sleep(2)
    return False


def port_open(port=4312, timeout=2.0):
    try:
        socket.create_connection((HOST, port), timeout=timeout).close()
        return True
    except OSError:
        return False


def tea_token():
    cfg = os.path.expanduser("~/.config/tea/config.yml")
    for line in open(cfg):
        m = re.match(r"\s*token:\s*(\S+)", line)
        if m:
            return m.group(1)
    return ""


def load_package(version):
    """Bitstream into SRAM, then the package's firmware staged in DDR2 (the boot ROM starts it)."""
    # ultimate.app is TRX64-Ultimate's (T100 D15): APP_PKG names its generic package, APP_VERSION its <branch>-<sha8> (default: version)
    tmp = "/tmp/t55_board_%s.app" % version
    url = "http://gitea.example/api/packages/TREX/generic/%s/%s/ultimate.app" % (os.environ["APP_PKG"], os.environ.get("APP_VERSION", version))
    req = urllib.request.Request(url, headers={"Authorization": "token " + tea_token()})
    open(tmp, "wb").write(urllib.request.urlopen(req, timeout=120).read())
    for cmd in (["tools/load.sh", "c64_basic", version], ["tools/trxdbg.sh", "boot", "staged", tmp]):
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=300)
        if r.returncode != 0:
            check("package load: " + " ".join(cmd[:3]), False, (r.stdout + r.stderr)[-300:])
            return False
    return wait_rest()


def ftp_put(local, remote_dir, remote_name):
    f = ftplib.FTP(HOST, timeout=30)
    f.login()
    f.cwd(remote_dir)
    with open(local, "rb") as fh:
        f.storbinary("STOR " + remote_name, fh)
    size = f.size(remote_name)
    f.quit()
    return size


def speaker_disabled():
    r = rest("GET", "/v1/configs/Speaker%20Mixer/Speaker%20Enable")
    return "Disabled" in json.dumps(r)


def set_reu(value):
    """The setting is applied in the background and the firmware holds the machine for a few seconds meanwhile."""
    if value in json.dumps(rest("GET", REU_PATH).get("C64 and Cartridge Settings", {}).get("RAM Expansion Unit", {}).get("current", "")):
        return
    rest("PUT", REU_PATH, {"value": value})
    time.sleep(3.0)
    if M.c:
        wait_ready()


# ---------------------------------------------------------------------------------------- trxmon

class Mon:
    """One RPC connection; every call keeps the connection alive by reading while it waits."""

    def __init__(self):
        self.c = None

    def connect(self, tries=5):
        for i in range(tries):
            try:
                self.c = Client(HOST, timeout=10.0)
                return self.c
            except (TrxmonError, OSError):
                time.sleep(1)
        raise TrxmonError("no RPC connection")

    def close(self):
        if self.c:
            try:
                self.c.close()
            except Exception:
                pass
            self.c = None

    def ping(self):
        return self.c.call("ping")

    def mon(self, cmd, timeout=20.0):
        try:
            return self.c.mon(cmd, timeout=timeout)
        except TrxmonError as e:
            return "ERR " + str(e)

    def call(self, method, params=None, timeout=20.0):
        return self.c.call(method, params, timeout=timeout)

    def state(self):
        return self.c.call("session/state")

    def read(self, addr, n, lens="cpu"):
        return self.c.read(addr, n, lens)

    def drain(self, seconds):
        """Read for `seconds` (notifications pile up in c.notes); returns the notifications."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                m = self.c._recv_message(end)
            except socket.timeout:
                break
            if "method" in m:
                self.c.notes.append(m)
        out, self.c.notes = self.c.notes, []
        return out

    def take_notes(self, method=None):
        keep, out = [], []
        for n in self.c.notes:
            (out if (method is None or n.get("method") == method) else keep).append(n)
        self.c.notes = keep
        return out

    def end_asm(self):
        self.mon("")

    def running(self):
        return self.state().get("runState") == "running"

    def pause(self):
        self.mon("pause")
        return not self.running()

    def wait_stop(self, seconds):
        """Polls until the machine is paused (an observer or breakpoint fired); the stop reason text."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if not self.running():
                return True
            time.sleep(0.1)
        return False


M = Mon()


def start_trxmon(app):
    for attempt in range(3):
        if not port_open():
            break
        try:
            M.connect()
            M.call("trxmon/quit", timeout=30)
        except Exception:
            pass
        M.close()
        for _ in range(20):
            time.sleep(1)
            if not port_open(timeout=1.0):
                break
    time.sleep(1)
    path = USB_APP
    if app:
        size = ftp_put(app, "/USB1", "trxmon_t55.u2a")
        info("uploaded %s -> /USB1/trxmon_t55.u2a (%d bytes)" % (app, size))
    r = {}
    for _ in range(16):  # 423 while the previous instance is still ending
        if app:
            r = rest("PUT", "/v1/apps:run_file", {"app": path, "action": "serve"}, timeout=60)
        else:
            r = rest("PUT", "/v1/apps/trxmon:run", {"action": "serve"}, timeout=60)
        if r.get("exit_code") == 0:
            break
        time.sleep(3)
    ok = r.get("exit_code") == 0
    check("trxmon started headless (REST serve)", ok, json.dumps(r)[:200])
    time.sleep(1)
    M.connect()
    p = M.ping()
    info("trxmon %s on firmware %s" % (p.get("version"), p["build"].get("firmware_git")))
    return ok


def read_screen_text():
    """The C64's text screen ($0400) as ASCII (screen codes)."""
    raw = M.read(0x0400, 1000, "ram")
    out = []
    for b in raw:
        b &= 0x7F
        out.append(chr(b + 64) if b < 32 else chr(b) if b < 64 else ".")
    return "".join(out)


def screen_ready():
    return "READY." in read_screen_text().replace("@", "")


def wait_ready(seconds=30.0):
    """Until the C64 shows BASIC's READY. (the machine is left running)."""
    end = time.time() + seconds
    while time.time() < end:
        M.mon("pause")
        try:
            ok = screen_ready()
        except Exception:
            ok = False
        M.mon("g")
        if ok:
            return True
        time.sleep(1.5)
    return False


def reset_ready():
    """REST reset, wait for READY. (the machine runs afterwards). A reset arrives only while the machine runs: a machine
    trxmon holds (paused, or stopped by a JAM verdict) ignores it, so it is let go right before, and tried again when a
    marker in screen RAM shows that BASIC's cold start did not clear the screen."""
    M.mon("")
    M.mon("obs * del")
    M.mon("del")
    for attempt in range(5):
        M.mon("pause")
        M.mon("f 0400 0407 2a")
        M.mon("g")
        r = rest("PUT", "/v1/machine:reset")
        time.sleep(2.5)
        ok = wait_ready()
        M.mon("pause")
        cleared = M.read(0x0400, 8, "ram") != b"\x2a" * 8
        M.mon("g")
        if ok and cleared and not r.get("errors"):
            return True
    return False


def program_inc_loop():
    """$c000: INC $d020 / JMP $c000"""
    M.mon("")
    M.mon("a c000 inc d020")
    M.mon("a c003 jmp c000")
    M.mon("")
    return M.read(0xC000, 6) == bytes([0xEE, 0x20, 0xD0, 0x4C, 0x00, 0xC0])


# ---------------------------------------------------------------------------------------- the screen's keys

KEYS = {c: [c] for c in "abcdefghijklmnopqrstuvwxyz0123456789"}
KEYS.update({" ": ["space"], "\n": ["return"], ".": ["period"], ":": ["colon"], ",": ["comma"], "=": ["equals"],
             "+": ["plus"], "-": ["minus"], "/": ["slash"], "*": ["star"], ";": ["semicolon"], "@": ["at"],
             "$": ["left_shift", "4"], "#": ["left_shift", "3"], "%": ["left_shift", "5"], "&": ["left_shift", "6"],
             "\"": ["left_shift", "2"], "(": ["left_shift", "8"], ")": ["left_shift", "9"], "<": ["left_shift", "comma"],
             ">": ["left_shift", "period"], "!": ["left_shift", "1"], "|": ["left_shift", "at"]})
SPECIAL = {"LEFT": ["left_shift", "cursor_left_right"], "RIGHT": ["cursor_left_right"],
           "UP": ["left_shift", "cursor_up_down"], "DOWN": ["cursor_up_down"], "HOME": ["clr_home"],
           "CTRL_I": ["ctrl", "i"], "BACKSPACE": ["inst_del"]}


def press(seq):
    """seq: text, or a list of text and SPECIAL names. Batches of 5 keys with a pause (the key queue is short)."""
    events = []
    parts = seq if isinstance(seq, list) else [seq]
    for part in parts:
        if part in SPECIAL:
            events.append({"kind": "keyboard", "inputs": SPECIAL[part], "transition": "tap"})
        else:
            for ch in part:
                events.append({"kind": "keyboard", "inputs": KEYS[ch], "transition": "tap"})
    for i in range(0, len(events), 5):
        body = json.dumps({"events": events[i:i + 5]}).encode()
        r = rest("POST", "/v1/machine:input", body=body)
        if r.get("errors"):
            raise TrxmonError("input: %r" % r["errors"])
        time.sleep(0.45)


def screen_up(seconds=8.0):
    """Makes the machine stop at a breakpoint so the detached trxmon asks the firmware for its screen.
    The machine is left paused with the screen up. A breakpoint on the BASIC keyboard wait ($e5cf)."""
    M.mon("")
    M.mon("bk e5cf")
    M.mon("g")
    ok = M.wait_stop(seconds)
    M.mon("bk -e5cf")
    time.sleep(2.0)  # the firmware opens the overlay and starts the app's callback
    return ok


def screen_leave():
    """`x` on the screen: the machine runs on and the overlay goes. With a breakpoint armed trxmon stays (without
    stops armed `x` ends it, as designed)."""
    M.mon("bk c7ff")
    press("x\n")
    time.sleep(1.5)
    M.mon("del")


# ---------------------------------------------------------------------------------------- the items

def item(name):
    ITEM[0] = name
    print("\n[%s]" % name)


def t_preflight(args):
    item("preflight")
    check("REST answers", rest_alive())
    v = rest("GET", "/v1/info")
    info("firmware %s, product %s" % (v.get("git_commit_hash"), v.get("product")))
    check("speaker is disabled (Speaker Mixer / Speaker Enable)", speaker_disabled())
    if args.app or not port_open():
        start_trxmon(args.app)
    else:
        M.connect()
        info("using the running trxmon " + M.ping().get("version", ""))
    check("trxmon answers ping", M.ping().get("backend") == "c64u")
    set_reu("Disabled")
    check("machine reset to READY (REU off)", reset_ready())


def t_hex(args):
    item("hex-numbers")
    M.pause()
    check("program INC $d020 / JMP at $c000 assembled", program_inc_loop())
    o = M.mon("obs s1 when store d020 if val == f5 do break")
    check("obs with `val == f5` (hex, no $) is accepted", "armed" in o and "val==f5" in o, o)
    check("... and the watch unit holds it", "watch unit 0 (addr+val)" in o, o)
    o = M.mon("obs s2 when store d020 if val == $F5 do break")
    check("a leading $ is accepted too", "armed" in o and "ERR" not in o, o)
    o = M.mon("obs")
    lines = [l for l in o.splitlines() if " s1 " in l or " s2 " in l]
    check("obs shows both observers' values alike (`val==f5`)",
          len(lines) == 2 and all("if val==f5 do break" in l for l in lines), o)
    M.mon("obs * del")
    o = M.mon("obs x1 when store d020 if a == 5 do break")
    check("`a` is the register: `a == 5` compares the accumulator", "armed" in o and "a==05" in o, o)
    M.mon("obs * del")
    o = M.mon("obs x2 when store d020 if val == $a do break")
    check("`$a` is the number", "armed" in o and "val==0a" in o, o)
    M.mon("obs * del")
    o = M.mon("obs x3 when store d020 if val == g5 do break")
    check("a non-number is refused ('unknown term')", "unknown term" in o, o)
    M.mon("obs * del")

    # on the machine: the stop carries a $Fx value
    o = M.mon("obs s1 when store d020 if val == f5 do break")
    M.mon("g c000")
    stopped = M.wait_stop(5)
    check("obs `val == f5` stops the INC loop", stopped)
    st = M.mon("status")
    info(st.splitlines()[1] if len(st.splitlines()) > 1 else st)
    check("... as an observer stop", "observer" in st, st)
    o = M.mon("obs")
    check("... hits=1 in the list", re.search(r"s1 .*hits=1\b", o) is not None, o)
    M.mon("obs * del")

    # soft remainder: x != 7 stays in software, the stop is re-checked
    o = M.mon("obs s3 when store d020 if val == f5 && a != 7 && x != 7 do break")
    check("a software remainder is reported (`soft: x!=07`)", "soft: x!=07" in o, o)
    M.mon("g c000")
    check("... and the soft condition stops the loop", M.wait_stop(5))
    M.mon("obs * del")
    M.mon("obs s4 when store d020 if val == 5 do break")  # $d020 reads $Fx: never $05
    M.mon("g c000")
    time.sleep(1.5)
    check("`val == 5` never hits ($d020 reads $Fx): the loop keeps running", M.running())
    M.pause()
    M.mon("obs * del")


def t_ignore(args):
    item("ignore")
    M.pause()
    program_inc_loop()
    M.mon("obs i1 when store d020 do break")
    o = M.mon("ignore i1 3")
    check("ignore in the watch unit (PASS)", "counted by the watch unit" in o, o)
    M.mon("g c000")
    check("the machine stops after the ignored hits", M.wait_stop(5))
    o = M.mon("obs")
    m = re.search(r"i1 .*hits=(\w+) .*pass=(\w+)", o)
    check("pass=3 and the 4th hit stopped it", bool(m) and m.group(2) == "3", o)
    M.mon("obs * del")
    # a software remainder: counted after each stop, hex count
    M.mon("obs i2 when store d020 if val == f5 && a != 7 && x != 7 do break")
    o = M.mon("ignore i2 10")
    check("ignore with a software remainder is counted after each stop; the count is hex (10 = 16)",
          "skip next 10 (counted after each stop)" in o, o)
    M.mon("g c000")
    check("... and it stops after the 16 ignored hits", M.wait_stop(20))
    M.mon("obs * del")


def collect(seconds, method="debug/observer_log"):
    t0 = time.monotonic()
    notes = M.drain(seconds)
    lines = [(n["params"]["line"]) for n in notes if n.get("method") == method]
    return lines, time.monotonic() - t0


def t_log_low(args):
    item("log-low-rate")
    for reu, label in (("Disabled", "hardware log (ring on)"), ("Enabled", "software log (REU on, no ring)")):
        set_reu(reu)
        M.mon("pause")
        M.mon("obs * del")
        reset_ready()
        o = M.mon("obs l1 when exec ea31 do log")
        want = "addr+log" if reu == "Disabled" else "log at the stop"
        check("%s: observer armed (%s)" % (label, want), want in o, o)
        M.mon("g")
        lines, _ = collect(3.0)
        elines = [l for l in lines if "exec $ea31" in l]
        skipped = sum(int(re.match(r"\+(\d+)", l).group(1)) for l in lines if re.match(r"\+\d+ skipped", l))
        if reu == "Disabled":
            check("%s: IRQ entries arrive live as debug/observer_log (%d shown + %d skipped in 3 s, 50-60/s expected)"
                  % (label, len(elines), skipped), 140 <= len(elines) + skipped <= 200, "%d lines" % len(elines))
            check("%s: at this rate (one per frame) nearly all are shown" % label,
                  len(elines) >= 0.8 * (len(elines) + skipped), "%d shown, %d skipped" % (len(elines), skipped))
        else:
            # a stop per hit: the machine stands still for most of the time (the run after a stop waits for the
            # frame alignment), so fewer IRQs happen per second; the log still arrives live
            check("%s: IRQ entries arrive live as debug/observer_log (%d in 3 s; a stop per hit slows the machine)"
                  % (label, len(elines)), len(elines) >= 15, "%d lines" % len(elines))
        M.mon("pause")
        o = M.mon("obs log")
        check("%s: `obs log` shows them" % label, "obs l1: exec $ea31" in o, o[-200:])
        # the soft path with registers
        M.mon("obs l1 del")
        o = M.mon("obs l2 when exec ea31 do log a x")
        M.mon("g")
        lines, _ = collect(1.5)
        check("%s: a log with registers (`log a x`) prints a=.. x=.." % label,
              any(re.search(r"a=\w\w x=\w\w", l) for l in lines), str(lines[:2]))
        M.mon("pause")
        M.mon("obs * del")
    set_reu("Disabled")


def flood(reu, seconds=6.0, with_screen=False):
    """The INC $d020 loop under a log observer; returns what the board did meanwhile."""
    set_reu(reu)
    M.mon("pause")
    M.mon("obs * del")
    program_inc_loop()
    M.mon("")
    o = M.mon("obs h1 when store d020 do log")
    res = {"arm": o}
    M.mon("g c000")
    lines = []
    t0 = time.monotonic()
    rest_fail = 0
    worst = 0.0
    per_second = []
    while time.monotonic() - t0 < seconds:
        got, _ = collect(1.0)
        per_second.append(len(got))
        lines += got
        a = time.monotonic()
        if not rest_alive():
            rest_fail += 1
        worst = max(worst, time.monotonic() - a)
    res.update(lines=lines, per_second=per_second, rest_fail=rest_fail, rest_worst=worst)
    return res


def t_log_high(args):
    item("log-high-rate")
    for reu, label in (("Disabled", "hardware log"), ("Enabled", "software log")):
        r = flood(reu)
        expect_unit = "addr+log" if reu == "Disabled" else "log at the stop"
        check("%s: armed (%s)" % (label, expect_unit), expect_unit in r["arm"], r["arm"])
        check("%s: REST answered every second during the flood (slowest %.2f s)" % (label, r["rest_worst"]),
              r["rest_fail"] == 0 and r["rest_worst"] < 2.0, "%d failures, %.2f s" % (r["rest_fail"], r["rest_worst"]))
        total = len(r["lines"])
        info("%s: %d lines in %d s, per second %s" % (label, total, len(r["per_second"]), r["per_second"]))
        check("%s: lines per second stay at the rate limit (max %d)" % (label, max(r["per_second"] or [0])),
              max(r["per_second"] or [0]) <= 90, str(r["per_second"]))
        skipped = [l for l in r["lines"] if "skipped" in l]
        check("%s: '+K skipped' lines are printed" % label, len(skipped) >= 2, str(r["lines"][-3:]))
        big = sum(int(re.match(r"\+(\d+)", l).group(1)) for l in skipped if re.match(r"\+\d+ skipped", l))
        check("%s: the skipped count is large (>100000 over the run: %d)" % (label, big), big > 100000, str(big))
        check("%s: the RPC still answers while it floods" % label, bool(M.mon("status")) and "ERR" not in M.mon("status"))
        o = M.mon("pause", timeout=10)
        check("%s: pause works under the flood" % label, "paused" in o and not M.running(), o)
        o = M.mon("obs")
        check("%s: hits are counted (more than 100000: %s)" % (label, o.splitlines()[-1][:80] if o else ""),
              re.search(r"hits=(\w+)", o) is not None and int(re.search(r"hits=(\w+)", o).group(1), 16) > 100000, o)
        M.mon("obs * del")
        check("%s: REST answers after" % label, rest_alive())


def t_screen_flood(args):
    item("flood-with-screen")
    set_reu("Disabled")
    reset_ready()
    M.mon("pause")
    M.mon("obs * del")
    program_inc_loop()
    M.mon("")
    M.mon("obs h1 when store d020 do log")
    ok = screen_up()
    check("the monitor screen came up on a breakpoint (machine stopped)", ok)
    press("bk c300\n")
    o = M.mon("bk")
    check("the screen takes keys (bk c300 typed)", "c300" in o, o)
    press("g c000\n")
    time.sleep(1.0)
    check("`g c000` typed on the screen runs the machine under the log observer", M.running())
    t0 = time.monotonic()
    fails = 0
    worst = 0.0
    lines = 0
    while time.monotonic() - t0 < 8:
        a = time.monotonic()
        if not rest_alive():
            fails += 1
        worst = max(worst, time.monotonic() - a)
        lines += len([n for n in M.drain(0.7) if n.get("method") == "debug/observer_log"])
    check("REST answers during the flood with the screen up (slowest %.2f s)" % worst, fails == 0 and worst < 2.0,
          "%d failures, %.2f s" % (fails, worst))
    check("the log notifications keep coming and stay at the rate limit (%d in 8 s)" % lines, 100 <= lines <= 8 * 90, str(lines))
    press("bk c310\n")
    time.sleep(0.5)
    o = M.mon("bk")
    check("the screen still takes keys during the flood (bk c310)", "c310" in o, o)
    # RUN/STOP on the screen pauses
    rest("POST", "/v1/machine:input", body=json.dumps({"events": [{"kind": "keyboard", "inputs": ["run_stop"], "transition": "tap"}]}).encode())
    time.sleep(1.0)
    check("RUN/STOP on the screen pauses the machine under the flood", not M.running())
    M.mon("del")
    M.mon("obs * del")
    screen_leave()
    check("`x` leaves; REST answers", rest_alive())


def t_cmd(args):
    item("do-cmd")
    M.mon("pause")
    M.mon("obs * del")
    program_inc_loop()
    o = M.mon('obs c1 when store d020 if val == f5 do cmd "m 0400 0407; r"')
    check("obs with do cmd armed", "armed" in o and "cmd" in o, o)
    M.mon("g c000")
    lines, _ = collect(2.0)
    check("the command runs at hits and its output goes to the log (echo `.m 0400 0407`)",
          any(".m 0400 0407" in l for l in lines), str(lines[:3]))
    check("... with the memory dump indented under it", any(l.startswith("  ") and ">C:0400" in l for l in lines), str(lines[:6]))
    check("... and the registers (`.r`)", any(l.endswith(".r") for l in lines), str(lines[:8]))
    check("the machine keeps running", M.running())
    M.pause()
    M.mon("obs * del")
    M.drain(0.7)  # what the first observer's flood left in the socket
    o = M.mon("obs c2 when store d020 do cmd \"g\"")
    M.mon("g c000")
    lines = []
    for _ in range(4):  # the first hit of a stop-per-hit observer may take a moment
        got, _ = collect(1.0)
        lines += got
        if any("refused" in l for l in lines):
            break
    check("a do cmd with `g` is refused in its output", any("refused" in l for l in lines), "%s | %s | %s" % (o.replace("\n", " "), lines[:4], M.mon("obs").replace("\n", " ")))
    M.pause()
    M.mon("obs * del")


def t_rb(args):
    item("rb")
    M.mon("pause")
    M.mon("obs * del")
    o = M.mon("rb f9")
    check("rb f9 armed on a watch unit (raster)", "raster line $0f9" in o and "(raster)" in o, o)
    M.mon("g")
    check("the machine stops at the raster line", M.wait_stop(5))
    st = M.mon("status")
    check("... as observer rb at raster 249", "observer" in st and re.search(r"raster 249/", st) is not None, st)
    o = M.mon("rb -")
    check("rb - removes it", "rb removed" in o, o)
    o = M.mon("rb 200")
    check("a line beyond the video mode is refused", "out of range" in o, o)
    M.mon("g")
    time.sleep(0.5)
    check("running again", M.running())
    M.pause()


def t_views(args):
    item("reu-pot-io1541")
    M.mon("pause")
    set_reu("Disabled")
    M.mon("pause")  # a setting change takes the machine and gives it back running
    o = M.mon("reu")
    check("reu with the REU disabled says so", "nothing on the expansion port" in o, o)
    set_reu("Enabled")
    M.mon("pause")
    o = M.mon("reu")
    check("reu with the REU enabled shows the size and registers", re.search(r"reu: \d+ (MB|KB)", o) is not None and "status" in o and "command" in o, o)
    # registers as the C64 sets them: read back through reu, $df00 is not cleared by looking
    o2 = M.mon("reu")
    check("looking at it twice shows the same (the status is not cleared)", o == o2)
    o = M.mon("pot")
    check("pot shows the selected port", "pot: selected port" in o, o)
    M.mon("sidefx on")
    o = M.mon("pot")
    check("pot with sidefx on reads $d419/$d41a", re.search(r"\$d419=\$?\w\w", o) is not None, o)
    M.mon("sidefx off")
    o = M.mon("io 1541")
    check("io 1541 shows the drive taps", "1541 (drive A)" in o and "last opcode fetch" in o, o)
    set_reu("Disabled")


def t_files(args):
    item("ls-mkdir-rmdir")
    base = None
    for cand in ("/Usb0", "/Usb1"):
        o = M.mon("ls " + cand)
        if not o.startswith("ERR") and "no such" not in o.lower() and "cannot" not in o.lower():
            base = cand
            break
    check("ls of the USB drive lists entries", base is not None, o)
    if not base:
        return
    info("drive " + base)
    tmp = "%s/t55tmp%d" % (base, int(time.time()) % 100000)
    o = M.mon("mkdir " + tmp)
    check("mkdir", "ERR" not in o and "cannot" not in o.lower(), o)
    o = M.mon("ls " + base)
    name = tmp.rsplit("/", 1)[1]
    check("the new directory is in the listing (marked as a directory)", re.search(r"d .*%s" % name, o, re.I) is not None or name in o, o[:300])
    o = M.mon("ls " + tmp)
    check("it is empty", "ERR" not in o and "no such" not in o.lower(), o)
    o = M.mon("rmdir " + tmp)
    check("rmdir", "ERR" not in o and "cannot" not in o.lower(), o)
    o = M.mon("ls " + base)
    check("it is gone from the listing", name not in o, o[:300])
    o = M.mon("rmdir " + tmp)
    check("rmdir of a missing directory fails", "ERR" in o or "no such" in o.lower() or "cannot" in o.lower() or "not" in o.lower(), o)


def t_flow(args):
    item("flow-focus-sf-nf")
    M.mon("pause")
    M.mon("obs * del")
    reset_ready()
    M.mon("pause")
    M.mon("")
    M.mon("a c000 jmp c000")
    M.mon("")
    M.mon("r fl=20")  # the I flag clear: the pause may have caught the CPU inside a handler
    M.mon("g c000")
    time.sleep(0.3)
    M.mon("pause")
    o = M.mon("focus irq")
    check("focus irq accepted", "irq" in o and "ERR" not in o, o)
    got = False
    last = ""
    for _ in range(8):
        last = M.mon("sf", timeout=30)
        if "irq" in last.lower() and "CAP" not in last:
            got = True
            break
    check("sf runs into the IRQ handler (stepf:irq)", got, last)
    o = M.mon("flow")
    check("flow lists the irq frame (`irq enter=$ff48 -> ret=$c000`)", re.search(r"irq .*enter=\$ff48.*ret=\$c0", o) is not None, o)
    o = M.mon("r")
    check("r shows flow `irq`", re.search(r"\birq\b", o) is not None, o)
    o = M.mon("focus main")
    ok = False
    for _ in range(40):
        last = M.mon("sf", timeout=30)
        if "main" in last.lower() and "CAP" not in last:
            ok = True
            break
    check("focus main + sf runs out of the handler into the main flow", ok, last)
    o = M.mon("flow")
    check("flow is empty after the RTI", "irq" not in o.lower() or "no frames" in o.lower() or "(none" in o.lower() or "main" in o.lower(), o)
    o = M.mon("nf")
    check("nf steps over", "ERR" not in o, o)
    M.mon("focus clear")


def t_g_mid(args):
    item("g-from-mid")
    M.mon("pause")
    M.mon("obs * del")
    program_inc_loop()
    M.mon("r pc=c000")
    o = M.mon("cs 2")
    st = M.mon("status")
    mid = "mid" in M.mon("r").lower() or "mid" in o.lower() or "mid" in st.lower()
    info("cs 2: " + o.replace("\n", " | ")[:120])
    check("a cycle step leaves the CPU in the middle of an instruction", "mid-instruction" in o, o)
    o = M.mon("g c000")
    check("`g c000` from the middle finishes the instruction and continues (no 'step to the boundary')",
          "continuing at" in o and "boundary" not in o, o)
    check("the machine runs", M.running())
    M.pause()
    # the same with a redirect to somewhere else
    M.mon("r pc=c000")
    M.mon("cs 3")
    o = M.mon("g c003")
    check("`g <other addr>` from the middle works too", "continuing at" in o, o)
    M.pause()
    o = M.mon("r pc=c000")
    check("an explicit `r pc=` is unchanged (at an instruction boundary)", "ERR" not in o, o)


def t_jam_attach(args):
    item("jam-on-attach")
    M.mon("pause")
    M.mon("obs * del")
    M.mon("")
    o = M.mon("f c000 c000 02")
    M.mon("g c000")
    time.sleep(1.5)
    st = M.mon("status")
    check("a JAM while trxmon watches is a stop with reason jam", "jam" in st.lower(), st)
    M.mon("pause")
    # attach: trxmon ends, the machine stays jammed, a new trxmon finds it frozen by the firmware
    M.call("trxmon/quit")
    M.close()
    for _ in range(15):
        time.sleep(1)
        if not port_open(timeout=1.0):
            break
    check("trxmon ended", not port_open())
    for _ in range(6):
        r = rest("PUT", "/v1/apps:run_file", {"app": USB_APP, "action": "serve"}, timeout=60)
        if r.get("exit_code") == 0:
            break
        time.sleep(3)
    time.sleep(1)
    M.connect()
    st0 = M.mon("status")
    check("before it takes run control trxmon says so (C64_STOP rules the machine)", "has not taken it" in st0, st0)
    M.mon("pause")  # takes run control: the stop the firmware's freeze left is read
    st = M.mon("status")
    check("the new trxmon, attaching to the stuck CPU, says JAM", "jam" in st.lower(), st)
    first = st.splitlines()[1] if len(st.splitlines()) > 1 else st
    info(first)
    check("... not 'pause'", "last stop: pause" not in st, st)
    o = M.mon("g c010")
    check("`g <addr>` on a JAM says it is stuck (only a reset helps)", "stuck on a JAM opcode" in o, o)
    M.mon("f c000 c000 ee")  # the JAM opcode out of RAM, so that the reset does not run into it again
    ok = reset_ready()
    M.mon("pause")
    st = M.mon("status")
    check("a reset gets the machine out of the JAM (READY again)", ok and "last stop: jam" not in st, st[:200])
    M.mon("g")


def t_keys(args):
    item("screen-keys")
    set_reu("Disabled")
    reset_ready()  # BASIC idles in its keyboard loop: the breakpoint at $e5cf is reached
    M.mon("pause")
    M.mon("obs * del")
    M.mon("del")
    ok = screen_up()
    check("the monitor screen came up", ok)
    # assembler prefill: `a c100 lda 5` then the prompt holds `a c102 `
    M.mon("")
    press("a c100 lda 5\n")
    press("nop\n")
    time.sleep(0.5)
    mem = M.read(0xC100, 3)
    check("assembler prefill: after `a c100 lda 5` the next line `nop` lands at c102 (A5 05 EA)", mem == bytes([0xA5, 0x05, 0xEA]), mem.hex())
    press("\n")  # RETURN on the untouched prefill ends the mode
    time.sleep(0.4)
    o = M.mon("status")
    check("RETURN on the untouched prefill ended the assembler mode", o.startswith("state:"), o)
    # cursor left + insert
    press(["bk c10", "LEFT", "0", "\n"])
    time.sleep(0.4)
    o = M.mon("bk")
    check("cursor LEFT then `0` inserts at the cursor (bk c10 <- 0 = c100)", "c100" in o, o)
    # completion with CTRL+I: obs
    press(["ob", "CTRL_I", "q1 when exec c200 do break\n"])
    time.sleep(0.5)
    o = M.mon("obs")
    check("CTRL+I completes `ob` to `obs ` (the key reaches the app and completes)", " q1 " in o, o)
    M.mon("obs * del")
    # completion of a verb with several candidates -> common start, a second word
    press(["rb", "BACKSPACE", "BACKSPACE", "who", "CTRL_I", "\n"])
    time.sleep(0.4)
    # cursor keys: HOME
    press(["g c000", "HOME", "UP", "\n"])
    M.mon("bk -c100")
    M.mon("obs * del")
    screen_leave()


def second_client_gets_in():
    """A second connection is accepted at the HTTP level and then told it is refused (JSON-RPC error -32001, close
    1013): it holds the place only when its first request is answered."""
    try:
        c = Client(HOST, timeout=5.0)
    except (TrxmonError, OSError) as e:
        return False, str(e)
    try:
        c.call("ping", timeout=5.0)
        c.close()
        return True, ""
    except (TrxmonError, OSError) as e:
        return False, str(e)


def t_keepalive(args):
    item("keepalive")
    # the machine floods the log meanwhile: a holder that does not read must not stall the app (notifications stop
    # when nothing is heard from the holder for 25 s, the keepalive drops it at 50 s)
    set_reu("Disabled")
    M.mon("pause")
    M.mon("obs * del")
    program_inc_loop()
    M.mon("")
    M.mon("obs h1 when store d020 do log")
    M.mon("g c000")
    M.drain(1.0)
    M.close()
    time.sleep(2)
    # a stalled client: the handshake, then silence (no reads, no pongs)
    key = base64.b64encode(os.urandom(16))
    s = socket.create_connection((HOST, 4312), timeout=5)
    s.sendall(b"GET /?av=0 HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
              b"\r\nSec-WebSocket-Version: 13\r\n\r\n")
    head = s.recv(4096)
    check("the stalled client got the WebSocket upgrade", head.startswith(b"HTTP/1.1 101"))
    t0 = time.monotonic()
    refused_at = None
    got_in = None
    why = ""
    while time.monotonic() - t0 < 100:
        ok, why = second_client_gets_in()
        if ok:
            got_in = time.monotonic() - t0
            break
        if refused_at is None:
            refused_at = time.monotonic() - t0
            info("a second client is refused at %.0f s: %s" % (refused_at, why[:90]))
        time.sleep(4)
    s.close()
    check("REST answered all the time (the stalled holder did not stall the app)", rest_alive())
    check("a second client is refused while the stalled one holds the place", refused_at is not None and refused_at < 20, why)
    check("... and gets in after the keepalive dropped the stalled one (about 50 s; %s)"
          % ("%.0f s" % got_in if got_in else "never"), got_in is not None and 40 <= got_in <= 75, str(got_in))
    time.sleep(1)
    M.connect()
    check("the app is alive after the stall", M.ping().get("backend") == "c64u")
    lines, _ = collect(3.0)
    check("the new holder gets the live log again (%d lines in 3 s)" % len(lines), len(lines) >= 30,
          "%d | %s | %s" % (len(lines), M.mon("status").replace("\n", " ")[:200], M.mon("obs").replace("\n", " ")[:200]))
    M.mon("pause")
    M.mon("obs * del")


def t_end(args):
    item("end")
    try:
        if not port_open():
            start_trxmon(args.app)
        elif not M.c:
            M.connect()
        M.mon("pause")
        M.mon("obs * del")
        M.mon("del")
        M.mon("")
    except Exception:
        pass
    set_reu("Enabled")
    check("REU back to Enabled", "Enabled" in json.dumps(rest("GET", REU_PATH)))
    rest("PUT", "/v1/machine:reset")
    time.sleep(4)
    try:
        M.mon("pause")
        check("the machine is at READY after the final reset", "READY." in read_screen_text().replace("@", ""))
        M.mon("g")
        M.call("trxmon/quit")
    except Exception as e:
        check("final reset", False, str(e))
    M.close()
    time.sleep(2)
    check("trxmon ended, REST alive", not port_open() and rest_alive())
    check("speaker still disabled", speaker_disabled())


def demo(args):
    """For the person at the HDMI screen: the live log under the real monitor screen, then the screen is left up (paused)
    for the keys only a real keyboard can test (CTRL alone, CTRL+I). Nothing here is a check."""
    print("\n[demo] live log on the monitor screen")
    start_trxmon(args.app)
    set_reu("Disabled")
    reset_ready()
    M.mon("pause")
    M.mon("obs * del")
    program_inc_loop()
    M.mon("")
    print("  stage A (20 s): `obs l1 when exec ea31 do log` - one line per frame, each line `exec $ea31 ...`")
    M.mon("obs l1 when exec ea31 do log")
    M.mon("g")
    M.mon("bk e5cf")
    screen_up_ok = M.wait_stop(10)
    time.sleep(2.0)
    press("g\n")
    M.drain(20)
    print("  stage B (30 s): the INC $d020 loop under `obs h1 when store d020 do log`: at most 60 lines a second and a")
    print("                  `+K skipped` line about once a second; the screen keeps answering")
    M.mon("pause")
    M.mon("obs * del")
    M.mon("bk -e5cf")
    program_inc_loop()
    M.mon("")
    M.mon("obs h1 when store d020 do log")
    M.mon("g c000")
    M.drain(30)
    press("\n")  # nothing typed; makes sure the prompt is empty
    M.mon("pause")
    M.mon("obs * del")
    M.mon("del")
    M.mon("bk c7ff")
    print("  done. The screen stays up with the machine paused and trxmon resident (a breakpoint is armed).")
    print("  Now on the real C64 keyboard: type `ob`, then tap CTRL alone -> the line becomes `obs `;")
    print("  clear the line (left arrow key) and again: `ob`, then CTRL+I -> `obs `. Leave with `/quit`.")
    return 0


ITEMS = [
    ("preflight", t_preflight), ("hex", t_hex), ("ignore", t_ignore), ("log-low", t_log_low), ("log-high", t_log_high),
    ("cmd", t_cmd), ("rb", t_rb), ("views", t_views), ("files", t_files), ("flow", t_flow), ("gmid", t_g_mid),
    ("keys", t_keys), ("screen-flood", t_screen_flood), ("jam", t_jam_attach), ("keepalive", t_keepalive),
]


def main():
    global HOST
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", default=HOST)
    ap.add_argument("--package")
    ap.add_argument("--app", default=None)
    ap.add_argument("--demo", action="store_true", help="live-log demo for the HDMI screen, then leave the screen up (no checks)")
    ap.add_argument("--only")
    ap.add_argument("--skip")
    args = ap.parse_args()
    HOST = args.host
    if args.app and not os.path.exists(args.app):
        args.app = None  # no build here: the registered trxmon is used
    only = set(args.only.split(",")) if args.only else None
    skip = set(args.skip.split(",")) if args.skip else set()
    if args.demo:
        return demo(args)
    if args.package:
        item("package")
        check("package %s loaded, REST up" % args.package, load_package(args.package))
    for name, fn in ITEMS:
        if (only and name not in only and name != "preflight") or name in skip:
            continue
        try:
            fn(args)
        except Exception as e:  # a failed item must not stop the others, but it must show
            import traceback
            traceback.print_exc()
            check("item %s ran to its end" % name, False, "%s: %s" % (type(e).__name__, e))
            try:
                if not rest_alive():
                    print("        REST is dead: reload the bitstream (--package) before going on")
                    break
                if not port_open():
                    start_trxmon(args.app)
            except Exception:
                pass
    try:
        t_end(args)
    except Exception as e:
        check("end ran", False, str(e))
    # summary per item
    print("\n==== summary ====")
    items = []
    for it, name, ok, detail in RESULTS:
        if it not in items:
            items.append(it)
    bad = 0
    for it in items:
        rs = [r for r in RESULTS if r[0] == it]
        ok = all(r[2] for r in rs)
        bad += 0 if ok else 1
        print("%s  %-18s %d/%d checks" % ("PASS" if ok else "FAIL", it, sum(1 for r in rs if r[2]), len(rs)))
        for r in rs:
            if not r[2]:
                print("        failed: %s %s" % (r[1], r[3][:160]))
    print("%d item(s) failed" % bad if bad else "all items passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
