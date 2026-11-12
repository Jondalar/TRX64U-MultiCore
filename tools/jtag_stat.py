#!/usr/bin/env python3
"""Read the Artix-7 configuration status over the USB-Blaster/JTAG without
touching the design: IDCODE, IR capture (INIT/DONE), STAT, BOOTSTS, COR0,
WBSTAR (CFG_IN/CFG_OUT, UG470) and the XADC (temperature, rails) over the
JTAG DRP port. Run it right after the board looks dead, before any reload:
it says unconfigured / reconfigured / hung.   tools/jtag_stat.py [--json]"""
import json, sys, os
sys.path.insert(0, os.environ.get("U64II_TOOLING", os.path.expanduser("~/Development/1541ultimate-tools/tooling")))
import u64ii_jtag as jt
from u64ii_bscan import Blaster

def flip32(w):
    return int(f"{w:032b}"[::-1], 2)

def read_regs(tap, regs):
    out = {}
    for name, hdr in regs:
        words = [0xFFFFFFFF, 0xAA995566, 0x20000000, hdr, 0x20000000, 0x20000000]
        tap.ir(jt.IR_CFG_IN)
        v = 0
        for i, w in enumerate(words):
            v |= flip32(w) << (32 * i)
        tap.dr(v, 32 * len(words))
        tap.ir(0x04)  # CFG_OUT
        out[name] = flip32(tap.dr(0, 32, read=True))
        # DESYNC
        words = [0xAA995566, 0x20000000, 0x30008001, 0x0000000D, 0x20000000, 0x20000000]
        tap.ir(jt.IR_CFG_IN)
        v = 0
        for i, w in enumerate(words):
            v |= flip32(w) << (32 * i)
        tap.dr(v, 32 * len(words))
    tap.ir(jt.IR_BYPASS)
    return out

def xadc(tap, addr):
    tap.ir(0x37)
    tap.dr((1 << 26) | (addr << 16), 32)
    r = tap.dr(0, 32, read=True)
    return r & 0xFFFF

STAT_BITS = ["CRC_ERROR","PART_SECURED","MMCM_LOCK","DCI_MATCH","EOS","GTS_CFG_B","GWE","GHIGH_B",
             "M0","M1","M2","INIT_COMPLETE","INIT_B","RELEASE_DONE","DONE","ID_ERROR","DEC_ERROR"]
BOOT_BITS = ["VALID_0","FALLBACK_0","IPROG_0","WTO_0","ID_ERROR_0","CRC_ERROR_0","WRAP_ERROR_0","HMAC_ERROR_0"]

def snapshot(tap, full=True):
    """Dict of the chip's state. `tap` is a plain jt.Tap (not the USER1-only DebugTap)."""
    res = {}
    if full:
        res["idcode"] = hex(tap.idcode())
        cap = tap.ir_capture()
        res["ir_capture"] = hex(cap)
        res["ir_INIT"] = bool(cap & jt.IR_CAPTURE_INIT)
        res["ir_DONE"] = bool(cap & jt.IR_CAPTURE_DONE)
        r = read_regs(tap, [("STAT", 0x2800E001), ("BOOTSTS", 0x2802C001),
                            ("COR0", 0x28012001), ("WBSTAR", 0x28020001)])
        res["STAT"] = hex(r["STAT"])
        res["STAT_set"] = [n for i, n in enumerate(STAT_BITS) if r["STAT"] >> i & 1]
        res["BOOTSTS"] = hex(r["BOOTSTS"])
        res["BOOTSTS_set"] = [n for i, n in enumerate(BOOT_BITS) if r["BOOTSTS"] >> i & 1]
        res["COR0"] = hex(r["COR0"]); res["WBSTAR"] = hex(r["WBSTAR"])
    try:
        xadc(tap, 0)  # prime
        res["xadc_temp_C"] = round((xadc(tap, 0x00) >> 4) * 503.975 / 4096 - 273.15, 1)
        for n, a in (("vccint", 1), ("vccaux", 2), ("vccbram", 6)):
            res["xadc_" + n + "_V"] = round((xadc(tap, a) >> 4) * 3.0 / 4096, 3)
        res["xadc_status"] = hex(xadc(tap, 0x3F))
    except Exception as e:
        res["xadc"] = f"err {e}"
    tap.ir(jt.IR_BYPASS)
    return res

def main():
    with jt.DeviceLock():
        cable = Blaster.open()
        try:
            tap = jt.Tap(cable)
            tap.reset()
            res = snapshot(tap)
            tap.reset()
        finally:
            cable.release()
    print(json.dumps(res, indent=None if "--json" in sys.argv else 1))

if __name__ == "__main__":
    main()
