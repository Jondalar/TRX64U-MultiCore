# T88 — An Atari 800 XL core, ported from the MiSTer Atari800 core (Mark Watson), runs as a core in the T79 host/core split

**Status:** BUILDING (2026-10-14): Q1-Q12 decided (2026-10-13); the machine, its glue and the layer-2 module exist, the whole core runs under GHDL
with a test OS (5.8), routes inside `pb_l2` against the host (5.9) and its Freeze bench passes for a reason (5.10). Since
2026-10-14 the real XL OS and BASIC are in the repository (`cores/atari800/roms/`, Alex allowed it) and `run.sh os` boots
them (5.11). Not on the board yet, and the application has no Atari window (T93).
**Status check 2026-11-13:** DONE (closed by Alex 2026-10-28, 448c6f613): the core runs on the board; the disk/.xex/cartridge media path continues in T108. Untested on hardware: sound, keyboard layout, PAL/NTSC switch (docs/todo/T88.md, last section).
**Repo:** TRX64-Ultimate (gateware under `cores/atari800/` and `host/layer2_atari.v`, app side, tools). The
sources are read from outside; nothing is copied into this repo by this spec.
**Number:** T88.
**Milestone:** M2.
**Origin:** Alex, 2026-10-12 night: "4. Atari 800 XL" in the order of the cores (`docs/todo/T79.md`); 2026-10-13: a
spec for it, "the Nano cores are already there" (translated). Spec only, no gateware code.
**Depends on:** T79 (host/core split, contract `T79.1-host-core-contract.md`), T83 (VC-20: the first non-C64 core, the
clock/RAM/ROM/app pattern), T85 (Amiga: the pixel layer path, the 9/10 enable, Freeze, DDR2 window), T87 D15/D16 (the
slim C64 is the reference every core routes against).

Adopted 2026-10-12 from the C64RE spec template (`C64ReverseEngineeringMCP/specs/_TEMPLATE.md`, Alex: "we use that
from now on"). Older specs keep their own layout.

Citations: `C1`..`C10` are the sections of `T79.1-host-core-contract.md` (it lives in the dfx worktree, not on this
branch); `T83:`/`T85:` are the specs on branches `t83-vc20`/`t85-amiga`; `D4` etc. without a prefix are this spec's.
Source facts were read on 2026-10-13 from scratch clones of `MiSTer-devel/Atari800_MiSTer` (commit 1440e41, 2026-08-31,
`M`), `carlosbravoa/atari800_tang_nano20k` (commit 3f0ad76, `N`) and `scrameta/atari_xl_fpga` (commit adcc444,
2026-10-04, `U`). Nothing was built, synthesised or simulated by us.

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to host more than one machine; the Atari 8-bit family is fourth in
his order of the cores (`docs/todo/T79.md`, 2026-10-12 night).

**What he wants:** pick "Atari 800 XL" in the core selector (T81/T84), the board swaps layer 2 by partial
reconfiguration, and an 800 XL (PAL) boots from its OS ROM to the BASIC READY screen, with keyboard, joystick,
picture, sound, disk images (ATR) served by the app, cartridges and `.xex` programs, and Freeze that really freezes.

**Today:** two non-C64 cores exist on the contract or are specified (VC-20 T83, Minimig T85). The contract's edge is
still C64-shaped (228 ports, 2,180 bits, `pix_color[3:0]`). The VC-20 proved the 16-colour scaler path; the Amiga spec
proved that anything with more colours goes through the pixel layer (RGB565 frame buffers in DDR2). The Atari sits in
between and needs a decision on both counts (P5).

**What the source is.** The brief said "the Nano cores are already there". **Checked: there is no Atari 800 core among
the MiSTle-Dev repositories** (GitHub API, 2026-10-13: `A2600Nano` is the Atari 2600, `MiSTeryNano` the Atari ST,
`NanoApple2`, `VIC20Nano`, `C16Nano`, `C64Nano`, `NanoMig`, `NanoMac`, `snescore`; no 800/XL/XE). What exists:

| Candidate | What it is | State |
|---|---|---|
| `MiSTer-devel/Atari800_MiSTer` (`M`) | Mark Watson's `atari800core` VHDL (ANTIC, GTIA, POKEY, PIA, 6502 wrapper, SIO emulation, cartridge logic, VBXE, PokeyMAX) in `rtl/common/`, plus Sorgelig's MiSTer wrapper `Atari800.sv`; Cyclone V | active (2026-08-31) |
| `scrameta/atari_xl_fpga` (`U`) | Mark Watson's own tree behind it: many boards, ZPU firmware, the same core | active (2026-10-04) |
| `carlosbravoa/atari800_tang_nano20k` (`N`) | a Tang Nano 20K port of `M`, **not** by MiSTle-Dev or Till Harbaum; PicoRV32 as IO CPU, SDRAM, genlocked 3x HDMI, SD/OSD/PC link; README: started "as a vibe-coding experiment"; NTSC only | active (2026-10-13), a snapshot of an older `M` |
| `rampa069/Atari800xl_Mist` | MiST port, 2020, no licence | stale, not used |

`N` and `M` hold the same core files: a `diff -rq` of `rtl/common` shows 11 files that differ (address decoder, ANTIC with
the HSCROL one-line fix, `atari800core*.vhd`, `atari5200core`, GTIA, `shared_enable`, `ultime`, `vbxe` (stubbed), `sio_handler`,
`simple_low_pass_filter`); `N` lacks `M`'s newer `pokeymax/`, `pokey_mixer_simple` and `large_bram` and carries its own
`pokey_mixer`/`pokey_mixer_mux`.

**What the core is (read from `M`):** the machine is one clock `CLK = cycle_length x 1.79 MHz` with `cycle_length` 16 or 32
(`atari800core.vhd:21`, `shared_enable.vhdl:19`, "tested 16 and 32 only"); all state advances on `CLK` with internal
enables from `shared_enable`/`enable_divider`. Memory is **one request/complete handshake** (`SDRAM_REQUEST`,
`SDRAM_REQUEST_COMPLETE`, 25-bit address, 32-bit data, `atari800core.vhd:139`), with optional `internal_ram` in block RAM
"at start of memory map" (`atari800core_simple_sdram.vhd:38`) and the OS/BASIC ROM either in that memory
(`ROM_IN_RAM <= '1' when internal_rom=0`, `atari800core_simple_sdram.vhd:409`) or internal. `PAL` is an input
(`atari800core.vhd:181`). Video is GTIA colour (8-bit RGB through `gtia_palette`, Altirra or laoo palette), audio is
16-bit L/R from POKEY (`AUDIO_L/R`, `:54`). The keyboard is POKEY's own scan: `KEYBOARD_SCAN[5:0]` out,
`KEYBOARD_RESPONSE[1:0]` in (`:76`); console keys START/SELECT/OPTION, joysticks through PIA port A and `GTIA_TRIG`,
paddles through `POT_IN[7:0]`. Disks are not in the core: SIO is a UART pair of FIFOs (`sio_handler.vhdl`: transmit
FIFO, receive FIFO, a snoop of the 5-byte command frame) and **the disk drive is software on the IO CPU** (in `N`: the
PicoRV32 firmware reads the ATR from the SD card and answers the SIO frames, `sio_disk_pipeline.md`). `HALT`
(`atari800core.vhd:186`) reaches only `PAUSE_6502` of `shared_enable.vhdl` (`:138`, `skip_cycle`); ANTIC, GTIA and POKEY
have no pause input. The 6502 is `cpu_65xx.vhd` (FPGA64 lineage, Peter Wendrich).

**Licence (read from the file headers, not from the README):**

| File(s) | Terms as written in the file |
|---|---|
| `M` `rtl/common/**` (68 of 90 source files: ANTIC, GTIA incl. palette, POKEY, PIA, `cpu.vhd`, `shared_enable`, `address_decoder`, `atari800core*.vhd`, `sio_handler`, utils) | "(c) 2013-14 Mark Watson. I am happy for anyone to use these for **non-commercial** use. If my vhdl files are used commercially or sold please contact me for explicit permission (scrameta@gmail.com). This applies for source and binary form and derived works." (same text in `COPYRIGHT_NOTICE`) |
| `cpu_65xx.vhd` | "FPGA 64 ... Copyright 2005-2011 by Peter Wendrich ... **All Rights Reserved**" (no permission text) |
| `cart_logic.vhd`, `freezer_logic.vhd`, `pbi_rom.vhdl` | LGPL v2 or later (Matthias Reichl; `pbi_rom` W. Mostowski 2025) |
| `vbxe.vhdl`, `tape_handler.vhdl` | W. Mostowski 2025/2026; `vbxe.vhdl`: "happy for this to be reused in any way suitable by anyone as long as (a) I am informed by email about it, (b) no interests or rights of the original authors of VBXE are violated"; `tape_handler` header names the author only |
| `fifo_receive/transmit/tape.vhd` | Quartus `scfifo` megafunction output (Altera), no licence text; replaced by inferred FIFOs |
| `gtia_palette.vhdl` | Watson's notice; the palette data is "from laoo and Altirra" (`COPYRIGHT_NOTICE`) |
| `internalromram.vhd`, `mult_infer.vhdl`, `bram.vhd`, `pll*`, `hps_ext.v`, `articolor.sv` | no licence text in the header (small glue / Quartus-specific, replaced) |
| `Atari800.sv` and `sys/` (MiSTer framework) | GPL v2 or later (Sorgelig); board glue, not used |
| `U` (`atari_xl_fpga`) README | "mixed and, in places, unsettled"; for the author's parts "free to use; for commercial use or building significantly on it commercially, get in touch first"; third-party parts keep their licences |
| `N` port files | CC BY-NC-SA 4.0 or GPL-3.0-or-later; nand2mario's IO subsystem GPL-3.0; SDRAM controller Apache-2.0; its own `LICENSES/` map is "authoritative" for the port |

There is **no "Replay only" clause** (the VC-20 trouble, T83:47) and no GPL-incompatible *extra restriction* beyond
"non-commercial", but the core is not GPL either: Watson's notice allows non-commercial use and requires his
permission for commercial use or sale, including binaries and derived works. The TREX-internal rule (T83 D13) covers
it: the core, its partial bitstream and its sources never leave TREX. One file is a real question: `cpu_65xx.vhd` has
no permission text at all (Q2).

**Why it matters:** the Atari is the first core whose *own* bus cycle is long (564 ns) and whose memory the core waits on
with a handshake, so it tests whether a core can live on the block RAM of `pb_l2` and still use the DDR2 ports for
the rest (banked RAM, cartridges). It is also the first 8-bit core with a 256-entry colour palette and a software disk
drive, so it tests the pixel layer path for a cheap case (11 MB/s, against the Amiga's 39 MB/s).

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms.
- **P2 Porting surface.** What is kept, replaced by host services, replaced because it is Altera/Gowin-specific.
- **P3 Clocks and reset** (C1).
- **P4 Memory and ROMs** (C3 and the app's ROM path): RAM, banking, OS/BASIC ROM, the DDR2 part.
- **P5 Video** (C4): format, colour depth, frame rate against the host's frame.
- **P6 Audio** (C5).
- **P7 Input** (C6): keyboard, console keys, joysticks, RESET.
- **P8 Storage and loading:** SIO disk drives (ATR), `.xex`, tape.
- **P9 Cartridges and connector pins** (C7): cartridge images, the idle slot.
- **P10 Registers, identity, application, swap and Freeze** (C2, C8, C9, T81/T84).
- **P11 Size and timing** against `pb_l2` and the C64 reference.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the licence route is written before any file is merged (P1)
  Given Watson's non-commercial notice on the core and "All Rights Reserved" on cpu_65xx.vhd
  When  gateware derived from them is about to be committed under cores/atari800/
  Then  the TREX-internal rule (T83 D13) and the answer to Q2 are in this spec's §3
  And   the build sits behind its own switch, off by default, and ci/package.sh strips everything named atari800

Scenario: S2 the core elaborates on Xilinx with no Altera/Gowin cell (P2)
  Given the kept entities, inferred RAM/ROM, VBXE and PokeyMAX tied off (Q7, Q8), PLL/BRAM wrappers removed
  When  the design is elaborated and synthesised out of context for xc7a50t
  Then  Vivado reports no black box, and GHDL elaborates the same tree
  And   the TRX64 build is unchanged (same LUT, timing met)

Scenario: S3 clocks are exact (P3)
  Given the PAL identity entry (machine cycle 1.7734475 MHz = 17.734475 MHz / 10; core tick 16 x that = 28.37516 MHz)
  When  the core runs on sys_clk with the 9/10 enable and the app releases core_rst
  Then  the ring-counter reading of 35,568 machine cycles per frame is 632,320 sys_clk (or the locked variant, Q9)
  And   the C64 entry still gives the C64 clocks after the swap back

Scenario: S4 RAM and ROM come from the app, not from the bitstream (P4)
  Given a layer 2 with no ROM contents baked in
  When  the app writes ATARIXL.ROM (16,384 B) and BASIC.ROM (8,192 B) through the ROM window and resets
  Then  the screen shows the BASIC READY screen from those ROMs and MEMTOP (PEEK(741), PEEK(742)) shows the value of a 64 KB XL with BASIC
  And   with a 130 XE configuration (Q3) the extra banks are reached through PORTB and a RAM test passes
  And   no Commodore or Atari code is in the bitstream, the .cor or CI

Scenario: S5 the elastic memory never changes what the machine does (P4)
  Given extra RAM banks or a cartridge in DDR2 (Q3, Q5) and the DDR2 busy
  When  a read is not back at the point ANTIC or the 6502 needs it
  Then  the whole core is held for those ticks and the machine's bus cycles are identical to a run with an instant memory
  And   a base 64 KB machine never touches the DDR2

Scenario: S6 the picture is complete and frame-correct (P5)
  Given the PAL machine and the pixel layer set up by the app (source window, scale, position)
  When  a test program draws known ANTIC modes (2, 4, 8, F, GTIA modes 9/10/11) with every palette entry
  Then  the frame buffer shows the same pixels as the core's colour output, all 256 palette entries as RGB565
  And   the C64 picture after the swap back is pixel-identical to before the first swap

Scenario: S7 sound comes out (P6)
  Given POKEY AUDF1/AUDC1 set to a tone
  When  the core delivers samples to the host mixer at 48 kHz
  Then  the frequency counted from the captured stream is the programmed one within the clock error of S3
  And   no TRX64 SID setting changes it

Scenario: S8 keyboard, console keys and joystick work (P7)
  Given the host's key matrix, joystick state and restore
  When  Alex types a BASIC line, presses START/SELECT/OPTION, BREAK, moves a joystick, presses RESTORE
  Then  the characters appear as typed (the 64-entry table, every key), CONSOL reads the keys, STICK(0) and STRIG(0) read right
  And   RESTORE does the Atari RESET (warm start), not a cold boot

Scenario: S9 an ATR boots and writes back (P8)
  Given an ATR on the stick mounted by the app as D1:
  When  the Atari boots from it and saves a file, then the image is ejected
  Then  the file is in the ATR on the stick, byte-exact; a read-only ATR is never written, a pulled stick never corrupts the image
  And   SIO command frames are answered inside the OS's ACK/complete timeouts (measured on the bench and on the board)

Scenario: S10 a cartridge image and an .xex run (P9, P8)
  Given an 8 KB, a 16 KB and a banked cartridge image, and an .xex
  When  the app loads them (cartridge into the cartridge region, the .xex through the loader)
  Then  each starts (autostart or by the cartridge's own menu)
  And   the real cartridge port stays idle: no oe, no transceiver on, a plugged C64 cartridge changes nothing

Scenario: S11 the app's windows answer without hanging (P10)
  Given the Atari core and the Ultimate app running
  When  the app reads the identity block and every window it knows (r_drive, r_gcr, cw, dma, port, sid, reu, sampler, uci)
  Then  identity reads "A8XL" with the contract version; windows the core does not have read as absent
  And   no I/O read stalls the RISC-V (the NO_SAMPLER lesson, T79.1)

Scenario: S12 Freeze freezes (P10)
  Given the Atari core running a program with sound
  When  the app sets the machine pause (Freeze, Interface Type = Freeze) and later releases it
  Then  ANTIC, GTIA, POKEY, PIA, the 6502, the SIO bridge and the picture writer hold between two whole ticks, sound is zero
  And   after the release the bus-cycle sequence equals that of a never-paused run; the DDR2 is quiet when `paused` rises

Scenario: S13 swap C64 -> Atari -> C64 on the board (P10)
  Given the C64 core running with a program on screen
  When  the core selector picks the Atari, then later TRX64 again
  Then  Alex's photos show the C64 screen, the Atari READY screen, the C64 screen; REST answers throughout
  And   the app does not offer C64-only actions (SID, REU, cartridge writer, monitor) while the Atari is loaded

Scenario: S14 the core fits pb_l2 and closes timing (P11)
  Given pb_l2 (18,000 LUT, 50 RAMB36-equivalent tiles, 80 DSP) routed against the locked slim-C64 host (D15/D16, T87)
  When  the Atari partial bitstream is routed
  Then  WNS >= 0 and the report lists measured LUT/BRAM/DSP, which replace the estimate in D11
  And   the host (layer 1) is not rebuilt for the Atari
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Licence settled before merging | review gate: §3 entry before the first `cores/atari800/` commit; `ci/package.sh` strip check | review, CI |
| S2 | P2 | Elaborates on Xilinx | `host/sim/atari/run_elab.sh` (GHDL, as `vc20-ghdl`) + out-of-context Vivado synth → `ci/sims.txt` row | CI `sims`, CI build |
| S3 | P3 | Exact clocks | `sim/atari/tb_atari_clk` (cycles per frame in `sys_clk`); board: ring counter by hand | CI `sims`, board |
| S4 | P4 | ROMs from the app, memory map, banks | `sim/atari/tb_atari_boot` with our own 6502 test OS (below) loaded through the window model; board with the real OS by REST | CI `sims`, board |
| S5 | P4 | Elastic memory is invisible | `sim/atari/tb_atari_mem` (DDR2 latency model, random busy; hash of every bus cycle vs instant memory) | CI `sims` |
| S6 | P5 | Picture, palette, swap back | `sim/atari/tb_atari_video` (pixels into the frame-buffer model); board photos | CI `sims`, board |
| S7 | P6 | Sound | `sim/atari/tb_atari_audio` (frequency from samples) | CI `sims` |
| S8 | P7 | Keys, console, joystick, RESTORE | `sim/atari/tb_atari_keys` (every table entry through POKEY's scan); board by hand | CI `sims`, board |
| S9 | P8 | ATR boot and write-back | `sim/atari/tb_atari_sio` (SIO frames against a software drive model, timing); board with a third-party ATR by hand | CI `sims`, board |
| S10 | P9, P8 | Cartridge images, `.xex`, idle slot | `sim/atari/tb_atari_cart` (mapper types on our own test images); `sim/atari/tb_pins_idle`; board with third-party images | CI `sims`, board |
| S11 | P10 | Windows and identity | `sim/atari/tb_windows` + app run on the board | CI `sims`, board |
| S12 | P10 | Freeze | `sim/atari/run_pause.sh` (as `sim/vc20/run_pause.sh`, `sim/amiga/run_pause.sh`: random pauses, hash of all bus cycles against a free run) | CI `sims` |
| S13 | P10 | Swap and back, app guard | board by hand (Alex's photos); app guard by REST script | board |
| S14 | P11 | Fit and timing | Gitea CI `pipeline.yml` build stages, reports | CI build |

Third-party items are never checked in: the Atari OS/BASIC ROMs, ATR/ATX/CAS images, cartridge images, `.xex` programs and
the ACID800 test (Avery Lee) are supplied locally and run by hand. CI uses a **test OS written by us** (a few hundred
bytes of 6502: reset vector, display list, colour registers, a POKEY tone, a keyboard poll, an SIO command) and our own
cartridge images, generated like `sim/vc20/gen_testrom.py`.

## §3 Decision

Facts are in §1; each D names its part.

- **D1 Source (P1, P2).** **Primary: `M`'s `rtl/common/` (`a8core`, `components`, `sioemu`)**, the newest tree and the one with
  the explicit `COPYRIGHT_NOTICE`; top is `atari800core_simple_sdram.vhd` (its own header: "provides VIDEO, AUDIO; passes
  upstream the DMA port for attaching a ZPU for SD card/drive emulation" — which is exactly the host/app shape here). `N`
  is read-only reference for what a small FPGA needed (its genlocked scandoubler notes, `sio_disk_pipeline.md`, the
  hardware command-frame capture in `sio_handler`, the ANTIC HSCROL fix, its tests); its own files are not taken (NC-SA
  and GPL parts, Gowin-specific, NTSC-only). `U` is the upstream of `M`'s core and is read when `M` and `N` differ.
  *Kept:* ANTIC, GTIA (+ palette), POKEY, PIA, `shared_enable`, `address_decoder`, `atari800core_simple_sdram`,
  `sio_handler` + FIFOs, `cart_logic`, `covox`, `pbi_rom` if Q6. *Replaced by ours:* `cpu.vhd` and `cpu_65xx.vhd` by `cores/atari800/cpu.vhd` on `proc_core` (§5). *Replaced by host services:* `Atari800.sv`/`sys/` (HPS, OSD,
  video mixer, SD, ZPU firmware), PLLs (`rtl/pll*`), HDMI, SDRAM controller, USB/PS2 keyboard (`ps2_to_atari800`,
  `ps2_keyboard`: our matrix table instead, D6), SPI. *Altera-specific, replaced:* `bram.vhd`, `large_bram.vhdl` (VBXE
  VRAM, out), `pll*.v`, `hps_ext.v`. *Out:* `vbxe*` (Q7), `pokeymax/` (Q8), `ultime.vhdl` (DS1305 clock, not needed),
  `tape_handler` unless Q6, the 5200 files. Every kept file keeps its copyright header.
- **D2 Licence handling (P1).** The core is TREX-internal (T83 D13): sources in the private Gitea repository, the partial
  bitstream and the `.cor` never in a release, an update package or a public download; own switch `TRX_CORE=atari800`, off
  by default; `ci/package.sh` strips everything named `atari800`. This is Watson's "non-commercial use" read as the
  strictest case; any commercial or released use needs his permission first (scrameta@gmail.com). `cpu_65xx.vhd` is not
  used (Q2); the 6502 is `proc_core` (GPL-3, §5). The LGPL files (`cart_logic`, `pbi_rom`, `freezer_logic`) stay LGPL and keep their headers.
- **D3 Clocks and reset (P3, C1).** Machine cycle PAL = 17.734475 MHz / 10 = 1.7734475 MHz; the core wants `CLK = 16 x` that =
  **28.37516 MHz**. `sys_clk` is 31.5278 MHz (17.734475 x 16/9, the repo's PAL value), so **28.37516 MHz = `sys_clk` x 9/10
  exactly**, the same ratio as the Amiga's chipset clock (T85 D3). `cycle_length` stays 16 (`shared_enable` indexes
  powers of two; 16 x 1.7734475 is the only fit). The core runs on `sys_clk` with a global enable `ce28` (9 of 10
  clocks, minus the stall of D4 and the hold of D10). A clock buffer inside `pb_l2` is not allowed (T85 gate check: the
  build strips every BUFG/MMCM from the partition; clocking is static in the host), so the gated-clock route of the Nano
  is out. **Every clocked process of the Atari core gets `ce28`:** grep finds one `rising_edge(CLK)` per entity in about
  35 entities (8 in `tape_handler`, 4 each in `cart_logic`/`freezer_logic`; the 6502 takes `ce28` through its own clock enable, §5) — an
  edit per entity, not per register, because the core is written in the one-clocked-process style; the count is a grep
  figure, S2 confirms it. Reset: `core_rst` stretched by the core; the 6502 is held until memory and ROMs are ready.
  The second core clock (`sid2x`) is unused.
- **D4 Memory (P4, C3). The plan that keeps the base machine out of DDR2.**
  *Block RAM in `pb_l2`:* the 64 KB RAM (`internal_ram` at the start of the memory map, the core's own generic) =
  16 tiles, the XL OS ROM 16 KB = 4 tiles, BASIC 8 KB = 2 tiles (tile = 4 KB at x8, as in T83/T85) = **22 tiles for the
  800 XL / 65 XE**, plus about 3 for the core's small RAMs and 2 for the SIO FIFOs, the palette and the picture line
  buffers (**estimate**), about 27 of 50. ROM is RAM written by the app: no ROM bytes in the bitstream (T83 D4), the
  partial is shippable; the ROM region needs a block RAM port added to `internalromram` (its `internal_rom=1` path says
  "TODO" for anything but the built-in case; **to be confirmed in the first elaboration**).
  *DDR2 (the general or real-time port, T85 gate check):* everything above 64 KB — the 130 XE banks (64 KB extra), RAMBO
  up to 1,088 KB, cartridge images up to 4 MB — because 64 KB more in BRAM would be 38 tiles of 50 with the rest, and
  1,088 KB would be impossible. The core's own memory interface is already a request/complete handshake with a 25-bit
  address (`M` map: SRAM 512 KB, ROM 4 MB, SDRAM 32 MB regions, `atari800core.vhd` comment at `:173`), so the DDR2 side
  is an adapter, not a rewrite.
  *Latency, in numbers:* one machine cycle is 16 ticks = 563.9 ns (**computed**). A DDR2 32-bit client read measured
  "about 0.4 us" (`ddr2_port.v` header, as quoted in T85 D4), the controller alone about 110 ns plus crossings, a refresh
  127.5 ns every 7.47 us; the worst-case sim reads on the *general* port are 670-730 ns (`mig_arb` header, `sim/lat`, **sim**).
  So a DDR2 read fits a machine cycle on paper (71 % of it at 0.4 us) and does not fit at the sim worst case. The Atari
  differs from the Amiga in that its cycle is four times longer; it does not differ in the rule: **a read that is not back
  at the point ANTIC or the 6502 latches holds the whole core** (`ce28` low, all state frozen between two ticks, T85
  D4 option 1), so correctness never depends on the latency. ANTIC DMA data is latched at a fixed phase (`N`'s `performance_and_timing.md` reports
  garbled frames when the SDRAM answered after it; the core has `MEMORY_READY_ANTIC` and `_CPU` flags, but the Tang port's
  fix was a faster memory, not a longer wait), so the hold must cover ANTIC too, not only the 6502's wait. With a base 64 KB machine no DDR2 access happens and the machine runs exactly.
  *DDR2 window:* the same 16 MB window as the Amiga (T85 D4; a swap is a cold boot, the two never coexist): extra RAM
  +0x000000 (1 MB), cartridge +0x400000 (4 MB), frame buffers +0xD00000, ATR/XEX staging not needed (D8). The window size
  is the host's to give; not yet built (T79.1 §7).
  *ROM files:* the app writes the ROMs through the ROM aperture exactly as the VC-20 app does (`/flash/roms/atari800`,
  `/usb0-3/roms/atari800`, `/sd/roms/atari800`, T83 §5). Names and sizes in D9.
- **D5 Video (P5, C4). Pixel layer, not the VIC-II frame path.**
  *The 16-colour path (what T83 did) does not fit:* GTIA shows 128 distinct colours (16 hues x 8 luminances) at a time,
  the palette table has 256 entries, 238 distinct as RGB888 in the PAL table (**measured** by parsing `gtia_palette.vhdl`), and
  `pix_color[3:0]` carries 16. Not acceptable for a core meant to run Atari software.
  *The pixel layer path (as the Amiga, T85 D5):* the core writes its picture into DDR2 as RGB565 for the host's pixel layer
  (`pix_layer.v`: source W <= 960, scale 1-3x, drawn over the C64 picture and under the overlay, double or triple buffer
  with a flip written by the app), no host change. The palette (the 256-entry `gtia_palette` case table, ~100-200 LUT or one
  tile, **estimate**) turns the 8-bit GTIA colour into RGB888 and then RGB565; the PAL table's 238 distinct
  RGB888 values stay **233 distinct in RGB565** (the NTSC table: 256 -> 244), **measured** by the same parse, so the
  565 loss is five to twelve merged near-identical greys and blues, no class of colours disappears.
  *Format (derived, S6 measures the real extent):* GTIA delivers 2 pixels per colour clock (hires pixel 7.094 MHz PAL =
  4 ticks, 114 machine cycles = 456 hires pixels per line, 312 lines); the visible window is 384 hires pixels (the wide
  playfield, 192 colour clocks) x 288 lines — exactly 4:3 — the border colour fills what ANTIC does not draw (**measured 2026-10-14, 5.8: ANTIC's hblank leaves 356 of the 384 columns, the last 28 repeat the last pixel, and the 24 lines above and below ANTIC's 240 are black, not the border colour**). Scale 3 =
  1152 x 864 at X0 = 384, Y0 = 108 on the 1920 x 1080 frame. Scale 3 is the largest the layer offers, so the picture does
  not fill the 1080 lines (80 %); filling needs a host change (the contract's V1a native video), a later step.
  *The window stays 384 wide (Alex, 2026-10-14: "leave it, the app shifts X0", translated).* The 28 columns hblank hides
  repeat the last visible pixel, which on a normal screen is the border colour, so they vanish into the border; it costs no
  gateware. The playfield's centre is hires pixel 178, the window's is 192, so the picture sits 14 px left of the window's
  middle and the application corrects that with the layer's X0 if it wants to. Narrowing to 352 columns (not 4:3 any more)
  and centring in 384 (a line buffer or a second read pointer in `a8_pixw`, with layer 1 already at 77 % of its LUT) are
  both rejected.
  *Traffic:* 384 x 288 x 2 B x 49.86 Hz = 11.0 MB/s written and the same read by the layer, three buffers of 221,184 B
  (rounded to 256 KB). A write sequencer packs 8 pixels into a 128-bit word with a two-line buffer (about 2 tiles and 100-200 LUT,
  **estimate**, the Amiga's figures).
  *Frame rate (a decision, Q9):* the host's output frame is fixed (1080p50 from `sys_clk`, 628,992 `sys_clk` = 50.1246 Hz) and the
  scaler locks once to a VIC-II-shaped raster the core must keep emitting (a dummy 504 x 312 dot raster, colour 0, a few LUT,
  as the Amiga and VC-20 do). The exact Atari PAL frame is 35,568 machine cycles = **632,320 `sys_clk` = 49.8608 Hz**
  (**computed**), 0.264 Hz slower than the host: with the exact machine a frame is repeated every **3.79 s** (judder in
  scrolling, audio and speed exact). Locking the machine to the host frame instead (35,568 cycles in 628,992 `sys_clk`) makes the
  machine run 0.53 % fast (pitch +9 cents, SIO and POKEY timing 0.53 % fast) and the repeat disappears. The exact variant needs
  no further design; the locked variant is a different `ce28` ratio (an NCO as the VC-20's `vc20_nco`).
  *Decided (Alex, 2026-10-14): the exact machine stays* - pitch, SIO and POKEY timing are right and a frame is repeated every
  3.79 s. The bench measures the exact figure (`a PAL frame is 632,320 sys_clk`). A register that switches between the two was
  offered and rejected; no NCO ratio of the VC-20's kind is built for the Atari.
  NTSC (59.92 Hz) does not fit a 50 Hz output without dropping 1 frame in 6: PAL first (global decision, T83 Q5).
- **D6 Audio (P6, C5).** POKEY's 16-bit `AUDIO_L/R` (mono by default, `STEREO` off) is averaged over a fixed number of
  `sys_clk` clocks (656 = 48.06 kHz as the VC-20, T83 D6), widened to signed 24 bit, handed over with `audio_sample_en`; the host
  resamples and mixes. The covox output is mixed in by the core already. No SID: `sid_*` ignored. Stereo POKEY / PokeyMAX: Q8.
- **D7 Input (P7, C6).** The host delivers `keys[63:0]` (the C64U matrix), `joy1/joy2[4:0]`, `restore`.
  *Keyboard:* the core's `KEYBOARD_SCAN[5:0]`/`KEYBOARD_RESPONSE[1:0]` is POKEY's scan of 64 key codes; a **64-entry table** maps
  the host matrix position to the Atari key code the scanner expects, written from the Atari keyboard layout and the
  C64U matrix (not copied from `ps2_to_atari800`, which maps PS/2 codes and carries Watson's notice anyway). The C64 matrix has
  no ESC, TAB, CAPS, CLEAR, INSERT, DELETE, BREAK, HELP: proposal: `<-` = ESC, RUN/STOP = BREAK, `C=` = Atari inverse-video
  key, CLR/HOME = CLEAR, INST/DEL = DELETE (shift = INSERT), CTRL/SHIFT/RETURN as they are, `F1` = START, `F3` = SELECT, `F5` =
  OPTION, `F7` = HELP, **RESTORE = RESET** (the Atari warm-start key). TAB and CAPS have no key (Q10).
  *Joysticks:* `joy1` -> Atari port 1 (PIA PORTA bits 3:0, `GTIA_TRIG(0)`), `joy2` -> port 2 (bits 7:4, `GTIA_TRIG(1)`); the 800 XL
  has two ports (the classic 800/400 four, Q4). Paddles (`POT_IN`) idle (Q11). The Atari fire is the C64's single fire.
- **D8 Storage and loading (P8).** *No MCU, no IO CPU in the core.* Per the contract the app is the smart peripheral
  (as `N`'s PicoRV32 is): the core exposes `sio_handler` as a register window on C2 (transmit, tx status, receive, rx
  status, divisor, framing error, the captured 5-byte command frame), the app (layer 3, Gideon's file/USB/SD stack) mounts
  the ATR/XFD from the stick or card (D1:-D4:, SD/ED/DD), answers the command frame (ACK, COMPLETE, sector data, checksum),
  and writes sectors back to the file itself (the image never needs to be in DDR2; a pulled stick reports an error in the
  overlay and the image stays consistent: write-through with a dirty flag, flush on idle/eject). Real-time budget: SIO is a
  serial bus at 19,200 baud (about 520 us per byte) with the OS's own ACK/complete timeouts (the numbers are not read from
  any source here; **to be measured** with the S9 bench against the real OS and then on the board); `N` does this on a 27 MHz PicoRV32, our
  RISC-V is faster but shared with the overlay and USB, so the SIO service is a high-priority task and the hardware
  command-frame capture (so the CPU polls a ready flag, not the FIFO) is taken from `N`'s idea, written by us.
  *`.xex`:* the app loads the executable through the core's DMA/XEX loader mode (`XEX_LOADER_MODE`, `DMA_*`, in `M`), the
  standard way these cores do it; the `N` loader sits in page 6 and clashes with programs that load there — ours is the
  core's DMA loader, which does not (to be verified in S10). *Tape (.cas):* Q6. *Hard disk / PBI:* Q6.
- **D9 ROM files (P4).** The user puts these in `roms/atari800/` on the USB stick, SD card or flash (the same three roots as the
  VC-20, T83 §5); names matched case-insensitively:

  | File | Size | What | Needed |
  |---|---|---|---|
  | `ATARIXL.ROM` | 16,384 B | XL/XE OS (Rev. 2 / Rev. 3 / any compatible XL OS) | yes |
  | `BASIC.ROM` | 8,192 B | Atari BASIC (Rev. C) | yes (BASIC at power-on; skipped with OPTION held) |
  | `ATARI800.ROM` | 10,240 B | classic 400/800 OS-A or OS-B | only if Q4 says classic mode |

  The sizes are those both `M` (`boot0/1/2.rom`, 16 KB/8 KB/10 KB, its README) and `N` (`ATARIXL.ROM`, `BASIC.ROM`) read; the
  names are `N`'s, kept for users who already have them. The
  app checks size and refuses a wrong file with a line in the overlay. Nothing of them is in the repo, CI, the partial or
  the `.cor`.
- **D10 Pins, registers, identity, app, swap and Freeze (P9, P10, C2/C7/C8/C9).** Same rules as the VC-20/Amiga: all
  `SLOT_*`, IEC and user-port pins idle (the 800 XL has a cartridge slot, but not the C64's: the cartridge is an *image* in
  DDR2, D4/D8; plugging a real cartridge into the C64 port does nothing); identity "A8XL", contract version, clock table
  (D3), geometry (D5), window sizes; a config window (RAM size, machine type, PAL, OS/BASIC enable, joystick swap, cartridge type
  and size, reset, SIO window, key injection, frame IRQ); everything else "absent". The application is the VC-20's kind
  (T83 D11a): an Ultimate-app image on Gideon's base, one app per core with `#ifdef` (`build_atari800.sh`, `ATARI800=1`),
  packed with the partial in `atari800.cor`; at start it checks the ID block, loads the ROMs, sets the pixel layer, resets
  the core. It shows only what exists for this machine: reset (warm/cold), mount ATR D1-D4, cartridge, `.xex`, RAM size,
  joystick swap, ROM/BASIC. No monitor, SID, REU, turbo, cartridge writer, Timewarp (plain machine, T83 Q6).
  *Freeze (T83 Q6 as changed 2026-10-13, T85 Freeze):* the contract's machine pause (`app_stop & app_pause`, `machine_ctl`,
  `mc_halt`) holds `ce28` low and freezes its phase accumulator, so **every** flip-flop of the core holds between two whole
  ticks, not only the 6502: the core's own `HALT` is **not** used because it reaches only `PAUSE_6502` (§1; with only the
  6502 paused, ANTIC would keep fetching and POKEY keep counting, and the resume would not be exact — the lesson of
  T83's "stopping only the CPU was rejected"). The units that talk to DDR2 (the memory adapter, the picture writer) finish
  what they began; `paused` (to the host as `c64_halted`) rises when `ce28` is held, the memory adapter and the picture
  writer are quiet for 32 clocks. Sound is ramped to zero. A reset takes the hold away. The SIO bridge's UART state is part
  of the held core; the app's SIO service is paused with the app's own menu.
- **D11 Size and timing (P11).** **Estimate, no measurement exists; I could not run a synthesis here** (no GHDL front end for
  Yosys on this Mac; the repository's CI does it, S2/S14). *Published:* `N`'s `docs/PHASE_AB_TESTING.md`: the whole Tang
  design (core + PicoRV32 + HDMI/scandoubler + SDRAM controller + OSD + USB HID) at **11,269 LUT4 and 42 of 46 BSRAM** (18 Kbit)
  at that build, the core's own share is not stated; the same repo's `firmware_spec.md` counts the Atari core + SIO FIFO + OSD
  text + USB ROM + scaler at **9 BSRAM blocks (~18 KB)** when RAM and ROM live in SDRAM. *Repository figures:* the C64's 6510 is
  803 LUT; TRX64 layer 2 is 14,036 LUT, the slim-C64 reference 13,643 LUT / 4,300 slices (T87 D15/D16); `pb_l2` has 18,000 LUT /
  4,500 slices / 50 tiles / 80 DSP. *Estimate (assumption: 0.6-1.0 Xilinx LUT per Gowin LUT4, as T85 D10):* the Atari core
  (6502 table decoder, ANTIC, GTIA with palette, POKEY, PIA, SIO, cart logic) **3,500-6,500 LUT**; our glue (ce28 and memory
  adapter with the elastic hold, picture writer with line buffer, register window and identity, key table, audio averager,
  frame-lock raster) **1,500-2,500 LUT** (the VC-20's glue was about 2,090 LUT with a drive, T83 D12). **Total about 5,000-9,000 LUT,
  28-50 % of `pb_l2`, 37-66 % of the C64 reference** — comfortably below the density the router already accepted (78 % for TRX64's
  layer 2; 88.6 % did not route). BRAM about 27 of 50 tiles (D4); DSP 0 (`mult_infer` is instantiated nowhere; PokeyMAX, out, may use
  some). Timing: the core runs at one `sys_clk` per tick (31.7 ns); `N` measured the 6502's `opcInfo` path at 18.7 ns on a Gowin
  at ~53 MHz Fmax (`performance_and_timing.md`), and our own 6510 already closes at `sys_clk`, so the logic is not the risk; the
  edge (clock-enable fan-out of `ce28` into ~35 entities, the DDR2 crossing) is. S14 decides.

**Open questions for Alex (one line each, not decided here):**

1. Q1: Source: "the Nano cores are already there" — MiSTle-Dev has no Atari 800 core; use Watson's MiSTer tree `M` as the base (D1) and read the third-party Tang port `N` only as reference, or base the work on `N`?
   **Answered (Alex, 2026-10-13):** "a)": Watson's MiSTer tree `M` is the base (D1); the Tang port `N` is read as reference only.
2. Q2: `cpu_65xx.vhd` (FPGA64, Peter Wendrich) says "All Rights Reserved" with no permission: use it inside TREX-internal only, or replace it with a 6502 we may use (our 6510, 803 LUT, if it can halt for ANTIC DMA cycles, or a clean one)?
   **Answered (Alex, 2026-10-13):** "b": `cpu_65xx.vhd` is not used; it is replaced by a 6502 we may use (our 6510 if it can halt for ANTIC DMA cycles, else a clean one).
3. Q3: RAM scope of the first build: 64 KB only (800 XL/65 XE, all in block RAM), also 130 XE 128 KB, or up to RAMBO 1,088 KB (the extra banks in DDR2 with the elastic hold)?
   **Answered (Alex, 2026-10-13):** "c": up to RAMBO 1,088 KB in the first build; the base 64 KB in block RAM, the extra banks in DDR2 with the elastic hold.
4. Q4: Classic 400/800 mode (OS-A/B, 10 KB ROM, four joystick ports) in the first build, or XL/XE only?
   **Answered (Alex, 2026-10-13):** "b": XL/XE only in the first build; classic 400/800 mode (OS-A/B, four joystick ports) later.
5. Q5: Cartridges: raw 8/16 KB images and the banked `.car` types up to 4 MB (image in DDR2) in the first build? (Flash-writable carts are Q12.)
   **Answered (Alex, 2026-10-13):** "a": raw 8/16 KB images and the banked `.car` types up to 4 MB (image in DDR2) in the first build.
6. Q6: Tape (`.cas`, `tape_handler`), PBI BIOS/HDD (APT, SpartaDOS X) — in the first build or later?
   **Answered (Alex, 2026-10-13):** "b": tape (`.cas`) in the first build; PBI BIOS/HDD (APT, SpartaDOS X) later.
7. Q7: VBXE (512 KB video RAM, 24-bit overlay video): in the first build, or out (the Tang Nano could not hold it; here it would be DDR2 and a second picture path)?
   **Answered (Alex, 2026-10-13):** "b": VBXE is out.
8. Q8: Stereo POKEY / PokeyMAX (extra POKEYs, SID/PSG/sample voices): in, or one mono POKEY?
   **Answered (Alex, 2026-10-13):** "b": one mono POKEY, as the original; no stereo POKEY / PokeyMAX.
9. Q9: Frame rate: exact PAL machine (49.86 Hz, a repeated frame every 3.79 s), or locked to the host's 50.12 Hz frame (machine 0.53 % fast)?
   **Answered (Alex, 2026-10-13):** "a": exact PAL machine (49.86 Hz); one repeated frame every 3.79 s on the host's 50.12 Hz is accepted.
10. Q10: Keys: the proposed mapping (D7: `<-` = ESC, RUN/STOP = BREAK, `C=` = inverse, F1/F3/F5/F7 = START/SELECT/OPTION/HELP, RESTORE = RESET) with TAB and CAPS unreachable from the C64 matrix — enough for now, or a USB keyboard through the app from the start?
   **Answered (Alex, 2026-10-13):** "a": the D7 mapping on the C64 matrix is enough for now (TAB and CAPS unreachable); no USB keyboard path yet.
11. Q11: Paddles (POT inputs from the C64U's paddle/mouse path, T47): in, or idle?
   **Answered (Alex, 2026-10-13):** "a": paddles in, the POT inputs fed from the C64U's paddle/mouse path (T47).
12. Q12: Flash-writable cartridges (AtariMax, SIC!, MegaCart 4 MB writeback, as `M` does since March 2026): in the first build (then a write-back path to the file), or read-only images?
   **Answered (Alex, 2026-10-13):** "b": read-only cartridge images in the first build; flash write-back later.

## §4 Out

- **VBXE, PokeyMAX, tape, PBI/HDD, paddles, flash-cart write-back, classic 400/800 mode, RAM above 64 KB:** each is a question above
  (Q3-Q8, Q11, Q12) and is *in or out by Alex's answer*, not by omission here.
- **Real cartridges on the C64 expansion port** as Atari cartridges: the pins and the bus are different; cartridges are images.
- **NTSC:** PAL first (T83 Q5); NTSC needs 59.92 Hz output or dropped frames (D5).
- **Analogue AV out** and a true 1080-line fill of the picture (scale 3 is the layer's maximum): host work (contract V1a), a later step.
- **The 5200 console** (`atari5200core.vhd`, `ps2_to_atari5200`, `rtl/rom/5200.mif`): not this core.
- **SIO over the real serial hardware** (FujiNet/ESP32 bridge, SIO2PC): the bus is the app's software drive; no pins.
- **Timewarp ring, trxmon, `.c64re` snapshots, UCI, SID, REU, turbo, sampler, cartridge writer, monitor** for the Atari: C64 features
  (plain machine). Freeze is in (D10).
- **The Tang port's PC link, OSD, USB HID and HDMI timing** (`N`'s `src/`): board glue the host replaces.
- **Switching cores without a full app restart** and any host change beyond what D5 says is unnecessary (none).


## §5 Decisions taken while building

**5.1 The 6502 (Q2), 2026-10-13.** Watson's `cpu.vhd` instantiates `cpu_65xx` (Wendrich, all rights reserved; not used).
What the core asks of it, read from `atari800core.vhd`, `cpu.vhd`, `shared_enable.vhdl`: the CPU advances one bus cycle on the
clock where `ENABLE and THROTTLE and MEMORY_READY` are high, `DI` is taken on that clock for the address presented before it,
the next cycle's `A/R_W_n/DO` are there after it; `CPU_FETCH = ENABLE and THROTTLE` starts the memory request even if the
memory is not ready yet; `RDY` (ANTIC) low stops the CPU on a read cycle only (`halt = not rdy and not we`); IRQ is a level,
NMI an edge; `HALT` of the core never reaches the CPU (it is `PAUSE_6502` in `shared_enable`).

| Candidate | RDY rule | Size (yosys `synth_xilinx`, flattened, `sim/cpu/synth_lut.sh`) | Licence | Verdict |
|---|---|---|---|---|
| `cpu_65xx` (Watson's tree) | read-only stop | n/a | all rights reserved | excluded (Alex) |
| T65 (`vendor/icepi/T65`, ver 315) | `really_rdy = Rdy or not WRn` (read-only stop) | 844 LUT, 180 FF | BSD-3 | works, no advantage: same size, more FFs, needs the DO-to-DI loop-back, its header lists interface signals that "behave incorrect" |
| `proc_core` (6502N, Gideon; `vendor/icepi/6502n`) | `ready = '1' or rwn_i = '0'` (read-only stop) | 848 LUT, 130 FF alone; 880 LUT, 153 FF with the adapter | GPL-3 (upstream 1541ultimate), already in the repo | **chosen** |

Why `proc_core`: its `ready` is the 6502 rule itself and its `clock_en` is the cycle enable; it is the CPU of the C64 core
(full Lorenz 297/297 on the board, T29 history), so it is the part with a measured cycle-exact record; no new third party.
The GPL-3 file sits beside Watson's non-commercial notice; that is no problem for the TREX-internal rule (D2), a release
would need both questions answered.

**5.2 Adapter `cores/atari800/cpu.vhd`** (entity `cpu`, ports of Watson's plus `CE` and `SYNC`; replaces `cpu.vhd` and
`cpu_65xx.vhd`, the other kept files stay unchanged): `clock_en = CE and ENABLE and MEMORY_READY and THROTTLE`; `ready = RDY`;
`CPU_FETCH = ENABLE and THROTTLE`; `A = addr(15:0)`. `CE` is the core's global enable (the 9 of 10 of D3); the enables of
`shared_enable` are produced by `ce28`-gated registers, so a pulse always spans a `CE` edge and is not lost. `proc_core`
samples NMI every `sys_clk`; the line only changes right after a `CE` edge, so that is the same. One addition: ANTIC holds
`NMI_n` low during reset and for a few cycles after (`antic.vhdl`, `nmi_reg` is '0' in reset); Wendrich's detector needs a
falling edge and never saw it, `proc_core` would take it as NMI right after reset (found by the A/B run, 5.4). `NMI_n` is
therefore masked until it has been seen high once since reset, and taken at the enable edge (`proc_interrupt_a8`, 5.4). Two behaviours that are the real NMOS 6502 and stay: a BRK taken
over by an NMI leaves B set on the stack; `SHA/SHS/SHX/SHY` write to another address when RDY drops in their cycles.

**5.3 Evidence** (`cores/atari800/sim/cpu/run.sh`, GHDL 5.0.1 in the `vc20-ghdl` container; the bench `tb_cpu.vhd` clocks the
adapter like the core: `CE` 9/10, enable pulse every 16th `CE` tick, memory wait states with the extra enable of
`shared_enable`, junk on `DI` when the memory is not ready or RDY stops a read):

| Test | Result |
|---|---|
| Opcode cycle counts (`cycles.py`: 244 opcodes, 325 cases with page crossing, taken/not-taken/across-page branches, JMP ($xxFF) page bug), table built from the instruction matrix | 325 of 325 match (cycles and where the next fetch lands), identical in 5 clocking/RDY/wait-state variants (9,641 cycles each) |
| RDY halt (`halt.py`): executed bus cycles with RDY patterns (random, mostly low, ANTIC-like 9 low per 114/115 ticks) equal those without; a stopped cycle is a read and repeats; no write is stopped | 9,339 executed cycles, 6 variants, up to 63,630 stopped reads; negative control (`ready` tied high) fails at once |
| Klaus Dormann functional test, decimal mode on | PASS, 96,241,371 cycles (the test's 96,241,367 plus the reset sequence) |
| Bruce Clark decimal test, ADC and SBC for all inputs, A/C/N/V/Z all checked | PASS, 53,953,832 cycles |
| Dormann interrupt test (feedback latch at $BFFC) | PASS, 2,907 cycles, with 2 checks patched out: the test says itself a real NMOS 6502 fails them (BRK taken over by an NMI keeps B set, and the BRK is lost) |

**5.4 System check with the real ANTIC** (`sim/atari`, needs Watson's tree outside the repo): his `antic` and `shared_enable`
unchanged, a mini machine of ours around them (RAM, our own test OS `testos.asm` — no Atari ROM —, POKEY stand-in), once with
`cpu_65xx` and once with the adapter, both bus logs compared line by line (`cmp_logs.py`).
Result: the two bus logs are identical line for line (clock, RDY, memory ready, address, read/write, data) over 300,000 6502
cycles (about 8.4 PAL frames, ~5.2 M clocks) in all of: plain 1x; memory wait states for CPU and ANTIC (random, 3 seeds); 2x and 4x
turbo (`THROTTLE_COUNT_6502`). The program runs ANTIC text mode with display-list DMA, player/missile DMA, a DLI with WSYNC, the VBI
NMI (17 NMIs and 8 WSYNC stops per run), a timer IRQ (80 per run) and register polling. The reset sequence is the one place that
differs by construction (Wendrich's core reads the vector one cycle earlier than `proc_core`, which does a dummy read first), so the
bench holds Watson's CPU in reset for one machine cycle and compares from there. It found two things the CPU-only benches cannot:
(1) the reset NMI above; (2) `proc_core` took an NMI one machine cycle later than Watson's CPU because its `nmi_c` stage samples
the line once per clock before the enable edge, and ANTIC's NMI changes exactly one clock before it — a fixed one-cycle delay of
every DLI/VBI. The core therefore has its own copy of those two files, `cores/atari800/proc_interrupt_a8.vhd` and
`proc_core_a8.vhd`, which take the line at the enable edge; the adapter instantiates `proc_core_a8`. The C64's own
`vendor/icepi/6502n` stays byte-identical - a core never edits a shared file to get core-specific behaviour (T94 D12). The mini machine has no `address_decoder` (memory is
ready at once or after random waits), so ANTIC's real bus arbitration is not covered here (S3/S4).

**5.5 Not done / open.** Watson's full core does not analyse under GHDL (about 20 places: `case` without `others`, sliced type
conversions, non-static port actuals in `address_decoder`, `atari800core`, `pokey`, `gtia`, `cart_logic`, VBXE, PokeyMAX;
Quartus accepts them); that is S2's work and was not started, so the OS self test / memo pad boot has not run and no Atari OS ROM
is on this Mac. The `ce28` edit of every other entity (D3) is not done; the adapter is ready for it. LUT figures are yosys, not
Vivado. (5.5 is overtaken by 5.7: the gate is closed and the `ce28` edit is done.)

**5.6 The GHDL gate, measured (2026-10-13, after vendoring).**

`tools/vendor_atari800.sh` put 41 of Watson's files into `cores/atari800/rtl` (commit 1440e41, unchanged).
Analysed with `ghdl -a --std=08 -frelaxed -fsynopsys` in `vc20-ghdl`, repeated until no file makes progress any more:
**34 of 41 analyse, 7 do not**, and each failure has a known cause and a decided direction:

| File | What GHDL says | What it means |
|---|---|---|
| `a8core/cart_logic.vhd` | `missing choice(s)` at line 587 | VHDL-2008 is stricter than Quartus about a `case` without `when others`. One patch in our tree, the vendored file stays as it is |
| `a8core/address_decoder.vhdl` | `unit "cartlogic" not found`, and `type conversion cannot be indexed or sliced` (line 1688) | the first follows from `cart_logic` failing; the second is a real 2008 strictness case and needs the same treatment |
| `sioemu/fifo_receive.vhd`, `sioemu/fifo_transmit.vhd` | `cannot find resource library "altera_mf"` | Altera megafunction FIFOs. They are replaced by our own, with the same entity name and ports |
| `sioemu/sio_handler.vhdl` | the two FIFOs are missing | follows from the line above |
| `a8core/atari800core.vhd` | `unit "pokeymax" not found` | pokeymax is out by Q8, and the instance is unconditional. A stub with the same ports (outputs idle) keeps Watson's wiring untouched |
| `a8core/atari800core_simple_sdram.vhd` | `unit "internalromram" not found` | out by D4: RAM and ROM come from the app over the host, so our own entity of that name takes its place |

So the ~20 places the spec expected are 7 files and four causes, and none of them is in ANTIC, GTIA, POKEY or PIA -
the machine itself analyses. The replacements belong in `cores/atari800/trx/` beside the vendored tree, as the other
cores have it.

**5.7 S2 and D3 done: the machine analyses and elaborates with the clock enable on every flip-flop (2026-10-13).**
`cores/atari800/rtl/` stays Watson's bytes. `tools/atari800_gen.sh` makes `cores/atari800/gen/` from it: two patches
(`tools/patches/atari800/`: the sliced type conversion in `address_decoder` as a signal, the `case` of `cart_logic` with
`when others`) and then `tools/atari800_ce28.py`. `gen/` is committed (the CI has no checkout of Watson's tree);
`tools/atari800_gen.sh --check` fails if it is stale. **Measured, not estimated (D3):** 33 clocked entities in Watson's files, 39
clock-edge processes, 123 instantiations get `CE28 => CE28`; the three spellings of an
edge are `rising_edge(x)`, `x'event and x = '1'` and the same with a second clock name (`clock`). The script adds the port
`CE28 : in std_logic := '1'` to each of them and the term `and CE28 = '1'` to each edge, nothing else, so the result is the
same netlist with a clock enable on every flip-flop (Vivado maps it to the CE pin). The 6502 adapter has had its enable (`CE`)
from the start. What the 7 files of 5.6 needed turned out to be more than four causes:
- `atari800core.vhd` instantiates four units that are out by decision, not one: `pokeymax`, `DS1305Redux` (ultime), `large_bram`
  and `VBXE`. `trx/a8_stubs.vhd` has the last three (VBXE passes the GTIA signals through as the switched-off real one does),
  and `trx/pokeymax.vhd` is **not a stub**: Watson's `pokeymax` is the wrapper around the POKEY itself, so ours is the Q8 chip -
  one mono POKEY at `$D200-$D2FF`, the covox mirror, Watson's `pokey_mixer_simple` and the console speaker.
- `trx/a8_fifos.vhd`: `fifo_receive`/`fifo_transmit` (256 words, show-ahead, usedw modulo 256) on LUT RAM.
- `atari800core_simple_sdram.vhd` is **not used**, and so is no `internalromram` of ours: the app has to reach the base RAM and the
  ROMs through the window (also while the core is in reset or frozen), which needs the second port of the RAM inside the core's
  own memory glue, and Watson's wrapper hides that RAM. `a8_machine` (ours) is the top; it keeps the wrapper's job (joystick to PIA
  and GTIA, paddles through `pot_from_signed`, SIO wiring) and replaces its memory. D1 named the wrapper as the top; this is the
  change, taken because the window is the contract's way to RAM and ROM.
`cores/atari800/sim/gate/run.sh` imports gen/, trx/ and the CPU and elaborates `atari800core`: PASS.

**5.8 The whole core runs under GHDL; layer 2, descriptor and recipe exist (2026-10-14).** What is in `cores/atari800/`:
`trx/a8_core.vhd` (what `layer2_atari800.v` wraps: clock enable `ce` = sys_clk x 9/10 minus the stall of a DDR2 miss, `a8_nco`;
the machine `a8_machine`; memory `a8_mem` (64 KB block RAM plus the DDR2 cache for RAM above 64 KB and the cartridge); the app's
window `a8_win` and registers `a8_regs`; keys `a8_keys` generated by `sim/keymap.py`; picture `a8_pixw`; sound `a8_audio`),
`layer2_atari800.v` (the 228 ports; `tools/contract_ports.py --check`: "atari800 matches (228 ports)"; yosys `hierarchy -check`
and `check -assert` clean against a blackbox of `a8_core`; every output the machine does not drive sits at the decoupler's idle
value, the 1541 and IEC ports at their idle values, the drive windows answer at once with 0), `core_sources.tcl` (sets `l2_dcp`,
reads the 6502 sub-units of `vendor/icepi/6502n` plus the core's own `proc_core_a8`/`proc_interrupt_a8`, `gen/`, `trx/`;
no Gideon VHDL, no `cores/common`), `core.toml` (`core_toml.py --check`: ok).
The bench `sim/machine/run.sh boot` loads the machine like the app does, with a test OS of our own (`testos2.asm`, no Atari ROM)
and BASIC/cartridge stand-ins; `PASS tb_a8_machine`. **Measured and fixed on the way (each one a finding of this bench):**
- **The frame is the sync, not ANTIC's vblank.** ANTIC's `vblank_out` is low for 240 lines (vcount 8..247); the picture window
  of 288 lines never completed (`latest 0 seq 0`). `a8_pixw` now takes GTIA's `VIDEO_VS` (high for the sync lines; a frame is the
  312 lines from one sync end to the next, measured 312), the window starts `Y0 = 18` lines after the sync ends. ANTIC's 240 lines sit
  at lines 24..263 of the window; GTIA outputs **black** outside them (blank), not the border colour, so the 4:3 window of D5
  has a black band of 24 lines above and below the picture (the border colour fills only the 240 lines ANTIC frames).
- **ANTIC's hblank leaves 356 of the 384 pixels** (`HBLANK` low for 356 hires pixels, measured), not 384. The window now counts
  pixels from the end of hblank and does not stop at hblank; what hblank hides (the last 28 pixels of the window) repeats the
  last visible pixel (the border colour on a normal screen). The picture therefore sits 28 pixels to the left in the window; the
  app can shift the layer's X0 by 28 x scale. **Decided (Alex, 2026-10-14):** it stays as it is, the window keeps 384 columns
  and the application shifts X0 (D5). The 28 repeated columns are the border colour on a normal screen.
- **CONSOL is active high at the core's input:** GTIA reads `not CONSOL_IN and not CONSOL_OUT`, so START/SELECT/OPTION arrive as
  1 = pressed (`a8_core` had them inverted); the reset value of GTIA's CONSOL latch pulls all three low until the OS writes `$08`
  (the test OS does that now, as the real one does).
- **Shift/Control are latched with the key:** POKEY puts them into KBCODE when a key goes down; the bench presses the modifier first.
- **`address_decoder` drove `SDRAM_WRITE_EN` twice** (a concurrent assignment and the process at the top of the decoder, same
  value): `ghdl --synth` refuses it and Vivado treats two drivers of one net as an error. `tools/patches/atari800/0004` drops the
  concurrent one; gen regenerated.
Bench result (`sh cores/atari800/sim/machine/run.sh boot`, GHDL 5.0.1 in `vc20-ghdl`, about 43 minutes, 8 frames and all of the
checks after them): base RAM, BASIC at `$A000`, 4 extended banks through the DDR2 cache (hits 2052, misses 2176, writes 1024), the
OS's VBI counter, PAL frame 632,320 `sys_clk`, 312 lines, line 2026 `sys_clk`, frame buffer in the DDR2 model equal to the bench's own
capture of the machine's pixels (0 of 110,592 differ, not flat: 92,160 pixels differ from the corner), POKEY tone 989.7 Hz against
989.6 programmed, key A / Shift+A / Ctrl+A = `$3F/$7F/$BF`, F1/F5 on CONSOL, joystick 1 and 2, no stray DDR2 access.
**Not run / not done:** no Atari OS ROM is on this machine, so there is no real OS boot (memo pad); GHDL cannot write the whole
core as Verilog for a yosys estimate (so no yosys figure of the whole core: `sim/synth/synth_lut.sh` stops, `ghdl --synth --out=verilog` of `atari800core` / `a8_machine` / `a8_core` dies with an
internal error in GHDL's Verilog writer, `netlists-disp_verilog.adb:1336`, while `a8_mem`, `a8_pixw`, `a8_regs`, `a8_keys`,
`antic`, `pokey`, `pia`, `address_decoder`, `cartlogic`, `sio_handler` and `pokeymax` write fine one by one); no app side (the
blueprint app of T93 has no Atari window driver yet); the T93 `.cor` packing of `roms = ["atarixl.rom"]`.

**5.9 Vivado reads the core, and the base RAM had to become a real dual-port template (2026-10-14).**
Run 25 built the Atari for the first time and it fell over in six seconds of synthesis:
`WARNING: [Synth 8-4767] Trying to implement RAM 'ram_reg' in registers` and then
`ERROR: [Synth 8-3391] Unable to infer a block/distributed RAM for 'ram_reg' ... the number of bits (524288) is too large` -
the 64 KB base RAM of `trx/a8_mem.vhd`. The reason Vivado prints is the first one: *RAM has multiple writes via different
ports in same process.* GHDL does not care, Vivado does, and the stage reported the core as failed (experimental, so the
pipeline stayed green and nothing of the Atari was in the package). Measured by hand on the builder, three attempts, each the
out-of-context synthesis of `core_sources.tcl` alone:
- both ports in one process (as written): 8-4767, then 8-3391, no inference at all;
- two processes, each writing before it reads: `WARNING: [Synth 8-6849] Infeasible attribute ram_style = "block" ... trying to
  implement using LUTRAM`, then `ERROR: [Synth 8-2914] Unsupported RAM template`;
- two processes, each **reading before it writes** - the shape `cores/vc20/rtl/trx/vc20_ram.vhd` already routes with:
  `INFO: [Synth 8-3971] The signal "mem/base_ram.ram_reg" was recognized as a true dual port RAM template.`
Reading first costs nothing here: a 6502 write cycle and a window write do not use the read data, and the real hardware returns
no new byte in the write cycle either. The two arrays sit in a `block` so their type stays invisible to the rest of the
architecture - an array OF `std_logic_vector` makes every `unsigned("1" & vector)` outside it ambiguous (GHDL named `ram8_t`
and `rom8_t` as candidate interpretations).

**The first measured figures of the whole core** (out-of-context synthesis, `xc7a50tfgg484-1`, Vivado 2024.1; `pb_l2` has
18,000 LUT, 50 block RAM tiles and 80 DSP):

| | measured | of pb_l2 |
|---|---|---|
| Slice LUTs | 5,914 | 33 % |
| Slice Registers | 6,612 | - |
| Block RAM tiles | 29.5 (27 RAMB36 + 5 RAMB18) | 59 % |
| DSPs | 2 | 3 % |

The base RAM is 16 tiles, the local ROM 8, the rest is the DDR2 cache and the SIO FIFOs. That places the Atari between the
VC-20 (16.5 tiles) and the C16/Plus-4 (43), and the LUT figure is inside D11's 5,000-9,000 estimate.

**Routed against the host, runs 26 and 27 (2026-10-14).** The core stage placed and routed it inside `pb_l2` on the first
attempt, no directive hunt: **7,020 Slice LUTs of 18,000 (39 %)**, 6,593 registers, **29.5 block RAM tiles of 50 (59 %)**,
2 DSPs - the same memories as the out-of-context run, 1,106 LUTs more once the partition pins and the real host edge are
in. **WNS +0.114 ns**, `report_timing -slack_lesser_than 0` finds "No timing paths", `route_status` 0 nets with routing
errors, and `pr_verify` says the partial is compatible with that very host (2,097 partition pins, 26,583 static cells
compared). Partial 1,329,702 bytes. What is still missing before a board test is above the gateware: no Atari window
driver in the application (T93), so no `.cor` is packed, and no real OS ROM on this machine, so no memo-pad boot.

**5.10 The Freeze bench, run for the first time, and what it took to make it say anything
(2026-10-14).** The agent wrote `sim/machine/run_pause.sh` and never ran it. One defect was the
core's, four were the bench's, and only after all five does a pass mean anything:
- **`FAIL: a machine tick while halted`, once per release.** `halted` is `hcnt = 32`, and `hcnt` is
  cleared on the clock *after* the hold goes away, so in the very clock the machine takes its first
  tick again the core still claims to be halted. The application reads that bit to know the machine
  is stopped, so it is a one-clock lie, and the bench is right to call it. `held_i` now carries
  `hold_eff` in its term and falls with the hold; the rising edge still needs its 32 quiet clocks.
- **The negative control was no control.** It ran the machine with `RAMSEL = 0` instead of 1 and
  expected a different trace. In 1,500,000 ticks the test OS never reaches the extended banks, so
  both runs are identical and the control reported "the hash does not see a difference" on every
  pass - which also means `pauses1 == free` proved less than it looked. The control now writes one
  byte over a loaded image (`CTRL_BYTE`, with `CTRL_SEL`/`CTRL_ADDR` for which one), by default the
  first byte of the BASIC image, which the OS reads at `$A000` in the first frames.
- **The trace hashed nothing. Every line of every run was `XXXXXXXXXXXXXXXX`.** One `'U'` or `'X'`
  anywhere in the hashed fields enters the chain and the hash stays a metavalue for the rest of the
  run, so all four runs wrote the same file: `pauses1 == free` was true because both sides were X,
  and the control said "no difference" whatever it changed. Every field now goes through `to_01`,
  the metavalues are counted and reported, and the first one is named with all its fields.
  The machine was never the problem, measured twice: a probe on the bus shows the CPU is handed the
  control byte (`probe: addr A000 data AA` with 170 written over BASIC's first byte), and the boot
  bench with 170 over the OS reset vector fails every one of its checks and runs no cache cycle at
  all, as a machine that jumps into nothing must.
- **The invariant is the sequence of GRANTED bus cycles, not of ticks.** Hashing every tick made the
  bench fail for a reason that is not a defect: a tick in which the memory makes the machine wait is
  not a machine cycle, and how many of those there are depends on the DDR2 latency and on where a
  hold falls. The pixel and audio fields went out for the same reason - they live in the host's
  clock, which never stops, so after a resume a machine cycle meets a different pixel phase. What
  has to be identical is the sequence of cycles the machine really takes, with their addresses and
  their data; `TICKS` now counts those (85,000 of them are the 0.8 s of machine time the old
  1,500,000 ticks covered, since only about 6 % of ticks are a granted cycle).
- **One data field per cycle, the one the cycle is about.** With the granted-cycle hash the bench
  still said `pauses1` differs from `free` (from cycle 8,192) and `pauses2` from 12,288 - which
  looked like a Freeze leak and was not. A per-cycle dump of the window (`DUMP_FROM`/`DUMP_TO`,
  written beside the hash trace) found **exactly one** differing line in 1,300 cycles:
  `8138 0090 01 01 we 1 cpu 1 ...` against `8138 0090 00 01 we 1 cpu 1 ...` - a *write* cycle to
  `$0090` with the same write data, differing only in the read-data field, which on a write holds
  whatever the memory last returned and therefore depends on timing. The core gained a `tap_we`
  (the machine's write strobe; the layer leaves it open like the other taps) and the hash takes
  `tap_wdata` on a write and `tap_snoop` on a read.
- **Result: Freeze freezes** (2026-10-14, the first run of this bench that proves anything):
  `pauses1 == free`, `pauses2 == free` (other seed, other DDR2 latency and busy rate),
  `the negative control differs`, 85,000 granted cycles hashed, `PASS tb_a8_pause`. 35 of the
  1.47 M ticks carried a metavalue, all of them the same one: a read of `$D40F` (ANTIC `NMIST`)
  where one data bit is undriven. It is counted and reported, not hashed.
- Not in `ci/sims.txt` yet: four runs of about 5 minutes each, and the images need `64tass` plus
  `gen_images.py` on the runner (the gate bench needs neither). Alex's call, with the 43-minute
  whole-machine bench, which is out for the same reason.

**5.11 The real OS is in the repository, and the machine boots it (2026-10-14).** Alex: "the Atari OS
ROM may go in here. That is TREX-internal and non-public" (translated), and he chose
`cores/atari800/roms/` as its place. What is there, identified by its own content and not by a file
name (`roms/README.md` has the table): `atarixl.rom`, the 16 KB XL operating system with Matthias
Reichl's SIO patch (`Hias 1.30 100923L`), `ataribas.rom`, Atari BASIC (its keyword table is in plain
sight), and `pbi_bios.rom`, which is not Atari's but the upstream core's own XEX loader BIOS. They
come from the `releases/` directory of the MiSTer core this port is based on. The 10 KB 400/800 OS-B
is deliberately not there: this core is an 800 XL. None of it goes into a release, a `.cor` or a
package, and on the board the ROMs live in the flash as the C16's do (T90).

`sim/machine/run.sh os` runs the whole-core bench on those images instead of `testos2`: a new generic
`TESTOS = 0` drops every check that reads what the test OS wrote (its RAM contents, the tone it
programs, the keys and joysticks it reports), because with a real OS those addresses mean something
else. What is checked is what does not depend on the program: the PAL frame (632,320 sys_clk, 312
lines), the line length, that a complete frame is announced, that the frame buffer equals the bench's
own capture of the machine's pixels, that the picture is not one flat colour, and that nothing touches
DDR2 outside the modelled pieces. The picture is written to `build/frame.ppm`. `rom2hex.py` turns a
`.rom` into the one-byte-per-line form the bench reads, on the host side, where `python3` is - the
GHDL image has none.
- One operational trap, and it cost a result: killing the script or its `container run` leaves the
  simulation running INSIDE the container, writing its trace file. A zombie from a killed batch
  rewrote `free.trace` with 424 lines of a different cycle budget, and the comparison then blamed
  Freeze. Stop a run with `container stop <id>`; both run scripts say so now.
- Both run scripts now forward their knobs. `run.sh` passes extra `-g` arguments through to
  `ghdl -r`, `run_pause.sh` takes case names (run one case, compare nothing) and `-g` arguments, and
  `TICKS`/`STOP`/`FRAMES` reach the container instead of being lost at its boundary - a short
  `TICKS=30000` run silently used 1,500,000 before.

**5.12 The real XL OS brings up its screen: 110 frames, measured (2026-10-15, GHDL).**

`FRAMES=110 sh cores/atari800/sim/machine/run.sh os` in a container (the real `ATARIXL.ROM` and
`ATARIBAS.ROM` of `cores/atari800/roms/`, `RAMSEL=1`): started 06:19 UTC, finished 10:44, **PASS
tb_a8_machine**. The machine ran with the genuine OS, not the test ROM:

```
 loaded osxl.hex 16384 bytes into array 0
 loaded basxl.hex 8192 bytes into array 1
 frames run: 110
 ok   a PAL frame is 632,320 sys_clk: 632320       ok   the frame has 312 lines: 312
 ok   a line is 114 machine cycles = 2026.67 sys_clk: 2027
 ok   a complete frame is announced: latest 130 seq 110
 ok   the frame buffer equals the bench's own capture of the machine's pixels: 0 of 110592 differ
 ok   the picture is not a flat colour: 61440 pixels differ from the corner
```

The picture (`build/frame.ppm`) is the editor screen: the blue field of the OS over the window,
with the cursor block in its top left corner. **No `READY` yet** - at frame 110 the machine is
about 2.2 s in and the OS has brought up ANTIC, the display list and the cursor, but BASIC has not
printed its prompt into the frame that was captured. That is a question of simulated time, not of
the core: 110 frames cost 4.4 hours of wall clock here, so the answer to "does it say READY" has to
come from the board or from a longer run, not from this bench.

What it does prove: the real OS drives ANTIC and the frame writer end to end at the right PAL
timing, every pixel the machine produced reached the frame buffer, and nothing touched memory
outside the modelled DDR2 pieces.

**5.13 The machine runs on the board: Atari BASIC, typed by hand (2026-10-15).** The first time the core
has run outside a simulator. Alex had put the bundle in the boot menu and reported "der Atari macht gar
nix" ("the Atari does nothing"); the cause was the application image, not the core (T93 5.12). With the
image repaired and started over JTAG against the core the host had already loaded through the ICAP, the
console read:

```
[core] A8XL Atari 800 XL, cor (no hand-over)
[TRX] IO block present, CAPS C0
[TRX UI] LVGL 9.6.0 on the pixel layer: buffers at 00101C00, 001FEE30, heap free 5700576
[core] XL OS: /usb1/roms/atari800/atarixl.rom (16384 bytes)
[core] BASIC: /usb1/roms/atari800/ataribas.rom (8192 bytes)
[core] Atari 800 XL released
```

On the screen: the blue editor, `READY`, and Alex typed `10 PRINT "HALLO"`, `LIST`, `RUN` → `HALLO`
(photo, 2026-10-15). So the 6502, ANTIC, GTIA, the OS and BASIC ROMs out of the 16 KB aperture, the
keyboard matrix through the contract lines and the picture over the pixel layer all work on silicon,
with the 110-frame GHDL result of 5.12 confirmed by the real machine.

Both ROMs were found by `romfind` at the first place it looks on the stick, `/usb1/roms/atari800/`,
with the names in lower case - the comparison is `strcasecmp` (`coreapp/src/romfind.cc:65`).

Not tested, because the Freeze menu could not be opened (T93 5.13): disks (`.atr`/`.xfd` in D1:-D4:),
`.xex` through the boot disk, cartridges, the tasks, the four settings (RAM size, PAL/NTSC, joystick
ports, CPU speed 1x-8x) and POKEY sound. The whole SIO path has still only ever run in simulation.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; TODO row ✅ and history entry,
in the same commit as the code.
-->
