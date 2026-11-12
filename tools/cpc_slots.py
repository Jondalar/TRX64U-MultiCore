#!/usr/bin/env python3
"""T92 S6: the CPC's memory-slot timing as a model of the Gate Array wait (measured on the core, cross-checked with the published whole-microsecond
instruction times), and the check of a bench log against it.
The Gate Array stretches the Z80 clock so that an opcode fetch (M1) ENDS on a multiple of 4 T-states (one microsecond slot) and a memory read or write
STARTS on one; I/O and internal cycles run on unchanged. So an instruction takes whole microseconds: NOP 1, LD A,(HL) 2, INC HL 2, PUSH BC 4, JR 3 ...
The model lists the cycles of each instruction of the bench program (a straight line of instructions, twice, in RAM at $8100) and gives the T-state of
every opcode fetch (the prefix bytes of ED/CB instructions are opcode fetches of their own). The bench logs "M1 <pc> <Gate Array clocks since the start>";
four Gate Array clocks are one T-state.

  tools/cpc_slots.py program   the assembler text of the program
  tools/cpc_slots.py check     read the bench log on stdin, print PASS or FAIL
  tools/cpc_slots.py selftest  known instruction times (the CPC's NOP counts) against the model, and its own negative control
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zx_asm  # noqa: E402

# (assembler text, [(kind, T-states)...]) - M1 opcode fetch, RD / WR memory, IO port, I internal (not aligned)
M1, RD, WR, IO, I = "M1", "RD", "WR", "IO", "I"
INSTRS = [
    ("nop", [(M1, 4)]),
    ("ld a, 0x12", [(M1, 4), (RD, 3)]),
    ("ld b, c", [(M1, 4)]),
    ("ld hl, 0x9100", [(M1, 4), (RD, 3), (RD, 3)]),
    ("ld a, (hl)", [(M1, 4), (RD, 3)]),
    ("ld (hl), a", [(M1, 4), (WR, 3)]),
    ("ld a, (0x9100)", [(M1, 4), (RD, 3), (RD, 3), (RD, 3)]),
    ("ld (0x9101), a", [(M1, 4), (RD, 3), (RD, 3), (WR, 3)]),
    ("ld (0x9102), hl", [(M1, 4), (RD, 3), (RD, 3), (WR, 3), (WR, 3)]),
    ("ld hl, (0x9102)", [(M1, 4), (RD, 3), (RD, 3), (RD, 3), (RD, 3)]),
    ("inc hl", [(M1, 4), (I, 2)]),
    ("inc (hl)", [(M1, 4), (RD, 4), (WR, 3)]),
    ("dec b", [(M1, 4)]),
    ("add hl, bc", [(M1, 4), (I, 7)]),
    ("push bc", [(M1, 4), (I, 1), (WR, 3), (WR, 3)]),
    ("pop bc", [(M1, 4), (RD, 3), (RD, 3)]),
    ("jr $+2", [(M1, 4), (RD, 3), (I, 5)]),
    ("jp $+3", [(M1, 4), (RD, 3), (RD, 3)]),
    ("ex de, hl", [(M1, 4)]),
    ("xor a", [(M1, 4)]),
    ("cp 0x20", [(M1, 4), (RD, 3)]),
    ("rlca", [(M1, 4)]),
    ("ld a, 0xFF", [(M1, 4), (RD, 3)]),            # the high byte of the port address: no device is selected
    ("out (0xFE), a", [(M1, 4), (RD, 3), (IO, 4)]),
    ("in a, (0xFE)", [(M1, 4), (RD, 3), (IO, 4)]),
    ("ld bc, 0xFEFF", [(M1, 4), (RD, 3), (RD, 3)]),                    # a port no device answers
    ("out (c), c", [(M1, 4), (M1, 4), (IO, 4)]),
    ("in a, (c)", [(M1, 4), (M1, 4), (IO, 4)]),
    ("ldi", [(M1, 4), (M1, 4), (RD, 3), (WR, 5)]),
    ("set 0, a", [(M1, 4), (M1, 4)]),
    ("bit 0, (hl)", [(M1, 4), (M1, 4), (RD, 4)]),
    ("neg", [(M1, 4), (M1, 4)]),
    ("ld a, i", [(M1, 4), (M1, 4), (I, 1)]),
    ("nop", [(M1, 4)]),
]


def program_text():
    t = "        org 0\n        di\n        jp start\n        org 0x66\n        retn\nstart:  ld sp, 0xBFF0\n        ld hl, rout\n        ld de, 0x8100\n        ld bc, rend-rout\n        ldir\n        ld bc, 0x7F8C\n        ld a, 0x8C\n        out (c), a\n        jp 0x8100\nrout:\n"
    t += "        .rout\n"
    return t


def body():
    """the straight-line body (each instruction twice), a label before each"""
    lines = []
    for rep in range(2):
        for k, (txt, _) in enumerate(INSTRS):
            lines.append("i%d_%d:  %s" % (rep, k, txt))
    return lines


def full_program():
    """the text the benches assemble: a boot that copies the body to $8100, switches the ROMs off and runs it"""
    # the body is assembled at $8100: first assemble it alone to know its size, then place it after the boot as data
    body_lines = body()
    txt = "        org 0x8100\n" + "\n".join(body_lines) + "\n        ld a, 0xA5\n        ld (0x9FFF), a\nspin:   jr spin\n"
    a = zx_asm.Asm()
    img = a.run(txt)
    code = img[0x8100:]
    boot = ("        org 0\n        di\n        jp start\n        org 0x66\n        retn\nstart:  ld sp, 0xBFF0\n        ld hl, rout\n        ld de, 0x8100\n"
            "        ld bc, %d\n        ldir\n        ld bc, 0x7F8C\n        ld a, 0x8C\n        out (c), a\n        jp 0x8100\nrout:  db %s\n"
            % (len(code), ", ".join(str(b) for b in code)))
    pcs = [a.sym["i%d_%d" % (rep, k)] for rep in range(2) for k in range(len(INSTRS))]
    return boot, pcs


def adv(t, kind, T):
    """measured rule of the Gate Array wait (grid = multiples of 4 T-states): an opcode fetch (M1) is stretched until it ENDS on the grid; a memory read
    or write is delayed until it STARTS on the grid; an I/O cycle (4 T-states at least) ends 3 T-states after a grid point; internal cycles run on"""
    if kind == RD or kind == WR:
        t = (t + 3) // 4 * 4
    t += T
    if kind == IO:
        return t // 4 * 4 + 3
    return (t + 3) // 4 * 4 if kind == M1 else t


def model_times():
    """the T-state (from the first opcode fetch) of every opcode fetch, in program order, and the pcs it belongs to (prefix bytes: pc + 1)"""
    boot, pcs = full_program()
    t = 0
    ev = []
    first = True
    b0 = 0
    for rep in range(2):
        for k, (txt, cycles) in enumerate(INSTRS):
            pc = pcs[rep * len(INSTRS) + k]
            n_m1 = 0
            for kind, T in cycles:
                if kind == M1:
                    if first:
                        b0 = t
                        first = False
                    ev.append((pc + n_m1, t - b0))
                    n_m1 += 1
                t = adv(t, kind, T)
    return ev


def check(lines):
    ev = model_times()
    got = []
    for ln in lines:
        f = ln.split()
        if len(f) >= 3 and f[0] == "M1":
            got.append((int(f[1], 16), int(f[2])))
    # only the events from the program body on ($8100 up), the first of which is time zero
    got = [(pc, t) for pc, t in got if 0x8100 <= pc < 0x8400]
    if not got:
        print("FAIL slots: no opcode fetches logged in the program")
        return 1
    t0 = got[0][1]
    got = [(pc, (t - t0) / 4.0) for pc, t in got]
    n = min(len(ev), len(got))
    bad = 0
    for k in range(n):
        if got[k][0] != ev[k][0] or abs(got[k][1] - ev[k][1]) > 0.5:
            if bad < 8:
                print("  slots: fetch %d at pc %04x T %.2f, model pc %04x T %d" % (k, got[k][0], got[k][1], ev[k][0], ev[k][1]))
            bad += 1
    if len(got) < len(ev):
        print("  slots: only %d of %d opcode fetches logged" % (len(got), len(ev)))
        bad += 1
    if bad == 0:
        print("PASS slots: %d opcode fetches of %d instructions (twice) fall on the T-states the 1 us slot rule gives (all whole microseconds)" % (len(ev), len(INSTRS)))
        return 0
    print("FAIL slots: %d of %d opcode fetches differ from the model" % (bad, len(ev)))
    return 1


def selftest():
    # the CPC's documented NOP counts (instruction time in microseconds) of a few instructions, from the model's rule
    def us(cycles):
        t = 0
        for kind, T in cycles:
            t = adv(t, kind, T)
        return (t + 3) // 4
    table = {"nop": 1, "ld a, 0x12": 2, "ld a, (hl)": 2, "inc hl": 2, "push bc": 4, "pop bc": 3, "jr $+2": 3, "ld hl, 0x9100": 3, "add hl, bc": 3,
             "out (0xFE), a": 3, "out (c), c": 4, "in a, (c)": 4, "ldi": 5, "ld a, (0x9100)": 4, "ld (0x9102), hl": 5}
    byname = dict(INSTRS)
    for k, v in table.items():
        assert us(byname[k]) == v, (k, us(byname[k]), v)
    ev = model_times()
    assert ev[0] == (0x8100, 0)
    # negative control: a log with the 7th fetch one T-state late must fail
    lines = ["M1 %04x %d" % (pc, 4 * t) for pc, t in ev]
    assert check(lines) == 0
    lines2 = list(lines)
    pc, t = ev[6]
    lines2[6] = "M1 %04x %d" % (pc, 4 * t + 4)
    assert check(lines2) == 1
    print("selftest: ok (the NOP counts of the CPC, a late fetch is found)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["program"]:
        print(full_program()[0])
    elif sys.argv[1:] == ["check"]:
        sys.exit(check(sys.stdin.read().split("\n")))
    elif sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    else:
        print(__doc__)
