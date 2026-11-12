#!/usr/bin/env python3
"""T85: a hand-assembled 68000 test ROM for the Amiga core benches (no Kickstart, nothing third party).
Writes a 512 KB image (hex words, one per line, for $readmemh) and prints the expected facts.
The program: switch the overlay off, build a Copper list and a bitplane in chip RAM, start the display
(1 bitplane, lores, COLOR00 dark blue, COLOR01 yellow, stripes), then count VBLANK interrupts' worth of
CIA timer ticks into chip RAM $0100 so the bench can see the CPU alive.   gen_testrom.py out.hex"""
import struct, sys
MODE = sys.argv[2] if len(sys.argv) > 2 else 'basic'

code = bytearray()
def w(x): code.extend(struct.pack(">H", x & 0xFFFF))
def l(x): code.extend(struct.pack(">I", x & 0xFFFFFFFF))
def move_w_abs(imm, addr): w(0x33FC); w(imm); l(addr)
def move_b_abs(imm, addr): w(0x13FC); w(imm & 0xFF); l(addr)
def move_l_abs(imm, addr): w(0x23FC); l(imm); l(addr)

CUSTOM = 0xDFF000
def reg(o): return CUSTOM + o

# vectors: SSP, PC (ROM at $F80000; overlay maps it to 0 at reset)
START = 0xF80008
head = struct.pack(">II", 0x00001000, START)
code.extend(head)

move_w_abs(0x7FFF, reg(0x09A))         # INTENA off
move_w_abs(0x7FFF, reg(0x096))         # DMACON off
move_b_abs(0x03, 0xBFE201)             # CIAA DDRA: OVL (bit 0) and LED output
move_b_abs(0x02, 0xBFE001)             # OVL low: chip RAM at 0
if MODE == 'slow':
    # slow RAM $C00000.. (512 KB): words, bytes, code run from it
    w(0x207C); l(0xC00000); w(0x303C); w(4095); w(0x7200)
    lp1 = len(code)
    w(0x30C1); w(0x5241); w(0x51C8); w((lp1 - len(code)) & 0xFFFF)          # move.w d1,(a0)+ ; addq.w #1,d1 ; dbra
    w(0x207C); l(0xC00000); w(0x303C); w(4095); w(0x7400)
    lp2 = len(code)
    w(0xD458); w(0x51C8); w((lp2 - len(code)) & 0xFFFF)                       # add.w (a0)+,d2 ; dbra
    w(0x33C2); l(0x104)                                                       # sum -> $104
    w(0x207C); l(0xC10000); w(0x303C); w(255); w(0x7200)
    lp3 = len(code)
    w(0x10C1); w(0x5201); w(0x51C8); w((lp3 - len(code)) & 0xFFFF)          # move.b d1,(a0)+ ; addq.b #1,d1 ; dbra
    w(0x207C); l(0xC20000)                                                    # code into slow RAM: move.w #$BEEF,$106 ; rts
    for x in (0x33FC, 0xBEEF, 0x0000, 0x0106, 0x4E75):
        w(0x30FC); w(x)                                                       # move.w #x,(a0)+ -- at $C20000
    w(0x4EB9); l(0xC20000)                                                    # jsr $C20000
    # a second routine at an odd-looking spot: copy the first 16 words of slow RAM to chip $3000 with long moves
    w(0x207C); l(0xC00000); w(0x227C); l(0x8000); w(0x303C); w(7)
    lp4 = len(code)
    w(0x22D8); w(0x51C8); w((lp4 - len(code)) & 0xFFFF)                       # move.l (a0)+,(a1)+ ; dbra
if MODE == 'cpu020':
    # 68020-only instructions; a 68000 takes the illegal-instruction exception (vector 4 -> $10) and the handler leaves $BAD0 in $208.
    # EXTB.L D0 ($49C0), MULU.L D1,D2 ($4C01 $2000, 32x32 -> low 32 bit), BFEXTU D0{24:8},D3 ($E9C0 $3608). Results: $200 $204 $20C, done flag $208.
    move_w_abs(0, 0x208)
    move_l_abs(0xF80000 + len(code) + 10 + 4, 0x10)    # vector 4 = the handler right behind the bra.w
    w(0x6000); w(12)                                 # bra.w over the handler (8 + 2 bytes)
    move_w_abs(0xBAD0, 0x208); w(0x60FE)             # handler: flag BAD0, bra.s *
    w(0x203C); l(0x00000080); w(0x49C0)              # move.l #$80,d0 ; extb.l d0   -> FFFFFF80
    w(0x23C0); l(0x200)
    w(0x223C); l(70000); w(0x243C); l(70000)         # d1 = d2 = 70000
    w(0x4C01); w(0x2000)                             # mulu.l d1,d2                 -> 4,900,000,000 mod 2^32 = $24101100
    w(0x23C2); l(0x204)
    w(0xE9C0); w(0x3608)        # bfextu d0{24:8},d3           -> $80
    w(0x23C3); l(0x20C)
    move_w_abs(0x020F, 0x208)                        # reached the end without an exception
if MODE == 'fast':
    # Zorro II autoconfig of the fast RAM (any write to $E80048 enables it), write 256 words, add them up into $104
    move_w_abs(0x2000, 0xE80048)
    w(0x207C); l(0x200000)             # movea.l #$200000,a0
    w(0x323C); w(0)                    # move.w #0,d1
    w(0x303C); w(255)                  # move.w #255,d0
    lpf = len(code)
    w(0x30C1)                          # move.w d1,(a0)+
    w(0x5241)                          # addq.w #1,d1
    w(0x51C8); w((lpf - len(code)) & 0xFFFF)
    w(0x207C); l(0x200000)
    w(0x303C); w(255)
    w(0x7400)                          # moveq #0,d2
    lpg = len(code)
    w(0xD458)                          # add.w (a0)+,d2
    w(0x51C8); w((lpg - len(code)) & 0xFFFF)
    w(0x33C2); l(0x104)                # move.w d2,$104
if MODE in ('fastpat', 'fastpatdma'):
    # T97 5.9: patterns through the fast RAM of the A1200 (68020 = TG68K, the direct RAM path, 5.5 MB = three Zorro II cards).
    # Results in chip RAM: $300 RAW errors, $304 verify errors, $308 mixed width errors, $30C code errors, $314 slow-window alias, $310 = $F457 when done.
    # d7 RAW, d6 verify, d5 mixed, d4 code, d3 alias.
    if MODE == 'fastpatdma':
        # the AmigaVision screen under the patterns: 8 bitplanes, hires, FMODE 3 (64 bit fetches), DDF $38..$D8, a copper list at $1800
        # (T97 5.7 measured these values); the DMA runs beside every CPU access to the fast RAM below
        cop = [(0x0100, 0x8211), (0x0102, 0), (0x0104, 0), (0x0108, 0), (0x010A, 0), (0x01FC, 3)]
        for k in range(8):
            pp = 0x10000 + k * 0x6000; cop += [(0x00E0 + 4 * k, pp >> 16), (0x00E2 + 4 * k, pp & 0xFFFF)]
        cop += [(0x008E, 0x2C81), (0x0090, 0x2CC1), (0x0092, 0x0038), (0x0094, 0x00D8), (0xFFFF, 0xFFFE)]
        a = 0x1800
        for r, v in cop:
            move_w_abs(r, a); move_w_abs(v, a + 2); a += 4
        move_l_abs(0x1800, reg(0x080)); move_w_abs(0, reg(0x088)); move_w_abs(0x8380, reg(0x096))
    for _ in range(3):
        move_w_abs(0x2000, 0xE80048)   # autoconfig: every write enables the next card (cfg 3: 4 MB, 1 MB, 0.5 MB)
        w(0x4E71); w(0x4E71)
    w(0x7E00); w(0x7C00); w(0x7A00); w(0x7800)                      # moveq #0 into d7 d6 d5 d4
    K = 0xA5C3F00F
    NSP = 1400; STR = 0x1004
    # T1: long at every 0x1004 bytes from $200000, read back at once (RAW) - the whole 5.5 MB is touched
    w(0x207C); l(0x200000); w(0x303C); w(NSP - 1)
    t1 = len(code)
    w(0x2208); w(0x0A81); l(K); w(0x2081)                           # move.l a0,d1 ; eori.l #K,d1 ; move.l d1,(a0)
    w(0xB290); w(0x6702); w(0x5287)                                  # cmp.l (a0),d1 ; beq.s +2 ; addq.l #1,d7
    w(0xD1FC); l(STR); w(0x51C8); w((t1 - len(code)) & 0xFFFF)      # adda.l #STR,a0 ; dbra d0,t1
    # T1b: verify the whole range after all of it was written (aliasing between the cards shows here)
    w(0x207C); l(0x200000); w(0x303C); w(NSP - 1)
    t1b = len(code)
    w(0x2208); w(0x0A81); l(K); w(0xB290); w(0x6702); w(0x5286)     # move.l a0,d1 ; eori.l #K,d1 ; cmp.l (a0),d1 ; beq.s +2 ; addq.l #1,d6
    w(0xD1FC); l(STR); w(0x51C8); w((t1b - len(code)) & 0xFFFF)
    # T2: mixed widths at three places: across the 4 MB / 1 MB card edge, at the end of the 0.5 MB card, in the middle of card 1
    def bsum(n): return sum(range(1, n + 1)) & 0xFFFFFFFF
    for base in (0x5FFFF0, 0x77FFE0, 0x300001):
        w(0x207C); l(base); w(0x7201); w(0x303C); w(31)             # movea.l #base,a0 ; moveq #1,d1 ; move.w #31,d0
        lb = len(code); w(0x10C1); w(0x5201); w(0x51C8); w((lb - len(code)) & 0xFFFF)   # move.b d1,(a0)+ ; addq.b #1,d1 ; dbra
        # read the bytes back as bytes and sum them: 1..32 = 528
        w(0x207C); l(base); w(0x7400); w(0x303C); w(31)
        lc = len(code); w(0x7600); w(0x1618); w(0xD483); w(0x51C8); w((lc - len(code)) & 0xFFFF)
        w(0x0C82); l(528); w(0x6702); w(0x5285)                      # cmp.l #528,d2 ; beq.s +2 ; addq.l #1,d5
        # words: write 16 words $1000+i, read the first long and the last long
        w(0x207C); l(base); w(0x323C); w(0x1000); w(0x303C); w(15)
        ld = len(code); w(0x30C1); w(0x5241); w(0x51C8); w((ld - len(code)) & 0xFFFF)  # move.w d1,(a0)+ ; addq.w #1,d1 ; dbra
        w(0x207C); l(base); w(0x2410); w(0x0C82); l(0x10001001); w(0x6702); w(0x5285)  # move.l (a0),d2 ; cmp.l #$10001001,d2
        if base & 1 == 0:
            w(0x2428); w(28); w(0x0C82); l(0x100E100F); w(0x6702); w(0x5285)         # move.l 28(a0),d2 ; cmp.l #$100E100F,d2
        # longs: write 8 longs $01010101 * i, sum the bytes: each long's bytes sum to 4*i, i = 1..8 -> 4*36 = 144
        w(0x207C); l(base); w(0x223C); l(0); w(0x303C); w(7)
        le = len(code); w(0x0681); l(0x01010101); w(0x20C1); w(0x51C8); w((le - len(code)) & 0xFFFF)  # addi.l #,d1 ; move.l d1,(a0)+ ; dbra
        w(0x207C); l(base); w(0x7400); w(0x303C); w(31)
        lf = len(code); w(0x7600); w(0x1618); w(0xD483); w(0x51C8); w((lf - len(code)) & 0xFFFF)
        w(0x0C82); l(144); w(0x6702); w(0x5285)
        # an odd-address long: write $DEADBEEF at base+2 ... (base+1 for the odd base) and read it back as bytes and as long
        w(0x207C); l(base + 2); w(0x223C); l(0xDEADBEEF); w(0x2081); w(0xB290); w(0x6702); w(0x5285)   # move.l d1,(a0) ; cmp.l (a0),d1
    # T3: code run from the fast RAM, then patched and run again (an instruction fetch from each card)
    for base in (0x200100, 0x400100, 0x77FF00):
        w(0x207C); l(base); w(0x20FC); l(0x203C1234); w(0x20BC); l(0x56784E75)     # movea.l #base,a0 ; move.l #..,(a0)+ ; move.l #..,(a0)
        w(0x4EB9); l(base); w(0x0C80); l(0x12345678); w(0x6702); w(0x5284)         # jsr base ; cmp.l #$12345678,d0 ; beq.s +2 ; addq.l #1,d4
        w(0x33FC); w(0x9ABC); l(base + 4)                                          # move.w #$9ABC,base+4  (the immediate's low word)
        w(0x4EB9); l(base); w(0x0C80); l(0x12349ABC); w(0x6702); w(0x5284)
    # T4: fast cards 2/3 ($600000..$77FFFF) against the slow RAM window ($C00000..$D7FFFF). The 5.5 MB are 4 MB + 1.5 MB "stolen
    # from the slow RAM"; with slow RAM also configured the chipset serves the same DDR2 bytes a second time at $C00000, so a
    # write there lands in the fast RAM. d3 counts fast words that changed under the write to the slow window.
    w(0x7600)                                                         # moveq #0,d3
    w(0x207C); l(0x600000); w(0x227C); l(0xC00000); w(0x303C); w(89)
    t4 = len(code)
    w(0x2208); w(0x0A81); l(0x3C96A5E1); w(0x2081)                    # move.l a0,d1 ; eori.l #K2,d1 ; move.l d1,(a0)
    w(0x4681); w(0x2281); w(0x4681)                                   # not.l d1 ; move.l d1,(a1) ; not.l d1
    w(0xB290); w(0x6702); w(0x5283)                                   # cmp.l (a0),d1 ; beq.s +2 ; addq.l #1,d3
    w(0xD1FC); l(0x4004); w(0xD3FC); l(0x4004); w(0x51C8); w((t4 - len(code)) & 0xFFFF)
    w(0x23C7); l(0x300); w(0x23C6); l(0x304); w(0x23C5); l(0x308); w(0x23C4); l(0x30C); w(0x23C3); l(0x314)
    move_w_abs(0xF457, 0x310)
    if MODE == 'fastpatdma':
        move_w_abs(0x7FFF, reg(0x096)); move_w_abs(0, reg(0x1FC))
if MODE == 'z3ram':
    # T138: the Zorro III board of 32 MB (A1200, FAST_CFG 4, 68020 = TG68K). AutoConfig as the OS does it: read the descriptor nibbles of the
    # board at $E80000 into chip RAM $320 (type), $322 (size), $324 (flags), $326 (product), then write the board its base ($40000000) to
    # register 0x44. Then long patterns at the first longword, at every power of two from +4 to +16 MB and at the last longword of the
    # 32 MB (an address bit that is lost shows as a second pattern on the first), mixed widths and code run from the board (the patterns come last).
    # Results in chip RAM: $300 RAW errors, $304 verify errors, $308 mixed width errors, $30C code errors, $310 = $F457 when done.
    def rd_to(src, dst):
        w(0x3039); l(src); w(0x33C0); l(dst)                         # move.w src,d0 ; move.w d0,dst
    rd_to(0xE80000, 0x320); rd_to(0xE80002, 0x322); rd_to(0xE80008, 0x324); rd_to(0xE80004, 0x326)
    move_w_abs(0x4000, 0xE80044)                                      # base $40000000: A31..A16 of the board
    w(0x4E71); w(0x4E71)
    w(0x7E00); w(0x7C00); w(0x7A00); w(0x7800)                        # moveq #0 into d7 d6 d5 d4
    Z3B = 0x40000000
    K = 0xA5C3F00F
    offs = [0] + [1 << k for k in range(2, 25)] + [0x1000004, 0x17FFFFC, 0x1FFFFFC]
    for base in (0x41FFFFE0, 0x407FFFF0, 0x40FFFFF1):                 # bytes, words, longs, odd long: the end of the board, across 8 MB and 16 MB
        w(0x207C); l(base); w(0x7201); w(0x303C); w(31)
        lb = len(code); w(0x10C1); w(0x5201); w(0x51C8); w((lb - len(code)) & 0xFFFF)
        w(0x207C); l(base); w(0x7400); w(0x303C); w(31)
        lc = len(code); w(0x7600); w(0x1618); w(0xD483); w(0x51C8); w((lc - len(code)) & 0xFFFF)
        w(0x0C82); l(528); w(0x6702); w(0x5285)
        w(0x207C); l(base); w(0x323C); w(0x1000); w(0x303C); w(15)
        ld = len(code); w(0x30C1); w(0x5241); w(0x51C8); w((ld - len(code)) & 0xFFFF)
        w(0x207C); l(base); w(0x2410); w(0x0C82); l(0x10001001); w(0x6702); w(0x5285)
        if base & 1 == 0:
            w(0x2428); w(28); w(0x0C82); l(0x100E100F); w(0x6702); w(0x5285)
        w(0x207C); l(base); w(0x223C); l(0); w(0x303C); w(7)
        le = len(code); w(0x0681); l(0x01010101); w(0x20C1); w(0x51C8); w((le - len(code)) & 0xFFFF)
        w(0x207C); l(base); w(0x7400); w(0x303C); w(31)
        lf = len(code); w(0x7600); w(0x1618); w(0xD483); w(0x51C8); w((lf - len(code)) & 0xFFFF)
        w(0x0C82); l(144); w(0x6702); w(0x5285)
        w(0x207C); l(base + 2); w(0x223C); l(0xDEADBEEF); w(0x2081); w(0xB290); w(0x6702); w(0x5285)
    for base in (0x40000100, 0x41FFFF00):                             # code run from the board, patched, run again
        w(0x207C); l(base); w(0x20FC); l(0x203C1234); w(0x20BC); l(0x56784E75)
        w(0x4EB9); l(base); w(0x0C80); l(0x12345678); w(0x6702); w(0x5284)
        w(0x33FC); w(0x9ABC); l(base + 4)
        w(0x4EB9); l(base); w(0x0C80); l(0x12349ABC); w(0x6702); w(0x5284)
    # the patterns last: the mixed widths above overwrite the middle and the end of the board, the DDR2 check needs the pattern there
    for o in offs:                                                    # write, read back at once (RAW)
        w(0x207C); l(Z3B + o)                                         # movea.l #a,a0
        w(0x2208); w(0x0A81); l(K); w(0x2081)                         # move.l a0,d1 ; eori.l #K,d1 ; move.l d1,(a0)
        w(0xB290); w(0x6702); w(0x5287)                               # cmp.l (a0),d1 ; beq.s +2 ; addq.l #1,d7
    for o in offs:                                                    # after all were written (aliasing shows here)
        w(0x207C); l(Z3B + o)
        w(0x2208); w(0x0A81); l(K); w(0xB290); w(0x6702); w(0x5286)  # ... addq.l #1,d6
    w(0x23C7); l(0x300); w(0x23C6); l(0x304); w(0x23C5); l(0x308); w(0x23C4); l(0x30C)
    move_w_abs(0xF457, 0x310)
if MODE == 'disk':
    # read track 0 of DF0 by disk DMA into $20000 (WORDSYNC on $4489), poll DSKBLK, set the flag $D15C at $106
    move_b_abs(0xFF, 0xBFD300)         # CIAB DDRB: all outputs
    move_b_abs(0xFF, 0xBFD100)         # nothing selected
    move_b_abs(0x7F, 0xBFD100)         # motor on
    move_b_abs(0x77, 0xBFD100)         # select DF0
    move_w_abs(0x4489, reg(0x07E))     # DSKSYNC
    move_w_abs(0x8400, reg(0x09E))     # ADKCON: WORDSYNC
    move_l_abs(0x20000, reg(0x020))    # DSKPT
    move_w_abs(0x8210, reg(0x096))     # DMACON: DMAEN | DSKEN
    move_w_abs(0x9900, reg(0x024))     # DSKLEN: DMA on, 0x1900 words
    move_w_abs(0x9900, reg(0x024))
    pl = len(code)
    w(0x3039); l(reg(0x01E))           # move.w INTREQR,d0
    w(0x0800); w(1)                    # btst #1,d0   (DSKBLK)
    w(0x67F4)                          # beq.s pl
    move_w_abs(0xD15C, 0x106)
if MODE == 'ide':
    # wait ~90 ms (the RDB is parsed through the mailbox meanwhile), then READ SECTORS cyl 0 head 0 sector 6 (= LBA 5) by PIO
    w(0x303C); w(0xFFFF)               # move.w #$FFFF,d0
    w(0x51C8); w(0xFFFE)               # dbra d0,*
    move_b_abs(0xA0, 0xDA0018)         # drive 0, head 0
    move_b_abs(0x00, 0xDA0010)         # cylinder low
    move_b_abs(0x00, 0xDA0014)         # cylinder high
    move_b_abs(0x06, 0xDA000C)         # sector 6
    move_b_abs(0x01, 0xDA0008)         # sector count 1
    move_b_abs(0x20, 0xDA001C)         # READ SECTORS
    w(0x1039); l(0xDA001C)             # move.b status,d0
    w(0x0800); w(3)                    # btst #3,d0   (DRQ)
    w(0x67F4)                          # beq.s back to the move.b (6 + 4 + 2 bytes)
    w(0x207C); l(0x200)                # movea.l #$200,a0
    for _ in range(4):
        w(0x30F9); l(0xDA0000)         # move.w $DA0000,(a0)+
    move_w_abs(0xD15D, 0x108)
# bitplane at $2000: 40 bytes x 256 lines of stripes
w(0x207C); l(0x2000)                   # movea.l #$2000,a0
w(0x303C); w(256 * 20 - 1)             # move.w #n,d0
loop = len(code)
w(0x30FC); w(0xF0F0)                   # move.w #$F0F0,(a0)+
w(0x51C8); w((loop - len(code)) & 0xFFFF)   # dbra d0,loop
# copper list at $1800 (words written by the CPU)
cop = [
    (0x0100, 0x4200 if MODE in ('stress', 'pause', 'load') else 0x1200),      # BPLCON0: 1 bitplane (stress: 4), colour on
    (0x0102, 0x0000), (0x0104, 0x0000),
    (0x0108, 0x0000), (0x010A, 0x0000),
    (0x00E0, 0x0000), (0x00E2, 0x2000),   # BPL1PT
] + ([(0x00E4, 0x0000), (0x00E6, 0x4000), (0x00E8, 0x0000), (0x00EA, 0x6000), (0x00EC, 0x0000), (0x00EE, 0x8000)] if MODE in ('stress', 'pause', 'load') else []) + [
    (0x008E, 0x2C81), (0x0090, 0x2CC1),   # DIWSTRT / DIWSTOP
    (0x0092, 0x0038), (0x0094, 0x00D0),   # DDFSTRT / DDFSTOP
    (0x0180, 0x0004), (0x0182, 0x0FF0),   # COLOR00, COLOR01
    (0xFFFF, 0xFFFE),
]
a = 0x1800
for r, v in cop:
    move_w_abs(r, a); move_w_abs(v, a + 2); a += 4
move_l_abs(0x1800, reg(0x080))         # COP1LC
move_w_abs(0, reg(0x088))              # COPJMP1
move_w_abs(0x8380, reg(0x096))         # DMACON: DMAEN | BPLEN | COPEN
if MODE == 'stress':
    # copy 8 KB of ROM into chip RAM at $10000, again and again: sequential reads and writes beside 4 planes of DMA
    top = len(code)
    w(0x207C); l(0xF80000)             # movea.l #$F80000,a0
    w(0x227C); l(0x10000)              # movea.l #$10000,a1
    w(0x303C); w(4095)                 # move.w #4095,d0
    lp = len(code)
    w(0x32D8)                          # move.w (a0)+,(a1)+
    w(0x51C8); w((lp - len(code)) & 0xFFFF)
    w(0x6000); w((top - len(code)) & 0xFFFF)
if MODE == 'load':
    # T85 rate bench: what Turrican 2 does to the memory path, scaled by LOADN. 4 planes of DMA, and a CPU that reads one
    # pseudo-random word of the 512 KB chip RAM (a miss almost every time: the cache is 4 KB) between LOADN sequential
    # ROM -> chip copy words. The board counted 4.6 % misses per read under Turrican 2 (stall clocks 14.6 M in 10 s).
    nload = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    nrand = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    w(0x7205)                          # moveq #5,d1 (seed)
    w(0x95CA)                          # suba.l a2,a2
    top = len(code)
    w(0x207C); l(0xF80000)             # movea.l #$F80000,a0
    w(0x227C); l(0x10000)              # movea.l #$10000,a1
    w(0x303C); w(4095)                 # move.w #4095,d0
    lp = len(code)
    for _ in range(nrand):
        w(0x3801); w(0xE54C); w(0xD244)    # move.w d1,d4 ; lsl.w #2,d4 ; add.w d4,d1   (d1 * 5)
        w(0x0641); w(13849)                # addi.w #13849,d1
        w(0x7400)                          # moveq #0,d2
        w(0x3401)                          # move.w d1,d2
        w(0xE78A)                          # lsl.l #3,d2
        w(0x3632); w(0x2800)               # move.w (a2,d2.l),d3
    for _ in range(nload):
        w(0x32D8)                      # move.w (a0)+,(a1)+
    w(0x51C8); w((lp - len(code)) & 0xFFFF)
    w(0x6000); w((top - len(code)) & 0xFFFF)
if MODE == 'pause':
    # Freeze bench: DMA of 4 planes beside a CPU that copies ROM -> chip, chip -> fast RAM, fast RAM -> chip (chip writes,
    # fast RAM writes and reads), and counts the rounds in $100. Needs FAST=1 (4 MB fast RAM) in the bench.
    move_w_abs(0x2000, 0xE80048)       # Zorro II autoconfig: any write to $E80048 enables the fast RAM
    top = len(code)
    for src, dst in ((0xF80000, 0x10000), (0x10000, 0x200000), (0x200000, 0x30000)):
        w(0x207C); l(src)              # movea.l #src,a0
        w(0x227C); l(dst)              # movea.l #dst,a1
        w(0x303C); w(4095)             # move.w #4095,d0
        lp = len(code)
        w(0x32D8)                      # move.w (a0)+,(a1)+
        w(0x51C8); w((lp - len(code)) & 0xFFFF)
    w(0x5279); l(0x0100)               # addq.w #1,$00000100
    w(0x6000); w((top - len(code)) & 0xFFFF)
if MODE == 'audio':
    # a constant sample played by audio channel 0 (left): the output must go away from zero
    move_w_abs(0x7F7F, 0x3000); move_w_abs(0x7F7F, 0x3002)
    move_l_abs(0x3000, reg(0x0A0))     # AUD0LC
    move_w_abs(2, reg(0x0A4))          # AUD0LEN (words)
    move_w_abs(200, reg(0x0A6))        # AUD0PER
    move_w_abs(64, reg(0x0A8))         # AUD0VOL
    move_w_abs(0x8201, reg(0x096))     # DMACON: DMAEN | AUD0EN
if MODE == 'key':
    # wait for a key from the keyboard (CIAA serial port flag), store the byte of the serial data register at $10C
    kp = len(code)
    w(0x1039); l(0xBFED01)             # move.b CIAA ICR,d0
    w(0x0800); w(3)                    # btst #3,d0   (SP)
    w(0x67F4)                          # beq.s kp
    w(0x1239); l(0xBFEC01)             # move.b CIAA SDR,d1
    w(0x13C1); l(0x10C)                # move.b d1,$10C
if MODE == 'dskchg':
    # T85 disk change: trackdisk's poll of DF0, forever. Select DF0, read /CHNG (CIAA PRA bit 2); when it is low pulse /STEP once (CIAB PRB bit 0)
    # and read it again; deselect; log the pair (before << 8 | after) at $400 + 2n, count the polls in $102, wait, again.
    move_b_abs(0xFF, 0xBFD300)         # CIAB DDRB: all outputs
    move_b_abs(0xFF, 0xBFD100)         # nothing selected, /STEP high, motor off
    w(0x247C); l(0x400)                # movea.l #$400,a2
    move_w_abs(0, 0x102)
    top = len(code)
    move_b_abs(0xF7, 0xBFD100)         # select DF0 (bit 3 low), motor off
    w(0x323C); w(60); w(0x51C9); w(0xFFFE)           # move.w #60,d1 ; dbra d1,*
    w(0x1039); l(0xBFE001)             # move.b CIAA PRA,d0
    w(0x0240); w(4); w(0xE448)         # andi.w #4,d0 ; lsr.w #2,d0
    w(0x3800); w(0x3A00)               # move.w d0,d4 ; move.w d0,d5
    w(0x4A44)                          # tst.w d4
    skip_at = len(code); w(0x6600)     # bne.s skip (patched below)
    move_b_abs(0xF6, 0xBFD100)         # /STEP low
    w(0x323C); w(30); w(0x51C9); w(0xFFFE)
    move_b_abs(0xF7, 0xBFD100)         # /STEP high: the rising edge steps
    w(0x323C); w(30); w(0x51C9); w(0xFFFE)
    w(0x1039); l(0xBFE001)             # move.b CIAA PRA,d0
    w(0x0240); w(4); w(0xE448)
    w(0x3A00)                          # move.w d0,d5
    code[skip_at + 1] = (len(code) - (skip_at + 2)) & 0xFF
    move_b_abs(0xFF, 0xBFD100)         # deselect
    w(0xE14C); w(0x8845)               # lsl.w #8,d4 ; or.w d5,d4
    w(0x34C4)                          # move.w d4,(a2)+
    w(0x5279); l(0x102)                # addq.w #1,$102
    w(0x323C); w(1500); w(0x51C9); w(0xFFFE)         # wait
    w(0x6000); w((top - len(code)) & 0xFFFF)         # bra.w top
# a counter in chip RAM that proves the CPU runs: $0100 += 1 forever (addq.w #1,abs.l)
w(0x33FC); w(0); l(0x0100)             # clr via move.w #0,$100
loop2 = len(code)
w(0x5279); l(0x0100)                   # addq.w #1,$00000100
w(0x6000); w((loop2 - len(code)) & 0xFFFF)   # bra.w loop2

rom = bytearray(524288)
rom[:len(code)] = code
# 512 KB image, the 256 KB part would be mirrored by the core; a 512 KB ROM is used as is
with open(sys.argv[1], "w") as f:
    for i in range(0, len(rom), 2):
        f.write("%04x\n" % ((rom[i] << 8) | rom[i + 1]))
print("code bytes", len(code), "start", hex(START))
