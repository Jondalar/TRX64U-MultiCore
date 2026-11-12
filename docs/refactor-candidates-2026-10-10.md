# LUT refactoring candidates (2026-10-10)

Analysis only, nothing built. Alex 2026-10-10: after M1 ("Erstmal schauen wir dass wir die Tests gleich zu Ende bringen und dann M1 fertig bekommen für die Tester."). Base: CI run 634, integ-m1 4984745, package integ-m1-49847451:
26,594 / 32,600 LUT (81.6 %), slices 8,142 / 8,150, BRAM 68.5 / 75 tiles (91 %, the next scarce
resource), DSP 21 / 120, CARRY4 1,410, WNS +0.183 ns (critical: 1541 6502N to drive RAM at 100 MHz;
turbo `a_mux_i_reg` to c64_mem on clk2x). Asked for by Alex: "Wir müssen unbedingt nochmal nach
Refactoring-Potenzialen suchen."

M = measured in the run-634 reports, E = estimate. Vivado books merged logic to one module, so
single hierarchy rows are only roughly right (e.g. `c64_eeprom_0` 447 LUT holds cart_slot's request
queue; rvlite `i_dmem` 1,063 LUT probably holds the app IO read mux).

| # | Candidate | LUT now (M) | Saving (E) | Risk | Effort | Build |
|---|---|---:|---|---|---|---|
| 0 | Honest numbers first: OOC synth per block with `-flatten_hierarchy none`, post-route LUTs grouped by driving module | – | 0 (settles 8-10) | none | S | CI only |
| 1 | Release variant (T46, ADR-0020) without jtag_dbg, con_ring, rv_dbg, debug read mux, dev registers in top.v | 845 | -850 to -950, -2,100 FF, -4 RAMB18 | low | S-M | release |
| 2 | Dev leftovers in both builds: `bk_mem` Bling edge recorder (1 RAMB36), usb_phy khz/edge counters, mdio/i2c override, eth counters | ~40-80 | -40 to -100, -1 RAMB36 | low | S | both |
| 3 | Vivado flow knobs (AreaOptimized_high, ExploreArea, shreg/control-set options), today all defaults | 26,594 | -130 to -800 | WNS | S (CI matrix, 2 seeds) | both |
| 4 | HDMI GTP: one TX reset FSM instead of four | 281 | -200 to -215 | medium (all System Modes) | M | both |
| 5 | HDMI packets: ACR as ROM for the 5 modes, no SPD frame, smaller packet picker | 557 | -120 to -250 | medium (sinks) | M | both |
| 6 | Wide slow counters into idle DSP48 (trx_engine, mode_ctl, top, case_leds) | ~300-500 bits | -150 to -350 | low-medium | M | both |
| 7 | Diagnostics in the core (slot_port counters, trx_dbg/trx_engine statistics, drive_unit dbg) | parts of 90/698/1,377 | -100 to -250 | low (JTAG scripts read some) | S-M | release or both |
| 8 | Cartridge address path / request queue narrower | 447 booked | -100 to -250 (weak) | medium (T16) | M | both |
| 9 | App IO read mux (booked to `i_dmem`) | 1,063 booked | 0 to -400 (weak) | low-medium | M | both |
| 10 | 6510 `proc_core` 1,248 vs the drive's identical 6502N 672 (nx_regs, sb_ld, data_early, turbo CE) | +576 | -80 to -250 | high (Timewarp snap, exactness) | L | both |
| 11 | Turbo (T29) cheaper structure; `fr_q` read mux duplicates the chips' muxes | 336 + glue | -30 to -80 | high (clk2x timing paths) | M | both |
| 12 | DDR2 client fabric (mig_arb, 128-bit client paths) | 338 + glue | -100 to -200 | medium (latency) | L | both |
| 13 | Audio chain: halfbands + fractional stage as one polyphase on DSP | 1,196 | -150 to -350 (low confidence) | medium (T25.3 quality) | L | both |
| 14 | flash_gate (T44/T50 safety) | 173 | -100 to -170 | Alex's decision, not a refactor | S | release |
| 15 | Gideon HDMI overlay once the LVGL UI owns the screen | 404, 3 RAMB36 | -350 to -400, -3 BRAM | none now | – | M2 |

Totals (E): release with 1, 2, 4-7 about -1,500 to -2,300 LUT (24,300-25,100, 74.5-77 %);
dev build (JTAG kept) with 2, 4-6 about -500 to -900; flow knobs on top, not reliably additive.

Top 3 plans:
- A (1, 2, 7): item 0, then a `DEV_DEBUG` generic in top.v (default 1) that generates the debug
  blocks out and ties their consumers to constants; `bk_mem` and the usb counters out of both builds;
  CI builds both variants, the release image boots once on the board. Open for Alex: which dev
  registers the t16/t20/t55 scripts still need.
- B (3): five CI variants x two seeds on the builder runner; accept a variant at -300 LUT or more with
  WNS no worse than baseline - 0.05 ns in both seeds. No RTL change.
- C (4, 5): one GTP reset FSM for four lanes (hand-wired wizard core) and ACR from a mode ROM; HDMI
  benches, then one board session: every System Mode, hot-plug, mode switch, two sinks.

Checked and not worth it: ring APPLY in software (Alex: hardware stays), reDIP waveform ROM, drive
line cache to BRAM, video delay chains, one packetizer for video and audio, 1541 on the RISC-V,
merging 2-FF synchronisers (FF is not the limit), CIA TOD (inherent), kawari (already minimal),
Vivado RQS_UTIL-206 (BRAM to LUTRAM goes the wrong way).

Reports: c64_basic.utilization_hier.rpt, utilization.rpt, timing.rpt, qor_suggestions.rpt,
design_analysis.rpt of package integ-m1-49847451.
