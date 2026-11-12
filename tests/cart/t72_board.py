"""T72 board check (ADR-0053): REU, Ultimate Audio and UCI are off while a cartridge runs.

    t72_board.py [--selftest]

Real EasyFlash (Timo's Castle) in the port, running at its menu; Alex's saved settings: RAM Expansion Unit,
Map Ultimate Audio and Command Interface all Enabled; Cartridge Preference Auto/External. trxmon must be
serving (PUT /v1/apps/trxmon:run?action=serve). REST readmem and lens=cart are NOT used for $DF00-$DFFF: they
return plain C64 RAM there. The CPU itself is the probe (as tests/cart/ef_ingame_debug.py io2): the machine is
reset, paused at the menu, a routine at $C800 writes a pattern to the cart RAM $DF80-$DFFB, reads it back,
copies $DF00-$DFFF to $CA00 and ends with BRK; the result is read from C64 RAM. The cartridge flash is never
written ($DE02 only). The game state is lost: the script resets the machine at the end.
Phase 1 (cart in the port): saved settings still read Enabled; SLOT_STATUS b16 armed (JTAG, optional);
  the CPU reads back the pattern at $DF80 (0 of 124 wrong); $DF1D is not $C9 (no UCI).
Then power off, pull the cart, power on (Enter). Phase 2 (no cart; the probe needs RAM-only run, nothing to
write to): saved settings Enabled; $DF1D == $C9 (UCI answers); $DF00 bit 4 set (REU status).
Exit code 1 on any FAILED line. Nothing is written to the flash or to the settings.
TRX64_HOST as in carts_board.py.
"""
import os
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import carts_board as cb  # noqa: E402

CAT = "C64 and Cartridge Settings"
ITEMS = ["RAM Expansion Unit", "Map Ultimate Audio $DF20-DFFF", "Command Interface"]
N = 124
PAT = bytes((i * 7 + 0x13) & 0xFF for i in range(N))
TEST = """asm c800 <<T
sei
lda #$87
sta $de02
ldx #0
l1: lda $c900,x
sta $df80,x
inx
cpx #124
bne l1
ldx #0
l2: lda $df80,x
sta $c980,x
inx
cpx #124
bne l2
ldx #0
l3: lda $df00,x
sta $ca00,x
inx
bne l3
brk
T"""

fails = 0


def rec(what, ok, detail=""):
    global fails
    if not ok:
        fails += 1
    print("  %-52s %s%s" % (what, "ok" if ok else "FAILED", "" if ok else "  " + detail))


def set_pc(m):
    o = m("r pc=c800")
    return "pc=$c800" in o.lower() and "mid-instruction" not in o.lower() and "err" not in o.lower()


def reach_boundary(m, entry, reset, sleep):
    """Put the CPU at $C800 (trxmon `r pc=` is refused unless the stop is at an instruction boundary AND the gate is
    shut with the pending update settled: mach_cpu_write, hw_cpu). 1) pause + `z`, up to 6 times, `r pc=` after each;
    with a game running from Ultimax ROMH that did not get there on the board (8 z in a row stayed 'mid-instruction').
    2) The cartridge's first instruction as a known boundary: a breakpoint on the reset-vector target, reset, wait for
    the breakpoint stop, set the PC from there."""
    for _ in range(6):
        if set_pc(m):
            return "pause"
        m("z")
    m("g")
    m("del")
    m("bk %04x" % entry)
    reset()
    for _ in range(20):
        sleep(0.5)
        if "stop breakpoint" in m("r").lower():
            break
    m("del")
    if set_pc(m):
        return "breakpoint"
    raise RuntimeError("no instruction boundary for r pc= (pause+z and breakpoint at $%04X both refused)" % entry)


def probe():
    """Run the routine on the CPU; returns (read-back of $DF80.., the 256 bytes $DF00-$DFFF)."""
    from trxmon_client import Client
    c = Client(cb.HOST, timeout=10)
    m = lambda x: c.mon(x, timeout=30)  # noqa: E731
    m("g")
    cb.http("PUT", "/v1/machine:reset")
    time.sleep(8)
    m("pause")
    m("obs * del")
    m("del")
    m(TEST)
    vec = c.read(0xFFFC, 2, "cpu")
    reach_boundary(m, vec[0] | (vec[1] << 8), lambda: cb.http("PUT", "/v1/machine:reset"), time.sleep)
    for o in range(0, N, 16):
        m("wr ram %04x " % (0xC900 + o) + " ".join("%02x" % b for b in PAT[o:o + 16]))
    m("bk c82b")
    m("g")
    time.sleep(1.0)
    back = c.read(0xC980, N, "ram")
    io = c.read(0xCA00, 256, "ram")
    m("g")
    cb.http("PUT", "/v1/machine:reset")
    c.close()
    return back, io


def setting(item):
    import json
    raw = cb.http("GET", "/v1/configs/%s/%s" % (urllib.parse.quote(CAT), urllib.parse.quote(item)))
    d = json.loads(raw)
    # {"C64 and Cartridge Settings": {"RAM Expansion Unit": {"current": "Enabled", ...}}} or a flat form
    while isinstance(d, dict):
        if "current" in d:
            return d["current"]
        if "value" in d:
            return d["value"]
        d = next(iter(d.values()))
    return d


def settings_enabled():
    for it in ITEMS:
        v = str(setting(it))
        rec("saved setting '%s' reads Enabled" % it, v.lower().startswith("enabled"), v)


def slot_status():
    try:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
        import trxdbg
        d = trxdbg.Dbg()
        try:
            return d.trx_read(0x7004)
        finally:
            d.close()
    except Exception as e:  # noqa: BLE001
        print("  (JTAG not available: %s)" % e)
        return None


def phase1():
    print("phase 1: cartridge in the port")
    settings_enabled()
    st = slot_status()
    if st is not None:
        rec("SLOT_STATUS 0x%08X b16 armed" % st, bool(st & (1 << 16)), "port not armed: is the preference Auto/External?")
    back, io = probe()
    bad = [k for k in range(N) if back[k] != PAT[k]]
    rec("cart RAM $DF80..: CPU reads back the pattern", not bad, "%d of %d wrong, first %s" % (len(bad), N, bad[:8]))
    rec("UCI off: $DF1D != $C9", io[0x1D] != 0xC9, "$%02X" % io[0x1D])


def phase2():
    print("phase 2: no cartridge")
    settings_enabled()
    _, io = probe()
    rec("UCI identifier $DF1D == $C9", io[0x1D] == 0xC9, "$%02X" % io[0x1D])
    rec("REU status $DF00 bit 4 set", bool(io[0x00] & 0x10), "$%02X" % io[0x00])


def selftest():
    """Offline: the checks against a stub probe."""
    global fails, probe, slot_status, setting
    probe_, slot_, set_ = probe, slot_status, setting
    slot_status, setting = (lambda: 0x00010000), (lambda it: "Enabled")
    good_io = bytes(256)
    probe = lambda: (PAT, good_io)  # noqa: E731
    fails = 0
    phase1()
    assert fails == 0
    probe = lambda: (PAT[:5] + b"\x00" + PAT[6:], good_io)  # noqa: E731
    phase1()
    assert fails == 1, fails          # one wrong byte
    fails = 0
    probe = lambda: (PAT, bytes(0x1D) + b"\xc9" + bytes(256 - 0x1E))  # noqa: E731
    phase1()
    assert fails == 1, fails          # UCI answers next to a cart
    fails = 0
    probe = lambda: (b"", bytes([0x10]) + bytes(0x1C) + b"\xc9" + bytes(256 - 0x1E))  # noqa: E731
    phase2()
    assert fails == 0, fails
    probe, slot_status, setting = probe_, slot_, set_
    # reach_boundary: boundary found after some z; or only through the breakpoint; or never
    log = []
    stuck = {"n": 0}

    def mk(refuse_until, bp_ok):
        def m(x):
            log.append(x)
            if x.startswith("r pc="):
                stuck["n"] += 1
                if stuck["n"] <= refuse_until or (refuse_until >= 99 and not bp_ok):
                    return "ERR r: mid-instruction: step to the boundary first (z)"
                return "set pc=$c800"
            if x == "r":
                return "ADDR\n stop breakpoint"
            return ""
        return m
    stuck["n"] = 0
    assert reach_boundary(mk(3, True), 0xE000, lambda: None, lambda t: None) == "pause"
    assert log.count("z") == 3 and "bk e000" not in log, log
    log.clear()
    stuck["n"] = 0
    assert reach_boundary(mk(6, True), 0xE000, lambda: log.append("reset"), lambda t: None) == "breakpoint"
    assert "bk e000" in log and log.index("reset") > log.index("bk e000"), log
    stuck["n"] = 0
    try:
        reach_boundary(mk(99, False), 0xE000, lambda: None, lambda t: None)
        raise SystemExit("selftest: reach_boundary must fail when no boundary is reached")
    except RuntimeError:
        pass
    print("selftest: ok")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return 0
    phase1()
    input("Power off, pull the cartridge, power on, wait for the C64 screen, then press Enter ")
    phase2()
    print("T72 board check: %s" % ("FAILED (%d)" % fails if fails else "all ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
