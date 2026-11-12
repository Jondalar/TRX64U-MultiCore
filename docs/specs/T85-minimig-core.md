# T85 — An Amiga (Minimig) core, ported from NanoMig, runs as the third core in the T79 host/core split

**Status:** DECIDED (2026-10-12): all open questions answered; not built.
to the open questions in §3.
**Repo:** TRX64-Ultimate (gateware under a new `designs/amiga/` and `cores/minimig/`, the core's application on
Gideon's base as for the VC-20, tools). The source is read from outside (MiSTle-Dev/NanoMig); nothing is copied into this
repo by this spec.
**Number:** T85.
**Milestone:** M3 (other cores; T79.1 §6, `docs/other-cores.md`). Alex's call.
**Origin:** Alex, 2026-10-12: "then on another branch: build the Amiga, NanoMig as the template" (translated), after the
VC-20 core (T83) booted on the board.
**Depends on:** T79 (host/core split, `T79.1-host-core-contract.md`, PoC 5 = the app swaps the core through ICAP), T83
(the precedent: second core, `.cor` bundle with its own application, D11/D11a, D13), T84 (boot chain, `.cor`; branch
`t84-bootapp`, not read for this spec, only as T83 §5 quotes it). Floorplan B (`pb_l2`) must stay as measured.

Template adopted 2026-10-12 (`docs/specs/_TEMPLATE.md`, C64RE spec template). Citations: `T79.1:N` is a line of
`T79.1-host-core-contract.md`, `T83:N` a line of `T83-vc20-core.md`, both on this branch (`t83-vc20`, a1c5e20). NanoMig
files are from its `main` at a19604a (2026-10-11), read in a scratch clone outside the repo
(`~/Development/C64/Tools/NanoMig-ref`). Marked **(estimate)** = derived, not measured. **(measured)** = taken from a
report or a run. **(source)** = read in a file or on a page named next to it.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to host more than one machine (T79.1:7 names "Minimig" next to the VC-20).

**What he wants:** pick "Amiga" in the core selector, the board swaps layer 2, and an Amiga (OCS/ECS Minimig) boots
Kickstart, shows its picture, plays its sound, takes keyboard, joystick and mouse, and boots Workbench from an ADF.

**Today:** two cores exist or are planned on the contract (TRX64, VC-20). `docs/other-cores.md` lists NanoMig as "Not
now". The VC-20 is a 40 KB machine with a 1.1 MHz bus; the Amiga is the opposite case, and it is the case the contract
was asked about before: T79.1:49 ("To check before a Minimig port: the real-time memory port must answer within the
Amiga's chip bus cycle ... our DDR2 controller's worst-case latency is not measured") and T79.1:48 (floppy and hard disk
images lie in DDR2, the core reads them itself). Neither is settled; this spec turns them into measurable steps.

**What the source is (source):** NanoMig (Till Harbaum et al., MiSTle-Dev) is the MiSTer Minimig-AGA chipset in
Verilog (`src/minimig-aga/`: `agnus*`, `denise*`, `paula*`, `ciaa/ciab`, `gary`, `gayle`, `userio`, `minimig.v`,
`cpu_wrapper.v`, optional `cpu_cache_new.v`), two CPUs (`src/fx68k/` Jorge Cwik's cycle-exact 68000, `src/tg68k/`
Gubener's TG68K for 68020), a top-level wrapper `src/nanomig.v` and board tops per Tang board (`src/tang/*/top.sv`)
with Gowin PLLs, SDRAM controller (`misc/sdram.sv`), HDMI (`src/hdmi/`), flash Kickstart loader, SD/MCU interface
(`sd_card.v`, `hid.v`, `sysctrl.v`, `mcu_spi.v`, `osd_u8g2.v`). It needs an external microcontroller (FPGA-Companion)
for USB, SD and OSD. It has not been built or simulated by us.

**Licence (read from the file headers, not from the README):**

| File(s) | Terms as written in the file |
|---|---|
| NanoMig repo | **no `LICENSE` file in the repository** (`git ls-files`); README states none |
| `minimig-aga/agnus*.v` (most), `denise*.v` (most), `paula.v`, `paula_audio.v`, `paula_floppy.v`, `ciaa.v`, `gary.v`, `gayle.v`, `userio.v`, `minimig.v`, `cpu_wrapper.v`, `minimig/Amber.v` | GPL v3 or later (Dennis van Weeren, Jakub Bednarski, Rok Krajnc, Alexey Melnikov, Tobias Gubener) |
| `minimig-aga/ide.v` | BSD-3-clause wording (Alexey Melnikov 2020); GPL-compatible |
| about 40 further files in `minimig-aga/` (`ciab.v`, `cia_timer*.v`, `agnus_diskdma.v`, `agnus_blitter_*.v`, `denise_playfields.v`, `paula_audio_channel.v`, `paula_floppy_fifo.v`, `paula_intcontroller.v`, `amiga_clk.v`, `minimig_syscontrol.v`, ...) | **no licence text in the header** (they belong to the same Minimig lineage; unverified) |
| `fx68k/*` | directory `LICENSE` = GPL v3; `fx68k.sv` header names the author, no licence text |
| `tg68k/*` | LGPL v3 or later in all four (corrected 2026-10-16 against the headers; T97 §5.4) |
| `misc/sdram.sv` | GPL v3 or later (Till Harbaum, Mateusz Nalewajski) |
| `nanomig.v`, `misc/hid.v`, `sd_card.v`, `sysctrl.v`, `amiga_keymap.v`, `src/tang/*/top.sv` | no licence text in the header read; all are board glue we replace (D2) |

No file read carries a "Replay only" or no-commercial clause (the VC-20 trouble, T83:47). The open point is the missing
headers and the missing repo licence, not an extra restriction. The Amiga's Kickstart and Workbench are Commodore/Cloanto/
Hyperion property: never in the repo, never in a `.cor`, never in CI (D4).

**Why it matters:** this is the first core whose memory does not fit in block RAM and whose real-time behaviour depends on
the host's DDR2 latency, so it tests the contract's C3 for the first time, and it is the first core whose picture is not
a 16-colour index. The decisive risk is chip RAM latency (P4).

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms.
- **P2 Porting surface.** What of NanoMig is kept, replaced by host services, replaced because it is Gowin.
- **P3 Clocks and reset** (C1).
- **P4 Memory** (C3): chip, slow and fast RAM, Kickstart; the latency against the chip bus cycle.
- **P5 Video** (C4): the picture format and colour depth.
- **P6 Audio** (C5).
- **P7 Input** (C6): keyboard, joysticks, mouse.
- **P8 Floppy and storage:** ADF images, write-back, hard disk.
- **P9 Pins, registers, identity, app and swap** (C2, C7, C9, T81/T84).
- **P10 Size and timing** against `pb_l2`.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the licence entry exists before any file is merged (P1)
  Given the NanoMig files with missing headers and no repo licence (§1 table)
  When  gateware derived from them is about to be committed under cores/minimig/
  Then  the route chosen by Alex (open question Q1) and the per-file list are written into §3 D1
  And   no file whose header carries no licence text is committed before that entry exists

Scenario: S2 the core elaborates on Xilinx and runs a test ROM in simulation (P2)
  Given the kept modules, inferred RAM, the Gowin PLL/DPB/rPLL wrappers and the SDRAM/MCU/HDMI parts removed
  When  the design is synthesised out of context for xc7a50t and simulated with our own 68000 test ROM
  Then  Vivado reports no black box and no Gowin cell, and the test ROM's Copper list, blitter fill and CIA timer results read back right
  And   the TRX64 and VC-20 builds are unchanged

Scenario: S3 the chipset clock is exact (P3)
  Given the PAL identity entry (28.37516 MHz chipset clock, 17.734475 MHz x 8/5)
  When  the app releases core_rst
  Then  the 7.09379 MHz enable and the 312 x 227 CCK frame are 50.08 Hz within 20 ppm, counted by the ring counter on the board
  And   the C64 and VC-20 entries still give their clocks after a swap back

Scenario: S4 chip RAM latency is measured and the machine keeps its speed (P4)
  Given a measurement build of the real-time port (random and sequential 16-byte reads, refresh and the host's other clients active)
  When  the latency distribution is taken on the board and in simulation
  Then  the table in §3 D4 is filled with measured numbers (minimum, 99.9 %, maximum)
  And   the chosen strategy (Q3) keeps the machine within the speed Alex accepts (dilation factor measured as chipset CCK per real second over a Kickstart boot)

Scenario: S5 Kickstart boots from a file with the chosen memory (P4)
  Given a Kickstart file on the stick and chip/slow/fast RAM configured by Q5, written by the app into the core's DDR2 window at core start
  When  the core is released
  Then  the screen shows the Kickstart hand (1.3) or the insert-disk screen, and a memory test reports the configured sizes
  And   no Kickstart byte is in the bitstream, the repo, the .cor or CI

Scenario: S6 the picture is straight, in colour and frame-complete (P5)
  Given Workbench 1.3 and a 32-colour game screen and a HAM screen
  When  Alex looks at HDMI
  Then  the picture is 4:3, centred, 4096-colour content shows its 12-bit colours, no tearing seam moves through the picture beyond what Q2 allows, the overlay opens over it
  And   the C64 picture after the swap back is pixel-identical to before the first swap

Scenario: S7 sound comes out (P6)
  Given Paula channels set to a tone by a test ROM
  When  the core delivers samples to the host mixer
  Then  the tone is audible with the right pitch (frequency counted from the captured stream in simulation; by ear on the board)
  And   no SID volume or filter setting changes it

Scenario: S8 keyboard, joystick and mouse work (P7)
  Given the host's key matrix, joystick state and the mouse deltas from the app
  When  Alex types in the Workbench shell, moves the pointer, clicks, and plays a joystick game
  Then  every key the chosen source (Q6) offers produces the right Amiga raw code, the pointer moves 1:1 with the mouse, the joystick port reads right in JOY1DAT/CIAA PRA
  And   a stuck key never survives a core reset

Scenario: S9 boot Workbench from an ADF and write to it (P8)
  Given an ADF on the stick inserted into DF0 by the app
  When  Alex boots, opens a drawer, saves a file, ejects
  Then  the Workbench screen appears, the file is on the ADF on the stick after the eject (or after the idle flush), read back byte-exact
  And   a read-only ADF is never written, and a pulled stick never corrupts a mounted image

Scenario: S10 the pins are idle, the identity answers, the swap works (P9)
  Given the Amiga core and its application (layer 3)
  When  the app reads the identity block, touches every window of `layer2.v`, and the core selector swaps C64 -> Amiga -> C64
  Then  the identity reads "AMIG" with the contract version, absent windows read 0 and echo the request (the NO_SAMPLER lesson, T79.1:201), no slot/IEC pin is driven
  And   no I/O read stalls the RISC-V, REST answers through the swap, Alex's photos show C64, Amiga, C64

Scenario: S11 the core fits pb_l2 and closes timing (P10)
  Given floorplan B (pb_l2: 18,000 LUT, 50 RAMB36-equivalent tiles, 80 DSP)
  When  the Amiga partial bitstream is routed against the unchanged host
  Then  WNS >= 0 and the report lists the measured LUT/BRAM, which replace the estimate in §3 D10
  And   the host (layer 1) is not rebuilt for the Amiga

Scenario: S12 a disk put in from the Freeze menu boots, as on a real A500 (P8; Alex 2026-10-16, "T85 bauen!" 2026-10-23)
  Given Kickstart polls DF0 like trackdisk (select, read /DSKCHG, step once when it is low, read again) and the machine is held
        while the application mounts: MOUNT=0 (win_eject, written even for an empty slot), the slot filled, MOUNT=1 (win_mount)
  When  the machine runs again
  Then  the drive is announced once with the last state, /DSKCHG goes low, the next step releases it, the poll reads "disk in"
  And   the same for an eject alone, a re-insert, and a mount over a disk that is already in (a swap)
  And   the same for a hard disk (MOUNT 0, size, MOUNT 1)
  Negative control: the announcement taken on ce28 only (the state before the fix) notices no disk at all in the same bench; a latch that
  ignores the announcement of a present disk misses the swap. Both must FAIL the bench.
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Licence entry before merge | review gate: entry in §3 D1 before the first `cores/minimig/` commit | review |
| S2 | P2 | Elaborates, test ROM | `designs/amiga/sim/rom_smoke` (new; Verilator/iverilog on the Verilog, our own test ROM assembled with vasm; CPU: fx68k is SystemVerilog) → `ci/sims.txt` row | CI `sims` |
| S3 | P3 | Exact clock | `designs/amiga/sim/clk_ratio`; board: ring counter | CI `sims`, board |
| S4 | P4 | Latency table, speed | `designs/amiga/sim/ddr_lat` (DDR2 latency model driven by the measured table); board latency build (new, small) | CI `sims`, board |
| S5 | P4 | Kickstart, RAM sizes | sim with the test ROM + REST-written image; board with Alex's Kickstart (third-party, by hand) | CI, board |
| S6 | P5 | Picture, colour, tearing | `designs/amiga/sim/fb_frames` (line/frame counts, RGB565 values vs Denise output); board photos | CI, board |
| S7 | P6 | Sound | `designs/amiga/sim/audio_tone` | CI `sims` |
| S8 | P7 | Keys, joystick, mouse | `designs/amiga/sim/keys` (table, every key); board by hand | CI, board |
| S9 | P8 | ADF boot, write, eject | `designs/amiga/sim/adf` (sector service against a DDR2 model); board with a third-party ADF by hand | CI, board |
| S10 | P9 | Idle pins, identity, swap | `designs/amiga/sim/windows`; app guard by REST script; board photos | CI, board |
| S11 | P10 | Fit and timing | Gitea CI `pipeline.yml` build stages, reports | CI build |
| S12 | P8 | Disk from the Freeze menu is noticed | `cores/amiga/sim/run_sdc.sh` (tb_sdc: eject + mount while ce28 is held), `run_dskchg.sh` (tb_dskchg: /DSKCHG as trackdisk polls it), `run_l2check.sh` (tb_l2 `MODE=dskchg`: the ROM polls, the bench freezes and mounts, A500 and A1200 builds); controls on each | CI `sims` |

Third-party media (Kickstart, Workbench, ADF, game disks) are supplied locally by Alex, never checked in. CI uses our own
test ROM (assembled from our source), not NanoMig's `sim/test_rom` (no licence header read).

## §3 Decision

Facts first, then D1-D11, then the open questions.

**NanoMig structure (source).** `nanomig.v` wraps the Minimig chipset (`minimig.v`) with a CPU (`cpu_wrapper.v`: fx68k by
default, TG68K with `ENABLE_TG68K`), memory and floppy interfaces and a system-control block. Clock: one 28 MHz-class clock
(`amiga_clk.v`, comment "28.375160MHz") with enables `clk7_en`/`clk7n_en`, phases `c1`/`c3`, `cck` (3.54 MHz) and `eclk`;
the Tang tops run it at 28.5 MHz from a Gowin PLL (`tang/nano20k/amigaclks.v`, 27 MHz x 19 / 6 / 8) and the SDRAM at 85 MHz.
The chipset is already enable-driven at 7 MHz. PAL beam: 227 CCK x 312 lines (long frame 313), `agnus_beamcounter.v`
(`HTOTAL_VAL 227-1`, `VTOTAL_PAL_VAL 312-1`; the file's comment: NTSC 227.5-CCK lines are not supported). Memory: chip RAM
(512 K - 2 M), slow RAM (to 1.5 M), fast RAM (Tang Nano: 4 MB), Kickstart 256/512 KB, all in one SDRAM, copied from flash at
power-up (`flash_dspi.v`), selected by `memory_config`. The chip bus is read synchronously: `sdram.sv` gets `sync = clk7_en` and
must return the word inside the same 7.09 MHz bus cycle (141 ns, 4 x 28 MHz clocks; `minimig_sram_bridge.v` timing diagram).
Floppy: Paula's MFM engine is in the FPGA; the image comes sector by sector from the MCU through `sdc_*`
(`sdc_rd/sdc_wr/sdc_sector`, 512-byte buffer via `sdc_byte_*`), up to four drives. Keyboard, mouse, joysticks arrive from the
MCU through `hid.v`; `amiga_keymap.v` maps USB-HID codes to Amiga raw codes. Video: `r/g/b[3:0]`, `hs`, `vs` at the 28 MHz
pixel rate; `Amber.v` is the Minimig scandoubler. Audio: `audio_left/right[14:0]`. IDE/HDF: `ide.v`, `gayle.v`.
Resource use: **NanoMig publishes none**. Two sources: the Tang Nano 20K has 20,736 LUT4, 15,552 FF, 828 Kbit block RAM in 46
blocks, 48 multipliers (Sipeed wiki, fetched 2026-10-12); a search-result excerpt of the atari-forum thread "Porting MINIMIG to TANG Nano 20k" says the early port with
fx68k "occupies 62 %" of the Nano 20K (atari-forum.com "Porting MINIMIG to TANG Nano 20k", search result, not re-read in full).
fx68k's own README: "just over 5,100 LEs and about 5 KB internal RAM" on Cyclone, "close to 40 MHz".

- **D1 Source: NanoMig (Alex's call, 2026-10-12).** Read-only study; the port is our own `cores/minimig/` + `designs/amiga/`
  that takes the machine logic and replaces everything board-specific. *Licence handling:* every file kept keeps its header;
  files without a licence header are not merged until Q1 is answered and the per-file list is written here (S1). The Amiga
  system ROMs are never merged.
- **D2 Porting surface (P2).** *Kept:* `agnus*`, `denise*`, `paula*`, `cia*`, `gary`, `gayle` (only if Q7 says HDF),
  `userio` (the keyboard/joystick/mouse paths), `minimig.v`, `cpu_wrapper.v`, `amiga_clk.v` (replaced by D3), fx68k (CPU,
  Q5). *Replaced by host services:* HDMI (`src/hdmi/`), OSD and u8g2, MCU link (`mcu_spi`, `hid`, `sysctrl`), SD (`sd_card`,
  `sd_rw`, `sdcmd_ctrl`), WS2812/LEDs, drive sounds, PLLs. *Replaced by C3:* `sdram.sv`, `flash_dspi.v`, the ROM loader.
  *Gowin/vendor-specific, must be inferred for Artix-7:* `gowin_dpb`, `gowin_rpll`, `gowin_clkdiv`, and the lattice/altera
  RAM wrappers (`bitplane_ram`, `sprite_ram`, `colortable_ram`, `paula_floppy_dma_buf`, `dpram*`) become inferred true
  dual-port block RAM (`ram_style = "block"`). *Not taken:* AGA (`ENABLE_AGA`, Q5), `cpu_cache_new.v` (D4 replaces it with a
  design of our own), IDE until Q7. This list is what the first elaboration (S2) must confirm; any file not read in detail is
  dropped with the board part.
- **D3 Clocks and reset (P3, C1).** Exact ratios (computed): the PAL crystal is 17.734475 MHz; the C64 `sys_clk` is
  17.734475 x 16/9 = 31.5278 MHz (the repo's PAL value, `vic_scaler.v` header); the Amiga chipset clock is 17.734475 x 8/5 =
  28.37516 MHz. So **28.37516 MHz = sys_clk x 9/10 exactly**, and = `sid2x_clk` (2 x sys_clk, `top.v:148`) x 9/20. A phase
  accumulator (add 9, fire at 10, or 9 / 20) produces an exact long-term 28.37516 MHz enable, as the VC-20's `vc20_nco` does.
  Three ways to give Minimig its "clk_28", in order of preference: **(a)** a BUFGCE-gated `sys_clk` with one pulse in ten
  removed (the Minimig sees an ordinary clock; no module edit; whether a clock buffer may sit inside the reconfigurable
  partition is a DFX rule I have not checked: UG909, to be read before the build); **(b)** `sys_clk` with the 9/10 enable on
  every register that runs at 28 MHz (Denise pixel logic, CPU phases, bridge; the 7 MHz-gated rest is already enabled);
  **(c)** a third, programmable core clock from the host's spare MMCM (contract C1/P5, not built; a host change; Q4). fx68k
  running single-cycle at 63 MHz (`sid2x`) will not close timing (its README: about 40 MHz on Cyclone) and is therefore not an
  option without multicycle constraints. Reset: `core_rst` stretched by the core, CPU held until Kickstart and memory are
  ready (D4). NTSC (227.5-CCK lines) is not supported by this Minimig at all (source above); see Q9.
- **D4 Memory (P4, C3). The decisive part.**
  *Sizes.* Block RAM cannot hold the Amiga's memory: `pb_l2` has 50 tiles of 4 KB (about 200 KB, **estimate** from 36 Kbit per
  tile); the smallest chip RAM is 256 KB, Kickstart 256-512 KB. So chip, slow, fast RAM and Kickstart live in DDR2 through
  the real-time port (T79.1:44, highest priority), inside the core's window. Footprint (**derived sum**): chip 2 MB + slow
  1.5 MB + fast 4 MB (the Nano's maximum) + Kickstart 0.5 MB + 4 ADF x 0.88 MB + two RGB565 frame buffers 1.6 MB = about 13
  MB; the window size is the host's to give (not yet built: T79.1 §7 "P2b-P7: not started"; today's edge has per-block
  ports, no window check).
  *Bandwidth (estimate).* Chip bus 7.09 M cycles/s x 16 bit = 14 MB/s; `ddr2_lean` peaks at 600 MB/s (OPT-ddr2 §3.2: one BL8
  per two 75 MHz clocks = 37.5 M commands/s). Worst case in commands per second: chip 7.1 M + fast RAM CPU about 3.5 M +
  frame buffer write 2.4 M + pixel-layer read (host) 2.4 M = about 15 M of 37.5 M (**estimate**, about 40 %, closed-page bank
  timing allows one command per cycle across 8 banks). Bandwidth is not the problem.
  *Latency (the problem).* A chip DMA slot needs its word inside the 141 ns bus cycle (source above). What exists on our
  side: the controller's own read latency is "8 cycles, about 110 ns plus the queue" (OPT-ddr2 §7 table, a bench-model
  figure); a 32-bit client read takes "about 0.4 us" (`ddr2_port.v` header, measured with MIG); the drive's and cartridge's
  worst-case reads are 670-730 ns and 546-706 ns in simulation under full video load (`mig_arb` header, `sim/lat`, **sim**).
  The real-time port with its priority and window is not built and not measured. Even the 110 ns figure plus the clock
  crossings into `ui_clk` and back (at least four to six clocks of 13 ns or 32 ns each, **estimate**) and one refresh every
  7.47 us (tRFC 127.5 ns) is above 141 ns. **A synchronous chip RAM read from DDR2 does not fit the bus cycle. This is the
  biggest risk of the spec.** The options, none of them free:
  1. *Elastic chipset clock:* gate the whole chipset (all `clk7_en` users, CPU, Agnus, beam counter) while a read is
     outstanding. Everything stays internally exact (Copper still races the same beam), but the machine runs slower than real
     time by the average stall. If every DMA cycle waited 250 ns the machine would run near 36 % (**illustrative, not a
     prediction**); audio pitch, disk speed and frame rate scale with it. It always works; it is the fallback.
  2. *Line cache plus prefetch in block RAM:* DMA and CPU streams are mostly sequential (bitplanes, audio, disk, Copper
     list, code), and a DDR2 read returns 16 bytes (8 words). A small fully associative cache of 16-byte lines (tens of
     lines, a few hundred bytes to a few KB of BRAM) serves the 7 following words of a stream in one cycle and prefetches
     the next line; a miss stalls the elastic clock (option 1) for that cycle only. How often misses happen with Kickstart,
     Workbench and games is **unknown**; it is measured in simulation with the DDR2 latency model and Minimig traces
     (S4). Writes are posted (the port takes the command; later reads see them through an in-order controller).
  3. *Look-ahead in Agnus:* DMA addresses are deterministic from pointer registers and the slot table, so a read could be
     issued early. This changes Agnus/Copper/blitter and is the riskiest rewrite; not planned.
  4. *Host change:* a dedicated low-latency DDR2 path with an open row per bank; touches the host; not planned.
  The plan in this spec is **1 + 2** (the cache reduces the stalls, the elastic clock makes it correct), with the host's
  real-time port latency measured first (S4). Kickstart is read by the CPU only (no DMA), so it only costs stalls. The
  threshold of acceptable slowdown is a decision (Q3).
  *Writes and loading.* The app writes Kickstart and the images into the window before the core is released, from files
  (`/usb*/roms/amiga/`, `/sd/roms/amiga/` as for the VC-20 ROM path, T83 §5); a ROM selection register chooses the file; no
  Commodore code is in the bitstream (shippable partial), nor in the `.cor`.
- **D5 Video (P5, C4). A host change is not unavoidable, but the obvious path is the wrong one.**
  *The VIC-II frame path (what T83 D5 did).* The scaler stores a 400 x 270 window of a 4-bit index per pixel, 64 raster lines
  in a ring, with a programmable 16-entry colour palette (`vic_scaler.v` ports `pix[3:0]`, `pal_we`, `pal_addr[5:0]`,
  `pal_wdata[7:0]`; palette written by the app only); it locks to a 312-line frame of exactly 628,992 `sys_clk` once
  (`vic_scaler.v` header; T83 §5). The Amiga chipset frame is 312 x 227 CCK = 566,592 chipset clocks x 10/9 = 629,547
  `sys_clk` (**computed**), 554 clocks (0.088 %) off the VIC frame, so the Amiga could be run 0.088 % fast to hit it exactly
  (as the VC-20 runs 0.17 % fast, T83 §5). **But the colour depth kills it:** the Amiga shows 32 colours (OCS), 64 (EHB), HAM
  and 4096-colour copper-bar screens; the path offers 16 colours at a time from a palette only the app writes. It would show
  the Kickstart screen and a 4-colour Workbench 1.3 and nothing else. Not acceptable for a core that is meant to run Amiga
  software.
  *The pixel layer path (proposed).* The host already contains a second picture source: the pixel layer reads an **RGB565
  frame buffer from DDR2** (`pix_layer.v` header: source W x H with W <= 960, scale 1-3x, position X0/Y0 on the 1920 x 1080
  frame, drawn over the C64 picture and under the overlay, opaque at global alpha 255, double buffer with a page flip at the
  frame start by a write to PX_BACK, registers on trx_io page 6 which the VC-20 core already forwards from the app,
  T83 §5 "page 6 forwarded"). The Amiga core writes its picture into DDR2 as RGB565 and the app sets the layer. No host
  change. Numbers: Denise delivers 12 bits (4 per colour); widened to R5 G6 B5 by repeating the top bit it keeps all 4096
  colours distinct (the host widens 5/6 bits back to 8 by bit repetition: exact at 0 and full scale, a few levels off in
  between, **estimate**). Format: source 720 x 540 (hires pixels, PAL line doubled: 270 lines x 2), scale 2 = 1440 x 1080 =
  exactly 4:3 on the 1920 x 1080 frame, X0 = 240. Interlace: the field's lines are written into the even/odd rows (weave),
  non-interlaced lines are written twice. Traffic: 720 x 540 x 2 B x 50 Hz = 38.9 MB/s written by the core and the same read
  by the pixel layer (**computed**), counted in D4. Costs in the core: a line buffer (2 lines of 720 x 16 bit, about 2 tiles
  **estimate**), a write sequencer (128-bit words of 8 pixels, about 100-200 LUT **estimate**), counters. SHRES (35 ns)
  pixels are dropped to hires (rare); AGA's 24-bit colour is cut to 12 bits (Q5 says no AGA anyway).
  *Frame rate and tearing.* The host's output frame is fixed at 1080p50 derived from `sys_clk` (50.12 Hz, `vic_scaler.v`);
  the core still emits a dummy VIC-II-shaped raster (504 x 312 dot counters, colour 0 = black, a few LUT) so that the scaler
  locks and the output runs (the VC-20 needed this lock, T83 §5). The Amiga frame is 50.08 Hz (**computed**) so a frame is
  repeated or dropped about every 25 s (**computed**, 0.04 Hz). With one buffer the core writes while the layer reads and a
  seam can show at the phase where writer and reader meet; with two buffers the flip needs a PX_BACK write per frame from the
  app's CPU on the core's frame interrupt (`irq[3:0]` exists, C2), jitter of the RISC-V decides whether a flip is
  missed; a missed flip repeats a frame. Whether the single-buffer seam is visible is exactly what Q2/S6 ask.
  *What this path has never done:* it is built for LVGL at 60 fps worth of UI, not as a game-speed video source; the line
  fetch of up to 256 reads and its priority against the real-time port (the video client goes first once a line is late,
  `mig_arb` header) must be checked on the board.
  *If Alex wants more later* (true 50 Hz source timing, 1080p with 3x, analogue out): that is the contract's V1a "native
  video" host work (T79.1:40) and a separate step.
- **D6 Audio (P6, C5).** Paula's `audio_left/right[14:0]` (4 channels summed per side, as in `minimig.v`) are averaged over a
  fixed number of `sys_clk` clocks (as the VC-20: 656 clocks = 48.06 kHz), scaled to signed 24 bit, handed over with
  `audio_sample_en`; the host resamples and mixes (C5, T79.1 §7 mixer decision). The A500's 3.3 kHz LED filter is not in
  the first build (Minimig has a flag; to be checked). The core's pitch follows its clock (exact if no elastic stall, D4).
- **D7 Input (P7, C6).** The host delivers `keys[63:0]` (the physical C64U matrix), `joy1/joy2[4:0]`, `restore`.
  *Joysticks:* port 2 -> Amiga port 2 (joystick), port 1 -> Amiga port 1 (shared with the mouse), five lines each;
  the second fire button (POT lines) is not available from the C64 ports. *Mouse:* the app already handles the USB mouse
  (it injects it as a 1351, `C64_PADDLE_*`/`C64_MOUSE_EN_*`, T47 §1); the core application passes the mouse deltas and
  buttons through a register window, and the core turns them into the Amiga's quadrature counters (JOY0DAT) and the left /
  right buttons (CIAA PRA6, POTGOR). *Keyboard:* the Amiga has about 90 keys (ESC, TAB, Help, Del, F1-F10, numeric keypad,
  two Amiga keys, brackets, `` ` ``, ...); the C64 matrix has 64 positions with no ESC/TAB/F6-F10/keypad. A 64-entry table
  (as the VC-20's `vic2c64`) maps what exists; the full key set needs a USB keyboard through the app's HID driver and a
  register with Amiga raw codes (NanoMig's `amiga_keymap.v` maps HID codes to raw codes; its file has no licence header, so
  our table is written from the Amiga raw code list, not copied). Which source is wanted is Q6.
- **D8 Floppy and storage (P8).** *No MCU, no `sdc_*` protocol.* The contract says bulk data goes through the memory ports
  (T79.1:48), so: the core application (layer 3, Gideon's file/USB/SD stack) reads the ADF from the stick or card into
  the core's DDR2 window (880 KB; a 1.76 MB HD image is not supported by the Amiga floppy hardware at 500 kbit/s, out), sets
  the drive registers (mounted, base address in the window, write-protect, which drive), and strobes insert/eject. The core's
  `paula_floppy` MFM encoder/decoder reads and writes sector data in that window itself through the general port (no
  deadline beyond the disk's own 200 ms per revolution, **source:** floppy is 500 kbit/s MFM). A written sector sets a dirty
  bit per track; the app flushes dirty tracks to the file after an idle time and on eject (the stick may be pulled: the
  image stays in DDR2, the flush reports an error in the overlay). Up to four drives (DF0-DF3) as NanoMig. Drive activity
  and the cylinder number go to the app through a status register (for the overlay and the case LED).
  *Hard disk (HDF/IDE, `gayle`/`ide`):* images are gigabytes, so DDR2 cannot hold them; they need block-wise access by the app
  (a request register and a staging buffer in the window; the app reads the card, ms of latency, a read cache). Not in the
  floppy build; Q7.
- **D9 Pins, registers, identity, app and swap (P9, C2/C7/C9).** Same rules as the VC-20 (T83 D8-D11, D13): all `SLOT_*`,
  IEC, user pins idle (the Amiga has none of those; no 1541 in the core, which also saves its roughly 1,690-2,000 LUT, T83 §5);
  identity "AMIG", contract version, clock table (D3), geometry notes for D5, window sizes; a config window (chip/slow/fast
  size, Kickstart select, CPU, PAL, reset, drive control, mouse and key injection, frame IRQ); everything else answers
  "absent". The application is the VC-20's kind (T83 D11a): an Ultimate-app image on Gideon's base (`build_amiga.sh`),
  packed with the partial in an `amiga.cor`, which initialises its own core (loads Kickstart, mounts ADFs, sets the pixel
  layer) and shows only what exists for this machine (overlay menu: reset, insert/eject ADF, Kickstart, memory, joystick
  swap). No monitor, SID, REU, turbo, cartridge writer, no Timewarp (Q6 of T83 applies: plain machine); Freeze is in
  (T83 Q6, changed 2026-10-13: Freeze stops the machine with a full-screen menu for every core). **TREX-internal
  until Q1.** The build is behind its own switch `TRX_CORE=amiga`, off by default, `ci/package.sh` strips everything named
  `amiga` (T83 D13) until Q1 allows otherwise.
- **D10 Size and timing (P10).** **Estimate, no measurement exists.** Sources: the early NanoMig (fx68k, OCS/ECS) "occupies
  62 %" of 20,736 LUT4, about 12,900 LUT4 (forum, above); fx68k alone is "just over 5,100 LEs" (its README); the later
  features (AGA, cache, IDE) needed trimming flags on the Nano (`DISABLE_ROM_LOADER`, `NO_WS2812`, `DENISE_EBR`, comments in
  `tang/nano20k/top.sv`), so it ran close to full. Converting Gowin LUT4 to Xilinx LUT6 depends on the netlist; I assume 0.6 to
  1.0 Xilinx LUT per Gowin LUT4 (**assumption**, no source), so the OCS/ECS chipset + fx68k is about **7,700 to 12,900
  LUT**. Our glue: pixel write sequencer and line buffer, elastic clock and line cache, DDR2 client, ADF sector service,
  register window and identity, keyboard table, audio averager, NCO: **1,500 to 3,000 LUT** (**estimate**, the VC-20's glue
  was about 2,090 LUT in synthesis with a Gideon drive, T83 §5). **Total about 9,000 to 16,000 LUT**, against `pb_l2`'s 18,000
  (50 % to 89 %). The measured limit: the TRX64 module routed at 78.0 % (14,036 LUT, run 722) and a denser attempt (88.6 %)
  did not route (T79.1 §7, runs 708/720 and 722). So the range straddles the routing limit: the low half fits, the top does not.
  BRAM: Gowin offers 828 Kbit in total, which NanoMig fits with its SDRAM-resident memory, so the chipset's own RAM
  (colour table, sprites/bitplane buffers, floppy buffer, fx68k micro/nano ROM) is **at most 23 RAMB36 equivalents**
  (bound from device capacity, **derived**); plus about 2 for the pixel line buffers and a few for the line cache: **about
  15 to 30 of 50 tiles** (**estimate**). DSP: fx68k uses none by README; TG68K uses multipliers (Q5). Timing: fx68k "close
  to 40 MHz" on Cyclone (README); at an effective 28.4 MHz on a gated clock (D3 a) it is a 31.5 MHz single-cycle path, which
  is a risk on a -1 Artix-7 next to the host's +0.013 ns worst path. Verdict: **fits on paper in the lower and middle of
  the range; not proven; the first out-of-context synthesis decides** (S11). If it does not route, the levers are: drop
  `gayle`/IDE, TG68K instead of fx68k (smaller per the same forum thread, not verified; not cycle-exact), smaller line cache, no 4th drive.
- **D11 What follows from earlier decisions (no new decision):** one core clock family from the host (T79.1 C1); video
  is the core's, host composites overlay and encodes HDMI (T79.1:40, within the existing pixel layer); peripherals are the
  host's, the core gets the matrix and joysticks (T79.1 2a); the register bus is 8 bits and carries control only, bulk data
  goes through the memory ports (T79.1:48); a swap is a cold boot, nothing survives (T79.1 decoupler); the core's
  application is on Gideon's base and brought by the `.cor` (T83 D11a); ROMs/OS images come from files at core start, never
  in the repo, CI or `.cor` (T83 D4); cartridge port and IEC idle (T83 D9); the core is a plain machine, no TRX64 features
  (T83 Q6); PAL first (T83 Q5, repeated as Q9 because NTSC is not just a clock here).

- **D12 A mount is an event that must survive the Freeze (P8, T85 2026-10-23).** The application mounts from its menu, and the menu is
  the Freeze (`run.cc` `open_menu`: `freeze->enter()`), so `ce28` stands still for the whole mount. MOUNT is a toggle (`amiga_regs.v:194`,
  one bit per drive) and `amiga_sdc.v` looked at it on `ce28` only: eject and mount write it twice (`winslot.cc:145`, written for an empty
  slot too, and `:205`), the parity is the same again when the machine runs, **no announcement is made, `disk_present`
  (`paula_floppy.v:966`, 965 before the fix) is never set**, `/DSKCHG` is never released: the insert-disk screen stays, a running Workbench does not see a
  swap. The latch itself is the real drive's (low when no disk, released by a step of the selected drive while a disk is in; the unit
  bench says so). Decision: (1) `amiga_sdc` looks at the toggles on every `sys_clk` and keeps one pending flag per drive (6, ADF and
  HDF) until `ce28` runs; it announces one drive per `ce28` (the size register is shared), with the state of that moment; (2) a mount
  announcement is a disk change: `paula_floppy` keeps it (`mount_seen`) until the `clk7_en` latch has taken it and pulls `/DSKCHG` low, so a
  disk replaced without the drive ever reading empty is noticed at the next step like a real swap. Gateware only, both cores (the A1200's copies of
  `amiga_sdc.v` and `paula_floppy.v` carry the same change, `paula_floppy.diff`/`amiga_sdc.diff` regenerated); the application and the host stay as they are.

**Open questions for Alex (one line each, not decided here):**

1. Q1: Licence/release: TREX-internal like the VC-20 (D13, never in a release), or releasable, which needs the licence of the ~40 headerless NanoMig files and `nanomig.v` clarified with the authors (no repo LICENSE exists)?
   **Answered (Alex, 2026-10-12):** "a": TREX-internal like the VC-20 (T83 D13): private repository only, never in a release, an update package or a public download; built behind its own switch, off by default.
2. Q2: Video: use the host's RGB565 pixel layer with the core writing the frame into DDR2 (no host change, D5), accepting a possible tearing seam until a per-frame flip works, or require a clean double buffer from the start, or change the host scaler for 12-bit colour?
   **Answered (Alex, 2026-10-12):** "let's start with b) and see how it feels when playing": the core writes its frame into DDR2 for the host's RGB565 pixel layer (no host change), with a clean double buffer from the start (one buffer shown, the other written, flip per frame at the frame start); judged by playing on the board, the host scaler change stays out.
   **Amended (Alex, 2026-10-12, "3"):** three frame buffers (shown / latest / writing), not two. The flip lands up to 20 ms after the frame is written (the next output frame start) and the core starts the next frame at once, so with two buffers the writer would overwrite the one still shown. The buffer count is a parameter of the core (`FB_BUFS`, default 3).
3. Q3: Chip RAM latency: elastic chipset clock plus line cache (D4 1+2) is the plan; how much slower than a real Amiga is acceptable (for example "within 5 %", "any as long as it runs")?
   **Answered (Alex, 2026-10-12):** "doesn't matter, as long as it runs for now": any slowdown is accepted for the first build; the elastic chipset clock plus line cache (D4) goes ahead, the stall rate is measured (S4) but is no gate.
4. Q4: May the host's spare MMCM give a third core clock of exactly 28.37516 MHz (host change), or does the core stay on `sys_clk` with a gated or enabled clock (D3 a/b)?
   **Answered (Alex, 2026-10-12):** "a) without a host change": the core stays on `sys_clk` and derives the 28.37516 MHz chipset timing itself (exact 9/10 enable, or a gated clock if UG909 allows a clock buffer inside the partition; to be checked first). No third host clock.
5. Q5: Chipset and memory: OCS/ECS with the 68000 (fx68k) and chip 512 KB / 1 MB / 2 MB, slow 0-1.5 MB, fast 0-4 MB first; AGA and the 68020 (TG68K) later or never?
   **Answered (Alex, 2026-10-12):** "OCS/ECS with 68000 for now, AGA later": the first build is OCS/ECS with fx68k and the proposed memory ranges (chip 512 KB-2 MB, slow 0-1.5 MB, fast 0-4 MB); AGA and the 68020 are a later step, not dropped.
6. Q6: Keyboard: C64 matrix only (about 60 keys, no ESC/TAB/F6-F10/keypad), or also a USB keyboard through the app, from the start?
   **Answered (Alex, 2026-10-12):** "a, make LARROW (the C64 left-arrow key) Escape ... that will do for now": C64U matrix only for the first build; the left-arrow key maps to Amiga ESC; no USB keyboard yet. Revised 2026-10-28 by T141 (Alex: "there too it would make sense to support USB keyboards").
7. Q7: Storage scope: floppies (ADF, four drives, write-back) only, or hard disk images (HDF/IDE with the app staging blocks from the card) in the same build?
   **Answered (Alex, 2026-10-12):** "b": the first build has both floppies (ADF, four drives, write-back) and hard disk images (HDF over gayle/IDE, the app staging blocks from SD/USB). Dropping gayle/IDE is no longer a size fallback without asking Alex.
8. Q8: Which Kickstart is the first target (1.3 on OCS or 3.1 on ECS), and may the app select it from files at start?
   **Answered (Alex, 2026-10-12):** "a, and yes, the app may choose": Kickstart 1.3 on OCS is the first target; the app may select the Kickstart from several files at start (files only, never in the repository, CI or a `.cor`).
9. Q9: PAL only (this Minimig has no 227.5-CCK NTSC line), and NTSC later as its own step?
   **Answered (Alex, 2026-10-12):** "PAL only": the core is PAL; NTSC is not planned in T85.

## §4 Out

- **AGA chipset, 68020/68030, CPU caches** (Q5), **RTG, Toccata, network (SANA-II), virtual modem**: NanoMig's "planned"
  list, not ours.
- **Hard disk / IDE** unless Q7 says yes; **CD-ROM**, **Zorro cards**, **PCMCIA / Gayle slot**.
- **Real Amiga cartridges, expansion ports, the real floppy drive, parallel/serial port pins:** the C64U has no matching
  pins; the cartridge slot, IEC and user port idle (D9).
- **NTSC** (this Minimig has no 227.5-CCK line), **HD floppies**, **SHRES (35 ns) pixels** in the frame buffer.
- **Pause-in-frames, Timewarp ring, trxmon, `.c64re`-style snapshots, UCI, SID, REU, turbo, sampler, cartridge
  writer** for the Amiga (plain machine). Freeze is in (T83 Q6, changed 2026-10-13).
- **A second video format on the host** (native-timing scaler, 12-bit palette, analogue out): T79.1 V1a host work.
- **Gowin primitives, the MCU/FPGA-Companion, OSD u8g2, WS2812, drive sounds** (`ENABLE_DRIVE_SOUNDS`): dropped with the board.
- **Copying NanoMig files into this repo** before Q1 is answered (S1).
- **Building any RTL** before the open questions are answered.

## §5 Decisions taken while building

Build started 2026-10-12 (branch `t85-amiga`). Status: gate checks done on paper, RTL not written yet (see "Stop" below).

### Gate checks (before any RTL)

- **Clock buffers in the partition (D3 a vs b): decided b, the enable.** UG909 is not available offline and I did not fetch it.
  The repo's own evidence is enough: `designs/c64_basic/build.tcl` (the "I/O and clock primitives stay static" block, run 682
  note) removes every `BUFG*`/`BUFH`/`BUFR`/`BUFIO`/`MMCM`/`PLLE` cell from `pb_l2`, and T79.1 §2a states "the host owns every
  MMCM/PLL and BUFG (7-series DFX rule: clocking stays static)". A BUFGCE-gated clock inside the core is therefore not an option.
  Consequence: the chipset runs on `sys_clk` with a global clock enable `ce28` (9 of 10 `sys_clk` clocks, minus the stall of
  D4). Every register of the Minimig that is not already gated by `clk7_en`/`clk7n_en` gets `if (ce28)`: counted with a rough
  script over the NanoMig sources, about 95 `always @(posedge clk)` blocks in about 25 modules (`cpu_wrapper`, `denise_*`
  shifters, `gayle`/`ide`, RAM wrappers, `userio`, `paula_floppy*`, `minimig_m68k_bridge`, ...); the 7 MHz-gated rest freezes by
  itself when `clk7_en` stops. fx68k only moves on its `enPhi1/enPhi2` strobes, which are derived from `ce28`.
- **Memory ports (D4): which ports.** `mig_arb` (`designs/c64_basic/ddr2_port.v`) serves the core's `c_*` port first (the
  highest priority of all clients; `ok && c_en` goes before everything), then the drive, an urgent video fetch, then the
  engine port `e_*`, then the host's own clients. Both `c_*` and `e_*` are native MIG-style ports in `ui_clk` (75 MHz), 128-bit
  data with a 16-bit byte mask, in-order reads; read data of both comes on `c_rd_data` qualified by `c_rd_valid` / `e_rd_valid`.
  So the plan is: **chip/slow/fast RAM and Kickstart on `c_*` (the real-time port in effect), frame writes, ADF/HDF sector
  traffic and staging on `e_*`.** The core crosses from `sys_clk` to `ui_clk` itself.
- **Pixel layer and the Ultimate overlay (D5, S6).** `pix_layer.v` header: the order is C64 picture, `overlay.v`, **pixel layer**,
  then the freeze text. The overlay (the Ultimate menu) therefore lies **under** the Amiga picture. The application must lower
  `PX_GALPHA` (Interface Type = Overlay: "the menu is readable through a reduced alpha") or switch the layer off (Freeze: the
  machine is paused and the menu is full screen, see "Freeze" below). The core cannot flip the layer itself: `pix_layer` is written from the host's `cw_addr/cw_wdata`
  (`top.v`), the core only gates `px_we/px_re`. So the flip is the application's, on the core's frame interrupt.
- **Triple buffer instead of double (D5); decided by Alex ("3", see Q2 amendment).** The flip lands at the next output frame start, up to one
  output frame (20 ms) after it is written, and the core's next frame starts writing right away. With two buffers the writer
  would overwrite the buffer that is still shown. Three RGB565 buffers (3 x 777,600 B = 2.3 MB, inside the 16 MB window) make it
  clean: shown, committed/latest, writing. Alex asked for "a clean double buffer"; this is the same idea with the one extra
  buffer that makes it clean. Not a reduction.
- **DDR2 window for the Amiga:** byte 0x1000000-0x1FFFFFF (16 MB, the REU / expansion-port block of the C64, unused by this
  core and its application; T20 §5.1). It is inside the bottom 64 MB, so the application's CPU can load Kickstart, ADFs and HDF
  staging blocks into it. `c_addr` is a 16-bit word address, so the core's base is 0x800000. Map: chip RAM +0x000000
  (2 MB), slow +0x200000 (1.5 MB), fast +0x400000 (4 MB), Kickstart +0x800000 (512 KB), ADF DF0-DF3 +0x900000 + n x 0xE0000,
  frame buffers +0xD00000 + n x 0x100000 (three), HDF staging +0xF00000.

### As built (2026-10-12, branch `t85-amiga`)

**Files.** Upstream, in `cores/minimig/src` (NOTICE there; TREX-internal, off unless `TRX_CORE=amiga`): the Minimig chipset
(`minimig-aga/*.v`, 50 files; `cpu_cache_new.v`, the RAM wrappers and the alternative floppy file are not taken, the unused
ones sit in `unused/` and are not compiled), fx68k (`fx68k/`), and NanoMig's wrapper `nanomig.v`. Changes to them are two: the
clock enable `ce28` (below) and the video/clock outputs of the wrapper (no Amber scandoubler: the raw 15 kHz stream and
`hblank/vblank/lace/field1/hires` go out; `clk7_phase`, the 7 MHz divider, goes out). Ours, in `cores/minimig/trx`: `amiga_ce`
(clock enable), `amiga_mem` (cache + stall + the DDR2 real-time port), `amiga_eport` (the general port), `amiga_fb` (frame
writer), `amiga_sdc` (floppy/HDF sector service), `amiga_regs` (the application's registers), `amiga_input`, `amiga_audio`,
`trx_afifo`, `amiga_core` (all of it behind the host's socket); `designs/c64_basic/layer2_amiga.v` (layer2.v's 228 ports,
no 1541 and no IEC).

**The clock enable.** `cores/minimig/tools/thread_ce28.py` puts `if (ce28)` behind every `always @(posedge clk)` of the
Minimig and fx68k (58 modules get a `ce28` input; the multi-edge blocks were done by hand). With ce28 = 1 on every clock the
machine is the original; with ce28 pulsing the whole machine advances only on the pulses. `amiga_ce` makes ce28: nine of ten
`sys_clk` clocks (28.37516 MHz exactly), minus the stall. Proof (bench `amiga_equiv`): with a random sparse ce28 the frames
and the CPU counter of the test ROM are identical to the dense run.

**Memory.** Chip, slow, fast RAM and Kickstart are the first 8 MB of the window (`amiga_mem`): a 256-line, 16-byte-line,
hashed-index, direct-mapped, write-through cache in block RAM (4 KB) between the chip bus and the DDR2 real-time port `c_*`;
a hit answers in two clocks, a miss fetches the line, a stream prefetches the next one. The request is sampled the clock
after the 7 MHz cycle starts; `amiga_ce` does not give the chipset the tick that ends phase 0 (the CPU takes its data there)
while the answer is not there. Measured in simulation (bench `amiga_mem`, a behavioural DDR2 with 8 or 20 clocks of read
latency, the real `mig_arb` priorities are not in this one, `amiga_l2` has them): the frames and the CPU counter are
identical to the ideal-memory run; stall clocks per run: light ROM 2,300-3,400 of 4.48 M (0.05-0.08 %), DMA-heavy ROM (4
bitplanes + a CPU copy loop) 7,223 (0.16 %); misses 192 of 102,813 and 568 of 127,082 reads. **Not measured: Kickstart,
Workbench, games, the real `ddr2_lean` latency.** Window layout (bytes from 0x1000000): Amiga memory +0 (Kickstart at
+0x780000), ADF DF0-3 +0x900000 + n x 0xE0000, HDF staging sector +0xC80000, frame buffers +0xD00000 + n x 0x100000.

**Video.** `amiga_fb`: RGB565, 720 x 540, lines doubled, interlace as bob, three buffers, window 36 hires pixels / 10 lines
after the blanking (the line has 753 after it, so the last word is padded with the last pixel; centred: the bench finds the stripes of the test ROM at columns 40..671, rows 14..525). The application
flips (page 6 `PX_BACK`, polled every 1-2 ms; the core snoops the write and the host's flip event). A dummy VIC-II
raster (colour 0) keeps the scaler locked. Pixel layer registers the application writes: see `doc/AMIGA.md` of the app.

**Keys.** The matrix table is in `amiga_input.v`: letters, digits and the C64's own keys by position; LEFT ARROW = Escape
(Alex), RUN/STOP = Tab, CLR/HOME = Help, C= = left Amiga, RESTORE = right Amiga, F1/F3/F5/F7 = F1/F3/F5/F7 and shifted
F2/F4/F6/F8, shifted CRSR = the other direction; CTRL + C= + RESTORE = the Amiga's reset. Missing on a C64: Del, Alt,
Caps Lock, keypad (except `+` and `*`), F9/F10, the brackets and the backslash by their own keys (the symbols are on
neighbouring keys: `@`=`[`, `:`=`'`, `;`=`;`, up-arrow=`\`, pound=`#`).

**Build.** `TRX_CORE=amiga` (the branch's `bitstream.yml`; part of `gateware_key`): `build.tcl` synthesizes `layer2_amiga` out
of context (all sources read as SystemVerilog, `INFER_DPRAM` defined, the two fx68k `.mem` files copied to where the
synthesis runs), then, after the C64 configuration, the same locked-static placement as the VC-20's. `ci/package.sh`
removes everything named `amiga`.

**Benches** (`designs/c64_basic/sim/amiga`, in `ci/sims.txt`): `amiga_sdc` (iverilog, floppy read/write, dirty log, HDF
mailbox read/write, mount), `amiga_input` (iverilog, keys, shifted cursor, RESTORE, reset combination, mouse pair, joystick
order, audio), tier 1; `amiga_equiv`, `amiga_mem`, `amiga_l2` (Verilator, 20-60 s each, the last drives the real
`mig_arb`, the application's register protocol, three buffers and checks the picture position), tier 2 until the CI image
has run them once.

### Decisions taken while building

- **Template stays NanoMig (Alex, 2026-10-12):** "then go on with NanoMig", after comparing m1nl's icepi-zero-minimig
  (its EightThirtyTwo control CPU does FAT/MFM in firmware, its SDRAM controller is welded to the chip bus, TG68K only;
  NanoMig does MFM in the FPGA behind a sector interface, has fx68k and the AGA path). icepi parts (Akiko, RTG, fast RAM
  autoconfig) may be ported later as separate small steps.

- **Clock by enable, not by a gated clock** (UG909 not read; `build.tcl` and T79.1 §2a already keep clock buffers out of the
  partition).
- **Three frame buffers** (Alex, "3"); `FB_BUFS` is a parameter of `amiga_regs`/`amiga_core` (default 3, 2 works with the
  writer overwriting the latest unshown frame).
- **Cache index hashed** with the next address bits: bitplanes at power-of-two distances collided in a plain direct-mapped
  cache (33 % misses; 0.4 % after the hash).
- **Audio is Paula's sums sampled in machine time, buffered, and resampled in wall time** (FIFO with rate adaption, see "Audio FIFO"
  below; Alex, 2026-10-13, run 406: "the sound sometimes cannot keep up ... as if it lags"). Before that it was the zero-order hold of the sums at
  the host's 48 kHz frame (no averaging; the sums change at 28 kHz at most).
- **The application's DMA window is not served** (answers at once with 0): the Amiga's RAM is in the DDR2 and the application
  reads it there (`machine:readmem`); writing from the application would bypass the cache and is not offered.
- **HDF staging moved to +0xC80000** (a third frame buffer would have overlapped +0xF00000).
- **Interlace shows each field alone ("bob")**, not a woven frame (the pixel layer scales both axes alike).

### Freeze (2026-10-13, branch `t85-amiga`; Alex: "for all cores - when I choose FREEZE it MUST freeze. Like the C64")

Built in gateware and in the app (app repository, branch `t85-amigaapp`, `doc/AMIGA.md`); proven in simulation, **not on the board**.

- **Pause.** `layer2_amiga.v` takes the contract's machine pause: `c64_halt_req` (= `machine_ctl[3] | (app_stop & app_pause)`,
  `gate_frz` is constant 1 here, so the app's `C64_STOP` is a machine pause unless JTAG register 9 bit 4 is set), `mc_halt_s[1]` and
  `machine_ctl[1]`. `amiga_ce` holds `ce28` low and freezes its phase accumulator: the state the elastic stall already leaves
  behind, so the whole Amiga (CPU, Agnus, Denise, Paula, CIAs, floppy, `amiga_fb`, `amiga_sdc`, `amiga_input`) stands where it was
  and resumes exactly. The request takes effect at a clock boundary, between two ticks, never inside one. It does not apply
  while the machine is in reset (`hold`): a reset needs ticks.
- **Safe point.** Holding `ce28` is exact at any clock because the units that talk to the DDR2 are not tied to it: `amiga_mem`'s
  request machine, command FIFO, line fill and the fast RAM handshake (`fastram_sel` falls the moment `p2_ready` toggles, so a
  request cannot be issued twice), `amiga_fb`'s line writer and `amiga_sdc`'s command queue all finish what they began while the
  machine is held. `paused` (STATUS bit 6 of the core window, `c64_halted` to the host) rises when `ce28` has been held and
  `amiga_mem.quiet` (request machine idle, no chip request taken or queued, no fast RAM request open, no line coming in, command
  FIFO and ui-side port idle), `amiga_fb.quiet` (the last line handed over is acknowledged) and `amiga_sdc.quiet` (queue and
  port idle) have all been true for 32 clocks in a row (the FIFO pointers need a few clocks to show on the other side). From then
  on nothing of the Amiga touches the DDR2 until the request falls, so the application may use the window.
- **Picture.** Unchanged: the host's pixel layer scans the DDR2 frame buffers, the front buffer is the last frame that
  `frame_done` announced, and the writer never targets the shown or the committed buffer, so a frame cut in half by the pause
  sits in a back buffer and is never shown; on release it is finished from the same line. **Audio** is muted in `amiga_audio`
  (a linear gain fade to zero in 5.3 ms while paused, back the same way; the FIFO stands still with its pairs), not cut.
- **Menu.** With Interface Type = Freeze the app sets the pause, waits for `paused`, switches the pixel layer off (the overlay menu is then
  the only thing on the screen, over the host's black C64 raster) and opens the menu; leaving it switches the layer on and
  releases the pause. Interface Type = Overlay is the old behaviour (the Amiga keeps running under a dimmed picture).
- **Simulation** (`designs/c64_basic/sim/amiga/run_pause.sh`, Verilator; `tb_l2` with `PAUSE=1`, monitor `tb_pause_mon.svh`): a pause
  is requested at random moments (also directed: while a chip request is unanswered, while the sector service transfers) and held
  from 16 clocks to 62 ms. Checks in the same run: `paused` within 600 clocks; while held no `ce28` tick, no command on `c_*`/`e_*`,
  every Minimig output frozen, no frame completes, the shown buffer is the last complete frame and does not change, audio is zero;
  a scoreboard matches every chip bus write and every fast RAM write against the DDR2 write command (data, mask, address, order)
  and is empty when `paused` rises. A second, never-paused run is the reference: hashes of every chip bus cycle (per window of
  100,000 ticks), of the picture per frame and of the buffers shown must be identical (`cmp_pause.py`). Numbers: see
  `docs/todo/T85.md`. Limit: the CPU's accesses to fast RAM cross the DDR2 and the line cache with their latency (wait states), so
  a run with fast RAM is exact for the picture but not cycle-exact for the CPU's bus cycles, paused or not; the comparison of those
  uses the chip-RAM-only ROM.
- **Not covered:** a floppy or hard disk *write* while a pause arrives (the write path of `amiga_sdc` is not exercised by any test ROM;
  its units are gated by `ce28` and its DDR2 side drains like the read path); reset during a pause (by design the pause yields).

### Debug of the board failures of runs 395 and 403 (2026-10-13, branch `t85-amiga`)

Boards: run 395 (e03c4fb) ECS chip 512K + slow 512K: Lotus intro with sound, title, hang before the race; OCS: Guru #00000003.00C01570.
Run 403 (7569599 = 395 + watchdog + `dirty_now`): ECS 512K + 512K no sound, Guru 0B (line F, task at $C01570); ECS 1M: Guru #3.C3C4C5C6;
OCS 512K + 512K: title, then the same Guru #3.C3C4C5C6; a grey/white bar at the right edge. Console: no STALL line, frames counted
on: the memory answers, with wrong data. The tools:

- **Boot bench** (`BOOT=1 ECS=1 KICK=... ADF=... sh run_l2.sh`, 70 s of machine time take about 2.5 h of Verilator on the Mac;
  `RUNARGS=+shadow=1`; `MEMCFG=` the MEM_CFG byte, default 4 = chip 512K + slow 512K, 1 = chip 1M; `+fire0=MS +firestep=MS +firehold=MS` press
  both fire buttons to get past menus): real Kickstart 1.3 and the Lotus II ADF. The bench had erased the ADF after loading it (a clear loop for
  the ADF slots ran after the load): the bench never reached the game. Fixed.
- **Shadow checker** (`tb_shadow_mon.svh`): every write `amiga_mem` takes goes into an array, every answer (cache hit, critical
  word) is compared with it, and so is the chip bus at the clocks where the CPU and the chipset take their data (end to end,
  includes the stall protocol). First mismatches are printed with the cache state and the last events of the line.
- **Cache fuzz** (`tb_fuzz.sv`, `run_fuzz.sh`, `run_fuzz_suite.sh`, `amiga_fuzz` in `ci/sims.txt`): `amiga_mem` alone behind the
  DDR2 model with random timing, driven like the chip bus (streams, writes next to them, aliasing lines, the CPU's fast RAM port),
  every read against a shadow array. 4 M bus cycles take 15 s. A negative control (the fix below removed) has to fail.

**Finding 1, stale cache line after a fill (`amiga_mem`): found by the fuzz, fixed.** A fill (a demand miss or the prefetch of the next
line) reads the line from the DDR2; a write that is queued to the DDR2 after that read leaves the line in the cache with the old word.
Run 395 closed that only while the fill was in flight (`fill_dirty`); run 403's `dirty_now` added the clock in which the install is
decided. Left open: a write that looks the line up in the next two clocks (the tag takes three clocks to show in the lookup: tag write, tag
read, `tag_q`) finds no tag, so it does not update the cached copy either; the line is then installed (valid) with the old word, and the
next read hits it. Fuzz of the 395 module: first mismatch after 17,595 bus cycles; of the 403 module (`dirty_now`): 2 to 340 mismatches in
3 M cycles at every DDR2 timing tried; every one is a read of a line whose install was followed within two clocks by a write to it
(printed events: `INST` at clock n, `WR-miss` at n+2, `RD-hit` with the old word). `dirty_now` itself breaks nothing (it removes the
same-clock case); it is not what took the sound away. The fix: `ins1`/`ins2` remember the line of an install for the two clocks in
which the lookup is blind; a write to that line in them invalidates it again (the tag write comes after the install's, the next read
refetches after the write). Fuzz after the fix: 0 mismatches in 9 timings x 4 M cycles (latency 3 to 100 ui clocks, row miss, stall up
to 30 %, with the fast RAM port). What this fits on the board (not proven there): a stale line means the CPU or Agnus reads what the memory held before a write, e.g.
the pointer a loader has just stored. The second number of Guru #3.C3C4C5C6 is `ThisTask`, and C3 C4 C5 C6 is a run of bytes of the
ADF (sector 358, offset 26: the only place in the image where those four bytes follow each other), i.e. old disk data where the new
pointer should be. The race is timing-dependent, which fits the same crash in two memory configurations and sound that is there in
one build and not in the next.

**Finding 2, the bar at the right edge (`amiga_fb`): found in simulation, fixed.** The window is 720 hires pixels after 36 skipped ones,
756; the Minimig's line holds 753 hires pixels after its blanking (`xcnt` is 753 with half a pixel at the end of the line, measured). The
line ended with 5 of the 8 pixels of the last word (columns 712-719) in the shift register: that word was never written to the line buffer,
and the DDR2 got whatever the block RAM held there (0 in simulation: a black bar in the screenshot of the boot bench; on the board whatever
the RAM was left with by the core before, grey/white). Fix: when the line ends inside a word, the word is completed with the last pixel
repeated (`tail`, a few ce28 ticks inside the blanking) and the line is handed to the ui side after that, so a pause cannot split it. The
picture position is unchanged (stripes still at column 40). Bench: `tb_l2` checks columns 690-719 of 500 rows against the border colour
(fails without the fix).

**Finding 3, a reset in the middle of a sector corrupts every later sector (`amiga_sdc`): found by reading `amiga_core`, fixed, proven in
`tb_sdc`.** `amiga_sdc` got only `sys_rst`: a warm reset (`hold`, the menu's reset; the keyboard reset) or a reset while a sector was being
streamed left the transfer state running, the read commands in the command queue and the burst answers in the answer FIFO. The next
sector was assembled from those leftovers, shifted by the missing words, and so was every one after it (the bench with the drain
removed: the next read and the two reads after it all fail). Now `flush = hold | reset_kbd` ends the transfer and drains the ui side (waits
for idle, empties the answer FIFO) before a request is taken again. The board report that Turrican II runs after a core load and Gurus after
a reset from the menu fits this; it does not by itself explain a first load of Lotus II or R-Type (not reproduced).

### Audio FIFO with rate adaption (2026-10-13, branch `t85-amiga`)

*Why.* The elastic stall holds `ce28` while a chip bus request waits for the DDR2 (D4), so Paula makes fewer samples per wall-clock
second than the host takes at its fixed 48 kHz frame (`audio_sample_en`, mclk/512, mclk 24.576 MHz). The average stall is about
0.2 %, the bursts are milliseconds; each one was a hole in the sound (Alex, run 406). Path before: `nanomig` `audio_left/right[14:0]`
(sys_clk, changing with Paula's samples at 28 kHz at most) -> `amiga_audio` held them in mclk -> 24 bit -> `top.v` `snd_l/snd_r`,
which `i2s_out` and the HDMI encoder take at `audio_sample_en`: a zero-order hold of machine time read in wall time.

*Design (`amiga_audio.v`, no new module; the strobe is wired through `amiga_core` `audio_sample_en` from `layer2_amiga`).*
- **Write side, sys_clk, machine time:** a phase accumulator advances on `ce28` only (+48,000 per tick, fires at 28,375,160 =
  591.15 ticks), so a pair (left, right, 2 x 15 bit) is stored every 1/48,000 s *of machine time*. A stall makes no pairs, a pause none.
  512 pairs x 30 bit in one block RAM, 10 bit pointers, the write pointer in Gray code to mclk (two flops).
- **Read side, mclk, wall time:** at every `audio_sample_en` (about 14 clocks of work, one shared multiplier) a linear interpolation
  between pair `r` and `r+1` at phase `ph` (16 bit); then `ph += step`, the integer part moves `r` on (0, 1 or 2 pairs).
  `step = 1 + clamp(3 x (fill_avg - 256) / 65536)`, clamp +-2 % (the gain alone gives +-1.2 % at an empty FIFO, 256 pairs below the target).
  `fill_avg` is a one-pole average of the fill (1/512 per frame, 11 ms), so a burst moves the pitch slowly and the loop settles in
  about 0.5 s. Fill below 256 (the machine is behind) -> step < 1 -> the sound plays slightly slower; above -> slightly faster.
- **Numbers:** target fill 256 pairs = 5.3 ms (this is the added latency), capacity 512 = 10.7 ms. Start/restart waits for 256 pairs.
- **Underflow** (fewer than two pairs): the read side stops, the gain falls linearly to zero in 5.3 ms (1/256 per frame; `gain` is the
  output stage's multiplier, the same used by the Freeze mute), the read side waits for 256 pairs and the gain rises again. No step
  in the output, no click, but a dip. `st_under` counts them. **Overflow** (448 pairs or more, unreachable with the clocks of this
  design: the machine cannot run faster than nominal): the read pointer jumps to the target fill (not click-free; `st_over` counts it).
- **Freeze:** the read side stands still while the mute is up (the FIFO keeps its pairs and the writes stop with `ce28`), the gain
  fades to zero in 5.3 ms and stays; on release it fades back and the sound goes on exactly where it stopped, without refill.
- **Cost (Yosys xc7 estimate, not Vivado):** about 350 LUT, 370 flip-flops, 1 RAMB18 (512 x 30 bit), 1-2 DSP48E1. The clock crossing is
  the Gray pointer only; the data words are read at least 2 mclk after the pointer moved (the pair is written the clock before).
  `st_under`/`st_over`/`st_fill` are ports of `amiga_audio`, not yet wired to a status register.

*Proof (`designs/c64_basic/sim/amiga/run_audio.sh`, Verilator, `tb_audio.cpp`; 6 s of machine time per scenario).* A 1 kHz sine (left) and a
440 Hz sine (right) are generated in machine time under random stall bursts (exponential lengths, capped); the host takes a frame every
512 mclk; the pitch is measured from interpolated zero crossings in 100 ms windows and compared with the machine's own average rate and
with what the old zero-order-hold path would have done in the same window (the ce28 ticks made in it).

| scenario | stalled | underflows | worst 100 ms window vs nominal (old path) | rms vs the machine's average (old path) | largest step / sine's own |
|---|---|---|---|---|---|
| clean | 0 | 0 | 0.09 % (1.5 cent) | 0.02 % | 1601 / 1579 |
| board (0.2 %, bursts to 3 ms, mean 0.3 ms) | 0.23 % | 0 | 0.54 % (9.4 cent) (1.78 %) | 0.12 % (0.41 %) | 1601 / 1579 |
| burst (0.15 %, bursts to 6 ms, mean 1.5 ms) | 0.15 % | 0 | 0.64 % (11 cent) (3.65 %) | 0.14 % (0.54 %) | 1601 / 1579 |
| heavy (1.2 %, bursts to 5 ms) | 1.19 % | 8 | 0.81 % (3.39 %) | 0.93 % (1.05 %) | 1601 / 1579 |
| long (one 30 ms stall) | 0.5 % | 1 | - | - | 1601 / 1579 |
| pause (Freeze 150 ms) | - | 0 | silent after 300 frames, back where it stopped | - | 1601 / 1579 |
| rate (machine 0.6 % slow all the time) | 0.6 % | 0 | 0.67 % (settling from 256 to 107 pairs) | 0.12 % | 1601 / 1579 |
| rate2 (1.5 % slow, beyond the range) | 1.5 % | 11 | - | - | 1601 / 1579 |

"Largest step" is the biggest change between two frames of the left channel; a clean 1 kHz sine of this amplitude moves 1579 per frame,
so no scenario has a click (underflow and pause included). The pitch limit of the design is the gain of 3: the loop follows a steady
slowdown of up to about 1 % (1.2 % with an empty FIFO) without an underflow; beyond that (heavy, rate2) the FIFO runs dry now and then and fades. Regression with the FIFO in: `run_input.sh`, `run_l2check.sh` (incl. the audio check), `run_pause.sh` audio/stress/storm/pause/fstorm/disk
(audio silent while held, back after; zero differences against the never-paused run) and `run_fuzz_suite.sh` all pass. Not measured:
how it sounds (board), and the real Paula's stall pattern (the model is the board's measured average with assumed burst lengths).

### Not done / not proven

- Everything on the board. No Kickstart, no Workbench, no ADF/HDF booted: only our own test ROM runs in simulation.
- The application side is in the app repository (branch `t85-amigaapp`, `doc/AMIGA.md` there).
- Audio FIFO: `st_under`/`st_over`/`st_fill` have no status register yet; the sound on the board (run 406 lagged) is untested with the FIFO.
- NTSC (Q9), AGA/68020 (Q5), hard disk beyond what the mailbox gives, the A500's low-pass filter, serial port, RTC (now T191),
  second fire button, USB keyboard (Q6).
- The Vivado numbers: see "Results" (filled from CI).


### Board test, step by step (for Alex; nothing of this has been run)

Files: from the CI run of `t85-amiga` (builder cache directory of the run, host and partial of the SAME run): `c64_basic.bit`
(host with the C64 module), `c64_basic_pb_l2_amiga_partial.bit`; from the app repository branch `t85-amigaapp`:
`build/build_amiga.sh` gives `ultimate.bin`; the `.cor` is packed with `tools/cor_pack.py pack -o amiga.cor --name Amiga
--version V --partial c64_basic_pb_l2_amiga_partial.bit --app ultimate.bin --load 0x30000 --entry 0x30000` (TRX64-Ultimate
`tools/cor_pack.py`, as for `vc20.cor`). Third-party files, supplied by you: a Kickstart 1.3 ROM (256 KB) as
`/USBn/roms/amiga/kick13.rom` (any name ending in `.rom`; 512 KB ROMs work too), a Workbench 1.3 ADF as `/USBn/df0.adf`
(auto-mounted) or any ADF through the file browser ("Mount DF0").
1. System Mode PAL. EF3 out of the port. Start as for the VC-20: host from SRAM or flash, host app, `amiga.cor` on the stick,
   select it in the core selector (T84).
2. Expect, within a few seconds: the Ultimate overlay message "Amiga ... " or nothing, then the Kickstart screen: on 1.3 the
   white screen with the hand and "Insert Workbench disk" (grey/white background, hand, disk icon); with `df0.adf` mounted
   Workbench 1.3 boots instead. Photo.
3. Keyboard (C64U keys, left arrow = Esc): in the Workbench CLI type `dir` + RETURN. Missing keys are listed in the spec.
   Ctrl + C= + RESTORE resets the Amiga.
4. Mouse: USB mouse moves the pointer; left/right button; joystick in port 2 (Amiga joystick port), a game with a joystick.
5. Sound: a game or a tracker; Kickstart's disk-insert sound is none, so use a program that plays.
6. Floppy write: in the CLI `echo hello >df0:test` (write enabled), eject DF0 from the task menu, mount again: the file is
   there (the ADF on the stick changed).
7. Hard disk (optional): `Mount HD0` with an RDB `.hdf` (Kickstart 2.0 or newer for a bootable one; 1.3 needs a mountlist).
8. Diagnostics if it does not come up: REST `GET /v1/machine:readmem?address=0&length=64` reads the Amiga's memory (chip RAM
   at 0, vectors); the core window registers at `0x101A0000` (ID "AMIG", FRAMES at 0x0AA must count at about 50 per
   second, FB_LATEST 0x0AC, STATUS 0x0A8; counters: write the number to STAT_SEL 0x0B8 (0 reads, 1 cache misses, 2 writes, 3 prefetches, 4 stall clocks), read 0x0BC-0x0BF). A picture that is black while FRAMES
   counts is the pixel layer (PX_BACK); no FRAMES means the machine is held (CTRL 0x0A0 bit 0) or has not left the 4-frame
   reset of the Minimig.


### Results of CI (branch `t85-amiga`)

- **Run 762 (71c5fd3, 2026-10-12):** lint, sims (all T85 benches of tier 1, including the iverilog latency bench on `ddr2_lean`) and
  app green; host + C64 module built, WNS +0.013 ns (as in the VC-20 runs). The Amiga module **synthesized out of context**
  (Vivado accepted the Minimig and fx68k sources), but its first netlist did not fit: 140,232 flip-flops, 379,422 LUT, F7/F8
  muxes 35 k / 17.6 k, 12.5 block RAM tiles, 7 DSP. Cause: the inferred IDE sector buffers of `ide.v` (`buf_l`/`buf_h`, 4096 x 16 each,
  two ports in one process) were built from flip-flops (131,072 of the 140,232); the rest of the module is about 9,000
  flip-flops. Fix (commit 8f2807b, not yet built): two true dual port block RAM modules (`trx_tdp16`); the IDE bench is green with them.
  Nothing else in the log of run 762 gives a LUT count of the rest (the report line filter missed `Slice LUTs*`; fixed in `build.tcl`).
- Earlier runs on this branch were stopped by me on purpose or replaced by newer pushes (the first carried the VC-20 switch);
  one failed in 65 s on a Tcl error (`read_verilog` has no `-verilog_define`).

- **Run 393 (bda6c77, 2026-10-12/13; log `c64_basic.vivado.log.gz` in `/srv/trx64-build/cache/bit/c64u-706fc9798f60e878/`):**
  host + C64 module green (WNS +0.013 ns). **Amiga module, out-of-context synthesis (measured): 11,914 LUT = 66 % of `pb_l2`'s 18,000**
  (10,410 logic, 1,458 LUT-RAM, 46 SRL), **8,945 FF, 16.5 block RAM tiles (6 RAMB36 + 21 RAMB18), 7 DSP**. By block (LUT / FF): fx68k
  3,242 / 1,466; Paula 2,196 / 884 (999 of it LUT-RAM: the floppy buffers); Agnus 1,559 / 670; Denise 716 / 1,090; Gayle 531 / 208 (4 RAMB36: the
  IDE buffers); our modules together about 2,700 / 3,800 (`amiga_sdc` 869 / 934, `amiga_mem` 446 / 681, `amiga_regs` 460 / 943,
  `amiga_fb` 211 / 242, `amiga_input` 213 / 141, `amiga_core` glue 31 / 299, `amiga_eport` 7 / 288). This is the size verdict: it fits
  `pb_l2` on LUT (66 % < 78 %) and block RAM (16.5 of 50), and it is not the TG68K/AGA variant (OCS/ECS, fx68k).
  Placement inside `pb_l2` completed in 44 s. **Routing failed**: `route_design` ran 63 min (until the CI limit) and ended "not legally
  routed, 10 node overlaps". The overlap count per iteration fell from 4,228 to about 15-20 within the first minutes and then sat on that
  plateau for five global iterations (Route 35-447: congestion prevents routing all nets); timing was fine all along (WNS +0.002 ns
  intermediate). The nets that stayed in conflict were all in `amiga_regs`: the read multiplexer and its wide input registers (`data44..49`,
  `hdf_size0`, `p_q`, `h_q`, `p_b`, `q[7]_i_30`). Cause (from the hierarchical report): `amiga_regs` had 943 flip-flops (five 32-bit counters
  with three register copies each, the 32-bit mailbox LBA with three copies, a 100-way byte read multiplexer) and `amiga_core` another 299 for
  double-registered wide configuration vectors with `ASYNC_REG` (which packs them into slices), all at the host's `cw_*` pins on the edge of
  the partition. Fix (commit after bda6c77, not yet built): counters behind one select register (`STAT_SEL`/`STAT_VAL`, 4 bytes of the old 20);
  the mailbox request is copied once at its toggle; the dirty-log head is read in its own clock; the read is two stages (32-bit word
  registered, then the byte), the acknowledge comes two clocks after the request; all configuration is copied once into the core at a
  commit toggle (any write to CTRL, MEM_CFG..IDE_CFG or a MOUNT register) instead of two `ASYNC_REG` copies; `hdf_size` reads back 0.
  The register map changed in two places: counters at 0x0B8/0x0BC (was 0x0C0-0x0D3) and HDF sizes are write-only.


### Board results (Alex, 2026-10-13; CI run 395, host + partial of the same run, T84 host app, `ultimate.bin` 3bba9048)

- **Works:** the Amiga core loads from `amiga.cor`; the app finds `/usb1/roms/amiga/kick13.rom` (Kickstart 1.3, exec 34.2, the 256 KB
  ROM doubled to 512 KB; the first search before USB mounted printed "Kickstart not found" once, the retry found it); console
  "[amiga] chip 512K slow 512K fast None OCS, 2 floppies", "Kickstart loaded, core running"; DF0 = Lotus Turbo Challenge II (ADF)
  boots, the intro starts. The USB mouse's left button reaches the Amiga (a click on the Guru screen rebooted it); the joystick works;
  with the ECS setting the intro music plays ("sound works too").
- **Open 1, Guru Meditation #00000003.00C01570 (address error, task at $C01570 = slow RAM) with the OCS setting.** The same
  memory setting (chip 512K, slow 512K) with the ECS setting does not guru. The chipset flag changes one thing in the memory decode:
  with ECS and exactly 512K chip + slow RAM, Gary lets Agnus's DMA use the slow RAM as the second 512 KB of chip RAM
  (`gary.v`, `sel_slow[0] = ecs && memory_config == 4'b0100 && dma_address[20:19] == 01`); OCS has no such mapping.
- **Open 2, Lotus II with ECS: black screen and a stuck, repeating sound "shortly before the start" of the game.** Looks like the
  whole chipset stopping (Paula replays its last buffer while DMA is frozen) rather than a 68000 crash. A watchdog was added
  (STAT_SEL 5-7, printed by the app on the console every second while a request is stuck).

### Disk change: a disk put in from the Freeze menu is noticed (2026-10-23, branch `t85-diskchange`; D12, S12)

**Cause (code).** `amiga_sdc.v` detected the MOUNT toggles inside `if (ce28)`; the Freeze holds `ce28` (`amiga_core.v:182`/`:191`) and
the application mounts frozen (`run.cc` `open_menu`). `win_mount` calls `win_eject` first, which writes MOUNT=0 unconditionally
(`winslot.cc:145`), then MOUNT=1 after the fill (`:205`): two toggles, parity unchanged when `ce28` returns, no `sdc_img_mounted`
pulse, `disk_present` stays 0. The same loss hit the hard disks (`sectors.cc:32`/`:89`). Not the latch: `paula_floppy.v:610` is the real
drive's, and the full-chain bench below shows a step releasing it as soon as the announcement arrives.

**Built.** `amiga_sdc.v` (and the A1200 copy): `mnt_pend[5:0]`, set by the unconditional toggle compare (last in the clocked block), taken
one per `ce28`. `paula_floppy.v` (and copy): `mount_seen` per drive, set by the announcement, cleared by `clk7_en` after the latch took it.
Benches (builder, container `trx64-sim-env:4`): `run_sdc.sh` (tb_sdc, ce28 held, DF1 and HD0 eject + mount: one announcement each),
`run_dskchg.sh` (new, `tb_dskchg.v`: 12 checks on `/DSKCHG` as trackdisk polls it, shared file and A1200 copy), `run_l2check.sh`
(`tb_l2` `DSKCHG=1`, test ROM `MODE=dskchg` polls DF0 and logs `before<<8|after` per poll at chip $400; the bench freezes and mounts
at frames 7, 10 (eject only), 12, 15 (a swap)). Expected log: no disk noticed before the first mount, then `0001` (the step released it)
three times, `0101` (disk in, no change) between. Results are in the TODO history of T85.
**Not covered:** Kickstart itself (no ROM here; the poll is our test ROM's, written from the task's description of trackdisk),
the board.

## §6 A second core: the Amiga 1200 (AGA), 2026-10-14

**Alex: "then open a second core, and build it"** (translated), after we found that AGA is not the
obstacle it looked like.

**What the measurement showed.** Our sources are minimig-AGA (`cores/amiga/rtl/src/minimig-aga/`),
and AGA there is not a parameter but a *signal*: `minimig.v` wires `.aga(chipset_config[4])` into
Denise and Agnus, and `denise.v` gates the eight bitplanes, the palette banks and the sprite
resolution with it. `chipset_config` comes from a register the application writes
(`amiga_core.v:176`), and Vivado cannot fold a writable register away - so the AGA logic is already
inside the 13,034 LUT the OCS core occupies. It has simply never been switched on.

**Why a second core and not a second setting.** The application is a prebuilt binary here (T93: the
core applications are not built in this repository), so nothing can write that register bit for us.
What a core *can* decide is what the machine is before any application has spoken: `amiga_core` and
`layer2_amiga` gained `CHS_INIT` and `MEM_INIT`, the power-up values of `chipset_config` and
`memory_config`, defaulting to today's OCS and 512 KB. `cores/amiga1200/core_sources.tcl` is the
Amiga core's recipe with `-generic CHS_INIT=16 -generic MEM_INIT=3`: AGA on, 2 MB chip RAM. (Since T102 also
`-generic HDF_SECT=1`: the hard disk size register in sectors. The power-up values hold only until the application's first
configuration commit; the A1200 application, T102, commits the same AGA and 2 MB.)

**One gateware, two cores.** `cores/amiga1200/` held a descriptor and that recipe, nothing else (since T97 step 2 it also holds the 68020: TG68K and a copy of `cpu_wrapper.v`, §5.4 there). It
names the Amiga core in its `sources`, so a change there rebuilds it too and the two can never drift
apart, and its `top` is `cores/amiga/layer2_amiga.v`, so the contract check sees it instead of
skipping it for want of a file. Two rules were widened for this, both deliberately: a core may name
another core in `sources` (`tools/core_toml.py`), and a file may be in two `core:` sections when
exactly one of them owns it (`ci/layers_check.sh`). A file in a core: **and** the host: section
stays an error. A core still never edits another core's files (T94 D12) - these are read, and the
difference is expressed in generics.

**What it was not, yet (superseded 2026-10-16).** The first A1200 core was an AGA chipset on fx68k, a 68000 (3,209 LUT of
the 13,034), "a machine that never existed". Step 2 (T97 §5.4) puts TG68K in as a 68020 in `cores/amiga1200/` (the OCS core keeps
fx68k); that is built and benched, the Vivado numbers are open. The first core was good enough to answer the two questions
that matter: does our Denise/Agnus run in AGA mode at all, and does the DDR2 path carry eight
bitplanes at 32-bit fetch.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->
