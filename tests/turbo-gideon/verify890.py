"""Helpers for the Spec 890 verification (round 3): x at a $D031 value, PHI2 per access."""
import os, sys, json, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tm, tm2
def x(d031=None, d011=0x0B, k=16, tenths=10):
    r = tm.measure(d031, d011, tenths, k)
    return round(r["outer"]*(k*1286+32)/r["cia2_ticks"], 3), r["d031_before"], r["d030"]
def cost(op, addr, d031, val=0, filler=0, per_block=64, rep=4, d011=0x0B):
    code, n = tm2.io_loop(op, addr, val, per_block, filler, rep)
    return tm2.timed(code, d031=d031, d011=d011) / n
def cfg(i, v): return tm.cfg(i, v)
