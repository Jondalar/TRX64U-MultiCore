#!/usr/bin/env python3
"""Q12 of docs/turbo-measurements-gideon.md (cost of an I/O access in turbo) on our core:

    TM_IP=<board> TM_PRG_LOCAL=tests/turbo-gideon/turbomeas.prg TM_HOLD_WAIT=5 tests/turbo-gideon/ours_io_cost.py

64 x LDA/STA abs to one address, x 256 x rep, timed in CIA 2 ticks, display off, $D031 bit 7 = 1.
PHI2 cycles per access at 64 MHz ($8F), 16 MHz ($89) and 1 MHz ($80); Gideon's numbers in data/io_cost.txt.
"""
import tm, tm2

ACC = [("RAM read $C0F0", tm2.LDA_ABS, 0xC0F0), ("RAM write $C0F0", tm2.STA_ABS, 0xC0F0),
       ("BASIC/KERNAL read $E000", tm2.LDA_ABS, 0xE000),
       ("CIA1 read $DC00", tm2.LDA_ABS, 0xDC00), ("CIA1 write $DC03", tm2.STA_ABS, 0xDC03),
       ("CIA2 read $DD00", tm2.LDA_ABS, 0xDD00), ("CIA2 write $DD03", tm2.STA_ABS, 0xDD03),
       ("SID read $D41B", tm2.LDA_ABS, 0xD41B), ("SID write $D418", tm2.STA_ABS, 0xD418),
       ("VIC read $D011", tm2.LDA_ABS, 0xD011), ("VIC read $D012", tm2.LDA_ABS, 0xD012),
       ("VIC read $D019", tm2.LDA_ABS, 0xD019), ("VIC read $D020", tm2.LDA_ABS, 0xD020),
       ("VIC write $D015", tm2.STA_ABS, 0xD015), ("VIC write $D018", tm2.STA_ABS, 0xD018),
       ("VIC write $D020", tm2.STA_ABS, 0xD020),
       ("Colour RAM read $D800", tm2.LDA_ABS, 0xD800), ("Colour RAM write $D800", tm2.STA_ABS, 0xD800),
       ("$D031 read", tm2.LDA_ABS, 0xD031),
       ("IO1 read $DE00", tm2.LDA_ABS, 0xDE00)]
tm.cfg("Turbo Control", "U64 Turbo Registers")
tm.run()
print(f"{'':28s} {'64 MHz':>8s} {'16 MHz':>8s} {'1 MHz':>8s}")
for name, op, addr in ACC:
    row = []
    for d031 in (0x8F, 0x89, 0x80):
        code, n = tm2.io_loop(op, addr, val=0x0F, per_block=64, rep=4 if d031 != 0x80 else 1)
        t = tm2.timed(code, d031=d031)
        row.append(t / n)
    print(f"{name:28s} {row[0]:8.4f} {row[1]:8.4f} {row[2]:8.4f}")
