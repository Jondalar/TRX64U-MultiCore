# T257 — The C128's 80-column picture shows a whole frame of up to 240 lines without interlace, and the VDC's registers and RAM can be read back without side effects

**Status:** PROPOSED (2026-11-23, Alex: "C128: its own agent please, or several; it should do all of it, only not the black/white", translated).
Gateware and benches built on `t257-c128-vdc` the same day; the register list for the application comes from the TOML (D6, Alex 2026-11-23), built on `t257-regs-toml`. Since 2026-11-23 this work lives on the branch `c128-integ` together with T170 and T258 (Alex: not on integration), merged on top of `keycode-app-dir` (T239.3 D27). Nothing proven on the board yet.
**Repo:** TRX64U-MultiCore — the C128 large core (`cores/c128/`: `c128_pixw.v`, `c128_regs.v`, `c128_x.v`, `vdc/`, benches in `cores/c128/sim/`), `ci/sims.*`, `cores/c128/PROVENANCE.md`; the application only through D6.
**Number:** T257 (registry: `TODO.md`).
**Origin:** GitHub issue #122 "C128 80 VDC emulation issues" (reporter 0x444454: `rfo_vdc2.d64` shows broken effects against VICE, `mandelbr8-c128.prg` shows a blank screen in 80 columns, 40 columns is fine). The research of 2026-11-23 found no cause in our code, two truncations that can explain part of it, and no measurement to settle it. Alex, 2026-11-23 (translated): "C128: its own agent please, or several; it should do all of it, only not the black/white."
**Depends on:** T113 (the C128 core, D11 interlace), T194 (the 40-column picture), T192 D24 (no new per-core application code).
**Milestone:** M3.

Open (one line, as in the TODO table): Built (VDC frame, read-back, D6 list, D11 cuts); on c128-integ with T170, T258; needs pipeline and board (S7)

---

## §1 The problem

- **Who:** a tester who runs a VDC program in 80 columns on the C128 core; Alex, who has to tell from the board what such a program set up.
- **What they try:** `rfo_vdc2.d64` (Risen From Oblivion VDC V2) and `mandelbr8-c128.prg` (80 x 50 text and a 640 x 200 bitmap, R9 = 3 and R9 = 1) as on VICE.
- **Today:**
  - `layer2_c128.v:724` instantiates `c128_pixw #(.LINES(200), .LINES_IL(240))`. `c128_pixw.v` takes `lim_c = il_c ? LINES_IL : LINES` lines from a frame and reports the frame at line `lim_c - 1`. A frame without interlace with more than 200 lines (R6 x (R9 + 1) > 200, e.g. 30 rows of 8 lines = 240) loses everything below line 199, and that is what most VDC demos with their own geometry set.
  - `c128_x.v:59` drives `vd_rdata` with 0 and nothing reads a VDC register from outside: what the program set (R4-R9, R25 ...) cannot be seen on the board. The research could not decide from code whether RFO V2 exceeds 200 lines or 640 dots; only a read-back can.
  - A frame of 201..239 lines would, after a fix of the first point, still leave the rows below its last line to an older frame of the buffer ring.
- **Why it matters:** #122 stays open without a measurement and without a possible cause fixed; the same read-back serves every later VDC report.

Parts (MECE):
- **P1** A frame without interlace of 201..240 lines is written whole (source line y in rows 2y and 2y+1, up to y = 239).
- **P2** The application is told what the buffer holds: 480 rows of this picture, or 400.
- **P3** The VDC's registers R0-R37 (and status, selected register) and its VRAM can be read from the register page, with none of the read side effects (R16/R17 light pen flag, R31 pointer move).
- **P4** The person can see these registers on the board (the application).
- **P5** The core still builds: LUT, Slice, BRAM, timing; no regression of the pictures that work today (200-line frames, the interlaced pictures).
- **P6** The third-party code is credited.

## §2 Scenarios

```gherkin
Scenario: S1 a 240-line frame comes through whole
  Given the VDC without interlace shows 30 rows of 8 lines (R6 = 30, R9 = 7)
  When  the frame writer reports five frames
  Then  every one of the 480 rows of the buffer is the frame: source line y in rows 2y and 2y+1, y = 0..239, b2 of FB_LATEST = 1, no late line

Scenario: S2 a frame of 201..239 lines is completed
  Given the VDC without interlace shows 28 rows of 8 lines (224 lines)
  When  the frame writer reports five frames
  Then  rows 0..447 are the frame, rows 448..479 are black (not an older frame), b2 = 1, nothing outside the three buffers

Scenario: S3 what worked still works
  Given a frame of 200 lines without interlace, and the interlaced pictures of T113 D11
  When  the frame writer runs as before
  Then  b2 = 0 for the 200-line frame (the application shows 400 rows), nothing in rows 400..479, the woven pairs are as T113 S9 says (their b2, their rows)

Scenario: S4 the registers read back as the CPU reads them
  Given the VDC programmed with the register set of S3's frame
  When  the register page's 100 + n is read for n = 0-37 (not 16-19, 30, 31)
  Then  every byte equals what the CPU reads at $D601 with n selected; 126 equals the status read in bits 7 and 3-0, 127 is the selected register

Scenario: S5 the VRAM reads back and nothing moves
  Given four bytes written through R31 at $0100 and one at $8001
  When  the bytes are read at 140/141/142
  Then  they come in order, 140/141 count up, $8001 reads right (the 64 KiB shuffle), and R18/R19 and the next CPU read of R31 are exactly what they were

Scenario: S6 a read never hangs the bus
  Given the page's reads are held until the VDC answers
  When  the answer takes long or never comes
  Then  every access is acknowledged within 200 cpu clocks with a VDC, and with none the read ends in 0xFF after 1023 clocks with 143 b0 = 1, cleared by the next answered read

Scenario: S7 the person reads R4-R9 and R25 of a running program on the board
  Given the C128 core runs rfo_vdc2.d64 or mandelbr8-c128.prg in 80 columns
  When  the person asks the board for it (the application's report)
  Then  R0-R37 of the program are shown

Scenario: S8 the core builds
  Given the core as of this branch
  When  the pipeline builds large:c128
  Then  route complete, WNS >= 0, Slice LUTs below the last measured, Block RAM tiles not above 75 (the second port shares the 16 tiles), DSP unchanged

Scenario: S9 the symptom of #122
  Given the board with this build
  When  rfo_vdc2.d64 and mandelbr8-c128.prg run in 80 columns
  Then  a photo of each next to the register values of S7 shows whether the lines or dots were the cause

Scenario: S10 the credits are complete
  Given the tree
  When  tools/check_c128_tree.py runs
  Then  it is ok and PROVENANCE.md names every block taken from eilers/c128Mega65
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 P2 | 240 lines whole, b2 = 1 | `cores/c128/sim/tb_c128_pixw.sv` phase 1b (`run_tall(30, ...)`) via `run_pixw.sh` | `ci/sims.txt` tier 1 `c128_pixw` |
| S2 | P1 P2 | 224 lines, black rows after, b2 = 1 | the same bench, `run_tall(28, ...)` | `c128_pixw` |
| S3 | P5 | 200-line frames and the woven pictures unchanged | the same bench, phases 1, 2, 3 (their existing checks; `LINES(240)` in its instance) | `c128_pixw` |
| S4 | P3 | registers = the CPU's reads | `cores/c128/sim/tb_c128_vkpeek.sv` via `run_vkpeek.sh` | `ci/sims.txt` tier 1 `c128_vkpeek` |
| S5 | P3 | VRAM read-back, no side effect | the same bench | `c128_vkpeek` |
| S6 | P3 | no hang, timeout 0xFF | `tb_c128_vkpeek.sv` (the 200-clock bound) and `cores/c128/sim/tb_regs.v` (a dead model, the hold of the byte) via `run_regs.sh` | `c128_vkpeek`, `c128_regs` |
| S7 | P4 | the register view of a running program | board step, F8 -> Bug report or `GET /v1/report`; the list from the TOML (D6) | board session |
| S8 | P5 | the flat build | report lines of the pipeline's `large:c128` job (`=== ... synth`, `WNS mono c128`, `utilization_hier.rpt`) | CI `bitstream` |
| S9 | P1 | #122's two programs | by hand on the board, photos, with S7; the programs are the reporter's, not in the repository | board session |
| S10 | P6 | credits | `python3 tools/check_c128_tree.py` (bench `c128_scan`) | lint / `c128_scan` |

Negative controls: none (no flash, no persistence).

## §3 Decision

**D1 — A frame without interlace is taken up to 240 lines (P1).** `layer2_c128.v:724` becomes `LINES(240), LINES_IL(240), LINES_BASE(200)`. `c128_pixw.v` itself keeps its structure: `lim_c`, the window compare and the `row_n == lim_c - 1` trigger already follow the parameter; every line is still written twice (rows 2y, 2y+1), so rows 0..479 are used and the 640 x 480 buffer (614,400 bytes, `BUFWORDS = 480 x 80`, stride 1 MiB) that T113 D11 made for the woven picture is exactly enough: no buffer, no address, no DDR2 window changes. A frame of more than 240 lines is cut at 240 (open, Q2).

**D2 — A frame of 201..239 lines is completed with black rows (P1, P2).** Without it the rows below the last line would show an older frame of the ring, and `b2` could not be set. New in `c128_pixw.v`: `last_row` (the row of the last line handed over), `fill_on / fill_done / fill_row`, a `ln_blank` flag with the line handoff and `u_blank` on the ui side. When the last real line is acknowledged and `!il_f && last_row >= LINES_BASE && last_row < LINES - 1`, the writer hands over one blank job per line `last_row + 1 .. LINES - 1` through the same toggle; the ui side writes zero words for such a job (the data register has a synchronous reset by `u_blank`, no 128-bit mux) to rows 2y and 2y+1 as for a line. Only then the frame is reported. A new frame ends a fill that is still running (`dotst && fs`), so a vertical blank shorter than the fill (about 40 lines x 160 words, about 100 us) loses that frame's report, never a following frame. A frame of up to 200 lines is not filled (as today).

**D3 — `latest` b2 means "all 480 rows are this picture" (P2).** It was only ever 1 for a woven pair. Now: woven pair as before (`fr_full & pf_full`), a frame without interlace: `last_row >= LINES_BASE`. The application already acts on b2 alone (`c128_rows`, `coreapp/cores/c128.cc`: 480 rows if b2, else 400); no application change; its comment is corrected. The pixel layer's picture entry is 640 x 480 (`c128.cc` layout), shown 2 x 2 and centred on 1920 x 1080 as for the interlaced picture; the T194 scaling (Integer, Fit, Fill) is the VIC's capture path (`pix_color/pix_x/pix_y`, 400 x 270 pixels) and not touched, the VDC picture is the layer's own 640 x 480 or 640 x 400 surface and takes the 480-row case through `Video::set_rows` as the woven picture does today (the open Fill question of T194 §5, "VDC Fill double picture", is the same for both heights).

**D4 — A second, read-only RAM port and the register mux in the VDC (P3).** Taken from the MEGA65 core (`vdc_top.sv:290-345`, `vdc_ramiface.sv:35-37,110-112`, `vdcram.v` second port; credited, D8): `vdc_top.sv` gets `dbg_addr[15:0]`, `dbg_ram[7:0]`, `dbg_reg[7:0]` and the function `regval()` (R0-R37 without side effects, 38 = status, 39 = selected register); `vdc_ramiface.sv` routes `dbg_addr` through the same `shuffleAddr` as the CPU's R18/R19 and into `vdcram.v`'s port B (`addr_b`, `dao_b`, read-first, every clock). Their `dbg_rd` is not taken. Port B is a read port of the same RAM and the 16 RAMB36 that the MEGA65 report measures (`vdcram` 16 BRAM) have two ports each, so the tile count is expected not to grow (to be seen in S8).

**D5 — The read-back crosses by a toggle handshake, held until the byte is back (P3).** `c128_x.v` (VDC_WHOLE) gets `vk_req[17:0]` = {request toggle, 1 = VRAM / 0 = register, address or register number} and `vk_ans[8:0]` = {answer toggle, byte}; the engine (three flip-flops of synchroniser, latch the address, one clock for the block RAM and the register mux, answer) is three states of `vk_st`. The ports go through `c64_top_c128.v` and `c64_core_c128.v` to `layer2_c128.v`, where they join `c128_regs.v` (the sim ports files are generated by `sim/mk_tb_ports.py`). In `c128_regs.v` (cpu_clk): a read of 100-13F or 142 puts the request out and holds its acknowledge until the answer toggle is seen (`vq_*`, the same shape as `c64u_io`'s far reads), 1023 cpu clocks at the most, then 0xFF and 143 b0 (a build without VDC_WHOLE has no answer and must not hang the bus). Registers: 100 + n = R n (n = 0-37), 126 status, 127 selected register, 128-13F read 0xFF; 140/141 the VRAM address (RW), 142 the VRAM byte at that address, then the address goes up by one; 143 the timeout flag. The old `vd_*` window ports (variant 2, unused in VDC_WHOLE) are left as they are. No snapshot of the whole register file crosses (320 bits and 3 stages of flip-flops would be about 960 FF); one byte at a time does.

**D6 — The register list is data in the core's TOML (P4, S7).** Alex, 2026-11-23, asked between a generic route (the description record of the .cor gets a register table that every core can use; no D24 exception) and about 8 code lines in `c128.cc` with a ledger exception, chose "Generic through TOML". The bug report's state block (`CoreDesc::report_regs`, T162 D3, `coreapp/src/corereport.cc`, F8 -> Bug report, `GET /v1/report`) reads the list from the record. The check in `coreapp/src/desc_check.cc:275` allows every register page of the 64 KiB window, not only page 0: the C128's page is `C128_PAGE = 0xE000` (`coreapp/cores/c128_regs.h:8`). `coreapp/cores/c128.cc` does not grow.

**D7 — Area and timing (P5).** Estimates by structure, not measured (the only numbers are the last reports): `c128_x` whole is 2,180 LUT / 3,220 FF / 16 BRAM (`mono/reports/b_r6/utilization_hier.rpt:307`), `vdcram` 4 LUT / 16 BRAM (`:311`).
- `regval` mux 40:1 x 8 bit: about 60-100 LUT, 8 FF; port B address shuffle and the output of the cascaded tiles: about 10-30 LUT.
- the engine in `c128_x`: about 30 FF (3 sync, 16 address, 2 state, 8 data, 3 flags), about 20 LUT.
- `c128_regs`: `vk_req` 18 FF, `pk_a` 16 FF, `vq_*` about 25 FF, the `+1` and the window decode about 50 LUT.
- `c128_pixw`: `last_row`, `fill_row` 20 FF, 4 flags, a 10-bit compare chain and the blank handoff: about 40 LUT, 35 FF; the zero write is a synchronous reset of an existing register.
- Total: about +200-250 LUT (0.7 % of 32,600), about +180 FF, +0 BRAM tiles (if Vivado keeps both ports on the 16 tiles), +0 DSP.
- Last measured: `b_r6` (2026-10-24) 28,451 Slice LUTs (87.27 %), 27,409 registers, WNS +0.039 ns; the T194 baseline (`c128-vicglitch`, T194 §5) 27,109 LUT (83.2 %), **Slice 8,122 of 8,150 (99.7 %)**, Block RAM 72.5 of 75 (RAMB36 60, RAMB18 25), WNS +0.228. The Slice figure is the one that binds; whether this change fits is decided by the next large:c128 job, not by LUT counts. The later builds of `main` are in the builder's job logs, which this session cannot read.
- Timing: the new paths are a toggle crossing (no arithmetic), a registered 40:1 mux in sys_clk (`dbg_reg`, the address is a register in front of it), a 10-bit compare/increment in the frame writer's existing `sys_clk` always block (row counters are 10 bit already), and the zero reset. No chain of the pixel path or of the CPU is touched.
- What T170 still needs (SRQ: 10-20 LUT, two pins of `mono/top.v`): LUT-wise it fits behind this change; a core at 99.7 % Slice is placement-limited, and T170's 20 LUT are smaller than this change's 250, so if the next run is close to full, which of the two goes in is a decision of Alex (Q3).

**D8 — Credits (P6).** `cores/c128/PROVENANCE.md` gets a section "Taken from eilers/c128Mega65" (GPL v3, Stefan Eilers and Jeff Flitton, `ef48a75`): `vdc_top.sv` ports + `regval()` + `dbg_reg` process, `vdc_ramiface.sv` ports + port-B connection, the second-port idea of `vdcram.v`; not taken: `vdc_peek.vhd`, the gated read, `dbg_rd`, the `clkdiv` move. The third-party headers of the VDC files are untouched.

**D9 — Benches.** `tb_c128_pixw.sv` instance `LINES(240) ... LINES_BASE(200)` plus phase 1b (`run_tall`); new `tb_c128_vkpeek.sv` / `run_vkpeek.sh` (the real `c128_regs.v` and the real `c128_x.v` joined as in `layer2_c128.v`, two clocks); `tb_regs.v` extended (a model of the engine, a dead model for the timeout, the byte held while the address stays); `ci/sims.txt|deps|lane` get `c128_vkpeek`; `tb_c128_inst.vh` / `tb_c128_ports.vh` regenerated by `mk_tb_ports.py`.

**D10 — The C128 makes room; T257 and T170 stay whole (P5; Q3; Alex, 2026-11-23).** The first build of this branch (pipeline run 45154622293, builder run 3445, eeaf7663) missed timing: WNS -0.224 ns, the better of two placements (ExtraTimingOpt -0.224, AltSpreadLogic_high -0.276), on a host path this change does not touch (`hout/mtim/yact0_reg` to the HDMI audio packet registers, `pix_mmcm` 150 MHz, 9 logic levels, route 56 %). The last green C128 build (mono-c128-39f2e9ea, 572ea62e, 2026-11-21) had +0.231 ns at nearly the same density: 26,679 LUT, Slice 8,103 of 8,150 (99.42 %). This build: 26,865 LUT (+186), 27,037 FF (+125), Slice 8,098 (99.36 %), 72 of 75 BRAM tiles; `l2` +189 LUT, of it `c64` +119 and `regs` +59 (the read-back of D4-D6), the 240 lines of D1-D2 nearly nothing. T170 adds about 380 LUT, 480 FF and 1 RAMB36 on top. Asked what gives way, Alex chose "Make room": the coordinator lists the big blocks of the C128 build that could be left out, Alex picks, and T257 and T170 are built whole. Lost: the debug parts out (the VDC read-back and the IEC trace later), and more placement runs with T257 whole.

**D11 — The cartridge emulation and the tape streamer leave the C128 build (P5; D10; Alex, 2026-11-23).** Asked what leaves the build (about 570 LUT are needed for T257 and T170, better 800 for a margin), from the REU (999 LUT, 195 FF), the cartridge models (755 LUT, 160 FF), the analog output (373 LUT, 170 FF, 8 RAMB18) and the datasette (309 LUT, 215 FF, 1 RAMB36), he chose "Cartridge models, Datasette". Built as: the emulated cartridge goes whole (`cart_slot_c64u` with its `c64_cartridge` model, 972 LUT, and its DDR2 fill `cart_fill_c64u`, 211 LUT), so .crt files no longer run on the C128; a real cartridge in the port (`slot_port_c64u`) stays. The .tap streamer `c64u_c2n_l2` goes (309 LUT, 215 FF, 1 RAMB36), so .tap files no longer play or record; the external datasette on the C2N port (T214) stays. Both by a define of the C128's own files; the C128's menu no longer offers what is gone. Lost: the REU and the analog output out instead.

## §4 Out

- The black-and-white NTSC A/V picture (#73): Alex, 2026-11-23: not now.
- The 7-pixel hscroll shift of the VDC's window at R25[3:0] = 0: no longer out; see §5 "Fault 1" (it was declined 2026-11-15 with the T113 hang, and the board shows its cost).
- Fast serial / SRQ with a real 1571 (the research's P-4.1): T170 P2, built separately and completely.
- The VDC's own `vdc_peek.vhd` and the MEGA65's REST window: ours is the register page.

## §5 Decisions taken while building

- **2026-11-23, what could not be run here.** No Vivado, no simulation (the brief). Checked: `verilator --lint-only` (width and implicit-net warnings on the files changed: clean for `c128_x` + `vdc/*`, `c128_regs`, both benches; `c128_pixw` carries the same integer-parameter width warnings as before), `yosys` read + `hierarchy -check` + `proc` + `check -assert` on `c128_pixw.v` and `c128_regs.v` (clean; yosys cannot read `vdc_top.sv` at all, with or without this change: `vdc_top.sv:59` "Invalid nesting of always blocks", the declaration inside `always`), `iverilog -g2012` compile of `tb_regs.v`. The benches have never run.
- **A comment found wrong and fixed** in the same change: `coreapp/cores/c128.cc` (`c128_rows`: b2 was "the picture is woven"), `layer2_c128.v:719` and the `c128_pixw.v` header ("rows 0..399 for a frame that is not interlaced").
- **vk ports instead of the `vd_*` window.** The `vd_*` ports have no room for the answer toggle; a 9-bit answer and an 18-bit request are new ports, four files' port lists and the generated tb files change (`c64_top_c128.v`, `c64_core_c128.v`, `c128_x.v`, `layer2_c128.v`).
- **D6 built (2026-11-23).** The description record is version 6 (`coreapp/src/descrec.h:59`, header 216): two words behind the restart (`DESCREC_HEADER_V5`: offset, count) point to a table of 12-byte entries (name string offset, reg, bytes 1/2/4), placed after the tables of the directory. A record without `[[report_reg]]` is written as before (version 4 or 5) and the reader still reads versions 2 to 6 (`descrec.cc` `ver >= 6`; `test_corereport.cc` reads a version 4 and a version 5 record: `nregs` 0, the C list stays). The reader refuses, with a sentence naming the register table or the entry: more than `DR_REGS` entries, a table outside the record or inside the header, an offset without entries, a name that is empty, outside the string table or longer than 60 bytes, a width other than 1, 2 or 4, `reg + bytes > 0x10000`. TOML: `[[report_reg]]` with `name`, `reg` (the byte offset from the core window), `bytes` (1, 2, 4; default 1), at most 64 entries (`tools/descrec.py` header comment, `_reg_entries`). Generic for every core; `tools/descrec.py` and `descrec.cc` check the same things.
- **How the lists combine.** The record's list replaces the descriptor's `report_regs` when the record has one (`descrec_apply`, `nregs > 0`), else the C list stays (`test_corereport.cc`: "the base is not touched", and the old records). No merge: a core has one list, in one place. A C descriptor that wants both writes its entries into the TOML.
- **Limit.** 38 one-byte entries (R0-R37) do not fit `REPORT_REGS_MAX` 24, so it is 64 (`desc.h`; `DR_REGS` is the same number). The cost is `ReportReg regs[64]` (16 bytes each, about 1 KB) in every `DescRecord`, which is made with `new` on the heap (`bundle.cc:56, 82`, `libmenu.cc:34`); the report's 40 C128 lines are about 2.4 KB; `test_corereport`'s worst case (all sections full, 25.5 KB of `CORE_REPORT_MAX` 28 KB) still leaves room for them, and an overfull report is cut with a marker, never overrun. The C128 declares R0-R37 plus the status (E126) and the selected register (E127), 40 entries, one byte each.
- **The 64 KiB window.** `desc_check.cc` allows `reg + bytes <= 0x10000` for a C descriptor's list too (`ReportReg::reg` is 16 bit, so the top is 0xFFFF). `test_corereport.cc` checks 0xE100, 0xFFFC + 4 (accepted) and 0xFFFD + 4 (refused).
- **Where the C128's list is and how it gets onto the board.** `cores/c128/app/desc.toml` (new): the C128 had no description record, its `.cor` carried none. The file lies in the core's app part, where t239.3 keeps a core's data (D17); T239.3 D27 (branch `keycode-app-dir`, merged first into `c128-integ`) keeps `cores/*/app/**` out of every bitstream key (`ci/lib.sh`, `ci/plan.py`, `tools/host_pin.sh`, `ci/test_layers.sh`), so renaming a register no longer rebuilds the core (the open question of the first version is closed). `tools/pack_large_cor.sh` takes `cores/<id>/app/desc.toml` as record 6 by itself, the shape of t239.3's packer; the `--desc` lines of `ci/package.sh` and `ci/cor.sh` are gone. Difference to t239.3: its `cor_pack.py` takes `--desc` repeated, this branch's takes one, so the packer refuses a caller's `--desc` together with an app/desc.toml (no core has both here; no `--ops`: the file holds no operation names). `bundle_merge` applies a record that has a register table (`bundle.cc`). `coreapp/cores/c128.cc` and the ledger are untouched; `tools/check_d24.py` is green.
- **Merge note (t239.3).** t239.3's description record has its own `[[report_reg]]` section (name, reg, bytes; 1..24 entries) in `cores/c128/app/desc.toml`. When t239.3 merges, the version-6 register table of this branch gives way to it: the entries of this file move unchanged into t239.3's `cores/c128/app/desc.toml`, t239.3's limit (1..24) rises to `REPORT_REGS_MAX` 64, and the repeatable `--desc` of t239.3's packer replaces this branch's single-file rule.
- **D11 built (2026-11-23).** Two defines of the C128's own files, both in `mono_env` of `cores/c128/mono/core.toml` (`TRX_DEFS`): `NO_CART_EMU` and `NO_TAP_STREAM`. Nothing under `cores/c64u/` is edited.
  - `NO_CART_EMU`: `c64_core_c128.v` drops the instance `cart` (`cart_slot_c64u` with `c64_cartridge`) and `layer2_c128.v` drops `cfill` (`cart_fill_c64u`); `cart_cfg` (the app's CART_TYPE != 0) is 0, so `flags[11]` and the port's `cart_int` follow the real port only (`s_present`). The app's CART_TYPE register still exists and is ignored.
  - `NO_TAP_STREAM`: `layer2_c128.v` drops `c2n` (`c64u_c2n_l2`) and the bus-event pulse that fed it (`ev_tgl`, `ev_s`); `core_caps` no longer claims `CAPAB_C2N_STREAMER` and `CAPAB_C2N_RECORDER`. The external datasette stays: `rd_line = pin_ctrl[4] ? rdp_s[1] : 1'b1`, `sense_low = pin_ctrl[4] && !snp_s[1]`, motor and write out as before.
  - Consumer table (every consumer read in the code; "without" is what the C128 build now gives):

    | signal | consumer (file:line) | with the block | without |
    |---|---|---|---|
    | `xc_exrom`, `xc_game` | `c64_core_c128.v` `.ext_cart_exrom/game` = `s_rom ? s_* : xc_*`; `c64_top_c128.v:1525-1526` -> PLA `cart_game/exrom` | cart's GAME/EXROM (high = released) | 1, 1: released; the real port's still win when `s_rom` |
    | `xc_dout` | `c64_core_c128.v` `.ext_cart_dout` = `port_rd ? s_dout : xc_dout`; `c64_top_c128.v:1527` -> `c64_bus_arbiter_c128.v:421,423,448` (ROML/ROMH when `cart_present`, IO1/IO2 select) | model data, else `last_bus` (the bus, one clock late) | `nc_bus`: the same register (`always @(posedge gclk) nc_bus <= xc_din`, 8 FF), so an open IO1/IO2 read is as before |
    | `xc_nmi`, `xc_irq` | `c64_core_c128.v` `.ext_cart_nmi/irq` ORed with the port's; `c64_top_c128.v:1872-1873` gated by `cart_present` | cart IRQ/NMI (active high) | 0 |
    | `xc_io2_we_valid` | `.ext_cart_io2_we_valid` ORed with `s_io2_ext` | model's | 0 |
    | `xc_wait` | `c64_core_c128.v` `.ext_cart_wait`; `c64_top_c128.v:724` `cpu_rdy` | RDY held for a cache miss | 0: never held |
    | `cart_active` | `layer2_c128.v:73` output, `c64u_io` | 1 while a cartridge runs | 0 |
    | `cart_req_*`, `cart_ack_tgl`, `cart_fill_*` | between `cart` and `cfill` only | line fetch / byte write requests | req 0, never made; ack and fill 0 |
    | `c_addr`, `c_cmd`, `c_en`, `c_wdf_*` (cfill's `app_*`) | `layer2_c128.v:63-72`; `mono/top.v:840,1587` `c_en = dc_ui ? 0 : l2_c_en`; `mono/host/ddr2_port.v:150-182` (`c_en` gates `v_ok`, `eng`, `go`; `cart` takes the command) | the cart fill's DDR2 commands, first priority | `c128_tie_ff` zero: `c_en` 0, the arbiter serves `e_*` (ROM and RAM line fetches, frame writer), video and the app's port as before; nobody else used the `c` port of this layer 2 |
    | `cart_evt_tgl`, `cart_evt_word`, `cart_ee_tgl`, `cart_ee_rdata` | `c64u_io` `.evt_tgl/.evt_word/.ee_tgl/.ee_rdata` (app-visible flash and EEPROM window) | model's | 0: the app sees no event and reads 0; its writes (`fl_*`, `inval_tgl`, `ee_*`, `rom_sel`) go to nobody |
    | `l_bank`, `c_pk_*` | declared only; no consumer | model's | 0 |
    | `c2n` `pb_rdata/pb_ack`, `rc_rdata/rc_ack`, `rc_irq` | `c64u_io` `.pb_*` `.rc_*`; `.level_irq({rc_irq,...})` | streamer's windows | 0 with ack 1 (as the existing no-`TAPE` branch), IRQ 0 |
    | `sense_pull` | `sense_low` -> `tsn_s` (CIA SENSE), `r_sn_o` -> `c2n_sense_o` | streamer pulls SENSE | 0: SENSE is the pin's only |
    | `c2n_rd_o`, `c2n_rd_en` | `rd_line` -> `trd_s` (CIA 1 FLAG), `c2n_read_o/en` | streamer drives FLAG and the READ pin | `rd_line` = the pin or idle high; `c2n_read_en` 0 |
    | `c2n_wr_o`, `c2n_wr_en` | no consumer (the WRITE pin is `r_wr_o` from `wr_s`) | streamer's | not used |
  - Removed, with the last measured cost (the report quoted in D10/D11): `cart` 972 LUT, `cfill` 211 LUT, `c2n` 309 LUT / 215 FF / 1 RAMB36. Added: `nc_bus` 8 FF. Estimated saving 1,492 LUT, about 207 FF net (cart and cfill FF not measured), 1 RAMB36; the cartridge cache block RAM inside `cart_slot_c64u` (not reported, D11) leaves too.
  - Copies (Alex's rule: a core reads no other core's file): `cores/c128/cart_slot_c64u.v`, `cart_fill_c64u.v`, `slot_port_c64u.v`, `c64_turbo_c64u.v`, each with a first line naming `cores/c64u/<file>` and 2026-11-23, content unchanged; read by `core_sources.tcl` and `sim/build_machine.sh` from there; `PROVENANCE.md` names them (`tools/check_c128_tree.py` green). The two cartridge files are read but unused while the define is set, so it stays switchable. Still read in place from `cores/c64u/`: `c64u_io.v`, `c64u_sys.v`, `dma_win_c64u.v`, `iec_bus_c64u.v`, `sid_sock_audio_c64u.v`, `trx_sid/`, `gideon/` (outside D11).
  - Application: the C128's descriptor (`coreapp/cores/c128.cc`) already has no `.crt`/`.tap` file type, no cartridge row and no tape task (`filetypes`, `caps` = iec, drive_a), so no row goes. The one wrong text was the datasette choice `Off (file only)`: now `Off` (`dset_names`), the other cores keep theirs; `tests/coreapp/golden/menu_c128.txt` follows. `tools/d24_ledger.txt` stays 1078 (only a string and comments changed). `test_keywalk` golden unchanged and green.
  - Checked: yosys (`read_verilog -sv`, `hierarchy -top layer2_c128`, `proc`, `check`) on layer 2, the core and the c64u files they read, with the mono defines plus both new ones: 0 `implicit`, no multiple drivers, the warnings are the same set as without the defines (missing VHDL/top modules) minus the cartridge model's; `iverilog -g2012 -tnull` of the same files: no error besides the modules it was not given. Unit tests `desc`, `c128`, `c128pic`, `keywalk` pass.
  - Benches: no C128 bench tests a cartridge or the streamer (`tb_c128.v` only ties the cart ports); the machine benches build without the two defines, so they exercise the old path. The new path is covered by the lint above and the board.
- **Not run here.** The unit tests were built and run on the Mac with the flags of `tests/coreapp/run.sh` (the three units named in the commit); `tests/coreapp/run.sh` as a whole, the RISC-V build, `ci/package.sh` and the board were not (the brief).

- **Fault 1 (board, c128.cor of run 45195870019 / 5b21c935): the first character column of the 80-column text is missing ("ersion:", "oading fonts").**
  - Cause: the T113 D1 window (`hviscol` opens on the dot where `vdc_video.sv` loads the first character) was never in this tree. It was built on branch `t113-vdc-ram` (2026-11-12, Alex: "yes, the column must be visible"), the branch was deleted with the T113 hang's "won't do" (2026-11-15, `486f4368e` changed only the spec text) and its code lives only in `refs/backup/t113-vdc-ram` (`341a7e8e3`). `git log -S VDC_NO_HSS_OFFSET --all` finds the spec commit only. So `vdc_signals_h.sv` at 5b21c935 still opens the window at `col==8 && endCol` (the last dot of column 8): with R25[3:0] = 0 (what Busted Boulders programs: `L37BF` writes R25 = $80) the first character is loaded at pixel `ps` = 0 of column 8 and is cut off, 7 dots go. No T257, T170 or T258 change touches `vdc_signals_h.sv`; `core_sources.tcl` and the `host/` include shadowing play no part (the module is read once, from `cores/c128/vdc/`).
  - Fix: `vdc_signals_h.sv` / `vdc_signals.sv` / `vdc_top.sv` as in `341a7e8e3` (`reg_cdh`, `reg_hss`, `ps`, `vstart`; byte for byte the backup's content). `sim/run_vdc.sh` / `vdc_model.py`: tests 5-10 add R25[3:0] = 3, 1, 2, 4, 5, 6 to 0 (test 4) and 7 (test 2), and the control (`-DVDC_NO_HSS_OFFSET`, test 4 must differ) is back.
  - Bench (builder, `trx64-sim-env:4`): `run_vdc.sh` tests 2-10 "2 checks ok" each, i.e. 0 differing pixels against the model for R25[3:0] = 0..7; control "the mutant differs from the model (92284 pixels differ)"; `PASS run_vdc`. `run_pixw.sh` (T257 S1-S3 and T113 S9 with the change in): `PASS run_pixw`.
  - Cost (estimate as in T113 D1, not measured): +15 to +20 LUT (a 5-bit add/mux for `ps`, `ps < cth`, `pixel == ps`).
- **Fault 2 (the interlaced title picture of Busted Boulders is made of broken lines): cause NOT found, nothing changed (2026-11-23).** Facts read, none of them a fix:
  - `SPLASH` is 38,400 bytes = one progressive 640 x 480 bitmap, memory line y = bytes 80y..80y+79 (rendered from the file; lines 0..239 are the top half of the picture, not the even lines). The game's init (`bustedboulders` `$37BF`) programs R0 $7E, R1 $50, R2 $66, R3 $49, R4 $33, R5 $07, R6 $30, R7 $32, R8 $03, R9 $09, R12 $40, R13 0, R20 $10, R23 $09, R25 $80, R27 0, R28 $30; its loop at `$3B00` then writes R12 = $40 or $8B (= $4000 + 240 lines) on every vertical blank, chosen by a field parity it measured from the lengths of two vblanks (`$3A47`).
  - RTL with exactly these registers and the file in VRAM (bench harness only, not committed): static R12 gives field 0 = memory lines 0..239 and field 1 = 244 + y, so a weave of the two is the two halves of the picture interleaved (not the photo: the photo shows one title, not doubled), with R12 flipped per frame fields show 240 + y or a blank field. Neither reproduces the board photo, which looks like even and odd lines of the whole picture with one set shifted: the board and this sim disagree, so the register state on the board is not the one read from the code.
  - c128Mega65 (`ef48a75`): its `vdc_signals_v.sv` is the MiSTer file before our T113 D11 edit (`cfield` in the `fetchFrame` condition, no `field` output); its README lists "Some high-res interlace bitmap modes ... fields not correct" as open. Its only other differences in the VDC are a conditional subtract instead of `% reg_ht` for the field-1 vsync column and a `blank` input of `vdc_video.sv`; neither changes which memory line a field shows. Nothing was taken (PROVENANCE unchanged). Its weave is outside the VDC (`video_sync_c128.sv` detects the field from the vsync position) and has no memory-line logic to copy.
  - Next measurement (the T257 S7 path is built): run the title on the board, F8 -> Bug report, read R4-R9, R12/R13, R25, R27 and the status of the running title. That decides which rule (carry on, refetch at R12/R13, a skip per line) the picture needs.

## Questions only Alex can answer (one at a time)

- **Q1** answered by Alex on 2026-11-23: D6 (the register list from the TOML).
- **Q2** A frame of more than 240 lines without interlace (e.g. 256 lines, or dots beyond 640 in a line): cut as now, or is more wanted? (The brief says 240; the buffer holds 480 rows.)
- **Q3** Decided: D10.
