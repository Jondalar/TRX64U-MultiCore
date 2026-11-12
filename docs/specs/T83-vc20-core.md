# T83 — A VIC-20 core, ported from VIC20Nano, runs as the second core in the T79 host/core split

**Status:** DONE (Alex, 2026-10-23: "ja, done"). Proposed 2026-10-12.
**Repo:** TRX64-Ultimate (gateware under a new `designs/vc20/`, app side, tools). The source is read from
outside (MiSTle-Dev/VIC20Nano); nothing is copied into this repo by this spec.
**Number:** T83.
**Milestone:** M2.
**Superseded in part (2026-10-23):** the picture path. Since T119 the picture is written into the DDR2 frame buffer of the pixel layer
(`vc20_pixw.vhd`); `pix_*` is only the idle raster the scaler locks to, the nearest-C64-colour mapping and `CTRL` bit 2 are gone, and the
text below about the scaler path (§5 row 12, the 1.775-dot aspect, the colour mapping) describes the state before T119.
**Origin:** Alex, 2026-10-12: a VIC-20 (VC-20) core as the second core behind the T79 host; "spec first, no
code"; source decided by Alex: `https://github.com/MiSTle-Dev/VIC20Nano` (the Tang Nano VIC-20 core).
**Depends on:** T79 (host/core split, contract `T79.1-host-core-contract.md`, PoC 4 = swap works), T81
(core selector app loads the partial bitstream). Floorplan B (`pb_l2`) must stay as measured.

Adopted 2026-10-12 from the C64RE spec template (`C64ReverseEngineeringMCP/specs/_TEMPLATE.md`,
Alex: "we use that from now on"). Older specs keep their own layout.

File:line citations of `T79.1-host-core-contract.md` (`C`), `T79-dfx-trial.md`, `layer2.v`, `build.tcl` and
`vic_scaler.v` refer to the dfx-trial worktree (`TRX64-Ultimate-wt-dfx`, PoC 4 build 23f00df); line numbers
of VIC20Nano files are from its `main` on 2026-10-12.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to be a host for more than one machine
(T79.1:7: "Layer 2 (a core: TRX64, later VC20, Minimig, ...)").

**What he wants:** pick "VIC-20" in the core selector (T81), the board swaps layer 2 by partial
reconfiguration, and a VIC-20 boots to its READY screen with keyboard, picture, sound and a 1541.

**Today:** the swap works end to end with exactly one core (T79.1:217, PoC 4: "the new core boots to
READY"; open there: "a second, different core"). The edge is still the TRX64 edge: 228 ports, 2,180 bits
(T79.1:173), and its video port is VIC-II shaped (`pix_color[3:0]`, `pix_x[9:0]`, `pix_y[8:0]`,
`layer2.v:148-150`; the scaler stores 64 raster lines of a 400 x 270 VIC-II window, `vic_scaler.v:2-27`).
No second core has ever been built against it, so the contract is untested on everything that is not a C64.

**What the source is.** VIC20Nano (Sipeed Tang boards, Gowin FPGAs) is the MiSTer/MiST VIC-20 lineage in
VHDL: `vic20.vhd` (top of the machine), `m6561.vhd` (VIC), `m6522.vhd` (VIAs), T65 (6502, git submodule
from `mist-devel/T65`), `vic20_keyboard.vhd`, `vic20_clocks.vhd`, plus a darfpga 1541 (`src/c1541/`), tape
(`c1530.vhd`), MegaCart (`megacart.v`), and, board-specific, HDMI, OSD, SDRAM, SD card, MCU link and Gowin
IP. It has not been built or simulated by us.

**Licence (read from the files, not from the README):**

| File(s) | Terms as written in the file header |
|---|---|
| repo `LICENSE` | GPL-3.0 (35,149 bytes; GitHub reports `GPL-3.0`); README: "Released under GPL-3.0" |
| `m6561.vhd`, `vic20.vhd`, `vic20_clocks.vhd` | MikeJ (2003-2009) and WoS (2011-2015), BSD-3-clause wording **plus**: "any commercial use is forbidden as well" and "**This code must be run on Replay hardware only.**" |
| `m6522.vhd` | MikeJ (2003), BSD-3-clause wording, no extra clause |
| `c1541/via6522.vhd` | "GPL 3.0" (Gideon Zweijtzer) |
| `megacart.v` | GPL v2 or later (Alastair M. Robinson, MiSTer changes by Alexey Melnikov) |
| `misc/scandoubler.v` | GPL v3 or later (Till Harbaum) |
| `hdmi/*.sv` | Sameer Puri's HDMI core, modified (licence not read; the host has its own encoder, not needed) |
| `c1541/c1541_logic.vhd`, `gcr_floppy.vhd`, `c1530.vhd`, T65 | no licence text in the header (darfpga, Mark McDougall, Stefan Voss; T65 header is a change log); not read further |

The README's "GPL-3.0" does not reach the three files that matter most: the Replay-only and no-commercial-use
clauses are additional restrictions and are not GPL-compatible (GPLv3 §10 forbids them). Our project is GPL.
The BSD-style part (`m6522.vhd`, the wording of the others without the extra clause) is GPL-compatible. The
MiSTer VIC20 core (VHDL, same MikeJ/WoS origin) is expected to carry the same files; not verified.

**Why it matters:** the VIC (`m6561.vhd`), the machine top (`vic20.vhd`) and the clock/reset generator
(`vic20_clocks.vhd`) are exactly what a VIC-20 core cannot do without; a port that merges them into a GPL
tree is a licence breach until that is settled. A second reason it matters: the first non-C64 core is the
test of the whole contract (clocks, memory, video geometry, input, identity).

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms, what is blocked until the licence is settled.
- **P2 Porting surface.** Which parts of VIC20Nano are kept, which are replaced by host services, which
  Gowin-specific primitives must be replaced for the Artix-7.
- **P3 Clocks and reset** (contract C1, `C:88-90`).
- **P4 Memory and ROMs** (C3 and the app's ROM path).
- **P5 Video** (C4, `C:100-107`).
- **P6 Audio** (C5, `C:109-111`).
- **P7 Input** (C6, `C:113-115`): keyboard, joysticks, RESTORE, paddles.
- **P8 Serial bus and drive** (IEC, the 1541).
- **P9 Connector pins and the rest of the machine's I/O** (C7, `C:117-119`): expansion port, user port,
  cassette.
- **P10 Registers and identity** (C2, C9, `C:92-94`, `C:125-127`): what the app's windows get answered.
- **P11 App side and swap** (T81, Ultimate app): what loads the core, what the app must not do with it.
- **P12 Size and timing** against `pb_l2`.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the licence is settled before any file is merged (P1)
  Given VIC20Nano's m6561.vhd, vic20.vhd and vic20_clocks.vhd carry "Replay hardware only"
  When  gateware derived from them is about to be committed under designs/vc20/
  Then  the licence route chosen by Alex (open question Q1) is written into this spec's §3
  And   no file with the Replay-only clause is committed before that entry exists

Scenario: S2 the core elaborates on Xilinx without Gowin primitives (P2)
  Given the kept VIC20Nano modules, inferred RAM/ROM, the Gowin PLL/CLKDIV/DCS/IP wrappers removed
  When  the design is synthesised out of context for xc7a50t
  Then  Vivado reports no black box and no Gowin cell, and the kept modules simulate the PAL boot to READY
  And   the TRX64 build is unchanged (same LUT, timing met)

Scenario: S3 clocks are exact (P3)
  Given a PAL identity entry (core clock 8 x 4.43361875 MHz = 35.46895 MHz) and an NTSC entry (8 x 4.090909 MHz)
  When  the app programs the host MMCM over DRP and releases core_rst
  Then  the ena_4 strobe measured on the board by the ring counter is within 20 ppm of 4.43361875 MHz (PAL)
  And   the C64 entry still gives the C64 clocks after the swap back

Scenario: S4 RAM and ROM come from the app, not from the bitstream (P4)
  Given a layer 2 with no ROM contents baked in
  When  the app writes BASIC, KERNAL (PAL or NTSC) and char ROM through the ROM window, then resets
  Then  the screen shows "CBM BASIC V2" / "3583 BYTES FREE" (unexpanded) from those ROMs
  And   with the 35K configuration selected the free count reads "38911 BYTES FREE"-class values, never the C64's

Scenario: S5 the picture is straight and frame-locked (P5)
  Given the VIC-20 identity states its raster (visible area, line count, frame rate)
  When  the host scaler receives the VIC-20 index stream
  Then  the picture is centred with no tearing or rolling at 50 Hz (PAL), the Ultimate overlay opens over it
  And   the C64 core's picture after the swap back is pixel-identical to before the first swap

Scenario: S6 sound comes out (P6)
  Given the VIC-20 sound registers $900A-$900D set to a tone
  When  the core delivers samples to the host mixer
  Then  the tone is audible on HDMI/line out at the right pitch (frequency counted from the captured stream)
  And   no TRX64 SID volume or filter setting changes it

Scenario: S7 keyboard and joystick work (P7)
  Given the host's key matrix and joystick state
  When  Alex types a BASIC line with SHIFT, CBM and the cursor keys, presses RESTORE, moves the joystick
  Then  the characters appear as typed (the C64-matrix to VIC-20-matrix table is right for every key)
  And   RUN/STOP-RESTORE returns to READY, joystick bits read right in PEEK(37151)

Scenario: S8 LOAD from the 1541 (P8)
  Given the existing drive (drive_unit in layer 2) with a D64 mounted by the app
  When  Alex types LOAD "$",8 and LIST, then LOAD "*",8,1 and RUN
  Then  the directory lists and the program runs
  And   IEC lines are released (no ATN/CLK/DATA held) while the core is not running

Scenario: S9 the port is idle and a cartridge image works (P9)
  Given the VIC-20 core running, a cartridge image (8K at $A000) chosen in the app
  When  the expansion port pins are probed (all oe off, transceivers disabled) and a C64 cartridge is plugged
  Then  the core drives nothing on the slot, no plugged cartridge changes what the VIC-20 shows, and the image starts (autostart or SYS)
  And   nothing in the core answers CIA-style or SID accesses (no C64 device appears in the VIC-20's map)

Scenario: S10 the app's windows answer without hanging (P10)
  Given the VIC-20 core and the Ultimate app (layer 3) running
  When  the app reads the identity block and then touches every window it knows (r_drive, r_gcr, cw, dma, port, sid, reu, sampler, uci)
  Then  identity reads "VC20" with the contract version; windows the core has not answer "absent" (reads 0, ack echoed)
  And   no I/O read stalls the RISC-V (the NO_SAMPLER lesson, T79.1:201)

Scenario: S11 swap C64 -> VIC-20 -> C64 on the board (P11)
  Given the C64 core running with a program on screen
  When  the core selector (T81) picks VIC-20, then later TRX64 again
  Then  Alex's photos show the C64 screen, the VIC-20 READY screen, the C64 screen; REST answers throughout
  And   the Ultimate app does not offer or run C64-only actions (SID, REU, cartridge writer) while VIC-20 is loaded; Freeze stops the VIC-20 (Q6)

Scenario: S12 the core fits pb_l2 and closes timing (P12)
  Given floorplan B (pb_l2: 18,000 LUT, 50 RAMB36-equivalent tiles)
  When  the VIC-20 partial bitstream is routed against the unchanged host
  Then  WNS >= 0 and the report lists the measured LUT/BRAM, which replace the estimate in §3 D12
  And   the host (layer 1) is not rebuilt for the VIC-20
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Licence settled before merging | review gate: entry in §3 before first `designs/vc20/` commit | review |
| S2 | P2 | Elaborates on Xilinx, boots in sim | `designs/vc20/sim/boot_ready` (new, GHDL or Verilator on the VHDL; ROMs supplied locally, not checked in) → `ci/sims.txt` row | CI `sims` |
| S3 | P3 | Exact clocks | `designs/vc20/sim/clk_ratio` (ena_4 / frame counts); board: ring counter reading by hand | CI `sims`, board |
| S4 | P4 | ROMs from the app, memory map | `sim/boot_ready` with ROMs loaded through the window model; board by REST | CI `sims`, board |
| S5 | P5 | Picture, frame lock, overlay, C64 unchanged | `sim/video_frames` (line/frame counts into the scaler model); board photos | CI, board |
| S6 | P6 | Sound | `sim/audio_tone` (frequency from samples) | CI `sims` |
| S7 | P7 | Keyboard/joystick/RESTORE | `sim/keys` (matrix table, every key); board by hand | CI, board |
| S8 | P8 | LOAD from 1541 | board with a D64 (third-party media, run by hand, nothing checked in) | board |
| S9 | P9 | Idle pins, no C64 devices | `sim/pins_idle`; board probe | CI, board |
| S10 | P10 | Windows and identity | `sim/windows` (every window of `layer2.v` answers or reads absent) + app run on the board | CI, board |
| S11 | P11 | Swap and back, app guard | board by hand (Alex's photos); app guard by REST script | board |
| S12 | P12 | Fit and timing | Gitea CI `pipeline.yml` build stages, reports | CI build |

Third-party ROMs (Commodore BASIC/KERNAL/char) and test media are supplied locally, never checked in.

## §3 Decision

Facts first, then D1-D12. Each D names its part.

**VIC20Nano structure (what the port starts from).** `vic20.vhd` has one 8.867 MHz-class system clock
`i_sysclk` with an enable `i_sysclk_en` ("8.867236 MHz enable signal", PAL crystal), a 4.43 MHz `ena_4` made
by `vic20_clocks.vhd` (a divide-by-two of the enable plus a 4,096-clock reset delay) and a 1.1 MHz CPU
enable derived inside `m6561.vhd`. Inside: T65, `m6561` (PAL 284 clocks x 312 lines, NTSC 260 x 261, 4-bit R/G/B
out, `o_hsync/o_vsync/o_hblank/o_vblank/o_ce_pix`, 6-bit mono `o_audio`), two `m6522` VIAs, `vic20_keyboard`
(8x8 matrix), five RAM-expansion blocks (`ramex0-3` 8K each at $0400/$2000/$4000/$6000, `cart` 8K at $A000)
plus 1K + 2 x 2K internal RAM and 1K x 4 colour RAM, all as `ram_conf_*` BRAM wrappers, a hook for external
memory (`o_extmem_*`, MegaCart), IEC (`atn_o/clk_o/data_o/clk_i/data_i`, open drain, double-registered inputs),
cassette (`cass_*`), user port (`user_port_*`), paddle inputs `i_potx/i_poty`. ROMs: char and BASIC as Gowin
`pROM` IP (contents inside the IP), KERNAL as a loadable Gowin SDPB RAM written from the SD card by the MCU
(`conf_clk/conf_wr/conf_ai/conf_di`, `rom_std`). The Tang top (`vic20nano_top_tp25k.vhd`) adds a Gowin PLL
pair for PAL and NTSC (core/pixel 35.625 MHz PAL, 33.125 MHz NTSC on the 25k board; 35.775/32.94 MHz on
the 20k), a `DCS` clock switch, `CLKDIV`, 32 MHz SDRAM access for tape and MegaCart, `c1541_sd` (D64 from raw
SD via `mist_sd_card`), HDMI TMDS with 5x serialiser, OSD, a BL616/RP2040/ESP32 MCU for USB HID and menus.
No resource figures are published in the repo or README.

- **D1 Source: VIC20Nano (Alex's decision, 2026-10-12).** Read-only study; the port is our own `designs/vc20/`
  that takes the machine logic and replaces everything board-specific. Other VIC-20 cores are not alternatives.
  *Licence handling:* the three files with the Replay-only/no-commercial clause (`m6561`, `vic20`,
  `vic20_clocks`) are **not merged** until the route is chosen (Q1). `m6522.vhd` (BSD wording) and Gideon's
  `via6522.vhd` (GPL 3.0) can be used under GPL; T65, `c1541_*`, `gcr_floppy`, `c1530` have no licence text and
  must be clarified before use; we do not need the last four (D8, D9). Each kept file keeps its copyright header.
- **D2 Porting surface (P2).** *Kept as machine logic:* T65, M6561, 2 x M6522, `vic20_keyboard` (rewritten to
  read the host matrix, D7), the RAM/ROM decode in `vic20.vhd`. *Replaced by host services:* HDMI, OSD,
  scandoubler/`video.v`, SD card and loader, MCU/HID/`sysctrl`, LED/WS2812, PLLs, DCS, DualShock. *Replaced by
  C2/C3:* ROM and image loading (D4). *Gowin-specific, must be replaced for Artix-7:* `rPLL`/`PLL_INIT`/`DCS`/
  `CLKDIV` (clocks come from the host MMCM, D3); `Gowin_pROM_char/basic`, `Gowin_SDPB_kernal_8k*`,
  `Gowin_SP_*`, `Gowin_DPB_*` (generated IP wrappers with `.ipc` files; become inferred RAM/ROM with
  `(* ram_style = "block" *)`, initialised empty and filled by the app); `FIFO_SC_HS` (tape FIFO; out, D9);
  `flash_dspi*`, `sdram*` and `psram_controller` (board memory; out). The `ram_conf_*` wrappers are plain
  VHDL RAMs with a config write port and can be mapped to inferred BRAM. This list is what the first
  elaboration (S2) must confirm; any file not read in detail (`hid.v`, `sysctrl.v`) is dropped with the board part.
- **D3 Clocks (P3, C1).** Per contract C1 (`C:88-90`, 2a decision 3 at `C:42`) the core asks for its
  frequencies in the identity block and the app programs the host MMCM before releasing reset. VIC-20 asks for
  **one core clock = 8 x the VIC dot enable**: PAL 35.46895 MHz (4.43361875 x 8; the PAL crystal 8.867236 MHz / 2,
  `vic20.vhd` header comment), NTSC 32.727273 MHz (4.090909 x 8; 14.31818 MHz / 3.5). `ena_4` is a 1-in-8
  enable (4.436 MHz PAL), the CPU enable is `ena_4` / 4 (about 1.108 MHz PAL, 1.023 MHz NTSC). The second core
  clock (TRX64's `sid2x`, `layer2.v:171`) is unused. Exact clocks fix the Nano's rounding (35.625 vs 35.469
  MHz, +0.44 %, on its 25k PLL). `sys_clk` (register bus, `layer2.v:207`) is unchanged. MMCM settings
  (VCO, dividers) to be computed against the host's MMCM range; the 12-clock-region limit where host and core
  clocks meet (`C:197`, run 692) applies. Reset: `core_rst` stretched by the core's own 4,096-clock delay.
- **D4 Memory and ROMs (P4).** RAM in **block RAM inside `pb_l2`**: 1K + 2 x 2K internal + 1K x 4 colour RAM
  plus up to 35K expansion = about 40.5 KB of RAM. The DDR2 ports (C3, `C:96-98`) are **not used by the base
  core** (a 35K RAM does not need 128-bit DDR2 bursts and the VIC-20's 1.1 MHz bus is the wrong user for a
  latency-bound port); they stay idle and answer "no request" so the host window check never sees an access.
  RAM expansion configuration (3K/8K/16K/24K/32K/35K, read-only regions as in `i_ram_ext`/`i_ram_ext_ro`) is
  selected by a C2 register and takes effect at the next reset. ROMs: BASIC 8K, char 4K, KERNAL 8K (PAL and
  NTSC separately, as in the Nano) are **written by the app** through a ROM window the same way the C64
  ROMs are (the DMA/ROM window `dma_*`, `port_*`, `dma_rom` in `layer2.v:94-101,151-155`; `win_port_*` at
  `layer2.v:229-235` is what the TRX64 core answers). No ROM bytes in the bitstream: the partial bitstream
  carries no Commodore code and is shippable. A cartridge image for the $A000 block (8K, `ramex` "cart") is
  loaded the same way (Q4 on larger carts).
- **D5 Video (P5, C4).** *Superseded in its first stage by the Q3 answer: the VIC-20 core first converts its picture to the VIC-II frame the scaler reads today, the host stays unchanged; the host-side geometry below follows later.* The core drives the existing C4 shape with a **4-bit colour index** (the VIC-20
  VIC has 16 colours; `pix_color[3:0]`, `layer2.v:148`) and its own raster counters (`pix_x`/`pix_y`, 10/9
  bits are enough for 284 x 312). The Nano's VIC emits 4-bit RGB; our M6561 variant must expose the index
  (to verify in the source; otherwise a 16-entry reverse map). The host needs three things it does not have:
  (1) the **visible window and scale per core** instead of the compile-time VIC-II window
  (`vic_scaler.v` parameters `X0=96, Y0=16`, 400 x 270 at 4x, `vic_scaler.v:2-27`): geometry (first visible
  pixel/line, width, height) and scale factor published in the identity block (contract: "states geometry and
  frame rate in its identity block", `C:40`); (2) the **lock ratio** from the core's frame period: PAL
  VIC-20 = 284 x 312 = 88,608 `ena_4` clocks = 50.04 Hz, NTSC = 260 x 261 = 67,860 = 60.3 Hz (computed from
  `m6561.vhd` constants), against the scaler's hard-coded `6875/1456` of `dot4x` (`vic_scaler.v:7-9`); (3) a
  **16-entry palette per core** (the VIC-20 colours differ from the VIC-II's). Border and `hblank/vblank`
  from the Nano's VIC are not needed; the scaler windows by raster position. Host scaler changes are layer 1
  work and are **a rebuild of the host**, so they belong to this spec's host part (D11), not to the core.
  The C64 text layer (freeze menu) does not exist in the VIC-20 core.
- **D6 Audio (P6, C5).** The VIC's 6-bit mono `o_audio` is low-pass filtered (the Nano has `lp_output`/
  `lp_filtered`; our own filter, not a copy) and delivered as `audio_l = audio_r` 24-bit signed with
  `audio_sample_en` (`layer2.v:41-43`) at the contract rate; the host mixer (C5, `C:109-111`) resamples,
  mixes with the app's own sources and outputs. No SID: `sid_*` inputs ignored.
- **D7 Input (P7, C6).** The host delivers `keys[63:0]` as the physical C64U keyboard wires it
  (`c64_keys`, `layer2.v:55`, `C:115`), `joy1/joy2[4:0]` (`layer2.v:53-54`) and `restore` (`layer2.v:57`).
  The VIC-20 matrix is a different 8x8 map; the core carries a **64-entry translation table** from the host
  matrix position to the VIC-20 keyboard matrix, derived from the physical keys (both machines print their
  legends on the same physical keyboard positions only approximately; the table is a design artefact of this
  spec to be written from the VIC-20 matrix and verified with S7). Joystick: VIA1 PA2-PA5 and VIA2 PB7 (fire) as
  in the Nano; port 1 only (the VIC-20 has one joystick port). RESTORE drives VIA2 CA1 / NMI. Paddles: the
  Nano takes `i_potx/i_poty`; idle (no paddles) in this spec (T47 paddles are C64 SID/CIA specifics).
- **D8 Serial bus and drive (P8).** Per decision 5 (`C:46`) the host drives the open-collector IEC lines and
  hands inputs over filtered (`layer2.v:8-13`, core side `c64_atn_o`, `c64_clk_o`, `c64_data_o` at
  `layer2.v:45-48`, `iec_port_off` `layer2.v:142`); the wired-AND and the protocol stay in the core. The
  VIC-20's serial bus is on VIA1/VIA2 (Nano: `atn_o/clk_o/data_o/clk_i/data_i`), wired to the same five
  signals; SRQ unused. **Drive: reuse the drive that is in layer 2 today** (`drive_l2.vhd`: `drive_unit` and
  `gcr_codec`, `drive_l2.vhd:108-117`) with its `drv_io_*` and `gcr_io_*` windows and its DDR2 client
  (`drv_*`, `layer2.v:104-125`). The drive is core-agnostic (it sees IEC and its own ticks). Not used:
  the Nano's darfpga `c1541_*`/`mist_sd_card` (SD-based, licence text absent). Measured cost of the drive in
  the TRX64 core is about 1,690 LUT (layer 2 14,036 LUT with it, 12,345 without: runs 722 and 718,
  `C:196,200`); the same block is reused here, so no new measurement is needed for it (Q2).
- **D9 Connector pins and other I/O (P9, C7).** Real VIC-20 cartridges **cannot** work on the C64U's port,
  with or without a passive adapter; only cartridge **images** loaded by the app (D4, RAM-block $A000 and
  the other blocks) work. Basis (VIC-20 pinout from c64-wiki.de "Expansionsport VC20": 44 contacts, 3.96 mm
  pitch; the C64U side is what the core can see at the edge, `layer2.v:14-30,185-189`):

  | VIC-20 signal (pins) | C64U port signal available to the core | Mappable? |
  |---|---|---|
  | D0-D7 (2-9) | `SLOT_DATA`, `slot_d_o`, `slot_d_t` (bidirectional) | yes, by an adapter that moves pins |
  | A0-A13 (B-S) | `slot_a[15:0]` (outputs, `slot_ahi_t` for the high byte) | yes, by an adapter |
  | I/O2, I/O3 (T, U) | `SLOT_IO1n`, `SLOT_IO2n` (outputs) | I/O2 yes; I/O3 only by sacrificing IO1n |
  | BLK1, BLK2, BLK3, BLK5 (10-13) | `SLOT_ROMLn`, `SLOT_ROMHn` (outputs); `slot_a[14]`/`slot_a[15]` | at most 4 outputs if A14, A15, ROMLn, ROMHn are spent on them; no clean fit |
  | RAM1, RAM2, RAM3 (14-16) | no spare output (`SLOT_BA`, `SLOT_DOTCLK` are the only free outputs) | at most 2 of 3 |
  | V R/W, C R/W (17, 18) | one `slot_rw` | one of two (VIC read/write has no source) |
  | IRQ (19), NMI (W) | `SLOT_IRQn`, `SLOT_NMIn` (inputs) | yes |
  | SΦ2 (V) | `SLOT_PHI2` (output) | yes, but at 1.108/1.018 MHz from the core's own divider |
  | RESET (X) | `SLOT_RST_I` in, `slot_rst_o` out | yes |
  | +5V, GND | board rails | yes |
  | NC (20, Y) | | n/a |

  Outputs the VIC-20 needs: 14 address, 4 BLK, 3 RAM, 2 I/O, 2 R/W, SΦ2, RESET = 27; outputs the C64U
  port offers: 16 address, ROML, ROMH, IO1, IO2, BA, DOTCLK, PHI2, RW = 24 (`GAME`/`EXROM`/`DMA` are inputs
  only). 27 > 24, and an adapter can only swap pin positions, not add outputs; no passive adapter
  gives a cartridge all of its selects (and the transceivers' directions are fixed by the board). The
  pin positions also differ between the machines (the page notes the layout is not the C64's), so every
  signal needs an adapter wire anyway. Decision: all `SLOT_*` outputs idle (`oe` off, transceiver
  direction/enable disabled, `layer2.v:14-30`) while the VIC-20 core runs; nothing is sampled from the
  slot; the host already holds a safe idle while no core runs (`C:119`). User port (VIA1, RS-232 VIC-1011),
  cassette (`cass_*`, `c1530` + FIFO) are not mapped to pins (§4); MegaCart (`megacart.v`) has no pins either but is wanted in M2 as a DDR2-backed image (Q4).
  The core's `o_io2_sel`/`o_io3_sel`/`o_blk*_sel` expansion selects are internal only, driving the RAM/
  cartridge-image blocks of D4. Not checked: the board's XDC for further spare FPGA pins on the port.
- **D10 Registers and identity (P10, C2/C9).** One window of the app's I/O space (`cw_*`, 17-bit address,
  `layer2.v:75-81`) plus the app's other windows the same way (`drv_io_*`, `gcr_io_*`, `dma_*`, `port_*`,
  `cram_*`, `sid_*`, `reu_*`, `smp_*`, `uci_*`, `turbo_*`). The VIC-20 core answers: **identity** (`core_id`
  "VC20", contract version, capabilities: video index, mono audio, one joystick; clock table PAL/NTSC;
  geometry/frame period for D5); **ROM window** (D4); **config register** (RAM expansion, PAL/NTSC, reset
  request, drive windows forwarded to `drive_unit`); **`cram_*`**: the VIC-20's colour RAM at $9400 for the app's
  screen-RAM peek (`cram_addr[9:0]`/`cram_rdata[3:0]`, `layer2.v:73-74`) works unchanged. **Everything else
  answers as an absent device:** reads return 0, the request toggle is echoed (`C:201`: a stub that never
  answers stalled the RISC-V for ever), so no window stalls; build switches are
  *not* needed here since these blocks do not exist in the VIC-20 core. Interrupts `irq[3:0]` unused.
- **D11 App side and swap (P11).** *Later decided (T81, T84): a `.cor` bundle carries the VIC-20 application, which initialises its own core; the Ultimate app does not run with a VIC-20 core.* T81's core selector loads the partial bitstream and releases the
  decouple register as for TRX64 (`docs/todo/T81.md`: "sets the host's decouple register `core_decouple`,
  loads the partial bitstream of layer 2 and releases the register"). The Ultimate app is C64-centric: while a
  core whose identity is not a C64 family core is loaded the app must **not** offer or run: the
  monitor (it pokes C64 RAM through `dma_*`; Freeze is offered, Q6 changed), SID/sampler/UCI/REU/turbo menus and REST calls, the cartridge
  menu and writer, `machine:readmem`/`writemem` on C64 addresses (VIC-20 RAM is in its own map; the known
  post-swap read hole, `C:217`, must be fixed first or the VIC-20 window sees the same), and any peek of C64
  I/O ($D000-$DFFF). It offers: reset, D64 mount/run for the drive, ROM and RAM-expansion selection, ROM
  load at core start, keyboard injection. The gate is the identity block (`C:125-127`), not a compile-time
  list. **Host changes** this spec causes (layer 1, one host rebuild): scaler geometry/ratio/palette
  parameters from identity (D5), the identity read in the app loader, and nothing else on the edge. Whether
  the host rebuild is acceptable at all or the scaler is made core-specific is Q3.
- **D12 Size and timing (P12).** **Estimate, no measurement exists** (VIC20Nano publishes no resource
  figures). Basis: T65 about 400-500 LUT, two M6522 about 300 LUT each, M6561 (PAL/NTSC counters, 4
  voices, colour/char fetch) about 600-900 LUT, glue (decode, keyboard table, audio filter, register block,
  identity) about 400-600 LUT, the 1541 about 1,690 LUT measured in our TRX64 core (D8). **About 3,500 to 4,500
  LUT** against `pb_l2`'s 18,000 LUT (`build.tcl:260`, SLICE_X0Y0:X55Y99 + X0Y100:X9Y149); TRX64's layer 2 is
  14,036 LUT (`C:200`, run 722). BRAM: about 40.5 KB RAM + 4 KB char + 8 KB BASIC + 16 KB KERNAL (both
  standards) is about 69 KB of data, about 17-20 RAMB36 for the core (4 KB/tile at x8) plus the drive's
  buffers; against `pb_l2`'s 50 tiles (`build.tcl:261`, RAMB36_X0Y0:Y29 + X1Y0:Y19, as counted in `C:197`),
  roughly 40 %. Timing: 35.5 MHz core clock against TRX64's 31.5 MHz-class clock; the edge, not the logic, is
  the risk (S12). The scaler change (D5) adds a few LUT/BRAM to layer 1, which has about 220 slices left
  (`C:183`, run 712 + later moves; layer 1 at 72 % LUT in run 722): to be measured in the host rebuild.

**Open questions for Alex (one line each, not decided here):**

1. Q1: Licence route for `m6561`, `vic20`, `vic20_clocks` ("Replay hardware only", no commercial use): ask MikeJ/WoS for a GPL-compatible grant, rewrite those three clean-room from the 6561 datasheet and a bench, or accept the restriction?
   **Answered (Alex, 2026-10-12):** "the VC-20 core stays TREX-internal, so no release": the restriction is accepted for internal use; the VC-20 core, its partial bitstream and the three MikeJ/WoS files never go into a release, a published package or a public repository (D13).
2. Q2: Reuse the TRX64 `drive_unit` as the VIC-20's 1541 (this spec), or the Nano's darfpga `c1541` (licence unclear, SD-based)?
   **Answered (Alex, 2026-10-12):** "ours": the VC-20 core uses our `drive_unit` (drive_l2.vhd) as its 1541; VIC20Nano's c1541 is not taken.
3. Q3: May this spec cost a host rebuild (scaler geometry/ratio/palette from identity), or must the scaler stay unchanged and the VIC-20 core scale its own picture (V1b)?
   **Answered (Alex, 2026-10-12):** "the host should get the picture from the guest in the guest's format and then output it; but as tight as everything is, I don't know whether we should fiddle with it right now": the principle is T79.1 V1 (the core sends its native picture, the host scales it), so the host learns geometry/ratio/palette from the core. Not now: the host change is measured first (layer 1 routed at 10,047 LUT and failed at 10,680 of 13,000, so about 600 LUT of room) and built when the VC-20 core is brought up, not before. **Then (Alex, same day):** "then the VC-20 guest should first hit the picture format itself": first stage V1b, the VC-20 core converts its picture to what the host's scaler expects today (VIC-II frame: the coordinates, frame start and 4-bit colour index the scaler reads now), so the host stays unchanged; the host-side geometry (V1) follows later as its own step.
4. Q4: Cartridge images beyond 8K at $A000 (MegaCart, DDR2-backed): in M2 or later? (Real cartridges are out, see D9.)
   **Answered (Alex, 2026-10-12):** "M2 if possible". VIC20Nano has it: `src/megacart.v` (Alastair M. Robinson 2021, GPL-2+, 145 lines) is the banking logic only, a 23-bit address (`mc_addr`) into the Nano's SDRAM plus an NVRAM select; the 2 MB image lives in external memory. Here that means the image in DDR2 through the core's memory port (T79.1 C3), loaded by the app; block RAM is far too small.
5. Q5: NTSC from the start, or PAL first?
   **Answered (Alex, 2026-10-12):** "PAL first": PAL (6561, 284 clocks x 312 lines) first; NTSC (6560) later as its own step.
6. Q6: Does the VIC-20 core get the Timewarp ring / monitor / overlay-text features later, or is it a plain machine?
   **Answered (Alex, 2026-10-12):** "no TRX64 features": the VC-20 is a plain machine (no ring, monitor, overlay text, freeze).
   **Changed (Alex, 2026-10-13):** "for all cores - when I choose FREEZE it MUST freeze. Like the C64". Freeze is
   no longer a C64-only feature: choosing Freeze stops the machine (the core holds every clock enable on the contract's
   `app_pause`/`machine_ctl` pause, audio silent) and shows the menu full screen over a held picture; leaving the menu resumes
   the machine where it stopped. The ring, monitor and overlay text stay out.

- **D13 TREX-internal only (Alex, Q1).** The VC-20 core is built and used inside TREX only. Its sources stay in the private Gitea repository; its partial bitstream is never part of a release, an update package (`.cfw`/`.ue2`) or a public download. Build: behind its own switch, off by default, so release builds cannot pick it up.

## §4 Out

- The click when the machine starts or resumes (DC step of `vc20_audio`): stays (Alex, 2026-10-23, Board results run 5).

- **Cassette and tape images** (`c1530`, TAP FIFO): not in; needs DDR2 buffering and a UI.
- **Real VIC-20 cartridges on the port** (27 outputs needed, 24 offered, pin positions differ: D9) (MegaCart / images larger than 8K are wanted in M2 after all, Q4).
- **User port / VIC-1011 RS-232** and paddles/mouse (C1351): no pins mapped, no UI.
- **SD-card-based 1541 (`c1541_sd`)**, JiffyDOS/Dolphin DOS ROM choice, a 1541-II or 1571: later, own spec.
- **A second drive, IEEE, VIC-20 printer**: not in.
- **Analog AV out** (composite/S-Video/RGB): gap G11 in the contract, host work, not this core's.
- **Pause-in-frames, Timewarp ring, trxmon, `.c64re` snapshots, UCI, SID, REU, turbo, sampler, cartridge
  writer** for the VIC-20: those are C64 features (Q6). Freeze is in (Q6, changed 2026-10-13).
- **Switching cores without a full CPU/app restart** and any change to the host beyond D5/D11.
- **Gowin primitives kept in any form** and the Tang boards' MCU/HID/OSD: dropped.
- **Copying VIC20Nano files into this repo** before Q1 is answered (S1).

## §5 Decisions taken while building

- **Board, 2026-10-13 (Alex, run 404, d25c320 + app fc3834b2):** "I can play games, there is sound, the picture is
  stable. I think the core is good enough for a first cut" (translated).
  Freeze on the board: "it stops, the menu is solid (opaque on top then)" (translated).

Branch `t83-vc20` (private, from `dfx-trial` 1092a96), 2026-10-12. Status of the work: built and simulated; the
Vivado numbers are in "Results" below; the board test is not done. The Commodore ROMs are not in the repository.

### Plan (as built)

**Files.** Upstream, in `cores/vic20nano/src` (NOTICE there; TREX-internal, off unless `TRX_CORE=vc20`):
`m6561.vhd` (VIC), `vic20.vhd` (machine), `vic20_clocks.vhd`, `vc20_via6522.vhd` (Gideon's VIA, renamed
because our 1541 carries one under the old name), `vic20_keyboard.vhd`, `t65/*` (6502). Ours, in
`cores/vic20nano/trx`: `vc20_ram.vhd` (the block RAM), `vc20_glue.vhd` (NCO, video, key map, audio, DMA
window), `vc20_regs.vhd` (the app's register window), `vc20_core.vhd` (the machine with its glue, no
C64 part). `designs/c64_basic/layer2_vc20.v` has layer2.v's 228 ports (checked pin by pin) and the C64
module's own `drive_l2` and `iec_bus`; everything the VC-20 does not use sits at the decoupler's idle.
Not taken: the Gowin primitives and IP, `ram_conf_*` (Wolfgang Scherr, also "Replay only"), the Nano's
loader/HDMI/OSD/MCU/SD/SDRAM, its `c1541`, `c1530`, `megacart.v` (M2).

**Clock.** One clock, `sys_clk` (the host's C64 clock, 31.5278 MHz PAL, contract C1); the machine runs on
enables, as VIC20Nano does. A VIC-20 line is 284 pixels at 4.4336 MHz, a VIC-II line is 2016 `sys_clk`
(504 dots x 4), both 312 lines. The enable generator (`vc20_nco`) adds 71 per `sys_clk` and fires at 252:
8.867 MHz, twice the pixel clock, so 71 pixel clocks come in every 504 `sys_clk`, and 284 of them (a VIC-20
line) in exactly 2016. A VIC-20 frame is then exactly 628,992 `sys_clk` = one VIC-II frame, which is what
the scaler's one-time frame lock needs (it never relocks; a 0.2 % frame-rate difference would walk the
64-line ring through the picture in two seconds). **Error:** the pixel clock is 4.4414 MHz instead of
4.43362 MHz, +0.17 % (50.12 Hz instead of 50.03 Hz); the CPU (1.1103 MHz instead of 1.1084), the sound
pitch (+3 cents) and the bit timing to the 1541 run that much fast. The 1541 keeps its true 16 MHz. No
MMCM is used: both host core clocks stay as the C64 module uses them (`sid2x_clk` unused).

**Memory.** Inferred true dual port block RAM (`vc20_ram`): port A the machine, one access per pixel enable
with the output held between accesses (what the Gowin blocks did), port B the host window. Arrays: 1 KB
$0000, 3 KB $0400 (4 KB array), 2 x 2 KB $1000/$1800, colour RAM 1 K x 4, 4 x 8 KB expansion ($2000, $4000,
$6000) + the 8 KB at $A000 (RAM, or a cartridge image with the read-only bit), CHAR 4 KB, BASIC 8 KB, KERNAL
8 KB: 63 KB, 32 RAMB18 = 16 tiles of `pb_l2`'s 50. The expansions are switched by a register
(`RAM_EXT`, `RAM_RO`); unexpanded after reset.

**ROM loading.** Nothing in the bitstream. The app writes the three ROMs through the same ROM block window
as for the C64 (`dma_rom`, offsets char $0000 (4 KB), KERNAL $2000 (8 KB), BASIC $4000 (8 KB), colour RAM
aperture $6000), so `machine:reboot`'s ROM stage works unchanged if it is given VC-20 images: the C64's own
BASIC/KERNAL/CHAR must NOT be written. RAM and everything else through the same window at the VIC-20 address
($0000-$FFFF, `dma_rom` = 0). The window is `vc20_win`: the app's toggle handshake, the host merges it with
JTAG (`win_port_*` out, `port_*` in) as `dma_win.v` does.

**Video.** `vc20_video` writes the current VIC-20 colour at `{y, x}` every `sys_clk` in the VIC-II frame:
x counts dots (504 per line, 4 clocks each), y lines (312). The visible VIC-20 picture (228 pixels, 284
lines, `hblank`/`vblank` low) starts at dot 94, line 9, which centres it in the scaler's window (404 of 400
dots, 284 of 270 lines: one pixel and two dots at the sides and 7 lines top and bottom are cut off). A
VIC-20 pixel is 1.775 VIC-II dots (7.1 output pixels), the correct pixel aspect. The host scaler, overlay,
pixel layer and HDMI chain are unchanged. Colour: the VIC-20's 16 colours are mapped to the nearest C64
palette index (default), or kept as numbers 0-15 (`CTRL` bit 2) for a palette the app writes with
`C64_PALETTE` (T59).

**IEC.** The C64 module's `drive_l2` (our `drive_unit`, the 1541) and `iec_bus` with the VIC-20's three
lines in place of the C64's; levels as `iec_bus.v` says (0 = pulled low). CLK is held low after reset until
the KERNAL has initialised VIA 2 (CA2 as an input floats high behind the inverter), as on the real machine.

**Keyboard.** The host's `c64_keys` (`col * 8 + row`, 1 = down) through a 64-entry table
(`vic2c64` in `vc20_keys`): the VIC-20 keyboard (columns on VIA 2 port B, rows on port A) has the same keys
as the C64's, differently wired (checked against the VIC-20 matrix in VIC20Nano's key map, every key; the
bench checks A and DEL). RESTORE goes to VIA 1 CA1. Joystick: `joy1 or joy2` to the one VIC-20 port (VIA 1
port A bits and PB7, fire on the light-pen line); paddles are not connected.

**Audio.** The VIC's 6 bit sum, averaged over 656 `sys_clk` (48.06 kHz), centred and scaled to a signed 24
bit sample, to `mclk` by a toggle handshake; mono on both channels. No SID filter, no TRX64 mixer.

**Registers (the app's core window, 0x101A0000).** ID "VC20" at 000-003 (generic `ID_TRX2` makes it "TRX2"),
CAPS 0xC0 / CAPS2 0x02, BUILD, IRQ_EN/IRQ_CAUSE and DDR_CAL as trx_io.v, page 6 forwarded to the host's pixel
layer, and at 0A0: CTRL (b0 reset hold, b1 halt, b2 identity palette), RAM_EXT, RAM_RO, STATUS, FRAMES, CPUADDR
(a sample of the CPU address bus, read twice). Everything else reads 0 and is acknowledged (S10).

**Build.** `TRX_CORE=vc20` (the branch's `bitstream.yml`; part of `gateware_key`): `build.tcl` synthesizes
`layer2_vc20` out of context, then (after the C64 configuration and the grey box) opens `routed.dcp`, makes
`l2` a black box, locks the static routing, reads the VC-20 checkpoint into `l2`, places and routes it inside
`pb_l2` and writes `c64_basic_pb_l2_vc20_partial.bit`; LUT/BRAM of `pb_l2` and the WNS of that configuration
are printed (`=== T83 ...`) and saved as reports. `ci/package.sh` removes everything named `vc20`, so the
module never reaches a published package. The host is not rebuilt for the VC-20 (scaler unchanged: Q3, V1b).

### Decisions taken while building

- **The frame is the VIC-II's, to the clock** (above): chosen over a frame buffer or a rate-converting
  line buffer because it needs no memory and the host stays unchanged; the price is +0.17 % machine speed.
- **No Gowin RAM wrapper reuse.** `ram_conf_*.vhd` carry Wolfgang Scherr's "Replay only" clause too and
  depend on Gowin blocks; `vc20_ram` replaces them (same enable behaviour, plus the host port).
- **`via6522` renamed `vc20_via6522`**: Gideon's 1541 VIA of the same name is in the drive's file list; one
  `work` library holds both.
- **ID is "VC20"**; whether the Ultimate app's loader accepts it or insists on "TRX2" is open (S10/S11, app
  side). `ID_TRX2` is the stop-gap.
- **`halted` is the 6502 and the VIAs held at a bus-cycle boundary** (Freeze, below); the C64 module's DMA-in-pause
  (`dma_bus`) has no counterpart, the window reads and writes memory at any time.
- **Reads of unmapped window addresses return 0**, not $FF.
- **Colour RAM aperture (`cram_addr`, the host's freeze text layer) is not served**: `cram_rdata` = 0.

### Results

CI run 746 (commit 35551d6a, builder, `TRX_CORE=vc20`; run 745 = the first, 3ac9ec64, before the keyboard fix):

| | VC-20 module (`pb_l2` after place and route) | C64 module (run 722-class, same host) | `pb_l2` has |
|---|---:|---:|---:|
| Slice LUT | **5,070** (28.2 %) | 14,058 | 18,000 |
| Slice registers | 2,768 | | 36,000 |
| Block RAM tiles | **16.5** (14 RAMB36 + 5 RAMB18) | 47 | 50 |
| DSP | 0 | 4 | 80 |

Out-of-context synthesis: 4,090 LUT, 2,779 FF, 16.5 tiles (drive 1,985 LUT, VC-20 core 2,090 LUT, IEC 13 LUT; the
980 LUT between synthesis and the placed `pb_l2` count are not analysed: partition-pin tie and route-through cells
are the suspects). The estimate in D12 (3,500 to
4,500 LUT, about 20 tiles) held. Timing of that configuration (host static locked as routed, the VC-20 placed and
routed into it): WNS +0.013 ns, WHS +0.051 ns, 35,385 endpoints, all constraints met; +0.013 ns is the host's own
worst path (the RISC-V cache loop of the C64 configuration is the same number), so every path in the VC-20 module has at
least that. 0 routing errors (`route_status_vc20.rpt`), DRC 0 errors, 83 warnings (run 745) (REQP-1840: T65's and the VIC's
async-reset registers drive block RAM address pins; they are only moving while the machine is held in reset, with the
RAM's write enable off). Partial bitstream 1,224,854 bytes (the C64's 1,425,686). The host did not change: layer 1 is
10,005 LUT (77.0 % of its 13,000) as in the run's C64 configuration, whose WNS is +0.013 ns. The grey-box partial still fails with the
known HDPR-72 (`ui_clk`); the VC-20's partial does not, it does not buffer the port.
Builder cache: `/srv/trx64-build/cache/bit/c64u-ebe0cc17f9831971/` holds `c64_basic.bit` (host with
the C64 module), `c64_basic_pb_l2_partial.bit` (C64 module) and `c64_basic_pb_l2_vc20_partial.bit`; the partial only fits the host
of the same directory (the static routing is the same, the stamp is not).

Bench (`designs/c64_basic/sim/vc20/run.sh`, GHDL, in CI `sims`, 47 s): ID register, ROM block window write and read
back, key map, a hand-written 6502 program: 3 frames of exactly 628,992 `sys_clk`, border, background and text colours
in the picture, screen RAM and colour RAM written by the program, read back through the DMA window handshake.
Local boot bench (`run_boot.sh`, real VICE ROM copies from the developer's disk, not in CI, 10 minutes per run):
**BASIC V2 start screen, 3583 BYTES FREE, READY., and an A typed on the C64 matrix' A key appears at the cursor.**
That run found and fixed the key lines (port B scan / port A read had been the wrong way round).
What the benches do not exercise: audio level and pitch, joystick, RESTORE, the 1541 and the IEC bus, the RAM
expansion bits, the cw page 6 forward, the swap itself, the picture on a real scaler/HDMI chain.

**Freeze (2026-10-13, GHDL `tb_vc20_pause`, all green; `tb_vc20` and `tb_vc20_ext` unchanged green):** halted 2 us after the
request; over 64 ms paused: no 6502 step, address constant, zero-page counters (main loop, IRQ count, T2 copy) constant, sound
sample exactly 0 (sound is non-zero before and after), `pix_y` wrapped 3 times and `pix_x`/`pix_y` equal the free machine's in
every clock of the whole run (0 mismatches); after the release B's 8370 logged bus cycles (address, R/W, data out, in order,
VIA2 T1 IRQs and T2 copies among them) equal the first 8370 of the free machine's 79659 (0 differ). A mutation (VIA strobes not
held) fails the same check (2949 differ from cycle 3933), so the check bites. Reset with the pause asserted: the 6502 steps
(111 steps in 100 us), the pause holds again after the reset.

### Board results

- **Run 1 (35551d6a, 2026-10-12):** swap and ROM load worked, but the machine was held in reset: the app's `C64_RESET` reaches the
  core as `core_reset_req` and stays set without a `machine:reset`. Photo: stale blue C64 picture with cyan lines in the scaler ring,
  RAM and VIC registers all 00 over JTAG.
- **Run 2 (f01e940e, 2026-10-12): works.** Steps that worked: host `c64_basic.bit` into SRAM, app over JTAG, partial on
  `/USB1/core.bit`, `coreload.u2a` ("Core loaded"), `PUT /v1/machine:reset` (releases the app's C64_RESET), then
  `tools/vc20_load.sh` (CHAR/BASIC/KERNAL, reset). Photo: white screen, cyan border, blue text "**** CBM BASIC V2 ****",
  "3583 BYTES FREE", READY.; text typed on the keyboard, `PRINT 2+2` gave 4.
  Bug found: the JTAG debug words read one word late (registered data; the host collected before it was updated). Fixed by a
  combinational read on the JTAG address (not yet built in CI).
  Still untested: sound, joystick, RESTORE, the 1541, swap back to the C64.
- **Run 3 (host app, 2026-10-12): vc20.cor from the boot menu works** (ICAP swap, VC-20 app loads the ROMs from
  `/USB1/roms/vc20`, no JTAG): READY; picture shifted after every reset (fixed in 382b4c8, D16, not yet built).
- **Run 4 (`vc20_interim.cor`, new app 8c65042f on the old partial, 2026-10-12): RAM expansion and Run PRG work** (Alex:
  "works"). The picture still shifts on reset (old partial).
- **Run 5 (release main-66eefd51, generic application, T119 pixel layer, 2026-10-23): works** (Alex: "VC20 hat ton, klappt
  alles. Ich kann auch .d64 laden, also 1541, autostart geht", "the VC-20 has sound, everything works; I can load a .d64 too,
  so the 1541, autostart works"). Picture and menu fit. Found: a loud click when the machine starts. Cause (code):
  `vc20_audio` centres the 6561 sum at 30, so silence (volume 0, sum 0) is a sample of about -5 M of +-8.4 M, while `mute`
  gives exactly 0; release from the hold, every freeze/resume and every write to $900E step the output. A DC blocker
  (about 10 Hz, the real machine's coupling capacitor) was proposed. **Decided (Alex, 2026-10-23): not built** - "Lass doch
  den Knackser, der ist mir egal" ("leave the click, I don't care about it").

### Board test (Alex), step by step

Files: the builder cache directory named above (host and VC-20 partial of the same run), the VIC-20 ROMs on your disk
(PAL: `kernal.901486-07`, `basic-901486-01`, `chargen-901460-03`, 8192 / 8192 / 4096 bytes; the VICE copies under
`vice-3.7.1/data/VIC20` are what the boot bench used). Order matters; the app's `machine:reset` / `machine:reboot` are
not used after the swap (they write the C64's ROMs into the VC-20).

1. System Mode PAL (the VC-20 module is PAL only; the scaler's 60 Hz modes would show garbage). EF3 out of the port.
2. Load the host: `openFPGALoader -c usb-blaster -m c64_basic.bit`, then the app over JTAG (`tools/trxdbg.sh riscv <app.bin> follow`)
   as for any dfx-trial build. The C64 module is in; the C64 boots.
3. The swap, as PoC 5: partial to the stick as `/USB1/core.bit` (`c64_basic_pb_l2_vc20_partial.bit`), then
   `PUT /v1/apps:run_file?app=/Flash/apps/coreload.u2a&file=/USB1/core.bit&action=run` (decouple, ICAP, release, about
   13 s). **No `machine:reboot` afterwards.** The picture goes black or garbled (empty ROMs, a blank VIC-20 runs BRK).
   Check the module: the app's core window `0x101A0000` bytes 0-3 read `VC20`.
4. First let the app release its C64 reset: the app holds `C64_RESET` (it reaches the core as `core_reset_req`, JTAG register
   `0x100009` shows only the JTAG bit) until its own reset sequence runs, and a held reset keeps the VC-20 in reset (board run 1: blank
   RAM, stale picture). Do `PUT /v1/machine:reset` once; it writes the C64's BASIC/KERNAL into the VC-20's ROM windows, which the next
   command overwrites. ROMs and reset: `tools/vc20_load.sh CHAR.bin BASIC.bin KERNAL.bin`. It writes them through the JTAG byte window at
   CHAR `$8000` (4096), BASIC `$C000` (8192), KERNAL `$E000` (8192), reads the first 64 bytes of each back, pulses the
   machine reset (JTAG register `0x100009` bit 0 = the host's `core_reset_req`) and prints "reset released". The same
   addresses work through the app's DMA window with `dma_rom = 0` (REST `machine:writemem` at those addresses; not tried).
   The three ROM images are also reachable at ROM-block offsets char `$0000`, KERNAL `$2000`, BASIC `$4000`, colour RAM
   `$6000` (`dma_rom = 1`, the C64's apertures).
5. Expect on HDMI within a second: white screen, cyan border, "**** CBM BASIC V2 ****", "3583 BYTES FREE", "READY." The
   colours are the nearest C64 palette entries (CTRL bit 2 would give the VIC-20 numbers 0-15 for a palette written with
   `C64_PALETTE`). A photo, not a REST read, decides (`fpga-gateware` skill, section 7).
6. Type on the C64U keyboard (every key, SHIFT, C=, CTRL, cursors, F1-F7, RESTORE with STOP); the joystick in port 2 or 1.
   Sound: `POKE 36878,15:POKE 36876,200` (a tone; +0.17 % fast).
7. The 1541: mount a D64 as drive A in the app, `LOAD "$",8` and `LIST`.
8. JTAG-readable debug words (`tools/trxdbg.sh trx 0 / 4 / 8 ...`, also printed by `vc20_load.sh`): 0 "VC20", 1 status (b0 in reset,
   b1 halted, b2 host reset request, b3 CTRL hold, b4 sys_rst, b5 halt request, b6 CTRL halt, b7 left reset once), 2 frames, 3 CPU
   address, 4 pixel enables, 5 lines, 6 CPU address changes. Registers in the app's window: `0x0A8/0x0A9` FRAMES (counts at 50 Hz), `0x0AC/0x0AD` CPUADDR (read twice), STATUS `0x0A4`.
9. Back to the C64: reload the C64 partial (`c64_basic_pb_l2_partial.bit`) the same way, then `machine:reboot`.

### VC-20 application and `vc20.cor` (D11a, 2026-10-12)

App repo TREX/1541ultimate, branch `t83-vc20app` (from `t79-coreload`), `doc/VC20.md` there. The Ultimate app as its own image:
`build/build_vc20.sh` (container, `make VC20=1 RESULT=result_vc20 OUTPUT=output_vc20`), `ultimate.bin` linked at 0x30000, 1,390,660 bytes. At start a
task checks the ID block (`VC20`), loads CHAR/BASIC/KERNAL from `/flash/roms/vc20`, `/usb0-3/roms/vc20` or `/sd/roms/vc20` through the
ROM apertures (retrying while USB mounts; the overlay says so after about 10 s), and resets the core (`C64_MODE` RESET then UNRESET). `machine:reset`
and `reboot` are the VC-20's, `readmem`/`writemem` reach its 64 KB, the overlay is the only menu. C64 capability bits are masked, the C64 init
functions, SID detection, the CIA 1 key scan at boot and the C64 config pages are gated or hidden (table in `doc/VC20.md`). The host app and the
gateware are untouched. `vc20.cor` = `tools/cor_pack.py` of the bootapp worktree: the partial of the board's host build
(`c64u-2234893c8ac1b9ca`, f01e940e, without the later JTAG word fix) + the app image; not committed (`out/`, ignored); ROMs are in neither.
Board (2026-10-12): `vc20.cor` from the host app boots to READY, ROMs from `/USB1/roms/vc20`, overlay over the VC-20 picture. Fixes after it: the menu
title is "*** VC-20 (TRX64 host) 3.15 ***"; Interface Type, Overlay Transparency, System Mode, C64U Model, Mouse Mode and Paddle Override are
not listed in the Config menu (they do nothing, or the wrong thing, on the VC-20); the picture was shifted down and torn after the start but correct after
"Reset VC-20" from the menu. Compared: both call the same `vc20_reset()` (C64_MODE RESET, 20 ms, UNRESET); only the state differs (core held in reset since the swap and no
frame seen by the scaler at the start, running core and a locked scaler at the menu reset), so the start now resets once more after one second (cause not measured,
the scaler's one-time frame lock is the suspect; host side, no host change).

### Decisions of 2026-10-12 (Alex): memory expansion, Run PRG, the 6561 is never reset

- **D14 Memory expansion (Alex: "can we add it? There is enough room"; Mike Robertson: "3kb more up to 28kb plus IO", "Max is 37kb in total").** The core
  already had the standard VIC-20 expansion (3K at $0400-$0FFF, BLK1/2/3 at $2000/$4000/$6000, BLK5 at $A000 as RAM or ROM) behind the registers
  `RAM_EXT` (0x0A1) and `RAM_RO` (0x0A2); the arrays are always in the bitstream, so there is **no BRAM change: 16.5 of 50 tiles** (the
  expansion switches blocks, it does not add them; the arrays total 63 KB of block RAM). New: `tb_vc20_ext` (CPU writes and reads back the first and
  last byte of every block in 8 configurations: unexpanded, +3K, +8K, +16K, +24K, +3K+24K, all with BLK5 as RAM, all with BLK5 read only; passes in CI `sims`),
  and the local boot bench with a real KERNAL: +3K 6655 BYTES FREE, +8K 11775 BYTES FREE (the real machine's numbers; the larger configurations take
  more simulated time than the bench waits). App: Config menu "VC-20 Settings", applied with every reset (`doc/VC20.md` in the app repo). BLK5 as ROM is
  only the read-only flag; loading a cartridge image into it is not built (cartridges stay out). Per-PRG auto-select of the expansion is not built (Alex: not now).
- **D15 Run PRG (Alex).** File browser Run/Load/DMA on a PRG, and REST `runners:run_prg` / `load_prg`, in the VC-20 application: reset (with the expansion), wait 2.5 s,
  DMA the bytes into the VIC-20 RAM at the load address, or for a BASIC start address ($1001, $0401, $1201) at the machine's own start of BASIC with the line links
  recomputed and `$2D-$32` set, then `RUN` typed into the keyboard buffer; other addresses get `SYS<addr>`. Not done: LOAD from the drive ("Real Run"), D64 auto
  directory, tape.
- **D16 The 6561 has no reset pin (Alex's lead, confirmed).** A machine reset reset the 6561's raster counters and frame start, so every reset moved the frame
  against the host scaler's one-time lock (picture shifted, and after a reset in mid-frame a short frame: stale ring lines, the "squash"). Now only the power-on
  `sys_rst` resets the VIC; a machine reset resets CPU, VIAs and the clock divider, never the 6561 timing (its registers keep their values through a reset, as on
  the real chip; the KERNAL rewrites them). The NCO was never reset by a machine reset. `tb_vc20` pulses the machine reset in the middle of the run and checks that no
  frame period changes (628,992 `sys_clk` throughout); it fails with the old wiring. The app's second-reset workaround is removed.
  **Picture position and size (squash analysis):** the visible area of the 6561 is fixed by the core's blanking (228 pixels, 284 lines, `hblank`/`vblank`); $9000 and
  $9001 only move the character matrix inside it (a matrix pushed past the visible area is cut off by the blanking, as on a TV), $9003 sets the rows (up to 63, 16-line
  characters with bit 0), and the 6561 has no interlace: the frame is always 312 lines. The converter shows VIC-20 pixels 1..226 and lines 7..276 (the scaler's
  window is 400 x 270 dots); the standard screen (origin $0C/$26, 22 x 23 characters of 8 lines = lines 76-259) lies well inside it. Only a matrix that ends below
  line 276 (a large vertical origin with many rows, or 8 x 16 characters with 23 rows) loses its bottom lines; that is a clip, the picture is not squashed. Chosen: fixed
  placement and symmetric clip (no scaling, no register tracking); the host scaler stays as it is.

- **D16a The wrong lock at the start (board run of CI 392: D16 held, the picture stayed put across resets but sat too low from the start, torn at the bottom).**
  The host scaler locks ONCE: on the decouple-release edge it clears its lock and sets it at the next moment `pix_y` reaches 20 with `pix_x` 0 (`vic_scaler.v`:
  `frame_tgl`), and from then on its output runs free. `vc20_video` counted `pix_y` from a start value of 9 at power-on and re-synchronised it only at the first
  `vblank` edge, about 29 lines later. After the swap the 6561 starts at release (it is held by `sys_rst` until then), so the first frame's `pix_y` was 9 + lines
  since start: it reached 20 at raster line 11 instead of 40, the scaler locked 29 lines early, and every later frame (now correct) was read against that phase:
  the picture sits 29 VIC lines (about 116 output lines, 11 % of the height) too low, and the reader, which needs 270 lines of the ring complete before it reads
  them, overtakes the writer near the bottom: lines of the previous frame mixed with the current one, the stripes. (In the JTAG run the 6561 was already
  running when the scaler locked, so the phase was right.) Fix: `pix_y` is computed from the 6561's own line counter (`O_VCNT`, first visible line vcnt 29 = y 9), right
  from the first line; the lock moment is the same in every frame. `tb_vc20` now requires that y reaches 20 exactly one frame (628,992 `sys_clk`) apart for every
  occurrence including the first (fails on the old counter: 1 off), and the steady-state position is unchanged (first black text line y 56 before and after).

### Freeze (Q6 changed, 2026-10-13)

"When I choose FREEZE it MUST freeze. Like the C64." Built as the C64's contract: the app's `C64_STOP` (`app_stop`), the
JTAG pause (`machine_ctl[1]`) and the host's halts (`mc_halt`) all reach `vc20_core.halt_req` (`layer2_vc20.v`; the host's
`gate_frz` gate belongs to the C64 layer's trx_sys and stays 0 here, so every pause is a whole-machine pause).

- **Hold point: the 6502 and both VIAs, at a bus-cycle boundary; the 6561 keeps running.** `vic20.vhd` latches the hold only
  at a CPU step (`ena_1mhz and ena_4`) and holds the step, the VIAs' rising and falling edge strobes (their timers, shifters
  and register accesses run on those two strobes only) and RAM / 6561 register writes (`v_rw_l`). The VIAs act on the
  falling strobe, which falls on the same clock as the CPU step: the hold starts in the clock after it, so the step and the
  edge complete the bus cycle first and the machine stops between two whole cycles; the release comes at a later step
  clock that is itself still held, so the next cycle starts with its rising edge exactly as in an uninterrupted run. The
  clock enables (`vc20_nco`) are no longer stopped: the 6561 draws, `pix_x`/`pix_y` run, the scaler never loses its lock
  (before, a halt stopped the NCO and with it the picture), and nothing else needs a stop (RAM is only written by the CPU,
  the DMA window and the host port; the 1541 is held by `drive_stop` = `C64_STOP` as for the C64).
  Stopping the whole NCO instead was rejected: it freezes the pixel stream mid-frame and the scaler's relock risks the
  position bug of 2026-10-13. Stopping only the CPU was rejected: the VIA timers (the KERNAL's jiffy clock IRQ, keyboard
  scan, serial timing) would run on and the resume would not be exact.
- **Sound: exactly 0 while held.** `vc20_audio` forces the next 656-clock window's average to the centre (sample 0); the 6561's
  voices keep counting, so the pitch phase is not frozen, the sound simply is not heard (the host mutes on `c64_halted` too).
- **A reset takes the hold away** (T65 sees `Res_n` only with its enable; the app's "Reset VC-20" in the menu must work while
  frozen). The pause applies again once the reset is over.
- **`halted`** (`c64_halted`: the scaler's black picture, the host mute, the status bit) now means "held", asserted a few
  clocks after the request (at the next CPU step, at most 1 us) and not during a reset.
- **Bench `sim/vc20/tb_vc20_pause.vhd`** (`run_pause.sh`, GHDL in the `vc20-ghdl` container): two identical machines, one
  free, one paused for 64 ms at an arbitrary sys_clk. Results below (Results).
- `vc20_core` has a bench-only output `cpu_tap` (step, R/W, address, data out of the 6502, `vic20.vhd` `o_tap`); the layer
  leaves it open. GHDL 5.0.1 aborts on VHDL-2008 external names into the instance, hence the port.

### Not done

- NTSC (Q5: PAL first), MegaCart/DDR2 images (M2), tape, user port, paddles, the C64-feature guards in the app
  (D11), the identity-gated app behaviour, the scaler's own geometry from identity (V1; Q3).
- The board test (S3, S5-S9, S11).
- `sid2x_clk`, `ui_clk`, the DDR2 client ports (`c_*`, `e_*`), `reu_*`, the cartridge/UCI/debug/trace ports are
  idle; the JTAG debug (`trx_j_*`) answers 0.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->

## §5 Note, 2026-10-22, onto the generic application (Alex): "apart from trx64 and c64u, switch everything to the generic core app for now" (translated). The VC-20 and C264 cores leave Gideon's application (`app = "ultimate"` without `app_ref`, so CI shipped the plain C64 application in `vc20.cor`/`c264.cor`; their machine code lived only on the fork branch `t90-c16app`) for a T104 descriptor; supersedes T84 D12 for these two. Only `trx64` and `c64u` stay on Gideon's application.

### 2026-10-22, what the old VC-20 application did, and what the generic application does now

The old application is `software/io/vc20/vc20.cc` (+ the VC20_APP gates) of the fork branch `t90-c16app` (commit 462af73f), read from the code
(`git show t90-c16app:software/io/vc20/vc20.cc`); its `doc/VC20.md` was used only to find where to look. The new one is `coreapp/cores/vc20.cc`
(descriptor, operations, register constants `vc20_regs.h`) plus `coreapp/cores/cbm_run.*` (the PRG job, shared with the 264) and the PRG library
`app/loaders/cbm_prg.*`. The core's registers are `cores/vc20/rtl/trx/vc20_regs.vhd`.

| # | What the user had (old application) | Core register / path | Generic application now | State |
|---|---|---|---|---|
| 1 | Identity check: ID bytes `VC20` at the core window, a message on the screen otherwise | `ID` 0x000-0x003 | `start.cc` picks the descriptor by the ID block; an unknown ID is refused by name; `tools/check_core_id.py` (lint) compares the VHDL constant with the descriptor's id | built; lint |
| 2 | ROMs: CHAR 4096, BASIC 8192, KERNAL 8192 from `/flash/roms/vc20`, `/usb0..3/roms/vc20`, `/sd/roms/vc20`; 6 / 3 / 4 file names; retried every 0.5 s while the sticks mount, message after about 10 s, read-back | host ROM aperture: BASIC +0, KERNAL +0x2000, CHAR +0x4000 (`vc20_glue.vhd` decode of what `ultimate_sys.vhd` r_rom hands over) | `roms[]` with the same names (`ROM_NAMES_MAX` raised from 4 to 7: the CHAR list has six); `rom_scan` per file over the same roots, 500 ms retry, message after 10 s, a wrong size at once; each put operation reads back | built, bench `test_vc20`. Difference: the old one wanted all three files in ONE directory, the common ROM manager takes each file from the first root that has it |
| 3 | Memory expansion (7 values: unexpanded, +3K, +8K, +16K, +24K, +3K+24K, +3K+24K+8K at $A000) and "Block 5 ($A000) when expanded: RAM / ROM", "at next reset", written while the core is held | `RAM_EXT` 0x0A1 (values 00 01 02 06 0E 0F 1F), `RAM_RO` 0x0A2 (bit 4 for ROM) | two settings bound to the registers (`RegBind`, taken in reset: `WHEN_RESET`), the same value table | built, bench. Difference: the old code wrote `RAM_RO` = 0x10 only when the largest expansion AND "ROM" were chosen; now it follows the block-5 setting alone. `vic20.vhd` uses `i_ram_ext_ro(4)` only where `I_RAM_EXT(4)` is set (lines 722, 790), so the machine behaves the same |
| 4 | Reset VC-20: stop released, `C64_MODE` reset, expansion written, 20 ms, unreset; the menu closes | `C64_MODE` of the host | task "Reset" = `machine.cold_reset` (hold, reset-time settings, 20 ms, release), closes the menu | built, bench |
| 5 | Reload ROMs, Reset (REST `machine:reboot` too) | aperture | task "Reload ROMs, reset" = `machine.reload_roms` | built, bench |
| 6 | Freeze: Interface Type = Freeze (default) sets `C64_STOP`, polls STATUS b1 for 20 ticks; Interface Type = Overlay leaves the machine running behind the menu | `STATUS` 0x0A4 b1 | Freeze only (descriptor `freeze` = STATUS, mask 0x02, 200 ms); an open core menu is Freeze (T104 5.3, Alex 2026-10-20) | built; Overlay mode deliberately not carried |
| 7 | Run PRG (`.prg`, `.p00`): reset, wait 2.5 s, BASIC (load address $1001, $0401 or $1201) relinked to the start pointer at $2B/$2C (window $0400-$2000), end pointers $2D/$2F/$31, `RUN` into the buffer $0277 / $C6; otherwise bytes at the address and `SYS<addr>` | RAM window, `C64_MODE` | "Run PRG": the PRG library (`cbm_prg`), the job waits for the machine to say READY (the start pointer is one of the three values, after at least 2.2 s of machine time plus 0.3 s) instead of a fixed delay, gives up after 4 s more with a notice; a file that is no program or would wrap past $FFFF is refused before the machine is touched | built, bench with a model whose KERNAL is slower than the minimum and a control for each constant. The times are the old 2.5 s in total; not measured on a board |
| 8 | Load / DMA of a PRG (no reset, nothing typed; the two entries did the same) | RAM window | "Load PRG (no reset, no start)" | built, bench |
| 9 | 1541 drive A: `.d64`/`.g64` mount (read-write, read-only, unlinked), "Mount & Run" inside a `.d64`, "Real Run" not offered, Drive A settings page, Built-in Drive A tasks, drive ROM (`.bin` of 16/32 KB), Create D64/G64 | `drv_io_*` and `gcr_io_*` pages (`drive_l2` in `layer2_vc20.v`), the IEC bus | the `cbm1541` library (T104.1 5.7): file actions *Mount Disk*, *Read Only*, *Unlinked* on `.d64` / `.g64`; *Mount & Run* mounts the disk, resets and types `LOAD"*",8,1` and `RUN`; the four Drive settings (Drive, Bus ID, Extra RAM, swap delay); tasks *Reset Drive*, *Create blank D64 / G64*, *Save Disk as D64 / G64*; the media slot *Drive A:*; the ROM `1541.rom` (16 or 32 KB) from `roms/vc20`, *Set as 1541 ROM* on a `.rom` file (`drive.set_rom`, this session); writes go back into the image file | **built, bench** (`test_cbm1541`), branch `t104-cbm1541`, not on a board. *Mount & Run* and *Real Run* of a PRG **inside** a disk image: *Browse Disk* makes the image a folder (T104.1 5.7.1), as does the name prompt of Create and Save; the old whole-disk entry is *Run Disk*. No 1581 (this drive gateware is a 1541 only) |
| 10 | Joystick: the host's two joystick signals are ORed into the VIC-20's one joystick (`vc20_core.vhd:181-182`); no setting | `c64_joy1/2` of the layer 2 contract | nothing in the descriptor: the path is gateware, the application never touched it. Paddles (pots tied to FF), mouse, 1351: none, as before | nothing to build |
| 11 | Keyboard: the host's C64 matrix into the VIC-20's keyboard; no setting | gateware | nothing to build | nothing to build |
| 12 | Picture: the host's scaler path, the 6561 draws the raster the scaler locks to; the old Video Configuration page was hidden | gateware; `CTRL` b2 (identity palette) never written by the app | `video.w == 0` (no layer, no buffers), nothing written; the application adds its "Picture" item (HDMI mode, T107) to every core | built; the HDMI picture item is new |
| 13 | Menu and REST: Ultimate menu with network, clock, services, file browser; REST `machine:reset/reboot/readmem/writemem/menu_button`, `runners:run_prg/load_prg` | - | the common menu (F1 Home with network, F3 Library, F5 the machine, F7 Actions, F8 System); settings in `<name>.cfg` beside the `.cor` (T84 D11). REST and WS adapters are T104.1 D3, not built for any core; the operations they would call exist by name (`vc20.run_prg`, `vc20.load_prg`, `machine.cold_reset`, `machine.reload_roms`) | menu built; REST **open** (same for every generic core) |
| 14 | Everything C64-only was hidden (SID, REU, tape, printer, LED ...) | - | a whitelist application: nothing of it exists | n/a |

Not offered by the old application either, so not built: a cartridge image at $A000 (block 5 could be ROM but nothing loaded it), tape, user port, NTSC.
Settings keep their text (`Memory Expansion (at next reset)`, `Block 5 ($A000), when expanded`) but live in a new store: a value chosen in the old
application's flash store is not read by the new one.

**Tests** (`tests/coreapp/test_vc20.cc`, `run.sh`, 21 controls, every one fails as it must): the ROMs at the aperture offsets and put in the hold, the
expansion registers written only while the machine is held, every expansion value, block 5, Reset leaves the ROMs and Reload puts them, Run on a BASIC
program for the three start addresses, machine code, `.p00`, refusals leave the machine untouched, the machine held by the menu does not reach READY, a
machine that never reaches READY, a reset ends the wait, Load without reset, Freeze's PAUSED bit, the golden menu `menu_vc20.txt`. Not run: the RISC-V
build (CI `trxapps`), the board.

**Cost.** `cbm_run.cc` has one 64 KB static buffer for the file (shared with the 264), next to the C64 descriptor's own and the PRG library's; the
application image grows by the code only. `core.toml` is part of the core's layer key (`sources = ["cores/vc20", ...]`), so changing `app` re-routes the
unchanged gateware once in CI.
