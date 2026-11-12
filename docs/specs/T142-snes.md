# T142 — The Super Nintendo runs as a large core: ROM from the SD card, picture on HDMI, sound on the speaker, saves on the SD card

**Status:** PROPOSED (2026-10-28); built and packed on branch t142-snes, board open (2026-10-28). Alex, 2026-10-28 (translated): "Then please let 2 agents build the two cores for the SNES and the Neo Geo in parallel, for real, so that they go onto the large-core list." Build started the same day; open questions are answered by the build's measurements and brought to Alex.
**Repo:** TRX64U-MultiCore — `cores/snes/` (RTL, glue, `mono/` flat build), `coreapp/cores/snes.cc` (descriptor, ROM and save handling), CI lane `large-snes`. Host untouched.
**Number:** T142 (as assigned by the owner; the registry in `TODO.md` has T141 as the last row seen on `main`, T143-T145 are the sister studies; check the table before the first commit).
**Origin:** Alex, 2026-10-28 (translated): "start 2 agents, they should port the SNES and the NeoGeo from MiSTer; the GBA and GBC would also be nice to look at, whether they would fit as large cores."
**Depends on:** T115 (large cores, `.cfw`/`.cor` delivery), T104 (generic core application), T118 (HDMI geometry per core), T136 (pixel layer copy per core), T95 (the NES core is the pattern for ROM in DDR2 behind a cache with the machine held on a miss).
**Milestone:** M3.

Legend used below: **[M]** measured (tool and file named), **[S]** read from source code, **[E]** estimate (basis named), **[?]** unknown. Comments in upstream files were not used as evidence.

---

## §1 The problem

Alex wants a Super Nintendo on the C64U as a **large core** (one flat bitstream, host and core together, started as a `.cfw`). The question this study answers: does the MiSTer SNES core fit the XC7A50T beside the host, can its memories be served from the 16-bit DDR2 behind the host's arbiter, and what has to be cut.

### What there is (all cloned 2026-10-28, shallow)

| Source | Commit / date | Licence (repo level, GitHub API) | What it is |
|---|---|---|---|
| `MiSTer-devel/SNES_MiSTer` | `bdcd33c5`, 2026-10-23 | GPL-3.0, `LICENSE` file present | srg320's cycle-accurate SNES in VHDL (CPU `CPU.vhd`, PPU `PPU.vhd`, S-DSP `DSP.vhd`, SMP `SMP.vhd`, 65C816, SPC700) + SystemVerilog top `SNES.sv` + all coprocessors |
| `nand2mario/snestang` | `5f0ef193`, 2026-09-28 | GPL-3.0 | the same design **translated from VHDL to Verilog/SystemVerilog by hand/tool** (`src/ppu.v`, `cpu.v`, `dsp.v`, `smp.v`, `65C816/`, `spc700/`) and fitted to Gowin boards: Tang Nano 20K, Primer 25K, Mega 60K/138K, Console 60K. Supports DSP-1/2/3/4, S-RTC, OBC-1; **no SA-1, no SuperFX in the shipped build** (`src/main.v:134-139` has `USE_GSU`, `USE_SA1` ... as parameters, default 0). |

### What the machine needs (S: read from `SNES.sv`, `rtl/SNES.vhd`, `snestang/doc/design.md` only where the code agrees)

| Memory | Size | Where MiSTer puts it | Access pattern |
|---|---|---|---|
| Cartridge ROM | up to 6 MiB (LoROM/HiROM; ExHiROM to 8 MiB address space) | SDRAM channel 0, `SNES.sv:762-768` | CPU/DMA, one byte per bus cycle, **6, 8 or 12 master clocks = 279 / 372 / 559 ns** (FastROM / slow / XSlow; 21.477 MHz master) |
| WRAM | 128 KiB | **SDRAM channel 1**, `SNES.sv:770-776` (not block RAM) | CPU + DMA, same cycle lengths |
| VRAM | 64 KiB (two 32 KiB halves) | block RAM, `dpram #(15) vram1/vram2`, `SNES.sv:801-835` | PPU, one access per dot-slot, dual port with the CPU side |
| ARAM | 64 KiB | block RAM, `dpram_dif #(16,8,15,16) aram`, `SNES.sv:837` | SPC700 + S-DSP |
| Backup RAM (BSRAM) | `BSRAM_BITS = 18` = 256 KiB, `SNES.sv:851` | block RAM | CPU; also SA-1 BW-RAM and SuperFX RAM |
| PPU internal | OAM 544 B, CGRAM 512 B, line buffers | block RAM / registers | PPU |
| Microcode ROMs | 65C816 `MCode.vhd` (256 KB source), SPC700 `MCode.vhd` (276 KB source) | inferred ROM | CPU cores |
| SPC IPL | 64 bytes, constant in `SMP.vhd:75-77` | logic | SPC700 boot |

Bus bandwidth is **not** the problem: worst case about 3.6 M ROM bytes/s + 2.7 M WRAM bytes/s, 6-7 MB/s against a DDR2 port that peaks at 600 MB/s (`host/ddr2_lean.v`, 150 MHz DDR, BL8) and a C128 frame writer that already takes 26 MB/s (T113 §5). **Latency is.** A read through the host's real-time port `c_*` takes 293-693 ns in the benches (`ddr2c_model.v`, quoted in T113 and T116 as bench-model values, **not board-measured**) and "about 0.4 us" through `ddr2_port` (`host/ddr2_port.v:5`); a 3.58 MHz FastROM cycle is 279 ns.

### Clocks (S)

PLL `rtl/pll/pll_0002.v`: 85.909080 MHz (SDRAM, 4x), 42.954540, **21.477270 MHz master**; PAL master is 21.281370 MHz. The S-DSP/SPC700 run from a 24.576 MHz-class clock in the original; the MiSTer core derives it with clock enables from the same domain. SNEStang derives everything from `mclk` ~21.5 MHz and `fclk` 4x (`doc/design.md:55-58`).

### The host's share in a flat build (M)

Measured from the C128 flat build, `cores/c128/mono/reports/b_r6/utilization_hier.rpt` and `utilization.rpt` (branch `t136-a1200-colour`; Vivado 2024.1, post-route, XC7A50T), host overlay stubbed out:

| | Whole design | `l2` (the C128 core) | **Host = difference** |
|---|---|---|---|
| Slice LUTs | 28,451 (87.27 % of 32,600) | 17,034 | **11,417** |
| Slice registers | 27,409 (42.04 % of 65,200) | 13,836 | **13,573** |
| Block RAM tiles | 73 of 75 | 60 (59 RAMB36 + 2 RAMB18 as 1) | **13** (+3.5 if the host overlay is kept, as in the A1200) |
| DSP | 12 | 3 | **9** |

Biggest host items (same report): `ult` (RISC-V SoC, Gideon's blocks) 6,453 LUT, `ddr` 782, `dbg` (JTAG) 686, `hdmi_enc` 623, `pxl` (pix_layer) 537, `gtp` 316, `kb` 298, `txt` 208. The highest flat LUT figure that has ever routed on this board is the C128's 87.27 %.

**What is left for a core in a flat build [M-derived]:** about **21,100 LUT**, **51,600 FF**, **62 block RAM tiles** (58.5 with the overlay kept), **111 DSP**. Glue to the host (DDR2 client, caches, pixel writer, registers, keys, audio port) is not in that figure; the NES glue was 1,569 LUT and about 11 tiles (T95 §5.17 table, `nes_mem` 392 LUT / 7 RAMB36 + 4 RAMB18, `nes_pixw` 235 LUT / 2 RAMB36, `nes_regs` 613).

### DDR2 space (planning figure from the owner, corrected 2026-10-28)

In a large core the host application is gone; the only other user of DDR2 is this core's own core application. From `host-app/target/u64ii/riscv/coreapp/linker.x` and the built `coreapp.elf`:
`memory ORIGIN = 0x00030000, LENGTH = 0xE60000` (end `0x00E90000`); **[M]** `nm`: `_end = __heap_start = 0x009B10A0` (text 497,172 B + data 136,576 B + **bss 9,331,968 B**), `__heap_end = 0x00E90000`, stack top `__heap_limit - 4`. So the application owns `0x00000000 - 0x00E8FFFF` (14.56 MiB). The A1200 puts its frame buffers from `0x01000000` (and `0x04000000` for 32-bit pixels). **Planning figure: the core owns `0x01000000 - 0x07FFFFFF` = 112 MiB** (113.4 MiB contiguous from `0x00E90000`, rounded to the existing 16 MiB convention). The 9.3 MB of bss is the application's own choice and could shrink; the figure is recomputed from the linker map at build time. Everything an SNES needs in DDR2 (ROM 8 MiB + WRAM 128 KiB + BSRAM 256 KiB + 4 frame buffers of 1 MiB = about 12.4 MiB) is a ninth of that. **DDR2 capacity is not a constraint for any SNES cartridge** (largest: 6 MiB, plus MSU-1 audio if ever wanted).

### Parts (MECE; every §2 scenario solves exactly one)

- **P1 Logic area:** LUT / FF / DSP fit beside the host at a routable level.
- **P2 Block RAM:** tiles fit beside the host.
- **P3 Memory latency:** ROM and WRAM from DDR2 at the machine's cycle times without breaking it.
- **P4 Clocks and timing:** 21.477 MHz master (NTSC and PAL), S-DSP 32 kHz, Vivado WNS.
- **P5 Picture:** 256x224/239 and 512x448/478 (hires, interlace) to HDMI.
- **P6 Sound:** S-DSP stereo to the host mixer.
- **P7 Input:** pads from C64U joystick ports and keyboard; mouse/Super Scope scope.
- **P8 Media and saves:** ROM from SD (header detection), battery RAM back to SD.
- **P9 Licence and firmware:** what may be built in, what must come from the user.
- **P10 Chips:** which coprocessors are in, which are refused.

### The numbers per part

**P1 Logic area — [E], not measured.**
- No resource report exists in either upstream repo (searched: no `.rpt`, `.fit.summary`, `.srf` with counts; the `.srf` files are suppressed-message lists). The Cyclone V figure cannot be quoted. An ALM is a fracturable 8-input cell; as a rule of thumb one ALM is 1.0-1.3 Xilinx LUT6 [E, rule of thumb, not verified for this design].
- **Tang evidence (upper bound only):** SNEStang runs on the Tang Nano 20K, 20,736 LUT4 (README), **including** the PicoRV32 I/O system, HDMI encoder, 3-channel SDRAM controller and `snes2hdmi`. So the SNES machine itself with DSPn is below 20,736 LUT4. LUT4 and LUT6 counts do not convert by a fixed factor; no conversion is claimed. The Tang reports are not in the repository.
- **Warning from this project:** the NES core was estimated at about 3,200 LUT (T95 §5.4) and routed at 13,114 LUT (T95 §5.17, `cores/nes/core.toml` header) — a factor four. MiSTer-derived cores arrive larger than their first estimate.
- MiSTer's `main.v` instantiates **every** coprocessor at once (`USE_CX4 .. USE_SS = 1'b1`, `rtl/main.v:146-156`); the build here would instantiate none or few. That is the main reduction.
- **Bracket for the machine (CPU+DMA, PPU, SMP+SPC700, S-DSP, WRAM/VRAM/ARAM glue, DSP-1 optional): 10,000-16,000 LUT, centre 13,000 [E].** With host 11,417 + glue about 2,000 [M, E] the flat design lands at **23,400-29,400 LUT = 72-90 %**, centre about 26,400 (81 %). The upper end is above the highest routed result (87.27 %). **This must be measured before the port is committed to** (see S1).
- I tried to get a measured number by Yosys `synth_xilinx` on SNEStang's Verilog (the method of `tools/nes_area.sh`, T95). It does **not** read the design: `cpu.v` and `ppu.v` use `typedef struct packed`, typedef'd array types and array assignment patterns (`ppu_defines.vh:18-37`, `BF_TBL [0:7][0:7] = '{...}`), `dsp.v` a typedef'd mask table, `65C816/mcode.sv` and `P65C816.v` further SV. After rewriting five tables/returns by hand in a scratch copy it still stopped at `ppu_defines.vh:23`. **Not measured; the Vivado trial was skipped because the builder was busy** (a `GITEA-ACTIONS-TASK-4194 ... Sims` container was running at 10:52 CET).
- FF: 65C816 + SPC700 + PPU pipelines; [E] 6,000-9,000 (NES-like cores here: 3,128 FF for the PPU alone in Yosys, T95). Not binding (65,200 - 13,573 = 51,600 free).
- DSP: S-DSP gaussian/BRR multipliers, PPU mode 7 (two 16x8 multipliers), PPU multiply register ($2134), CPU hardware multiplier/divider. [E] 6-12 DSP48; SuperFX adds a multiplier, SA-1 a multiplier/divider (`SA1MULT.vhd` is an Altera `lpm_mult` wrapper). Far below 111.

**P2 Block RAM — [E] from sizes [S]; the dominant constraint.** RAMB36 = 4 KiB of 8-bit data.

| Item | Tiles |
|---|---|
| VRAM 64 KiB | 16 |
| ARAM 64 KiB | 16 |
| WRAM 128 KiB, **if in block RAM** | 32 |
| BSRAM 32 KiB typical (up to 256 KiB, `SNES.sv:851`), **if in block RAM** | 8 (up to 64) |
| PPU: OAM, CGRAM, line buffers | about 4 |
| Microcode ROMs 65C816 + SPC700 | about 3-5 |
| ROM line cache 8 KiB + WRAM line cache 4 KiB (NES pattern) | 3 |
| Pixel writer line buffers (`nes_pixw`: 2 RAMB36) | 2 |
| SuperFX cache / DSP-1 tables, if built | 1-3 |

- **Everything as MiSTer has it (WRAM + 256 KiB BSRAM in block RAM): 16+16+32+64+4+4+3+2 = 141 tiles. Does not fit anywhere.** Even with 32 KiB BSRAM and WRAM in block RAM: 85 tiles, more than the 62 left.
- **WRAM in DDR2 (as MiSTer itself does, `SNES.sv:770`) and BSRAM 32 KiB in block RAM: 16+16+8+4+4+3+2 = 53 tiles. With BSRAM also in DDR2: 45 tiles.** Against 62 (58.5 with the overlay) left: fits, with 5-17 tiles of slack. 73 of 75 is the highest routed (C128), so 53 + 13 = 66 is a sane place to be.
- It does not fit a DFX `pb_l2` (50 tiles): 53 + glue exceeds it. **A large core is the right frame for the SNES**, not a choice.

**P3 Memory latency — [S] machine, [bench-model] DDR2.**
- The NES solution applies unchanged: direct-mapped line cache (16 B = one `c_*` word, 128 bit), write through, keyed by the linear address, **a miss takes the clock enable away from the whole machine** (`nes_mem.v` header; same circuit as `a8_mem.vhd`). SNES CPU, PPU, SMP and S-DSP all run off one master clock in the MiSTer core (`CEGen`, `SNES_SYSCLK*_CE`), so stalling the enable stalls consistently. SNEStang already does the same on purpose (`pause_snes_for_frame_sync`, `doc/design.md:158`).
- **Cost of a stall:** the machine runs slower by (miss rate x 0.3-0.7 us). ROM is mostly sequential (code) so a next-line prefetch is cheap. At 1 % misses on 3.58 M accesses/s and 0.5 us: 1.8 % lost. At 5 %: 9 %. [E] — the hit rate must be measured on real code (S3).
- **The audio problem:** the S-DSP produces 32,000 samples/s of *machine* time; held clocks stretch *real* time, so the sample rate drops by the stall fraction (pitch error) unless the host's audio path buffers and the S-DSP/SPC700 domain is allowed to run on past a ROM miss. Options in §3 D5.
- WRAM: the hottest memory (every stack/direct-page access, all DMA). 128 KiB behind a 4 KiB cache (256 lines x 16 B) with write-through; misses stall like ROM. Alternatives rejected in §3 D4.

**P4 Clocks and timing — [S], [E].**
- 21.477 MHz is slow for Artix-7; the long paths will be the combinational `cart` mapper address logic, as in the NES (T95 §5.17, 9.928 ns of 10.000 with 90 % routing). One clock enable on a faster clock, as in `nes_core.v`: master enable from a phase accumulator on the host system clock; PAL by changing the increment.
- The 85.9 MHz SDRAM clock and `sdram.sv` disappear; the DDR2 engine and caches are ours.

**P5 Picture — [S].** PPU output 256x224 (239 overscan) or 512x448/478 (hires/interlace), 15-bit BGR. At 512x478x2 B = 489 KB per frame, 30 MB/s at 60 Hz — the same class as the C128's 26 MB/s. The A1200 pattern applies: own `pix_layer` copy (T136), 16-bit format (the host's pixel layer "stays at 16 bit", T136 decision of 2026-10-28), frame buffers in the core's DDR2 range, `fb_*` registers, geometry from T118.

**P6 Sound — [S].** S-DSP stereo 16-bit at 32 kHz; goes to `audio_l/audio_r/audio_sample_en` (24 bit signed, as `nes_audio.v`).

**P7 Input — [S].** Two pads of 12 buttons. C64U joystick ports give four directions + one fire; the NES pad glue (`nes_pad.v`, Alex's T95 mapping) shows the pattern: stick = D-pad + A, keyboard = the rest. SNES needs B, X, Y, L, R, Select, Start as well. USB pads are not in the large-core application yet; T141 is about keyboards only. Mouse and Super Scope exist in `SNES.sv` (`MOUSE_EN`, `GUN_MODE`) and are out.

**P8 Media and saves — [S].** ROM in DDR2 at the core base, written by the core application from the SD card directly (the application runs on the RISC-V with DDR2 access; the byte-wise toggle DMA window of `nes_win.v` is for DFX cores and would load a 4 MiB ROM too slowly). The MiSTer header logic — copier header (512 B) detection, LoROM/HiROM/ExHiROM, `rom_type`, `rom_mask`, `ram_mask` (`SNES.sv:473-474`) — is ported into the application or a small RTL block. Battery RAM: SNEStang reads back the BSRAM every 10 s and writes `/saves/<name>.srm` (README); the same works here with BSRAM in DDR2 (the application can read it without touching the machine).

**P9 Licence — see §3 D9.** Verdict: **usable TREX-internal, with five groups that need action** (below).

**P10 Chips — see §3 D8.**

### Licence, per group (file headers read, not only the README)

| Group | Header / licence found | Verdict |
|---|---|---|
| Repo `LICENSE`, `README.md` | GPL-3.0 full text (GitHub API: GPL-3.0) | covers every file without its own header |
| `SNES.sv` | "Copyright (C) 2017-2019 Srg320, 2018-2019 Sorgelig ... GNU General Public License" | GPL, v3-or-later as stated in the full header |
| `rtl/ddram.sv`, `chip/MSU1/msu_data_store.sv`, `hps_ext.v`, `uart.sv` | GPL v3 or later, headers present | GPL |
| `rtl/sdram.sv`, `ddram.sv`, `hps_ext.v`, `pll*`, MiSTer framework (`sys/`) | MiSTer-specific; SDRAM/DDRAM/HPS/PLL replaced by our DDR2 engine | **not built** |
| `rtl/65C816/*`, `SPC700/*`, `CPU.vhd`, `PPU.vhd`, `DSP.vhd`, `SMP.vhd`, `SNES.vhd`, `chip/*` (GSU, SA1, SDD1, SPC7110, CX4, DSPn, ...) | **no per-file header** | GPL-3.0 by the repo `LICENSE`; no author named in the files |
| **`cx4cache.vhd`, `SA1MULT.vhd`, `SPC7110_FIFO.vhd`, `pll.v`, `bram.vhd`/`mlab.vhd` (altera_mf)** | Altera Quartus megafunction wrappers: "subject to the terms and conditions of the Altera Program License Subscription Agreement" | **do not use**; replace by inferred RAM/multiplier (they are tool output for Altera devices) |
| **`dsp11b23410_p.mif` / `_d.mif`** (DSP-1B firmware), **`CX4/drom.mif`**, **`BSX/bsx121-124.mif`**, **SPC700 IPL 64 B in `SMP.vhd:76`** | dumps of Nintendo/NEC/Capcom chip ROMs, inside the GPL repo | **third-party firmware**: the rule "ROM never in the repository, a `.cor` or a `.cfw`" applies; exceptions need Alex's word in writing (Q3, Q4) |
| SNEStang `src/gowin_dpb_*.v` | Gowin Semiconductor vendor IP | not used |
| SNEStang `src/CEGen.v` | translated from Vincenzo Liguori's code, "GPLv2" | GPL v2 only on that file: do not combine, use MiSTer's `CEGen.vhd` or write one |
| SNEStang `dualshock_controller.v` | "Katsumi Degawa, all rights reserved" (freeware non-commercial per the Tang ports) | not used |

Clean-room note: nothing here involves Gideon's UltiSID/Copper/lp_filter/sampler VHDL; none was opened.

---

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1, P2, P4 | The flat synth-only trial of the **machine alone** (CPU, PPU, SMP, S-DSP, no coprocessor, memories as D3) reports a LUT count; the full flat build routes at **<= 85 % LUT, <= 70 tiles, WNS >= 0** | Vivado `utilization.rpt` + `utilization_hier.rpt` of `cores/snes/mono/build.sh`; first the synth-only trial in `vivado-env:2024.1` | builder, CI lane `large-snes` |
| S2 | P3 | A hand-written 65816 test program (our own, no third-party code) runs from ROM in a DDR2 model with 293-693 ns latency and the result (WRAM and VRAM dump) equals the run with an ideal zero-latency memory, bit for bit | `cores/snes/sim/mem_ref/run.sh` (ideal) vs `sim/mem/run.sh` (model); compare the dumps | `ci/sims.txt` tier 2 |
| S3 | P3 | The ROM miss rate and the stall fraction on a 1 MiB test image of our own code (sequential, bank switching, loops) are counted by a register; the bench **fails above 3 % stall** | `sim/mem/` counters | `ci/sims.txt` |
| S4 | P3 | **Negative:** key the cache by the CPU address instead of the linear one (`SNES_MUT_NOINV`, as `NES_MUT_NOINV`) — a bank switch must then serve stale data and S2 must go red; a skipped write-through (`SNES_MUT_NOWT`) must too | the same bench with the mutation define | `ci/sims.txt` |
| S5 | P5 | A mode 1 + mode 7 + hires test frame from our own program renders to the DDR2 frame buffer; the CRC equals the golden CRC of the zero-latency run; the pixel at a known coordinate is the expected RGB565 | `sim/frame/run.sh` | `ci/sims.txt` |
| S6 | P5 | **Negative:** swapping red and blue in the writer changes the CRC and the bench fails | mutation `SNES_MUT_RB` | `ci/sims.txt` |
| S7 | P6 | S-DSP sample count per video frame is 533.3 +- 1 over 60 frames **while ROM misses stall the machine** (the audio-stretch problem of P3) | `sim/audio/run.sh` | `ci/sims.txt` |
| S8 | P7 | Keys and joystick bits arrive in `$4218-$421B` and the serial `$4016/$4017` reads in the bench; port 2 bits are not port 1 | `sim/pad/run.sh` | `ci/sims.txt` |
| S9 | P8 | The application loads a ROM image (headered and headerless; LoROM, HiROM, ExHiROM from synthetic headers made by the test) into DDR2 and derives the same `rom_type/rom_mask/ram_mask` as `SNES.sv` for each; a corrupt header is refused with a message | `tests/coreapp/snes_header` | `tests/` |
| S10 | P8 | Battery RAM written by the machine is read back by the application, written to the SD card, and after a reset (by the user: power cycle) restored — a game's save appears | board, one commercial ROM and its `.srm` supplied by Alex; photo | board session (by hand: needs Alex's ROM and the board) |
| S11 | P9 | The build tree contains no `.mif`/`.hex`/`.bin` of a chip ROM, no Altera megafunction wrapper, and `NOTICE` names every vendored group | `ci/lint.sh` rule `snes_clean` (grep + hash list) | CI lint |
| S12 | P10 | A ROM whose header names SA-1/SuperFX/CX4/SPC7110/SDD-1 (synthetic header) is refused with a visible message and does not start the machine; with the chip built in (if Q2 says yes) it starts | `tests/coreapp/snes_header` | `tests/` |
| S13 | P5, P6, P7 | A commercial game boots on the board: title screen, pad control, sound, no tearing | board, Alex's ROM; photo | by hand |

Every part has at least one scenario: P1 S1, P2 S1, P3 S2-S4, P4 S1, P5 S5-S6, P6 S7, P7 S8, P8 S9-S10, P9 S11, P10 S12. S13 is the end-to-end check on the real machine.

---

## §3 Decision (proposed; each open point is in §3a)

- **D1 Large core, flat build, host unchanged.** `cores/snes/mono/` with `core.toml` (`delivery = "cfw"`, `app = "blueprint"`, `experimental = true`) and `build_mono.tcl` on the pattern of `cores/amiga1200/mono` and `cores/c128/mono`; lane `large-snes`. Reason: P2 — 53 tiles plus glue cannot be a `pb_l2` (50).
- **D2 Source base: the MiSTer VHDL/SV core, not SNEStang's Verilog.** Reason: it is the reference; SNEStang's translation is derived from it, uses Gowin RAM blackboxes and a different memory arrangement. For CI/lint/simulation the VHDL goes through the GHDL-to-Verilog route the NES and the ZX Next use (`tools/<core>_gen.py`, `gen/` tracked, `rtl/` pristine upstream); Vivado could also read the VHDL directly in the flat build (the A1200 does with TG68K). SNEStang is used as a cross-check of the translation of each module and for its memory timing notes.
- **D3 Memories:** VRAM, ARAM in block RAM (as MiSTer, 32 tiles); **WRAM 128 KiB and BSRAM in DDR2** behind write-through line caches (WRAM 4 KiB, ROM 8 KiB, BSRAM 2 KiB), ROM in DDR2; core application loads ROM straight into DDR2. Core DDR2 base `0x01000000`, 112 MiB planned (above), layout: ROM at +0 (8 MiB), WRAM +0x0800000, BSRAM +0x0820000, frame buffers `0x02000000`, 1 MiB stride. DDR2 client = the host's real-time port `c_*` as the NES.
- **D4 WRAM in DDR2, not block RAM.** 32 tiles would leave 30 for everything else; MiSTer itself keeps WRAM in SDRAM (`SNES.sv:770`), so the machine already tolerates a slow WRAM. Lost: a block-RAM WRAM is stall-free — then BSRAM must go (more than the stall costs). Could be revisited after S1 shows real tile numbers.
- **D5 Stall policy:** hold the whole machine on a miss (NES). Audio: the S-DSP output goes through a small FIFO into the host mixer and the sample rate is derived from **machine** time as it is now; if S7 shows the pitch error is audible, move the SMP + S-DSP onto an enable that continues during a ROM stall, with the four CPU-SMP port bytes crossed through a latch pair (the SNES's own interface is exactly four registers, which makes the crossing small). Not built before S7 says it is needed.
- **D6 Clock:** one system clock, master enable by phase accumulator, NTSC 21.477272 MHz default, PAL 21.281370 MHz by the region bit of the header (decided from the ROM header, as the frame rate must follow the cartridge).
- **D7 Picture/sound/input:** pix_layer copy with 16-bit format, `snes_fb.v` after `nes_pixw.v`; `audio_l/r` after `nes_audio.v`; `snes_pad.v` after `nes_pad.v`: joystick = D-pad + B, keyboard = A/X/Y/L/R/Select/Start (final key table is Q5).
- **D8 Chips:** build-time subset set by `main.v`'s `USE_*` parameters. Proposed v1: **none, plus S-RTC and OBC-1 (tiny, no firmware)**; DSP-n only if its firmware is supplied by the user (D9). SA-1 (second 65C816, I-RAM, BW-RAM sharing BSRAM, multiplier/divider, +4-6 k LUT [E]) and SuperFX (GSU 58 KB VHDL, own cache RAM, multiplier, +3-5 k LUT [E]) are **out of v1** (§4); they would be measured one at a time after v1 routes.
- **D9 Firmware and licence:** the SPC IPL (64 bytes) is an embedded constant in `SMP.vhd`; DSP-n/CX4/BSX data are `.mif` dumps. v1 carries the IPL as the upstream core does **only if Alex allows it as he did for the Atari OS ROM**; otherwise the 64 bytes are read from a user file at start (`spc_ipl.bin`). DSP-n/CX4/BSX data are never in the tree, a `.cor` or a `.cfw`: loaded from `/SD/...` into a block RAM by the application, like the Kickstart for the A1200. `NOTICE` lists GPL-3.0 and the repo commit; Altera wrappers replaced by inference; `CEGen` rewritten.
- **D10 Verification:** our own test programs only (written for this project, no third-party ROM in the repo; "no ROMs/BIOS ever"), benches in iverilog/GHDL as the other cores, board scenarios by hand with Alex's ROMs.

## §3a Open questions for Alex (one per line, to be asked one at a time)

1. **Q1** Does the SNES go through a *measure first* gate: Vivado synth-only of the machine alone (no coprocessors) on the builder before any glue is written, and is "unmeasured, 72-90 % LUT" a good enough reason to do that first?
2. **Q2** Which coprocessors does v1 need: none, or DSP-1 (Super Mario Kart, Pilotwings), SuperFX (Star Fox, Yoshi's Island), SA-1 (Super Mario RPG, Kirby Super Star)?
3. **Q3** Answered 2026-10-29: embedded (see Open questions).
4. **Q4** DSP-n/CX4/BSX firmware: user-supplied from the SD card (as Kickstart), yes?
5. **Q5** Key map for the SNES pad on the C64 keyboard and joystick (B on fire, the rest on keys)?
6. **Q6** PAL and NTSC: follow the cartridge header's region, or a menu switch?
7. **Q7** Is a stall-induced pitch error (P3) acceptable in v1, or must the audio side be decoupled from the start?
8. **Q8** Large SNES saves: battery RAM to `/saves/<name>.srm` on change (every 10 s as SNEStang), good enough?

## §4 Out (proposed; all to be decided with Alex before work)

- SA-1, SuperFX/GSU, SPC7110, SDD-1, CX4, BSX/Satellaview, Sufami Turbo, ST010 (unless Q2 adds them).
- MSU-1 (needs up to 1 GB of audio; DDR2 has 112 MiB), Super Scope, mouse, multitap (second port is a pad).
- MiSTer savestates, cheats, rewind (`savestates*.sv`, `cheatcodes.sv`), the HPS interface, SNI, the UART.
- CPU/SuperFX turbo hacks (`SNES_Turbo.md`), borders.
- USB gamepads (needs T141-like work in the application).
- SNEStang's PicoRV32 I/O system and its HDMI/OSD (the host has its own).

## §5 Decisions taken while building (2026-10-28, branch t142-snes)

Measured on the builder (Vivado 2024.1, flat host + core, `cores/snes/mono/build_mono.tcl`); report lines from run m4 (identical to m3):
`utilization.rpt`: Slice LUTs 21,144 (64.86 %), Slice Registers 20,357 (31.22 %), Block RAM Tile 71 (94.67 %), DSPs 22; `timing.rpt`: WNS +0.221 ns, WHS +0.051 ns, 0 failing of 48,533 endpoints.
The machine alone (SNES.vhd, S-CPU/PPU/SMP/DSP, plain cartridge mapper, out of context): 11.9k LUT before the microcode ROMs moved to block RAM, about 10.2k in the flat design; 4,710 FF, 13 DSP.
The spec's centre estimate (13k LUT machine, 26.4k total, 81 %) was high; the measured total is 64.9 %. Block RAM is the tight resource (spec estimate 53 tiles for the core, host 13-16): the core uses 50.5, the whole design 71.

- **D2 base:** SNES_MiSTer VHDL (bdcd33c5), converted by GHDL 6.0.0 (`tools/snes_gen.py`, scratch edits counted: complete binary case lists, boot ROM as RAM with a port, competition-cart status bit). SNEStang not used. openfpga-SNES (agg23, 1a08c84) checked: its `rtl/upstream` is the same SNES_MiSTer RTL (older), with ROM/WRAM in the Pocket's external SDRAM; no advantage, not taken.
- **Clock (D6):** the machine runs on the host's 100 MHz cpu_clk with the 21.477 MHz (PAL 21.281) master clock as an enable from a phase accumulator (`cep`), the falling edge (`cen`, only the S-DSP's and the mapper's CE generators) two clocks later. A miss takes the enable away (`stall`).
- **Timing (m2 -> m3):** m2 WNS -3.842 ns, 302 failing endpoints: path from the cache data block RAM (`mem/c_wram/data_reg`) through byte select, mapper and S-CPU decode (15 LUT levels) timed at one clock. The multicycle recipe (`cores/snes/core_timing.tcl`) did not match the block RAM (it had absorbed the output register); pattern added (3 clocks, the data is taken at a tick 4-5 clocks later), and the write-hit byte update became a word write (no byte lanes: 4 block RAM per lane before). m3: post-place WNS -0.06, routed +0.221.
- **D3 memories, as built:** VRAM 2x32 KiB and ARAM 64 KiB in block RAM (32 tiles), the 65C816 and SPC700 microcode ROMs in block RAM (3+4 tiles, about 4,500 LUT saved), WRAM 128 KiB, battery RAM up to 256 KiB and ROM up to 8 MiB in DDR2 behind write-through line caches (ROM 8 KiB, WRAM 4 KiB, battery RAM 2 KiB, index = line number folded on itself, 16 byte lines, 4 entry write queue, in-order c_ port). **DDR2 layout (base `CART_BASE` 0x01000000):** ROM +0, WRAM +0x800000, battery RAM +0x820000, frame buffers +0x900000, four buffers, stride 512 KiB (512x448 RGB565). This replaces the layout of D3 (frame buffers at 0x02000000, 1 MiB stride).
- **D4:** WRAM in DDR2 as proposed; measured: the machine is held 1.08 % of the clocks on the mem bench (233,165 of 21.6M; 1,723 ROM and 1,579 WRAM line fetches), under the 3 % of S3. The DDR2 latency of the bench is the model of 293-693 ns, not a board figure.
- **D5 audio:** not decoupled: S-DSP/SMP stall with the machine, so samples per machine frame are constant; the real-time rate drops by the stall fraction (about 1 %). Zero order hold 32 kHz -> 47.35 kHz. S7 (sample count under stall) not built as a separate bench.
- **D7 picture:** 512 wide in every mode (low resolution: the main pixel twice; hires keeps both), 224 lines (239-line mode loses 8 top and 7 bottom), every line two rows (interlace: the field's row; the two fields weave into one buffer). Window 1024x896 at scale 2. Input: C64 joystick = D-pad + B; keyboard port 1: SPACE=A Z=Y X=X A=L S=R F3=Select F1=Start; port 2: RIGHT SHIFT=A comma=Y full stop=X K=L L=R F7=Select F5=Start.
- **D8 chips, v1:** none, plus S-RTC (ExHiROM only, time from the board clock) and OBC-1. DSP-n (DSPn.vhd vendored, not built), SA-1, Super FX, Cx4, SPC7110, S-DD1, BSX, Sufami, MSU-1, Game Genie, save states out; the application refuses such a cartridge naming the chip (S12).
- **D9 boot ROM:** the S-SMP's 64 byte boot ROM is not in the tree: `tools/vendor_snes.sh` cuts it out of SMP.vhd, the application writes the embedded 64 bytes into a small RAM at the start (Q3 decided, see the first entry above; there is no `spc700.rom`). The S-DSP's gaussian table (DSP_PKG.vhd, 512 entries) stays. `rtl/bram.vhd`, `mlab.vhd` (Altera wrappers) and every `.mif` are not taken; memories are `ip/snes_ip.vhd`. `CEGen.vhd` (upstream, GPL via the repository LICENSE) is used as is.
- **P8 saves:** `<core folder>/saves/<image name>.srm`, checked every 5 s, written when changed, and before a reset or another load; 0xFF-filled when there is no file. Header read by `coreapp/cores/snes_rom.cc` (LoROM/HiROM/ExHiROM, copier header, mapper byte, masks as SNES.sv, region), sizes above the header's are widened to the image; images over 8 MiB refused.
- **Verification (builder, Verilator, programs of our own, a boot ROM stub of our own):** S1 flat build routes (above); S2 `mem` program (ROM banks, WRAM, stack, MVN, DMA to WRAM/VRAM/CGRAM, PPU read-back) ends equal behind the caches and behind a never-late memory, and its six checksums equal `tools/snes_expect.py`; S3 stall 1.08 %; S4 mutations `mut_nostall`, `mut_nowt`, `mut_nohash` make the bench fail; S5 `video`: pixels (10,10) f800, (400,10) 001f, boundary at x=256 exact; S6 `mut_rb` fails; S8 `pad`: $4218-$421B read 80 88 80 02, `mut_padorder` fails; S9, S12 `tests/coreapp/test_snes.cc` and `test_snes_rom.cc` (27 controls); S11 `tools/check_snes_tree.py`. Not run: S10, S13 (board).
- **Package:** `snes.cor` 2,781,576 bytes (the m4 bit and the generic core application).

- **Q3 decided on main, built here (2026-10-29):** the 64-byte SPC700 boot ROM is embedded as MiSTer does. The vendored RTL keeps it cut out (`SMP.vhd:76` is
  zeros, the port `IPL_WR` writes the small RAM), so the application carries the bytes: `coreapp/cores/snes.cc` `spc_ipl[64]` (PROVENANCE comment: SNES_MiSTer
  `bdcd33c5`, `rtl/SMP.vhd:76`) is written in `snes_init`, i.e. at every start while the reset is held. `spc700.rom`, the `snes.put_ipl` op, the descriptor's ROM
  entry and `roms` in `core.toml` are gone. **No gateware change**; the existing bitstream is reused, only `coreapp.bin` and the `.cor` are new. The bench
  `test_snes.cc` compares the 64 port writes after `init` with an independent hex string; controls `snes_ipl` (a byte flipped) and `snes_ipl_call` (init does
  not write) have to fail. The stale "the user's spc700.rom" wording in `snes_regs.v`, `snes_gen.py`, `vendor_snes.sh`, `rtl/NOTICE`, `snes_asm.py` and the NOTICE check of
  `check_snes_tree.py` was corrected on 2026-10-31 (comments only; the NOTICE check now wants `spc_ipl`; `snes_regs.v` is an RTL file, so the next CI run rebuilds the lane).

- **First board run, 2026-10-29: no sound, keys dead, "Save the battery RAM now" fails (Alex).**
  - *Sound, gateware measured, not the cause.* `tb_snes.v` counts the non-zero samples of `audio_l/r` (new line `SNES audio nonzero=N peak=P`). Program `sim/prog/audio.s` +
    `tools/snes_asm.py spcaudio` (an S-SMP stub that writes one voice into the S-DSP; the bench puts the directory and one BRR block into the audio RAM): 1,190,948 non-zero
    clocks, peak 1,308,544 of the 24 bit range; control `spcaudio_nokon` (no key-on): 0 and 0. Then the whole chain with the application's **real 64 IPL bytes** (extracted from
    `snes.cc`): `sim/prog/audio_ipl.s` uploads the voice program through the boot ROM's port protocol and starts it: 1,135,414 non-zero, peak 1,308,544; control without
    key-on: 0. So the IPL write, the S-SMP, the S-DSP, `snes_audio.v` and the 24 bit hand-over to the host work in simulation; the host path (`top.v:935`) is the one the A1200
    uses. The sound is not lost in code we can read. Not yet explained: the board. The earlier "no sound" reports of the Plus/4 and the C128 were the game-capture device in
    Alex's audio path (T90, 2026-10-26). **Peak registers built (2026-10-31, gateware):** `snes_audio.v` keeps, in the machine clock domain, the largest |sample| of the S-DSP's pairs
    (`PEAK_DSP` 0F8) and of what it hands to the host's audio path (`PEAK_OUT` 0FA, the cubic result before x 128) and counts the hand-overs (`HANDS` 0FC); a write to 0F8 clears both peaks. The menu
    task "Sound check" (`snes.sound`) reads them and says one of: the host takes no audio from the core / the core made no sound / the sound is lost before the host / it reaches the host
    (then a silent speaker is behind the core). Bench `run.sh audio`: the registers agree with the output port (`peak` x 128 within 10 %), silence control all zero; `mut_peak` (hand-over peak
    stuck) has to fail; app test `test_snes.cc` with six controls `snes_sound_*`. Needs a Vivado run (a few flip-flops and two comparators).
    (A bench pitfall met on the way: `tools/snes_asm.py` sizes a forward reference to a label after a `sta $7EFFFF` one byte short; tables go before the code.)
    Run 1091 (flat build): WNS -0.141 ns, cl/y_reg -> pk_out CE in cpu_mmcm (14 levels, the saturation, |x| and the compare in one clock); the peak now takes the
    registered pair (hl/hr) in two stages (|x|, then the maximum). Benches snes_audio, snes_audio_q, snes_pad PASS (builder, 2026-10-31).
  - *Keys.* `snes_pad.v`'s indices are the C64 matrix (`col*8+row`) of `keyboard_c64.cc` `keymap_normal`, checked key by key; the path `kb_keys | bl_keys` -> `app_input` ->
    `c64_keys` -> `snes_core` -> `snes_pad` is the one every core uses and the pad bench passes (`+PAD`). It reaches the core only from the C64U's own keyboard (bling or P7);
    a USB keyboard types into menus only (T141, open).
  - *Save fails: cause found in code.* `save_dir` built the core folder with `snprintf(parent, plen, "%.*s", n - 4, p)`; the board's printf has no `%.*s`, so the folder name was
    wrong, `mkdir` and `save` failed, and the message printed the wrong text. Now `nf_copy` (as `romfind.cc`). Control: `tests/coreapp/run.sh` `printf_lint` finds a
    string precision in any format of `coreapp/src` and `coreapp/cores` (the Mac's printf accepts it, so no behavioural test could); the control feeds it a `%.*s` and has to see it.

- **Board run 2 (new2: keys work in Zelda, F-Zero, SMW; DKC does not; the sound is there but bad), 2026-10-29.**
  - *Sound quality: cause found, fixed in gateware.* `snes_audio.v` held the newest S-DSP pair (32.07 kHz) for an output clock of its own (2112 clocks = 47.35 kHz); the host
    frame is 48.003 kHz (`host/top.v:223`, mclk 12.2887 MHz / 256). So the pairs were repeated one or two times in an uneven pattern (the bench log: 2184 runs of 1, 1988
    runs of 2) and the host repeated one output sample in about 70 (a click every 1.5 ms). Measured against the ideal band-limited resampling of the same pairs
    (`tools/snes_audio_q.py`, a 1 kHz tone): **23.2 dB** (old), the tone's level and spectrum were right (125 Hz and 1 kHz come out at the same amplitude, 26.5k of 32k, so
    no high-pass in the chain; the DSP's own pairs are smooth, Gauss interpolation works). New: the host's `audio_sample_en` is passed to `snes_core` / `snes_audio`; for each
    frame the machine clock side computes a cubic (Catmull-Rom, `snes_cubic`, 8 clocks) interpolation between the S-DSP pairs two back in time, at the fraction since the
    newest pair arrived in machine ticks (`ce`, 670 ticks per pair, measured 669 / 670), and hands it over by a request/answer toggle pair; it is taken only after the
    pipeline has settled (12 clocks after a new pair, so no mixed state). Delay two pairs (62 us). Bench `run.sh audio_q`: **52.9 dB** at 1 kHz, 60.7 dB at 125 Hz, largest
    single error 88 of 26.5k (no glitch samples); control `mut_zoh` (no interpolation) 24.9 dB and has to fail the 40 dB limit. Builder run (flat, Vivado 2024.1): WNS +0.120
    ns, WHS +0.028, LUT 21,769 (66.8 %), 71 / 75 BRAM tiles, DSP 29 of 120 (was 22). Level unchanged: `x 128` (+-2^22), the Amiga and the ZX Next use `x 256`; not changed
    without a board comparison. "Little or no bass" has no cause in the chain (equal level at 125 Hz); the old pattern's aliasing noise is the most likely reason it sounded so.
  - *DKC keys.* The bench runs the real 4 MiB image (`+GAME`, `+TYPE=1 +MASK=3FFFFF +RAMMASK=7FF`, frame dumps): DKC boots, draws the Rare logo, the Rareware logo and, from
    about 6.5 s of machine time, the Nintendo presents screen; a Start press at 5 s changes nothing. HiROM 4 MiB mapping and the
    cache read what the file holds (`prog/hirom.s`: 256 bytes over banks C0..FF and 00..3F, sums equal to the file's); the manual serial read of $4016 / $4017 gives the same
    `$8880 / $0280` as the automatic read (`prog/pad_manual.s`, +PAD).
  - *DKC, longer run (`+GAME +MS=21000`, Start on F1 every 2 s from 12 s, state per second in the log line `SNES game ...`):* frames 60 per second (real time), the game reads the pad
    through the automatic read every frame (60 strobes, 17 clocks per frame and port, all 21 s), the sound program is uploaded at 10 to 11 s (audio RAM non-zero bytes 23.6k ->
    53.7k, then stable at 50.5k), the picture is the Cranky Kong / gramophone scene from 12 s to the end (a 3 s animation loop) and Start does not leave it. SMW in the same bench
    (`prog` none, Start every 1.5 s from 4 s) goes from "Nintendo presents" to the file select and "1 PLAYER GAME", so the key path and the dump script work. So DKC behaves in
    the bench as on the board; the machine reads its pad and runs; whether the real game takes Start in that scene before the title appears (the intro) is not known from here,
    and the bench is real time only, one second of game costs 4 to 8 minutes of builder time (a run to 60 s is two to five hours).
  - *DKC keys: cause found and fixed in gateware (2026-10-31).* DKC's pad routine (ROM `C0:C11B`, called from its joypad read at `C0:A292` / `C0:C1AF`; read from the image, not from a comment) first waits for
    `$4212` bit 0 (auto read finished), takes `$4218` / `$421A`, and then checks by hand whether a pad is plugged in: when the low three (ID) bits of the port 1 word are 0, a standard pad, it reads `$4016` **once** and
    wants **bit 0 = 1** (otherwise it flushes 16 reads and clears the state); a 0 there makes it clear its button state (`$0500` / `$0504`) as "no pad"; port 2 the same with `$4017` (a missing pad 2 counts up `$14F9`). On a real SNES a pad
    that has shifted out its 16 bits returns 1 in the register (the data line goes low: the 4021's serial input is grounded; `ioport.sv` shifts zeros in). `snes_pad.v` shifted ones in, so the
    line stayed high and the register read 0 after the 16th clock: the other games (Zelda, F-Zero, SMW) never look there, DKC does. The comment in `snes_pad.v` said "stays high ... (shifts zeros in)", which
    contradicted itself. Now `FILL = 0`. Proof in the game itself (`+GAME`, Start held from 0.3 s, `wram0500` in the log line is `$0500..$0507` of the work RAM, DKC's own pad state):
    new gateware `wram0500=1000 ...` from 2 s on (Start = bit 12 of the 16 bit pad word), `wram14F9=00`; the same machine with the old behaviour (`SNES_MUT_PADEND`) `wram0500=0000` for the whole 4 s and
    `wram14F9=01`. Bench `run.sh pad`: `prog/pad.s` reads `$4016` / `$4017` by hand after the auto read, the line `SNES padm` must be `0101 0000`; control `mut_padend` (the old behaviour, a build of its
    own) has to fail, and the stale pre-fix binary did fail it with `0000 0000`. Port 2 is treated as a plugged-in pad as before (both ports always have the C64 side's joystick / keys), a real SNES
    with one pad would read 0 on port 2. `snes_pad.v` is RTL: **a Vivado run is needed**. The game reacts (the long run, new gateware, real DKC image, Start on F1 for 300 ms every 2 s from 9 s, a picture dumped every second, all of it in
    one run of 21 s of machine time on the builder): Nintendo presents 6 to 12 s, the Cranky / gramophone scene at 13 s, **the title screen "DONKEY KONG COUNTRY" at 14 s** (the old gateware stayed in the
    Cranky scene from 12 s to the end of its 21 s run, Start every 2 s from 12 s), the copyright screens at 18 and 19 s and **"SELECT A GAME" at 20 s** (the file select, three empty slots, "Erase Game");
    the previous schedule differed by the start of the presses (12 s there, 9 s here), the gateware control with the identical schedule is the `wram0500` / `$14F9` pair above. A bench pitfall met on
    the way: `+MS` above 21474 overflows the testbench's 32 bit clock count and the run ends at once.)

- **S-DSP sound character: cause found and fixed in the generator (2026-11-01, board: "digi samples far too loud, melody far too quiet, only scratchy highs", Samsung and Sony alike).**
  Measured, not guessed: the S-DSP's envelope was checked first (`run.sh dsplog`, voice 0 on a constant BRR sample, ADSR attack 15 / decay 3 / sustain level 4 / rate 10: ENV 2047 at the start,
  1280 = `$500` after about 1750 samples, then 5 per 256 samples, the exponential decay of the chip; Gauss table of `gen/snes.v` = `DSP_PKG.vhd` = 512 words, every four-tap sum 2047..2049) and was right.
  Then the BRR decoder: `+BRR` pokes four blocks (range 10, filter 0, 1, 2, 3) and logs every word written to the ring buffer; `tools/snes_brr.py` is the reference decoder. Filter 0 equal, filter 1 not:
  first word got -4544, want -3648 (p0 = -7168: `p0/2 - p0/32` wants -3584 + 224, the core had -3584 - 224). Cause: `DSP.vhd` writes the feedback terms of the filters 1..3 as `shift_right(0 - P0, 5)`
  (integer literal minus signed) and GHDL 6.0.0 converts `0 - X` to `X - 0` (`gen/snes.v`: `assign n16647 = p0 - 17'b0`): the sign of the term is lost, so a filtered BRR block (the music instruments, which are coded
  with filters 1..3) decodes with a wrong feedback and comes out as a loud, unstable high-passed residual, while filter 0 samples (raw PCM digi) are exact. `tools/snes_gen.py` now writes the three terms with a unary
  minus (three counted edits; `gen/` regenerated by GHDL 6.0.0 on the builder, `gen/snes.v` differs from the old only by these three lines and the renumbering of the generator's names). The `PPU.vhd` places of
  `0 - X` are unsigned and convert right (checked in `gen/snes.v`). Bench `run.sh brr`: `SNES PASS brr: 3120 decoded words equal the reference (filters 0..3)`; control `mut_brrsign` (gen/snes.v with `= -p0;` put back to
  `= p0;`): `SNES FAIL brr: 2239 of 3120 words differ from the reference, first at word 16 (block 1, filter 1): got -4544, want -3648` = `SNES PASS mut_brrsign`. Bench `snes_brr` in `ci/sims.txt`. Gateware: a Vivado run is needed (board check by Alex).
  Night 1144 (2026-11-03) marked `snes_brr` `FAIL (output)`: `ci/sims.sh` reads FAIL outside `ctl| ` lines as the bench's verdict, and the control's `SNES FAIL brr: ...` line (the decoder check's output) was printed bare. `run.sh` now prefixes the control's check output with `ctl| `.

### Open questions
- **Q3** Answered 2026-10-29 (Alex: "Ja machen wir" - "Yes, we do that", to embedding the 64 bytes as MiSTer does): the S-SMP
  boot ROM (SPC700 IPL, `SMP.vhd:76`) is embedded in the core, like the Atari OS exception; `spc700.rom` is no longer needed.
- **Q5** The key map above is a proposal (joystick = D-pad + B, keys for the rest); to be confirmed on the board.
- Q1/Q2/Q4/Q6/Q7/Q8 as answered by the build: Q1 measured (done); Q2 v1 without coprocessors (S-RTC, OBC-1 in); Q4 DSP-n firmware not built; Q6 region follows the header, with a menu override (Auto/NTSC/PAL); Q7 pitch error is the stall fraction (about 1 %); Q8 saves every 5 s as built.

---
**Effort [E]:** several weeks of agent work for v1, an order of magnitude more than the NES core (the PPU source alone is 95 KB of VHDL against the NES's small PPU); the unknowns are the PPU's routed size and the stall audio. The estimate has no measured basis. **Biggest risks:** (1) LUT may land above 85 % (P1, unmeasured); (2) audio pitch under stalls (P3); (3) the build cannot be checked on this Mac with Yosys, so every area number needs the builder; (4) firmware ROM policy (D9).
