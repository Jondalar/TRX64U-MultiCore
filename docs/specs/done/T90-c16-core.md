# T90 — A Commodore 264 core (C16, and the Plus/4 in the same core), ported from C16Nano, runs as a core in the T79 host/core split

**Status:** DECIDED (2026-10-13): all 4 open questions answered by Alex; not built.
**Status check 2026-11-13:** DONE (board confirmed by Alex 2026-10-28, 448c6f613): Lykia, sound, 64 KB default, 1541 ROM, F7 reset; no longer "not built".
**Repo:** TRX64-Ultimate (gateware under a new `cores/c16nano/` + `designs/c64_basic/layer2_c264.v`, app side, tools). The
source is read from outside (MiSTle-Dev/C16Nano); nothing is copied into this repo by this spec.
**Number:** T90.
**Milestone:** M2.
**Origin:** Alex, 2026-10-13: a C16 core (and the Plus/4 "if cheap") for TRX64 Ultimate, "the Nano cores are already there";
spec only, no gateware code.
**Depends on:** T79 (host/core split, contract `T79.1-host-core-contract.md`), T83 (the VC-20 core: the pattern this core
copies), T84 (`.cor` bundles, one application per core), T87 (layered CI: the core builds against the cached reference
host). Floorplan B (`pb_l2`) and the reference host (T87 D15/D16) stay as measured.

Template `docs/specs/_TEMPLATE.md`. File:line citations of VIC20Nano/T83 files refer to branch `t83-vc20` (d25c320), of
`layer2.v`, `pix_layer.v`, `vic_scaler.v`, `T79.1-host-core-contract.md` to `modular-integ-1` (cf20708); of C16Nano to
`MiSTle-Dev/C16Nano` main (82fed40, 2026-10-08) and of the MiSTer core to `MiSTer-devel/C16_MiSTer` main, both shallow
clones read on 2026-10-13. Nothing here is built or simulated by us yet: every number is marked as measured, published or
estimated.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to host more than one machine (`docs/todo/T79.md`, core order: "7. C16").

**What he wants:** pick "C16" (or "Plus/4") in the core selector / boot menu, the board swaps layer 2, and a Commodore 264
series machine boots to BASIC V3.5 with keyboard, picture, TED sound, joystick and a 1541 on the serial bus.

**Today:** one C64-family core and (T83) a VIC-20 core run on the contract. A TED machine is a third kind: its video chip
has **121 colours** (16 hues x 8 luminances; `ted.v` `color[6:0]`), and the host's only index-based picture path is the
VIC-II one (`pix_color[3:0]`, 16 entries, `layer2.v` ports; `vic_scaler.v` 4-bit ring and 16-entry palette, `pal_ram.v`).
The memory is 64 KB, the CPU a 6502 (8501), the bus a muxed RAS/CAS affair with the TED as bus master.

**What the source is (read, not assumed).** C16Nano (Till Harbaum, MiSTle-Dev; Tang boards, Gowin) is the MiSTer C16 core
(Sorgelig) with changes, and the MiSTer core is Istvan Hegedus' **FPGATED** (Verilog) plus a MiSTer shell. The pieces that
matter are Verilog and small: `ted.v` (the 8360 TED, 1,922 lines, version 1.9), `c16.v` (the machine top, 332 lines),
`mos8501.v` (CPU shell around T65), `mos6529.v` (keyport), `c16_keymatrix.v`, `colors_to_rgb.v`, and the T65 6502 (VHDL).
The Nano adds a 6551 UART (`uart6551/`), a tape player (`c1530.vhd`), a darfpga 1541 (`c1541/`), SDRAM/flash/HDMI/OSD/MCU
glue and Gowin IP wrappers. It has not been built or simulated by us.

**Licence (read from the file headers, not from the README):**

| File(s) | Terms as written in the file |
|---|---|
| repo `LICENSE` (C16Nano) | GPL v3 text (35,149 bytes) |
| `ted.v` | "Copyright 2013-2026 Istvan Hegedus ... FPGATED is free software ... GNU General Public License ... version 3, or (at your option) any later version" (`ted.v:3-16`) |
| `c16.v`, `mos8501.v`, `mos6529.v`, `c16_keymatrix.v`, `colors_to_rgb.v` | the same GPL v3-or-later header (2013-2016, Hegedus) |
| `t65/T65*.vhd` | BSD-3-clause wording (Wallner, MikeJ, WoS, Leikvoll; `T65.vhd:55-95` and the three siblings), no extra clause; GPL-compatible |
| `c1541/via6522.vhd` | "GPL 3.0" (Gideon Zweijtzer) |
| `uart6551/*.v`, `c1530.vhd`, `c1541/c1541_logic.vhd`, `gcr_floppy.vhd`, `c1541_sd.vhd`, `mist_sd_card.sv` | no licence text in the headers read (darfpga, others); **not needed** (D8, §4) |
| `gowin_*` IP, `hdmi/`, `misc/` (OSD, SD, MCU, flash, scandoubler), `tang/*` | board glue and vendor IP; replaced (D2) |

**No file carries a "Replay hardware only" or no-commercial clause** (a grep over the whole tree finds "replay" once, in a
change-log line of `ted.v:707` about a test tool). This is the difference to the VIC-20 (T83 §1), whose MikeJ/WoS files
carried one. The MiSTer core has the same FPGATED files under `gpl-3.0.txt` with the same headers, and its own 1541/1581
(`iec_drive/`, Alexey Melnikov 2021), a SID card (`sid_card.v`, `sid/`) and Altera-specific `ddram`/`pll`.

**Nano or MiSTer?** Same TED (both v1.9). The Nano's `ted.v` additionally has a clock enable `clk_en` on every process
(74 hits; the MiSTer file has none), and `c16.v` has `CORE_EN`/`INWAIT` (a pause hook) and the 6551; the MiSTer
shell has the SID card, `tvmode`/`wide`/`CE_PIX` video options and Altera RAM wrappers. The enable-driven TED is exactly
what a core that runs on the host's `sys_clk` with enables needs (T83 did the same), so the Nano is the source and the
MiSTer files are the cross-check. One defect found while reading: the Nano's Console-60K top instantiates `c16` with ports
`CS_RAM`/`CS_IO` that `c16.v` does not have (`c16nano_top.vhd:1068-1071` against `c16.v:33-100`), so that top does not match
its own core file; we write our own top anyway (D2).

**Why it matters:** it is the first core whose video chip needs more than the host's 16-colour index, and the first one
where the host's pixel layer is the natural picture path (the Minimig, T85, comes to the same conclusion for a different
reason). It also tests the contract on a machine that is almost free: 64 KB, one 6502, no DDR2 for the machine itself.

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms.
- **P2 Porting surface.** What is kept, what the host replaces, what is Gowin/Altera specific.
- **P3 Clocks and reset** (C1).
- **P4 Memory and ROMs** (RAM, KERNAL, BASIC, function ROM, cartridge image; model and RAM size).
- **P5 Video** (C4): the 121-colour picture, geometry, frame rate, overlay.
- **P6 Audio** (C5): TED sound.
- **P7 Input** (C6): keyboard, joysticks.
- **P8 Serial bus and drive** (IEC, the 1541).
- **P9 Connector pins and other I/O:** expansion port, user port, tape, SID card, TCBM.
- **P10 Registers, identity, app and swap** (C2, C9, T84).
- **P11 Freeze.**
- **P12 Size and timing** against `pb_l2` and the reference core.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 only files with a known licence are merged (P1)
  Given the file list of D1 with the licence of every file
  When  a file is about to be committed under cores/c16nano/
  Then  it is on that list, its copyright header is intact, and no file without licence text is on it
  And   no ROM image (BASIC, KERNAL, function, cartridge, 1541) is in the repository, a CI artifact or a .cor

Scenario: S2 the core elaborates on Xilinx without vendor primitives and boots in simulation (P2)
  Given ted.v, mos8501.v, mos6529.v, T65 and our own top, inferred RAM/ROM, no Gowin cell
  When  the design is synthesised out of context for xc7a50t and simulated with the PAL ROMs supplied locally
  Then  Vivado reports no black box, and the simulation reaches the BASIC V3.5 banner in both models
  And   the TRX64 build is unchanged (same LUT, timing met)

Scenario: S3 the clock is exact (P3)
  Given sys_clk = 31.527778 MHz (17.734475 MHz x 16/9) and the 9/10 enable
  When  the core runs one PAL frame
  Then  the TED sees 28.37516 MHz on average (within 20 ppm), a frame is 569,088 TED clocks = 632,320 sys_clk
  And   the C64 core's clocks are unchanged after the swap back

Scenario: S4 RAM, ROM and model come from the app (P4)
  Given a layer 2 without any ROM content
  When  the app writes KERNAL and BASIC (and, for the Plus/4, both function ROM halves), sets the model, resets
  Then  the C16 prints "12277 BYTES FREE"-class and the Plus/4 "60671 BYTES FREE"-class values with the BASIC V3.5 banner
  And   the expected numbers are taken from the first bench run against real ROMs and pinned then, not from this spec

Scenario: S5 the picture shows all TED colours, straight and tear-free (P5)
  Given a test program that puts every one of the 121 colour codes on screen
  When  the core writes its frame into the DDR2 buffer and the app flips it
  Then  the 121 codes give 121 distinct RGB565 values in the buffer (bench) and on HDMI (photo), 4:3, centred
  And   no tear line is visible while content scrolls (double buffer), a frame is shown twice about every 3.8 s (accepted, D3)
  And   the C64 picture after the swap back is pixel-identical to before the first swap

Scenario: S6 Freeze stops the machine exactly and the menu is readable (P11)
  Given the C16 running a program with a visible counter
  When  Freeze is chosen, the menu is shown and left again
  Then  no TED/CPU/timer state changes while frozen, the held picture is visible around the menu, the menu text is not covered
  And   after the resume the bus-cycle log equals the log of an uninterrupted run (as T83 tb_vc20_pause)

Scenario: S7 sound comes out (P6)
  Given TED voice 1 set to a tone ($FF0E/$FF0F/$FF11)
  When  the core delivers samples to the host mixer
  Then  the tone is at the right pitch (frequency counted from the captured stream) and silent while frozen
  And   no SID setting of the C64 core changes it

Scenario: S8 keyboard and joystick work (P7)
  Given the host's key matrix and joystick state
  When  Alex types a BASIC line with SHIFT, C=, CTRL, cursors, ESC, HELP, F1-F3, HOME/CLR, DEL, and moves the joystick
  Then  the characters appear as typed (every key of the translation table), and the joystick reads right in port 1 and 2
  And   the keys the C64 matrix cannot give are the ones listed in §4, no others

Scenario: S9 LOAD from the 1541 (P8)
  Given the existing drive (drive_l2) with a D64 mounted by the app
  When  Alex types DIRECTORY / LOAD "*",8 and RUN
  Then  the directory lists and the program runs
  And   the IEC lines are released while the core is not running

Scenario: S10 the port is idle and no C64 device answers (P9)
  Given the core running
  When  the expansion port pins are probed and the app touches the C64-only windows (sid, reu, sampler, uci, cart)
  Then  the core drives nothing on the slot; the windows answer "absent" (reads 0, ack echoed); no CIA/SID/VIC appears in the map

Scenario: S11 identity, app and swap C64 -> C264 -> C64 (P10)
  Given the C64 core running with a program on screen
  When  c16.cor is started from the boot menu, then later c64.cor
  Then  Alex's photos show the C64 screen, the BASIC V3.5 screen, the C64 screen; the C16 app loads its ROMs from /roms/c16
  And   the C16 application never offers C64-only actions; REST answers throughout

Scenario: S12 the core fits pb_l2 and closes timing against the reference host (P12)
  Given the reference host of T87 D15/D16
  When  the C264 partial bitstream is routed against it
  Then  WNS >= 0 and the report lists the measured LUT/FF/BRAM, which replace the estimate in §3 D13
  And   the host (layer 1) is not rebuilt for this core
```

| # | Solves | Scenario | Test (bench / CI stage / board script) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Licence list before merging | review gate: the list in this spec is the gate; `cores/c16nano/NOTICE` | review |
| S2 | P2 | Elaborates, boots in sim | `designs/c64_basic/sim/c264/run_boot.sh` (Icarus; T65 as GHDL-synthesised Verilog, D2; ROMs supplied locally, not checked in) | local, not CI (ROMs); skeleton in CI `sims` |
| S3 | P3 | Exact clock | `sim/c264/tb_c264` (enable count per frame) → `ci/sims.txt` row | CI `sims` |
| S4 | P4 | ROMs/model from the app | `sim/c264/run_boot.sh` with the ROM window model; board by the app | local, board |
| S5 | P5 | 121 colours, flip, tear | `sim/c264/tb_c264_video` (colour table, frame into a DDR2 model, geometry); board photos | CI `sims`, board |
| S6 | P11 | Freeze exact | `sim/c264/tb_c264_pause` (two machines, bus-cycle log, as T83) | CI `sims`; board photo |
| S7 | P6 | Sound | `cores/c264/sim/tb_c264_tedsnd` (real `ted.v` from register writes, frequency and level from samples; two negative controls), `tb_c264_glue` (averager) | CI `sims` |
| S8 | P7 | Keys, joystick | `sim/c264/tb_c264_keys` (every table entry); board by hand | CI `sims`, board |
| S9 | P8 | LOAD from the 1541 | board with a D64 (third-party media, by hand, nothing checked in) | board |
| S10 | P9 | Idle pins, absent windows | `sim/c264/tb_c264_windows` (every window of `layer2.v` answers or reads absent); board probe | CI `sims`, board |
| S11 | P10 | Identity, swap, `.cor` | board by hand (Alex's photos); app guard by REST script | board |
| S12 | P12 | Fit and timing | Gitea CI `pipeline.yml` core stage, reports | CI build |

Third-party ROMs (Commodore BASIC/KERNAL/function), the 1541 ROM, test programs (Diag264, the TED demos the FPGATED
history names) and D64s are supplied locally, never checked in. Whether the CI runner can host the T65-as-Verilog
simulation, or the whole thing runs in Vivado's xsim from the CI image, is the first thing S2 finds out.

## §3 Decision

Facts first, then D1-D14. Each D names its part.

**C16Nano structure (what the port starts from).** `c16.v`: one 28 MHz-class clock `CLK28` for everything, a 24-bit reset
counter (1,000,000 clocks, "emulates the length of a real reset", `c16.v:283-292`), the 8501 (`mos8501`: T65 with `Enable` =
TED `cpuenable`, `Rdy` = TED `ba`, `aec`, the 6510-style port at $00/$01), the TED (`ted`: address/data muxing through
`mux`, `ras`, `cas`, `cs0`, `cs1`, `aec`, `ba`), the keyboard port (`mos6529` at $FD3x, `c16_keymatrix` from PS/2 codes),
joystick lines merged into the key bus (`c16.v:115-121`), a 6551 at $FD0x, and the IEC lines on the CPU port (`port_out[0..2]`
inverted to DATA/CLK/ATN out, `port_in[6]`/`[7]` = CLK/DATA in). RAM and ROMs are **outside** `c16.v`, in the board tops:
64 KB RAM (`Gowin_DPB_64kram`), KERNAL 16 KB, BASIC 16 KB, function ROM low/high 16 KB each, cartridge low/high 16 KB each,
selected by `cs0`/`cs1` and the ROM bank register at $FDD0-$FDDF (`roml`/`romh`, written only in the Plus/4 model;
`c16nano_top.vhd:1011-1031`, MiSTer `C16.sv:437-444`). TED: clock must be 4 x dot clock, 28.375152 MHz PAL (`ted.v:76`);
`hcounter` 0..455, one step per 4 clocks (`ted.v:430`, `tick8`), `vcounter` 0..311 PAL / 0..261 NTSC (`EOS`, `ted.v:1280`),
`pal` is a register bit ($FF07 b6) the KERNAL writes; video `color[6:0]`, `hblank`, `vblank`, `hsync`, `vsync`; audio
`digi_sound` 16 bit signed, the sum of two channels' DAC volume (`ted.v:1759`); `clk_en` gates every process.

- **D1 Source: C16Nano (Alex's call), MiSTer C16 as the cross-check.** Read-only study; the port is our own
  `cores/c16nano/` that takes the machine logic and replaces everything board-specific. *Merged, each with its header intact:*
  `ted.v`, `mos8501.v` (shell only; it instantiates T65, not the fpga64 core its comment names), `mos6529.v`,
  `colors_to_rgb.v` (the 121-entry 12-bit table; its values are Jozsef Laszlo's, header says so), `c16_keymatrix.v` only as
  the reference for the matrix (we rewrite it to read the host matrix, D7), `t65/*` (BSD wording). `c16.v` is the reference
  for our own top (`c264_core.v`), not copied wholesale (enables, pause, memory, no UART). *Not taken:* `uart6551/*`,
  `c1530.vhd`, every `c1541/*`, `gowin_*`, `hdmi/`, `misc/`, `tang/*` (no licence text or board glue). The licence situation
  would allow a release under GPL-3.0 (source offer); the standing rule for new cores is TREX-internal (D14), so nothing
  changes.
- **D2 Porting surface (P2).** *Kept as machine logic:* TED, 8501 + T65, 6529, colour table, the ROM-bank register and the
  address decode of `c16.v`. *Replaced by host services:* HDMI, OSD, SD/loader, MCU/HID, PLLs, LEDs (as T83 D2).
  *Vendor-specific, must be inferred for Artix-7:* `Gowin_DPB_64kram`, `Gowin_SDPB_*`, `Gowin_pROM_*` become inferred block
  RAM (`ram_style = "block"`), empty at synthesis, filled by the app. *Reused from T83 (branch `t83-vc20`):*
  `vc20_ram.vhd` (true dual-port block RAM: machine port with held output + host window port), the DMA/ROM window
  (`vc20_win`: toggle handshake merged with JTAG, `dma_rom`), `vc20_regs.vhd` (register page: ID, CTRL, STATUS, FRAMES,
  page 6 forwarded to the pixel layer), `vc20_nco`, the audio averaging (656 sys_clk = 48.06 kHz), `layer2_vc20.v` (all 228
  ports of `layer2.v` checked pin by pin, `drive_l2` and `iec_bus` instances, everything unused at the decoupler's idle),
  the build switch (`TRX_CORE`), `ci/package.sh` stripping, `tools/vc20_load.sh`, and the GHDL/Icarus bench scaffolding.
  *New:* the TED-specific glue (D3-D7), the pixel-layer frame writer (D5), the key translator (D7). *Mixed-language
  simulation:* `ted.v` is Verilog, T65 VHDL, our T83 benches are GHDL; the plan is `ghdl --synth --out=verilog` on T65
  (not tried) so that one Icarus bench (`ci/sim_iv.sh`) runs the whole machine; fallback Vivado xsim from the CI image.
- **D3 Clocks and reset (P3, C1).** One clock, `sys_clk` (31.527778 MHz PAL, contract C1); the machine runs on enables, as
  VIC20Nano and T83 do. The TED clock is 17.734475 x 8/5 = 28.37516 MHz (`ted.v:76`: "1.6 x PAL system's clock"); `sys_clk` is
  17.734475 x 16/9; so **TED clock = sys_clk x 9/10 exactly**, the same ratio as the Minimig (T85 D3). A phase accumulator
  (add 9, fire at 10) is the single enable `ted_en` for TED, CPU shell, 6529, key port and audio; no MMCM, no host change.
  Exact, so **no speed error** (the VC-20 runs +0.17 % fast, T83 §5). The price is the frame rate: a TED PAL frame is
  456 x 4 x 312 = 569,088 TED clocks = 632,320 `sys_clk` = 49.86 Hz (computed), the host's 1080p50 frame is 628,992 `sys_clk`
  = 50.12 Hz, so the host shows one frame twice about every 189 frames = 3.8 s (computed, 0.53 %). This is judder, not
  tearing (D5 double buffer). *Rejected:* running the TED 0.53 % fast to hit the VIC-II frame exactly (as T83 does for the VC-20):
  needed only for the scaler's one-time lock, which the pixel-layer path does not depend on (D5); pitch +9 cents and disk/serial
  timing +0.5 % are the price, and the drive stays exact. Reset: the `RESET` input of `c16.v` (1,000,000-clock stretch, `c16.v:287`)
  is driven by `core_rst`/the app's reset; the TED's raster counters restart on reset, which the pixel-layer path tolerates
  (the T83 D16 problem, a reset moving the frame against the scaler's one-time lock, does not arise: the lock raster of D5 is
  independent of the TED). PAL only (Q5 of T83, a general rule): the `pal` register bit is read for the
  status only; an NTSC KERNAL would run at the wrong clock and is not supported.
- **D4 Memory and ROMs (P4).** *RAM:* one 64 KB inferred block RAM = 16 tiles of 4 KB. The C16 has 16 KB (mirrored through
  the 64 KB space), the Plus/4 64 KB; a `RAM_SIZE` register masks the address (16 KB: A14/A15 ignored) and takes effect at the
  next reset. The Nano's "C16 (64 KB)" is not what a C16 is (README); the mirrored 16 KB is the faithful option, the 64 KB
  the compatible one (Q1). *ROMs:* KERNAL 16 KB (4 tiles), BASIC 16 KB (4), function ROM low + high 16 KB (8; Plus/4 only),
  cartridge low + high 16 KB (8). Total 160 KB = 40 tiles (+0.5 for the drive, T83 measured 0.5) = **40.5 tiles**, against
  `pb_l2`'s 50 and above the reference core's 39 (D13). *Fallback if that blocks routing:* one pair of 16 KB arrays for
  "function ROM or cartridge" (the app loads either) = 32.5 tiles; only a Plus/4 program that switches between the internal
  function ROM and a cartridge notices. No DDR2 for the machine (the VC-20 decision, T83 D4). *Loading:* nothing in the
  bitstream; the app writes the images through the ROM window (`dma_rom`, offsets chosen with the VC-20's window layout:
  one aperture per array) and RAM through the window at the TED machine's address; machine reset follows. The cartridge image
  (PRG-less `.bin`, 16/32 KB, as the Nano's `*.bin` loader) goes into the cartridge arrays; `$FDD0` bank selection
  as in `C16.sv:437-444` (Plus/4 model only). No 6551 at $FD0x: reads there are open bus ($FF, as the Nano's
  `openbus_data` for the unmapped window $FDE0-$FDFF).
- **D5 Video (P5, C4) — the pixel layer, not the VIC-II index path.** *Why not the index path:* the scaler stores a 4-bit
  index and draws a 16-entry palette only the app writes (`vic_scaler.v`, `pal_ram.v`); the TED's 121 colours cannot be
  shown through it, and a quantisation to 16 loses the luminance steps that TED programs use as their palette. A wider index
  (7 bits, 128-entry palette, ring 32K x 7) is the contract's V1 host work (T79.1 C4, T83 Q3): a host rebuild at 96 %
  slices, so not now (Q2). *The path:* the host's **pixel layer** (`pix_layer.v`): an RGB565 frame in DDR2, W <= 960,
  scale S = 1..3 (square), position, global alpha, colour key, double buffer with a flip at the next frame start by a write
  to `PX_BACK`, registers on trx_io page 6 (which the VC-20 core already forwards from the app). The core converts
  `color[6:0]` -> 12 bit (`colors_to_rgb`) -> RGB565 (top bits repeated; 121 distinct codes stay distinct, to be proved by
  S5), crops the TED raster to **384 x 288** (the VICE/MiSTer visible size; the exact origin is fixed in the first bench from
  the TED's own blanking: `vblank` is lines 251-269 in PAL, `ted.v:1285-1286`), and writes it as 128-bit words of 8 pixels
  (768 bytes per line, a multiple of 16) through one of the DDR2 clients that are idle in this core (`c_*`, 128-bit write with
  mask; the host's window check is not built yet, the app assigns the buffer addresses from the DDR2 map). 384 x 288 at S = 3
  is 1152 x 864 = exactly 4:3, centred on the 1920 x 1080 frame (`PX_POS`); the layer cannot do the TED's slightly wider dot
  (a TED dot is 504/456 = 1.105 VIC-II dots wide), so the aspect is square-pixel 4:3, judged on the board. Traffic:
  384 x 288 x 2 B x 50 Hz = 11 MB/s written plus the same read by the layer (computed); one word per 8 TED dots = every
  1.13 us (computed), against the hand-off precedent of `pix_layer.v`'s still capture (a word every ~4 us, "ONE hand-off word",
  toggle + ack): a small FIFO of 4 words in LUTRAM absorbs a late DDR2 grant; latency on the board is the open measurement.
  *Flip:* the core raises a frame-done cause on `cw_irq` (identity `irq` bit, as T85 D5), the app writes `PX_BACK`; the core
  alternates two buffers, so a flip that comes up to one frame late repeats a frame and never tears; a single buffer
  (no flip) would show a slowly rolling seam (the 0.53 % beat of D3) and is rejected. *Lock raster:* the scaler still needs
  a frame to lock to or the output does not run (`vic_scaler.v` header: locks once on the decouple release), so the core
  also emits a dummy VIC-II-shaped raster (`pix_x` 504 dots, `pix_y` 312 lines, `pix_color` 0, from its own `sys_clk`
  counter, a few LUT) exactly as T85 D5 plans for the Minimig; it is not tied to the TED and never stops (Freeze, D12).
  *Layer order (a finding, the code against a header):* `top.v:1468-1510` chains **scaler -> overlay (the app's menu) ->
  pix_layer -> c64_text**, i.e. the pixel layer is drawn **over** the app's menu overlay (`pix_layer.v` header: "over the C64
  and overlay.v"); T85's header says "under the overlay", which the code contradicts. For this core that means the Ultimate
  menu would be hidden by an opaque picture. *Chosen without a host change:* while the menu is open the app punches a
  transparent hole into the held buffer: it writes the colour key (a 16-bit value no TED colour produces; `PX_KEY`, key on)
  into the rectangle the menu occupies (the menu box is fixed by the overlay, `overlay.v` header; source coordinates = output
  / 3) and the menu shows through; the machine is frozen so the buffer is stable, and after the resume the core rewrites
  every pixel within one frame. *Fallbacks:* lower `PX_GALPHA` while the menu is open (the picture ghosts over the menu), or
  swap the order in the host (a rebuild; affects the C64's freeze still, to be checked first). The hole punch is the thing
  to prove on the board (S6). 60 Hz modes of the host are out of scope (PAL, as T83 S3/Q5).
- **D6 Audio (P6, C5).** `digi_sound` (16 bit, two channels of DAC volume, `ted.v:1759`) is not a sample stream: it is a
  square/noise level at the 28 MHz rate. As T83: average it over a fixed number of `sys_clk` (656 = 48.06 kHz), centre,
  scale to signed 24 bit, `audio_l = audio_r`, toggle handshake to `mclk`; our own low-pass if the host mixer does not
  provide one (not a copy of any other project's). Mono. Silent (sample exactly 0) while frozen (D12). No SID: `sid_*` ignored.
- **D7 Input (P7, C6).** The host delivers `keys[63:0]` (`c64_keys`, `col * 8 + row`, 1 = down), `joy1/joy2[4:0]` and
  `restore`. The C264 matrix is 8 x 8 and all 64 positions are used (`c16_keymatrix.v:137-144`: DEL, RETURN, GBP, HELP, F1,
  F2, F3, @, SHIFT, C=, CTRL, STOP, ESC, HOME, four separate cursor keys, ...). A **64-entry translation table** from the
  C64 matrix to the C264 matrix, written from the two matrices (not from the Nano's PS/2 decoder), with shift-aware rules
  where the machines differ: the C64's two-way cursor keys (CRSR up/left = SHIFT + key) become the C264's four separate
  cursor keys (SHIFT removed from the C264 matrix), the C64's F1/F3/F5/F7 map to F1/F2/F3/HELP (positions, shifted ones to the
  C264's shifted functions), the C64 back-arrow to ESC. The table is a design artefact of this spec, bench-checked entry by
  entry (S8). RESTORE has no C264 key; unused (or RESET, the app's choice). *Joystick:* the TED reads the joysticks through
  the keyboard lines when $FF08 selects them (`c16.v:115-121`); the host's two C64 ports go to JOY0/JOY1 with a swap
  register (the C264's two ports are what games use; which C64 port is which is the app's default). Paddles/mouse: not
  connected. The keys the C64 matrix cannot give are listed in §4 once the table is written.
- **D8 Serial bus and drive (P8).** Per T79.1 decision 5 the host drives the open-collector IEC lines and hands inputs over;
  the wired-AND and the protocol stay in the core. The C264 puts DATA/CLK/ATN on the 8501 port ($01 bits 0..2 out inverted,
  bits 7/6 in, bit 2 ATN out only; `c16.v:321-327`), wired to the same `iec_bus` and `c64_atn_o/clk_o/data_o` as T83 D8;
  **the drive is `drive_l2` (our `drive_unit` + `gcr_codec`) as in the VC-20**, measured about 1,985 LUT out of context
  (T83 results, run 746), with its DDR2 client (`drv_*`) and the 1541 ROM the app already places at the drive image
  (`drive_mem.vhd`: ROM at +0x8000 of the drive's 64 KB image). The Nano's darfpga `c1541_*`/`mist_sd_card` (SD-based, no
  licence text) and the MiSTer `iec_drive` are not taken. `IEC_RESET` (= core reset) drives `drv_c64_rst_n`. The real IEC port
  (a real 1541 on the cable) works as in T83: the same lines. A **1551** (TCBM, the C16's own parallel drive) is not a serial
  device and is out (Q3).
- **D9 Connector pins and other I/O (P9, C7).** The C16/Plus/4 expansion connector is a different connector with different
  signals (its own cartridge slot on the Plus/4/C16, user port with the 6551 on the Plus/4, TCBM, cassette); the
  C64U's port cannot carry any of them for the same reasons as the VIC-20's (T83 D9: pin positions differ, output counts
  do not fit). All `SLOT_*` outputs idle (`oe` off, transceivers disabled) while the core runs; nothing is sampled.
  Cartridge **images** work (D4). User port / 6551 / modem, cassette (`cass_*`), SID card ($FD40/$FE80) and the 1551 are out
  (§4, Q3, Q4).
- **D10 Registers and identity (P10, C2/C9).** One window of the app's I/O space (`cw_*`, 17-bit address) exactly as
  `vc20_regs.vhd`: **identity** "C264" (one ID for C16, C116 and Plus/4), contract version, capabilities (video: pixel
  layer, no index stream; mono audio; two joysticks), clock table (one PAL entry, `sys_clk` only); **config**: CTRL (reset
  hold, halt), MODEL (C16 / Plus/4), RAM_SIZE (16 KB mirrored / 64 KB), JOY_SWAP, ROM-window apertures (KERNAL, BASIC,
  function lo/hi, cart lo/hi), FB0/FB1 (DDR2 byte addresses of the two frame buffers), STATUS, FRAMES, frame-done cause;
  **page 6** forwarded to the host's pixel layer. Everything else answers as an absent device (reads 0, request toggle
  echoed: the `NO_SAMPLER` lesson, T79.1:201). `cram_*` (colour RAM peek for the freeze text) is not served (T83: 0).
- **D11 App side and swap (P10).** As T83 D11a and T84: **one application per core**, the Ultimate app built with a
  switch (`C16=1`, `build/build_c16.sh`, `doc/C16.md` in the app repo, an `#ifdef` like `VC20=1`), bundled with the partial
  as `c16.cor` (host identity in the bundle, T87). The app: checks the ID block ("C264"), loads KERNAL/BASIC (and function
  ROM for the Plus/4, optional cartridge) from the ROM directory (below) through the apertures, sets MODEL/RAM_SIZE, resets;
  menu: Reset, Model (C16 / Plus/4), RAM size, Joystick swap, drive mount (D64) and the file browser's Run/Load; **Run PRG**
  as the VC-20's D15: reset, wait, DMA the bytes into RAM at the load address ($1001 for BASIC on this machine; to be
  confirmed from the KERNAL when the app is written, together with the pointer cells and the keyboard buffer), then `RUN`
  typed into the buffer, else `SYS`; it runs the pixel layer (flip on the frame-done cause, hole punch for the menu,
  D5) and nothing C64-specific (monitor, SID/REU/sampler/UCI/turbo menus, cartridge writer, `readmem` on C64 addresses,
  peeks of $D000-$DFFF). The Ultimate overlay is the only menu. **ROM files** (the user puts them, never in the repo, CI
  or `.cor`), searched like the VC-20's (`/flash/roms/c16`, `/usb0-3/roms/c16`, `/sd/roms/c16`):

  | File (name proposed; the app accepts the usual dump names, as for the VC-20) | Size | Used for |
  |---|---:|---|
  | PAL KERNAL (character set included) | 16,384 | both models |
  | BASIC V3.5 | 16,384 | both models |
  | function ROM low (3-plus-1) | 16,384 | Plus/4 only, optional |
  | function ROM high (3-plus-1) | 16,384 | Plus/4 only, optional |
  | cartridge image `.bin` | 16,384 or 32,768 | optional, via the file browser |
  | 1541 DOS | 16,384 | the app's existing drive ROM path, not new |

  The C16Nano README names the function-ROM halves `3-plus-1.317053-01.bin` / `.317054-01.bin` (to be concatenated for its
  own loader; we load them as two files). The sizes of KERNAL/BASIC follow the 16 KB arrays of the Nano top
  (`c16nano_top.vhd:924-1000`); the KERNAL's size and the charset inside it are checked on the first load.
- **D12 Freeze (P11).** "When I choose FREEZE it MUST freeze" (Alex, 2026-10-13, T83 Q6): the app's `C64_STOP`, the JTAG
  pause and the host's halts all reach the core's `halt_req`. Hold point: **the single enable `ted_en` is gated** (the Nano's
  `clk_en`/`CORE_EN` hook exists for exactly this: every `ted.v` process, the 8501 shell `enable`, the keyport and the
  c16 latches that today run on `CLK28` ungated, `c16.v:298-301`, move into the enable domain in our top). Because TED
  counters, CPU, timers and sound state all sit behind the same enable, the freeze is exact by construction; RAM is only
  written by the CPU, the DMA window and the host port, so nothing else needs a stop; the 1541 is held by `drive_stop`
  (`C64_STOP`) as for the C64 and the VC-20. The pixel writer stops with the TED (no new words), the front buffer keeps
  being shown (the held picture, with the hole punched for the menu, D5); the dummy lock raster keeps running so the scaler
  never loses its lock (T83 learnt this the hard way: stopping the pixel stream mid-frame makes the scaler relock and the
  picture shifts). Sound: the averager outputs 0 while held. A reset takes the hold away (the menu's Reset must work while
  frozen), as T83. Proof: a pause bench with two machines as `tb_vc20_pause` (bus-cycle log equality after the release), plus a
  mutation that leaves one register ungated and must fail (S6).
- **D13 Size and timing (P12).** **Estimates, no measurement exists** (neither C16Nano nor MiSTer publishes resource
  figures). Basis and result:

  | Part | LUT (out of context) | Basis |
  |---|---:|---|
  | TED (`ted.v`, 1,922 lines: raster, DMA, 2 timers, 2 voices, 121-colour pixel generator) | 2,000-2,800 | no figure; comparable in role to the slim VIC-II (kawari, 2,117 LUT, T82 row of `TODO.md`) |
  | 8501 shell + T65 + 6529 | 500-700 | T83 estimated T65 at 400-500 LUT |
  | key translator, registers, identity, NCO, audio averager, dummy raster | 400-600 | inside the VC-20 core's 2,090 LUT (T83 results) |
  | pixel writer (RGB565 pack, 4-word FIFO, hand-off, frame-done) | 250-400 | `pix_layer.v` still-capture hand-off as precedent |
  | ROM/RAM wrappers, DMA window | 100-200 | `vc20_ram`/window |
  | `drive_l2` + `iec_bus` | 1,985 | **measured**, T83 run 746 (out of context) |
  | **Sum** | **about 5,200-7,300** | mid about 6,200 |

  Placed and routed inside `pb_l2` the VC-20 grew from 4,090 (out of context) to 5,070 LUT (x1.24, partition pins and
  route-throughs, not analysed); applied here: **about 6,500-9,000 LUT, mid about 7,700 = 43 % of `pb_l2`'s 18,000 and 56 % of
  the reference C64's 13,643** (T87 D16). FF about 3,000-4,500 (VC-20: 2,768 for 4,090 LUT). DSP 0. **BRAM 40.5 tiles** (D4):
  81 % of `pb_l2`'s 50, and **above the reference core's 39**; this is the number that can block, not the LUT (fallback 32.5
  tiles, D4). The drive's `drv_*` DDR2 client and the frame writer's client are the only DDR2 users. Timing: the TED runs on
  `sys_clk` enables at 31.5 MHz where the Nano closes it at 28.4 MHz on a slower fabric; the unknown is the combinational depth
  of the TED's address/data muxing at one enable per clock (the 9/10 enable makes consecutive enables back-to-back); the edge,
  not the logic, was T83's risk and is here (S12). The host does not change.
- **D14 TREX-internal only.** Standing rule for new cores (T83 D13, T85 Q1): sources in the private Gitea repository,
  the partial bitstream never in a release, an update package or a public download, built behind its own switch
  (`TRX_CORE=c264`, off by default) and stripped by `ci/package.sh`. The licence above would permit more; the decision does
  not depend on it.

**Open questions for Alex (one line each, not decided here):**

1. Q1: Scope: C16 only (16 KB mirrored), or the whole 264 family in one core (C16/C116 and Plus/4: 64 KB, function ROM, cartridge, +~28 BRAM tiles (12.5 against 40.5), ~0 LUT; this spec assumes the family and a MODEL register)?
   **Answered (Alex, 2026-10-13):** "b": the whole 264 family in one core (C16/C116/Plus/4, MODEL register, 64 KB, function ROM, cartridge).
2. Q2: Video: the pixel-layer path (no host change, +1 DDR2 client, menu hole punch, square 4:3 pixels, one repeated frame per 3.8 s), or wait for the host's native-video work (V1, 7-bit palette scaler, a host rebuild at 96 % slices) before building this core?
   **Answered (Alex, 2026-10-13):** "a": the host pixel layer (no host change); the core does not wait for native-video work.
3. Q3: The 1551 (the C16's parallel TCBM drive): out, with the 1541 on the serial bus as the only drive, or in as its own later step?
   **Answered (Alex, 2026-10-13):** "a": the 1551 is out; the 1541 on the serial bus is the only drive.
4. Q4: The Plus/4 SID card ($FD40, popular on these machines): out, or in later from the C64 core's SID engines?
   **Answered (Alex, 2026-10-13):** "b": the Plus/4 SID card ($FD40) comes later as its own step, from the C64 core's SID engines.

## §4 Out

- **NTSC** (Q5 of T83: PAL first); the KERNAL's `pal` bit is not acted on.
- **Real C16/Plus/4 cartridges and the TCBM/user/expansion connectors** (different connector and signals; D9); cartridge
  **images** are in (D4).
- **Cassette** (`c1530`, TAP loader, `cass_*`): needs DDR2 buffering and a UI.
- **6551 ACIA / user-port modem** ($FD00): the Nano wires it to its MCU; no pins here.
- **The 1551 and any second drive, 1581, IEEE** (Q3); the darfpga and MiSTer drives; JiffyDOS selection (the user's ROM
  files make it possible without a feature).
- **SID card** (Q4), **paddles/mouse**, light pen.
- **Pause-in-frames, Timewarp ring, trxmon, `.c64re` snapshots, UCI, REU, turbo, sampler, cartridge writer**: C64 features
  (T83 Q6). Freeze is in.
- **Analog AV out** (host gap G11), host scaler changes for TED geometry (V1), the 60 Hz modes.
- **Keys the C64 matrix cannot give:** to be listed here when the translation table is written (D7); expected: those that
  need a USB keyboard path (T85 Q6 style), which this core does not add.
- **Copying C16Nano files into this repo** before the file list of D1 is the merge gate (S1).

## §5 Decisions taken while building

- **Colour table (Alex, 2026-10-13):** C16Nano's `colors_to_rgb.v` has 116 distinct 12-bit colours, not 121; Alex: "leave it
  like that for now" (translated). A finer table is a later step if the picture asks for it.

Filled in as the core is built (2026-10-13, Claude, branch t90-c16core); each entry names what the spec left open or changed.

- **Own text for the small parts.** `c264_cpu.v` (the 8501 shell around T65), the 6529 key port (inside `c264_core.v`) and
  `c264_keys.v` are our text, written from reading `mos8501.v`, `mos6529.v`, `c16.v` and `c16_keymatrix.v`; they are not copied
  (D1 listed `mos8501.v`/`mos6529.v`). Taken unchanged by `tools/vendor_c16nano.sh` (pinned commit and SHA-256 of every file): `ted.v`,
  `colors_to_rgb.v`, `t65/*`, `LICENSE`. The C16Nano T65 differs from the VIC20Nano copy (`NMI_ack`, IRQ/NMI request signals), so
  `cores/c16nano/src/t65` is its own copy; the two cores never share a compile.
- **One enable, nine of ten clocks (D3, as specified).** `c264_nco` fires unless its 4 bit phase is 0; the pause freezes the phase.
  Measured in `tb_c264_glue`: 632,320 `sys_clk` = 569,088 enables (a PAL frame, exact). The hold is ignored while the machine is in
  reset (a reset takes the pause away, D12). `ted.v` takes the enable as `clk_en` (its processes run on `clk_en || reset`), the CPU's
  `Enable` is `cpuenable & ted_en`, the latches and the key port of `c16.v` are moved into the enable domain.
- **Memory timing without a gated RAM.** The block RAMs and ROMs read on the ungated `sys_clk` (one clock latency, as the Nano's
  Gowin blocks): the address changes only on an enabled edge, so the data is there at the next enabled edge exactly as in the
  28 MHz original (and a skipped clock only gives it more time). The RAM write is the MiSTer core's (one pulse after CAS falls)
  qualified by `ted_en`, so a write never repeats while the machine is held.
- **ROM loading: one 16 KB aperture and a select register.** The host's ROM apertures are 8 KB (KERNAL, BASIC) plus the char and
  colour areas and cannot carry six 16 KB images. `c264_win`: `dma_rom = 1` addresses ROMS_BASE + 0x0000-0x3FFF as ONE 16 KB aperture
  (the host maps BASIC to `dma_addr` 0x4000 and KERNAL to 0x2000, so the array offset is `dma_addr[13:0]`), and `ROM_SEL` (0xB0)
  names the array behind it: 0 KERNAL, 1 BASIC, 2 function low, 3 function high, 4 cartridge low, 5 cartridge high; the char and colour
  RAM apertures read 0. `dma_rom = 0` is the 64 KB RAM (what `machine:readmem` and a PRG load use), also through the JTAG port.
  `CART` (0xB1) says which cartridge halves exist (the Nano's `cartl`/`carth` flags).
- **Register map** (`c264_regs.v`, header lists it): MODEL (0xA1), RAM_SIZE (0xA2), JOY_SWAP (0xA3) are taken at the next reset (the
  core latches them while in reset); STATUS (0xA4) b1 = held; FB_LATEST (0xC0), FRAME_SEQ (0xC1), FB_OVERRUNS, FB_BASE (0xC4),
  FB_STRIDE (0xC8). Interrupt cause bit 5 = a TED frame is complete.
- **Video path as the Amiga's, not as D5 wrote it.** (1) The frame goes out through the host's general port `e_*` (proven by the
  Minimig core), not a `c_*` client. (2) **Four buffers, not two:** a flip is applied at the host's next frame start (up to 20 ms
  later) and the TED frame is 20.05 ms, so with two buffers the writer would overwrite the buffer that is still shown; with four
  the buffer being written was last shown three flips ago. `FB_BASE`/`FB_STRIDE` are registers (default 0x1D00000 and
  0x100000, the Amiga core's DDR2 block: the host's REU area). (3) **No colour-key hole punch:** Alex (2026-10-13) asked for what
  the Amiga app does, so: the menu open (Overlay) = global alpha 70 on the layer, Freeze = layer off (`PX_CTRL` off) and the overlay
  stands alone; the C64 menu code for the hole is not needed and `PX_KEY` stays off. (4) The window is 384 x 288 starting at dot
  X0 = 2 (set from the first real frame) after hblank ends and line Y0 = 3 after vblank ends (the TED's visible raster is 386 x 294); both are parameters of
  `c264_pixw`. (5) The right-edge bug of `amiga_fb` is avoided by construction: 384 dots is
  48 whole words and a line is handed over when its last word is stored (tested: all 48 words of all 288 rows, `tb_c264_pixw`).
  (6) The dot strobe sits two TED clocks after the TED has latched its colour (`ph == 2`); `ph` mirrors `phicounter[1:0]` of the TED
  (same reset value, same enable), which the core bench must assert once `ted.v` is there.
- **Sound.** The TED's 16 bit level is averaged over 656 `sys_clk`, a slow DC tracker (about 5 ms) takes the level out so a silent TED
  and a muted core give exactly 0, scaled by 1024 / 656 to 24 bit and saturated (256 / 656 before 2026-10-23, see Level). Measured: a 1 kHz square wave of +-4000 gives 40
  zero crossings in 20 ms.
  **Level (2026-10-23, after the board run of main-66eefd51: "no sound at all", Alex).** The averager was scaled for a TED level of
  +-32768, but `ted.v` runs with `GAIN = 2` (`ted.v:1877`): `dacvolume` is 32767 >> 2 at volume 8, `digi_sound` is unipolar 0..16382
  (two voices), so a Plus/4 voice at volume 8 reached only +-1.26 M of the 8.39 M full scale (voice 2 at volume 5: 0.49 M, -24 dBFS),
  against +-5 M of the VC-20 core on the same host path. The scale is now 1024 / 656 (`c264_audio.v`): one voice at volume 8 gives
  5.05 M, both 8.39 M. `tb_c264_tedsnd` drives the real `ted.v` with the CPU's writes to `$FF0E/$FF12/$FF11` and `$FF0F/$FF10`
  (the NCO as `ted_en`), through `c264_audio`: 430 Hz for 433 Hz (voice 1), 220 Hz for 216 Hz (voice 2), silent with the voice off,
  peak 5.05 M / 1.96 M; controls `TEDSND_MUT_DEAF` (digi_sound cut) and `TEDSND_MUT_QUIET` (a quarter of the level, the old scale)
  fail. What this does not prove: that the old level was the whole reason for the silence on the board; the host side (layer2
  wiring, `host/top.v` mute, the app registers: the old C16 app wrote none for audio, `c264_regs.h` has none) was read and is the same
  as the VC-20's.
- **Sound probe (2026-10-26, silence after the x4 scale).** JTAG words of the core (`trxdbg` "trx 0 <word>"): 7 = {8'd0, last CPU write to
  `$FF11`, number of TED clocks with a CPU write to `$FF0E-$FF12`}; 8 = peak `digi_sound` since reset (0 = the TED never produced a
  level; 16382 max); 9 = peak abs `audio_l` at the layer-2 port (0 with word 8 > 0 = the averager/handshake is dead; > 0 = the core
  delivers and the fault is in the host/codec path). Reading: play something with sound (`VOL 8:SOUND 1,600,600`), then read 7, 8, 9.
  Word 7 = 0 means the program never writes the sound registers on the board. Not simulated beyond a Verilator lint.
- **Joysticks.** Port A is the host's joystick 1, port B joystick 2 (swap register); the TED's last `$FF08` write selects a port
  (`c16.v`'s mapping: select bit 2 -> port A, bit 1 -> port B, up/down/left/right on k0-k3, fire on k6/k7).
- **Keys the C264 has and the C64 matrix cannot give / the reverse (the list D7 promised).** The C64 up-arrow and RESTORE have no
  C264 key and are unused; the C64's `<-` is ESC; F1/F3/F5/F7 are F1/F2/F3/HELP, SHIFT is passed through so F2/F4/F6/F8 give
  the C264's F4/F5/F6/F7; C64 CRSR down/right with SHIFT are UP/LEFT and SHIFT is removed from the C264 matrix then. The C264 has no
  key the C64 matrix cannot reach.
- **Build switch.** `TRX_CORE` is a comma list (`vc20`, `c264`, `vc20,c264`); every named module is synthesized out of context and
  implemented into the same locked static part, each with its own partial bitstream
  (`c64_basic_pb_l2_c264_partial.bit`); the VC-20 and C64 results do not change. If `cores/c16nano/src/ted.v` is not there the C264
  module is skipped with a message instead of failing the build. `ci/package.sh` strips `*c264*` as it strips `*vc20*`.
- **Whole machine, real ROMs (local benches, ROMs from the VICE data directory, never in the repository; Verilator, T65 as Verilog from
  GHDL in the container).** `sim/c264/run_boot.sh KERNAL BASIC [FUNC_LO FUNC_HI]`: ROMs written through the app's window, reset, 1.3-1.6 s of
  machine time (about 2.5 min wall). Measured: C16 (16 KB, MODEL 0) prints `COMMODORE BASIC V3.5 12277 BYTES FREE` and `READY.`; Plus/4
  (64 KB, MODEL 1, function ROM) prints `60671 BYTES FREE` and `3-PLUS-1 ON KEY F2` (S4: these are now the pinned numbers). The text screen
  is read back through the window; the DDR2 frame (PPM) shows the TED screen, light border, 320 x 200 text area. A typed key reaches the
  screen (S8); the app's Run PRG method (BASIC at `$1001`, links, pointers `$2D-$32`, `RUN` into the buffer at `$0527`/`$EF`) runs a BASIC
  program; `10 SYS4109` plus a machine code loop sets TED voice 1 (N = 0x300) and the captured stream has 347 zero crossings in 400 ms
  (433 Hz = 346): the pitch is exact (S7), silent and `halted` one clock after `halt_req`. `DIRECTORY` with nothing on the bus asserts
  ATN and ends in `?DEVICE NOT PRESENT ERROR` (the IEC out/in path, polarity and timeout; the real 1541 load stays a board test: the drive
  is the VC-20 layer's unchanged `drive_l2` on the same `iec_bus`, and the drive's VHDL is not in this bench).
- **Freeze bench (S6): `sim/c264/run_pause.sh`.** Two cores, same ROMs; B is held for 59 ms (100 ms after the reset stretch); each machine hashes
  (address, data, R/W) at every TED clock; the hashes at every millionth TED clock agree in A and B (12 checks, 12.3 M clocks), B counts no
  TED clock and its bus does not move while held. Mutation `-DC264_MUT_NOHOLD` (the hold does nothing) makes the bench FAIL (B counted
  1,674,128 clocks while held, not halted).
- **The dot phase assertion (S5, finding 6 above) holds:** `ph` equals `ted.phicounter[1:0]` on every clock of a 1.3 s run (also across the
  pause bench's holds, since both move on `ted_en | sreset`).
- **Window position from the first real frame.** The TED text screen in the 384 x 288 window had 33 border pixels on the left and 31 on the
  right and 44/44 top and bottom: X0 is now 2 (32/32), Y0 stays 3.
- **Finding: the C16Nano colour table has 116 distinct 12 bit values, not 121** (`tb_c264_colors`: 128 codes, 116 distinct at 4 bits per
  channel, RGB565 loses nothing of them). S5's "121 distinct RGB565 values" cannot be met with this table; a table with more channel
  bits would be needed (a decision for Alex, not taken here).
- **Size, estimated with Yosys (`synth_xilinx`, flattened, T65 as GHDL Verilog; not Vivado, not placed):** the core without the drive is
  about 2,690 LUT (LUT2-6), 2,000 FF, 204 CARRY4 and 42 RAMB36 (RAM 16, six ROMs 24, line buffer 2), no DSP. Add the measured drive
  (about 1,985 LUT) and the x1.24 placed factor of the VC-20: about 5,800 LUT placed (D13 estimated 7,700), and **42.5 BRAM tiles
  against `pb_l2`'s 50** (D13: 40.5; the line buffer of the frame writer is the two extra). The TED's `char_buf`/`attr_buf` became
  registers in Yosys; Vivado may put them in distributed RAM. Vivado's number (S12) replaces this.
- **Still open:** the Vivado synthesis and timing (S12, CI), the real 1541 LOAD, the board checks (photo of 4:3 centring, the layer flip
  without a seam, Freeze with the layer off), `tb_c264_windows` against the full `layer2_c264.v` (needs the drive VHDL: CI/GHDL), and the
  colour-table decision above.

- **2026-10-22, onto the generic application (Alex): "apart from trx64 and c64u, switch everything to the generic core app for now" (translated).** The
  C264 core leaves Gideon's application (`app = "ultimate"` without `app_ref`, so CI shipped the plain C64 application in `c264.cor`; the C16 machine code
  lived only on the fork branch `t90-c16app`) for a T104 descriptor, `coreapp/cores/c264.cc`; supersedes T84 D12 and the application half of D11/D12 here.
  `core.toml` says `app = "blueprint"`; `ci/trxapps.sh` copies the one image to `c264.app`. Id `C264`, folder `roms/c16`, rail title `C264`.

### 2026-10-22, what the old C16 application did, and what the generic application does now

The old application is `software/io/c16/c16.cc` (+ the shared VC20_APP gates) of the fork branch `t90-c16app`, read from the code
(`git show t90-c16app:software/io/c16/c16.cc`); `doc/C16.md` was used only to find where to look. The new one is `coreapp/cores/c264.cc`
(descriptor, operations, `c264_regs.h`) plus `coreapp/cores/cbm_run.*` (the PRG job, shared with the VIC-20) and the PRG library
`app/loaders/cbm_prg.*`. The core's registers are `cores/c264/rtl/trx/c264_regs.v`, the aperture `c264_win.v`.

| # | What the user had (old application) | Core register / path | Generic application now | State |
|---|---|---|---|---|
| 1 | Identity check: `C264` at the core window | `ID` 0x000 | `start.cc` picks the descriptor by the ID; `tools/check_core_id.py` compares the Verilog constant with the descriptor | built; lint |
| 2 | ROMs: KERNAL and BASIC (16384 each, PAL) required; function ROM low/high (the Plus/4's 3-plus-1, 16384 each) used when both are there; five file names each; `/flash/roms/c16`, `/usb0..3`, `/sd`; 0.5 s retry, message after about 10 s; ROM_SEL read back, data read back | `ROM_SEL` 0x0B0 (0 KERNAL, 1 BASIC, 2 func lo, 3 func hi), the contiguous 16 KB aperture from the host's BASIC and KERNAL halves | `roms[]` with the same names; the put operations write ROM_SEL, check it, write 16 KB at aperture offset 0, read back; the function ROMs are optional, each on its own | built, bench `test_c264`. Differences: files come from the first root that has them (not one directory for all); a lone function ROM is loaded now (the old code needed both) |
| 3 | Settings "at next reset": Model (C16 / C116, Plus/4), RAM (16 KB mirrored, 64 KB), Joystick ports (port 1 / swapped) | `MODEL` 0x0A1, `RAM_SIZE` 0x0A2, `JOY_SWAP` 0x0A3, taken by the core at its reset | three settings bound to the registers (`RegBind`, `WHEN_RESET`), written in the hold | built, bench |
| 4 | Reset C16: stop released, reset, settings written, 20 ms, unreset (the core stretches the reset to 35 ms itself); the menu closes | `C64_MODE` of the host | task "Reset" = `machine.cold_reset`, closes the menu | built, bench |
| 5 | Reload ROMs, Reset | aperture | task "Reload ROMs, reset" = `machine.reload_roms` | built, bench |
| 6 | Cartridge images: a `.bin` of 16384 or 32768 bytes, "Load as Cartridge": low half to array 4, high half to array 5, `CART` bits, model set to Plus/4, reset; the bits are written again at every settings apply | `ROM_SEL` 4/5, `CART` 0x0B1 | file type `.bin`: "Load as cartridge (Plus/4)" (`c264.cart_load`): arrays, `CART`, the Model setting set to Plus/4, reset; media slot "Cartridge" shows the file; new: task "Eject cartridge and reset" (`c264.cart_eject`: `CART` = 0 and reset) | built, bench. `CART` is written once at load (nothing in the core clears it: `c264_regs.v` writes it only from the bus), not at every reset. The eject task is new, the old application had no way out short of restarting the core. Choosing the cartridge changes the saved Model setting, as the old one changed its config value |
| 7 | Run PRG (`.prg`, `.p00`): reset with the settings, wait 2.5 s, BASIC at $1001 relinked to the start pointer ($2B, window $0400-$2000), end pointers $2D/$2F/$31, `RUN` into $0527 / count $EF; otherwise bytes + `SYS<addr>`; with 16 KB a program past $3FFF is refused with a message | RAM window | "Run PRG" through the PRG library and the shared job (waits for READY: start pointer = $1001 after at least 2.2 s of machine time plus 0.3 s); the 16 KB refusal reads the RAM setting (a program past $3FFF is refused before anything is touched, for Run and for Load) | built, bench; times not measured on a board |
| 8 | Load / DMA of a PRG (no reset, nothing typed) | RAM window | "Load PRG (no reset, no start)" | built, bench |
| 9 | Picture: four 384 x 288 RGB565 buffers at DDR2 0x01D00000 + n x 0x100000 (cleared black), `FB_BASE`/`FB_STRIDE` written, the pixel layer 384 x 288 at scale 3 (1152 x 864) at (384, 108), a flip to `FB_LATEST` whenever `FRAME_SEQ` changes | `FB_LATEST` 0x0C0, `FRAME_SEQ` 0x0C1, `FB_BASE` 0x0C4, `FB_STRIDE` 0x0C8, pixel layer page 6 | `VideoDesc` { 384, 288, 3, 384, 108, 0x01D00000, 0x100000, 4, ... } served by the base's video service (it also moves the layer with the HDMI "Picture" item) | built, bench: registers, layer size/position/control, flip |
| 10 | Freeze: Interface Type = Freeze stops the machine and switches the layer off, waits up to 200 ms for STATUS b1; Overlay: machine runs, layer at alpha 70 | `STATUS` 0x0A4 b1 | Freeze only (descriptor `freeze` = STATUS, mask 0x02, 200 ms); the base hides the layer for the menu (T93 Q1, T104 5.3) | built; Overlay mode deliberately not carried |
| 11 | 1541 drive A: `.d64`/`.g64` mount, Mount & Run, drive ROM, Drive A settings, Create | `drv_io_*`, `gcr_io_*` pages (`drive_l2` in `layer2_c264.v`), the serial bus | the `cbm1541` library (T104.1 5.7, details in T83 section 5 row 9): the same file actions (also Browse Disk, Mount & Run and Real Run of a PRG inside an image, the name prompt of Create / Save: T104.1 5.7.1), settings, tasks, media slot and ROM (`roms/c16/1541.rom`) | **built, bench** (`test_cbm1541` on the shared library; `test_c264` types a script), branch `t104-cbm1541`, not on a board |
| 12 | Joystick/keyboard: the host's matrix and two joystick signals into the TED's key lines, the swap above | gateware (`c264_core.v` `swap_s`) | the swap is setting 3; the rest is gateware | built |
| 13 | REST `machine:reset/reboot/readmem/writemem/menu_button`, `runners:run_prg/load_prg`; the Ultimate menu's network, clock and services | - | the common menu; settings in `<name>.cfg`; REST/WS adapters are T104.1 D3 and not built for any core (the operations exist by name: `c264.run_prg`, `c264.load_prg`, `c264.cart_load`, `machine.*`) | menu built; REST **open** |

Not offered by the old application either, so not built: the 1551, the SID card of the Plus/4, the user port, the cassette port, NTSC (T90 §4).
Settings keep their text but live in a new store (`<name>.cfg`, section "C16 Settings"); a value in the old application's flash store is not read.

**Tests** (`tests/coreapp/test_c264.cc`, `run.sh`, 25 controls, every one fails as it must): KERNAL/BASIC/function ROMs into the right arrays through
ROM_SEL (with a core that takes no write, and one whose ROM_SEL does not hold), the settings written only in the hold and taken at the release, the
picture's registers and layer and a flip, Freeze's PAUSED bit, Run on BASIC / machine code / `.p00`, the 16 KB refusal and its absence at 64 KB,
a machine that never reaches READY, Load without reset, cartridge 16 and 32 KB (arrays, `CART`, model, reset), a wrong size refused untouched, eject,
the golden menu `menu_c264.txt`. Not run: the RISC-V build (CI `trxapps`), the board.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->

### 2026-10-23, the defaults on the board (Alex)

Alex, on the board with the C264 core: "Then let's set Plus/4 and 64 KB" (translated). `coreapp/cores/c264.cc`: Model (at next reset)
defaults to Plus/4 and RAM (at next reset) to 64 KB (index 1 each); the joystick swap stays 0. A settings file that already holds a value
keeps it. The 16 KB refusal of a long PRG (`refuse_wrap`) now bites only after the user chose 16 KB. His report "Reset does not work" is
answered in T104 section 5.13: the task's code is the one Run Disk uses, no defect found; the next board log decides.
