# T95 — An NES core in the host/core split: NES_MiSTer (GPL v3), cycle accurate, with the mappers

**Status:** BUILT AND SIMULATED, NOT ROUTED (2026-10-15). Alex decided on `M` (NES_MiSTer, GPL v3) after the first build: D12, and the core is
built on it: the machine, the 6502 converted by GHDL, the cartridge in the DDR2 behind line caches, eleven mapper families with a bench
each, the layer-2 edge, all in Verilator with negative controls (5.8-5.10). **No Vivado has seen it**: the first build (`fpga_nes`) routed in
CI run 39 and missed timing (5.5), so there has never been an `nes_partial.bit`, and the area of the PPU is not known (5.10). No board,
no application window (T93), no controller (section 4).
**Repo:** TRX64U-MultiCore (gateware under `cores/nes/`, vendoring and generator in `tools/`, benches in `cores/nes/sim`).
**Number:** T95 (the row of `TODO.md`; the NES was put on the core list on 2026-10-13).
**Origin:** Alex, 2026-10-13: "put NES on the core list, and SNES too if it works, though the buttons need looking at there, or USB
gamepads like 8BitDo's help then" (translated; `docs/todo/T79.md`). Alex, 2026-10-15: "then take NES on as a spec please and build it
overnight too! How we handle the pad I do not know yet, but never mind for now." (translated).
**Depends on:** T94 (layout, pipeline, `experimental`), `contract/` (the 228 ports), T88 (the Atari core: the precedent for vendoring,
the clock enable, the frame writer, the DDR2 window), T90 (the C16 core: the Verilog glue this core's glue is built from).
**Milestone:** M3.
**Branch of the second build:** `t95-nes-mister` (M1-M5 as commits, 5.7-5.12).

---

## §1 The problem

**Who:** Alex, who wants the C64 Ultimate board to run more than one machine; the NES is on his list of cores since 2026-10-13.

**What he wants:** pick "Nintendo Entertainment System" in the core selector, the board swaps layer 2, and an NES runs a cartridge image
from the stick with picture and sound. How the pad reaches it is not decided (section 4).

**Today (2026-10-15):** the core is built and simulated, not routed (Status above). The partition `pb_l2` is 18,000 Slice LUTs and 50 block
RAM tiles; the Atari 800 XL core uses 7,020 LUT and 29.5 tiles, the reference C64 14,103 and 39. An NES is small: 2 KiB work RAM, 2 KiB
video RAM, 256 bytes OAM, a 6502 without decimal mode, a PPU and an APU - and a cartridge of up to 2 MB PRG and 1 MB CHR with mappers.

**What the sources are (read on 2026-10-15 from shallow clones, nothing built from them by anybody else's recipe):**

| Candidate | What it is | Licence as written in its files |
|---|---|---|
| `brianbennett/fpga_nes` (`B`, commit 26085d6, 2012-07-14) | pure Verilog, 7,900 lines: a 6502 (`cpu.v`, 2,277 lines, per-clock case logic), APU without the DMC channel, PPU with background and sprites, sprite DMA, serial pad port, PWM audio, a mapper-0 cartridge, a debug link (`hci`) to a Windows tool. One clock of 100 MHz; the PPU's raster *is* a 640x480 VGA signal of 25 MHz pixels, one NES pixel = two VGA pixels | BSD 2-clause, in every file's header and in `LICENSE` |
| `MiSTer-devel/NES_MiSTer` (`M`, commit 49a0a66, 2026-09-30) | Ludde's NES with the MiSTer wrapper: SystemVerilog, 11,000 lines in `rtl/`, 14 mapper files, savestates, composite video decoder, SDRAM controller, T65 | GPL v3 (`COPYING`, file headers "GPL Licensed") |
| `nand2mario/nestang` (`N`, commit 5b24a71, 2025-06-20) | `M`'s lineage reduced for Tang Nano boards (Gowin PLLs, HDMI, SDRAM, USB HID) | GPL v3 |

`B` has no mapper beyond NROM and no DMC and is not cycle accurate (its PPU timing is the VGA timing; it passes the NROM games its author
tried, README). `M` and `N` are the accurate ones.

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms, and that the sources are exactly the pinned ones.
- **P2 The machine is alive.** CPU, PPU, NMI, sprite DMA, work RAM run a program and make a picture.
- **P3 Clock, reset and Freeze** (contract C1/C10).
- **P4 The cartridge.** Where PRG and CHR live, how the app loads them, what is supported.
- **P5 Video.** The picture into the host's pixel layer.
- **P6 Audio.**
- **P7 The partition's edge.** The 228 ports, the registers, identity, the idle values of everything unused.
- **P8 Size and timing** against `pb_l2`.
- **P9 Input.** The controller. **Out for now (section 4).**

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the sources are the pinned ones, and what is built from them is what the generator makes (P1)
  Given cores/nes/rtl vendored from NES_MiSTer 49a0a66 by tools/vendor_nes_mister.sh, with SHA256SUMS
  When  ci/lint.sh runs `tools/nes_gen.py --check`
  Then  rtl/ matches SHA256SUMS byte for byte, cores/nes/gen is exactly what rtl/ makes, and the VHDL the 6502 conversion reads has the
        recorded hash
  And   one changed byte in rtl/ or a changed upstream line the generator matches on makes the check fail (every change is counted)
```
```gherkin
Scenario: S2 a program of ours runs and draws (P2, P5)
  Given a cartridge of our own (cores/nes/sim/mkcart.py: waits two vblanks, palette, a band of tile 1, four tiles of a checker, one
        sprite through $4014, NMI on, an NMI handler that counts in $00), its header in the INES registers and its image through the
        app's DMA window into the DDR2
  When  the machine leaves reset and runs six frames
  Then  the newest frame buffer holds a 0x16 band over rows 0..7, a 0x16/0x30 checker at x 32..63 on rows 8..15, a 0x2A sprite at
        x 100..107 on rows 53..60, the backdrop 0x21 everywhere else; $00 counted 3 to 5 NMIs; the frame period is 1.664 M clocks
  And   the same program without `$2001` (norender) shows only the backdrop and the NMI never counts; with an empty NMI handler
        (nonmi) the counter stays 0 and the picture is unchanged
```
```gherkin
Scenario: S3 Freeze freezes the whole machine through its own handshake (P3)
  Given two cores from the same reset, B asked to pause (pausecore) after 3 ms
  When  B has paused (it waits for vblank on an instruction boundary: up to a frame), is held 5 ms and released
  Then  B reports halted, counts no dot, CPU cycle or frame and writes nothing to the DDR2 while held; afterwards A and B agree frame
        for frame on the hash of the machine's own request addresses and PPU position (B is shifted by one master tick)
  And   a core whose pause request never reaches the machine (-DNES_MUT_NOHOLD) fails this bench, and the bench says it never halted
```
```gherkin
Scenario: S4 the app's window loads, reads back and rewrites the cartridge image in the DDR2 (P4)
  Given the machine in reset and CART_BASE at its default
  When  the app writes the PRG image and the CHR image byte by byte through the DMA window (offset = {WIN_PAGE, 16 bit address}) and reads
        bytes back, across pages and at the top of the 2 MB window
  Then  every byte reads back, one lane of a 16 byte DDR2 word is written at a time, another CART_BASE is another place, the registers
        answer (ID "NES_", BUILD, INES bytes, the flags word made of them)
  And   a byte of the image changed through the window under the running machine is seen by its next read (cached line dropped); a core
        that leaves the line cached (-DNES_MUT_NOWINV) fails the `inv` bench
```
```gherkin
Scenario: S5 the APU makes sound and silence is silence (P6)
  Given the program of S2 plus pulse channel 1 on
  When  the host's mclk reads the samples for six frames
  Then  they swing by more than 1,000,000 of the 24 bit range, and the same program without the tone gives exactly 0 on every sample
```
```gherkin
Scenario: S6 the 6502 that GHDL made of T65 computes like the instruction set says (P2)
  Given a program of ours (build_cpu) that runs ADC, SBC, logic, shifts, compares, INC/DEC, every RAM addressing mode, the stack, JSR and
        24 branches on edge values and stores 228 result bytes
  Then  they equal plain arithmetic written from the instruction set, and a wrong expectation fails the bench
```
```gherkin
Scenario: S7 bank switching reaches the right bytes through the cache (P4)
  Given a cartridge of ours for each built mapper (UNROM/CNROM/AxROM family = Mapper28, MMC1, MMC2, MMC3, MMC4, VRC2/4, GxROM, BNROM,
        Camerica, NINA-03/06), images up to 128 KB PRG and 64 KB CHR in the DDR2 behind a model of the host's port (293-693 ns per read)
  When  its program switches PRG and CHR banks in several patterns and reads the same CPU / PPU addresses in each
  Then  every byte is the one of the selected bank (the byte pattern differs for every 8 KB PRG and 4 KB CHR bank), the MMC2 latches
        change the CHR bank for the reads after $0FE8 and not for that read itself
  And   a cache that knows nothing of banks (-DNES_MUT_NOINV) fails the UNROM and CNROM benches, an address that follows the machine
        during a read (-DNES_MUT_NOADDRHOLD) fails the MMC2 bench
```
```gherkin
Scenario: S8 a miss holds the machine and nothing else changes (P4)
  Given the model of the host's port and a program that runs from the DDR2
  When  the cache misses
  Then  the machine's clock enable is held until the line is in and the results are those of a memory that always answers; a core that does
        not hold (-DNES_MUT_NOSTALL) gets wrong data and fails the instruction bench; the share of clocks held is counted (STALL)
```
```gherkin
Scenario: S9 the core sits on the partition's edge (P7)
  Given layer2_nes with all 228 ports
  When  tools/contract_ports.py --check runs, and run_l2.sh drives the real edge
  Then  the ports are the contract's; the ID reads "NES_" through cw_*, the machine leaves reset, Freeze through c64_halt_req gives
        c64_halted (within 40 ms) and no DDR2 write, release runs on; the drive port, the REU, the cartridge port and the IEC lines sit
        at the decoupler's idle values
```
```gherkin
Scenario: S10 it routes in pb_l2 with positive slack (P8)
  Given the core stage of the CI against the cached host
  Then  Slice LUT <= 18,000, block RAM tiles <= 50, WNS > 0 and pr_verify says compatible (the report lines of the core stage)
```
```gherkin
Scenario: S11 a real cartridge on the real board (P2, P4, P5, P6)   -- by hand, later
  Given the core on the C64U and an image from the stick (never in the repository)
  Then  the picture is on the screen and the sound is on the speaker; this needs the app side (T93) and a way to load the image
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | `python3 tools/nes_gen.py --check` | `ci/lint.sh` ("nes gen") |
| S2 | P2, P5 | `sh cores/nes/sim/run.sh frame norender nonmi` | `ci/sims.txt` `nes` (tier 2) |
| S3 | P3 | `run.sh pause` and its mutation | `nes` |
| S4 | P4 | `run.sh win inv` and the nowinv mutation | `nes` |
| S5 | P6 | `run.sh tone frame` (silence) | `nes` |
| S6 | P2 | `run.sh cpu` and its negative control | `nes` |
| S7 | P4 | `run.sh banks2 banks3 mmc1 mmc2 mmc3 mmc4 vrc24 mmc66 mmc34 mmc71 mmc79` and the noinv, noaddrhold mutations | `nes_maps` (tier 2) |
| S8 | P4 | the nostall mutation of `cpu` | `nes` |
| S9 | P7 | `tools/contract_ports.py --check`; `sh cores/nes/sim/run_l2.sh` | `ci/lint.sh`; `nes_l2` (tier 2) |
| S10 | P8 | Vivado report lines of the core stage (`=== T95 nes synth ...`, timing, `pr_verify`) | CI `bitstream core nes`, **not run yet** |
| S11 | P2, P4-P6 | board session | open |

## §3 Decision

- **D1 Source: `NES_MiSTer` (`M`), D12.** The first build used `fpga_nes` (`B`, BSD 2-clause, mapper 0 only, no DMC, not cycle accurate)
  because it was the simplest to wrap; it routed in CI run 39 and missed timing (5.5). Alex chose `M` (D12) and `B` is gone from the tree.
- **D2 Vendor unchanged, wrap in our own top.** `tools/vendor_nes_mister.sh <checkout>` copies 22 files at 49a0a66 to `cores/nes/rtl` and
  writes `SHA256SUMS`; `NOTICE` states GPL v3 (D12). Not taken: the MiSTer framework and board parts (5.7). `cores/nes/trx/nes_core.v`
  is what `NES.sv` is for MiSTer: it wires `module NES` to our memory, pad, picture, sound and registers.
- **D3 The machine runs on `cpu_clk`, 100 MHz, with the master clock as an enable.** The machine keeps its own dividers (CPU /12, PPU /4
  of a 21.477 MHz clock); an NCO makes that clock as a clock enable (4 or 5 clocks apart) and every clocked process has it (D4). The
  paths inside the machine have at least four clocks: `cores/nes/nes.xdc` says so (D15). Measured frame period 60.10 Hz.
- **D4 `tools/nes_gen.py` makes `cores/nes/gen` from `rtl/`, and only `gen/` is built.** Every change counted (5.7): the clock enable, the
  savestate registers replaced by their defaults, the wired-or nets of `cart.sv` made an OR/AND over the mappers that are built, the
  mappers that are not built cut, the cheat module and the composite board cut, the 6502 converted by GHDL, one tap (`px_ce_o`).
  `gen/` is committed (the CI has no checkout of NES_MiSTer and no GHDL); the lint stage runs `--check`.
- **D5 The cartridge memory is ours, `nes_mem.v`** (D13): the machine's linear address space decoded by its top bits; PRG and CHR ROM in the
  DDR2, work RAM, PRG RAM, nametable RAM and CHR RAM in block RAM.
- **D6 The app's window** is the only way to put an image in: `{WIN_PAGE, 16 bit address}` is the 0-based offset in the PRG or CHR image
  (2 MB each), `nes_win.v` answers when the DDR2 is done. The port inputs are registered before use (5.5's lesson).
- **D7 Picture: 256 x 224, RGB565, through the existing pixel layer.** The visible window is the NES picture without the 8 overscan lines
  top and bottom; the pixel of dot `cycle` is x = cycle - 2 (measured). The frame writer is `c264_pixw.v`'s circuit (`nes_pixw.v`: line
  buffer, four buffers in the DDR2 at `0x01D00000`, stride 1 MiB, frame-done toggle, `fb_*` registers like the other cores), strobed
  once per PPU dot three clocks after the tick that made it. Colours: the PPU's 6-bit palette index through `nes_pal.v`. The host's
  VIC-II input gets the C16 core's dummy raster. NTSC; the register for PAL is 5.11.
- **D8 Audio:** the APU's 16 bit `sample` (all five channels, DMC included) is read every 2112 clocks (on `cap`), the DC level is
  tracked out, the result goes to `mclk` by a toggle (`nes_audio.v`). Mono; Freeze mutes.
- **D9 Freeze:** the machine's own `pausecore` / `corepaused` handshake (D12). The request is held until the machine is in vblank on an
  instruction boundary (measured 12.4 ms in the bench, up to one frame), then all counters stand; release resumes at the same dot, one
  master tick later than a free machine. `c64_halted` is "paused and the frame writer has nothing on its way to the DDR2".
  `gate_frz = 1` as for every non-C64 core: choosing Freeze freezes. A reset takes the pause away.
- **D10 The registers** are `c264_regs.v`'s block with NES contents (ID `NES_`, CTRL, MISS/STALL counters, CART_BASE, WIN_PAGE, the 16
  INES bytes, the frame writer, page 6 to the pixel layer); the JTAG words are `trxdbg trx 0` words 0-6.
- **D11 Descriptor:** `app = "blueprint"` (T84 D12), `experimental = true`, `order = 8`, `sources = ["cores/nes"]` without `sim` and `rtl`
  (the coordinator's `host/layers.txt` `ignore:` lines, 5.4).
- **D12 Source: `NES_MiSTer` (`M`), decided by Alex on 2026-10-15.** His words: "the MiSTer NES core, definitely. GPL is great,
  after all" (translated). This reverses the first build's choice of `B`. What it buys: cycle accuracy, the DMC channel, 40 mapper modules
  (MMC1, MMC3, MMC5, VRC, Namco, Sunsoft, FDS ...), PAL timing, and a machine with a clean memory interface and a pause handshake of
  its own (5.6). What it costs is in 5.6 as well, measured and not guessed: SystemVerilog that Yosys cannot read, a VHDL 6502 (T65)
  that Verilator cannot compile, about 90 savestate hooks into a framework that is not in the repository, `tri0` bus resolution in
  `cart.sv`, and a cartridge that no longer fits in block RAM.
  **Licence:** GPL v3, and the repository is private. What a GPL v3 core changes for us: the gateware of this core and everything
  linked into its partial bitstream is covered, so a `.cor` of this core that is given to a third party has to come with its
  corresponding source — the vendored tree, our generator and our glue, which is exactly what is in this repository. Alex's own
  releases on his NAS are not conveying to a third party. The host, the other cores and the applications are separate works in
  separate partial bitstreams and are not touched by it. `cores/nes/rtl/NOTICE` says this, and `host/` stays out of this core's build.
- **D13 The cartridge is in the DDR2 behind line caches that hold the machine on a miss (M3, 5.9).** PRG ROM and CHR ROM at `CART_BASE`
  (default 0x01000000) through the real-time port `c_*`; a 4 KB PRG and an 8 KB CHR cache, direct mapped, 16 byte lines, keyed by the
  linear address the mapper produced (a bank switch needs no invalidation, a window write does); the address of an access is taken
  when it begins (MiSTer's SDRAM does the same; MMC2's latch needs it). A miss takes the clock enable away until the line is in.
- **D14 Mappers (M4, 5.10):** MMC0/Tepples (`Mapper28`: 0, 2, 3, 7, 28, 94, 97, 180, 185), MMC1, MMC2, MMC4, MMC3 (4, 33, 37, 45, 47,
  48, 52, 74, 76, 80, 82, 88, 95, 112, 118, 119, 154, 189, 191, 192, 194-196, 205-208, 268), VRC2/4 (21, 22, 23, 25, 27), GxROM
  (11, 38, 46, 66, 86, 87, 101, 140, 144, 145, 149), BNROM (34), Camerica (71, 232), NINA-03/06 (79, 113, 133, 146, 148). The rest of
  the 40 modules is in `rtl/` and costs one line in `tools/nes_gen.py` (`BUILT`) and a bench each; the measured prices are in 5.10.
- **D15 Timing (5.5, 5.11):** the machine's paths are `set_multicycle_path`'d in the core's own `nes.xdc` (4 clocks inside the machine, 3
  from it to the glue, 4 from what is tick-aligned into it); everything outside the machine samples it on `cap` or three clocks
  after a tick, and what goes in changes on a tick. **No Vivado run has seen any of it.**

## §4 Out

- ~~**The controller.**~~ **Built on 2026-10-15, see 5.18:** the C64's joystick ports and its keyboard drive both NES ports
  (`nes_pad.v`). What stays out of that decision: a USB HID gamepad (the firmware's HID driver decodes keyboards and mice only,
  5.18), a Famicom four-player adapter, the Zapper and the Power Pad, keys for the directions, and a mapping the user can change
  (the table is fixed in the gateware; the application has no page for it).
- **Mappers that are not built** (D14; the machine's 40 modules, the rest are in `rtl/`): MMC5, FDS, NSF, Namco 163, Sunsoft 5B,
  VRC6/VRC7 (their expansion audio), JY Company, Sachen, and everything in `misc.sv`. Expansion audio altogether (the machine is told
  `ext_audio = 0`). Prices in 5.10.
- **Savestates, rewind, cheats, composite video, the Zapper, Miracle Piano, keyboard, Vs. System, PAL pictures other than by the
  register of 5.11, overscan options, the OSD.** Savestates are replaced by the Freeze (D9).
- **Battery RAM / saves, PRG RAM beyond 8 KB, CHR RAM beyond 8 KB.** `$6000-$7FFF` is 8 KB of block RAM and not saved.
- **Core application (T93):** no window of the blueprint exists for this core; the app cannot load an image yet. The registers
  (INES bytes, CART_BASE, WIN_PAGE) and the window are the interface it will use; the contract is 5.8 and 5.9.
- **Routing, board test, a release set.** The core is `experimental`; CI routes it after the push.
- **ROM images.** None in the repository, none in a bench, none in a package.

**Open questions for Alex** (5.12)
- Q1 Mappers: answered by Alex on 2026-10-15 - "the MiSTer NES core, definitely. GPL is great, after all" (translated), D12.
- Q2 The mappers beyond D14 and the audio of the ones with expansion chips: which of the prices in 5.10 are worth paying?
- Q3 The first Vivado run is the measurement of D15 and of the PPU's area; if the multicycle constraints do not take, the answer is a
  second clock enable domain or a lower machine clock (5.11): wait for the run before deciding.

## §5 Decisions taken while building

### 5.1 The benches of the first build (`fpga_nes`, Verilator, 2026-10-15, local; superseded by 5.8, 5.9, 5.10)

`sh cores/nes/sim/run.sh` (build 15 s, all modes 50 s on the Mac) and `run_l2.sh` (3 s), all green:

```
== frame     ok a frame is complete / every DDR2 write is inside the four frame buffers / FB_OVERRUNS = 0
             frame period 1667200 clocks, 28672 DDR2 writes ($00 = 4)
             ok band, backdrop, checker (x 32..63, rows 8..15), checker edges, sprite (x 100..107, rows 53..60), sprite edges
             ok no sound programmed: the samples are exactly 0                                   PASS tb_nes frame
== norender  ok the band row, the sprite and the checker are the backdrop; $00 stays 0             PASS
== nonmi     ok $00 stays 0 with an empty handler, the picture is the same as in frame             PASS
== tone      3742 audio samples (2112-clock windows), min -648877 max 5341269 ok swing > 1,000,000  PASS
== win       ok vector bytes, tile 1, last bytes, no aliasing below $8000 / at $2000, ID "NES_", BUILD, CARTCFG, STATUS  PASS
== pause     A 6819741 clocks, B 6299741 (5.2 ms held = 520,000 clocks): ok no clock, halted, bus still, no DDR2 write, bus hash equal  PASS
== mutation: -DNES_MUT_NOHOLD fails "B counted no clock in 5 ms held"                        ok
== l2        ID NES_, left reset, 8672 DDR2 words, idle ports, Freeze halts and stops the writes, release runs   PASS tb_nes_l2
```

The first run of the picture had one failing check - the expected colour of the sprite was my arithmetic error in the RGB565 of 0x2A
(0x5EEA, the machine has 0x5ECA = 58D854 rounded); the sprite sits at exactly x 100..107, rows 53..60, which is what a real NES shows
for OAM y = 60 (lines 61..68; the window starts at line 8).

The negative controls bite: `norender` and `nonmi` run the same checks and must see the other result; the mutation removes the hold and
the pause bench must name the clock count. A 40 KB cartridge of our own (`mkcart.py`, 110 bytes of 6502 code and three tiles) is
the only "ROM" anywhere; it is generated into `build/`.

### 5.2 What `B` does that the spec did not know (first build, history)

- Its CPU is not a divided-clock design: `cpu.v` has a 56-phase counter inside, so the whole machine moves on every 100 MHz clock and
  `ce` has to reach all of it (hence D4, 39 processes).
- The PPU's pixel strobe `pix_pulse` comes one clock before `nes_x` changes; the palette index is stable then, which is where the
  frame writer samples (the bench would show a one-pixel shift otherwise: sprite and checker edges are checked).
- `jp.v` polls the serial pad itself (latch and clock lines at a fixed rate) and reads data active low.
- The vendored `ppu_vga.v` blanks the first 8 and last 8 NES lines and everything right of x 255 with a border colour; the window
  therefore is 256 x 224.

### 5.3 What I did not do tonight (first build, history)

No Vivado (the brief), so no S7. No application, no board.

### 5.4 Area and timing of the first build: an estimate, not a measurement (history; the new numbers are 5.10)

The only synthesis here is Yosys `synth_xilinx -family xc7` on `layer2_nes` (flattened for the total), which is not Vivado but counts
the same primitives:

| | LUT | FF | RAMB36 | RAMB18 |
|---|---|---|---|---|
| whole `layer2_nes` | 3,237 (of which 5 distributed RAM64M sites) | 2,290 | 12 | 2 |
| `cpu.v` (the 6502) | 791 | 156 | | |
| `ppu` + `ppu_spr` + `ppu_bg` + `ppu_ri` + `ppu_vga` | 297 + 345 + 128 + 88 + 34 | | | |
| `nes_pixw` (line buffer in 2 RAMB36, 604 FF) | 233 | 604 | 2 | |
| `nes_cart` (PRG 8 + CHR 2) | 29 | | 10 | |
| `nes_regs` | 288 | 101 | | |
| APU (all channels, from the per-module counts) | about 375 | | | |

That is **about 3,200 LUT of 18,000 (18 %) and 13 block RAM tiles of 50 (12 RAMB36 + 2 RAMB18 = 13)**, against 7,020 / 29.5 for the Atari.
Vivado usually packs tighter than Yosys, so I expect the routed number to be lower, not higher. Two things the estimate cannot say:
timing at 100 MHz (`cpu.v` has long case-decoded paths; the `ce` net is registered and has ~2,300 sinks, which the tools replicate),
and whether the partition's clock region has the BRAM columns for 13 tiles near the ports - the Atari's 29.5 shows it has.
The 6502 question: `cpu.v` is 791 LUT in Yosys; `vendor/icepi/6502n` measured 848 LUT alone in the Atari core (T88 §5, Vivado), so a swap saves
nothing in area and no second CPU was vendored; if S7 reports a negative WNS on `cpu.v`, the swap is the first thing to try (D3).

---

- **2026-10-15, review (coordinator): the core's cache key no longer holds its benches or the
  pristine copy.** `host/layers.txt` got `cores/nes/sim` and `cores/nes/rtl` in its `ignore:`
  section (a core may not edit that file; the coordinator did), and `core.toml` excludes both. What
  is built is `gen/`, which is tracked and which `tools/nes_gen.py` makes from `rtl/` with
  `ci/lint.sh` checking the two against each other — so the upstream copy belongs in lint's key and
  not in the gateware's. `tools/host_pin.sh --check` after the change: "the host is unchanged (257
  files)", as it must be — `host:` and `shared:` did not move and `layers.txt` is excluded from the
  host key by T94 D13.

### 5.5 CI run 39: `fpga_nes` routed and missed timing (measured, 2026-10-14 23:14-23:20 UTC)

The push of `d508668` was the first run that implemented this core. The pipeline is green (the core is `experimental`, T94 D8), and the
package `main-d5086688` holds **`nes.timing.rpt` and nothing else of this core** — no `nes_partial.bit`, no utilisation report, so the
stage dropped the bitstream on the timing failure:

```
WNS -0.664 ns   TNS -7.058 ns   37 failing endpoints of 34,912   WHS +0.052 ns (hold is clean)
worst path: l2/nes/win/p_ram_reg/C  ->  l2/nes/m/cart/prg_reg_0_7/ADDRARDADDR[13]
            9.928 ns of 10.000: logic 0.952 ns (4 levels, LUT2+LUT3+2xLUT6), route 8.976 ns (90.4 %)
```

So the open question of 5.4 has an answer, and it is not the one that was expected: **the 6502 is not the problem**. The path that
fails is the DMA window's data register driving the cartridge block RAM's address — 90 % route delay, which is distance, not logic.
That path only moves while an image is being loaded, and it is one register stage (or a `set_multicycle_path`) away from closing.
Two lessons for the port, both of them cheap to apply: the loader path into the cartridge memory gets a register stage, and a machine
that only advances on `ce` can have its paths relaxed with `set_multicycle_path` instead of being pipelined by hand - a core may ship
its own XDC through `core_sources.tcl`.

### 5.6 What `M` is, read and measured on 2026-10-15 (commit 49a0a66)

Shallow clone in the scratchpad, nothing of it in the repository yet. `rtl/` is **30,899 lines** (SystemVerilog, Verilog and VHDL):

| part | lines | note |
|---|---|---|
| `cart.sv` | 2,592 | the mapper multiplexer, `cart_top` |
| `mappers/*.sv` (14 files, 76 modules) | 12,722 | `misc.sv` 3,552, `VRC.sv` 1,559, `generic.sv` 1,335, `MMC3.sv` 1,262, `FDS.sv` 981, `MMC5.sv` 786 … |
| `ppu.sv` | 2,366 | |
| `apu.sv` | 1,290 | with the DMC channel |
| `nes.v` | 1,178 | the machine, `DmaController`, `VsProtection` |
| `t65/*.vhd` | 2,546 | **VHDL** 6502 (`T65.vhd`, `T65_MCode.vhd` 1,271 lines, `T65_ALU.vhd`, `T65_Pack.vhd`) |
| `composite_decoder.sv`, `video.sv`, `miracle.sv`, `keyboard.sv`, `zapper.sv`, `dsp.v`, `sdram.sv`, `ddram.sv`, `pll.v` | about 2,900 | board and MiSTer framework, all of it out |
| `savestates.vhd`, `bus_savestates.vhd`, `statemanager.vhd`, `regs_savestates.sv`, `savestate_ui.sv` | about 1,100 | the MiSTer savestate framework, out (we have the Freeze) |

**The machine's interface is clean** (`module NES`, `rtl/nes.v:74`) and this is what makes the port tractable: `cpumem_addr[24:0]`,
`cpumem_read/write/dout/din`, `ppumem_addr[21:0]`, `ppumem_read/write/dout/din`, `refresh`, `prg_mask[20:0]`, `chr_mask[19:0]`,
`bram_addr/din/dout/write` (the battery RAM), `color[5:0]`, `sample[15:0]`, `joypad_clock/out`, `joypad1_data/joypad2_data[4:0]`,
`mapper_flags[63:0]`, `sys_type[1:0]` (NTSC/PAL/Dendy) — and **`pausecore`/`corepaused`**, a pause handshake the design already has
because the savestates need one. That is our Freeze (D9) with no surgery on the machine.

**Four obstacles, each measured, none of them a show-stopper:**

1. **Yosys cannot read it, so there is no local area number for the machine.** `ppu.sv:532` is a `typedef enum ... } oam_state =
   STATE_IDLE;` with an initialiser, `misc.sv:3273` is `'{default:0}` on an unpacked array; both are legal SystemVerilog and both end
   the Verilog-2005 frontend with a syntax error. Vivado takes them. Consequence: the area of PPU, APU, T65 and `cart_top` can only
   be measured on the builder, and the first core run is the measurement, not a formality.
2. **The mappers are cheap; `cart.sv`'s wiring is the work.** Yosys does read the mapper files once the savestate instances are
   removed, `synth_xilinx -family xc7` per module (I/O buffers not counted):

   | module | LUT | FF |
   |---|---|---|
   | `MMC3` (mappers 4, 118, 119, 47, 206 …) | 559 | 205 |
   | `VRC24` (21, 22, 23, 25) | 121 | 114 |
   | `VRC6` (24, 26) | 95 | 106 |
   | `Mapper28` (Action 53) | 89 | 23 |
   | `MMC1` (1, 105, 155) | 40 | 25 |
   | `MMC2`/`MMC4` (9, 10) | 34 | 27 |
   | `MMC0` (0, 2, 3, 7, 94, 180 … the discrete logic boards) | 2 | 0 |

   A subset of MMC0 + MMC1 + MMC2/4 + MMC3 + VRC is **under 900 LUT** and covers the great majority of the library. What is not cheap:
   `cart.sv` resolves the mappers' outputs on **twelve wired-or nets in five declarations** (`cart.sv:83-86` and `:2307`:
   `prg_allow_b`, `vram_a10_b`, `vram_ce_b`, `chr_allow_b`, `irq_b`, `prg_addr_b[21:0]`, `chr_addr_b[21:0]`, `flags_out_b[15:0]`,
   `audio_out_b[15:0]`, `prg_dout_b[7:0]`, `chr_dout_b[7:0]`, `fds_diskside[1:0]`) — an idiom Quartus synthesises and Vivado does not. It has to become an explicit multiplexer
   over the mappers that are built, which is also where the subset is chosen.
3. **The savestate hooks have no framework here.** `eReg_SavestateV` and the `SSREG_INDEX_*` constants live in MiSTer's `sys/`
   library, which this repository will not carry; there are about 90 instantiations across `ppu.sv` (9), `apu.sv` (4), `generic.sv`
   (9), `misc.sv` (15), `VRC.sv` (11), `Namco.sv` (8), `Sunsoft.sv` (8), `MMC5.sv` (6), `MMC3.sv` (5) and six more files. They come
   out in the generator (`tools/nes_gen.py`, D4: every change counted, a changed upstream fails lint), not by hand in the vendored
   copy. Each one is a 64-bit register, so this is area as well as tidiness.
4. **The cartridge cannot live in block RAM any more.** Mapper 0 was 32 KB + 8 KB; MMC3 images are up to 512 KB PRG + 256 KB CHR,
   and the whole partition has 50 tiles = 225 KiB for the machine *and* the pixel path. So PRG and CHR move behind the DDR2 window,
   which is what `cpumem_*`/`ppumem_*` are for. The budget that decides whether that works, from the NES's own rates: the CPU reads at
   1.79 MHz (558 ns per access) and the PPU fetches at 5.37 MHz (186 ns) while rendering, and the MIG's user port is 128 bits wide, so
   one read returns 16 bytes — a whole tile row pair for the PPU, a cache line for the CPU. Nametable and OAM stay internal (2 KB +
   256 B). That is the one piece of real engineering in this port, and it is a cache, not a rewrite.

### 5.7 M1: vendored, and the generator (2026-10-15, local)

`tools/vendor_nes_mister.sh <checkout>` copies 22 files of `NES_MiSTer` at 49a0a66 to `cores/nes/rtl` (nes.v, ppu.sv, apu.sv, cart.sv,
`regs_savestates.sv`, the 13 mapper files, the four T65 VHDL files, COPYING) and writes `SHA256SUMS`; it refuses another commit and a dirty
checkout. `fpga_nes`, its generator output and `tools/vendor_nes.sh` are gone. `cores/nes/rtl/NOTICE` is the GPL v3 text of D12.

`tools/nes_gen.py` makes `cores/nes/gen` (about 580 KB, 42 modules reached from `NES`, the rest of the mapper files is not written)
and every change is counted (the table is the state after M4: ten mappers built; 5.7 was written at M1 with six); the numbers of the run (`python3 tools/nes_gen.py`) are in its output and `--check` (ci/lint.sh "nes gen")
fails when `rtl/` is not `SHA256SUMS`, when `gen/` is not what `rtl/` makes, or when the VHDL the 6502 conversion reads has another hash:

| change | places |
|---|---|
| clock enable `ce_in`: clocked processes / module ports / instances | 49 / 42 / 61 |
| `eReg_SavestateV` replaced by its default (built files) | 42 (+1 in T65.vhd) |
| `savestates`, `statemanager`, cheat module, composite board cut from nes.v | 1 each |
| `: N'hZ` driver in a mapper made the pull value (0, all ones on the data buses); `inout` made `output` | 255; 262 |
| wired-or connections of the built mappers (per-instance wire + OR/AND in cart.sv) | 101 on 12 nets |
| mapper instances cut from cart.sv (58) and the three tie-offs (EEPROM bus, mapper 413 address bits, FDS disk side) | 58; 3 |
| `wire`/`output` that a process drives made `logic`; `always begin` made `always @* begin` | 22; 10 |
| T65: bus package use and savestate registers, `DO`->`DOut`, `Break`->`BrkS`, `ALU_Q`->`ALUQs` (GHDL and SV name clashes) | 1+1; 1+1; 5; 8 |

Findings that were not in 5.6: (1) the tri0/tri1 idiom is uniform (`assign X_b = enable ? v : N'hZ;` in every mapper), so the
multiplexer is a mechanical rewrite and not a hand edit; (2) the savestate register is `Dout_buffer <= def` on reset and written only
through the bus, so with no bus it is the constant `def`; (3) `ppu.sv` and the mappers drive `wire`s from `always_comb`, which Quartus
accepts and the language does not (Verilator PROCASSWIRE), 18 declarations become `logic`; (4) GHDL names an instance's output wire
`<label>_<port>` and clashes with the signal `ALU_Q` of T65 (`alu_q` declared twice and assigned to itself); the SV keywords `do` and
`break` are VHDL names in T65. `verilator --lint-only` on `gen/` has no error (the warnings are upstream's: missing pins of `ClockGen`).

### 5.8 M2: the machine simulates (Verilator, 2026-10-15, local)

**The 6502.** Convert T65 once, mechanically (M2 a): GHDL 5.0.1 (`debian:trixie-slim`, `apt install ghdl`, container, niced)
`--synth --std=08 -frelaxed -fsynopsys --out=verilog` of a wrapper entity `T65_nes` (the ports the NES uses, the savestate bus and the
65C816 tied off) makes `cores/nes/gen/t65/t65_nes.v` (8,900 lines; `tools/nes_gen.py --vhdl` re-runs it, `--check` compares the hash
of its VHDL input, `t65_nes.vhd.sha256`). It took about 40 minutes including the generator, not the 90 allowed. Three VHDL-side
edits were needed and are counted in 5.7 (two SystemVerilog keywords as names, one name clash inside GHDL's output). **What this does
not prove:** the converted netlist is not compared against a VHDL simulation of T65. It is exercised by the program of every bench
and by the `cpu` bench: a program of ours (`mkcart.py build_cpu`) runs ADC and SBC over eleven edge-value triples each with
N, V, Z and C read back through PHP, SED/ADC (the 2A03 has no decimal mode), AND/ORA/EOR, ASL/LSR/ROL/ROR with both carries,
CMP/CPX/CPY, INC/DEC, INX/DEY, every addressing mode that reads or writes RAM (abs,X abs,Y zp,X (zp,X) (zp),Y with a page crossed),
the stack, JSR/RTS and 24 conditional branches; the 228 result bytes it stores are compared with plain Python arithmetic written
from the instruction set (not a model of the chip): 0 differ. Its negative control (one expected byte wrong) must fail and does.
Not covered: BRK, IRQ, the undocumented opcodes, `JMP (ind)` page wrap.

**Clock.** The machine keeps its own dividers (CPU /12, PPU /4) and runs on `cpu_clk` with `ce` = an NCO of 21.477272 MHz / 100 MHz
(`nes_core.v`, 32 bit step 922441840): measured frame period **1,663,935 clocks = 60.10 Hz** (`frame` bench). The enable ticks 4 or 5
clocks apart. `cap` is the clock before a tick: everything outside the machine that samples its combinational outputs does it
there (`nes_mem`'s captured request, the audio sample, the debug address) or three clocks after a tick (the dot strobe), so a write
never happens on a transient; what the machine gets from outside (pause request, reset, mapper flags) is registered on the enable.
These rules are what `cores/nes/nes.xdc` states as multicycle paths (5.9).

**Memory (`nes_mem.v`, stage one: block RAM).** The machine's `cpumem_*`/`ppumem_*` address the linear space MiSTer's SDRAM held;
`nes_mem` decodes it by the top bits like `cart.sv`'s last block: PRG ROM `0x000000` (32 KB here), work RAM 2 KB `0x380000`, PRG RAM
`0x3C0000` (8 KB), nametable RAM `0x3A0000` (2 KB), CHR `0x200000` (8 KB, writable when the header says CHR RAM). The data comes
back two ticks late at most (captured request, block RAM, registered select), which the machine tolerates because it asks five
ticks (CPU) or four (PPU) before it uses a byte. The cartridge arrives as the app writes it: the iNES header into the 16 `INES`
registers (`0E0-0EF`, `nes_flags.v` makes `mapper_flags`, `prg_mask`, `chr_mask` from them with the expressions of GameLoader in
NES.sv) and the image through the DMA window, whose address is now the 0-based image offset `{WIN_PAGE (0D4), 16 bit window address}`;
an offset beyond the memory reads 0 and writes nothing. The window's port inputs are registered before use (the answer to run 39's
failing path, 5.5): data is two clocks after the request instead of one, which only the JTAG peek of the window notices.

**What the benches measured** (`sh cores/nes/sim/run.sh`, 40 KB image of ours, mapper 0; all green, mutation fails as it must):

| bench | result |
|---|---|
| frame | frame complete; **window X0 = 2** (the PPU's `color` of dot `cycle` is the pixel x = cycle - 2); band, backdrop, checker x 32..63 rows 8..15, sprite x 100..107 rows 53..60, all edges; `$00` counted 3 NMIs in six frames (two warm-up); 43,008 DDR2 words |
| norender, nonmi | the same checks see the other result (backdrop only, counter 0) |
| tone | 4,632 samples, min -1,331,840 max 3,822,976 (24 bit): pulse 1 audible; without it exactly 0 |
| cpu | 228 instruction results equal plain arithmetic; a wrong expectation fails the bench (negative control) |
| win | window read back, last bytes, out of range drops, page register, ID/BUILD, INES bytes and the flags word (mapper 1 from byte 6 = 0x10) |
| pause | **B halted 12.4 ms after the request** (the pause waits for vblank and an instruction boundary: up to one frame); 5 ms held: no dot, CPU cycle, frame, request address or DDR2 write; after release the machine matches a free one frame by frame (357,368 requests hashed per frame) - **shifted by exactly one master tick**: nes.v skips the first enable after a pause to realign with its shadow clock (frame 2 starts at live tick 357,101 against 357,100). Not a cycle-exact resume; invisible except to a game that counts master cycles across a pause. |
| mutation (`-DNES_MUT_NOHOLD`) | the pause request never reaches the machine: "B halted within 40 ms" fails, and so do the freeze checks |
| `run_l2.sh` | ID `NES_`, leaves reset, Freeze halts (waits up to 40 ms) and stops the DDR2 writes, release runs on |

**Findings.** (1) The PPU ignores its registers until it has warmed up (about one frame); a program that writes at once draws
nothing, exactly like the real chip - `mkcart.py` now waits two vblanks, as software does. (2) During a pause the machine's `cycle`
and `scanline` outputs follow its shadow clock and keep running (video must not stop); only the machine's own state stands still.
(3) A bench that passes a PPM path to the simulator must give an absolute one (it runs in `build/`); the first pictures I looked at
were a stale file of the previous core.

### 5.9 M3: the cartridge behind the DDR2 (2026-10-15, local)

**Budget on paper, before the build.** What the machine asks: the CPU one bus cycle per 558 ns (a PRG byte per cycle, opcode and
operand fetches mostly sequential), the PPU one CHR byte per 186 ns while rendering (a background tile is one 16 byte line,
fetched twice per 1.49 us, and every sprite on a line is one line); nametable and OAM are internal. What the host's port gives:
`host/ddr2_port.v` says a read through the arbiter takes about 0.4 us (ddr2_lean's own 110 ns plus the queue, ui_clk 75 MHz) and
the cartridge client `c_*` measured 546 ns idle and 706 ns with the worst video load (`sim/lat`); one command moves 128 bit = 16
bytes. Our engine adds two clock crossings (cpu_clk -> ui_clk 2-3 ui clocks, back 2-3 cpu clocks) and six clocks of fill: about
150 ns. So a miss costs 0.35 to 0.85 us - **more than one CPU cycle and four PPU fetches**, which settles the design: the DDR2 cannot
serve the machine cycle by cycle, only through a cache that hits almost always and a machine that **waits** on the miss. The
wait is exact: the whole machine's clock enable stops (a8_mem.vhd does the same for the Atari), so nothing is out of step and
the machine only runs slower while it waits. The cost is the miss rate times the miss time; a direct mapped 4 KB PRG cache and
8 KB CHR cache hold a loop's code, a screen's tiles and a few banks. BRAM: PRG cache 1 RAMB36 + tags, CHR cache 2 + tags, CHR RAM
2, PRG RAM 2, work RAM 1, nametable 1 = 9 RAMB36 and 3 RAMB18 for the machine (Yosys `nes_mem`: 7 RAMB36 + 4 RAMB18), the frame
writer 2: about 12 of the 50 tiles, against 10 (mapper 0 fitted) before and the 50 that a 512 KB + 256 KB image would need.

**What is built** (`nes_mem.v`, `nes_win.v`, 5.8 stage one replaced): PRG ROM and CHR ROM live in the DDR2 at `CART_BASE` (register
0D0, default 0x01000000, below the frame buffers at 0x01D00000): PRG at +0, CHR at +0x200000, the image as the iNES file holds it.
Two direct mapped caches, 16 byte lines, write through, keyed by the *linear* address the mapper produced; the PRG index is
`line[7:0] ^ page bits`, so a fixed bank and a switched one do not fight for lines. One engine serves misses and the window
(a byte write is a 128 bit write with one lane enabled; a byte read a line read), the `c_*` side is the circuit of `a8_mem.vhd`
(toggle requests, a 3-flip-flop crossing, one command open at a time). The window is now the only way to put an image in (or the
host DMAs it): `dma_done_tgl` comes when the DDR2 is done, so a byte costs about a microsecond; a 128 KB image about 0.15 s.
Counters: `MISS_PRG`, `MISS_CHR`, `STALL` (registers 0B0-0BB).

**The cache invalidation question, answered differently than asked.** The brief asked for a bench that a cache not invalidated on a
mapper write fails. Because the key is the linear address, a bank switch *needs* no invalidation: another bank is another
line, and nothing stale can be served. What does change the content behind a line is the app writing the image, so that is
what is invalidated (the line a window write hits). The two negative controls are therefore: `-DNES_MUT_NOINV` makes the cache
a cache that knows nothing of banks (index and tag from the CPU-visible window offset only: what a cache keyed by CPU address
would be without invalidation) and the bank benches fail; `-DNES_MUT_NOWINV` leaves the line cached on a window write and the `inv`
bench fails. Both fail as they must (`run.sh`, below), and `-DNES_MUT_NOSTALL` (a miss does not hold the machine) fails the
instruction bench, which shows the wait is needed at this latency.

**Benches** (model of the port: 293-693 ns per read, `c_rdy` low a quarter of the clocks; the images are of ours; `banks2`,
`banks3`, `inv` load the model by hierarchy because a 128 KB image through a microsecond window takes seconds of simulation, the
window itself is tested by `win`, `frame`, `inv`):

| bench | what | result |
|---|---|---|
| frame | picture, NMI, sound, as in 5.8, now through the DDR2 | pass; 11 PRG + 4 CHR line fetches, machine held 956 clocks (0.0 %) |
| cpu | 228 instruction results | pass; 135 PRG fetches, held 9,363 of 973,078 clocks = 0.96 % |
| banks2 | UNROM (mapper 2): 8 banks x 16 KB, 4 switch patterns, same CPU addresses read in each bank, 24-round ping-pong | pass, 45 results; mutant noinv fails |
| banks3 | CNROM (mapper 3): 4 x 8 KB CHR banks read through $2007 | pass, 17 results; mutant noinv fails |
| inv | a byte of the image changed through the window under the running loop ($11, $22, $33 seen) | pass; mutant nowinv fails |
| win | window: every address of the 2 MB space, page register, CART_BASE, one lane of a 16 byte write | pass |

The first stall numbers (0.96 % on a program that runs from ROM, 4.3 % on the bank bench that switches banks every few
instructions, nothing measurable on the picture) are from programs of ours, not games; the counters are there for the board.
The ranking that matters for the Freeze and for T93: a miss never delays the machine by more than one DDR2 round trip.

**Not done, deliberately:** CHR RAM is 8 KB in block RAM (a 16/32 KB CHR RAM mapper reads aliased), battery RAM and PRG RAM beyond
8 KB, a second way or prefetch (a hot loop whose code and data share an index would thrash: the XOR index makes that unlikely
for banked mappers, it is measured nowhere), reading the window from the JTAG (the peek gets the last window read).

### 5.10 M4: which mappers, and what they cost (Yosys, 2026-10-15, local; `tools/nes_area.sh`)

Method as in 5.4 and 5.6: Yosys `synth_xilinx -family xc7`, one module at a time, flattened, I/O buffers not counted, from `cores/nes/gen`
(so with the clock enable and with the pull value where the machine had Z). It is not Vivado and it counts a mapper alone: in `cart_top`
the outputs of a mapper that nothing reads are gone, which is why the whole is smaller than the sum.

**Built (D14), 1,496 LUT / 461 FF as separate modules; `cart_top` with all ten and its OR/AND multiplexer, flattened: 1,163 LUT / 461 FF -
under the 2,000 LUT the brief set, 837 LUT of headroom.**

| module | mappers | LUT | FF | bench |
|---|---|---|---|---|
| `Mapper28` (Tepples) | 0, 2, 3, 7, 28, 94, 97, 180, 185 (NROM, UNROM, CNROM, AxROM, ...) | 145 | 23 | `banks2` (2), `banks3` (3) |
| `MMC1` | 1, 155, 171 | 90 | 25 | `mmc1` |
| `MMC2` | 9 | 78 | 27 | `mmc2` (PRG banks, both CHR latches) |
| `MMC4` | 10 | 83 | 27 | `mmc4` |
| `MMC3` | 4, 33, 37, 45, 47, 48, 52, 74, 76, 80, 82, 88, 95, 112, 118, 119, 154, 189, 191, 192, 194-196, 205-208, 268 | 592 | 206 | `mmc3` (R6, R7, R0, R2; no IRQ, no mirroring) |
| `VRC24` | 21, 22, 23, 25, 27 | 175 | 114 | `vrc24` (as mapper 23; no IRQ) |
| `Mapper66` | 11, 38, 46, 66, 86, 87, 101, 140, 144, 145, 149 | 125 | 12 | `mmc66` (as 66) |
| `Mapper34` | 34 (BNROM; NINA-001) | 75 | 14 | `mmc34` (BNROM) |
| `Mapper71` | 71, 232 | 60 | 5 | `mmc71` |
| `Mapper79` | 79, 113, 133, 146, 148 | 73 | 8 | `mmc79` (as 79) |

Every bench is a cartridge of ours that switches banks and reads bytes which are a function of the bank (the pattern differs for every
8 KB PRG and 4 KB CHR bank of up to 2 MB, so an aliased bank is seen), with the DDR2 model's latency; the expected bytes are computed in
Python from the mapper's documented register layout, not from the module. Mirroring, the MMC3 and VRC IRQ counters, PRG RAM,
the bus-conflict corner cases and the mapper numbers other than the one named are **not** exercised. The benches found one real
defect in my glue (not in the machine): `nes_mem` re-sampled the address during a read, so a mapper that flips its banks in answer to
the read itself (MMC2/4 and tile `$FD`/`$FE`) returned the byte of the new bank; the address is now taken when an access begins, as
MiSTer's SDRAM does (D13), and `-DNES_MUT_NOADDRHOLD` is the control. Two of my own mistakes were caught too: a header that said 64 KB
for a 128 KB image, and a byte pattern that repeated every 64 KB (so two banks that far apart looked alike).

**Not built, with what each would cost** (same method, mapper alone; "ext. audio" needs the machine's `ext_audio` and the sound
modules, which are not built):

| module | mappers | LUT | FF | note |
|---|---|---|---|---|
| `Rambo1` | 64, 158 | 150 | 122 | Tengen MMC3 |
| `VRC1`, `VRC3` | 75, 73 | 68, 88 | 24, 41 | |
| `VRC6` + `vrc6sound` | 24, 26 | 133 + 102 | 106 + 117 | ext. audio |
| `VRC7` | 85 | 102 + (OPLL, not readable by Yosys) | 85 | ext. audio |
| `Mapper69` + `SS5b_audio` | 69 (FME-7) | 124 + 151 | 111 + 161 | ext. audio |
| `Mapper67`, `Mapper68` | 67, 68 | 123, 76 | 60, 50 | |
| `N163` + `namco163_sound` | 19, 210 | 192 + (not readable) | 135 | ext. audio |
| `Mapper13`, `30`, `77`, `78`, `89`, `107` | 13, 30, 77, 70/78/152/81, 89/93/184, 107 | 49, 82, 53, 79, 75, 55 | 2-12 | |
| `Sachen8259`, `SachenJV001`, `SachenNROM` | 137-150, 136-173, 143 | 134, 109, 48 | 26, 25, 0 | |
| `JYCompany` + `multiplier` | 90, 209, 211, 35 | 390 + 38 | 263 + 39 | |
| `MapperFDS` + `fds_audio` | 20 | 432 + 605 (3 DSP) | 155 + 183 | disk system |
| `MMC5` and everything in `misc.sv` | 5; 15, 16, 18, 32, 41, 42, 65, 72, 83, 91, ... | not measured | | Yosys cannot read them (5.6) |

Adding one is a line in `tools/nes_gen.py` (`BUILT`: the instance name of `cart.sv` and the module), a run of the generator, and a bench.

**The rest of the core, same method** (`tools/nes_area.sh`): the 6502 `T65_nes` 817 LUT / 168 FF; the APU's parts (a Yosys run of the whole
APU takes minutes and 3 GB): 2 x `SquareChan` 129, `TriangleChan` 65, `NoiseChan` 80, `DmcChan` 149, `FrameCtr` 64, `APUMixer` 237 =
**853 LUT**; our glue: `nes_mem` 392 LUT / 682 FF / 7 RAMB36 + 4 RAMB18, `nes_pixw` 235 / 604 / 2 RAMB36, `nes_regs` 613 / 263,
`nes_flags` 251, `nes_audio` 57 / 75, `nes_win` 5 / 72, `nes_pal` 16 = **1,569 LUT**, 9 RAMB36 + 4 RAMB18 (about 11 tiles).
**The PPU has no honest number.** Yosys reads `ppu.sv` only after two constructs are patched in a scratch copy (the enum initialiser at line
543 of `gen/ppu.sv`, and the unpacked array literals `'{64{8'hFF}}` and `'{a, b, c, d}`), and then, with the composite outputs left open, reports
**6,670 LUT / 3,128 FF with no RAM cell at all**: it dissolves the OAM, the secondary OAM and the palette into flip-flops and multiplexers
(about 2,500 FF are those arrays), which Vivado maps to distributed RAM. So 6,670 is an upper bound and the real figure is somewhere
between roughly 4,000 and that; the first Vivado run is the measurement (S10).
**Total, honestly:** 817 + 853 + 1,163 + 1,569 + (4,000 to 6,670) + the machine's glue (`nes.v`: DMA controller, clock generators, a few
hundred) = **about 8,600 to 11,300 LUT of the partition's 18,000 (48 to 63 %)** and about 11-15 of its 50 block RAM tiles; for comparison the
first build was estimated at 3,200 LUT / 13 tiles (5.4) and the Atari core is 7,020 / 29.5.

### 5.11 M5: the answer to run 39, the PAL register, and what no simulation can say (2026-10-15, local)

**Run 39** (5.5): the worst path was the window's registers into the cartridge block RAM's address pins, 90 % routing, through the host's merge
network (the static part is far from layer 2). What the new core does about it, in the order it matters: (1) `nes_win.v` registers everything that
comes back from the host's merged port (`p_*` -> `s_*`) before any use, and nothing of it reaches a RAM address pin any more - the
cartridge is not in block RAM, the requests go to the engine of `nes_mem`; (2) the machine's own paths are declared what they are - four clocks
between two flip-flops of the clock-enabled machine, `cores/nes/nes.xdc` (D15) - instead of being pipelined by hand; (3) every signal that crosses
between the machine and our glue is timed for the strobe that carries it (`cap`, three clocks after a tick, the enable-registered inputs),
which is a design rule of `nes_core.v` and `nes_mem.v` and not only a constraint. The CE net `ce_r` (thousands of sinks, one clock) is
`MAX_FANOUT 200`.

**What the Vivado run will decide, and I cannot:** (a) whether the constraints are applied. `nes.xdc` finds the machine by the cell names
`nes/m/*` and is read with `used_in_synthesis false` so that it is stored in the checkpoint for the implementation; if the names do not
match after flattening, nothing is relaxed and the machine's paths show as failing in `timing.rpt` (the log line `=== T95: nes.xdc read` says it
was read, nothing says it matched). (b) whether the machine closes in four clocks: it was written for a 46.5 ns master clock and gets 40 or 50 ns
here (the enable's spacing is 4 or 5 clocks) on a device and speed grade that are not Cyclone V's; 6502 decode, the PPU's sprite evaluation and
the mapper address arithmetic are the long paths, and T65 alone was 848 LUT in Vivado in the Atari core. If it does not close, the options
are a machine enable that never ticks closer than five clocks (a 20 MHz master, which is 7 % slow), or two enable phases; both are a change of
`nes_core.v` and not of the machine. (c) the PPU's area (5.10). (d) the block RAM inference of the cache arrays and of the 8 KB RAMs (written in
the template of `nes_cart.v` of the first build, which routed in run 39). (e) that Vivado takes the SystemVerilog of `ppu.sv` / `cart.sv` after
the generator's `wire` -> `logic` changes (Alex's information, 5.6; Verilator's lint is clean).

**PAL.** Register `SYSTEM` (0A1, set while the reset is held): 1 = PAL changes the NCO to 21.2813696 MHz and the machine's `sys_type`; bench `pal`
(the frame bench on a PAL console): frame period **1,999,721 clocks = 50.00 Hz**, the picture and NMI as on NTSC except that the PAL PPU
blacks the edge pixels (checked), and it fails on an NTSC register (the period check). Dendy (2) runs the NTSC clock.

**What stands between this and the board**, none of it by omission: Vivado (above); the application window and the way an image gets into
the DDR2 and the INES registers (T93; the contract is 5.8/5.9: header bytes, `CART_BASE`, `WIN_PAGE`, `SYSTEM`, reset hold, then release);
the pad (section 4); a game on the board (S11).

### 5.12 Left out, and the questions

- **Left out on purpose (section 4):** the pad, the mappers that are not built (5.10), expansion audio, savestates, battery RAM, PRG RAM beyond
  8 KB, CHR RAM beyond 8 KB, Vs. System, the OSD options; the application.
- **Known limits of what is built:** a miss costs the machine 0.35-0.85 us of its own time, so a game that thrashes the cache runs slow
  (counted in `STALL`; the benches' programs lose 0 to 4 %, no game was measured); sound pitch follows the machine's clock, so it dips while the
  machine waits; the Freeze takes up to a frame to engage and resumes one master tick late; the JTAG peek of the cartridge window returns the last
  window read; an image written through the window costs about a microsecond a byte; the converted 6502 is tested by program
  (S6), not compared with the VHDL's own simulation.
- **Not exercised by any bench:** nametable mirroring modes, the MMC3 and VRC4 IRQ counters, PRG RAM at `$6000`, CHR RAM (the PPU's writes), the
  bus-conflict corner cases, sprite 0 hit and the sprite overflow bug, the DMC channel's DMA stealing, controller reads.
- **Questions for Alex:** Q2 and Q3 of section 4. One more: the benches load their 128 KB images by hierarchy (a microsecond per byte through
  the window); should the host get a faster way to put an image into the DDR2 (the host's `cart_fill` writes the DDR2 for the C64's cartridge
  port) for T93, or is a microsecond a byte fine for a 512 KB game (half a second)?

### 5.13 CI run 40: the first Vivado run of the MiSTer core, and what it caught (2026-10-15 05:17-05:24 UTC)

The run is green in 7m30 (the core is `experimental`), the benches ran in CI as well (`sims` 4m25, after the package - T100 D7),
and the core stage did exactly what it should: `core cache miss nes 7f5dcea2432ffcfb: implementing the partial against host
0e8fb06e58872c33`, then

```
=== T95: nes.xdc read (multicycle paths of the clock-enabled machine, implementation only)
ERROR: [Synth 8-36] 'SSREG_INDEX_APU_TOP'  is not declared [cores/nes/gen/nes.v:532]
ERROR: [Synth 8-36] 'SSREG_INDEX_APU_DMC1' is not declared [cores/nes/gen/nes.v:533]
ERROR: [Synth 8-36] 'SSREG_INDEX_APU_DMC2' is not declared [cores/nes/gen/nes.v:534]
ERROR: [Synth 8-36] 'SSREG_INDEX_APU_FCT'  is not declared [cores/nes/gen/nes.v:535]
ERROR: [Synth 8-36] 'SSREG_DEFAULT_TOP'    is not declared [cores/nes/gen/nes.v:887]
ERROR: [Synth 8-439] module 'layer2_nes' not found
core nes: no timing report (the script stopped early)
```

**The cause, and why no bench could have found it.** The savestate index and default constants are in the package
`regs_savestates`. Upstream, only `ppu.sv` imports it; `nes.v` and `apu.sv` use the names and rely on the global import in
`NES.sv`, MiSTer's top level, which we do not vendor. Quartus and Verilator accept that - one compilation unit, the names are
unique - so all 20 benches passed over it. Vivado does not: a wildcard import is file-scoped there. `tools/nes_gen.py` now adds
`import regs_savestates::*;` to every generated file that uses the names and does not import the package (2 files, counted as
`package import added`), and **fails** if one is left without it, so the next upstream file that reaches for a package name
cannot pass lint. Benches after the fix: all 20 green, all six mutants and the wrong-expectation control still fail as they must.

This is the first thing in this core that only a real synthesis could say, which is why D15 (area, timing, the multicycle cells)
stays open until a run gets past elaboration - what run 40 proves is the toolchain's reading of the sources, nothing about area.

### 5.14 CI run 41, and the two things Verilator cannot see (2026-10-15 05:28-05:32 UTC)

Run 41 is green again (7m30, `experimental`), and the core died a little later than in run 40 - this is what the second real
synthesis said:

```
core nes 2674eb217b49adf0 ... implementing the partial
ERROR: [Synth 8-2577] procedural assignment to a non-register Savestate_OAMReadData is not
       permitted, left-hand side should be reg/integer/time/genvar   [gen/ppu.sv:627]
ERROR: [Synth 8-439] module 'layer2_nes' not found
WARNING: [Synth 8-8895] 'oam_bus' is already implicitly declared on line 1594  [gen/ppu.sv:1620]
         ... the same for sprite_overflow, obj0_on_line, vram_r_ppudata, vram_w_ppudata,
             vram_din, color0 and ppu_dbus
core nes FAILED (nothing cached for it)
```

**The error: the generator's own pass had a blind spot.** `proc_wires` already retypes a `wire` or an output port that a process
drives (`wire -> logic`), but its test for "a process drives this" was anchored at the start of a line, so
`if (Savestate_OAMRdEn) Savestate_OAMReadData <= oam[...]` (upstream `ppu.sv:619`) did not look like an assignment to it. The test
is now `proc_driven()`: a non-blocking `<=` anywhere, or a blocking `=` at the start of a statement. Two more signals were caught by
the wider test (22 -> 24).

**The warnings are worse than the error, and they are the reason this run was worth more than a timing number.** Vivado creates an
implicit net *at the point of use*: a bus used in a continuous assign or an instance's port connection **above** its own
declaration becomes a 1-bit net there, and the real declaration below is then "already implicitly declared". That is a warning, so
a run that got past the error would have produced a bitstream in which eight signals of the PPU - among them `vram_din[7:0]`,
`ppu_dbus[7:0]` and `oam_bus[7:0]` - are one bit wide at the places they are read. Quartus and Verilator read a module's scope as
a whole and never showed it; all 20 benches pass either way. It would have reached the board as a picture that is wrong in a way
no bench of ours describes.

`tools/nes_gen.py` now has `hoist_implicit()`: for every module it finds the names used in an assign or a port connection, and
moves any declaration that sits below its first such use up to just under the module header - **the whole declaration, initialiser
and all**. The first version split `reg [1:0] div_sys = 2'd0;` into a declaration and an `assign`, which gave a signal its process
also drives a second driver: the `pause` bench failed at once (`B ticked no live clock while held`), which is what that bench is
for. 207 declarations are hoisted, counted as `declarations hoisted`, and the generator **raises** if one is left - the same
treatment as the package import of 5.13.

Benches after both fixes: 20 green, all six mutants and the wrong-expectation control still fail as they must, `nes_gen --check`
says `gen is current`, and a scan of every generated module finds **0** names still used in an implicit-net context above their
declaration.

### 5.15 CI run 42: the core synthesises, and the constraints were never applied (2026-10-15 05:46-07:50 UTC)

Run 42 (`165bb84`) got the NES past elaboration and synthesis for the first time. The out-of-context result, from
`build_core_nes/utilization_nes_synth.rpt`:

| | synthesised | of the device |
|---|---|---|
| Slice LUTs | **12,139** | 37.2 % of 32,600 |
| Slice Registers | 5,933 | 9.1 % of 65,200 |
| Block RAM Tile | 11 | 14.7 % of 75 |
| DSP | 0 | - |

That is **above** the estimate of 5.11 (8,600-11,300 LUT): the Yosys numbers per block do not add up to what Vivado
infers for the whole machine. The number the pipeline prints did not include this line, because an out-of-context
report writes `Slice LUTs*` with the footnote marker and `core_sources.tcl` matched `Slice LUTs` followed by a space.
The regexp now accepts the asterisk - the one line that carries the core's size was the one line the run did not print.

**The defect of this run is mine, and it is in the constraints.** Vivado's XDC parser accepts only constraint commands:

```
CRITICAL WARNING: [Designutils 20-1307] Command 'concat' is not supported in the xdc constraint file. [cores/nes/nes.xdc:21]
CRITICAL WARNING: [Designutils 20-1307] Command 'puts' is not supported in the xdc constraint file.   [cores/nes/nes.xdc:23]
CRITICAL WARNING: [Designutils 20-1307] Command 'if' is not supported in the xdc constraint file.     [cores/nes/nes.xdc:24]
CRITICAL WARNING: [Designutils 20-1307] Command 'if' is not supported in the xdc constraint file.     [cores/nes/nes.xdc:27]
```

Every `set_multicycle_path` of 5.9 sits inside one of those `if` blocks, so **not one of them was applied**. The machine,
built for a 21.477 MHz clock enable and relaxed to 4 clocks on paper, was timed at one 10 ns clock. What that cost,
measured in the same log:

| stage | WNS | TNS |
|---|---|---|
| after placement | **-8.563 ns** | -19,710 ns |
| route, global iteration 3 | -12.885 ns | -31,737 ns |
| route, global iteration 4 | -19.731 ns | -50,451 ns |

The router's overlaps tell the same story: 12,211 -> 952 in iteration 4, then **back up to 12,825** in iteration 5. It was
still routing after 2 h 01 and would not have closed. The run's own warning line (`nes.xdc matched no machine cell`) could
not fire either - it is a `puts` inside an `if`, and both were dropped.

**The fix** (`cores/nes/nes_timing.tcl`, sourced by `core_sources.tcl` after `synth_design`): the same commands as a Tcl
script, not an XDC file. There the cells exist, `if` and `puts` run, and the constraints go into the checkpoint, which the
core stage reads back scoped to the cell (`read_checkpoint -cell l2`). `ci/lint.sh` gained the check `xdc without Tcl`:
no tracked `.xdc` may hold a control command, and its negative control (a file with an `if`) has to be caught - it is, and
the whole scan is the only thing between a dropped constraint and a two-hour route that proves nothing.

Also seen, and not ours to fix from here: `WARNING: [Place 30-640] Pblock pb_cpu has 2099 LUT as Logic(s) assigned to it,
but only 1800 LUT as Logic(s) are available` - the host's RISC-V pblock (`host/build.tcl:246`), pinned, reported here so it
is on record.

Still open after this run: the routed area in `pb_l2`, the real WNS with the constraints applied, and `pr_verify`.

### 5.16 Run 43: the constraints applied — and then did not, a second time (2026-10-15 08:02-08:16 UTC)

With `nes_timing.tcl` sourced after `synth_design`, the synthesis said exactly what 5.15 wanted:

```
=== T95 nes timing cells: machine 18111, glue 3424, tick-aligned 1282
=== T95 nes synth | Slice LUTs*                | 12152 |     0 |          0 |     32600 | 37.28 |
```

and the placement still landed at **WNS -8.504 ns**, then **-8.969 ns**. The reason is in the implementation, not the
synthesis. Vivado writes an out-of-context design's constraints back out as the **command text**, with the `get_cells`
expression inside it, and evaluates it a second time when the core stage reads the checkpoint:

```
Parsing XDC File [build_core_nes/layer2_nes_ooc/layer2_nes_late.xdc] for cell 'l2'
CRITICAL WARNING: [Vivado 12-4739] set_multicycle_path:No valid object(s) found for
                  '-from [get_cells -quiet -hierarchical -filter {NAME =~ nes/m/*}]'.      (six times)
```

In that second evaluation the scope is the cell, and `NAME` carries the **full** hierarchical name, which now begins with
the cell: `l2/nes/m/...`. The pattern `nes/m/*` matches none of them, `-quiet` makes the empty result silent, and the
`set_multicycle_path` applies to nothing. Both runs were timed as if the core had asked for nothing at all.

The fix is one character per pattern: every filter starts with `*` (`*nes/m/*`, `*nes/mem/*`, `*nes/pausecore_reg*` …),
which matches in both evaluations, because `*` also matches the empty prefix.

**What makes this a two-run mistake is that it was silent, so the silence is what got fixed.** `ci/bitstream_layers.sh`
now fails a core build whose log holds `[Vivado 12-4739]`, names the count and prints the first two lines. No core of ours
has such a line (checked in the `trx64` and `amiga` logs of run 41), so one is a defect, not a note. The negative control
is in `ci/test_layers_flow.sh`: the fake Vivado writes that warning on `FAKE_CORE_UNRESOLVED=1` while producing a good
WNS and both bitstreams, and the stage has to fail anyway — with the check removed, the case fails as it must.

Run 43 was cancelled at the second placement; `atari800` had routed before that and is in the builder's cache
(`core-atari800-dc4cdf7502c81596`), at **WNS +0.114 ns, 0 failing endpoints, 0 routing errors**, so the next run only
has the NES to route.

### 5.17 Run 44: the NES routes (2026-10-15 08:20-09:0x UTC)

With the patterns fixed, the second evaluation of the constraints resolved — **zero** `[Vivado 12-4739]` in the log,
against six in run 43 — and the core went through:

| | measured |
|---|---|
| WNS after placement | **+0.100 ns** |
| WNS after routing | **+0.106 ns**, 0 failing endpoints of 40,928 |
| Hold | +0.052 ns, 0 failing endpoints |
| `pr_verify` | passed |
| `pb_l2` Slice LUTs | **13,114 of 18,000 (72.9 %)** |
| `pb_l2` Slice Registers | 5,935 of 36,000 (16.5 %) |
| `pb_l2` Block RAM Tile | 11 of 50 (22.0 %) |

So the whole relaxation argument of 5.9 holds in silicon terms: the machine's paths are given four clocks, and what is
left over closes with 0.1 ns to spare. The core is the densest one in the partition so far (the Atari 800 XL routes at
7,020 LUT), and the 13,114 LUT is after implementation, above the 12,152 the synthesis reported.

One blemish, not a defect: the cell lists hand Vivado every primitive under `nes/m/*`, including LUTs, GND and VCC, and
a multicycle path has no start or end point on those. The log holds 100 `[Constraints 18-401]` and 100
`[Constraints 18-402]` (Vivado stops counting at 100 per message). The constraint still applies to the sequential cells,
which is what matters; narrowing the lists to flip-flops is a cleanup for the next change to that file, not a rebuild of
its own.

### 5.18 The controller: the C64's joystick and keyboard (2026-10-15, Verilator)

Alex asked how input should work and answered it himself: "B is good ... but would it not be good to map the C64 joystick + the
keyboard sensibly? F1 = Start, F3 = Select ... and so on", and on the one open point, the second button, "Space" (translated,
2026-10-15). "B" is the second of the two shapes I put to him:

- **A** the application writes the host's own joystick register (page 3, `0x30`/`0x31`, `host/ultimate_sys.vhd:1163`) and the core
  reads `c64_joy1`: no core change, but five bits per port, so three of the NES's eight buttons have nowhere to go.
- **B** the core reads the contract lines itself. `c64_joy1/2` (5 bits each) and `c64_keys` (64) are in
  `contract/layer2_ports.txt`, so this needs **no host change, no contract version, no application** and the keys arrive in the
  clock they are pressed instead of a service tick later.

A USB gamepad is not an option today and that is a measurement, not an opinion: both applications link Gideon's HID stack
(`usb_hid.cc`, `hid_decoder.h`), but `UsbHidDriver::test_driver` takes interface class 3 and the driver then looks for
`locateMouseFields` / `locateKeyboardFields` only - there is no gamepad locator (usage page 0x09 buttons, hat switch 0x39) and no
joystick report path. Decoding a pad report is new code; our fork `host-app/software/io/usb/` is where it would go, the submodule
`apps/ultimate` stays untouched. Equally measured: the POT lines that carry Gideon's fire 2 and fire 3
(`joystick_output.cc`, `C64_PADDLE_1_X/Y`) are **not** at the partition edge, so a two-button stick cannot help either.

**The mapping** (`cores/nes/trx/nes_pad.v`). Matrix indices are `keys[col*8 + row]` (`host/c64u_keyboard.v`), the key names of
each group from `host-app/software/io/c64/keyboard_c64.cc` (`keymap_normal`); joystick bits are up, down, left, right, fire
(`host/top.v:67`, active high after `app_input`).

| NES | port 1 | port 2 |
|---|---|---|
| Up / Down / Left / Right | `c64_joy1[0..3]` | `c64_joy2[0..3]` |
| A | `c64_joy1[4]` (fire) | `c64_joy2[4]` |
| B | space (`keys[60]`) | right shift (`keys[52]`) |
| Select | F3 (`keys[5]`) | F7 (`keys[3]`) |
| Start | F1 (`keys[4]`) | F5 (`keys[6]`) |

Nothing of this collides with the application: its menu opens on the hardware menu button or F10 / ScrollLock of a USB keyboard
(`coreapp/src/run.cc:248`), and while the menu is up the machine is frozen anyway. The host ORs the physical lines with the ones
software may drive (`host/app_input.v`), so a pad the application decodes later arrives through the same two inputs.

**The protocol** is the hardware's, and `NES_MiSTer`'s `NES.sv` is the reference for how `nes.v` wants to be driven:
`joypad_out[0]` (the write to `$4016`) latches the buttons **while it is high**, a read of `$4016`/`$4017` raises
`joypad_clock[0]`/`[1]` and the **falling** edge shifts. Bit 0 leaves first and the order is A, B, Select, Start, Up, Down, Left,
Right. The register is 24 bits with ones above the eight buttons, as in `NES.sv`: on an NES (not a Famicom) the ninth and later
reads of a standard controller give 1. Two flip-flops bring each line from `sys_clk` into `cpu_clk`.

**The bench** (`sh cores/nes/sim/run.sh pad`, 1.5 s after the build): a cartridge program of our own (`mkcart.py build_pad`)
strobes and reads both ports the way a game does - `LDA $4016 / LSR A / ROL $10`, eight times - and stores one byte per port per
phase. It names the phase in `$0300`; the bench presses the lines of that phase (`tb_nes.v`, `+PAD`). Three phases: joystick 1 up
and fire with F1; joystick 1 down and left with the port-1 keys **while** joystick 2 holds right and fire with the port-2 keys, so
a key that leaked into the other port would show; nothing pressed. The expected bytes come from the table above written out in
Python, not from the gateware. Result:

```
== pad
ok   the program ran to its end marker
ok   every result matches what the program must compute
     8 results compared, 0 differ
PASS tb_nes check
== mutation padorder: A and B of port 1 swapped; the pad bench must fail
ok   the padorder mutant failed the pad bench
```

The negative control is `NES_MUT_PADORDER` in `nes_pad.v` (A and B of port 1 swapped); `run.sh` requires it to fail the bench.

What the bench taught, and the hardware does the same: a strobe latches **both** ports, so a ninth read of port 1 taken *after*
port 2 has been read gives A again, not 1. The bench therefore takes each ninth read before anything strobes again. The first run
failed exactly there (`FAIL result at $0406: got 00 expected 01`) - the expectation was wrong, not the core.

Still out: a USB gamepad, keys for the directions, a four-player adapter, the Zapper and the Power Pad, and a mapping the user can
change. The core stays `experimental` until it has run on the board.

### 5.19 Run 46: the pad in silicon (2026-10-15)

The pad is 32 LUT and 80 flip-flops of synthesis (`12,184` and `6,015` against run 44's `12,152`
and `5,935`), and the core got *better*, not worse: **WNS +0.114 ns** after routing (run 44:
+0.106), hold +0.048, `pr_verify passed`, partial 1,319,290 bytes, and no `[Vivado 12-4739]` in the
log - the constraint patterns still match with the two new flip-flop stages in the design. The six
other cores came from the cache untouched, as they must: nothing outside `cores/nes/` was touched.

### 5.20 The NES bundle had the same defect as the Atari (2026-10-15)

Both blueprint cores ship the same application image, so `nes.cor` of `trx64mc-main-51e89821` carries
the same twelve-byte header on its entry point and cannot start either - it was never tried on the
board, the Atari failed first. Cause, fix and guard: T93 5.12. The repacked bundle (1,949,892 bytes,
with the rocker fix of T93 5.13) is on the stick, the released one is still broken.

Alex's game for the first board test, `sams-journey-demo-1.0.0-ntsc.nes`, is on the stick in
`/Usb1/nes/` (524,304 bytes). It is NES 2.0, mapper 4 (MMC3), 512 KB PRG, CHR RAM, battery save:
mapper 4 is in `supported[]` (`coreapp/cores/ines.cc:9-12`), the NES 2.0 branch of the flag word reads
it (`ines.cc:34-47`), and 512 KB fits the 2 MB PRG window (`NES_PRG_MAX`). The battery save has nowhere
to go - the core keeps no save RAM.
