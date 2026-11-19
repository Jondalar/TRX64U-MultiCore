import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from emu816.cpu import CPU
from emu816.memory import Memory, IORegion

CODE = 0x1000


def native(code, m=1, x=1, a=0, xr=0, yr=0, **kw):
    """CPU in native mode with the given widths; code loaded at $00:1000, PC there."""
    cpu = CPU(Memory(), trace=32)
    cpu.E = 0
    cpu.S = 0x01FF
    cpu.fM, cpu.fX = m, x
    cpu.C, cpu.X, cpu.Y = a, xr, yr
    cpu.PB, cpu.PC = 0, CODE
    cpu.set_state(**kw)
    cpu.mem.load(CODE, bytes(code))
    return cpu


def emu(code, a=0, xr=0, yr=0, **kw):
    cpu = CPU(Memory(), trace=32)
    cpu.reset_6502()
    cpu.C, cpu.X, cpu.Y = a, xr, yr
    cpu.PC = CODE
    cpu.set_state(**kw)
    cpu.mem.load(CODE, bytes(code))
    return cpu


def nmos(code, a=0, xr=0, yr=0, **kw):
    cpu = CPU(Memory(), mode6502=True, trace=32)
    cpu.reset_6502()
    cpu.C, cpu.X, cpu.Y = a, xr, yr
    cpu.PC = CODE
    cpu.set_state(**kw)
    cpu.mem.load(CODE, bytes(code))
    return cpu


def steps(cpu, n=1):
    for _ in range(n):
        cpu.step()
    return cpu


def flags(cpu):
    return dict(N=cpu.fN, V=cpu.fV, Z=cpu.fZ, C=cpu.fC)
