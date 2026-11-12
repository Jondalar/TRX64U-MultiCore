# T138 — The A1200 has 32 MB Zorro III fast RAM

**Status:** PROPOSED (2026-10-28), building on branch `t136-a1200-colour`.
**Repo:** TRX64U-MultiCore — the A1200 large core (`cores/amiga1200/`), its benches, the core application (`coreapp/cores/amiga.cc`).
**Number:** T138.
**Origin:** Alex, 2026-10-28, at the board: the AGA demo "Deep Meet" (2024) runs on the Analogue Pocket's Amiga core, which "kommt aber
auch mit 32MB Fast Ram daher" ("comes with 32 MB fast RAM"); on ours AmigaOS sees 5.5 MiB fast at most (`avail`: fast maximum
5,767,168). Asked whether to spec and build Zorro III fast RAM: "Ja und dann bauen." ("Yes, and then build it.")
**Depends on:** T97 (A1200 core), T115 (large core), T136 (its frame buffers at DDR2 0x4000000-0x45FFFFF).
**Milestone:** M3.

---

## §1 The problem

- **P1 CPU side.** `cores/amiga1200/rtl/cpu_wrapper.v` (from Minimig/MiSTer via nanomig) has Zorro III autoconfig and the selects
  `sel_z3ram0/1`, but only under `ENABLE_RAM32`, which the build does not set (`cfg_z3 = 0`, line 542), and `ramaddr[28:24]` is tied
  to 0 (line 211): a Z3 address has no place to go. The fast RAM path (`fastram_addr[23:1]` out of nanomig, `p2_addr[21:0]` into
  `amiga_mem`) is 16 MB wide and window-relative.
- **P2 memory.** The core's 16 MB DDR2 window (0x1000000-0x1FFFFFF) is full (chip/fast/slow RAM, Kickstart, floppies, HDF staging).
  32 MB need DDR2 outside it.
- **P3 application.** "Fast RAM" offers None/2M/4M/5.5M; there is no choice for Zorro III.
- **P4 area and timing** of the A1200 build.

## §2 Scenarios

```gherkin
Scenario: S1 Zorro III RAM is configured and holds data
  Given the A1200 bench (tb_l2, TG68K) with FAST_CFG = Zorro III 32 MB and a test ROM that runs AutoConfig as the OS does
  When  the ROM assigns the Z3 board its base and writes, then reads a pattern at its first, middle and last longword
  Then  every word reads back, and the DDR2 model holds it at 0x6000000 + offset
  And   control: the Z3 card not offered (cfg_z3 forced 0) -> no board at $E80000 answers Z3, the bench fails

Scenario: S2 the old fast RAM settings are unchanged
  Given FAST_CFG 0..3
  Then  run_fastpat.sh passes as before (2 MB, 4 MB, 5.5 MB)

Scenario: S3 the application offers it
  Given the A1200 descriptor
  Then  "Fast RAM" lists "32M (Zorro III)" and writes its FAST_CFG value; the A500 does not list it (tests/coreapp)

Scenario: S4 the build fits
  Then  the A1200 mono build routes, WNS >= 0 (report line "WNS mono amiga1200")

Scenario: S5 board
  Given Fast RAM = 32M (Zorro III), reset
  Then  `avail` shows about 32 MB more fast memory, and "Deep Meet" runs
```

## §3 Decision

- **D1 Size 32 MB, one Zorro III board** (what the Pocket has). Autoconfig answers a Z3 memory board of 32 MB (extended size code),
  the OS assigns its base (normally $40000000).
- **D2 DDR2 0x6000000-0x7FFFFFF.** Outside the core window; the upper 64 MB are only spoken for in the TRX64 large core (Alex,
  2026-10-28: "der DDR2 Bereich für den RING ist *NUR Für den TRX64 large CORE!* .. also total unkritisch"). Not 0x4800000-0x5FFFFFF:
  the host's still capture writes 0x5050000-0x506FFFF when its pixel input moves (`pix_layer.v`, default STILL_BASE), and the T136
  frame buffers sit at 0x4000000-0x45FFFFF.
- **D3 Setting.** FAST_CFG gets a fourth value for Zorro III 32 MB (the register has room, `cpu_wrapper.v` reads `fastramcfg_d[2]`);
  with it the Zorro II fast RAM is off (Z3 alone), the 2 MB chip and the slow RAM setting stay as chosen. Needs the 68020 (cpucfg).

## §4 Out

- More than 32 MB, a second Z3 board, RTG.
- The A500 core.

## §5 Build notes

- 2026-10-28, branch t136-a1200-colour (a13001741, a9e6d0710, 6b4f77ea3): Z3 board in the A1200 forks (cpu_wrapper, nanomig,
  amiga_core, new amiga_mem fork), FAST_CFG 4 = "32M (Zorro III)". S1 bench `amiga_z3`: "PASS amiga_z3 (32 MB Zorro III board
  configures, holds data at DDR2 0x6000000, all address bits count; three controls failed)"; S2 fastpat still passes; S3 coreapp
  tests + 3 controls. D3 corrected: the board is switched by FAST_CFG bit 2 alone (the A1200 app writes CPU_CFG 0; tg68k_020
  forces the 68020). S4: CI run 1027, A1200 mono WNS +0.107 ns, all constraints met. S5 (board: avail, Deep Meet) open.
