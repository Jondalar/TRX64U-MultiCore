# Lorenz suite on the core: history

Source: TODO.md section 3 (2026-10-01 and 2026-10-02, build m10); moved here 2026-10-03.
Result today: 297/297 on the board (TODO.md T10).

- Lorenz 6510/CIA suite on the core (`tests/run_lorenz.sh`), first run
  2026-10-01: about 265 of 296 pass. Real failures to look at:
  CPUPORT ($00/$01 of the 6510), TRAP10/14/15/16 (and their ANE/LXA
  variants), CIA timers (CIA1TA/TB, CIA2TA/TB), NMI and ICR01 ($DD0D),
  CIA2PB6/PB7 (timer output on PB6/7) and IMR (time out). IRQ, BEQR, ANEB
  and TRAP17* time out after 400 s (maybe only slow).
- 2026-10-02 (m10): all but TRAP16/17* pass. Those hang in the test, not
  the core: they skip ANE/LXA after `jsr ram`, so the next waitborder polls
  the RAM under $D011/$D012 with $01 = all RAM, and bytes left there by
  earlier programs (bit 7 clear, $D012 >= 30) keep it waiting for good.
  `tests/lorenz.py` now clears $D000-$DFFF RAM before each test (sim:
  `FILLRAM=seed` reproduces it, `KEEPIO=1` skips the clear).
- A rarer hang stays, also on a real C64: a CIA1 timer IRQ that comes due
  around the SEI case stays pending (the mask write does not drop /IRQ) and is
  taken in the next case through the RAM vector $FFFE/$FFFF, which holds the
  test's own opcode bytes; it depends on the start phase (rerun).
