# T116 — A ZX Spectrum Next core on the C64 Ultimate board: the original FPGA source, its RAM on DDR2, its SD card served from an image

**Status:** BUILT, ROUTED, MERGED, ON THE BOARD (2026-11-15 sync; the first sentence of 2026-10-22 said "not routed, not on a board" and is outdated: `zxnext.cor` ran on the board, Alex 2026-10-28, D11 Auto 2026-11-04). Original text: BUILT on branch `t116-build` (2026-10-22): §5.1 says what exists and what the benches measured, §5.2 where it differs from this spec, §5.3 what is open. Before: DECIDED (Q1-Q10 answered), PROPOSED. The answers stand under each question at the end of §3. 2026-11-13 status check: merged to main (Merge t116-auto etc.), ships zxnext.cor; board S13 passed 2026-10-28 and D11 Auto passed 2026-11-04 (docs/archive/board-tests/board-tests-2026-11-04.md 1.6); open: the two decisions of 5.3 (frame lock, S6 beyond Layer 2).
**Repo:** TRX64U-MultiCore — a future `cores/zxnext/` (vendored source, glue, benches, `core.toml`), the core's application descriptor (T104), tools. Sources are read from outside; nothing third-party is committed by this spec.
**Number:** T116 (registry: the table in `TODO.md`; T115 was the highest in use on 2026-10-22).
**Origin:** Alex, 2026-10-22: "Please ... it would be quite 'funny' if we ran the brand-new ZX Next on the C64U, haha" (translated).
**Depends on:** T79.1 (contract, version 2), T104 (generic application), T115 (`.cfw`, if the monolith is chosen), T91 (Z80 core: the Spectrum precedent), T95 (DDR2 behind line caches), T113 (measurement method). `pb_l2` must stay as measured.
**Milestone:** M3.

Evidence tags: **(source)** read in a file at `file:line`; **(measured)** a report line or a run, command in §5; **(computed)** arithmetic on stated numbers; **(estimate)** derived, not measured; **(hardware fact)** published machine documentation; **(quoted)** taken from another spec of this repository, not re-measured here.
Sources read 2026-10-22 from shallow clones on the builder (`~/t116`, outside any repository): original `gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA` at `b047011` (2025-11-06, "CORE VERSION 3.02.03 (Issue 2 untested)"), MiSTer port `MiSTer-devel/ZXNext_MISTer` at `7daebd1` (2026-09-28).

---

## §1 The problem

**Who:** Alex, who wants a ZX Spectrum Next as one more core on the C64 Ultimate board (and finds it funny).

**What he wants:** pick "ZX Spectrum Next" in the core selector, the board starts it, NextZXOS boots from an SD image on the stick, and he sees the picture (ULA, Layer 2, sprites, tilemap, Copper), hears it (three AY, DACs, beeper) and drives it with the C64U's keyboard and joysticks.

**What the Next is (source, hardware fact):** a Z80N (Z80 plus extra instructions) selectable at 3.5 / 7 / 14 / 28 MHz, DMA, Copper, 768 KB (1792 KB expanded) RAM in 16 KB / 8 KB banks, ROMs and the SD card's NextZXOS, Layer 2 (256x192 8bpp, 320x256 8bpp, 640x256 4bpp), tilemap, 128 sprites, 512 colours (RGB333), three AY (TurboSound Next), soundrive DACs, UART, SPI SD, PS/2 keyboard and mouse, joystick ports (`orig/README.md` of the MiSTer port quotes the spec; the entity is `orig/cores/zxnext/src/zxnext.vhd:31-326`). The real hardware has external 16-bit asynchronous SRAM, 1 to 4 MB at 28 MHz, (IS61WV204816BLL-10BLI, 10 ns, `zxnext_top_issue5.vhd:45`) whose port in the entity has no wait or acknowledge: request, address and data in, data back (`zxnext.vhd` ports `o_RAM_A_*`, `i_RAM_A_DI`). Our board has no SRAM.

**What is there:**

| Candidate | What it is | Terms as written in its files |
|---|---|---|
| Original `ZX_Spectrum_Next_FPGA` (`O`) | `cores/zxnext/src`, 51,545 lines of VHDL/Verilog, Vivado projects for issue 4 (XC7A15T) and issue 5 (XC7A35T) in `synth-zxnext/` | `zxnext.vhd:1-22` header: "GNU General Public License ... version 3 of the License, or (at your option) any later version"; `COPYING` is GPL v3 |
| MiSTer `ZXNext_MISTer` (`M`) | `O`'s `zxnext.vhd` edited (`rtl/zxnext.vhd:181-191`: `o_RAM_A_REQ2`, `i_RAM_A_REQ_ALLOW`, `o_RAM_A_CYCLE`, `o_RAM_A_RFSH`, `i_CPU_WAIT`), `rtl/mister/zxnext_top.vhd`, `sdram.sv`, `ddram.sv`, `tzxplayer.vhd`, Quartus project | repository `LICENSE` is GPL v2 (first lines read); per-file headers of `M` not checked in this step |
| `M`'s `boot.vhd` | an SD image with NextZXOS (README: "from releases folder") | not GPL (task statement: "Next License"); **not verified here, no image was opened** |

The sources are the machine; the NextZXOS image and the ROMs the boot ROM loads from it are a separate, non-GPL thing and never go into this repository, a `.cor` or a `.cfw` (CLAUDE.md "Clean room and licences").

**Today:** nothing exists. Measured for the first time in §5: `zxnext` with the Z80N, DMA, Copper, ULA, Layer 2, sprites, tilemap, 3 AY and everything else but the memory is **7,835 LUT, 4,619 FF, 14.5 RAMB36 tiles, 0 DSP** (xc7a50t, Vivado 2024.1, out of context, synthesised only). `pb_l2` is 18,000 LUT and 50 tiles; a monolith (T115) 32,600 LUT and 75 tiles.

**Why it matters:** the Next is the first core that needs **more RAM than the board has in block RAM, at a rate of one access per CPU cycle**, plus a storage device (an SD card) that the machine drives itself. Everything the earlier cores did with ROMs from files (VC-20, Spectrum) does not carry over.

**Break the problem into parts, MECE** (§2 is checked against this list):

- **P1 Source and licence.** What is used, under which terms, pinned how.
- **P2 CPU memory.** The CPU's 1 to 4 MB RAM and ROM space on DDR2 instead of SRAM; wait states; the four CPU speeds.
- **P3 Video memory.** Layer 2 reads its own port (`o_RAM_B_*`) at pixel rate.
- **P4 The SD card.** The machine speaks SPI to a card; there is none; an image on the stick has to answer.
- **P5 Video.** 9-bit RGB at the 14 MHz dot clock, layers, resolutions, into the host's picture path.
- **P6 Audio.** Three AY, DACs, beeper, tape ear.
- **P7 Input.** Keyboard (the Next has extended keys), joysticks, mouse.
- **P8 Clocks, reset and Freeze.** 28/14/7 MHz and a CPU clock that is stretched for contention and speed; contract C1/C10.
- **P9 Boot, ROMs, NextZXOS.** What starts the machine and where its software comes from.
- **P10 Size and timing** against `pb_l2` and the monolith.
- **P11 Delivery shape and the contract edge.** `.cor` or `.cfw`; the 247 ports, identity, idle values.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the sources are the pinned ones (P1)
  Given cores/zxnext/rtl vendored from the original at a pinned commit by a script, with SHA256SUMS and each file's header kept
  When  ci/lint.sh runs the check
  Then  rtl/ matches SHA256SUMS byte for byte, every edit to a vendored file is a listed patch, and no ROM, boot.vhd or NextZXOS byte is in the tree
  And   a changed byte, a missing header and a file named *.rom/*.vhd image-like fail the check (a control that cannot fail proves nothing)
```
```gherkin
Scenario: S2 the core stays inside its budget as synthesised (P10)
  Given the glue (DDR2 port, SD, pixel writer, keys) and zxnext with the IP memories as BRAM / LUTRAM
  When  the core stage synthesises it out of context for xc7a50t
  Then  the hierarchical report stays under the budget Alex sets (Q1) and the line is quoted in §5
  And   a design whose IP memories are black boxes shows BRAM 10.5 instead of 14.5 and Vivado critical warnings (measured in §5): the check reads them
```
```gherkin
Scenario: S3 the CPU runs from DDR2 with the same result as from SRAM, at each speed the spec allows (P2)
  Given zxnext with a one-cycle SRAM model, and the same with the model of the host's DDR2 port (293-693 ns a read, quoted from T95) behind line caches and the wait input
  When  our own Z80N test program (assembled by a script in tools/) runs memory copies, DMA, bank switching (MMU slots, ports $7FFD/$1FFD/$DFFD, nextreg $50-$57) at 3.5 and 7 MHz (and at 14/28 MHz if Q2 allows)
  Then  the final RAM image and the sequence of port writes equal the SRAM run byte for byte
  And   a wait that is dropped (-DZXN_MUT_NOWAIT) fails; a cache that ignores a write to a cached line fails the self-modifying-code case
```
```gherkin
Scenario: S4 Layer 2 and the sprite/tile fetches get their bytes in time (P3)
  Given Layer 2 in 256x192, 320x256 and 640x256 mode with a known pattern in DDR2, behind the model of the host's port
  When  one frame runs
  Then  the frame the pixel writer produced equals the frame from the SRAM model, and the line prefetch never underruns (a counter in the bench)
  And   a prefetch that starts one line late (-DZXN_MUT_LATE) underruns and the bench goes red
```
```gherkin
Scenario: S5 an SD image on the stick answers the machine's SPI reads and writes (P4)
  Given a test image of ours (a few MB, FAT32, files of our own) on a model of the app's block service
  When  the boot ROM or our Z80N test program issues CMD0/8/55/41/58, CMD17 and CMD24 over the SPI port (nextreg/port $E7/$EB)
  Then  CMD17 returns the image's block with a correct token and CRC, CMD24 changes exactly that block (or is refused in a read-only setup, Q3), and a card-detect/presence reads as a card is there
  And   a wrong LBA, a missing token and a flipped block fail
```
```gherkin
Scenario: S6 the picture arrives in the host's pixel layer as the machine draws it (P5)
  Given a test program that draws ULA, Layer 2 (three modes), sprites, tilemap (40 and 80 columns) and a Copper palette change in one frame
  When  the core's pixel writer fills the frame buffer (RGB333 expanded to RGB565) for 50 Hz and 60 Hz
  Then  the frame hash equals the hash of the original's o_RGB stream expanded the same way, for every resolution Q5 allows
  And   a writer that drops every second 14 MHz sample (-DZXN_MUT_DECIM) fails the 640-wide case
```
```gherkin
Scenario: S7 the three AY, the DACs and the beeper are heard in the right mix (P6)
  Given a test program that plays a tone on each AY channel, on each DAC channel and on the beeper
  When  the audio path runs for 1 s
  Then  each source shows its frequency in the stream (FFT peak) and the left/right mix equals the original's o_AUDIO_L/R scaled to the host's format
  And   a muted AY (-DZXN_MUT_AYOFF) loses its peak
```
```gherkin
Scenario: S8 the C64U's keys and joysticks reach the matrix, the extended keys and the joystick ports (P7)
  Given the host's key and joystick inputs (C6)
  When  a bench presses each of the 40 matrix keys, the extended keys the table names, and each joystick direction/button
  Then  port $FE (rows) and the joystick ports read the expected bits, and a key held across a Freeze is not lost
  And   a table row off by one fails
```
```gherkin
Scenario: S9 the clock enables give the machine's rates, and Freeze stops it exactly (P8)
  Given the core on sys_clk with enables for 28/14/7 MHz and the CPU clock
  When  the bench counts enables per frame at 50 Hz and 60 Hz and holds Freeze for a while
  Then  the frame length is the host's frame within the accepted drift (Q6), and while held no CPU clock, no frame, no AY/DAC sample, no SPI clock and no DDR2 write occur; after release the run is cycle-identical to a never-paused run
  And   a core that keeps counting under Freeze fails
```
```gherkin
Scenario: S10 the boot ROM runs, finds the card and hands over to NextZXOS (P9)
  Given zxnext with its own boot ROM (in the source), the test image of S5 holding our own stub firmware
  When  the machine is released from reset
  Then  the boot ROM reads the stub from the SD model and starts it (a marker byte appears at an agreed address), and with no image it stops with its error screen
  And   an image with a flipped byte in the stub fails the boot
```
```gherkin
Scenario: S11 the 247 ports, the identity and the idle values meet the contract (P11)
  When  the lint stage checks layer2_zxnext.v against contract/layer2_ports.txt and its version
  Then  it passes, every unused output has its idle value, and the descriptor has an entry per setting the app shows
  And   a missing port, a wrong version and a constant output merged into GND (the l2_keep trap, T87) are caught
```
```gherkin
Scenario: S12 the routed core meets timing against the host (P10)
  Given the host and the core in the pipeline
  When  CI routes the core stage (or the monolith) with CPU speeds 28 MHz logic at sys_clk-derived enables
  Then  WNS >= 0 and pr_verify passes (for a .cor), the numbers are quoted in §5
  And   a deliberately slowed path (negative control in a branch) turns WNS negative
```
```gherkin
Scenario: S13 NextZXOS boots from the user's image on the board and a program runs (P9, by hand)
  Given the board with the core loaded and an SD image that Alex supplies on the stick
  When  Alex starts the core from the menu
  Then  the NextZXOS menu appears (photo), a keypress moves it, a .NEX or BASIC program runs with sound, and the image is still readable after Freeze/resume
  And   without the image the core says so on screen instead of showing a black picture
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1 | `tools/vendor_zxnext.sh` + check in `ci/lint.sh` | CI `lint` |
| S2 | P10 | OOC synthesis report of the core stage (§5 is the first measurement) | CI `synth` |
| S3 | P2 | `cores/zxnext/sim/run.sh mem_ref0-3 mem0-3` (SRAM model vs DDR2 model) | `ci/sims.txt` |
| S4 | P3 | `cores/zxnext/sim/run.sh video0-2` | `ci/sims.txt` |
| S5 | P4 | `cores/zxnext/sim/run.sh sd` | `ci/sims.txt` |
| S6 | P5 | `cores/zxnext/sim/run.sh video0-2` | `ci/sims.txt` |
| S7 | P6 | `cores/zxnext/sim/run.sh audio` | `ci/sims.txt` |
| S8 | P7 | `cores/zxnext/sim/run.sh keys` | `ci/sims.txt` |
| S9 | P8 | `cores/zxnext/sim/run.sh clk1-4 freeze` | `ci/sims.txt` |
| S10 | P9 | `cores/zxnext/sim/run.sh boot noimg` | `ci/sims.txt` |
| S11 | P11 | `ci/lint.sh` contract check | CI `lint` |
| S12 | P10 | Vivado report line of the core (or monolith) stage | CI `bitstream` |
| S13 | P9 | by hand on the board with a photo: the image is Alex's and may not be in CI | board session |

Nothing third-party is checked in except where Alex has allowed it in writing; the test programs and the stub firmware are ours.

## §3 Findings and proposals

Nothing below is decided. D-numbers are proposals; the questions at the end are Alex's.

**F1 The original is the better base than the MiSTer port (source).** `O` has GPL v3-or-later headers on the files read (`zxnext.vhd:1-22`), Vivado projects (`synth-zxnext/zxnext-issue5/*.xpr`, six IP cores as `.xcix`) and **synthesised in Vivado 2024.1 here with no edit to any original file** (§5). `M` adds the SDRAM-oriented wait mechanism (`i_CPU_WAIT`, `i_RAM_A_REQ_ALLOW`, `o_RAM_A_CYCLE`, `o_RAM_A_RFSH`) that we need in some form, but drags Quartus files, the HPS framework, its SD-image logic and a repository licence of GPL v2.

**F2 The memory does not map one to one (source, computed).** `O` has no wait input for RAM: `z80_wait_n` is built from `ula_wait_n`, `ulap_wait_n`, `sram_wait_n` and the expansion bus (`zxnext.vhd:1839`); `sram_wait_n` is an internal signal set at `:3176-3178`, the hook a wait input would join. `M` adds `i_CPU_WAIT` to that line (`M` `rtl/zxnext.vhd:1778`). The host's DDR2 read costs 293 to 693 ns (quoted from T95 S7). A Z80 memory read is 3 T-states: 857 ns at 3.5 MHz, 429 ns at 7 MHz, 214 ns at 14 MHz, 107 ns at 28 MHz (computed). So an uncached DDR2 read fits a 3.5 MHz cycle and not a 14 or 28 MHz cycle. 14 and 28 MHz need either wait states (the machine runs at about the DDR2's pace, as `M` does with SDRAM) or a cache that answers within the cycle.

**F3 What block RAM could do for it (computed, estimate).** `pb_l2` has 50 tiles; the measured core takes 14.5 (§5), the rest of the glue (DDR2 port, SD buffer, pixel writer) is not counted yet. At 4 KiB per tile that leaves at most 35.5 tiles = 142 KiB before any glue, against a machine of 768 KB to 2 MB. The monolith has 75 tiles, 60.5 left = 242 KiB. Neither holds the RAM; both can hold a cache. A cache answers a hit in a cycle; a miss waits.

**F4 Layer 2 is a second, read-only RAM port (source).** `o_RAM_B_ADDR` (21 bit) with a toggle request `o_RAM_B_REQ_T` and `i_RAM_B_DI`; modes 256x192x8, 320x256x8, 640x256x4 (`zxnext.vhd:195-197` in `M`, same ports in `O`). Demand (computed): 256 or 320 bytes a line, one line per 64 us at 50 Hz, so about 5 MB/s; the problem is the latency, not the bandwidth. A line prefetch in the style of T95 (line caches) fits.

**F5 The SD card is a SPI device the machine drives itself (source).** `O` has `spi_master` (19 LUT, 30 FF) with chip selects `o_SPI_SS_SD0_n`, `o_SPI_SS_SD1_n`, `o_SPI_SS_FLASH_n` and `i_SPI_SD_MISO`. `M` answers it with a virtual SD in `ZXNext.sv` fed by `sd_lba/sd_rd/sd_wr/sd_buff_*` from the MiSTer HPS (`ZXNext.sv:118-165`, `:376`). We have no HPS: the SPI answer is a small SD-over-SPI model in gateware (CMD0, 8, 55, 41, 58, 17, 24, 9, 10, tokens, CRC7/CRC16) that fetches 512-byte blocks from the app. How the blocks reach it is a choice (D4).

**F6 Video is RGB333 at the 14 MHz dot clock (source).** `o_RGB[8:0]`, `o_RGB_VS_n/HS_n/BK_n`, "synchronized to `i_CLK_14`", plus `o_VIDEO_50_60`, `o_VIDEO_SCANDOUBLE`. Layers: ULA (256x192, Timex hi-res 512x192), Layer 2, LoRes, tilemap 40 or 80 columns, sprites; 512 colours by palettes. Our host's choices are the 16-entry palette path (VC-20, Spectrum) and the RGB565 pixel layer in DDR2 (Amiga, CPC, C16; `pix_layer.v`, quoted T92 D6). A Next picture has far more than 16 colours at once: **the pixel layer is the only fit**. 9 bits expand to 16 without loss of the 9-bit information (3 bit to 5 and 6 bit by replication is within one LSB of the DAC's level).

**F7 Audio (source).** `o_AUDIO_L/R[12:0]` already mixed (`audio_mixer`, 16 LUT) from three `ym2149` in `turbosound` (883 LUT, 911 FF, the largest block after the CPU), soundrive DACs, beeper, ear/mic. Stereo.

**F8 Input (source).** Matrix `o_KBD_ROW`/`i_KBD_COL`, 16 extended keys `i_KBD_EXTENDED_KEYS`, keymap writes `o_KEYMAP_*` (for PS/2, unused by us), `i_JOY_LEFT/RIGHT[11:0]` (MODE X Z Y START A C B U D L R, so Sega 6-button), mouse X/Y/buttons/wheel. The C64U matrix has 64 keys, the Next 40 plus extended; one key translator as in T91 D8.

**F9 Clocks (source).** The core wants `i_CLK_28`, `_28_n`, `_14`, `_7`, a CPU clock "adjusted for speed and contention" (`i_CLK_CPU`) and a PSG enable. The top derives them from a PLL (`src/pll/A7-Issue-5`). Our cores use clock enables from `sys_clk` (T91 D4, T85 D3). The 180-degree clock `clock_master_180o_i` in the sprite RAMs (`sprites.vhd:470-571`) is a 180-degree-shifted master clock input; a design on clock enables from `sys_clk` has to reproduce that phase (a risk, not measured).

**F10 The boot ROM is in the source, the OS is not (source).** `rom/bootrom.vhd` (8 KB, 2 RAMB36, 2 LUT in §5) is VHDL data and starts the machine; it reads the SD card and loads NextZXOS' firmware from there (`M`'s README: "boot.vhd"). The 48K/128K/+3 ROMs and esxdos are on the image, not in the FPGA. So everything that is not GPL arrives on the user's image.

**F11 The cost of the core itself (measured, §5).** 7,835 LUT, 4,619 FF, 14.5 tiles, 0 DSP; the Z80N is 3,053 LUT (39 %), `turbosound` 883, sprites 655, ULA timing 524, UART 458, `peripherals` 351, DMA 330. Against `pb_l2`: 43.5 % LUT, 29 % tiles, before the glue; against the monolith: 24.0 % LUT, 19.3 % tiles. Synthesis only; routed numbers come with S12.

**Proposals**

- **D1 Source (P1).** The original `O`, pinned to a commit, vendored by a script with checksums as T95 did; the wait/handshake that `M` added is written by us as a small patch on `O`'s `zxnext.vhd` (a listed patch, S1), not taken from `M`. Reason: GPL v3-or-later headers, Vivado-proven, no HPS code. *Lost:* `M` (Quartus, GPL v2 repository, SDRAM-specific).
- **D2 Memory (P2, P3).** All Next RAM and the ROM area in DDR2 behind line caches with write-through and invalidation (the T95 pattern); the CPU waits on a miss through a wait input added by D1; Layer 2 has its own line prefetch (F4). Block RAM holds the core (14.5 tiles) and the caches. Which CPU speeds are offered is Q2.
- **D3 Boot (P9).** The core's own boot ROM runs unchanged; NextZXOS and the ROMs come from the user's image (D4). The app shows a clear message when no image is chosen.
- **D4 SD (P4).** An SD-over-SPI model in gateware answering the Next's SPI port; the 512-byte blocks come from the app through the DDR2 window (the image or its needed part resident in DDR2, or block requests answered by the app; which of the two is Q3). The image is a plain file on the stick; no card slot is emulated beyond presence.
- **D5 Video (P5).** The core samples `o_RGB` at the 14 MHz dot clock, expands 9 bit to RGB565 and writes a DDR2 frame buffer through the pixel writer already used by the CPC and the C16 (`cores/cpc`, `cores/c264`); the host's pixel layer shows it. Capture width and frame sizes are Q5.
- **D6 Audio (P6).** `o_AUDIO_L/R` (13 bit) scaled to the host's signed 24-bit stereo with `audio_sample_en`, as the other cores.
- **D7 Input (P7).** One key translator table (T91 D8) from the C64U matrix to the 40-key matrix and the extended keys; joysticks to `i_JOY_*`; mouse out until Q7.
- **D8 Clocks and Freeze (P8).** Clock enables from `sys_clk`; the CPU clock enable is the Next's own contended/speed-adjusted enable; Freeze gates every enable (C10).
- **D9 Delivery (P11).** Either the `.cor` in `pb_l2` or a monolith as `.cfw` (Q1); the 247 ports with the idle values and `l2_keep` where an output is constant.

**Open questions for Alex** (ordered; one at a time; none answered)

1. **Q1 Delivery.** `.cor` in `pb_l2` (measured core is 43.5 % of its LUT and 29 % of its tiles before glue; routing against the locked host is the unknown) or a monolithic `.cfw` (24.0 % / 19.3 %, the host constraints gone, the way back is a power cycle)?
   **Answered (Alex, 2026-10-22): `.cor`** (".cor is fine then", translated): the core goes into `pb_l2` behind the contract; D9 takes the `.cor` branch.
2. **Q2 CPU speeds.** All four (3.5 / 7 / 14 / 28 MHz) with DDR2 wait states, so 14/28 MHz run at the DDR2's pace and are not "28 MHz"; or only 3.5 and 7 MHz? (Corrected 2026-10-22: an uncached read fits only at 3.5 MHz, 857 ns against 293-693 ns; at 7 MHz the cycle is 429 ns, so the worst case waits there too.)
   **Answered (Alex, 2026-10-22): all four** ("all 4", translated): 3.5 / 7 / 14 / 28 MHz, the RAM in DDR2 behind a block-RAM cache in `pb_l2` with the CPU held (wait) on a miss; how often 14/28 MHz run at full pace is a bench measurement (hit rate), not a promise.
3. **Q3 SD image service.** Whole image (or a window of it) resident in DDR2, written by the app before release; or block requests answered live by the app? And writable, or read-only?
   **Answered (Alex, 2026-10-22): (b), live block requests answered by the app** ("well, b .. best would be to pass the SD card and USB ports through, or?", then "b", translated). Passing the board's SD card (already SPI, `host/top.v:91`) through to the core was offered and declined: it needs a host switch and contract 3. USB has no meaning to the Next; the app serves an image file from stick or SD. **Writable (Alex, 2026-10-22):** "well then a .img and it simply writes to it? that is standard file system stuff" (translated): the SD card is a `.img` file (a whole FAT16/FAT32 card as one file, block n at byte n x 512), read and written by the Next through the app, each written block goes straight back into the file. A folder as a virtual FAT was discussed and is not built.
4. **Q4 Image and ROMs.** The user supplies the NextZXOS image (and any ROM) as a file; the repository, the `.cor` and CI never hold one. Confirm?
   **Answered (Alex, 2026-10-22): yes** ("yes, do it like that", translated): never in the repository, a `.cor` or CI; Claude downloads the NextZXOS image from the official source (specnext.com or MiSTer's `boot.zip`) and puts it on Alex's stick, as for the ZX and CPC ROMs.
5. **Q5 Video capture.** 14 MHz dot sampling (640-wide hi-res kept, frame about 896 x 312 dots before cropping) or 7 MHz (320 wide, Layer 2 640x256 and Timex hi-res lose half)?
   **Answered (Alex, 2026-10-22): 14 MHz** ("yes, do it like that", translated, on the recommendation): the 640-wide modes keep every column; the captured line with border stays under the pixel layer's 960 px.
6. **Q6 Frame lock.** Accept the Next's own 50/60 Hz and the pixel layer's repeat/drop, or run the machine fast/slow to match the host's frame as T91 D4 does (+0.09 to +0.2 %)?
   **Answered (Alex, 2026-10-22): (a)**: the machine runs fast/slow by +0.09 to +0.2 % to match the host's 50 Hz frame, as T91 D4; the Next's 60 Hz mode cannot be matched and repeats/drops frames.
7. **Q7 Input scope.** Keyboard with the extended keys and joysticks only, or mouse (and the Sega 6-button joystick modes) as well?
   **Answered (Alex, 2026-10-22): keyboard with the extended keys, joysticks and the Kempston mouse** ("let us do it like that", translated, on the proposal); the mouse from the 1351 on a joystick port (T47) or a USB mouse through the app; the Sega 6-button modes are Out (§4).
8. **Q8 RAM size.** 768 KB (unexpanded), 1792 KB (expanded), and the issue-5 4 MB configuration bit: which sizes, and is 2 MB of DDR2 the one allocation or app-selectable?
   **Answered (Alex, 2026-10-22): 1792 KB, fixed** ("yes, fixed 1792 KB", translated): the expanded machine, no setting; the 768 KB and the issue-5 4 MB configurations are not offered.
9. **Q9 Vendoring.** Vendor the original's tree with one patch (D1) the way T95 vendors NES_MiSTer, or read it from outside at build time and keep only the patch in the repository?
   **Answered (Alex, 2026-10-22): (a)**: the original tree vendored unchanged into `cores/zxnext/rtl` with our patch beside it (D1, as T95 with NES_MiSTer); lint checks the tree against its pinned commit.
10. **Q10 Out list.** Confirm §4 (ESP/WiFi UART, Raspberry Pi GPIO, expansion bus and DivMMC/Multiface/NMI buttons, flash-boot slots, HDMI audio, I2C RTC).
   **Answered (Alex, 2026-10-22): §4 confirmed, and the app supplies the time** ("yes, and the app supplies the time", translated): the I2C RTC is in, answered from the app's clock, so NextZXOS has date and time for files.

- **D10 Board issue chosen in the menu (Alex, 2026-10-26).** Board 2026-10-25: the image's `TBBLUE.FW` stopped with "Unknown board
  id 0x03"; a newer firmware patched into `next.img` booted. Asked whether setting the id in the menu makes sense - proposed: the id
  the core reports (NextReg 0x0F, `port_253b_dat`, today the generic `g_board_issue`) comes from a register the core app sets before
  the reset, a menu option "Board issue 2 / 4 / 5", default 5, stored in the settings, so an older image runs without a patch; the
  firmware treats RTC, ESP and keyboard per issue, so it is an option and not automatic - Alex: "Passt." ("fits"). Only the reported
  id changes: `g_board_issue` keeps selecting the structure (SRAM, FDC, core id, ROM/RAM generates stay issue 5).
- **D11 Board issue "Auto", the new default (Alex, 2026-11-02).** Board 2026-11-02 (package t164-c83a83c2): of Alex's two images
  the 277 MB one boots, the 2 GiB one (older `TBBLUE.FW`) stops with "wrong board revision" at the default Issue 5. Alex: "didn't we
  want to make a parameter and 'auto' out of that?"; proposed: a value "Auto" next to Issue 2 / 4 / 5, the default, where the app
  picks the id from the mounted image's firmware before the release; Alex: "Yes, do that" (translated). Amends D10's "an option and not
  automatic": the fixed values stay for the user who wants one; Auto chooses among them. How the app tells the firmware's issue from
  the image (a version string or table in `TBBLUE.FW`, or a first boot that reads what the firmware rejects) is the builder's to find
  and to prove with both of Alex's images' firmware in a bench; an image Auto cannot place falls back to Issue 5 and says so.
  Same day: the default image name the core looks for is `next.img` (T148); Alex's working image was `next_new.img` and was renamed
  on the card to `/SD/roms/zxnext/next.img`, which is why every start reported "Error initializing SD card" until a manual pick.

## §4 Out (confirmed by Alex, 2026-10-22, Q10)

- The ESP/WiFi UART peer, the Raspberry Pi accelerator GPIO (`i_GPIO`, `o_GPIO`), the external expansion bus (`o_BUS_*`, no DMA from outside), the +3 disk-controller strobes: no hardware behind them.
- Flash boot slots and `o_FLASH_BOOT/o_CORE_ID` (anti-brick): the host starts cores.
- XADC and DNA ports (`o_XADC_*`, `o_XDNA_*`): tied off.
- HDMI/VGA output stages of the original top (`scan_convert`, `hdmi`, `hdmi_out_xilinx_a7`): the host's pixel layer replaces them.
- Multiface/DivMMC hardware buttons (the NMI keys come from the keyboard translator or the app menu, if at all).
- PS/2 keyboard and mouse protocol blocks (`ps2_keyb`, `ps2_mouse`): replaced by the key translator.
- Any ROM or NextZXOS content, shipped or built (CLAUDE.md).
- 100T board: not released.

## §5 Measurement of the original core (2026-10-22)

**What was run:** an out-of-context Vivado synthesis of the entity `zxnext` (top of `cores/zxnext/src/zxnext.vhd`, 7,595 lines) with the Z80N (`T80Na`/`T80N`) and everything it instantiates, for `xc7a50tfgg484-1`, Vivado 2024.1. Nothing is placed or routed. The RAM is not modelled: `o_RAM_A_*`/`o_RAM_B_*` are ports (the 768 KB to 4 MB of the machine are outside the entity).

**Where:** the builder, a scratch tree `~/t116` (outside any repository; `orig/` = the original at `b047011`, `mister/` = the port at `7daebd1`), `nice -n 15 flock /srv/trx64-build/cache/heavy.lock docker run --rm -v $HOME/t116:/w vivado-env:2024.1 ... vivado -mode batch -source /w/synth.tcl`. Script: `read_vhdl -vhdl2008` of `zxnext.vhd` and every `.vhd` under `audio audio/i2s cpu device ram rom serial misc video` except `flashboot`, `flashboot_256`, `flashboot_old`, `uart_old` (top-level pin files, `video/hdmi`, `video/vga`, `pll`, `input` are not read); `synth_design -top zxnext -part xc7a50tfgg484-1 -mode out_of_context -generic g_machine_id=8'h0A -generic g_video_def=3'b000 -generic g_version=8'h32 -generic g_sub_version=8'h03 -generic g_board_issue=4'h3 -generic g_video_inc=2'b11` (the generics of `zxnext_top_issue5.vhd:34-39, 2357-2366`), working directory `src/ram` (the RAM initialiser opens `init/none.bin.txt` at elaboration), then `report_utilization` plain and `-hierarchical -hierarchical_depth 3`, `report_ram_utilization`.

**Edits to the original: none.** Vivado took the files listed above unchanged; the one warning of note is `tdpram.vhd:62` "shared variables must be of a protected type" (a warning, synthesis went on). The only addition is `ipmodels.vhd`, five behavioural stand-ins for the original's Xilinx IP cores, which the project keeps as `.xcix` (`synth-zxnext/ipcore_dir/*.xco`) and which are not in the source tree: `sdpbram_16k_8` (16384 x 8, block RAM, the sprite pattern memory), `sdpram_128_8` (x5, sprite attributes), `sdpram_64_9` (x2, UART TX FIFOs), `sdpram_16_9` (tilemap), `spram_320_9` (x2, sprite line buffers). The first is written as block RAM (`ram_style "block"`, matching its `.xco`: `memory_type=Simple_Dual_Port_RAM`, `primitive=8kx2`, `write_depth_a=16384`); the other four use the generator's distributed-memory ports (`DPRA`/`SPO`, asynchronous read) and are written as distributed RAM. (The generator type of the four is inferred from the port names `DPRA`/`SPO` and the name without `b`; their `.xco` files read here carry only `memory_type`.)

**Negative control (the first run, no models):** the same script without `ipmodels.vhd` finishes with five `CRITICAL WARNING [Project 1-486/1-560] Could not resolve non-primitive black box cell` (`sdpbram_16k_8` as `sprite_mod/pattern`, `sdpram_128_8` x5, `sdpram_16_9`, `sdpram_64_9` x2, `spram_320_9` x2) and reports **7,519 LUT, 4,619 FF, 10.5 tiles (8 RAMB36 + 5 RAMB18)**: the numbers that leave the IP out are 316 LUT and 4 tiles short. The run with the models is the one used below.

**Result (xc7a50t, synthesised, `util.rpt` of the run with models):**

| Resource | Used | Available (device) | Share |
|---|---|---|---|
| Slice LUTs (total) | 7,835 | 32,600 | 24.0 % |
| of which logic | 7,568 | | |
| of which distributed RAM | 242 | | |
| of which shift registers | 25 | | |
| Slice registers (FF) | 4,619 | 65,200 | 7.1 % |
| F7 / F8 muxes | 182 / 21 | | |
| RAMB36E1 | 12 | | |
| RAMB18E1 | 5 | | |
| Block RAM tiles (RAMB36 + RAMB18 / 2) | **14.5** | 75 | 19.3 % |
| DSP48 | **0** | 120 | 0 % |

Primitives (report section 7): LUT6 3,694, LUT5 1,814, LUT3 1,110, LUT4 1,088, LUT2 835, LUT1 312, CARRY4 348, RAMD64E 144, RAMS64E 90, SRL16E 25, RAMD32 12, RAMS32 4.

**By block (hierarchical report, LUT / FF / RAMB36 / RAMB18; instances at depth 1):**

| Instance | Module | LUT | FF | RAMB36 | RAMB18 |
|---|---|---|---|---|---|
| `cpu_mod` | T80Na / T80N (Z80N) | 3,053 | 434 | 0 | 0 |
| `turbosound_mod` | 3 x AY (ym2149) | 883 | 911 | 0 | 0 |
| `sprite_mod` | sprites (incl. pattern RAM, attribute/line LUTRAM) | 655 | 223 | 4 | 0 |
| `(zxnext)` | the top's own logic (nextreg file, MMU, ports) | 630 | 1,489 | 0 | 0 |
| `timing_mod` | ULA timing | 524 | 92 | 0 | 0 |
| `uart_mod` | UART (two, FIFOs) | 458 | 435 | 0 | 1 |
| `im2per_mod` | interrupt peripherals | 351 | 60 | 0 | 0 |
| `dma_mod` | z80dma | 330 | 215 | 0 | 0 |
| `ctc_mod` | CTC (4 channels) | 188 | 140 | 0 | 0 |
| `ula_mod` | ULA | 152 | 175 | 0 | 0 |
| `palette_utm` | tile/ULA palette RAM | 130 | 0 | 0 | 1 |
| `tilemap_mod` | tilemap | 127 | 109 | 0 | 0 |
| `i2s_mod` | I2S | 126 | 71 | 0 | 0 |
| `layer2_mod` | Layer 2 | 68 | 105 | 0 | 0 |
| `soundrive_mod` | DAC | 34 | 50 | 0 | 0 |
| `palette_l2s` | Layer 2/sprite palette RAM | 30 | 0 | 0 | 1 |
| `im2ctl_mod` | | 26 | 19 | 0 | 0 |
| `divmmc_mod` | | 20 | 3 | 0 | 0 |
| `copper_inst_msb_ram` / `lsb_ram` | Copper program RAM | 20 / 3 | 0 | 0 | 1 / 1 |
| `spi_master_mod` | SPI (SD) | 19 | 30 | 0 | 0 |
| `audio_mixer_mod` | | 16 | 26 | 0 | 0 |
| `copper_mod` | Copper | 15 | 28 | 0 | 0 |
| `multiface_mod` | | 5 | 4 | 0 | 0 |
| `bank7_ram` | second display RAM, 8 KB | 4 | 0 | 2 | 0 |
| `bank5_ram` | first display RAM 16 KB | 2 | 0 | 4 | 0 |
| `gen_bootrom.bootrom_mod` | boot ROM, 8 KB | 2 | 0 | 2 | 0 |

(The LUT figure per instance includes its descendants, as the report's "Total LUTs"; the block-RAM tiles are 4 (sprite pattern) + 4 (bank5) + 2 (bank7) + 2 (boot ROM) = 12 RAMB36 and the five RAMB18 are the two palettes, the two Copper RAMs and the UART receive FIFO.)

**Fit (computed from the table above, glue not yet included):**

| Budget | LUT | Tiles | DSP | Core share | Left |
|---|---|---|---|---|---|
| `pb_l2` 18,000 LUT / 50 tiles / 80 DSP | 7,835 | 14.5 | 0 | 43.5 % LUT, 29.0 % tiles | 10,165 LUT, 35.5 tiles for DDR2 port, line caches, SD model, pixel writer, keys, contract |
| Monolith (T115) 32,600 LUT / 75 tiles / 120 DSP | 7,835 | 14.5 | 0 | 24.0 % LUT, 19.3 % tiles | 24,765 LUT, 60.5 tiles |
| Reference C64 core (13,614 LUT, 39 tiles, quoted T113 F4) | 7,835 | 14.5 | 0 | 58 % of its LUT, 37 % of its tiles | — |

Reading: size is **not** the obstacle. The Z80N is 39 % of the core, no DSP is used, block RAM is one fifth of the chip. What the numbers do not say: the routed LUT count (typically lower than the synthesised one), timing at 28 MHz with both clock edges in the sprite RAMs (F9), the glue (DDR2 port with line caches is the part of T95 that cost area), the 3-AY/ DAC audio path through the host's mixer, and anything that routes against the locked host (T87 D15: a core denser than the reference may not route). The next measurement is the glue plus a flat route (S12).

**Not measured:** routing, timing, the glue, the pixel writer, power. The IP stand-ins are behavioural, not the generator's netlists; a Block Memory Generator netlist for the pattern RAM may use a different tile count (the `.xco` says `primitive=8kx2`, i.e. a 2-bit wide RAMB36 array, 4 tiles, the figure used).

## §5.1 What was built (t116-build, 2026-10-22)

Tags: **(source)** read in a file, **(measured)** a run or a report, **(computed)** arithmetic on those, **(estimate)** neither. Every bench number below is a line of the log named with it; the benches are `cores/zxnext/sim/run.sh <mode>` (Verilator, `nice -n 15`), the report is the Vivado log of the builder.

### 5.1.1 Source, generator, lint (P1, S1)

- **Vendored (source):** `tools/vendor_zxnext.sh` copies the 45 VHDL files and `COPYING` of the original at `b047011` into `cores/zxnext/rtl/`, byte for byte, with `SHA256SUMS`; `rtl/NOTICE` is ours. No board top, no PLL, no PS/2, no flash boot, no HDMI/VGA, no Xilinx IP, no ROM, no NextZXOS file. `bootrom.vhd` (the machine's own boot ROM, GPL, data as VHDL) is in; it has no licence header of its own, the repository's GPL v3 text covers it (`check_zxnext_tree.py` lists it with `dac.vhd` and `bootrom_ab.vhd` as the three files without a header).
- **Memories (source):** `cores/zxnext/ip/zxn_ip.vhd` is ours: the five Xilinx IP memories of the original (`sdpbram_16k_8`, `sdpram_128_8`, `sdpram_64_9`, `sdpram_16_9`, `spram_320_9`) and the two generic RAMs the original wrote for Vivado (`dpram2`, `tdpram`) as plain VHDL.
- **Generator (source):** `tools/zxnext_gen.py` makes `cores/zxnext/gen/zxnext.v` (4,401,316 bytes, 92,739 lines): four counted edits to a scratch copy of the VHDL (a signal declared in a package, `ALU_Q` renamed, `to_unsigned(-1)`, `UNISIM`), GHDL 6.0.0 in the container `trx-ghdl6` (`tools/ghdl6/Dockerfile`) converts it, then a text transform puts the five clocks of the machine into the enable form (every clock `X` becomes `X_cep` / `X_cen`, every process `always @(posedge fclk) if (X_cep)`), makes the zero initialisers a loop, and turns the Z80N's register file into flip-flops with one write block (5.1.2). The raw GHDL output is kept as `gen/zxnext_ghdl.v.gz`; `gen/zxnext.vhd.sha256` is what it was made from.
- **GHDL 5.0.1 does not work (measured):** its conversion read back `LD BC,0x4000` as 0 (the register file's width declarations come out wrong), a clear-screen loop never ended and a port address read `0x903B`. GHDL 6.0.0 converts the same VHDL correctly; the generator refuses any other version.
- **Lint (S1):** `zxnext gen` (`--check`: `rtl/` is on its hashes, the VHDL input is what was converted, `gen/` is what the raw output makes; no GHDL needed), `zxnext tree` (`tools/check_zxnext_tree.py`: no ROM, no `.bin`, no `.img`, no file without its place; selftest refuses a changed byte, a missing file, a stripped header, a `.rom`, a foreign `.vhd`, a `.bin` and an `.img`), `zxnext programs` is the bench programs assembling (`tools/zxnext_progs.py check`), the 247 ports (`contract_ports.py`: `zxnext matches (247 ports)`), the core id (`check_core_id.py`: `ZXN_`), `layers.txt` (the Next's `rtl/` and `sim/` are `ignore:`), and the logo (`tools/core_logos/build.py --check`).

### 5.1.2 Synthesis with the glue (S2, P10)

Out of context for xc7a50t, Vivado 2024.1, `cores/zxnext/core_sources.tcl` as the core stage sources it (builder, `~/t116glue`, under `flock heavy.lock`; 1 min 45 s; **no error, no critical warning**; not placed, not routed):

| | LUT | FF | RAMB36 | RAMB18 | tiles | DSP |
|---|---|---|---|---|---|---|
| `layer2_zxnext` (core + glue) | **10,502** | 7,537 | 26 | 8 | **30** | 0 |
| of which `zxnext` (the original, as generated) | 8,413 | 4,364 | 12 | 5 | 14.5 | 0 |
| `mem` (two caches, DDR2 engine) | 648 | 1,543 | 12 | 2 | 13 | 0 |
| `sd` (card model, 512 byte buffer) | 513 | 599 | 0 | 1 | 0.5 | 0 |
| `regs` | 377 | 292 | 0 | 0 | 0 | 0 |
| `pixw` (frame writer) | 265 | 431 | 2 | 0 | 2 | 0 |
| `rtc_i2c`, `audio`, `zclk`, `zxn_core` | 110 + 66 + 53 + 54 | 40 + 126 + 50 + 90 | 0 | 0 | 0 | 0 |

Against `pb_l2` (18,000 LUT, 50 tiles, 80 DSP): **58.3 % of the LUT, 60 % of the tiles** (computed); the budget has 7,498 LUT and 20 tiles beyond this core, and how much of that placement can use is for routing to say. The original alone was 7,835 LUT / 14.5 tiles (§5, measured before): the generated machine is 578 LUT larger (the enable muxes of the clock-enable form, the register file as flip-flops), the glue adds 2,089 LUT and 15.5 tiles, of which 13 are the two caches. **Routing and timing are not measured here**: `core_timing.tcl` gives the machine-to-machine paths two clocks (multicycle 2); that is an assumption (the gap between a rising and a falling enable is 1 clock, between a falling and the next rising 2 or 3) and S12 (WNS, `pr_verify`) is the CI's.

**What the first synthesis found (measured, builder 2026-10-22):** (1) `ERROR [Synth 8-2914] Unsupported RAM template` for the five memories GHDL makes of the Z80N's register file (`regsh_n1..3`, `regsl_n1..2`, eight entries, three read ports, three write statements of one process) - the generator now gives them `ram_style "registers"`; (2) with that attribute alone, `CRITICAL WARNING [Synth 8-6859] multi-driven net` on every bit, because GHDL writes each of the three statements as its own `always` block - the generator now merges the three into one block in the VHDL's order (later wins, as in the VHDL). The simulation did not see either: Verilator accepts both. The numbers above are the third run.

### 5.1.3 Clocks, frame lock, Freeze (P8, S9)

- **Clock enables (source):** `zxn_clk.v` makes `r28` (a rising 28 MHz edge) from a 32-bit NCO on `cpu_clk` (100 MHz): one pulse every 3 or 4 `cpu_clk`, `f28` two clocks later, `r14`/`f14`, `r7`/`f7` from its 2-bit phase, `cpu_r`/`cpu_f` the CPU clock at 3.5 MHz (the original's `CLK_3M5_CONT` flop: held while the ULA contends), 7, 14 or 28 MHz, and the PSG enable (1/16 of `r28`). The machine's reset is held 1,024 master cycles after the app's reset and after its own soft reset.
- **Frame lock (measured, bench `clk1`-`clk4`):** the machine runs the NCO step that makes its frame the C64's PAL frame. Locked, the frame is **1,995,029-1,995,030 `cpu_clk` in every timing** (48K, 128K, +3, Pentagon; the PAL frame is 19,656 cycles at 985,248 Hz = 1,994,980 `cpu_clk`, within 0.0025 %, bound 0.01 %). Unlocked (the app's `LOCK` register) the machine's own frame, as the T-states of its frame give it (computed: 69,888, 70,908, 70,908, 71,680 T x 8 master cycles at 28 MHz, in `cpu_clk`): **1,996,800 / 2,025,943 / 2,025,943 / 2,048,000**, measured 1,996,800 / 2,025,942-2,025,943 / 2,025,942-2,025,943 / 2,048,000. So the lock makes the machine **+0.089 % (48K timing), +1.55 % (128K and +3), +2.66 % (Pentagon) fast** - not the +0.09 to +0.2 % that Q6 named (that figure was the 48K one); the machine's default after the boot ROM is the +3 timing, +1.55 %. 60 Hz timings run at 28.000 MHz unlocked and the pixel layer repeats and drops frames (as decided). See 5.3 for the question this leaves.
- **Freeze (measured, bench `freeze`):** the app's halt holds the master clock at the next `r28`; the machine reports held when no master cycle is half done, the frame writer is quiet and no memory or card job is pending. Three Freezes (at the 20th, 60th and 110th of 150 rounds of a program that copies memory every round) of 3 ms each: **0 master cycles, 0 CPU clocks, 0 port writes, 0 DDR2 writes by the machine, 0 by the frame writer while held**; the run is equal to the run without a Freeze, **150 port writes and the final memory, master cycle for master cycle**.

### 5.1.4 Memory (P2, P3; S3, S4)

- **Design (source):** `zxn_mem.v`. Port A (CPU, DMA): 32 KB direct-mapped cache of 2,048 lines of 16 bytes in block RAM (index = address bits 14:4 xor 20:15, tag 6 bits + valid), write-through without allocation, a one-entry write buffer. A read that hits is answered three `cpu_clk` after the request appears (two until 2026-10-23, see 5.2 item 9); **a miss holds the whole machine** (`stall` removes the clock enables) until the line is in, so the machine sees a memory that always answers in time; the spec's wait input (`sram_wait_n`, D1/D2) is not used and the original's file stays unpatched. Port B (Layer 2): 16 KB, 1,024 lines, next-line prefetch (an access to the second half of a line asks for the line behind it: the next 16 pixels of a row in 256x192, the next 16 lines of a column in 320x256 and 640x256), written bytes invalidate a line. The engine does one DDR2 command at a time on the host's real-time port `c_*` (buffered write, miss of A, miss of B, a line of the card buffer, a prefetch). The machine's 1,792 KB and its 256 KB of ROM/DivMMC space are one 2 MB image at `ram_base` (default `0x01000000`).
- **S3 (measured, benches `mem_ref0-3`, `mem0-3`):** the program `prog/mem.s` (a rotate-and-xor fill of 2 KB and `LDIR`, `LDDR`, port `0x7FFD` paging of eight banks, the MMU slots, a 1 KB DMA through port `0x6B`, two self-modifying routines, a 16 KB `LDIR`) runs at each CPU speed on the one-cycle SRAM of the original (`sim/zxn_mem_sram.v`, the same ports as `zxn_mem`) and on the DDR2 caches (the DDR2 model answers in 22 + 0..30 `ui_clk`: 0.15 to 0.35 us). **The final memory image (a hash over every range the program owns), the 11 results and the 1,078 port writes are the same on both, at 3.5, 7, 14 and 28 MHz; at 7, 14 and 28 MHz the master cycles between the port writes are the same too.** At 3.5 MHz the times differ because the ULA's contention depends on where in the frame the program started, which the length of the boot decides (a fixed offset after the first stage, -616 master cycles in the first measurement; the content is equal, which `run.sh` checks). What the misses cost, the whole program, `cpu_clk` (the `the program:` line of each log):

  | speed | cpu_clk, whole run | held for DDR2 | line fetches |
  |---|---|---|---|
  | 3.5 MHz | 40,339,312 | 90,745 (0.22 %) | 1,490 |
  | 7 MHz | 18,076,862 | 90,799 (0.50 %) | 1,490 |
  | 14 MHz | 9,231,685 | 90,006 (0.97 %) | 1,490 |
  | 28 MHz | 5,574,534 | 90,838 (1.63 %) | 1,490 |

  The program is a few memory blocks of 2-16 KB, so this is the cost of cold lines, not a game's working set: the hit rate of a real program is Alex's board measurement (S13). 1,490 fetches in 90,800 clocks is 61 `cpu_clk` (610 ns) per miss.
- **Controls (measured):** `mut_nowait` (a miss does not hold the machine) and `mut_nowrcache` (a write that hits leaves the cached line alone) both fail the equivalence - the program does not run to its end.
- **S4 (measured, benches `video0-2`):** Layer 2 in 256x192, 320x256 and 640x256 shows the pattern in every dot (5.1.6); in steady state it makes **2 demand misses in 2 frames** (the bound is 256 a frame: a frame reads 5,120 lines); `mut_late` (no prefetch) makes 9,920 and fails. Both counters are the ones of `zxn_mem` (`st_miss_b`).

### 5.1.5 SD card (P4; S5, S10)

- **Model (source):** `zxn_sd.v`, an SDHC card in SPI mode 0 on the Next's `spi_master`: CMD0, 1, 8, 9, 10, 12, 13, 16, 17, 18, 24, 25, 55, 58, 59 and ACMD41 (22, 23, 51 get an R1, anything else R1 = 0x04). A block read posts the request in the mailbox (`MBOX_STATUS/LBA`), answers 0xFF until the app has written the 512 bytes into the staging sector in DDR2 and `MBOX_DONE`, then sends token 0xFE, the block and its CRC16; a block write takes the 512 bytes (CRC16 not checked), answers 0x05, holds MISO low while the app writes the block into the file, then accepts the next command. The block buffer is one 512 byte block RAM that serves the SPI side and the memory engine's 128-bit lines.
- **Hold (board C, `zxn_sd.v` `hold`):** while the card waits for the application (a read from the eighth poll byte on, a write in its busy wait) the machine's master clock is held, so that the application's time does not count in the machine's driver; 2^28 `cpu_clk` at most. Details and benches in the board C paragraph below.
- **S10 (measured, benches `boot`, `ctl_boot`, `noimg`, `ctl_noimg`):** the machine's own boot ROM runs from reset on a FAT16 image made by `tools/zxnext_img.py` (MBR, boot sector at LBA 2,048, a root directory, `TBBLUE.FW`): it reads 10 blocks (last LBA 2,052), loads the stub to `0x6000` and starts it; the stub (20 bytes) writes its marker. A flipped byte in the stub (`ctl_boot`) fails. With no image (`+NOIMG`) the card never answers, the boot ROM prints its error into the display RAM (about 5 s of machine time, `noimg`) and the stub never starts; `ctl_noimg` (the image present, the run cut short) fails.
- **S5 (measured, bench `sd`):** the program talks to the card through ports `0xE7`/`0xEB`: CMD0 `01`, CMD8 `01 00 00 01 AA`, CMD55 `01`, ACMD41 `00`, CMD58 OCR `00 C0 FF 80 00`; CMD17 of LBA 8,192 returns `00`, token `FE`, the 512 bytes of the file and a **CRC16 equal to the one the bench computes**; CMD24 of LBA 12,000 returns `00`, data response `05`, **the file changes in exactly that block and no other byte** (the app model writes it back), the card was busy during the write, the block reads back with its CRC; a block beyond the end of the image reads as token and zeros. 13 reads and 1 write reached the app. Controls: a wrong LBA expectation (`ctl_sd_lba`), a write that is not put into the file (`ctl_sd_drop`), a flipped block bit in the model (`mut_sdflip`) and a wrong CRC (`mut_sdcrc`) all fail.
- **S5, board B (2026-10-23, release main-f6f3dfdc; benches `sdfast`, `sdmulti`, controls `ctl_sdfast`, `ctl_sdmulti`, `mut_noseq`):** the card was found, the FAT read and a firmware started, then "ZX Spectrum Next Configuration" and "Error reading TBBLUE.FW data!". **Neither text is in the boot ROM** (`rtl/rom/bootrom.vhd` holds "Error reading TBBLUE.FW file" and no "Configuration"): the ROM stage worked on the board (init, MBR, FAT, TBBLUE.FW found and loaded, `JP 0x6000`); the message is the loaded firmware's, whose code is not in this repository, so which SD commands it sends is **not known**. What was measured on the model: the ROM's own read pattern (CMD17 with CS kept low, 512 bytes by two INIR, two INs for the CRC; ROM 0x04E8) at 3.5 MHz is the `boot` bench (10 blocks); the same pattern with no gap between SPI accesses at 28 MHz (SPI clock = CPU clock / 2, `spi_master` runs on `i_CLK_CPU`) is `sdfast` (6 blocks, token and CRC16 equal to the bench's). A multi-block read that is stopped (CMD18 of three blocks, CMD12 right after the third CRC, CMD17, CMD17) with an application that needs 1.5 ms per request is `sdmulti`. **That one failed on the old mailbox** (`mut_noseq` is the old rule): the card had asked for the fourth block when CMD12 came, the application's late answer for it was taken for the answer to the next CMD17 (LBA 8,200 came back as the block of LBA 8,195, CRC16 231E), because DONE carried no name of the request. Now the status carries a 4 bit request tag (b7:4, `mb_seq`, bumped with every request the card posts) and DONE counts only when it carries the tag of the pending request (`zxn_regs.v`); `sectors.cc` writes back the tag it read with the status (`SectorMediaSpec.done_tag_mask`, 0xF0 here, 0 for the Amiga: its DONE is still any write). Whether the firmware uses CMD18/CMD12 is open; the application now logs `[zxnext] SD CMDn` for every change of the card's last command (100 lines at most), so that the next board run answers it. Not causes (code): the image (2 GiB = 4,194,304 sectors, CSD v2 C_SIZE 4,095 = 2 GiB, OCR C0FF8000 = block addressing, the ROM reads it right), the ROM's timeouts (R1 and token waits of 25,600 reads each, ten tokens: 2.6 s at 3.5 MHz against an application that answers in the service period of 2 ms plus the file read).

- **S5, board C (2026-10-24, release trx64mc-main-54e63c47; benches `fwdrv`, `fwslow`, `fw`, controls `mut_nosdhold`, `mut_fw_nohold`):** the same screen and "Error reading TBBLUE.FW data!" after the T116 tag fix; the application's log shows `SD CMD58, 17, 12, 17, 18, 12` and nothing after. **Cause, measured with the firmware's own code:** the message is printed by the loaded firmware (TBBLUE.FW, module at 0x6000, FatFS with a driver of its own; read from the user's file by disassembly, the file is not in this repository) at `0xB686`, in the helper `0xB649` that does `f_read(fil, buf, len, &br)` and reports when the result is not zero or `br != len` - there is no size, checksum or magic check in that path. The driver (`disk_read` 0x7C39): CMD17 for one block, CMD18 for a run inside one cluster, per block `rcvr_datablock` 0x792F = poll the data port for a byte other than 0xFF **at most 5,000 times** (one poll is about 360 T-states, the bench measures 14.6 microseconds at 28 MHz, so about 70 ms), the byte has to be 0xFE, 512 bytes by `IN` / `LD (BC),A` loop, two CRC bytes thrown away; a block that fails ends the loop, CMD12 follows (its answer is not looked at: eight `IN`s, then 250 polls for an R1), the shortfall makes `disk_read` fail. The firmware sets 28 MHz at its start (`nextreg 7 = 3`, 0x6AFB). **A block that the application needs more than about 70 ms for makes the firmware give up on it.** The board's card is 2,147,483,648 bytes, FAT32, 8 sectors a cluster, TBBLUE.FW 304,640 bytes in 75 clusters in two runs (14452..14455 and 18444..18514, LBA 132,112 and 164,048); the application serves a block with `File::seek` + `read` (`coreapp/src/fm_fs.cc`), and the Ultimate's FatFS is built with `FF_USE_FASTSEEK 0`, so a seek that goes back walks the cluster chain from the start of the image (a FAT32 stick: tens of milliseconds and more at 84 MB); the directory blocks at 67 MB were fast enough, the second run of TBBLUE.FW at 84 MB was not (the application's time per block is **not measured on the board**; the application now logs every request that takes 20 ms or more, `[sectors] unit 0: sector N read took M ms`, 20 lines). **Reproduced with the firmware's code on the machine** (`fw`, the card's real layout from `tools/zxnext_fwmap.py`: 10,263 sectors of the image at their LBAs, the bench answers a block in 3 ms and, from machine time 1,000 ms on, a block at LBA 164,000 and above in 100 ms): the firmware reads the card as the board's log says (CMD12, CMD0, CMD8, CMD55, ACMD41, CMD58, CMD17s, CMD18 at 1,373 ms) and, with the card as it was, ends at 1,454 ms with its PC at 0xB686 after `CMD18, CMD12` and no command after (`mut_fw_nohold`: **FAIL as the board**); with a 3 ms application throughout it reads the file to its end (127 requests, 1,529 ms). Sweeps with the real firmware before the fix: 60 ms for every block passes, 100 ms for every block passes up to the 54th request of 127, 200 ms for a block at LBA 100,000 and above stops at the first of them (the timeout lies between 60 and 200 ms). **Fix (gateware, `zxn_sd.v` `hold`, `zxn_core.v`):** the machine's time stands while the card waits for the application: the master clock is held from the eighth poll byte of a read wait (before that the host may be about to send CMD12; a guess of a multi-block read is not waited for, `sdmulti` caught that in the first version) until the block is in the card's buffer, and in the busy wait of a write until the application has taken the block; a driver then finds the token at its next poll (`fwdrv`: at most 10 polls where the unheld card took 205). An application that does not come back lets go after 2^28 `cpu_clk` (2.7 s). Cost: sound, picture and timers of the Next wait as long as the application does (the cache miss's hold, longer); nothing is missed. Benches (builder, `trx64-sim-env:4`, 2026-10-24): `fw` PASS (127 requests, the second run of TBBLUE.FW at 100 ms a block, machine time 5,116 ms of which 3.5 s are the frozen waits) against `mut_fw_nohold` FAIL ("the firmware says Error reading TBBLUE.FW data", 1,454 ms, last command CMD12); `fwdrv` (the firmware's driver as a program of ours, `prog/fwdrv.s`, written from its instructions: CMD18 of 8 blocks twice and CMD12, CMD17; 5,000 polls) PASS; `fwslow` (300 polls against 12 ms a block) PASS and `mut_nosdhold` FAIL (token polls 300 of 300, a block lost); `boot`, `sd` (CMD24 under the hold), `sdfast`, `sdmulti`, `mem_ref1` and `mem1` (equal to the reference) PASS; the controls `mut_noseq`, `ctl_sdmulti`, `ctl_sd_lba`, `ctl_sdfast` fail as they must. Not rerun with the hold: `noimg` (no card, the hold needs `present`), `mut_sdflip`, `mut_sdcrc`, `mem0`, `mem2`, `mem3`, `video*`, `io`, `audio`, `clk*`, `freeze` (the CI). `fw` needs the user's card (ZXN_FW_MAP, sectors made by `tools/zxnext_fwmap.py`): it is not in CI; `fwdrv`, `fwslow` and `mut_nosdhold` are (`zxnext_sd_hold`, `zxnext_sd_hold_mut`). **Not proven:** the board (the application's real time per block, that the hold is long enough for it, that sound and picture recover without a glitch worth hearing); the benches' application is a model (`+LAT`, `+FARLAT`).

### 5.1.6 Picture (P5; S6)

- **Design (source):** `zxn_pixw.v` samples the machine's 9-bit RGB at every 14 MHz dot (`dotst`, two clocks after the `CLK_14` edge that made it), expands it to RGB565, packs eight dots into a 128-bit word and writes a 640 x 256 source frame into a DDR2 frame buffer through the host's general port `e_*` with every line twice (640 x 512 in the buffer, a dot is half as wide as a line is tall, the Next's aspect). Four buffers (1 MiB apart, base `0x01D00000`); the app's descriptor lists them (`video` record) and the host's pixel layer shows the newest complete one. The window starts after the blank at dot `x0` and line `y0` (48K 0/24, 128K and +3 16/24, Pentagon 64/32; 60 Hz line 0).
- **Measured (benches `video0-2`):** Layer 2 filled through the MMU with a pattern that is a function of the byte's offset, the Layer 2 palette made the identity by the program. In 256x192 the window is **columns 64..575, rows 64..447 of the 640 x 512 buffer** (32 pixels and 32 lines of border, two dots a pixel, two rows a line) and **196,608 of 196,608 dots are the colour of the pattern**; in 320x256 (column-major in the Layer 2 memory) **327,680 of 327,680**, in 640x256 4 bit (high nibble first) **327,680 of 327,680**; no overrun of the line hand-over. `ctl_video_shift` (the expectation one pixel to the right), `ctl_video_row` (one line down) and `mut_decim` (every second 14 MHz dot repeats its neighbour: fails the 640-wide mode with 153,600 wrong dots) fail.
- **Not benched (decision for Alex, 5.3):** sprites, the tilemap (40 and 80 columns), the ULA's own picture and the Copper do not appear in a bench; they are the original's blocks and reach the writer through the same RGB path that Layer 2 does, which the bench proves dot for dot.

### 5.1.7 Input, sound, clock (P6, P7, Q10; S7, S8)

- **Keys (source, measured, bench `keys`):** the 8 x 5 matrix is the Spectrum core's key table (`zx_keys.v`, SHIFT alone = CAPS SHIFT), read through the half-row the machine selects (`o_KBD_ROW`, a zero in A15:A8); the 16 extended keys are the C64 keys listed in `zxn_keys.v` (EXTEND = C=, BREAK = RUN/STOP, EDIT = HOME, DELETE = DEL, cursor keys, `;` `"` `,` `.`, TRUE/INV VIDEO = F1/F3, GRAPH = left arrow, CAPS LOCK = CTRL). The bench presses A (C64 matrix 10, half-row 1), Q and P (62, 41), SPACE (60), RETURN (1), SHIFT alone (15) and reads **port `0xFE` of each half-row with the right bit and no other**; BREAK, EDIT, RIGHT, `,` and `.` show in `nextreg 0xB0/0xB1` as the Next's table says (`membrane.vhd` order); the two sticks are Kempston 1 (port `0x1F`, the C64's port 2) and Kempston 2 (`0x37`, port 1), left + fire = `0x12`, up + left = `0x0A`, and swapped by the app's `JOY` register; the Kempston mouse (+5 right, +3 down, left button) reads X 5, Y 0xFD (the Y counter counts the other way), buttons `0x0D`, then right `0x0E`. 34 checks, 0 wrong. `ctl_keys` (the expectation one half-row off) and `mut_keyrow` (the half-row selection rotated) fail.
- **Sound (measured, bench `audio`):** `zxn_audio.v` takes the mixed 13-bit stereo of the machine every 2,112 `cpu_clk` (47.35 kHz), takes a slow DC tracker off, scales it to 24 bits and hands it to the host's audio clock; the pause mutes (exactly 0). The program plays one source at a time for 100 ms: AY 1, 2 and 3 (tone periods 218, 109, 73), the DAC (port `0xFB`, a square wave from a delay loop), the beeper (port `0xFE` bit 4), then silence. Rising zero crossings against what the machine did: AY by `f = machine clock / 16 / 16 / TP` (machine clock measured as master cycles per `cpu_clk`: 28.4339 MHz in the locked +3 timing), DAC and beeper by the CPU's own write rhythm; **all five sources inside two periods of the expectation** (AY 1: 56 periods against 54.7 expected, AY 2 110 / 109.3, AY 3 164 / 163.2, DAC and beeper 93 / 92.3 in 101 ms), **the silence is exactly zero from 52 ms in** (3,179 of 3,179 samples). `ctl_audio` (another frequency expected of AY 3) and `mut_mute` (the mixer's output never reaches the host) fail. The spec's `-DZXN_MUT_AYOFF` (one AY muted inside the original's mixer) is not built: the original's mixer is not ours to cut; `mut_mute` loses every peak instead.
- **Clock (source, measured on the app side):** `zxn_i2c.v` is a DS1307 on the Next's I2C pins whose seven registers the app writes once a second in BCD (seconds, minutes, hours 24 h, weekday 1-7, day, month, year); the app takes the time from the board's real-time clock (`get_fattime` of the file system) and, without one, a fixed date that moves on with the uptime. Not benched in RTL (the NextZXOS reads it, the boot ROM does not): the coreapp test checks the registers written for 2026-10-22 14:05:09 (`test_zxnext`).

### 5.1.8 The application (T104)

`cores/zxnext/app/lib/zxnext.cc` + `zxnext_regs.h` (since T239.5 D15 its own app, no longer the shared one; `cores/zxnext/trx/zxn_regs.v` is the register block): descriptor id `ZXN_`, `.cor` name "ZX Spectrum Next", settings **Joystick ports**, **Frame lock** and **Board issue** (5.1.10), tasks **Reset**, **Eject the SD image**, **F3 50/60 Hz**, **F8 CPU speed**, **NMI Multiface**, file type `.img` with **Mount SD image and reset** and **Mount SD image**, the SD slot as a sector medium on the Amiga's mailbox layout (the base's sector server writes every block the Next writes back into the file, syncing when idle and before a reset), the mouse as a counter sink, one `zxnext.service` op every 2 ms (sector mailbox, idle sync, mouse, the clock once a second). `sectors.cc` logs a request that took 20 ms or more (`[sectors] unit 0: sector N read took M ms`, 20 lines at most). The default image is `roms/zxnext/next.img` on the first root that has it (`/flash`, `/usb0..3`, `/sd`); without one the application says what to do and the machine's boot ROM shows its own error. **The default is looked for until it is found (board, 2026-10-23, release main-f6f3dfdc: "Error initializing SD card!" with the image on /USB1):** the application starts before the stick is mounted and this core has no ROM scan to wait in, so the single look at the start found nothing; `zxnext.service` now repeats it once a second until the image is mounted and then starts the machine over (the boot ROM reads the card once). A choice of the user (Mount, Mount and reset) and an Eject are never overridden; checks in `test_zxnext.cc`, controls `zxn_late*`. **No ROM list** (`roms = []`, `app = "blueprint"`); `zxnext.app` was built from the one blueprint image by `ci/trxapps.sh` and `tools/build_coreapp.sh` (until T239.5 D15: now `cores/zxnext/app`). `FakeIo`/`CoreIo::wall_time` is new (default: no clock). Host-side test `tests/coreapp/test_zxnext.cc` (60 checks, 12 controls) and the golden menu `golden/menu_zxnext.txt`. **The board build of the application was not run here** (the `apps/ultimate` submodule is empty in the worktree; CI builds it) and `get_fattime` as the weak symbol behind `wall_time` is unverified on the board.

### 5.1.9 Benches and what they cost

All in `cores/zxnext/sim/run.sh`, rows in `ci/sims.txt` (`zxnext_boot`, `zxnext_sd`, `zxnext_mem_a`, `zxnext_mem_b`, `zxnext_video`, `zxnext_io`, `zxnext_audio`, `zxnext_boardid`, `zxnext_pins`), dependencies in `ci/sims.deps`. Every control is a run that has to fail; `run.sh` prints `PASS control ...` when it did.

| Scenario | Modes | Controls (all fail as they must) |
|---|---|---|
| S3 memory | `mem_ref0-3`, `mem0-3` | `mut_nowait`, `mut_nowrcache` |
| S4 / S6 Layer 2 and the writer | `video0`, `video1`, `video2` | `ctl_video_shift`, `ctl_video_row`, `mut_late`, `mut_decim` |
| S5 card, S10 boot | `sd`, `boot`, `noimg` | `ctl_sd_lba`, `ctl_sd_drop`, `mut_sdflip`, `mut_sdcrc`, `ctl_boot`, `ctl_noimg` |
| S7 sound | `audio` | `ctl_audio`, `mut_mute` |
| S8 input | `keys` | `ctl_keys`, `mut_keyrow` |
| D10 board id | `boardid`, `boardid0`, `boardid2` | `ctl_boardid`, `mut_boardid` |
| D11 Auto | host test only (no gateware change: the register is D10's) | `zxn_auto_*`, `zxn_fw_*` (5.1.11) |
| S9 clocks and Freeze | `clk1-4`, `freeze_ref`, `freeze` | `ctl_clk`, `mut_nco`, `mut_nohold` |
| S11 contract | `run_pins.sh`, lint | the pin check's own controls |

Each Verilator model is built once under the build lock of the other cores' benches (4.4 MB of Verilog: about 10 minutes and 3 GB), a mutation has a build of its own. Machine time: the benches allow 1.5 s for the boot (`MS=`), `noimg` runs 6.2 s. The memory set is the long one (tens of minutes a run at 3.5 MHz); the CI rows are split in two for that. All benches of this report ran on the builder in `trx64-sim-env:4` under `timeout`.

### 5.1.10 D10 as built (board issue in the menu)

- **The id.** `zxnext.vhd` reported `g_board_issue` in NextReg 0x0F (`port_253b_dat <= "0000" & std_logic_vector(g_board_issue);`). `tools/zxnext_gen.py` now patches two places in its scratch copy (`rtl/` stays as vendored, counted edits "zxnext: port i_BOARD_ID", "zxnext: NextReg 0x0F from i_BOARD_ID"): a new entity input `i_BOARD_ID` (4 bits) and that line reads `"0000" & i_BOARD_ID`. The generic is untouched and still selects every `generate` (SRAM, FDC, core id, ROM/RAM bank: issue 5). `gen/` was converted again by GHDL 6.0.0 (builder, `trx-ghdl6`); the Verilog differs from before by the port and the signal numbering.
- **The mapping.** The generates of `zxnext.vhd` split the generic into 0..1 (`gen_coreid_23`, `g_board_issue <= 1`), 2 (`gen_coreid_45` from 2 on, `gen_romram_234` up to 2) and 3 (`gen_romram_5`, `gen_sram_5`, from 3 on): 3 is issue 5 (the generics of the issue-5 top, and the id the board printed in "Unknown board id 0x03"), 2 is issue 4. The code does not tell issue 2 from issue 3 (both are 0..1); the id is taken as the issue's number minus 2, so 0 is issue 2. Issue 3 has no menu entry (D10 offers 2 / 4 / 5).
- **The register.** `BOARD_ID` at 0x0A3 of the register block (`zxn_regs.v`), read/write, bits 3:0, reset value 3: an application that never writes it reports issue 5 as before. `zxn_core.v` wires it to `i_BOARD_ID`.
- **The application (D10; D11 added Auto as the default, 5.1.11).** Setting **Board issue** = Issue 2 | Issue 4 | Issue 5 (D10: default Issue 5), `WHEN_RESET` with a `RegBind` on `ZXN_BOARD_ID` (values 0, 2, 3; since D11 an operation `zxnext.set_board`): the settings commit of `boot_finish` writes it while the core is held, before the release, and it is stored in the core's `.cfg` like the other settings. A change takes effect at the next Reset; it is not live.
- **Benches.** `zxnext_boardid` (`boardid`: no write, expects 3; `boardid0`, `boardid2`: the bench writes the register while the core is held and `prog/boardid.s` reads NextReg 0x0F through ports 0x243B / 0x253B), controls `ctl_boardid` (wrong expected id) and `mut_boardid` (`-DZXN_MUT_BOARDID`: the register is not wired). Host test `tests/coreapp/test_zxnext.cc` (default 3, "Issue 4" writes 2, "Issue 2" writes 0, a stored choice is read back, the setting is `WHEN_RESET`), controls `zxn_board_id`, `zxn_board_when`; golden `menu_zxnext.txt`.
- **Not run / open.** Not tried on the board: whether the firmware of Alex's image runs with issue 4 (id 2) is S13. Real issue-4 behaviour (keyboard, RTC, ESP per issue) is the firmware's reading of the id; the machine keeps its issue-5 structure.

### 5.1.11 D11 as built (board issue Auto)

- **What the firmware decides** (read from the `TBBLUE.FW` of both of Alex's images, 2026-11-02; the source is not on the builder, so the binaries are the evidence). Both files have the board test three times (file offsets of the NextReg read `3E 0F 01 3B 24 ED 79`: new 0x1732, 0x730A, 0x158D8; old 0x1945, 0x80B7, 0x17FAA). It reads NextReg 0x0F, maps 0xFA to 10, and then `SUB n; RET C` on the id: **new firmware `D6 04 D8` (id < 4 passes: Issue 2..5), old firmware `D6 03 D8` (id < 3 passes: Issue 2..4)**; every other id prints `Unknown board id: 0x%02x` (string at 0x1772 new, 0x198D old; the stop with "Unknown board id 0x03" of D10). The firmware's own board table, which the same file carries three times, has the same size: 37 bytes an entry, `<id> "ZX Next Issue <id+2>" 0 <core file> ...`, ids 0..3 with `core.bit`, `core.bit`, `core2.bit`, `core3.bit` in the new file (id bytes at 0x6090, 0xBE4A, 0x1A38A), ids 0..2 without `core3.bit` in the old file (0x6BAF, 0xD4EB, 0x1D4BB). The new file is 286,720 bytes, the old 304,640; the old one is the larger, so a version number or file size does not order them, the table does. The message Alex quoted ("wrong board revision") is not a string of `TBBLUE.FW`; the stop the old image makes at Issue 5 is "Unknown board id".
- **The rule** (`cores/zxnext/app/lib/zxnext_fw.h`, `zxfw_board_max`): open the mounted image as a FAT card (an MBR with a FAT16/FAT32 partition, or a bare volume), find `TBBLUE.FW` in the root directory (8.3 entry, not deleted, not a directory/label/long-name entry), follow its cluster chain through the FAT and look for entries `id, "ZX Next Issue ", digit '2'..'9', 0` with `id == digit - 2`; the highest id found is what the firmware takes. Auto writes `min(that id, 3)` into `BOARD_ID` (3, because the machine has issue 5's structure and the firmware of a later issue still takes 3). Real files: new -> 3, old -> 2. No file, no FAT card or no table: id 3 (Issue 5) and the notice "Board issue Auto: no TBBLUE.FW in <image>, reported as Issue 5 - choose the issue in the Settings if the machine stops" (or "no board table in TBBLUE.FW"); no image mounted: id 3, no notice of its own (the "No SD image" one stands).
- **When.** Auto is resolved where the setting is applied (`zxnext.set_board`, `WHEN_RESET`): with every start, Reset, "Mount SD image and reset" and the late start-over of the default image, after the image is mounted and while the core is held, so a changed image is read again at the next reset. The log line `[zxnext] board issue Auto: id N (the firmware of <image> takes up to M)` names it. A fixed choice (Issue 2 / 4 / 5) is written as before and is never corrected. The values of the setting are stored as labels: a `.cfg` of before D11 ("Issue 2/4/5") means the same, a fresh one is Auto.
- **Order and geometry.** Values: Auto, Issue 2, Issue 4, Issue 5 (default Auto, golden `menu_zxnext.txt`). The scan reads the image by offset through the open `CoreFile`, FAT sectors on demand and the file in 2 KiB pieces with a 16 byte carry (so an entry that straddles two pieces or two clusters is found); a chain longer than the volume's clusters ends the scan.
- **Proof** (`tests/coreapp/test_zxnext.cc`, 118 checks without the local files; stand-ins that reproduce the table, no image or firmware of the user's in the repository): Auto picks 3 for the table with ids 0..3 and 2 for ids 0..2 (with the text "ZX Next Issue 5" in the file behind a byte that is not 3), on FAT32 and FAT16, behind an MBR and bare, contiguous and scattered (bait clusters between the chain's, a deleted entry, a directory, a volume label, a long name entry and another file of the name's neighbourhood in the root directory); 1 and 0 for firmwares that stop at Issue 3 and Issue 2, 3 for a later firmware (cap); the carry across a 2 KiB piece and a cluster boundary; fallback 3 with the notice for an image without `TBBLUE.FW`, one without a table, and the silent 3 without an image; a fixed Issue 2 / 4 / 5 wins over either firmware; Auto is the default; an old `.cfg` value keeps its meaning. Controls (all fail as they must): `zxn_auto_default`, `zxn_auto_fixed`, `zxn_auto_always5`, `zxn_auto_cap`, `zxn_auto_notice`, `zxn_auto_tablemsg`, `zxn_fw_chain`, `zxn_fw_carry`, `zxn_fw_idmatch`, `zxn_fw_lfn`, `zxn_fw_dir`, `zxn_fw_mbr`, `zxn_fw_fat16`, `zxn_fw_max`, and D10's `zxn_board_id`, `zxn_board_when` adapted. Local only (environment `ZXN_REAL_FW_NEW/OLD`: the two firmware files wrapped in a generated card; `ZXN_REAL_IMG_NEW/OLD`: the first 84.3 MB of the two real cards over FTP, which hold the firmware's last cluster): new -> 3, old -> 2, no notice. The bytes of the firmware files were read into the builder-side scratch only.
- **Not done / open.** Not tried on the board: the old image with id 2 (Issue 4) reaching NextZXOS (the firmware's Issue 4 handling of keyboard, RTC and ESP is its own reading of the id, S13). No gateware change, so the `zxnext_boardid` benches are unchanged. FAT12 and exFAT cards are not read (Auto falls back to Issue 5 with the notice).

## §5.2 Deviations from the spec as decided

1. **Wait by stall, not by an input.** D1/D2 proposed a small patch on the original's `zxnext.vhd` that makes `sram_wait_n` wait; the machine instead stands still as a whole while a line comes in (`zxn_clk`'s `hold`), so the original's files stay unpatched (Q9 "vendored unchanged" holds without a patch) and no CPU cycle sees a late memory. The cost: while one memory access waits, the sound, the picture and the timers wait too (5.1.4 gives the share: 0.2 to 1.6 % of the time in the test program).
2. **Q6's +0.09 to +0.2 % is +0.09 % only in 48K timing** (5.1.3: +1.55 % in 128K and +3, +2.66 % in Pentagon). The lock to the host's PAL frame costs that much speed, and the default timing after the boot ROM is +3.
3. **S6 is Layer 2 and the writer, not the five layers** (5.1.6); and compares with a pattern the program wrote, not with the hash of the original's `o_RGB` stream (stronger for the writer, nothing for the layers themselves).
4. **S7 has no `-DZXN_MUT_AYOFF`**; `mut_mute` is its control (5.1.7).
5. **S9 checks 60 Hz only by arithmetic** (the 60 Hz timings run unlocked at 28.000 MHz): not benched.
6. **Not honoured** (tied off, no function): `o_KBD_CANCEL` (the extended keys also enter the matrix as combinations in the table, so cancelling is not needed), `o_RESET_HARD` (a hard reset the machine asks for is a soft one: no reconfiguration), the mouse DPI setting, `i_SPKEY_FUNCTION` except through the register (`FKEYS`), the Multiface button except through the app's menu entry. §4 holds otherwise: ESP/WiFi, Raspberry Pi GPIO, expansion bus, flash boot, XADC and DNA, HDMI/VGA stages, PS/2 blocks, Sega 6-button read of `i_JOY_*` bits 7-11 (not wired to a host input).
7. **`l2_keep` is not used** (as the other cores that have no constant output merged into GND).
8. **The synthesised glue is not the 7,835 LUT of §5 plus a guess** but 10,502 LUT / 30 tiles (5.1.2): the spec's S2 number is now this one.
9. **Port A reads its arrays from a held capture, one `cpu_clk` later (S12, CI 942).** Source of the failing paths (report of run 942, job 6357): `l2/zxn/zxn/cpu_mod/z80n/n16396_reg[2]_rep_replica_1/C` to `l2/zxn/mem/adat_reg_*/ADDRBWRADDR[4|5]`, requirement 10.000 ns (path group `cpu_mmcm`, one `cpu_clk`), data path 9.695 ns, 12 logic levels, 7.751 ns of it routing. `n16396` is the Z80N's instruction register `ir` (`gen/zxnext.v`: `assign ir = n16396`); the path is `ir` -> address generation and the MMU slots (`sram_A21_A13`, `sram_addr`, `zxnext.vhd`) -> `o_RAM_A_ADDR` -> `aidx()` (`zxn_mem.v`) -> the address pin of the cache's data and tag RAMs. It is a single-cycle path by construction: the destination is glue (`zxn/mem`), and `core_timing.tcl` relaxes only machine-to-machine (`zxn/zxn/*` to `zxn/zxn/*`); a multicycle on it would need `o_RAM_A_ADDR` stable for two clocks, which holds for the flops that change at `r28` (3 or 4 clocks apart) but is not shown for the falling-edge flops that also feed it, and the block RAM would then be read a clock late anyway. The same source already ends in the capture flops `ca_q` (21 bits) without a violation. Fix, `trx/zxn_mem.v`: `ca_q`, `cd_q`, `crd_q` are loaded only at `a_start` (held), `adat`/`atag` are read with `aidx(ca_q)` (`a_go`), and the lookup (`rd_go`, `wr_go`) happens one clock later (`a_go2`). Consumers: (1) `a_di`, the byte of a hit: valid at the third `cpu_clk` after the request, was the second. The machine takes it at the second CPU clock edge after the request at the earliest: `zxnext.vhd` raises `sram_wait_n` for every read cycle at 28 MHz (`sram_req_t = 1 and cpu_rd_n = 0 and cpu_speed = "11"`), and slower speeds have longer CPU cycles; at 28 MHz two master cycles are 6 or 7 `cpu_clk`. (2) `stall`: `a_missing` and `pw_v` are set one clock later, so a master cycle that is 4 `cpu_clk` after the previous may pass before the hold (it could not before); a second request cannot follow within the 6 `cpu_clk` of two master cycles. (3) A write that hits: `adwe`, `adaddr`, `addata` come from the held capture, one clock later; with the request visible in cycle k+1 the write reaches the array at the end of k+4 and the next request's array read (visible no earlier than k+4, read at the end of k+5) comes after it. (4) Misses and the engine (`a_ma <= ca_q`) read the held capture. Benches (builder, `trx64-sim-env:4`, 2026-10-23): `mem_ref0-3` and `mem0-3` PASS, `mem1`/`mem2`/`mem3` equal to the one-cycle SRAM "the final memory image and the 1078 port writes, with the master cycles between them" (7, 14, 28 MHz), `mem0` equal in content; `boot` PASS ("the stub started and left its marker"), `video0` PASS ("every dot of the Layer 2 window is the colour of the pattern"), `sd` PASS. Not rerun here: `mem_a`/`mem_b` controls (`mut_nowait`, `mut_nowrcache`), `video1`/`video2`, `io`, `audio` (the CI runs them); the effect on timing is unproven until the next CI routing.

## §5.3 Open (decisions that are Alex's, and things not done)

- **Frame lock in 128K, +3 and Pentagon (decision):** keep the lock in every timing (the machine +1.55 % / +2.66 % fast, every frame shown once), or lock only 48K and leave the other timings at their own frame (the pixel layer then drops one frame in about 60 at 50 Hz)? The default after the boot ROM is +3.
- **S6 beyond Layer 2 (decision):** a bench for the ULA picture (attributes, border, Timex modes), the tilemap, sprites and the Copper needs an expected picture; the only independent one is the original's own `o_RGB` stream captured next to the writer. Build that, or accept Layer 2 plus the board photo (S13)?
- **Routing and timing (S12)** are the CI's. CI 942 (commit `2895ec1a`) routed the core: WNS -0.401 ns, five paths -0.40 to -0.20 ns, all in the glue (5.2 item 9); the machine-to-machine multicycle-2 of `core_timing.tcl` did not show a path, so its assumption held for that run. The corrected glue is not routed yet.
- **S13 on the board** (NextZXOS from Alex's image) is not run; no image was ever in a repository, a bench or a package.
- The 3.5 MHz memory bench compares content, not times (5.1.4).
