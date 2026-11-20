#!/usr/bin/env python3
"""T182 step 6: the synthetic SuperCPU ROM and the expectations of the interrupt / vector bench S9 and the native-mode part of S3 (cores/c64scpu/sim/run_irq.sh, spec T182 sections 2 S9 / S3 and 5.8).
The ROM is our own code (no byte of a CMD or Commodore ROM): 128 KiB, reset vector $8000. The head copies the emulation-mode tail to SRAM $1000 and the native-mode code to SuperRAM $02:0000.
The expectations are written here from scpu64cpu.c (LOAD_INT_ADDR :409-417) and scpu64mem.c (scpu64_interrupt_reroute :1741-1744) as read by hand, and from the 65816 instruction set (the S3 model below is plain
Python integer arithmetic of the instructions the program uses, not read back from the machine).

  gen_c64scpu_irq.py <out dir>   writes <out>/mem/scpu_rom.mem, <out>/mem/c64scpu_rom.mem (zeros: the system ROM slot), <out>/expect.json;
                                 prints "+EXPECT=aaaa:vv,... +DEXPECT=aaaaaaaa:vv,..." (C64 RAM bytes, absolute DDR2 bytes)

Program (the C64 RAM verdict bytes $0320+n are $AA when the phase's handler count is in its range, the raw counts are at $0330+n, handler counters at $0310-$0318 and $031A-$031B):
  0  emulation mode, hwenable: CIA 1 timer A IRQ every 4,000 PHI2 cycles for 100,000 cycles (the vector is rerouted to the ROM's $FFFE: class KS, hwenable)      25 +- 1
  1  emulation mode, hwenable: CIA 2 timer A NMI (vector $FFFA from the ROM)                                                                                    25 +- 1
  2  emulation mode, hwenable: VIC raster IRQ (line $80, once a frame)                                                                                           5..6
  3  emulation mode, hwenable off: CIA 1 IRQ, no reroute, the vector comes from SRAM bank 1 ($01:FFFE: class KT)                                                  25 +- 1
  T241, emulation mode, hwenable off (vector $01:FFFE, as 3): a VIC raster IRQ (line $80) acknowledged by the handler's read-modify-write instruction, 40,000 PHI2 cycles, 16-bit count:
  7  ASL $D019 at software 1 MHz (the DMAgic intro's acknowledge)                                                                                                 2..3
  8  INC $D019 at software 1 MHz (Master's Boom's acknowledge)                                                                                                    2..3
  9  ASL $D019 at 20 MHz                                                                                                                                          2..3
     (each IRQ once a frame; without the write of the byte read in the modify cycle the flag stays set, the IRQ is taken again at the end of every RTI and the main program stops in phase 7: the verdicts stay $00)
     then ASL $0B02 on RAM at 20 MHz: three completed writes to $0B02 (STA, the byte read, the new byte; run_irq.sh counts the tb's "mark|2|...|frame" lines) and $0B02 = $82
  T241 S2, emulation mode, hwenable off, software 1 MHz, display off:
  10  an IRQ that rises in the HIGH half of an instruction's last cycle is taken after the NEXT instruction (the 65816 polls one cycle before the last): the raster compare of the VIC is set
      to the line $20 by `INC $D012` while the raster is on line $20 (mask on, flag clear, display off). In emulation mode the modify cycle writes the byte read, the current line, back into
      the compare; the VIC matches in the high half of the last cycle (the write of line + 1), so the line rises after the CPU's poll point. The handler must see M0 = 1 (the `INC M0` after the
      `INC $D012` ran) and M1 = 0 (verdict $032A, raw $033A = R0 + 2 R1 + 4 taken). The old logic (the live level at the end of the last cycle) takes the IRQ right after the `INC $D012`: M0 = 0.
      A write that enables the source (`STA $D01A`) does NOT discriminate: the VIC takes a register write at the PHI2 fall, the wrapper samples the line at that same edge, so old and new logic
      both see it first during the next instruction.
      source: VICE 65816core.c:889-890 (LOAD_ABS_FUNC_RRW, emulation mode: CHECK_INTERRUPT, then the dummy STORE_LONG of the byte read, the modify cycle), :2588 (CHECK_INTERRUPT before the
      next opcode fetch) and :2575 (an interrupt is taken at the top only from a check made earlier); viciisc/vicii-cycle.c:467-474 (the compare match is found in the cycle after the compare write): the
      check in the modify cycle finds nothing, the match comes in the last cycle, the check before the next opcode fetch finds it, that instruction (`INC M0`) runs, the IRQ is taken before the second (`INC M1`)
  11  RTI in emulation mode takes 6 cycles: a subroutine that runs X times `LDA #hi; PHA; LDA #lo; PHA; PHP; RTI; DEX; BNE` between a write to $0B00 and one to $0B01 (the tb's marks 0 and 1
      print their PHI2 cycles), called with X = 20 (warm-up, ignored), 20 and 10; the difference of the last two is 10 x (2+3+2+3+3+RTI+2+3) = 240 (check_irq.py)
  native mode: 12  RTI in native mode takes 7 cycles: the same with `PHK; PEA ret; PHP; RTI; DEX; BNE` at 1 MHz: 10 x (3+5+3+7+2+3) = 230
      source of the cycle counts: W65C816S datasheet (WDC, instruction table: RTI 7 cycles with E = 0, 6 with E = 1; PHA, PHP, PHK 3; PEA 5; LDA #, DEX 2; BNE 3 taken, +1 when it crosses a page with E = 1);
      VICE 65816core.c:2217-2240 (RTI: INC_PC, two FETCH_PARAM_DUMMY, PULL P, PULL PCL, PULL PCH and, in native mode, PULL PBR: one clock each, plus the opcode fetch)
      Which phases discriminate (fail on the logic before T241 S2): 10 and 11 (the old RTI took 5 cycles in emulation mode: 230 instead of 240). Phase 12 passes on the old core as well (native RTI was 7
      cycles): it guards the native RTI against a regression of the same change.
  native mode ($02:0000), hwenable: 4 CIA 1 IRQ (vector $FFEE) 25 +- 1, 5 CIA 2 NMI (vector $FFEA) 25 +- 1, 6 ten RESTORE pulses (tb: a write to $0B08 pulses ext_restore) = exactly 10 NMIs,
  BRK x3 ($FFE6) and COP x2 ($FFE4) = exactly 3 and 2, then the S3 block: 16-bit arithmetic, MVN across banks, the 16-bit stack beyond page 1, [dp],Y, XBA, long,X, JSL/RTL, in SuperRAM $03:0000,
  and (T241) an 8-bit INC $0B03 in native mode: one write (the modify cycle stays an internal one), two marker-3 lines with the STA before it, the result $80 at $03:0030.
"""
import json
import os
import sys

ORG = 0x1000          # the emulation-mode tail in SRAM
NAT = 0x9000          # the native code in the ROM (copied to $02:0000)
DATA = 0x0600         # 16 data bytes in the native image (bank 2)


class Asm:
    def __init__(self, org):
        self.org, self.b, self.lab, self.fix = org, bytearray(), {}, []

    def pc(self):
        return self.org + len(self.b)

    def label(self, n):
        self.lab[n] = self.pc()

    def e(self, *x):
        self.b += bytes(x)

    def lda_i(self, v): self.e(0xA9, v)
    def ldx_i(self, v): self.e(0xA2, v)
    def ldy_i(self, v): self.e(0xA0, v)
    def cmp_i(self, v): self.e(0xC9, v)
    def sbc_i(self, v): self.e(0xE9, v)
    def and_i(self, v): self.e(0x29, v)
    def lda_a(self, a): self.e(0xAD, a & 255, a >> 8)
    def sta_a(self, a): self.e(0x8D, a & 255, a >> 8)
    def inc_a(self, a): self.e(0xEE, a & 255, a >> 8)
    def sta_l(self, a): self.e(0x8F, a & 255, (a >> 8) & 255, a >> 16)
    def jmp(self, a): self.e(0x4C, a & 255, a >> 8)

    def br(self, op, n):
        self.e(op, 0)
        self.fix.append((len(self.b) - 1, n))

    def done(self):
        for at, n in self.fix:
            d = self.lab[n] - (self.org + at + 1)
            assert -128 <= d <= 127, (n, d)
            self.b[at] = d & 255
        return bytes(self.b)


CNT = {"ei": 0x0310, "en": 0x0311, "ni": 0x0312, "nn": 0x0313, "brk": 0x0314, "cop": 0x0315, "sv": 0x0316, "rst": 0x0318}
VERD, RAW = 0x0320, 0x0330
BFLAG = 0x0319          # bit 4 of the P byte pushed by the emulation-mode interrupts (T182 real-ROM boot: it must stay 0)
CNT16 = 0x031A          # T241: the 16-bit handler count of phases 7-9 (low, high)
M0, M1, R0, R1, NTAKEN, TMP = 0x0350, 0x0351, 0x0352, 0x0353, 0x0354, 0x0355     # T241 S2 phase 10: the two instructions' marks, what the handler saw, handler runs, scratch
SYNC_LINE = 0x20      # T241 S2 phase 10: the raster line the INC $D012 starts on (below the first badline, line 48)
RTI_N = (20, 10)         # T241 S2: the iterations of the two RTI loops (phases 11, 12)
RTI_EMU, RTI_NAT = 10 * (2 + 3 + 2 + 3 + 3 + 6 + 2 + 3), 10 * (3 + 5 + 3 + 7 + 2 + 3)    # the cycle differences of the two loops (docstring)
RAW_SLOTS = (0, 1, 2, 3, 4, 5, 7, 8, 9, 10)       # the phases that write a raw byte at RAW + slot (6 is the RESTORE count, kept in CNT["rst"]); run_irq.sh dumps $0330-$033F
MARKS = {"2": 3, "3": 2}    # T241: completed 65816 writes to $0B02 (emulation ASL: STA + 2) and $0B03 (native INC: STA + 1), counted by run_irq.sh (check_irq.py)


def stop_all(t):
    t.lda_i(0); t.sta_a(0xDC0E); t.sta_a(0xDD0E); t.sta_a(0xD01A)
    t.lda_i(0x7F); t.sta_a(0xDC0D); t.sta_a(0xDD0D); t.lda_a(0xDC0D); t.lda_a(0xDD0D)
    t.lda_i(0xFF); t.sta_a(0xD019)


def wait(t, base, lo, hi, n, tag):
    """n times the one-shot timer B of the CIA at base (PHI2 cycles lo + 256 hi each), polled through its ICR"""
    t.ldx_i(n)
    t.label("wl" + tag)
    t.lda_i(lo); t.sta_a(base + 6); t.lda_i(hi); t.sta_a(base + 7); t.lda_i(0x19); t.sta_a(base + 0xF)
    t.label("wp" + tag); t.lda_a(base + 0xD); t.and_i(2); t.br(0xF0, "wp" + tag)
    t.e(0xCA); t.br(0xD0, "wl" + tag)


def verdict(t, counter, lo, span, slot, tag):
    t.lda_a(counter); t.sta_a(RAW + slot)
    t.e(0x38); t.sbc_i(lo); t.cmp_i(span); t.br(0x90, "vk" + tag)
    t.lda_i(0xEE); t.br(0x80, "vs" + tag)
    t.label("vk" + tag); t.lda_i(0xAA)
    t.label("vs" + tag); t.sta_a(VERD + slot)


def cia_irq(t, tag, counter, slot, lo=24, span=3, nmi=False):
    """a CIA timer A at 4,000 cycles as the interrupt source, 100,000 PHI2 cycles measured by the other CIA's timer B"""
    src, clk = (0xDD00, 0xDC00) if nmi else (0xDC00, 0xDD00)
    stop_all(t)
    t.lda_i(0); t.sta_a(counter)
    t.lda_i(0x9F); t.sta_a(src + 4); t.lda_i(0x0F); t.sta_a(src + 5)
    t.lda_i(0x81); t.sta_a(src + 0xD); t.lda_i(0x11); t.sta_a(src + 0xE)
    if not nmi:
        t.e(0x58)                                        # CLI
    wait(t, clk, 0x50, 0xC3, 2, tag)
    t.e(0x78)                                            # SEI
    stop_all(t)
    verdict(t, counter, lo, span, slot, tag)


def vic_irq(t, tag, counter, slot):
    stop_all(t)
    t.lda_i(0); t.sta_a(counter)
    t.lda_i(0x0B); t.sta_a(0xD011); t.lda_i(0x80); t.sta_a(0xD012)
    t.lda_i(0xFF); t.sta_a(0xD019); t.lda_i(1); t.sta_a(0xD01A)
    t.e(0x58)
    wait(t, 0xDD00, 0x50, 0xC3, 2, tag)
    t.e(0x78)
    stop_all(t)
    verdict(t, counter, 5, 2, slot, tag)


def verdict16(t, counter, lo, span, slot, tag):
    """T241: verdict() of the 16-bit count at counter / counter + 1; 256 or more is first set to $FF in the low byte (it can not pass, the raw byte shows it)"""
    t.lda_a(counter + 1); t.br(0xF0, "v16" + tag)
    t.lda_i(0xFF); t.sta_a(counter)
    t.label("v16" + tag)
    verdict(t, counter, lo, span, slot, tag)


def vic_rmw(t, tag, handler, slot, one_mhz):
    """T241: a VIC raster IRQ (line $80) in emulation mode with hwenable off, the vector in the SRAM of bank 1 ($01:FFFE = handler, as phase 3); the handler acknowledges with a
    read-modify-write instruction. 40,000 PHI2 cycles (one-shot timer B of CIA 2) are 2.03 PAL frames: 2..3 handler runs when each IRQ is taken once"""
    stop_all(t)
    t.lda_i(0); t.sta_a(CNT16); t.sta_a(CNT16 + 1)
    t.lda_i(handler & 255); t.sta_l(0x01FFFE); t.lda_i(handler >> 8); t.sta_l(0x01FFFF)
    t.sta_a(0xD07A if one_mhz else 0xD07B)               # software 1 MHz / 20 MHz
    t.lda_i(0x0B); t.sta_a(0xD011); t.lda_i(0x80); t.sta_a(0xD012)
    t.lda_i(0xFF); t.sta_a(0xD019); t.lda_i(1); t.sta_a(0xD01A)
    t.e(0x58)                                            # CLI
    wait(t, 0xDD00, 0x40, 0x9C, 1, tag)
    t.e(0x78)                                            # SEI
    stop_all(t)
    t.sta_a(0xD07B)                                      # 20 MHz again
    verdict16(t, CNT16, 2, 2, slot, tag)


def rmw_handlers(t):
    """T241: the handlers of phases 7-9, emulation mode: count (16 bits) and acknowledge the VIC with ASL $D019 / INC $D019, as the DMAgic intro and Master's Boom do.
    They change no register (RTI restores the flags), so no PHA."""
    for name, op in (("hasl", 0x0E), ("hinc", 0xEE)):
        t.label(name)
        t.inc_a(CNT16); t.br(0xD0, "c" + name); t.inc_a(CNT16 + 1)
        t.label("c" + name)
        t.e(op, 0x19, 0xD0)                              # ASL $D019 / INC $D019: the acknowledge under test
        t.e(0x40)                                        # RTI


def last_handler(t):
    """T241 S2 phase 10: switches the VIC source off first (the compare is line + 1 after the INC: the next line matches again), copies what the main program did before the IRQ was taken
    (M0, M1), counts the entry and acknowledges the flag. PHA/PLA keep A."""
    t.label("hlast")
    t.e(0x48)                                            # PHA
    t.lda_i(0); t.sta_a(0xD01A)                          # the source off
    t.lda_a(M0); t.sta_a(R0)
    t.lda_a(M1); t.sta_a(R1)
    t.inc_a(NTAKEN)
    t.lda_i(0xFF); t.sta_a(0xD019)                       # the flag acknowledged
    t.e(0x68)                                            # PLA
    t.e(0x40)                                            # RTI


def vic_last(t, tag, handler, slot):
    """T241 S2 phase 10 (docstring). Emulation mode, hwenable off (vector $01:FFFE = handler), software 1 MHz, display off. The compare is set to line $A0, the CPU waits for line $20 (a line
    before the first badline: no BA hold in any case) and clears the flag (the compare is not line $20: nothing is pending), the mask is switched on (a guard: no match, no IRQ), CLI, then
    `INC $D012`: the byte read (line $20) is written back by the modify cycle (T241 RMW write), the VIC matches in the high half of the last cycle (the write of $21) and the line rises there.
    INC M0 is the first instruction after it (it must run before the handler), INC M1 the second (it must not). The handler copies M0 and M1 to R0 and R1.
    Verdict $AA when R0 = 1, R1 = 0 and the handler ran once; the raw byte is R0 + 2 R1 + 4 runs.
    From the line change to the INC's last cycle at most 9 (the poll loop's period) + 18 (CMP, BNE, LDA #, STA, LDA #, STA, CLI after the poll's read) + 6 (the INC) = 33 PHI2 cycles; the line has
    63 (PAL) or 65: it does not change under the INC."""
    stop_all(t)
    for a in (M0, M1, R0, R1, NTAKEN):
        t.lda_i(0); t.sta_a(a)
    t.lda_i(handler & 255); t.sta_l(0x01FFFE); t.lda_i(handler >> 8); t.sta_l(0x01FFFF)
    t.sta_a(0xD07A)                                      # software 1 MHz
    t.lda_i(0); t.sta_a(0xD015)                          # no sprite: no sprite DMA, the CPU is not held
    t.lda_i(0x0B); t.sta_a(0xD011)                       # display off (DEN = 0): no badline, the CPU is not held
    t.lda_i(0xA0); t.sta_a(0xD012)                       # the compare: a line other than the one the INC starts on
    t.label("wf" + tag); t.lda_a(0xD012); t.cmp_i(SYNC_LINE); t.br(0xD0, "wf" + tag)     # sync to line $20
    t.lda_i(0xFF); t.sta_a(0xD019)                       # clear the flags (the compare did not match on this line)
    t.lda_i(1); t.sta_a(0xD01A)                          # the mask on (a guard: the flag is clear, the compare is not this line)
    t.e(0x58)                                            # CLI
    t.e(0xEE, 0x12, 0xD0)                                # INC $D012: read $20, modify cycle writes $20, last cycle writes $21; the match comes in the last cycle
    t.inc_a(M0)                                          # the first instruction after it
    t.inc_a(M1)                                          # the second
    t.e(0xEA, 0xEA, 0xEA, 0xEA)                          # (an IRQ taken late still finds a place here)
    t.e(0x78)                                            # SEI
    stop_all(t)
    t.sta_a(0xD07B)                                      # 20 MHz again
    t.lda_a(R0); t.e(0x49, 1); t.e(0x0D, R1 & 255, R1 >> 8); t.sta_a(TMP)             # LDA R0, EOR #1, ORA R1
    t.lda_a(NTAKEN); t.e(0x49, 1); t.e(0x0D, TMP & 255, TMP >> 8); t.sta_a(TMP)       # zero when R0 = 1, R1 = 0 and one run
    t.lda_a(NTAKEN); t.e(0x0A); t.e(0x0D, R1 & 255, R1 >> 8); t.e(0x0A); t.e(0x0D, R0 & 255, R0 >> 8); t.sta_a(RAW + slot)
    t.lda_a(TMP); t.br(0xD0, "lb" + tag)
    t.lda_i(0xAA); t.br(0x80, "ls" + tag)
    t.label("lb" + tag); t.lda_i(0xEE)
    t.label("ls" + tag); t.sta_a(VERD + slot)


def rti_loop(t, tag, native):
    """T241 S2 phases 11 / 12: the subroutine that times X returns through RTI to the next instruction: a write to $0B00 (the tb's mark 0), the loop, a write to $0B01 (mark 1), RTS. Emulation
    mode: LDA #hi, PHA, LDA #lo, PHA, PHP, RTI, DEX, BNE (the return address is the DEX); native mode (8-bit registers, the code is in bank 2): PHK, PEA ret, PHP, RTI, DEX, BNE. The branch
    stays on one page (a page crossing costs a cycle in emulation mode). Emitted behind a BRA; returns the subroutine's address."""
    t.br(0x80, "ro" + tag)
    t.label("rs" + tag)
    t.sta_a(0x0B00)                                      # mark 0: the start of the loop
    t.label("rl" + tag)
    if native:
        ret = t.pc() + 1 + 3 + 1 + 1                     # PHK, PEA (3 bytes), PHP, RTI: the DEX follows
        t.e(0x4B)                                        # PHK
        t.e(0xF4, ret & 255, ret >> 8)                   # PEA ret
    else:
        ret = t.pc() + 2 + 1 + 2 + 1 + 1 + 1             # LDA #, PHA, LDA #, PHA, PHP, RTI: the DEX follows
        t.lda_i(ret >> 8); t.e(0x48)                     # LDA #hi, PHA
        t.lda_i(ret & 255); t.e(0x48)                    # LDA #lo, PHA
    t.e(0x08)                                            # PHP
    t.e(0x40)                                            # RTI
    assert t.pc() == ret
    t.e(0xCA)                                            # DEX
    t.br(0xD0, "rl" + tag)                               # BNE
    assert native or (t.lab["rl" + tag] >> 8) == (t.pc() >> 8), "the branch crosses a page"
    t.sta_a(0x0B01)                                      # mark 1: the end of the loop
    t.e(0x60)                                            # RTS
    t.label("ro" + tag)
    return t.lab["rs" + tag]


def rti_phase(t, native):
    """the subroutine called three times: a warm-up with the long loop (the code and the stack are in the cache, as the second pass of run_emu.sh), then the long and the short loop, which
    the check compares (marks 0 / 1: three pairs per mode); software 1 MHz around them (native mode: hwenable is on), 20 MHz again after"""
    t.e(0x78)                                            # SEI
    t.sta_a(0xD07A)
    sub = rti_loop(t, "n" if native else "e", native)
    for n in (RTI_N[0], RTI_N[0], RTI_N[1]):
        t.ldx_i(n)
        t.e(0x20, sub & 255, sub >> 8)                   # JSR
    t.sta_a(0xD07B)


def handlers(t):
    """the handlers live in the tail (bank 0): an interrupt sets PBR = 0. Each counts, acknowledges its source and returns."""
    for name, ctr, ack in (("hei", CNT["ei"], 0xDC0D), ("hen", CNT["en"], 0xDD0D), ("hni", CNT["ni"], 0xDC0D), ("hnn", CNT["nn"], 0xDD0D),
                           ("hbrk", CNT["brk"], 0), ("hcop", CNT["cop"], 0), ("hsv", CNT["sv"], 0xDC0D)):
        t.label(name)
        t.e(0xE2, 0x20)                                  # SEP #$20: the counter is a byte in both modes
        t.e(0x48)                                        # PHA: the interrupted wait loop polls through A, the handler must not change it
        if name in ("hei", "hen", "hsv"):                # emulation mode: the P byte the interrupt pushed has B (bit 4) = 0; every handler ORs its bit 4 into BFLAG (expected 0 at the end)
            t.e(0xDA, 0xBA, 0xBD, 0x03, 0x01)            # PHX, TSX, LDA $0103,X (P: above A, X and the return address)
            t.and_i(0x10); t.e(0x0D, BFLAG & 255, BFLAG >> 8); t.sta_a(BFLAG)    # ORA BFLAG, STA BFLAG
            t.e(0xFA)                                    # PLX
        t.inc_a(ctr)
        if ack:
            t.lda_a(ack)
        if name in ("hei", "hni", "hsv"):
            t.lda_i(0xFF); t.sta_a(0xD019)               # the VIC's flag too (phase 2)
        t.e(0x68)                                        # PLA
        t.e(0x40)                                        # RTI


def tail(pre=None):
    """pre: the labels of the first pass (the handlers' addresses that the program writes into $01:FFFE); the code's size does not depend on them"""
    pre = pre or {}
    hsv = pre.get("hsv", 0)
    t = Asm(ORG)
    t.e(0x78, 0xD8)                                      # SEI, CLD
    t.sta_a(0xD07E)                                      # hwenable
    t.sta_a(0xD0B6)                                      # boot map off
    t.sta_a(0xD07B)                                      # 20 MHz
    t.lda_i(0x36); t.sta_a(0x0001)                       # CPU port: KERNAL visible (class KS with hwenable, KT without)
    t.lda_i(0)
    for a in range(0x0310, 0x0340):
        t.sta_a(a)
    t.sta_a(0xFFFE); t.sta_a(0xFFFF)                     # the bank-0 SRAM vector is 0: only a rerouted fetch can reach a handler
    cia_irq(t, "0", CNT["ei"], 0)
    if os.environ.get("IRQ_STOP") == "0":                # debugging only: stop after phase 0
        t.label("dbg"); t.br(0x80, "dbg")
    cia_irq(t, "1", CNT["en"], 1, nmi=True)
    vic_irq(t, "2", CNT["ei"], 2)
    # phase 3: hwenable off, the E-mode IRQ vector comes from the SRAM of bank 1 (reroute needs hwenable in emulation mode)
    t.lda_i(hsv & 255)
    t.sta_l(0x01FFFE)
    t.lda_i(hsv >> 8)
    t.sta_l(0x01FFFF)
    t.sta_a(0xD07F)                                      # hwenable off
    cia_irq(t, "3", CNT["sv"], 3)
    # T241, phases 7-9 (hwenable still off): the VIC acknowledged by a read-modify-write instruction, then one on RAM
    vic_rmw(t, "7", pre.get("hasl", 0), 7, True)
    vic_rmw(t, "8", pre.get("hinc", 0), 8, True)
    vic_rmw(t, "9", pre.get("hasl", 0), 9, False)
    t.lda_i(0x41); t.sta_a(0x0B02); t.e(0x0E, 0x02, 0x0B)          # LDA #$41, STA $0B02, ASL $0B02 (20 MHz): three writes to $0B02, $82 at the end
    # T241 S2 (hwenable still off, emulation mode): an IRQ that rises in the high half of an instruction's last cycle (phase 10), then RTI's cycles (phase 11)
    vic_last(t, "10", pre.get("hlast", 0), 10)
    rti_phase(t, False)
    t.sta_a(0xD07E)                                      # hwenable on
    # to native mode
    t.e(0x18, 0xFB, 0xC2, 0x30)                          # CLC, XCE, REP #$30
    t.e(0x5C, 0x00, 0x00, 0x02)                          # JML $020000
    handlers(t)
    rmw_handlers(t)
    last_handler(t)
    return t.done(), t.lab


def native(hsv_unused=0):
    n = Asm(0x0000)
    n.e(0xE2, 0x30)                                      # SEP #$30: the phases use 8-bit immediates
    cia_irq(n, "4", CNT["ni"], 4)
    cia_irq(n, "5", CNT["nn"], 5, nmi=True)
    # 6: ten RESTORE pulses (a write to $0B08 makes the bench pulse ext_restore), 2,000 PHI2 cycles apart
    stop_all(n)
    n.lda_i(0); n.sta_a(CNT["nn"])
    n.ldy_i(10)
    n.label("rl")
    n.sta_a(0x0B08)
    wait(n, 0xDC00, 0xD0, 0x07, 1, "6")
    n.e(0x88); n.br(0xD0, "rl")
    n.lda_a(CNT["nn"]); n.sta_a(CNT["rst"])
    # BRK x3, COP x2
    n.lda_i(0); n.sta_a(CNT["brk"]); n.sta_a(CNT["cop"])
    for _ in range(3):
        n.e(0x00, 0x00)
    for _ in range(2):
        n.e(0x02, 0x00)
    # T241 S2 phase 12: RTI in native mode (8-bit registers, hwenable on: $D07A / $D07B act)
    rti_phase(n, True)
    # S3 block, 16-bit registers
    n.e(0xC2, 0x30)                                      # REP #$30
    n.e(0x18, 0xA9, 0xFF, 0x7F, 0x69, 0x03, 0x00); n.sta_l(0x030000)                       # CLC LDA #$7FFF ADC #3 STA $030000
    n.e(0x38, 0xA9, 0x00, 0x10, 0xE9, 0x01, 0x00); n.sta_l(0x030002)                       # SEC LDA #$1000 SBC #1
    n.e(0xA2, DATA & 255, DATA >> 8, 0xA0, 0x10, 0x00, 0xA9, 0x0F, 0x00, 0x54, 0x03, 0x02)  # LDX #$0600 LDY #$0010 LDA #15 MVN $02 -> $03 (operands dest, src)
    n.e(0xE2, 0x20, 0xA9, 0x00, 0x48, 0xAB, 0xC2, 0x20)                                    # SEP #$20 LDA #0 PHA PLB REP #$20: DBR = 0 again
    n.e(0xA2, 0xFF, 0x0F, 0x9A)                                                            # LDX #$0FFF TXS: the stack leaves page 1
    n.e(0xA9, 0xEF, 0xBE, 0x48, 0xA9, 0x00, 0x00, 0x68); n.sta_l(0x030004)                 # LDA #$BEEF PHA LDA #0 PLA STA $030004
    n.e(0xA9, 0x34, 0x12, 0x48, 0x3B); n.sta_l(0x030006); n.e(0x68)                         # LDA #$1234 PHA TSC STA $030006 PLA
    n.e(0xA9, DATA & 255, DATA >> 8, 0x85, 0xE0, 0xE2, 0x20, 0xA9, 0x02, 0x85, 0xE2, 0xC2, 0x20)   # dp $E0 = $02:0600
    n.e(0xA0, 0x04, 0x00, 0xB7, 0xE0); n.sta_l(0x030008)                                   # LDY #4 LDA [$E0],Y STA $030008
    n.e(0xA9, 0x10, 0x00, 0x85, 0xE0, 0xE2, 0x20, 0xA9, 0x03, 0x85, 0xE2, 0xC2, 0x20)       # dp $E0 = $03:0010 (the MVN copy)
    n.e(0xA0, 0x06, 0x00, 0xB7, 0xE0); n.sta_l(0x03000A)                                   # LDY #6 LDA [$E0],Y STA $03000A
    n.e(0xA9, 0xCD, 0xAB, 0xEB); n.sta_l(0x03000C)                                         # LDA #$ABCD XBA STA $03000C
    n.e(0xE2, 0x20, 0xA9, 0xFE, 0x18, 0x69, 0x03); n.sta_l(0x03000E); n.e(0xC2, 0x20)       # SEP #$20 LDA #$FE CLC ADC #3 STA $03000E (8 bit) REP #$20
    n.e(0xA2, 0x00, 0x00, 0xA9, 0x00, 0x00)                                                # LDX #0 LDA #0
    n.label("sum"); n.e(0x18, 0x7F, DATA & 255, DATA >> 8, 0x02, 0xE8, 0xE8, 0xE0, 0x10, 0x00); n.br(0xD0, "sum")   # CLC ADC $020600,X INX INX CPX #16 BNE
    n.sta_l(0x030010)
    n.e(0xA9, 0xFF, 0x00)                                                                  # LDA #$00FF
    n.e(0x22, 0, 0, 0x02)                                                                  # JSL sub (patched)
    sub_at = len(n.b) - 3
    n.sta_l(0x030012)
    n.e(0xA2, 0x02, 0x00, 0xA9, 0xAA, 0x55, 0x9F, 0x20, 0x00, 0x03)                        # LDX #2 LDA #$55AA STA $030020,X
    # T241: native mode keeps the internal modify cycle: SEP #$20 LDA #$7F STA $0B03 INC $0B03 (one write, DBR = 0) LDA $0B03 STA $030030
    n.e(0xE2, 0x20, 0xA9, 0x7F); n.sta_a(0x0B03); n.inc_a(0x0B03); n.lda_a(0x0B03); n.sta_l(0x030030)
    n.e(0xE2, 0x20, 0xA9, 0x5A); n.sta_l(0x0300FF)                                         # SEP #$20 LDA #$5A STA $0300FF: the end marker
    n.label("end"); n.br(0x80, "end")
    n.label("sub"); n.e(0x1A, 0x6B)                                                        # INC A RTL
    n.b[sub_at] = n.lab["sub"] & 255
    n.b[sub_at + 1] = n.lab["sub"] >> 8
    return n.done(), n.lab


def data_bytes():
    return [(i * 37 + 11) & 0xFF for i in range(16)]


def s3_model():
    """the S3 result block, by hand from the instructions above: address -> byte (bank 3 offsets)"""
    d = data_bytes()
    r = {}

    def w16(a, v):
        r[a], r[a + 1] = v & 255, (v >> 8) & 255

    w16(0x0000, (0x7FFF + 3) & 0xFFFF)
    w16(0x0002, (0x1000 - 1) & 0xFFFF)
    for i in range(16):
        r[0x0010 + i] = d[i]                              # MVN
    w16(0x0004, 0xBEEF)                                   # PHA / PLA
    w16(0x0006, 0x0FFF - 2)                               # TSC after PHA: S = $0FFF - 2
    w16(0x0008, d[4] | d[5] << 8)                         # [dp],Y in bank 2
    w16(0x000A, d[6] | d[7] << 8)                         # [dp],Y in bank 3 (the copy)
    w16(0x000C, 0xCDAB)                                   # XBA
    r[0x000E] = (0xFE + 3) & 0xFF                         # 8-bit ADC
    w16(0x0010, sum(d[2 * i] | d[2 * i + 1] << 8 for i in range(8)) & 0xFFFF)   # ADC long,X over 8 words
    w16(0x0012, 0x00FF + 1)                               # JSL / INC A / RTL
    w16(0x0022, 0x55AA)                                   # STA long,X
    r[0x0030] = 0x7F + 1                                  # T241: the native 8-bit INC $0B03
    r[0x00FF] = 0x5A
    return r


def image():
    f = bytearray(0x20000)
    t, lab = tail()
    t, lab = tail(lab)                                   # second pass: the handlers' addresses are known
    n, nlab = native()
    assert len(t) <= 0x800, len(t)
    assert len(n) <= DATA, len(n)
    f[0x8100:0x8100 + len(t)] = t
    f[NAT:NAT + len(n)] = n
    f[NAT + DATA:NAT + DATA + 16] = bytes(data_bytes())
    a = Asm(0x8000)
    a.e(0x78, 0xD8); a.ldx_i(0xFF); a.e(0x9A)            # SEI, CLD, TXS
    a.ldx_i(0)
    a.label("cp")
    for k in range(8):
        a.e(0xBD, 0x00, 0x81 + k); a.e(0x9D, 0x00, (ORG >> 8) + k)   # tail: LDA $8100+k*256,X / STA $1000+k*256,X
    for k in range(8):
        a.e(0xBD, 0x00, (NAT >> 8) + k); a.e(0x9F, 0x00, k, 0x02)    # native code: LDA $9000+k*256,X / STA $02:k*256,X
    a.e(0xE8); a.br(0xF0, "dn"); a.jmp(a.lab["cp"])
    a.label("dn"); a.jmp(ORG)
    head = a.done()
    f[0x8000:0x8000 + len(head)] = head
    vec = {0xFFE4: lab["hcop"], 0xFFE6: lab["hbrk"], 0xFFEA: lab["hnn"], 0xFFEE: lab["hni"], 0xFFF4: lab["hcop"], 0xFFFA: lab["hen"], 0xFFFC: 0x8000, 0xFFFE: lab["hei"]}
    for base in (0x0000, 0x10000):                       # the image is 128 KiB, the second copy is where bank $FF reads
        for a_, v in vec.items():
            f[base + a_] = v & 255
            f[base + a_ + 1] = v >> 8
    return f, lab


def main():
    out = sys.argv[1]
    f, lab = image()
    os.makedirs(os.path.join(out, "mem"), exist_ok=True)
    with open(os.path.join(out, "mem", "scpu_rom.mem"), "w") as fh:
        fh.write("\n".join("%02x" % x for x in f) + "\n")
    with open(os.path.join(out, "mem", "c64scpu_rom.mem"), "w") as fh:
        fh.write("0000\n" * 0x2800)
    exp = [(VERD + i, 0xAA) for i in range(11) if i != 6]
    exp += [(CNT["rst"], 10), (CNT["brk"], 3), (CNT["cop"], 2), (BFLAG, 0), (0x0B02, 0x41 << 1)]
    m = s3_model()
    dexp = [(0x02000000 + 0x030000 + a, v) for a, v in sorted(m.items())]
    with open(os.path.join(out, "expect.json"), "w") as fh:
        json.dump({"probes": len(exp), "dprobes": len(dexp), "raw": [hex(RAW + i) for i in RAW_SLOTS], "marks": MARKS,
                   "rti": {"n": list(RTI_N), "emu": RTI_EMU, "nat": RTI_NAT}}, fh)
    print("+EXPECT=" + ",".join("%04x:%02x" % (a, v) for a, v in exp) + " +DEXPECT=" + ",".join("%08x:%02x" % (a, v) for a, v in dexp))


main()
