#!/usr/bin/env python3
"""T208: the synthetic SuperCPU ROM of the Metal Dust benches (cores/c64scpu/sim/run_mdust.sh, spec T208 section 2 S1-S3). The ROM is our own code (no byte of a game, a crack or a CMD ROM); the
access patterns are those of the static analysis in the spec (section 1), the code is written here.

  gen_c64scpu_mdust.py <out dir> menu20|menu1|scroll|hud|hudx|stz|smc|stars|starsq<N>      writes <out>/mem/scpu_rom.mem, <out>/mem/c64scpu_rom.mem (zeros), <out>/expect.json; prints the extra bench arguments

  menu20 / menu1  S3: the crack menu's shape - emulation mode, hwenable off, $01 = $35, a raster IRQ at line $F3 (vector in RAM at $FFFE), a handler that sets the lower text mode, waits
                  for the raster byte 0 with `lda #0 / cmp $D012 / bne`, sets the upper bitmap mode, scrolls the text row ($63C0), moves the sprite Y registers from $7700/$7800, plays a
                  25-register "music" into $D400, reads the keyboard and acknowledges $D019 at the very end; the main loop waits for a flag the handler sets. Eight expanded sprites. 30 frames at
                  20 MHz, then (menu1 only) $D07A (1 MHz) and 30 more. Markers (a write to $0B00-$0B03, the bench prints the PHI2 cycle and frame): 0 handler entry, 1 handler end (before RTI),
                  2 the speed switch, 3 end.
  scroll          S1: native mode, optimisation mode $D075 (mirror $40-$7F), a dense stream of mirrored writes (the game's scroll: 1,024 bytes from SuperRAM bank 2 to the screen at $7400, the sprite
                  pointers at $77F8, sprite registers between) and short $D077 windows (mirror all) that write VIC bank 3 ($FB90, $FC88, $FC18), at 20 MHz with eight double-height sprites and a
                  bitmap screen (badlines and sprite DMA everywhere). The values change every pass (an XOR with the pass counter), so a lost or late byte shows.
  hud             S2: `scroll` as the main loop plus the game's raster chain: IRQ at line $D1 (the HUD stage: $D011 = $6B, 22 I/O writes, $DD00, $D016/$D018, $D011 = $3B, the next stage at $FF)
                  through the native vector $FFEE and an indirect jump through a RAM word.
  smc             T208: the code cache's next-line prefetch against self-modifying code (smcbench): 60 straight code lines in SuperRAM bank 2 that store into the next line while it is fetched, 8 passes,
                  markers 1 (all results right) / 2 (a stale line was executed) / 3 (end).
  stars           T208: the crack menu's stars (starsbench below): a 3-line raster IRQ chain and a nested CIA music IRQ, the PHI2 cycle of the $D00C store and of the handler's end per line.
  stz             S2 bare: 24 `stz $D0xx` in a row at 20 MHz (a subroutine called three times: cold, then warm), no IRQ, no sprites, no badlines; the PHI2 gaps between the stores against the VICE accounting (check_mdust.py).
"""
import json
import os
import sys

ORG = 0x1000
BLOB = 0x8100         # the blob in the ROM, copied to $1000
PAGES = 16

OPS = {
    "lda": {"imm": 0xA9, "abs": 0xAD, "absx": 0xBD, "absy": 0xB9, "long": 0xAF, "longx": 0xBF},
    "sta": {"abs": 0x8D, "absx": 0x9D, "absy": 0x99, "long": 0x8F, "longx": 0x9F},
    "ldx": {"imm": 0xA2, "abs": 0xAE},
    "ldy": {"imm": 0xA0, "abs": 0xAC},
    "stx": {"abs": 0x8E}, "sty": {"abs": 0x8C}, "stz": {"abs": 0x9C},
    "cmp": {"imm": 0xC9, "abs": 0xCD}, "cpx": {"imm": 0xE0}, "cpy": {"imm": 0xC0},
    "inc": {"abs": 0xEE}, "dec": {"abs": 0xCE},
    "and": {"imm": 0x29, "abs": 0x2D}, "ora": {"imm": 0x09, "abs": 0x0D}, "eor": {"imm": 0x49, "abs": 0x4D},
    "adc": {"imm": 0x69, "abs": 0x6D},
    "jmp": {"abs": 0x4C, "ind": 0x6C}, "jsr": {"abs": 0x20},
}
IMPL = {"inx": 0xE8, "iny": 0xC8, "dex": 0xCA, "dey": 0x88, "pha": 0x48, "pla": 0x68, "phx": 0xDA, "plx": 0xFA, "phy": 0x5A, "ply": 0x7A, "tax": 0xAA, "txa": 0x8A,
        "tay": 0xA8, "tya": 0x98, "sei": 0x78, "cli": 0x58, "rti": 0x40, "rts": 0x60, "nop": 0xEA, "clc": 0x18, "sec": 0x38, "cld": 0xD8, "xce": 0xFB, "asl": 0x0A, "lsr": 0x4A}
BR = {"bne": 0xD0, "beq": 0xF0, "bcc": 0x90, "bcs": 0xB0, "bpl": 0x10, "bmi": 0x30, "bra": 0x80}


class Asm:
    def __init__(self, org):
        self.org, self.b, self.lab, self.fix = org, bytearray(), {}, []

    def pc(self):
        return self.org + len(self.b)

    def label(self, n):
        assert n not in self.lab, n
        self.lab[n] = self.pc()

    def raw(self, *x):
        self.b += bytes(x)

    def __getattr__(self, name):
        if name in IMPL:
            return lambda: self.raw(IMPL[name])
        if name in BR:
            return lambda lab: (self.raw(BR[name], 0), self.fix.append((len(self.b) - 1, lab, "rel")))
        if name in OPS:
            return lambda arg, mode=None, idx=None: self.op(name, arg, idx)
        raise AttributeError(name)

    def op(self, name, arg, idx=None):
        t = OPS[name]
        if isinstance(arg, tuple):                            # ("lo", label) / ("hi", label): immediate
            kind, lab = arg
            self.raw(t["imm"], 0)
            self.fix.append((len(self.b) - 1, lab, kind))
            return
        if idx is None and isinstance(arg, str):               # abs to a label
            self.raw(t["abs"], 0, 0)
            self.fix.append((len(self.b) - 2, arg, "abs"))
            return
        if idx == "imm":
            self.raw(t["imm"], arg & 255)
        elif idx == "ind":
            self.raw(t["ind"], arg & 255, arg >> 8)
        elif idx in ("x", "y"):
            if arg > 0xFFFF:
                self.raw(t["longx"], arg & 255, (arg >> 8) & 255, arg >> 16)
            else:
                self.raw(t["abs" + idx], arg & 255, arg >> 8)
        elif arg > 0xFFFF:
            self.raw(t["long"], arg & 255, (arg >> 8) & 255, arg >> 16)
        else:
            self.raw(t["abs"], arg & 255, arg >> 8)

    # the usual forms
    def ldai(self, v): self.raw(0xA9, v & 255)
    def ldxi(self, v): self.raw(0xA2, v & 255)
    def ldyi(self, v): self.raw(0xA0, v & 255)
    def cmpi(self, v): self.raw(0xC9, v & 255)
    def cpxi(self, v): self.raw(0xE0, v & 255)
    def andi(self, v): self.raw(0x29, v & 255)
    def lda(self, a, idx=None): self.op("lda", a, idx)
    def sta(self, a, idx=None): self.op("sta", a, idx)
    def sep(self, v): self.raw(0xE2, v)
    def rep(self, v): self.raw(0xC2, v)
    def ldai_l(self, lab, kind): self.raw(0xA9, 0); self.fix.append((len(self.b) - 1, lab, kind))

    def done(self):
        for at, lab, kind in self.fix:
            v = self.lab[lab]
            if kind == "rel":
                d = v - (self.org + at + 1)
                assert -128 <= d <= 127, (lab, d)
                self.b[at] = d & 255
            elif kind == "lo":
                self.b[at] = v & 255
            elif kind == "hi":
                self.b[at] = v >> 8
            else:
                self.b[at] = v & 255
                self.b[at + 1] = v >> 8
        return bytes(self.b)


# C64 RAM variables (bank 0, $0A00-$0AFF: not in the $40-$7F mirror window)
V_FRAME, V_FLAG, V_CNT, V_PH, V_SCR, V_DISP = 0x0A00, 0x0A17, 0x0A18, 0x0A20, 0x0A1B, 0x0A30
MK = [0x0B00, 0x0B01, 0x0B02, 0x0B03]
SRC = 0x021000                                   # SuperRAM bank 2: the scroll source (filled by the program)


def setup_common(t):
    """what the SuperCPU ROM leaves behind, as far as the benches need it: hwenable, boot map off, 20 MHz, $01 = $35"""
    t.sei(); t.cld()
    t.sta(0xD07E)                                # hwenable
    t.sta(0xD0B6)                                # boot map off
    t.sta(0xD07B)                                # 20 MHz
    t.ldai(0x35); t.sta(0x0001)                  # all RAM, I/O visible
    t.ldai(3); t.sta(0xDD02)
    t.ldai(2); t.sta(0xDD00)                     # VIC bank 1 ($4000-$7FFF)
    t.ldai(0x7F); t.sta(0xDC0D); t.sta(0xDD0D); t.lda(0xDC0D); t.lda(0xDD0D)
    t.ldai(0); t.sta(0xD020); t.sta(0xD021)


def sprites(t, double_height=True):
    """eight sprites over the whole screen height: pointers at $77F8 or $63F8 (the caller's screen), Y spread, all enabled"""
    for i in range(8):
        t.ldai(0x30 + 24 * i); t.sta(0xD001 + 2 * i)
        t.ldai(0x30 + 40 * i); t.sta(0xD000 + 2 * i)
    t.ldai(0xFF); t.sta(0xD015); t.sta(0xD01C)
    if double_height:
        t.sta(0xD017)
    t.ldai(0x07)
    for i in range(8):
        t.sta(0xD027 + i)


# ---- S3: the menu ------------------------------------------------------------------------------------------------------------------------------
def menu(one_mhz):
    t = Asm(ORG)
    setup_common(t)
    t.sta(0xD07F)                                # hwenable off again: the crack's menu does not know the SuperCPU registers
    # screen at $6000 (VIC bank 1), text rows; the sprites' pointers at $63F8
    for i in range(8):
        t.ldai(0xB0 + i); t.sta(0x63F8 + i)
    sprites(t, False)
    t.ldai(0x80); t.sta(0xD018)
    for a in (V_FLAG, V_CNT, V_PH, V_SCR):
        t.ldai(0); t.sta(a)
    t.ldai(1); t.sta(V_DISP)
    t.ldai(0xF3); t.sta(0xD012)
    t.ldai(0x1B); t.sta(0xD011)
    t.ldai_l("irq", "lo"); t.sta(0xFFFE)
    t.ldai_l("irq", "hi"); t.sta(0xFFFF)
    t.ldai(0xFF); t.sta(0xD019)
    t.ldai(1); t.sta(0xD01A)
    t.cli()
    t.label("main")
    t.ldai(1)
    t.label("poll"); t.op("cmp", V_FLAG); t.bne("poll")
    t.ldai(0); t.sta(V_FLAG)
    # the main program's work between two interrupts: a loop of table reads and writes
    t.ldxi(0)
    t.label("work")
    t.lda(0x3600, "x"); t.lsr(); t.lsr(); t.lsr(); t.lsr(); t.sta(0x3700, "x"); t.inx(); t.bne("work")
    t.lda(V_CNT); t.cmpi(12); t.bcc("main")
    t.lda(V_PH); t.bne("ph1")
    t.ldai(1); t.sta(V_PH)
    t.sta(MK[2])
    if one_mhz:
        t.sta(0xD07A)                            # system 1 MHz
    t.bra("main")
    t.label("ph1")
    t.lda(V_CNT); t.cmpi(24); t.bcc("main")
    t.sta(MK[3])
    t.label("end"); t.bra("end")

    # the raster IRQ at $F3
    t.label("irq")
    t.pha(); t.txa(); t.pha(); t.tya(); t.pha()
    t.sta(MK[0])
    t.lda(V_SCR); t.sta(0xD016)
    t.ldai(0x8A); t.sta(0xD018)
    t.ldai(0x1B); t.sta(0xD011)
    t.ldai(0)
    t.label("w0"); t.op("cmp", 0xD012); t.bne("w0")         # the raster byte 0 (line 256 comes first on a 312-line frame)
    t.ldai(0x18); t.sta(0xD016)
    t.ldai(0x80); t.sta(0xD018)
    t.ldai(0x3B); t.sta(0xD011)
    t.jsr("scroll")
    t.jsr("sprmux")
    t.jsr("music")
    t.ldai(0x7F); t.sta(0xDC00); t.lda(0xDC01); t.andi(0x10)
    t.ldai(0xFF); t.sta(0xD019)                             # the acknowledge, at the very end
    t.inc(V_CNT)
    t.ldai(1); t.sta(V_FLAG)
    t.sta(MK[1])
    t.pla(); t.tay(); t.pla(); t.tax(); t.pla(); t.rti()

    t.label("scroll")                                       # the text row $63C0-$63E7 one cell left: 40 load/store pairs
    for k in range(40):
        t.lda(0x63C1 + k); t.sta(0x63C0 + k)
    t.rts()
    t.label("sprmux")
    t.lda(0x7700)
    for i in range(4):
        t.sta(0xD001 + 2 * i)
    t.lda(0x7800)
    for i in range(4):
        t.sta(0xD009 + 2 * i)
    t.rts()
    t.label("music")                                        # 25 SID registers from a table, then some arithmetic
    t.ldxi(0)
    t.label("m0"); t.lda(0x3700, "x"); t.sta(0xD400, "x"); t.inx(); t.cpxi(25); t.bne("m0")
    t.ldxi(100)
    t.label("m1"); t.clc(); t.op("adc", 0x3700, ); t.sta(0x0A40); t.dex(); t.bne("m1")
    t.rts()
    return t


# ---- S1/S2: the game's write stream ---------------------------------------------------------------------------------------------------------------
def game(hud):
    t = Asm(ORG)
    setup_common(t)
    t.clc(); t.xce()                              # native mode
    t.sep(0x30)                                   # 8-bit accumulator and index
    # SuperRAM bank 2: the scroll source, 1 KiB of a pattern (the sprite pointers at +$3F8..)
    t.ldxi(0)
    t.label("fill")
    for pg in range(4):
        t.txa(); t.raw(0x49, 0x5A + pg); t.sta(SRC + 0x100 * pg, "x")       # eor #$5A+pg
    t.inx(); t.bne("fill")
    for i in range(8):
        t.ldai(0xB0 + i); t.sta(SRC + 0x3F8 + i)
    # VIC: bitmap, screen $7400, bitmap $6000
    t.ldai(0xD8); t.sta(0xD018)
    t.ldai(0x3B); t.sta(0xD011)
    t.ldai(0x18); t.sta(0xD016)
    sprites(t)
    t.sta(0xD075)                                 # optimisation mode: mirror $40-$7F
    t.ldai(0); t.sta(V_FRAME)
    if hud:
        t.ldai_l("hud", "lo"); t.sta(0x81BD)
        t.ldai_l("hud", "hi"); t.sta(0x81BE)
        t.ldai_l("irq", "lo"); t.sta(0xFFEE)
        t.ldai_l("irq", "hi"); t.sta(0xFFEF)
        t.ldai(0xD1); t.sta(0xD012)
        t.ldai(0xFF); t.sta(0xD019)
        t.ldai(1); t.sta(0xD01A)
        t.cli()
    t.label("pass")
    t.sta(MK[0])
    # sprite multiplexer: pointers and registers between
    for rep in range(3):
        for i in range(8):
            t.lda(V_FRAME); t.raw(0x69, 0xB0 + i); t.sta(0x77F8 + i)       # adc #
            t.sta(0xD001 + 2 * i)
    # the scroll: 1,024 bytes SuperRAM -> screen ($7400-$77FF, the pointers at the end), changing every pass
    t.ldxi(0)
    t.label("scr")
    for pg in range(4):
        t.lda(SRC + 0x100 * pg, "x"); t.op("eor", V_FRAME); t.sta(0x7400 + 0x100 * pg, "x")
    t.inx(); t.bne("scr")
    # the same with a block move (MVN $00,$02: 7 cycles a byte, the destination bank is 0) and 16-bit stores (two mirrored bytes in two PHI2 cycles)
    t.rep(0x30)
    t.raw(0xA2, SRC & 255, (SRC >> 8) & 255); t.raw(0xA0, 0x00, 0x74); t.raw(0xA9, 0xFF, 0x01)      # LDX #$1000, LDY #$7400, LDA #$01FF
    t.raw(0x54, 0x00, 0x02)                                                         # MVN $00,$02 (destination bank, source bank)
    t.sep(0x30)
    t.rep(0x20)
    t.ldxi(0)
    t.label("w16")
    t.lda(V_FRAME); t.sta(0x7800, "x"); t.inx(); t.inx(); t.cpxi(0xF0); t.bne("w16")
    t.sep(0x30)
    # the HUD windows: $D077 (mirror all), VIC bank 3
    t.sta(0xD077)
    t.ldxi(0)
    t.label("hudw")
    t.lda(0x3000, "x"); t.op("eor", V_FRAME)
    t.sta(0xFB90, "x"); t.sta(0xFC88, "x"); t.sta(0xFC18, "x")
    t.inx(); t.cpxi(0x60); t.bne("hudw")
    t.sta(0xD075)
    t.inc(V_FRAME)
    t.sta(MK[1])
    t.jmp("pass")
    if hud:
        # IRQ entry of the native vector: 16-bit register save as the game's chain does, then the stage through a RAM word
        t.label("irq")
        t.rep(0x30); t.pha(); t.phx(); t.phy(); t.sep(0x30)
        t.sta(MK[2])
        t.lda(0xD019); t.sta(0xD019)                         # acknowledge (the stage that set this one up did it in the game; before the CLI below at the latest)
        t.jmp(0x81BD, idx="ind")
        t.label("hud")                                       # the HUD stage at line $D1 ($9A6E-$9AE4 of the analysis)
        t.sei()
        t.lda(0xD012); t.cmpi(0xD2); t.bcs("h1")
        t.label("h0"); t.op("cmp", 0xD012); t.beq("h0")
        t.label("h1")
        t.ldai(0x6B); t.sta(0xD011)
        t.stz(0xD021); t.stz(0xD015)
        for r in (0xD000, 0xD002, 0xD004, 0xD006, 0xD008, 0xD00A, 0xD00C, 0xD00E, 0xD010):
            t.stz(r)
        t.ldai(0x00); t.sta(0xDD00)
        t.ldai(0xD8); t.sta(0xD016)
        t.ldai(0xD9); t.sta(0xD018)
        t.ldai(0xFB)
        for r in (0xD001, 0xD003, 0xD005, 0xD007, 0xD009, 0xD00B, 0xD00D, 0xD00F):
            t.sta(r)
        t.ldai(0x3B); t.sta(0xD011)
        t.cli()
        t.ldai(0xFF); t.sta(0xD012)
        t.ldai_l("low", "lo"); t.sta(0x81BD)
        t.ldai_l("low", "hi"); t.sta(0x81BE)
        t.rep(0x30); t.ply(); t.plx(); t.pla(); t.rti()
        t.label("low")                                       # the stage at line $FF: the screen back
        t.ldai(0x02); t.sta(0xDD00)
        t.ldai(0xD8); t.sta(0xD018)
        t.ldai(0x3B); t.sta(0xD011)
        t.ldai(0xFF); t.sta(0xD015)
        t.ldai(0xD1); t.sta(0xD012)
        t.ldai_l("hud", "lo"); t.sta(0x81BD)
        t.ldai_l("hud", "hi"); t.sta(0x81BE)
        t.ldai(0xFF); t.sta(0xD019)
        t.rep(0x30); t.ply(); t.plx(); t.pla(); t.rti()
    return t


def stzbench():
    """S2, the bare case: 24 back-to-back `stz $D0xx` at 20 MHz (sprites off, the screen off: no badline, no sprite DMA, no IRQ), emulation mode as the SuperCPU ROM leaves it"""
    t = Asm(ORG)
    setup_common(t)
    t.ldai(0x0B); t.sta(0xD011)                  # DEN off: no badlines
    t.ldai(0); t.sta(0xD015)
    t.jsr("seq"); t.jsr("seq"); t.jsr("seq")     # the first call runs the code cold (its cache lines are refilled), the next ones warm
    t.label("end"); t.bra("end")
    t.label("seq")
    for r in list(range(0xD000, 0xD010)) + list(range(0xD020, 0xD028)):
        t.stz(r)
    t.rts()
    return t


NLINES, NPASS = 60, 8
F_FAIL, F_PASS, SMC0, SMCB = 0xF4, 0xF5, 0x1800, 0x02       # the lines run in SuperRAM bank 2 at $1800 (copied from bank 0 at the start); results to $02:0C00+k
V_RES = 0x020C00


def smcbench():
    """T208, the coherence of the code cache's next-line prefetch (scpu_cache.v): a straight run of 60 code lines of 16 bytes in SuperRAM bank 2 (no mirror, no write buffer: the stores
    come when the code says) modifies itself. Line k loads an immediate (the byte the line k-1 stored there a moment before: the pass number), stores it to $02:0C00+k and stores the pass
    number (X) into the immediate of line k+1 - while that line is on its way into the cache (the prefetch started with the first fetch of line k) or just arrived. Two line shapes
    sweep the store through 35..76 clk2x after the line's first fetch (the fill takes 25..94, median 39): A = lda, tay, p NOPs (p 0..3), txa, sta, tya, sta; B = lda, sta, p NOPs (p 0..5),
    txa, sta. A stale line (the old immediate) would leave the previous pass's number in $02:0C00+k. Before every pass a sweep of 8 KiB of bank 3 (same cache indexes, other tag) evicts
    all code lines. Markers: 1 every result right in every pass, 2 a wrong result, 3 end."""
    t = Asm(ORG)
    setup_common(t)
    t.ldai(0x0B); t.sta(0xD011)                  # DEN off: no badlines, the timing is the CPU's
    t.ldai(0); t.sta(0xD015)
    t.ldai(0); t.sta(F_FAIL)
    t.ldxi(0)                                     # copy the lines to bank 2
    t.label("cp")
    for pg in range(4):
        t.lda(SMC0 + 256 * pg, "x"); t.sta((SMCB << 16) + SMC0 + 256 * pg, "x")
    t.inx(); t.bne("cp")
    t.ldai(1); t.sta(F_PASS)
    t.label("pass")
    t.ldx(F_PASS)
    t.txa(); t.sta((SMCB << 16) + SMC0 + 1)      # the immediate of line 0
    t.jsr("evict")
    t.ldx(F_PASS)
    t.raw(0x22, SMC0 & 255, SMC0 >> 8, SMCB)     # jsl L0
    t.jsr("verify")
    t.lda(F_PASS); t.raw(0x1A); t.sta(F_PASS)    # inc a
    t.cmpi(NPASS + 1); t.bne("pass")
    t.lda(F_FAIL); t.bne("bad")
    t.sta(0x0B01); t.bra("fin")                  # A = 0 here: any write to the marker counts
    t.label("bad"); t.sta(0x0B02)
    t.label("fin"); t.sta(0x0B03)
    t.label("end"); t.bra("end")
    t.label("evict")                              # read 16 bytes apart over 8 KiB of bank 3 through a long pointer in the direct page
    t.ldai(0); t.sta(0xF0); t.sta(0xF1); t.ldai(3); t.sta(0xF2)
    t.label("ep"); t.ldyi(0)
    t.label("eq"); t.raw(0xB7, 0xF0)             # lda [$F0],y
    t.tya(); t.clc(); t.raw(0x69, 16); t.tay(); t.bcc("eq")
    t.inc(0xF1); t.lda(0xF1); t.cmpi(0x20); t.bne("ep")
    t.rts()
    t.label("verify")
    t.ldxi(NLINES - 1)
    t.label("vl"); t.lda(V_RES, "x"); t.cmp(F_PASS); t.beq("vk"); t.inc(F_FAIL)
    t.label("vk"); t.dex(); t.bpl("vl"); t.rts()
    assert t.pc() <= SMC0, hex(t.pc())
    t.raw(*([0xEA] * (SMC0 - t.pc())))
    t.label("L0")
    base = t.pc()
    for k in range(NLINES):
        a = base + 16 * k
        nxt = (SMCB << 16) + a + 16 + 1
        res = V_RES + k
        t.raw(0xA9, 0x00)                         # lda #imm (stored by the line before)
        if k % 2 == 0:                            # shape A
            t.tay()
            for _ in range((k // 2) % 4):
                t.nop()
            t.txa(); t.raw(0x8F, nxt & 255, (nxt >> 8) & 255, nxt >> 16)
            t.tya(); t.raw(0x8F, res & 255, (res >> 8) & 255, res >> 16)
        else:                                     # shape B
            t.raw(0x8F, res & 255, (res >> 8) & 255, res >> 16)
            for _ in range((k // 2) % 6):
                t.nop()
            t.txa(); t.raw(0x8F, nxt & 255, (nxt >> 8) & 255, nxt >> 16)
        assert (t.pc() - base) <= 16 * (k + 1), (k, t.pc() - base)
        while (t.pc() - base) % 16:
            t.nop()
    t.raw(0xA9, 0x00, 0x6B)                       # line 60: lda #imm (the last store lands here), rtl
    return t


def starsbench(pad=None):
    """T208 stars (run_mdust.sh `stars`): the structure of the crack menu's flickering stars (04_menu, board trace 2026-11-14, spec T208 section 5): native mode, mirror all ($D077), $D011 = $3B,
    sprites 6 and 7, a raster IRQ chain every 3 lines from line 129 to 231 (the handler: acknowledge $D019, five stores $D00C/$D00E/$D010/$D02D/$D02E, four mirrored stores $836C/$836D/$8370/$8371,
    the next line to $D012, $D011 read and written back, marker 1 before the pops and the RTI) and a CIA 1 timer A IRQ every 2457 PHI2 cycles (8 a frame) whose handler does CLI, then 31 mirrored
    stores to $E0xx and 15 stores to $D4xx (the raster IRQs nest in it). The main loop only waits."""
    t = Asm(ORG)
    setup_common(t)
    t.clc(); t.xce()
    t.sep(0x30)
    t.ldai(0x3B); t.sta(0xD011)
    t.ldai(0x18); t.sta(0xD016)
    t.ldai(0xD8); t.sta(0xD018)
    t.ldai(0xC0); t.sta(0xD015)                      # sprites 6 and 7
    t.ldai(0x90); t.sta(0xD00D); t.sta(0xD00F)
    t.ldai(0x30); t.sta(0xD00C); t.ldai(0x60); t.sta(0xD00E)
    t.ldai(0x01); t.sta(0xD02D); t.sta(0xD02E)
    t.sta(0xD077)                                    # mirror all
    t.ldai(129); t.sta(0x0A40)
    t.ldai(0); t.sta(0x0A41); t.sta(0x0A42)
    t.ldai_l("irq", "lo"); t.sta(0xFFEE)
    t.ldai_l("irq", "hi"); t.sta(0xFFEF)
    t.ldai(129); t.sta(0xD012)
    t.ldai(0xFF); t.sta(0xD019)
    t.ldai(1); t.sta(0xD01A)
    t.ldai(0x98); t.sta(0xDC04)
    t.ldai(0x09); t.sta(0xDC05)                      # 2456 + 1 = 2457 PHI2 cycles
    t.ldai(0x81); t.sta(0xDC0D)
    t.ldai(0x01); t.sta(0xDC0E)
    t.cli()
    t.label("main"); t.bra("main")
    t.label("irq")
    if pad is None:
        t.rep(0x30); t.pha(); t.phx(); t.phy(); t.sep(0x30)     # 16 bit register save (6 mirrored stack stores in mode $D077)
    else:
        t.pha()                                          # starsq<N>: only A is saved and N NOPs stand in for the rest of a dispatcher, the I/O accesses fall into the VIC's BA window (cycles 12-18)
        for _ in range(pad):
            t.nop()
    t.lda(0xDC0D); t.andi(1); t.bne("music")
    t.lda(0xD019); t.andi(1); t.beq("out")
    t.ldai(1); t.sta(0xD019)
    t.lda(0x0A40)                                    # the line counter is only read before the five stores: a store to it would be a mirrored one (mode $D077)
    for r in (0xD00C, 0xD00E, 0xD010, 0xD02D, 0xD02E):
        t.sta(r)
    for r in (0x836C, 0x836D, 0x8370, 0x8371):
        t.sta(r)
    t.lda(0x0A40); t.clc(); t.raw(0x69, 3); t.cmpi(232); t.bcc("nx"); t.ldai(129)
    t.label("nx"); t.sta(0x0A40); t.sta(0xD012)
    t.lda(0xD011); t.sta(0xD011)
    t.sta(MK[1])
    t.label("out")
    if pad is None:
        t.rep(0x30); t.ply(); t.plx(); t.pla(); t.rti()
    else:
        t.pla(); t.rti()
    t.label("music")
    t.cli()                                          # the music ISR is interruptible: the raster IRQs nest in it
    t.inc(0x0A41); t.lda(0x0A41)
    for k in range(31):
        t.sta(0xE000 + (k * 7) % 0x80)
    for k in range(15):
        t.sta(0xD400 + k)
    t.jmp("out")
    return t


def image(kind):
    global SRC
    if kind == "hud":
        SRC = 0x022800                           # not on the cache index of the HUD stage's code ($12C7-$13FF, index = address bits 12:4): the stage stays in the code cache between two frames
    if kind == "stz":
        t = stzbench()
    elif kind == "smc":
        t = smcbench()
    elif kind == "stars":
        t = starsbench()
    elif kind.startswith("starsq"):
        t = starsbench(int(kind[6:]))
    elif kind in ("menu20", "menu1"):
        t = menu(kind == "menu1")
    else:
        t = game(kind in ("hud", "hudx"))
    blob = t.done()
    assert len(blob) <= PAGES * 256, len(blob)
    f = bytearray(0x20000)
    f[BLOB:BLOB + len(blob)] = blob
    a = Asm(0x8000)
    a.sei(); a.cld(); a.ldxi(0xFF); a.raw(0x9A)           # SEI, CLD, LDX #$FF, TXS
    a.ldxi(0)
    a.label("cp")
    for k in range(PAGES):
        a.raw(0xBD, 0x00, (BLOB >> 8) + k); a.raw(0x9D, 0x00, (ORG >> 8) + k)
    a.inx(); a.beq("dn"); a.jmp("cp")
    a.label("dn"); a.jmp(ORG)
    head = a.done()
    f[0x8000:0x8000 + len(head)] = head
    for base in (0x0000, 0x10000):
        f[base + 0xFFFC] = 0x00
        f[base + 0xFFFD] = 0x80
    return f, t


def main():
    out, kind = sys.argv[1], sys.argv[2]
    f, t = image(kind)
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "scpu_rom.mem"), "w") as fh:
        fh.write("\n".join("%02x" % x for x in f) + "\n")
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as fh:
        fh.write("0000\n" * 0x2800)
    with open(os.path.join(out, "expect.json"), "w") as fh:
        json.dump({"kind": kind, "labels": {k: hex(v) for k, v in t.lab.items()}, "size": len(t.b)}, fh)
    print("")


main()
