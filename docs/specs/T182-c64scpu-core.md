# T182 — A C64 with a SuperCPU runs as its own core, C64SCPU

**Status:** DECIDED (D1-D17, Alex) and BUILDING, merged to main. Built: steps 1-6 and 8 (fork, 65816, memory map 3c-e with S1/S6 bench `run_map.sh`, 20 MHz speed, mirroring, IRQ/vectors, core app), D17 SD-card drive 1581 (app only), the CMD ROM 1.4 pport fix (§5.11, sim); in the 2026-11-07 tester release as experimental. Board pass 2026-11-10 (Alex, commit 6373ba284): the real SuperCPU ROM boots to READY with 16M RAM. Not built: step 7 (REU DMA halt as VICE D12, 16 MB REU window D11, cartridge types D13, Mount & Run D15; code has no such change: `cores/c64scpu/` REU halt is the C128 fork's `reu_dma_active`) and step 9 (full proof list S1, S8, S12, S13 on the board). The board steps CMD ROM 1.4, S7 (JiffyDOS) and S4 (20 MHz against 1 MHz) are closed as done 2026-11-18 (shipped and in use, Alex; end of §5). 2026-11-15 sync: header rewritten; the old text (PROPOSED, 'steps 3c-e not built') was stale.
**Repo:** TRX64U-MultiCore — a new core `cores/c64scpu/` (gateware), its descriptor in the core app; nothing in the host.
**Number:** T182.
**Origin:** Alex, 2026-11-04 (translated): "check whether we can do the SuperCPU (https://www.c64-wiki.com/wiki/SuperCPU); VICE has SuperCPU";
then "yes, C64SCPU as a core".
**Depends on:** T29 (turbo in the c64u core), T142 (the 65C816 of the SNES core on DDR2).
**Milestone:** M3.

---

## §1 The problem

CMD's SuperCPU is a cartridge with a 65816 at 20 MHz, its own 128 KB RAM, up to 16 MB SuperRAM and its own ROM; software written for it
(native 65816 mode, SuperRAM) does not run on any core we have. What exists today:

- `cores/snes/rtl/65C816/P65C816.vhd` (MiSTer SNES, GPL, TREX-internal): a 65C816, running from DDR2 with 293-693 ns latency (T142 S2).
- `cores/c64u/c64_turbo_c64u.v` (T29): the 6510 at up to 64 CPU cycles per PHI2 cycle on internal RAM and ROM; `$D07A`/`$D07B` speed select,
  `$D0BC` SuperCPU detect only (T29 §1 non-goal: "A 65816 or a SuperCPU memory map").

Parts:
- **P1 CPU:** the 65816 in place of the 6510 (emulation and native mode); the 6510 port `$00`/`$01` outside the CPU.
- **P2 Memory:** SCPU RAM and SuperRAM in DDR2; banks 0/1 and the C64 RAM the VIC sees kept consistent.
- **P3 Registers:** `$D070`-`$D07F`, `$D0B0`-`$D0BF` (speed, optimisation/mirroring modes, hardware registers).
- **P4 ROM:** the SuperCPU ROM (CMD, copyrighted): supplied by the person like the KERNAL, never in a release, `.cor` or package.
- **P5 Timing:** 20 MHz against PHI2 for VIC, CIA and SID access; the I/O and VIC fetches at C64 speed.
- **P6 Area:** the flat design (host + C64 + 65C816 + cache; D7) against the device: LUT, block RAM, WNS. Measured as a synthesis trial in §5.
- **P7 App:** the core's descriptor, settings (speed switch, JiffyDOS, ROM check and the missing-ROM message), media as in the C64 core.
- **P8 Pass-through (D6):** REU and cartridge port behind the 65816: DMA halts the CPU, cartridge accesses are PHI2 bus cycles.
- **P9 JiffyDOS (D5):** the switch bit the ROM reads and the JiffyDOS 1541 ROM in drive A.

## §2 Scenarios

Written 2026-11-04 (step 1, coordinator proposal; D1-D8 are Alex's, everything else in §2/§3 is **proposed, coordinator**). The behaviour oracle is
VICE `scpu64` (GPL, read-only copy outside the repository, cited as `file:line` of VICE trunk, `src/scpu64/`); where VICE is silent or marked FIXME the
scenario says so and the board decides. The SCPU ROM in every bench is a **synthetic test ROM of our own** (65816 code written for the bench,
`cores/c64scpu/sim/rom/`, assembled in the bench, not CMD's); CMD's ROM exists only on the board, on the person's stick. Benches live in
`cores/c64scpu/sim/<name>/run.sh` and are rows of `ci/sims.txt` with lane `large:c64scpu`; "mutation" = a define that breaks the one thing
the scenario proves, the bench must fail with it (the negative control).

```gherkin
Scenario: S1 boot with a ROM
  Given the core is reset with a 128 KiB test ROM whose reset vector (file offset 0xFFFC) points to code that stores 0xA5 to SRAM $00:2000
  When  the bench runs 20,000 PHI2 cycles
  Then  SRAM $00:2000 = 0xA5, the CPU left the boot map ($D0B6 written), and the 65816 fetched its reset vector from the ROM image
  And   with the mutation SCPU_MUT_NOBOOTMAP (ROM not mapped over $8000-$FFFF at reset) $00:2000 stays 0x00 and the bench fails

Scenario: S2 missing ROM (D2)
  Given /SD/cores/c64scpu/roms/ holds no SCPU ROM (and none in the places 12.2.3 of the manual copies from)
  When  the core starts
  Then  the 65816 stays in reset (bench: PC never leaves the reset state, no bus cycle), after about 10 s the screen says "SuperCPU ROM (scpu64.rom) is missing in
        /SD/cores/c64scpu/roms/", and the plain C64 KERNAL never runs
  And   with a correct ROM in the folder the same test starts the CPU within 1 s of the file appearing (the core looks again by itself); a ROM of 100 bytes, or of
        131,072 bytes with a reset vector below $8000, is refused with its own message (negative: a bench ROM of 131,072 bytes and a good vector is accepted)

Scenario: S3 native mode from SuperRAM
  Given the S1 boot, the test program copies 4 KiB of its own code to SuperRAM $02:0000 and executes CLC, XCE, REP #$30, JML $020000
  When  the code in $02:0000 runs 16-bit arithmetic (ADC, MVN across banks $02 to $03, PHA/PLA in the 16-bit stack, a 24-bit indirect long LDA [dp],Y)
  Then  the result block in SuperRAM $03:0000 equals the reference bytes computed by an independent 65816 model (a VICE-free Python model written for the bench) bit for bit
  And   with the mutation SCPU_MUT_EMUONLY (the CPU's E flag forced to 1) the block differs and the bench fails

Scenario: S4 20 MHz / 1 MHz switch and its effect in PHI2 cycles
  Given a loop of 5,000 x (INX, BNE-style 4-cycle body, in SRAM bank 0) and a PHI2 cycle counter in the bench
  When  the loop runs with $D07B written (software 1 MHz off, 20 MHz) and again after $D07A (software 1 MHz on)
  Then  1 MHz mode takes N PHI2 cycles (N within 2 % of 5,000 x the 65816's own cycle count), 20 MHz takes between N/17 and N/20.3 (VICE accumulates 20 MHz against the PHI2
        rate, `scpu64cpu.c:129-131`), $D0B8 reads 0x80/0xC0 and $D0B5 bit 6 follows the speed switch input as in `scpu64mem.c:656-670`
  And   a loop of 1,000 x LDA $D020 (VIC register) takes at least 1,000 PHI2 cycles and at most 1,500 at 20 MHz (every I/O access is a PHI2 cycle, `scpu64cpu.c:178-191`);
        the measured value goes into §5 and becomes the bench's pinned figure
  And   with the mutation SCPU_MUT_NOFAST (the clock enable always PHI2) the ratio is 1.00 and the bench fails

Scenario: S5 the VIC sees writes made at 20 MHz (mirroring)
  Given the reset optimisation mode (0xC7: pages $02-$FF of bank 0 are mirrored into the C64 RAM, `scpu64mem.c:81-86`, index 15, :1480-1483)
  When  the 65816 at 20 MHz writes 0x41 to $0400 (screen RAM) and the VIC fetches the character matrix of that line
  Then  the VIC's fetched byte at $0400 is 0x41 within two PHI2 cycles of the write, plus one per retry (a mirror write the VIC holds off with AEC low is driven again; VICE writes at once and has no retry); the bench reads the picture (VIC matrix fetch) and the C64 RAM
  And   after `$D0B4` selects a mode without mirroring (optim 0x84, table index 10, range 0) the same write does NOT reach the VIC (it still fetches the old byte), and with the
        mutation SCPU_MUT_NOMIRROR the first half already fails; writes to $00-$01FF are never mirrored in mode 15 (`0x02ff`)

Scenario: S6 SuperRAM 16 MiB, size detect, address wrap
  Given the S1 boot and the SCPU's SIMM register at its reset value (`$D078` = 4: 16 MiB, `scpu64mem.c:1527`, :1510-1513)
  When  a program writes the bank number to the first and last byte of every bank $02 to $F5 (244 banks), reads all back, and writes then reads $F6:0000, $F7:FFFF, $F8:0000, $FF:FFFF
  Then  all 244 banks keep their own tag (no alias, no wrap); $F6:0000 .. $F7:FFFF are the SIMM window = SuperRAM bytes 0..$1FFFF (VICE `scpu64mem.c:529-542`: $F6:0000 is SuperRAM
        byte 0, $F7:FFFF byte $1FFFF, not two further banks), reads always, writes only with hwenable (`$D07E`): with it off a write to $F6:0000 changes nothing, with it on the byte
        is SuperRAM byte 0; $F8-$FF ignore writes and read the ROM file repeated over 512 KiB (`scpu64mem.c:544-551`); the tag loop is the memory-size probe (every bank own, 16 MiB)
  And   with the mutation SCPU_MUT_ADDR23 (address bit 23 dropped on the way to the DDR2) banks $82-$F5 alias $02-$75 and the bench fails

Scenario: S7 JiffyDOS switch (D5)
  Given the F8 setting "JiffyDOS" Off, then On
  When  the program reads $D0B5
  Then  bit 7 follows the setting (`scpu64mem.c:656-658`: 1 = Jiffy), and the core app with On + a supplied JiffyDOS 1541 ROM loads it into drive A (the drive's ROM bytes read back equal the file, sha256
        printed), with Off the stock 1541 ROM
  And   On without the file leaves the stock ROM in the drive and shows one line "JiffyDOS 1541 ROM not supplied"; the switch bit still follows (negative: file present, setting Off -> stock ROM)
  And   a LOAD from the drive with JiffyDOS on is faster than off by the factor the person measures with a stopwatch: board step by hand, no bench (a bench would need a JiffyDOS ROM)

Scenario: S8 REU under the SuperCPU
  Given a 512 KiB REU and the 65816 in native mode at 20 MHz
  When  it programs a stash of 16 KiB from SRAM bank 1 to REU and a fetch back to $0800, triggering with a write to $DF01 and once with the $FF00 trigger
  Then  the data equals the source, the 65816 does not move during the transfer (no bus cycle from the CPU between the REU's first and last byte), the REU registers read back as on the C64 core
  And   with the mutation SCPU_MUT_REUNOHALT (CPU not held) the bytes differ and the bench fails. VICE cannot serve as the oracle here (its SCPU REU path is a FIXME, `scpu64mem.c:207-213`); the
        board step is a REU program run with the SuperCPU switched on, by hand

Scenario: S9 interrupts and the vector reroute
  Given the S1 boot, a CIA 1 timer interrupt every 4,000 PHI2 cycles and a handler that counts in SRAM
  When  the CPU runs at 20 MHz in emulation mode and in native mode (vectors at $FFEE/$FFFE and the native table)
  Then  the handler count after 100,000 PHI2 cycles is 25 +- 1 in both modes, and the vector fetch comes from the ROM when `scpu64_interrupt_reroute()` says so (`scpu64mem.c:1749-1752`: kernal-shadow or
        bank-1 map and (native mode or hwenable or sys 1 MHz or dosext or ramlink)), from SRAM otherwise
  And   with the mutation SCPU_MUT_NOREROUTE (always SRAM vectors) the native-mode run hits the wrong handler and fails

Scenario: S10 badlines and BA do not slow SRAM code, they do hold I/O
  Given the S4 SRAM loop with the display off and again with a full text screen (badlines on)
  When  both run at 20 MHz
  Then  the two PHI2 counts differ by at most 2 % (SRAM cycles are not stolen by BA in fast mode, `scpu64mem.c:150-162`), while an I/O loop (LDA $D012, 1,000 times) on the full screen takes the badline-extended time of a 1 MHz 6510 (+-3 %)
  And   in 1 MHz mode the SRAM loop IS stretched by the badlines (negative control inside the scenario)

Scenario: S11 registers $D070-$D07F, $D0B0-$D0BF
  Given hwenable off after reset ($D0B2 = 0x00 after reset, bit 7 = hwenable)
  When  the program writes $D0B4/$D0B3/$D0B8/$D0BC while hwenable is off, then $D07E, then the same writes, then $D07F
  Then  the first set of writes changes nothing (readback equal to reset values), the second set changes exactly what `scpu64mem.c:688-821` says (optim, soft 1 MHz, dosext), $D07F restores the disabled state
  And   the register read of every address $D0B0-$D0BF equals a table extracted from `scpu64_hardware_read` (`scpu64mem.c:634-686`) for four register states (a 64-entry readback table, written once from the VICE source, committed)

Scenario: S12 cartridge behind the SuperCPU (D6)
  Given a 16 KiB CRT (our own test cartridge image, ROML $8000 + ROMH $A000) behind the pass-through
  When  the 65816 in emulation mode at 20 MHz reads and executes code from ROML
  Then  the code runs (marker in SRAM), every ROML/ROMH read is a PHI2-synchronised bus cycle (`scpu64mem.c:1178-1214`), and cartridge RAM writes at $DE00-$DFFF hold the PHI2 sync of an I/O write
  And   an Ultimax cartridge (GAME low, EXROM high) makes the 65816 see the Ultimax map for the CPU's bank-0 accesses as `scpu64meminit.c` (UM entries) says; whether a given real cartridge works is the original's behaviour (not a bench)

Scenario: S13 a real SuperCPU program
  Given our own test program `scpu_demo` (65816 native code in SuperRAM: a 16-bit sieve plus a text screen written at 20 MHz, assembled with 64tass, source and expected output in `cores/c64scpu/sim/prog/`)
  When  it runs on the core with the person's SCPU ROM (board) and, in the bench, with the synthetic test ROM
  Then  the bench's screen dump and result words equal the model; on the board the screen shows the sieve result and the 20 MHz run finishes in a fraction of the 1 MHz run (a stopwatch figure in §5)
  And   third-party SuperCPU software (game or demo images) runs as a board step by hand with images Alex supplies; such images live only in `cores/c64scpu/sim/media/` (never in a release, `.cfw`, package)

Scenario: S14 area and timing (P6)
  Given the full flat design
  When  the CI lane `large-c64scpu` builds it
  Then  routed LUT <= 85 % of 32,600 (27,710), block RAM <= 74 of 75 tiles, WNS >= 0, 0 routing errors (the report lines of `utilization.rpt`, `timing.rpt`, `route_status.rpt`, as the C128's `r_v1`)
  And   a design that misses any of the three is red (the C128 flow's `wns_fails`); the trial figures of §5 say what is expected
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P4 | boot with a ROM | `cores/c64scpu/sim/run_map.sh` s1 (mutations NOBOOTMAP, NOVALID with +ROMVALID=0) | `ci/sims.txt`, lane `large:c64scpu` |
| S2 | P7 | missing-ROM message and ROM check (D2) | `tests/coreapp` descriptor test + `cores/c64scpu/sim/romgate/run.sh` (CPU held) | tier 1 / app tests |
| S3 | P1 | native mode from SuperRAM | `cores/c64scpu/sim/native/run.sh` + Python 65816 model (mutation EMUONLY) | tier 1 |
| S4 | P5 | 20/1 MHz and speed in PHI2 cycles | `cores/c64scpu/sim/speed/run.sh` (mutation NOFAST) | tier 1 |
| S5 | P2 | VIC sees 20 MHz writes (mirroring) | `cores/c64scpu/sim/mirror/run.sh` (mutation NOMIRROR) | tier 1 |
| S6 | P2 | SuperRAM 16 MiB wrap / size detect | `cores/c64scpu/sim/run_map.sh` s6 (mutations ADDR23, NOALIAS) with the DDR2 model of T142 `ddr2c_model.v` (depth parameter) | tier 2 |
| S7 | P9 | JiffyDOS switch + drive ROM | app test (`tests/coreapp`) for the drive ROM; bit 7 in `sim/regs`; load speed by hand on the board | app tests / board session |
| S8 | P8 | REU under the SuperCPU | `cores/c64scpu/sim/reu/run.sh` (mutation REUNOHALT); REU program on the board by hand | tier 2 / board session |
| S9 | P1 | interrupts, vector reroute | `cores/c64scpu/sim/irq/run.sh` (mutation NOREROUTE) | tier 1 |
| S10 | P5 | BA / badlines vs SRAM and I/O | `cores/c64scpu/sim/speed/run.sh` second half | tier 1 |
| S11 | P3 | register file semantics | `cores/c64scpu/sim/regs/run.sh` against the committed 64-entry table | tier 1 |
| S12 | P8 | cartridge behind the SuperCPU | `cores/c64scpu/sim/crt/run.sh` (our CRT); a real cartridge by hand on the board | tier 2 / board session |
| S13 | P1 | a real SuperCPU program | `cores/c64scpu/sim/prog/run.sh` (ours); third-party images by hand on the board with a photo | tier 2 / board session |
| S14 | P6 | area and timing | Vivado `utilization.rpt` / `timing.rpt` of CI lane `large-c64scpu` | CI `large-c64scpu` |

Every part of §1 has a scenario and every scenario solves one part: P1 S3 S9 S13, P2 S5 S6, P3 S11, P4 S1, P5 S4 S10, P6 S14, P7 S2, P8 S8 S12, P9 S7.

## §3 Decisions

- **D1 (Alex, 2026-11-04: "yes, C64SCPU as a core", translated):** a core of its own, `C64SCPU`, not an option in the c64u core.
- **D2 (Alex, 2026-11-04: "a"):** no SuperCPU ROM, no start: the core shows "SuperCPU ROM missing" and does not run; no fallback to the
  plain KERNAL. The ROM is supplied by the person like the KERNAL (P4).
- **D3 (Alex, 2026-11-04: "a"):** SuperRAM is always 16 MB (in DDR2); no size setting.
- **D4 (Alex, 2026-11-04: "a"):** speed as the original: 20 MHz or 1 MHz (`$D07A`/`$D07B` and the cartridge's switch), nothing faster.
- **D5 (Alex, 2026-11-04: "a"):** F8 setting "JiffyDOS" (On/Off) as the cartridge's switch; with On the JiffyDOS 1541 ROM is loaded into the drive
  Amended 2026-11-05 (Alex: "F5"): speed and JiffyDOS sit in the core's F5 Settings tab, as every core's settings do; F8 stays the System page.
  when the person has supplied it.
- **D6 (Alex, 2026-11-04: "a"):** the pass-through port as in the C64 core: REU and cartridges (CRT) behind the SuperCPU; a cartridge that
  does not work with a SuperCPU fails as on the original.
- **D7 (Alex, 2026-11-04: "the C64SCPU should be a large core, I am fairly sure of that", translated):** a LARGE core (T100 D9): host and core
  flat in one bitstream (`cores/c64scpu/mono/`, as `cores/c128/mono/`), delivered as a `.cfw`; no DFX lane.
- **D8 (Alex, 2026-11-05: "let's try to make that as close to the original as possible", translated; T8 a):** the core reproduces the
  original's wait states (VICE's model: SIMM row/cell timing `scpu64cpu.c:285-325`, EPROM 3 cycles `:151-175`, I/O `:178-242`), not
  "a miss costs what the DDR2 costs". The cycle count of a program follows the original's; the DDR2 has to hide behind that budget
  (T5 is redesigned for it). Where a DDR2 access cannot fit the original's budget, the deviation is measured and reported, never silent.
- **D9 (Alex, 2026-11-05: "let's start with P-A", translated; T5 placement):** everything the CPU owns (SCPU SRAM 128 KB, SuperRAM 16 MB, SCPU ROM) lives in DDR2 behind one 8 KB write-through cache; the C64 RAM the VIC reads stays in block RAM. The waits the program sees come from the VICE timing model; a cold miss beyond them holds the CPU, is counted (`DEV_CYC`) and reported. P-B (SRAM bank 0 in block RAM, 72 of 75 tiles) stays the fallback.
- **D10 (Alex, 2026-11-06: "As VICE", translated; step 7 question 1):** REU DMA follows VICE: the bytes go through the CPU's bank-0 map
  into SRAM and, when mirrored, the C64 RAM (`scpu64mem.c:431-455`), not C64-RAM-only.
- **D11 (Alex, 2026-11-06: "B"; step 7 question 2):** the REU offers 16 MB as in the C64 core; the C64 ROM/RAM backing moves out of
  the REU window `0x01000000-0x01FFFFFF` (T10 `ROM_BASE` contract and the step-1 ROM path change with it; not released yet).
- **D12 (Alex, 2026-11-06: "B"; step 7 question 3):** REU DMA halts the CPU as VICE does in fast mode: SRAM code runs on until its
  next access that needs the C64 bus, then waits for the transfer's end; the app's own DMA (Mount & Run) holds the CPU at once. S8 is
  amended to this.
- **D13 (Alex, 2026-11-06: "A"; step 7 question 4):** every cartridge type the C64 core supports (`c64_cart_desc.h`), EasyFlash 3
  and the flash write-back included.
- **D14 (Alex, 2026-11-06: "A"; step 7 question 5):** the REU and cartridge blocks already in the build (icepi REU, descriptor
  cartridge, no Gideon VHDL) stay; step 7 closes their gaps (5.9) instead of new VICE-modelled RTL.
- **D15 (Alex, 2026-11-06: "A"; step 7 question 6):** step 7 is complete only with REU, cartridge and the app's Mount & Run (the
  app's DMA with the CPU held), all three in one step.
- **D16 (Alex, 2026-11-06: "B"; found by the step-7a bench):** `vendor/icepi/c64_reu.v:309` starts a SWAP in `ST_READ_REU`, so byte 0
  writes a stale C64 byte to the REU. The fixed copy goes to `cores/common/` and is used by C64SCPU and the C128; the pinned vendor file
  (host + `c64ref`, `contract/host_pin.txt`) stays unchanged, the host is not rebuilt.
- **D17 (Alex, 2026-11-07: "A", translated; "but I want to see SCPU tomorrow"):** the 2026-11-07 tester release carries the C64SCPU core as
  it is on `main` (steps 1-6 and 8: 65816 at 20 MHz, SuperRAM, mirroring, IRQ/vectors, speed switch, JiffyDOS, drive), marked experimental;
  the release notes say REU, cartridges and Mount & Run are missing and come with step 7, which continues on `t182-step7`. The user supplies
  the SuperCPU ROM in `/SD/cores/c64scpu/roms/`.

### Technical proposals (step 1, 2026-11-04; **proposed, coordinator** - none of this is an Alex decision until it is recorded as D8 and up)

**T1 Base.** Fork `cores/c128/` into `cores/c64scpu/` the way the C128 was derived from `cores/c64u` (`cores/c128/PROVENANCE.md`: copies of the c64u files, cut down and extended; the unchanged
chips read in place from `vendor/icepi`), with the C128's own flat flow: `mono/core.toml` (`delivery = "cfw"`, `app = "blueprint"`, `mono_env`), `mono/build_mono.tcl`, `mono/top.v` + `mono/host/` (the core's own copy of
the host files, T164 3a Q1), lane `large-c64scpu` found by `ci/plan.py universe()` from `cores/c64scpu/mono/core.toml` with no further CI edit, `ci/bitstream_mono.sh --lane c64scpu`. Kept from the C128 tree:
Kawari VIC, CIAs, SID block (one SID, `SID_REDIP`; the second one only if the budget of S14 allows), drive A, tape, REU, cartridge slot and its DDR2 CRT path, the sockets' audio, the analogue A/V port
(`c128_avout.v`), the DDR2 ROM line cache pattern (`c128_romc.v` + `c128_ddrx.v`). Cut: VDC (`vdc/`, `c128_pixw.v`), Z80, MMU 8722, `c128_map/x/regs/keys/rgbi/joyswap`, the 8502 wrapper
`cpu6510_c128.v`, the C128's 2 MHz bus cycles. Lost against forking `cores/c64u` itself: that tree is the DFX layer 2 (the 247-port contract, the turbo block, UDP stream blocks) and would need the mono top
the C128 already has; D7 makes the DFX tree worthless for this core. **Finding for the fork:** `cores/c128/c64_top_c128.v:617-655` declares `rom_hold` / `rom_wait_q` only under `ifdef Z80`, so ROM_CACHE without Z80 does not elaborate
(`ERROR: [Synth 8-36] 'rom_hold' is not declared`, builder, 2026-11-04): the fork moves the declaration out of the `ifdef`.

**T2 CPU.** The 65C816 of the SNES core (`cores/snes/rtl/65C816/`: `P65C816.vhd`, `MCode.vhd`, `ALU.vhd`, `AddrGen.vhd`, `AddSubBCD.vhd`, `BCDAdder.vhd`, `P65816_pkg.vhd`; MiSTer, GPL-3.0) as an own copy under
`cores/c64scpu/cpu/` (nothing in `cores/snes` is edited), read into its own VHDL library (`ALU`/`MCode`/`AddrGen` meet the 6502n's `alu` in `work`; the trial does it with library `s816`). One edit: an `EF` output
(the emulation flag is an internal signal today, `P65C816.vhd:37`); `$D0B6` bit 7 and the vector reroute of `scpu64mem.c:1749` need it. The CPU runs on `sid2x_clk` (63.0556 MHz, = 64 x the PAL PHI2 of 985,248 Hz) with a clock enable; every halt
(PHI2 sync, DDR2 miss, REU DMA, BA, freeze) is a gated `CE`, not `RDY_IN` (the 65816 completes write cycles under RDY, which is wrong for DMA and freeze). Like the T142 SNES it needs a multicycle timing recipe for the
paths that start at `A_OUT` (CE every 3.15 clocks at 20 MHz); no timing number exists yet (the trial is synthesis only). The 65816 has no undocumented 6510 opcodes - the original has none either (nothing to emulate).

**T3 6510 port.** `$00`/`$01` live outside the CPU (`scpu64mem.c:234-302`): reads of `$00`/`$01` come from the port registers (`dir`, `data`), writes go to the port (data masked `& 7` for the memory configuration,
`:277`) AND to SRAM; a 24-bit access to bank 0 `$0000`/`$0001` does not reach the port (`:504-514`, `:553-556`). The port lines (datasette motor/write/sense, bit 3-5) drive the existing tape pins; the fall-off timer of the 6510
(`FALL_OFF_CYCLES`) is kept. New small module `scpu_pport` (the port code of `cpu6510_c128.v`, taken out of the CPU wrapper); `pport[2:0]` is index bits 2:0 of the configuration table.

**T4 Memory map and DDR2.** CPU view (24 bit), after `scpu64mem.c:478-570` and `scpu64meminit.c:56-257`:

| CPU address | What | Where it lives |
|---|---|---|
| `$00:0000-$01:FFFF` | SRAM, 128 KiB, banks 0 and 1; which pages are SRAM, ROM, I/O, colour RAM, cartridge or the internal C64 RAM is a 256-entry configuration table x 10 areas, index `{boot, dos, hw, !game, !exrom, loram, hiram, charen}` (`scpu64meminit.c:45-52`, areas `:73-84`, entries `:86-257`) | DDR2 behind the CPU cache |
| internal C64 RAM `RC` (configuration entries 224-255, `scpu64meminit.c:103-104`) and what the VIC sees | 64 KiB | block RAM (16 tiles), written by the mirror path (T6) and by the C64-side bus |
| `$02:0000-$F5:FFFF` | SuperRAM (D3: always 16 MiB; `$D078` SIMM register reads/writes as in VICE but the size is fixed) | DDR2 behind the cache |
| `$F6:0000-$F7:FFFF` | alias of the first 128 KiB of SuperRAM (`scpu64mem.c:529-542`) | same DDR2 bytes |
| `$F8:0000-$FF:FFFF` | the ROM image mirrored up to 512 KiB, writes ignored (`:544-551`, `scpu64rom.c:85-87`); at reset the boot map also shows it over `$8000-$FFFF` of bank 0 (`scpu64mem.c:1399`, config bit 7) | DDR2 behind the cache (read only) |

DDR2 users in this repository and the bytes they hold (128 MB board; sources: [T20-delta-ring.md](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T20-delta-ring.md) 5.1, `docs/specs/T142-snes.md` "DDR2 space", `cores/c128/mono/host/cart_fill.v:18-29`, `reu_ddr_map.v`,
`cores/c128/c128_ddrx.v:13-14`): the core application (code, data, bss, heap) `0x00000000-0x00E8FFFF` (`linker.x`, 14.56 MiB, the 9.3 MB bss is its own choice); drive A's image `0x0EE0000`, cartridge RAM `0x0EF0000`, the built-in
cartridge slot `0x0F00000-0x0FFFFFF`; the expansion-port block `0x01000000-0x01FFFFFF` (the REU's 16 MiB or the CRT image: `ROM_BASE_BLOCK`, "the app never has REU and a cartridge on together"; the C128 puts its system ROM at
`0x01C00000` inside this block, so its REU is smaller than 16 MiB); Gideon's legacy ROM base `0x03C00000-0x03FFFFFF` (`ROM_SEL` 0, not used by our app); the T20 ring `0x04000000-0x07FFFFFF` (TRX64 core only; other cores must not use
it). Nothing in this table is "free": a range is only usable when no row above names it and the app's linker map agrees. **Proposed placement, all outside every row above:** SuperRAM `0x02000000-0x02FFFFFF` (16 MiB, bank n at
`0x02000000 + n * 0x10000`; the app can reach it for tests and a later save state), SCPU SRAM `0x03000000-0x0301FFFF` (128 KiB), SCPU ROM `0x03080000` with the file's length (the mirroring is address arithmetic `& (size - 1)`, not
copies). Bytes in use: 16 MiB + 128 KiB + 128 KiB (ROM at its maximum 512 KiB window: 0x03080000-0x030FFFFF); the rest of `0x02000000-0x03BFFFFF` stays unassigned and is not offered to anyone by this spec.

**T5 Memory stores, wait states and the DDR2 (redesigned for D8, 2026-11-05, step 3; coordinator proposal, the placement is Alex's to decide).**
*Principle.* Two layers that do not mix. (1) The **timing model** is a pure function of the access stream (class of the access, address, the previous SIMM cell, the write buffer) that says how many 20 MHz cycles the CPU's clock enable is withheld; it is a transcription of `scpu64cpu.c:129-325`
(step 4 builds it; the accumulator is in 1/20,000,000 units against the PHI2 rate) and is the only source of waits the program may see. (2) The **data path** (block RAM, or a DDR2 line cache) has to have the byte ready when the model lets the CPU continue. Data that is late is a deviation from the original:
it holds the CPU (a gated `CE`, T2), is counted (`DEV_CYC`, a status counter) and reported, never silent (D8). The VIC never fetches from the DDR2 (gateware skill: only the CPU may wait): the C64 RAM the VIC reads stays in block RAM.
*Numbers used.* DDR2 latency of the host port, measured: 293-693 ns (T142 S2, `ddr2c_model` LAT 22..52 ui clocks of 13.3 ns); one 20 MHz cycle = 50 ns, so **a miss costs 5.9-13.9 cycles** (6-14), at 1 MHz it is 0.3-0.7 of a PHI2 cycle. One RAMB36 = 4 KiB (as 4K x 9) = 4,096 data bytes.
| store (CPU address) | original's wait states in 20 MHz mode (VICE) | what a DDR2 miss adds | block RAM if it lived there |
|---|---|---|---|
| SCPU SRAM, 128 KiB, banks 0/1 | read: none (`mem_read2` case 0, no stretch); write: `scpu64_clock_write_stretch` makes the write buffer busy for 1 cycle (+1 if the accumulator is past the cycle), only the next write or I/O access waits for it (`scpu64cpu.c:176-185`, `wait_buffer`) | 5.9-13.9 cycles on every cold or capacity miss, against 0 in the original | **32 RAMB36** (64 KiB of bank 0 alone: 16) |
| SuperRAM, 16 MiB (`$02:0000-$F7:FFFF`, `$F6/$F7` alias SIMM bytes 0..0x1FFFF) | read: same 4-byte cell 0, next cell 0, same row +1, other row +2.5 cycles (row = 8 KiB default, `simm_row_mask`); write: same row +1, other row +2 (`scpu64cpu.c:285-325`; a write does not go through the write buffer) | a miss replaces the original's 0 / 1 / 2.5 / 1 / 2: **+3.4-13.9** on a row change, +4.9-13.9 on a same-row read | 4,096 RAMB36: DDR2 only |
| SCPU ROM, 64-256 KiB (file image repeated to 512 KiB) | read: +3 cycles (`scpu64_clock_read_stretch_eprom`, `accu += 3 x diff`), write: +3 (ignored write) | the original's +3 covers 150 ns of the 293-693 ns: net **+2.9-10.9** if the fetch starts with the address | **32** for 128 KiB (64 for 256) |
| C64 RAM the VIC reads (`RC`, 64 KiB; CPU access to it is an I/O-class access, PHI2 synchronous) | the I/O classes: read `accu = 11.5 M`, CIA write 16.8 M, long write 17.6 M (`scpu64cpu.c:107-150`, step 4) - synchronous to PHI2, the DDR2 is not involved | not applicable: the VIC reads block RAM | 16 today (4 slots x 4: `c64scpu_ramd.v`, 64 KiB backed by 128 KiB DDR2) |
*Block RAM today* (step 2 build: 56 of 75 RAMB36-equivalent tiles; the only per-module report kept is step 1's flat `mono/reports/s1`, 53 tiles; the step-2 hierarchical report was not kept, so the table is read from the RTL, array by array, the rest is the difference): `c64scpu_ramd` slots 4 x 2 x (8192 x 8 bit) = **16**, its CPU line cache 512 x 136 bit = **2**; `c64scpu_romc` line cache 512 x 133 bit = **2**; `c64scpu_chr` 4096 x 8 = **1**; colour RAM 1024 x 4 = 0.5; 65C816 microcode ROM 2048 x 53 = **3** (step 2, +3 against step 1); together 24.5; the other 31.5 are the host (`ult` about 10, `hout` about 6, measured for the C128 in 5.1), the drive's RAM and ROM, SID, VIC, CIA and tape buffers, not attributed per module. **Free: 19 tiles.** What could be freed by the new design: romc 2 + ramd CPU cache 2 (both replaced by one CPU cache, 4 tiles for 8 KiB in 128 bit lanes with byte enables), i.e. nothing net; the 16 VIC slots cannot go (rule above).
*Proposal, three placements for Alex (none is built):*
**P-A, everything the CPU owns in DDR2 behind one 8 KiB write-through line cache** (cache 4 tiles replacing the 4 above, so the total stays 56): SRAM, SuperRAM and ROM streams run at the DDR2's pace after a miss; the program sees +6-14 cycles per cold line, the model's waits sit on top. Deviation counted. Cheapest in block RAM, furthest from D8 at 20 MHz. At 1 MHz it is hidden (a miss is 0.3-0.7 of a PHI2 cycle; a fetch started with the address at `phi2_l` arrives before `phi2_n` except in the worst case 693 ns + synchronisers against 720 ns, which then repeats the read as the ROM cache does today).
**P-B, SRAM bank 0 in block RAM (16 tiles, total 72 of 75, S14 limit 74), rest in DDR2** (bank 1, SuperRAM, ROM behind the cache of P-A): zero page, stack, screen and most 20 MHz code and data in bank 0 never miss, exactly the original's zero wait; code in bank 1, SuperRAM and the ROM (KERNAL/BASIC calls run from the ROM!) can miss. Routing risk at 96 % of the tiles.
**P-C, P-B plus the hot ROM** (the 32 KiB `$8000-$FFFF` boot-map window of the ROM, 8 tiles, total 80): does not fit (75 tiles). Not offered.
*Prefetch (all placements):* the model's waits give time: a ROM or SIMM access starts its line fetch with the address (the CPU's `CE` is withheld by the model anyway) and every miss also fetches the next line; this hides streaming code and block moves but not the first touch of a line. *What stays impossible:* a cold miss in SRAM (P-A) or in bank 1/SuperRAM/ROM (P-B) at 20 MHz costs more than the original; the deviation is the DDR2 latency minus the model's wait (table), measured by `DEV_CYC` on the board and reported in §5.
*DDR2 placement* (T4's table is unchanged and checked against `docs/layering-map.md`, `T20-delta-ring.md` 5.1, `T142-snes.md` "DDR2 space", `cores/c128/mono/host/cart_fill.v`): SuperRAM `0x02000000-0x02FFFFFF`, SCPU SRAM backing `0x03000000-0x0301FFFF`, SCPU ROM `0x03080000-0x030FFFFF` (T10 `SROM_BASE`); the application's windows are `0x01C00000-0x01C03FFF` (C64 ROMs) and the ROM at `0x03080000`; the expansion-port block ends at `0x01FFFFFF`, the T20 ring (`0x04000000-0x07FFFFFF`, TRX64 only) starts above: no overlap. The ROM at `0x03080000` is above SuperRAM (`0x02000000 + 16 MiB` ends at `0x02FFFFFF`) and above the SRAM backing; `0x03100000-0x03BFFFFF` stays unassigned. **Superseded text:** the first T5 (a plain 8 KiB cache, misses cost what the DDR2 costs) is replaced by the above by D8.

**T6 Registers and timing.** Register set `$D070-$D07F` / `$D0B0-$D0BF`, behaviour exactly `scpu64mem.c:634-821` (reset values `:1518-1532`: optim `0xC7`, SIMM 4, bootmap 1, hwenable 0): `$D071` no-op, `$D072`/`$D073` system 1 MHz on/off,
`$D074-$D077` optimisation mode (only with hwenable), `$D078` SIMM, `$D07A` soft 1 MHz on, `$D079`/`$D07B` off, `$D07E` hwenable on, `$D07D`/`$D07F` off, `$D0B0` version (`0x40` = v2), `$D0B2` hwenable + system 1 MHz, `$D0B3`/`$D0B4` optim, `$D0B5`
switches (bit 7 Jiffy, bit 6 1 MHz), `$D0B6` emulation flag / bootmap off, `$D0B7` bootmap on, `$D0B8` soft 1 MHz, `$D0BC`-`$D0BF` DOS extension; `fast = !(sys1MHz | soft1MHz | (switch1MHz & !hwenable))` (`:702`, `:760`). The c64u core's turbo
registers `$D07A/$D07B` and the `$D0BC` detect (T29 §1) are replaced, not combined (`NO_TURBO`). Three cycle classes: (a) SRAM, SuperRAM, ROM: one CPU cycle on a cache hit; (b) I/O, colour RAM, cartridge ROML/ROMH: the access waits for the PHI2
phase of the C64 bus (the existing 6510 bus path through `c64_bus_arbiter`: address and data held for one PHI2 cycle, `din` taken at the PHI2 fall), the CPU resumes at the VICE-measured phase (`scpu64cpu.c:178-242`, accumulator set to 11.5 / 16.8 / 17.6 M of 20 M -
the exact phase is a bench value, S4); (c) the internal C64 RAM `RC`: a block RAM access, one cycle. In 20 MHz mode BA never stops class (a) (`scpu64mem.c:150-162`); in 1 MHz mode every cycle is a (b) cycle and BA/AEC rules are the C64's.
The CE accumulator gives 20 MHz against the real PHI2 rate (13/41 per `sid2x_clk` edge = 19.99 MHz). **Mirroring** (S5): a write to bank 0 whose page lies in the active mirror range also goes to the internal C64 RAM; the 16-entry range table is
`scpu64mem.c:81-86`, the index `:1480-1483`; the second write port of the internal RAM is a 4-entry queue arbitrated against the C64-side bus writes (a block RAM keeps one write port: the "second write turns it into flip-flops" trap of the gateware skill).

**T7 ROM file and check (D2).** One file `scpu64.rom` (accepted names `scpu64.rom|scpu.rom|supercpu.rom|scpu64-v2.04.rom`, a list the app owns), 65,536, 131,072 or 262,144 bytes - VICE takes any power of two from 64 KiB to 512 KiB
(`scpu64mem.h:39-40`, `scpu64rom.c:80-84`); the 2.04 image is 128 KiB [E, from the usual dumps, the first file Alex supplies decides]. Layout: the file as is; byte `k` is ROM address `k` and the image is repeated to fill 512 KiB; bank-0 boot map `$8000-$FFFF` shows file bytes
`0x8000-0xFFFF`, so the reset vector is file offset `0xFFFC`. The VICE loader also copies file bytes `0x4100-0x60FF` as the "kernal trap" RAM (`scpu64rom.c:89`); the C64 KERNAL/BASIC the CPU sees are SCPU-ROM and SRAM contents, so no separate KERNAL/BASIC file is needed;
the VIC's character ROM is the C64 one (file `characters.901225-01`, as the `c64ref` descriptor lists it). The check in `coreapp/cores/c64scpu.cc` (pattern of `coreapp/cores/c128.cc:112-146`): size in the set, the 16-bit reset vector at offset `0xFFFC` is `>= 0x8000`,
the image is not all `0x00`/`0xFF`; then the image is written through the DDR2 window at `SCPU_ROM_BASE`, read back, the ROM cache flushed (`ROM_CTRL`), only then is the CPU released from reset. A missing or refused file: the generic held-core mechanism of
manual 12.2.4 with the table row `{ "Commodore 64 + SuperCPU", "c64scpu", "SuperCPU ROM", "scpu64.rom|...", { 131072, 65536, 262144 }, 1, 0 }` in `coreapp/src/table.cc` (so the screen says "SuperCPU ROM ... is missing in /SD/cores/c64scpu/roms/").
No ROM byte enters the repository, the CI or a release; the benches use the synthetic ROM of §2.

**T8 Open for Alex (technical, one at a time when it is his turn).** (a) [decided: D8] Is "a cache miss costs what the DDR2 costs" (T5) enough, i.e. SuperRAM-streaming programs run slower than on the original, or must the core reproduce the original's SIMM/EPROM wait states for
timing-sensitive 20 MHz software (decides whether the cache can be a plain line cache or needs a cycle model)? (b) The ROM name list and the three accepted sizes of T7 are the coordinator's reading of VICE; the first file Alex supplies decides.

**T9 Core application.** `cores/c64scpu/mono/core.toml` `app = "blueprint"` (the generic application, as the C128), descriptor `coreapp/cores/c64scpu.cc` + `c64scpu_regs.h`. F8 settings: "JiffyDOS" On/Off (D5: the `$D0B5` bit 7 input; with On
the app loads the supplied JiffyDOS 1541 ROM into drive A, table row optional, flags 0), "SuperCPU speed" 20 MHz / 1 MHz (D4: the cartridge's switch, `$D0B5` bit 6), plus the C64 core's own items (SID, joysticks, drive A, tape, REU size, CRT). Host-side registers (the core's
IO page, after `c128_regs.v`): `SCPU_ROM_BASE`, `ROM_CTRL` (flush, image valid / release reset), `SCPU_SW` (speed switch, jiffy switch), `STATUS` (rom ok, fast, emulation, hwenable), counters (cache hits/misses, hold clocks - for S4/S14).

**T10 ROM and register contract between the application and the gateware (step 8, 2026-11-05; coordinator proposal, written with the application that implements its side: `coreapp/cores/c64scpu_regs.h`, `coreapp/cores/c64scpu.cc`).**
The gateware side (`cores/c64scpu/c64scpu_regs.v`, steps 2-4) implements exactly this; a change is made in both and in this table. All offsets are in register page E of the core window (`0x101AE000 + off`, byte registers, 32 bit values little endian, absent offsets read 0). The offsets up to `0x0A5` are step 1's and unchanged.

| Off | Name | Access | Contract |
|---|---|---|---|
| `0x000-003` | ID | R | `S C P U` (checked against the descriptor by `tools/check_core_id.py`) |
| `0x004` | CAPS | R | `0x01` step 1's page; `0x02` with the registers below (the application does not read it; it reads `SROM_BASE`) |
| `0x030` | STATUS | R | `b3` held (Freeze), `b4` PAL (step 1); new: `b0` SuperCPU ROM valid (= `ROM_VALID`), `b1` 20 MHz in effect, `b2` emulation mode (E flag) |
| `0x040` | ROM_CTRL | W/R | W `b0`: flush the ROM line caches, the C64 image's **and** the SuperCPU ROM's; R `b0`: a flush is running (the application waits at most 100 ms) |
| `0x050` | ROM_BASE | R | `0x01C00000`: the C64 ROM image in the DDR2 (step 1's path): BASIC at `+0x0000` (8,192 bytes), KERNAL at `+0x2000` (8,192 bytes); window offset `0x00C00000` |
| `0x090-092` | CHR_ADDR / CHR_DATA | RW | the character ROM store (4,096 bytes), as step 1 |
| `0x054` | SROM_BASE | R | new: `0x03080000`, the DDR2 byte address of the SuperCPU ROM (T4's placement); a 512 KiB window; window offset for the application `0x02080000` (the application refuses to write when the register reads anything else) |
| `0x058` | SROM_LOG2 | RW `b4:0` | new: log2 of the file length, 16, 17 or 18 (64, 128, 256 KiB). The CPU's ROM view is `file[k & ((1 << SROM_LOG2) - 1)]` for `k` up to 512 KiB (the image repeated); the reset vector is file offset `0xFFFC`. Not touched by COLD |
| `0x05C` | ROM_VALID | RW `b0` | new: 1 = the image in the DDR2 is complete, read back and checked. Reset 0, **not touched by COLD** (the image stays in the DDR2). With 0 the 65816 is held in reset: no bus cycle of its own, no PHI2 access. The application writes 0 first, then the image, then the flush, then 1 |
| `0x060` | SCPU_SW | RW | new: `b0` the speed switch (1 = 20 MHz, 0 = 1 MHz: `$D0B5` bit 6 follows it), `b1` the JiffyDOS switch (1 = JiffyDOS: `$D0B5` bit 7 follows it). Live. Reset and COLD value `0x01`. The application writes it with every start, every setting change and after COLD |
| `0x064` | DEV_CYC | R, 32 bit | new: CPU enables withheld because the CPU's own memory (DDR2 behind the cache) was not ready, saturating, from reset (D8: the deviation from the original, which has no such wait). Unit: clk2x clocks (15.9 ns) in which a due enable is withheld at 20 MHz (one per clock, the enable stays due), enables at 1 MHz (one clk2x pulse each); it is a count of waits, not of time. Sampled twice in the register clock domain, so a read is never torn |
| `0x068` | CACHE_HITS | R, 32 bit | new: CPU reads of SRAM / SuperRAM / ROM answered by the 8 KiB cache (D9) |
| `0x06C` | CACHE_MISSES | R, 32 bit | new: line fetches the cache started (reads that were not in the cache; a read the CPU has to wait for is counted here once) |
| `0x024` | JOY_SWAP | RW `b0` | step 1's, unchanged |
| `0x080` | COLD | W/R | step 1's, unchanged; also puts `SCPU_SW` back to `0x01` |

DDR2 use by the application: `0x01C00000-0x01C03FFF` the C64 ROMs, `0x03080000-` the SuperCPU ROM (up to 256 KiB written; the window is 512 KiB, the rest is not touched); the application's DDR2 window reaches these as `0x01000000 + off` (the window is the DDR2 itself, one address space from the application's side: **to be confirmed on the board**, the read-back of the write is the check). Nothing else is written by the application (SuperRAM and SRAM are the gateware's alone).
Sequence of a start: hold; scan; per file the `put` operation (SuperCPU ROM first: `ROM_VALID` 0, `SROM_LOG2`, image, read-back, flush, `ROM_VALID` 1; BASIC and KERNAL into the window, flush; CHAR through `CHR_*`, read-back); settings (`SCPU_SW`); release. The descriptor's release reads `ROM_VALID` once more and does not let the machine go when it is 0 (the second lock of S2). The generic start (`boot_start`) does not reach the release without every required file.
Decisions of the coordinator in this step, each a proposal until Alex confirms: (a) **BASIC, KERNAL and CHAR are required files** (Alex's list on the card has them, and step 1's gateware ROM path reads them); T7 said no separate KERNAL/BASIC file is needed once the SuperCPU ROM is the CPU's ROM: when step 3 stops using the C64 image's BASIC/KERNAL, they become optional (`required` false in the descriptor, one line); CHAR stays required (the VIC's glyphs). (b) the ROM name list of T7 is used as written; the JiffyDOS 1541 ROM names are `jiffydos-1541.bin`, `jiffydos1541.bin`, `jiffydos_1541.bin`, `jiffydos-1541-ii.bin`, `jiffydos1541-ii.bin`, `jiffydos.bin` (16 or 32 KiB, optional; Alex has none yet, so the name is a guess to be fixed with the first file). (c) The two settings are the core's F5 "Settings" tab items "SuperCPU speed" (20 MHz default, 1 MHz) and "JiffyDOS" (Off default, On) of the generic menu, not an F8 item (the generic application has no per-core F8 entries; F8 is the host's). (d) With "JiffyDOS" On and the file there, drive A gets the JiffyDOS ROM; with Off, or On without the file, the stock 1541 ROM when supplied (the line "JiffyDOS 1541 ROM not supplied" is the answer to the setting; the switch bit follows the setting either way). The drive holds one ROM at a time; a change of the setting rewrites it (the drive's own reset is the person's: "Reset Drive").

## §4 Out

Proposed, to be confirmed with Alex one item at a time (none of it is decided; nothing here is out by omission):

- Anything faster than 20 MHz (D4), a SuperCPU 1.x memory model (`scpu64_version_v2 = 0` paths in VICE), the SuperCPU's own display/LED hardware.
- RAMLink and the CMD HD ports: the `ramlink` flag and the `$DF7E`/`$DF7F` registers exist in VICE (`scpu64mem.c:1158-1169`, `$D0BC` bit 6) and are mirrored as flag bits only; no RAMLink device.
- Save states and the T20 ring (TRX64 only); the freeze menu is the generic one (a gated CE gives the exact resume).
- A CMD ROM, a JiffyDOS KERNAL or any ROM image in the repository, a release or the `.cfw`.

## §5 Build notes

The design step (2026-11-04) is the design in §2/§3 and the area trial below; the build starts with step 1 of the plan (2026-11-05, §5.3). The trial is in `cores/c64scpu/trial/` (not under `mono/`: a `mono/` folder marks a shipping large core for the lint, T161) (`scpu_trial.v`, `scpu_cpu816.vhd`, `build_trial.tcl`,
`build_trial_lib.tcl`, `ooc.tcl`, `cells.tcl`, `run_trial.sh`; the reports are in `reports/`); it is NOT the core; it builds `cores/c128` and stays as the record of the measurement (`run_trial.sh` runs from `cores/c64scpu/trial/`).

### 5.1 Area trial (synthesis only)

**What was run.** Vivado 2024.1 (`vivado-env:2024.1`, container `t182-synth`, builder, under `/cache/heavy.lock`, `nice -n 15`), `xc7a50tfgg484-1`, `synth_design` only (no place, no route: a routed C128 run takes
an hour or more of the shared lock, and the question was whether it is worth planning at all). The base is the C128 flat build (`cores/c128/mono/build_mono.tcl`, patched as text by `build_trial.tcl`) in four variants;
the SuperCPU block `scpu_trial_top` is instantiated inside `layer2` next to the C128's 8502 (which stays, so the figures contain both CPUs; section 5.2 takes the 8502 out again). **What the block contains:**
the 65C816 (`cores/snes/rtl/65C816`, read into library `s816`), a direct-mapped 512 x 16 byte cache (8 KiB, 128 bit wide block RAM, byte-lane writes), a 4-entry write queue, the `$D07x/$D0Bx` registers with VICE's semantics,
the 6510 port, the mirror-range decision, the 20 MHz / PHI2 clock enable, a stand-in memory-configuration decode; around it `scpu_trial_stub` (random data, a DDR2 that answers after 10 clocks), whose LUT/FF are counted separately and left out below.
Wired so that synthesis keeps it: the block's outputs are folded into 32 `DONT_TOUCH` flip-flops. **What it cannot say:** timing (nothing was placed), the real configuration table, the C64-side bus adaptor, the mirror write queue
into the C64 RAM, the app-facing registers - those are an allowance (5.2).

**Whole design against the device** (`xc7a50t`: 32,600 LUT, 65,200 FF, 75 block RAM tiles, 120 DSP; the report line is the one of `reports/build_trial_<v>/utilization_synth.rpt`):

| Variant | What it is | Slice LUTs | Slice Registers | Block RAM Tile | DSP |
|---|---|---|---|---|---|
| **C** | the C128 as shipped (`core.toml` `mono_env`: VDC, Z80, tape), no trial: calibration | 28,394 (87.10 %) `\| Slice LUTs* \| 28394 \| 0 \| 0 \| 32600 \| 87.10 \|` | 29,012 | 70.5 | 30 |
| **B** | C128 without VDC and tape, Z80 kept (see below): the base | 24,870 (76.29 %) `\| 24870 \| ... \| 76.29 \|` | 25,000 | 53.5 | 30 |
| **S** | B + the SuperCPU block, microcode ROM as LUTs (the file as it is) | 26,695 (81.89 %) `\| 26695 \| ... \| 81.89 \|` | 25,518 | 56 | 30 |
| **S2** | B + the block, microcode ROM as block RAM (one ROM per record field) | 26,227 (80.45 %) `\| 26227 \| ... \| 80.45 \|` | 25,465 | 64 | 30 |

Calibration: C is the shipped C128; its last routed report in the tree (`cores/c128/mono/reports/b_r6`, quoted in T142) has 28,451 LUT / 27,409 FF / 73 tiles; the synthesis figure is within 0.2 % in LUT and 3 tiles
below in block RAM (the DSP count 30 vs 12 differs and is not explained here; b_r6 is a different branch state of the same variant). So a synthesis LUT figure here is a good routed LUT figure; tiles are about 3 low.
The Z80 stays in B/S/S2 because `c64_top_c128.v:617-655` declares `rom_hold` / `rom_wait_q` inside `ifdef Z80` (T3 finding); its cost is read off the hierarchy and taken out in 5.2.

**Per top-level module** (`utilization_hier_synth.rpt`; LUT / FF / tiles, RAMB18 counted as half):

| Module | S (LUT ROM) | S2 (block ROM) | Note |
|---|---|---|---|
| `scpu_t` the block, without the stub | 1,764 / 378 / 2.5 | 1,296 / 325 / 10.5 | stub: 60 LUT / 140 FF, not counted |
| of it `cpu816` (the 65C816) | 1,703 / 269 / 0 | 1,235 / 216 / 8 | alone, out of context: 1,391 / 270 / 0 (`ooc816_lut`) and 913 / 217 / 8 (`ooc816_block`) |
| of it cache + registers + port + mirror + queue + clock enable | about 60 / 109 / 2.5 | about 61 / 109 / 2.5 | in the design Vivado puts most of this logic's LUTs under the 65C816's name; out of context the whole `scpu_cpu` is 1,714 LUT / 346 FF / 2.5 tiles (`ooc_lut`) and 1,242 / 293 / 10.5 (`ooc_block`), i.e. **glue about 323-329 LUT / 76 FF / 2.5 tiles** (Yosys on the same file: 374 LUT, 91 FF) |
| `l2` (`layer2`) without the block | 14,183 | 14,183 | `l2` with the block: 16,007 (S) |
| `ult` (the host's RISC-V system, `NO_NET`) | 4,389 / 3,601 / 10 | same | |
| `hout` (HDMI out, pix_layer, scaler) | 3,346 / 4,778 / 6 | same | |
| Z80 `z80_0` (`cpu_z80`) | 1,547 / 389 / 0 | same | goes in the real core |
| 8502 `cpu6510_0` (`cpu6510_c128`) | 589 / 215 / 0 | same | goes in the real core |
| `c64_mem_c128d` (the C128's RAM path: slots, cache, ROM cache) | 1,191 / 870 / 26.5 | same | replaced (T4) |
| MMU + `c128_map` + `c128_x` | 56 + 16 LUT | same | go |

**The microcode ROM is the surprise.** `MCode.vhd` is a 2048-entry table of a 16-field record; read as it is, it becomes LUT ROM (Vivado: `M_TAB[0][ALUCtrl] | 2048x5 | LUT`, ... in `reports` log lines) and costs about 470 LUT.
With `rom_style = "block"` and the asynchronous reset of the output register removed (`scpu_mcode_block` in `build_trial_lib.tcl`), Vivado builds **one block RAM per record field, 16 RAMB18 = 8 tiles**, and saves only 468 LUT (S2 vs S). The SNES core gets 3 tiles from its GHDL-generated
Verilog, which has the table as one 53-bit array (`cores/snes/gen/snes.v:7816`, `rom_style = "block"`, T142 D3). So the design item for the real core is **one flat 2048 x 53 ROM (3 tiles, or 1.5 as RAMB18)**; the trial measured both extremes
(0 tiles / +470 LUT and 8 tiles / -470 LUT) and the single array is between them in tiles and equal to S2 in LUT.

### 5.2 What this means for the real core (estimate, not measurement)

Real core = B - Z80 - 8502 - MMU/map/x + the block (with the flat ROM) + the glue the trial does not have:

| | LUT | FF | Block RAM tiles |
|---|---|---|---|
| S2 (above) | 26,227 | 25,465 | 64 |
| minus the stub | -60 | -140 | 0 |
| minus Z80, 8502, MMU + map + x | -2,208 | -604 (389 + 215) | 0 |
| microcode ROM as one 53 bit array instead of 16 fields | 0 | 0 | -5 (8 -> 3) |
| the C128 RAM path (`c64_mem_c128d`, 26.5 tiles) replaced by 64 KiB C64 RAM + the trial's cache (in the block already) | about 0 | about 0 | -10.5 (26.5 -> 16) |
| allowance for what the trial leaves out (configuration table 256 x 10, bus adaptor to the 6510 path, mirror queue, app registers, ROM-valid gate, freeze) | +600 to +1,000 | +300 | +1 |
| **Estimate** | **about 24,600 - 25,000 (75.5 - 76.7 %)** | **about 25,000 (38 %)** | **about 50 of 75** |

Against the budget of S14 (<= 85 % = 27,710 LUT, <= 74 tiles): about 2,700 LUT and 24 tiles of margin; the C128 routed at 85.69 % (`r_v1`) and 87.27 % (`b_r6`) and the T142 SNES at 64.9 %. **The estimate is only as good as the allowance and the assumption that the 65C816 closes timing at a clock enable every 3.15 clocks of
`sid2x_clk`; neither is measured.** Block RAM is not binding for this core (unlike the C128), LUT is not either; the risk is timing (the 65816's `A_OUT` -> cache tag/data -> `D_IN` path, T2) and the placer's density near 77 %.

Not done: place and route of any variant (the trial answers "does it fit", not "does it close"), a Vivado run of the full block with the flat ROM, a bench of any scenario.

### 5.3 Build step 1: the C64 with its 6510 as `cores/c64scpu` (2026-11-05)

**Built.** `cores/c64scpu/` is a fork of `cores/c128/` (provenance and the file-by-file list: `cores/c64scpu/PROVENANCE.md`): the flat large-core tree `mono/` (`core.toml`, `build_mono.tcl`, `build.sh`,
`top.v`, the A/V port, `host/` as its own copy of the host files, `stubs/`), the core files (`c64_top_`, `c64_core_`, `c64_bus_arbiter_`, `c64_mem_`, `layer2_`, `vicii_`, `c64scpu_ramd/romc/ddrx/chr/regs/joyswap`), the boot bench `sim/`,
`tools/gen_c64scpu_test.py`, `tools/c64scpu_rom_image.py`. `experimental = true`, `delivery = "cfw"`; `ci/plan.py` finds the lane `large-c64scpu` from `cores/c64scpu/mono/core.toml`; the job `large-c64scpu` in
`.gitea/workflows/pipeline.yml` is generated (`ci/lanes.py write`); the bench is a row of `ci/sims.txt` (`c64scpu_base`, lane `large:c64scpu`). **Cut:** the VDC (`vdc/`, `c128_pixw.v`, `c128_rgbi.v`), the Z80 (`z80/`), the MMU 8722, the C128 address map
(`c128_map.v`, `c128_x.v`), the C128 keys layer, the 8502 wrapper `cpu6510_c128.v` (the CPU is `vendor/icepi/cpu6510.v` in place), the 2 MHz bus cycles, the VIC-IIe registers, the extra matrix columns and the colour RAM bank bit; the register page
keeps the offsets of the kept registers (`c64scpu_regs.v`, identity `SCPU`). **Fixed in the copy (T1 finding):** `rom_wait_q` / `rom_hold` are declared outside the `ifdef Z80` (`c64_top_c64scpu.v`), so the ROM cache elaborates without the Z80.

**Decided by the coordinator in this step, not by Alex (each is a proposal until recorded as D8 and up):**
- The RAM path stays the C128's DDR2 path (slot cache for the VIC, line cache for the CPU; `c64scpu_ramd.v`, `c64scpu_ddrx.v`), 64 KiB used, instead of 64 KiB of block RAM. Reason: it is the proven path of the fork and step 3 replaces the memory anyway (T4); the 16 block RAM tiles T4 plans for the internal C64 RAM are not spent here.
- The character ROM store is cut to one 4 KiB set (one RAMB36), the register page to the kept registers; the mutation hooks that the C128's benches use (`RAMD_MUT_*`, `ROMC_MUT_*`, `DDRX_MUT_*`, `AVOUT_MUT_*`) stay in those leaf modules, their benches (`cores/c128/sim/`) are not carried.
- Not in `bug.yml`, not in a release (Alex 2026-11-05, translated: "yes, I want to see it locally"): `cores/c64scpu/mono/core.toml` has `build_only = true`; `tools/check_issue_forms.py` leaves a `build_only` large core out of the form, `tools/release_mc.sh` does not pack it. The Gitea package keeps `c64scpu.cor` for the board.
- `build_mono.tcl` sets `general.maxThreads` to 1 for `synth_design` and back to the library's value after it (see the build below).
- No logo, no `coreapp` descriptor, no `README.md` table row (step 8; the core is not offered to testers). The Gitea package carries a `c64scpu.cor` without descriptor (blueprint app), for the board only.
- `cores/c64scpu/trial/run_trial.sh` pointed one directory too far up since the move out of `mono/` (2026-11-05); fixed (`cd .../../../host`).

**Proof (a): the boot bench** (`sh cores/c64scpu/sim/run_base.sh`, `trx64-sim-env` on the builder, Verilator 5.053; the whole machine `c64_core_c64scpu` with the 6510, VIC, CIAs, SID, the ROM line cache and the RAM in a DDR2 model; a synthetic KERNAL/BASIC of our own,
`tools/gen_c64scpu_test.py`, no ROM byte). The program clears the screen, sets the VIC up, writes the banner from the KERNAL, jumps through the BASIC vector at `$A000` and writes `READY.`. The bench checks the RAM bytes of `READY.` at `$04C8` (read from the DDR2 model) **and the VIC's own pixels** of
frame 3 (`tb +PIXDUMP`, read back as text through the character ROM by `sim/check_screen.py`). Report lines (builder, `base.log` of the run):
```
cycles: 98637, frames: 5
ok   ram[04c8] = 12 ... ok   ram[04cd] = 2e            (R E A D Y .)
romc|90266|15|18|42|bad 0                              (ROM line cache: 90266 hits, 15 misses, no wrong answer)
PASS tb_c64scpu
pic| frame 504 x 312 positions, text area x 137..456 y 51..250 = 320 x 200
pic|  1     **** COMMODORE 64 BASIC V2 ****
pic|  3  64K RAM SYSTEM  38911 BASIC BYTES FREE
pic|  5 READY.
PASS check_screen: 1000 of 1000 cells of the picture are the screen codes of the program
```
Negative controls (must fail): `ctl_nobasic` (the BASIC vector leads to an idle loop: the KERNAL's banner stays, the second line and `READY.` do not come) fails both checks (`FAIL check_screen: 38 of 1000 cells differ`), `ctl_nochars` (blank character ROM) passes the RAM check and fails the picture check (`1000 of 1000 cells differ`). **With the real C64 ROMs**
(local, never in CI: `C64_ROMDIR=... sh cores/c64scpu/sim/run_real.sh`, the files of `~/.trx64/roms` on the builder, deleted after the run): the real KERNAL's RAM test, reset and BASIC's cold start run through the core's ROM path in 3.55 s of simulated time:
```
cycles: 3551131, frames: 180
scr|    **** COMMODORE 64 BASIC V2 ****
scr| 64K RAM SYSTEM  38911 BASIC BYTES FREE
scr|READY.
romc|2278117|260|267|44|bad 0
PASS check_screen: 1000 of 1000 cells of the picture are the screen codes of the program
PASS run_real: BASIC V2 banner and READY. in RAM and in the picture
```

**Proof (b): the flat build** (builder, `vivado-env:2024.1`, `xc7a50tfgg484-1`, under `/cache/heavy.lock`, `nice -n 15`; the Tcl of the lane: `vivado -mode batch -source ../cores/c64scpu/mono/build_mono.tcl` from `host/` with the `mono_env` of `core.toml`, as `ci/bitstream_mono.sh` runs it;
the tree is the one of this commit - commit `41016e5ff` minus a `set_param synth.maxThreads 1` line that did not help, see the finding below; the reports are in `cores/c64scpu/mono/reports/s1/`). Report lines:
```
utilization_synth.rpt   | Slice LUTs*  | 23496 | ... | 72.07 |   Slice Registers | 24550 | ... | 37.65 |   Block RAM Tile | 53 | ... | 70.67 |   DSPs | 30 | ... | 25.00 |
utilization.rpt         | Slice LUTs   | 21958 | ... | 67.36 |   (LUT as Logic 21171, as Memory 787)   Slice Registers | 22095 | ... | 33.89 |   Block RAM Tile | 53 | ... | 70.67 |   DSPs | 30 |
timing.rpt              WNS 0.167  TNS 0.000 (0 failing of 52485)  WHS 0.053  "All user specified timing constraints are met."
route_status.rpt        # of nets with routing errors: 0    # of fully routed nets: 41435 of 41435
log                     === impl pass 1 (ExtraTimingOpt): WNS 0.167 ns   ...   === bitstream: build_s1_route/c64scpu_mono.bit 1682809 bytes
```
Against §5.2: LUT 21,958 routed (the estimate was 24,600-25,000 synthesised, 23,496 synthesised here), block RAM 53 tiles (estimate about 50), WNS closed in the first pass at the CPU's 1 MHz bus cycle. S14's budget (<= 85 % LUT, <= 74 tiles, WNS >= 0, 0 routing errors) holds for this step; the 65816, its cache and
the configuration table are still to come, so this is the base, not the answer to S14.

**Finding, not understood: `cores/c64scpu/mono/build.sh` hangs.** Run as `flock /lock/heavy.lock nice timeout 3h cores/c64scpu/mono/build.sh` in the container, Vivado 2024.1 stopped after the line `Synthesis finished with 0 errors` in 5 of 5 runs (as user and as root; with `synth.maxThreads 1`
and with `general.maxThreads 1`): every thread of `vivado` waits on a futex, a forked `[vivado]` child is a zombie, the heavy lock stays held. The same Tcl started as `vivado -mode batch -source ../cores/c64scpu/mono/build_mono.tcl` (what the CI does) finished synthesis in 4 minutes in 3 of 3 runs, and the trial variant B
(`cores/c128`, same flow) too. `cores/c128/mono/build.sh`, from which this script is copied, was not run. Until it is understood, build with the direct `vivado` call or the CI lane, not with `mono/build.sh`.

**Not done in step 1:** the 65816 and everything after it (steps 2-9); the register-page layout (`c64scpu_regs.v`) has no application side (`coreapp/cores/c64scpu.cc`, `table.cc`, a logo, `README.md`); the CI lane `large-c64scpu` itself was not run (the push is the coordinator's); the tape and the REU of the `mono_env` variant are built but not
exercised by the bench (the bench defaults are `NO_REU`, one SID block, no tape: `C64SCPU_SIM_DEFS` selects another set); no board run (the bitstream is not loaded; Alex's rule: nothing new on the board before T94 delivers one host with all cores).

### 5.4 Build step 2: the 65C816 in place of the 6510, emulation mode, 1 MHz (2026-11-05, branch `t182-step2`)

**Built (sub-step a, the CPU in the machine).** Everything below is a **coordinator proposal**, none of it is an Alex decision.
- `cores/c64scpu/cpu/` is the core's own copy of the MiSTer 65C816, **generated** by `tools/scpu_vendor816.py` from `cores/snes/rtl/65C816` (`--check` keeps the copy on the script). Changes: entity names prefixed `s816_`
  (the 6502n's `alu` and the 65C816's `ALU` meet in library `work`; the trial used a second library, one library is simpler for ghdl, Verilator and the Vivado file lists); ports spelled in lower case (the Verilog
  that instantiates the entity and the ghdl netlist use one spelling); a new output `e_out` = the emulation flag; every `case` over all binary values that ends in `when others => null;` ends in `when others =>`
  (the generator of the SNES core does the same for GHDL, `tools/snes_gen.py exhaustive_cases`, imported here); **the microcode ROM is one flat 2048 x 53 array** with a registered output and no reset on it, plus a
  registered "last row was the fetch row" bit in place of the reset value of the old output register (same behaviour, `rom_style = "block"`).
- `cores/c64scpu/scpu_pport.v`: the 6510 port `$00/$01` taken out of `vendor/icepi/cpu6510.v` (direction, latch, the floating bits 3/6/7 with the fall-off timer, bit 5, the app's DMA write while the CPU is held), without
  the turbo and state-bus paths.
- `cores/c64scpu/cpu65816_c64scpu.v`: the bus adaptor, port-compatible with the 6510 wrapper so `c64_top_c64scpu.v` changes in one instance (`cpu_0`; `ifdef C64SCPU_CPU6510` keeps the step-1 6510 for the reference
  bench). One CPU step per PHI2 cycle (clock enable `g_edge && phi2_l` on `clk2x`, the edge the 6510 core took); `D_IN` = the byte the bus left at the PHI2 fall; RDY/IRQ/NMI sampled at the PHI2 fall;
  **RDY holds read cycles only** (`RDY_IN = rdy | write`: the s816_cpu stops its state machine on RDY low even in a write cycle, the 6510 and the C64's BA rule finish writes); **a cycle with neither VDA nor VPA is not a bus access**:
  it is shown as a read of `$0000` (the 6510 would have made a dummy read; the original SuperCPU does not drive the bus then); the 24-bit address is cut to 16 bit; NMI acknowledge (keyboard RESTORE toggle) = the
  `$FFFA` vector pull; no state bus (registers read 0), no turbo.

**Proof (a): the step-1 boot bench with the 65816** (`sh cores/c64scpu/sim/run_base.sh`, builder, `trx64-sim-env:4`, unchanged bench and synthetic ROM; the CPU in the machine is now the 65C816): `PASS+PASS boot`, `READY.` in RAM (`ok ram[04c8..04cd]`) and
in the VIC's picture (`PASS check_screen: 1000 of 1000 cells`), `romc|89117|15|17|37|bad 0` (step 1 with the 6510: 90266 hits, 15 misses - the bus traffic differs, so the other CPU is really in); both controls fail as before
(`ctl_nobasic`: RAM FAIL, picture FAIL; `ctl_nochars`: RAM PASS, picture FAIL); `PASS run_base`.

**Proof (b): the emulation-mode bench** (`sh cores/c64scpu/sim/run_emu.sh`, row `c64scpu_emu` of `ci/sims.txt`; three Verilator builds of the machine: 65816, 6510 (`C64SCPU_CPU6510`), mutation (`SCPU_MUT_SLOW`)).
The program is ours (`tools/gen_c64scpu_emu.py`, no ROM byte): R1 with documented NMOS opcodes only (table fill, 16 bit sum with carry, shifts/rotates in memory, stack and JSR, `(zp),Y`, indexed reads across a
page, a taken branch across a page), R2 with what the 65C02 and the 65816 in emulation mode add (STZ, PHX/PHY/PLX/PLY, INC A/DEC A, TSB/TRB, `(zp)`, BIT #, BRA, `SEC; XCE` leaving E = 1). Both run twice (the second pass is
measured, warm caches) and write a marker byte first and last (`$0B00-$0B03`; the bench prints the PHI2 cycle of every marker write). The oracle is a Python interpreter in the same file with the documented cycle table (NMOS =
65C02 = 65816 emulation for every instruction used; RMW with index and decimal mode are left out on purpose because they differ), run over the same ROM bytes from the reset vector. Report lines:
```
ram| 52 RAM bytes equal the model's
cyc| R1 second pass: 902 PHI2 cycles (model 902)
cyc| R2 second pass: 219 PHI2 cycles (model 219)
cyc| R1 second pass of .../ref6510/log: 902                      (the 6510 machine, same routine)
PASS check_emu: the PHI2 cycles between the markers are the model's
PASS+PASS emu (RAM bytes PASS, PHI2 cycles against the model and the 6510 PASS)
PASS the 6510 reference takes the model's cycles in R1
```
Negative controls (each must fail, all do): `ctl_slow` (the 65816 takes a step in every second PHI2 cycle): `R1 1804 (model 902)`, `R2 438 (model 219)`, `FAIL check_emu`; `ctl_emu_wrong` (SBC for ADC in R1's sum
loop, expectation of the real program): `FAIL: ram[0811] = 0d, expected 00`; `ctl_6510` (the program on the 6510 machine): `FAIL: ram[0830] = 77, expected 00` (no STZ) - the RAM check tells a 65C02 from an NMOS CPU. `PASS run_emu`.

**Proof (c): the flat build routed** (builder, `vivado-env:2024.1`, `xc7a50tfgg484-1`, under `/cache/heavy.lock`, `nice -n 15`; the Tcl of the lane started directly, not `mono/build.sh` - it did not hang this time; tree of commit `f6f7cf9bf`; the reports stayed in the scratch
directory of the run, `host/build_mono_c64scpu/`). Report lines:
```
utilization_synth.rpt   | Slice LUTs* | 23631 | ... | 72.49 |   Slice Registers | 24636 |   Block RAM Tile | 56 | ... | 74.67 |   DSPs | 30 |
utilization.rpt         | Slice LUTs  | 22339 | ... | 68.52 |   (LUT as Logic 21552, as Memory 787)   Slice Registers | 22183 | ... | 34.02 |   Block RAM Tile | 56 | ... | 74.67 |   DSPs | 30 |
timing.rpt              WNS -2.175  TNS -120.585 (91 failing of 52923)  WHS 0.051
route_status.rpt        # of nets with routing errors: 0    # of fully routed nets: 42126
log                     === impl pass 1 (ExtraTimingOpt): WNS -2.175 ns   === impl pass (AltSpreadLogic_high): WNS -2.231 ns   === kept: routed_pass1.dcp
```
Against step 1 (21,958 LUT, 22,095 FF, 53 tiles, WNS +0.167): **+381 LUT, +88 FF, +3 tiles** (the flat microcode ROM is 3 tiles as planned: `ROMQ_reg_0..2` RAMB36, `2048x53 Block RAM` in the synthesis log; the 65C816 is 985 LUT in the hierarchy report, the 6502n it replaces 739).
**Timing is not closed: WNS -2.175 ns.** The worst path is inside the CPU: `s816_mcode/ROMQ_reg_2` (RAMB36) to `cpu/P_reg[1]/D`, 23 logic levels, 17.33 ns of data path against the 15.859 ns period of `sid2x_mmcm` (clk2x); the next four are
the ROM to `A_reg`, `X_reg`, `Y_reg` and `PCr_reg/CE`; the same registers are also the endpoints of the paths from the VIC's `irst_reg`. All 91 failing endpoints are registers of `s816_cpu`. **Cause:** `s816_cpu` is timed as a single clk2x cycle although its clock enable is
`g_edge && phi2_l` (once per PHI2 cycle, 63 clk2x cycles at 1 MHz, 3.15 at 20 MHz). The 6502n of step 1 was short enough for one cycle; the 65C816 is not. This is the multicycle recipe that T2 names ("a multicycle timing recipe for the paths that start at `A_OUT`");
it is **not built in step 2** (open point 1). The sim is not affected (no timing in Verilator).

**Sub-step e: the enable-spacing assertion and its control** (coordinator decision: multicycle recipe of T2 with N = 3). `tb_c64scpu.v` (every run of `run_base.sh` and `run_emu.sh`; not in the 6510 build) watches `cpu_0.core_ce` on every clk2x edge and fails when two enables are closer than 3 clk2x
cycles, and when no enable was seen at all. Report line `ce|98637 CPU enables, smallest gap 64 clk2x cycles (must be >= 3), 0 too close` (`run_base.sh boot`), `ce|19723 ... smallest gap 64 ... 0 too close` (`run_emu.sh`). Control `ctl_ce2` (`SCPU_MUT_CE2`: a second enable 2 clk2x cycles after each one,
a fourth Verilator build): `FAIL ce gap: two CPU enables 2 clk2x cycle(s) apart at 6249000 (minimum 3)`, `ce|39446 CPU enables, smallest gap 2 ... 19723 too close`, `FAIL tb_c64scpu`; `ctl| control ctl_ce2: the enable-spacing assertion fires as it must`. Rerun after the change: `PASS run_base` (boot, both
controls fail as before), `PASS run_emu` (all four controls fail as they must). **Step 4's 20 MHz enable (3.15 clk2x cycles per CPU cycle) must keep the gap >= 3**, else the constraint of sub-step f is wrong, not only the bench red.

**Sub-step f: what the CPU's enable gates, the multicycle recipe, and the flat build with it** (coordinator decision: the T2 recipe, N = 3; read from the RTL, not from comments).
*The enable.* `cpu65816_c64scpu.v`: `core_ce = g_edge && phi2_l` on `clk2x`. `phi2_l` (`vicii_c64scpu.v`) is the pulse ~280 ns after the PHI2 fall and `clk_phi` is low then; `g_edge` (`c64_turbo_c64u.v`) picks one of the 2 clk2x cycles of a gclk cycle; at 1 MHz the bench measures 64 clk2x
cycles between enables (sub-step e asserts >= 3). Inside `s816_cpu` the working enable is `EN = RDY_IN and CE and not WAIExec and not STPExec` (`s816_cpu.vhd:81`).

*Which registers change only on the enable* (every clocked process of `s816_cpu`, `s816_mcode`, `s816_addrgen`; `s816_alu`, the BCD adders and the package have none):
| registers | gated by | only on the enable? |
|---|---|---|
| `STATE`, `IR`, `P`, `T`/`DR`/`D`/`PBR`/`DBR`, `PCr`/`PCOffset`, `AAL`/`AAH`/`AB`/`DL`/`DH`/`AAHCarry`/`SavedCarry`, `ROMQ` (RAMB36, enable pin) and `ST0` | `EN` | yes |
| `OLD_NMI_N`, `NMI_SYNC`, `RDY_IN_DELAYED` | `CE` | yes |
| `IsResetInterrupt`, `IsNMIInterrupt`, `IsIRQInterrupt`, `GotInterrupt`, `WAIExec`, `STPExec` | `RDY_IN and CE` / `EN` | yes |
| `A`, `X`, `Y`, `SP`, `oldXF` | `EN`, **except** the first branch `if IR = x"FB" and P(0) = '1' and MC.LOAD_P = "101"` (XCE into native mode) | **no**: that branch writes the constants `X(15:8)`, `Y(15:8)` <= 0, `SP(15:8)` <= 1, `oldXF` <= 1 in every clk2x cycle in which its condition holds, enable or not. The condition is `IR`, `P(0)` and `MC` (from `ROMQ`), all changed only on the enable, and short (no ALU), but the multicycle constraint cannot check that it has settled by the first capturing edge. Open point 6 |
| asynchronous reset `RST_N` (= `rst_q`) of all of them | not an enable path | left at one period (recovery/removal) |

*What enters and leaves `cpu_0/cpu`* (`cpu65816_c64scpu.v`, `c64_top_c64scpu.v`) and what the build does with it (`build_mono.tcl`; the register sets are logged: cpu 221, sampled inputs 11, port 116, consumers 25):
| path | other end | qualified by the same enable? | constraint |
|---|---|---|---|
| CPU register -> CPU register (incl. `ROMQ`; `WAIExec`, `STPExec`, `RDY_IN_DELAYED` -> `EN` -> `CE` pins) | `s816_cpu` | yes | setup 3 / hold 2 |
| `d_in` <- `din_r`; `rdy_in` <- `rdy_r`; `irq_n` <- `irq_r`; `nmi_n` <- `nmi_r` (clk, loaded at `phi2_n`) | wrapper registers | no, loaded ~280 ns (17+ clk2x) before `phi2_l` | setup 3 / hold 2 |
| `d_in` <- `port_rdata` (`scpu_pport` registers) | `scpu_pport` | no: written while `clk_phi` is high, at `phi2_n`, or by the app's DMA at `phi2_n` with the CPU held | setup 3 / hold 2 |
| `a_out`, `d_out`, `we` -> `scpu_pport` (`access_io`, `wdata`, `port_wr`) | `scpu_pport` | no: captures only with `clk_phi` high / at `phi2_n` | setup 3 / hold 2 |
| `a_out` -> `pc_q` | wrapper | yes (`core_ce && rdy_in && fetch`) | setup 3 / hold 2 |
| `a_out`, `d_out`, `we` -> `addr_a`, `dout_a`, `we_a` (clk, every cycle) | arbiter input stage | no, the bus uses them from `phi2_p`, >= 200 ns later | setup 3 / hold 2 |
| live `rdy` / `irq` / `nmi` (`clk_phi ? x : x_r`) | VIC / core registers | no; `clk_phi` is low at the enable so the sampled arm is the one used | **not covered**; `cpu_rdy` keeps its T29 exception of 2 periods |
| `a_out` -> other readers of `cpu_addr` (SID read address `ra`, `sock_r`), `nmi_ack` | other core registers | not tied to the enable | **not covered**, one period |
| `g_edge`, `phi2_l` -> `core_ce` -> every `CE` pin | turbo / VIC registers | this is the enable itself | **not covered**, one period (the recipe stands on it) |

*The constraint* (`cores/c64scpu/mono/build_mono.tcl`, after `opt_design`, scoped by the `cpu_rdy` net's hierarchy; five `-from/-to` pairs: cpu->cpu, sampled->cpu, port->cpu, cpu->port, cpu->{`pc_q`,`addr_a`,`we_a`,`dout_a`}):
```
set_multicycle_path 3 -setup -end -from $from -to $to
set_multicycle_path 2 -hold  -end -from $from -to $to
```
`$from`/`$to` are `get_cells -quiet -hier -filter {IS_SEQUENTIAL && NAME =~ <pfx>cpu_0/cpu/*}` and the like; the Tcl stops with an error when a set is nearly empty. Not a false path.

*Re-derived for the 20 MHz enable (step 4, 2026-11-05; not the 1 MHz enable of step 2).* At 20 MHz `core_ce` is `tick && ok`, not `g_edge && phi2_l`; the bench asserts the smallest gap 3 clk2x clocks (measured 3, 0 too close, `ce|` lines of run_speed/run_map/run_emu), so cpu -> cpu stays setup 3 / hold 2. Table of the paths that changed: (1) `din_r`/`rdy_r`/`irq_r`/`nmi_r` (loaded at the clk edge ending the `n_ev` clock) and the port registers -> CPU: an I/O cycle's enable comes in the clock after `n_d[1]`, `n_ev` + 2 clocks, so these paths have **2 periods, hold 1** (they had 3 when the enable followed a `bus_ok` register); the paths are only sensitised in RK_BUS / port cycles, which are the PHI2-synchronous ones (`sync_w`); every other cycle's data comes from `mem_q`/`int_q`. (2) CPU -> `scpu_mem` registers, class-table and cache BRAM addresses: 2 / 1 (sampled in every clk2x, decided from `settled`, 2 clocks after the enable). (3) `scpu_mem` registers and BRAM -> CPU (`core_ce` at the CE pins, `mem_q`/`int_q`/`cls` into the data mux): one period, not covered (`smm -> cpu` removed). (4) `scpu_mem` -> C64 bus: `fwd` = `bus_up && ...` is combinational from `arm_go` (class lookup, `ph`, `n_d`) into `addr`/`we` and the arbiter's `addr_a` stage: one period, not covered. The old `port -> smem` 3 / 2 stays. `mono/build_mono.tcl` is changed accordingly (the `smp_r`/`port_r` -> `cpu_r` pairs in their own `foreach`, 2 / 1).

**Proof (f): the flat build with the constraint** (builder, `vivado-env:2024.1`, under `/cache/heavy.lock`, `nice -n 15`, the Tcl started directly; same tree as (c) plus sub-steps e and f; one pass, the second placement was not needed). Report lines:
```
log                     === T182 T2 multicycle: cpu regs 221, sampled-input regs 11, port regs 116, consumer regs 25   === impl pass 1 (ExtraTimingOpt): WNS 0.026 ns
timing.rpt              WNS 0.026  TNS 0.000 (0 failing of 52918)  WHS 0.051  THS 0.000   "All user specified timing constraints are met."
utilization.rpt         Slice LUTs 22186 (68.06 %)   Slice Registers 22184   Block RAM Tile 56 (74.67 %)   DSPs 30
route_status.rpt        # of nets with routing errors: 0    # of fully routed nets: 42023
```
Against (c): WNS -2.175 -> +0.026, TNS -120.585 -> 0, LUT 22,339 -> 22,186 (placement noise, the logic is the same), 56 tiles unchanged. The margin is 26 ps: read it as met, not as comfortable. Step 4 (20 MHz enable, more logic around the CPU) must rerun it.

**Open points of step 2 (coordinator proposals, for Alex when it is his turn):**
1. Timing: **done in sub-step f** (coordinator decision, N = 3, WNS +0.026 ns). Valid only while the enable gives the CPU >= 3 clk2x cycles between two steps (bench assertion, sub-step e; at 20 MHz it is 3.15: N = 3 is the limit) and while din_r/rdy_r/irq_r/nmi_r and the port load at least 3 clk2x before the enable (step 4 changes the PHI2 relation: re-derive the table).
2. The 65816's registers (A, X, Y, S, P, D, PBR, DBR) are not exposed: `st_*`, `pc` as far as the state bus and the freeze/state save of the C64 core go read 0 (PC is the last opcode-fetch address). A state save of this core does not hold the CPU.
3. Turbo block (`c64_turbo_c64u.v`) is still instantiated with `inhibit = 1` (NO_TURBO) and `t_*` tied; the register `$D07A/$D07B` of T29 is therefore still the c64u turbo's decode. Replaced by the SuperCPU registers in step 4.
4. NMI acknowledge is the `$FFFA` vector pull in emulation mode; in native mode the NMI vector is `$FFEA` (step 6, T182 S9/S13 bench part).
5. The 20 MHz clock enable, `E` for the vector reroute and `$D0B6` are not wired: `e_flag` leaves the CPU wrapper and ends in the top as an unused wire (step 3/4).
6. **Done in step 3a (5.5):** the XCE branch of `A`/`X`/`Y`/`SP` in `s816_cpu` wrote constants without the enable (table, sub-step f). Was: its settling within one clk2x is not checked by the multicycle constraint. Fix then offered: gate the branch with `EN` in the generator (`tools/scpu_vendor816.py`) and rerun the emulation bench with an XCE-into-native program (step 6), or constrain the condition path single-cycle.

### 5.5 Build step 3 (2026-11-05, branch `t182-step3`): sub-step a and the T5 design; the rest waits for Alex

**Scope change (coordinator, during the step):** sub-step a and the T5 design (b) only; no memory RTL (c-e) until Alex has decided the placement (P-A / P-B in T5). Everything here is a coordinator proposal.
**Sub-step a: the XCE branch gated with the enable.** `tools/scpu_vendor816.py` now makes `if (EN = '1' and IR = x"FB" and P(0) = '1' and MC.LOAD_P = "101") then` of `s816_cpu` (the only write of `X(15:8)`, `Y(15:8)`, `SP(15:8)`, `oldXF` without `EN`; `A` was already under `EN`); `--check` is clean. Same behaviour at enable granularity: `IR`, `P` and `MC` (from `ROMQ`) change only on `EN`, so the old branch wrote the same constants in the clk2x cycles between two enables and again at the next enable, where it preempted the normal register load; now it writes once, at that enable. All `s816_cpu` registers are now enable-gated, so the multicycle table of 5.4 f has no "no" row left. Rerun on the builder (`trx64-sim-env:4`): `ok: cores/c64scpu/cpu is what tools/scpu_vendor816.py makes`; `run_emu.sh`: `PASS run_emu` (R1 902 / R2 219 PHI2 cycles = model and 6510, the four controls fail as they must, `ce|19723 ... smallest gap 64`); `run_base.sh`: `PASS run_base` (boot, both controls fail). The emulation bench has no XCE into native mode; the equivalence above is argued from the RTL and is first exercised by the native-mode bench of step 6. The flat build was not rerun (no Vivado in this step): the change removes a not-covered path, it adds none.
**Sub-step b: T5** is rewritten in §3 (stores, original's wait states from VICE, DDR2 miss cost 5.9-13.9 cycles at the measured 293-693 ns, block RAM per store and today's use, placements P-A/P-B, DDR2 placement checked against the other users).
**Sub-step c (RTL, D9 P-A; Alex: "let's start with P-A", translated).** `tools/scpu_cfgtab.py` (table from `scpu64meminit.c`, `--check` in lint), `scpu_mem.v` (decision per bus cycle: MEM / BUS / INT, boot map, hwenable, dosext, the $F6/$F7 window and the ROM window, `DEV_CYC`), `scpu_cache.v` (one 8 KiB write-through cache), the S and SW jobs in `c64scpu_ddrx.v` (SuperRAM / SRAM / ROM at `0x02000000 + p`), `cpu65816_c64scpu.v` using `scpu_mem`, the `scpu_*` plumbing through top, core and layer 2, the registers of T10. `SCPU_FLATMAP` restores the old flat map (the machine benches of steps 1 and 2 build it: `build_machine.sh` default and `run_emu.sh` base).
**Sub-step d: the benches (this step).** (1) With `SCPU_FLATMAP` in the defaults the machine compiles under Verilator (`trx64-sim-env:4` on the builder) and `run_base.sh` is `PASS run_base` (boot, ctl_nobasic and ctl_nochars fail as they must), `run_emu.sh` is `PASS run_emu` (`ce|19723 CPU enables, smallest gap 64`; ctl_slow, ctl_emu_wrong, ctl_6510 fail, ctl_ce2 fires with `smallest gap 2 clk2x cycles ... 19723 too close`). (2) `tb_c64scpu.v` wires the S/SW jobs, loads the 128 KiB test ROM at `0x03080000` (`mem/scpu_rom.mem`), drives `SROM_LOG2` = 17 and `ROM_VALID` (`+ROMVALID=0` holds the CPU), checks absolute DDR2 bytes (`+DEXPECT=aaaaaaaa:vv,...`, truth after the write jobs drained) and prints `+TRACE816` (the 65816's own bus cycles) and a `scpu|hits|misses|dev_cyc|boot|hw|dos` line; `ddr2c_model.v` has a `DEPTH_LOG2` parameter (default 18 = 4 MB, unchanged for its other users; 21 here = 32 MB, `0x01C00000-0x03BFFFFF`). (3) `tools/gen_c64scpu_map.py` makes the ROM (our own code, 65816 in emulation mode, long addressing) and the expectations; `cores/c64scpu/sim/run_map.sh` runs them, `ci/sims.txt` `c64scpu_map` (lane `large:c64scpu`, deps in `sims.deps`).
Results (builder, `trx64-sim-env:4`; the lines are `run_map.sh`'s):
- `scpu|hits 16432|misses 13|dev_cyc 4|boot 0|hw 1|dos 0`, `3 DDR2 bytes equal the model's`, `PASS s1` (`ddr[03002000] = a5`, the tail ran from SRAM after `$D0B6`: `ddr[03002002] = 5a`, `$FFFD` no longer reads the ROM's `$80`: `ddr[03002003] = 00`; 24,587 CPU enables).
- `ce|0 CPU enables ...`, `PASS s1_held` (`+ROMVALID=0`: nothing stored, `dev_cyc 0`, no CPU enable).
- `scpu|hits 66698|misses 4425|dev_cyc 250|boot 1|hw 0|dos 0`, `33 DDR2 bytes equal the model's`, `PASS s6` (244 tags first and last byte, error counter 0, the alias with hwenable `ddr[02000000] = 5a`, `ddr[0201ffff] = 6b`, off: `ddr[02f60000] = 00`, ROM reads `$F8:0100`, `$F9:0100`, `$FA:0100` (= `$F8`), `$FF:8100`, `$FF:FFFF` equal the file pattern, writes there ignored).
- Controls, each fails: `ctl_nobootmap` (`ddr[03002000] = 00, expected a5`), `ctl_novalid` (`+ROMVALID=0`, `ddr[03002000] = a5, expected 00`: the CPU ran), `ctl_noalias` (`ddr[02000000] = 00, expected 5a`, `ddr[03002010] = 11, expected 00`), `ctl_addr23` (`ddr[02020000] = 82, expected 02`, `ddr[03002020] = e8`: errors counted). `PASS run_map`.
Two corrections made while building: the S1 program must jump into SRAM **before** it writes `$D0B6` (the next fetch would otherwise come from the unmapped ROM; the first run showed BRK vectors at `$FFFE` = 0); the bench's "CPU enables > 0" assertion is off for `+ROMVALID=0`. The standard SuperCPU memory-size probe is not run separately: the tag loop is its read-back. dev_cyc is small (250 of 98 k) because a DDR2 miss (22-52 ui clocks) fits mostly inside one 1 MHz PHI2 cycle; the figure at 20 MHz is step 4's.
**Sub-step e: flat build (Vivado 2024.1, `vivado-env:2024.1`, direct `vivado -mode batch -source cores/c64scpu/mono/build_mono.tcl` with the env of `tools/core_toml.py env c64scpu`, `TRX_STOP=route`, under the heavy lock, builder).** `=== T182 T2 multicycle: cpu regs 223, sampled-input regs 11, port regs 116, consumer regs 25, smem regs 194`; design timing summary `WNS 0.098 ns, TNS 0.000, 0 failing endpoints of 53981, WHS 0.051`, "All user specified timing constraints are met"; Slice LUTs 22,595 (69.31 % of 32,600), Slice registers 22,819 (35.00 %), Block RAM tile 58.5 of 75 (78.00 %), DSP 30 of 120. The recipe is extended with three pairs (cpu regs -> smem regs, port regs -> smem regs, smem regs -> cpu regs; `smem regs` = `cpu_0/smem/*` without `cpu_0/smem/cache/*`, 194 registers; `int_q` is one of them). `mem_q` (the cache's `lk_q`) is deliberately NOT multicycle: it is registered together with `hit` and the CPU takes it at the next edge, one period is its real budget (it met timing). Not a bitstream for the board: no deploy (freeze).
**Open for step 4 (found while building):** `scpu_mem` settles a cycle in `cnt` = 5 clk2x cycles after the enable plus one (`tx_valid_q`), so a CPU cycle takes at least 7 clk2x cycles; the 20 MHz enable (3.15 clk2x per cycle) cannot be reached with this settling. Step 4 has to pre-compute the next cycle's class (the address is known one cycle ahead for most instructions) or accept a coarser model; the multicycle recipe (N = 3) and the bench assertion (>= 3 apart) stay valid either way.
**Not done:** the wait-state model and the speed registers (step 4).

### 5.4b Build step 8: the core application (2026-11-05)

**Built.** `coreapp/cores/c64scpu.cc` + `c64scpu_regs.h` (descriptor `c64scpu_desc`, ID `SCPU`, in `coreapp/src/table.cc`, `host-app/target/u64ii/riscv/coreapp/Makefile`, `tools/build_coreapp.sh`), `cores/c64scpu/mono/core.toml` (`roms = [...]`, `app` comment), the ID pair in `tools/check_core_id.py`, `host-app/software/hostapp/romreq_table.inc` (regenerated: the host's ROM page lists the six files), logo `cores/c64scpu/logo/` (the letters "SCPU" in the family's monochrome style, drawn by `tools/core_logos/build.py`; no CMD artwork, no Commodore mark), the `README.md` row (PoC, not in a release), the bench `tests/coreapp/test_c64scpu.cc` and the golden menu `tests/coreapp/golden/menu_c64scpu.txt`. The contract with the gateware is **T10** above. `build_only = true` stays.
Files (folder `/SD/cores/c64scpu/roms/`, the core's own folder only, T148): `scpu64.rom` (also `scpu.rom`, `supercpu.rom`, `scpu64-v2.04.rom`; 64, 128 or 256 KiB, reset vector at file offset `0xFFFC` at or above `$8000`, not all `0x00`/`0xFF`), `kernal-901227-03.bin`, `basic-901226-01.bin`, `chargen-901225-01.bin` (the C64 core's name lists), `dos1541-325302-01+901229-05.bin` (the C64 core's list; shared folder too), optional `jiffydos-1541.bin` and the other names of T10 (b). Settings: "SuperCPU speed", "JiffyDOS", "Joystick ports", "System Mode" and the drive's five (F5 Settings tab). Media: drive A (`.d64`, `.g64`, the 1541 library as the C128's); `.rom` "Set as 1541 ROM". Tasks: Reset, Reload ROMs, the drive's.
**Not built, by decision of the scope of this step, and open with Alex (D6 and the Done criteria of the plan):** REU size / preload / save and the cartridge (`.crt` run, remove, save) as in the C64 core: the C64 core's operations talk to Gideon's cartridge registers (`c64_cart.cc`, `c64.cc` REU settings through `u64cfg`) that this flat core has no application contract for, and the cartridge/REU behind the 65816 is step 7 of the gateware (S8, S12); the descriptor has no `cartridge`/`reu` capability. The PRG library, Mount & Run and `type_op` (as the C128: the machine has no typed start here yet). The tape. A "JiffyDOS" On/Off effect on the *SuperCPU side* beyond the `$D0B5` bit (that is the gateware's reading of `SCPU_SW`).

**Proof (app tests, `tests/coreapp/run.sh`, row `c64scpu`; the descriptor, the operations and the generic start are the real ones against a model of page E and of the DDR2 window; no ROM byte: every image is a pattern made in the test, `srom()` sets the vector).** Local run (Mac, one process) and the controls (each breaks the product by one sed expression and the same test must fail):
```
test_c64scpu: 126 checks, 0 failed
S2: held through 29 passes (in reset, no release, no ROM_VALID write, no DDR2 write), message "SuperCPU ROM: scpu64.rom or scpu.rom or ... is missing in /SD/cores/c64scpu/roms/"; file put in pass 30 -> started within 1 s;
    100 bytes ("... is 100 bytes, but 131072 or 65536 or 262144 bytes are expected"), vector $7FFF and all-0xFF ("... is not a SuperCPU ROM image") refused, never "missing"; 64/128/256 KiB accepted (SROM_LOG2 16/17/18);
    release with ROM_VALID 0 -> refused (no mode_unreset), with 1 -> released
S7: SCPU_SW 0x01 -> 0x00 (1 MHz) -> 0x02 (+JiffyDOS) -> 0x03 -> 0x01; start with both stored: 0x02; restart (COLD) -> written again;
    drive ROM read back from the model: stock with Off, JiffyDOS file with On, stock again with Off; On without the file: "JiffyDOS 1541 ROM not supplied", stock stays, bit set; file present + Off: stock
```
Controls (all 20 fail as they must; the sed lives in `tests/coreapp/run.sh`): `s2_required` (SuperCPU ROM not required: the start does not wait), `s2_vector`, `s2_sizes` (256 KiB not in the list), `s2_blank`, `s2_gate` (release without the ROM_VALID check), `s2_valid_first`, `s2_valid_last`, `s2_flush`, `s2_base`, `s2_log2`, `s2_win` (window offset in the header), `s7_speed` (polarity), `s7_jiffy`, `s7_reg` (SCPU_SW at 0x061), `s7_bit`, `s7_choose` (JiffyDOS ROM with the setting Off), `s7_nofile`, `s7_cold`, `joy`, `sysmode`. `test_desc` (the descriptor checks of every table row: id, caps, hooks, bindings, ranges, menu golden) and the stale-table check `gen_romreq` pass with the new row.

**Builder (2026-11-05, `u64-build-env:ci`, CI cache mounted, commit 51263cc8e as a snapshot):** host app and core app built for RISC-V (`RISCV OK`, `check_app_size` passed, c64scpu.cc compiled into coreapp); `ci/apptests.sh`: `hostapp: PASS in 94s`, `coreapp: PASS in 1185s` (the 20 `c64scpu_*` controls each `fails as it must`), `loader: PASS in 3s`, `apptests ok`. No rendered screen changed, so no render bench.

**Findings.** (1) The generic menu has the core's settings in the F5 "Settings" tab, F8 is the host's: D5's "F8 setting" is built as the F5 items (T10 c). (2) `tools/core_logos/build.py` (its `--check` and a full run) is red on `main` already: `cores/diag` has a core descriptor and no logo (`missing=['diag']`); the script is not part of the CI; for c64scpu the generator skips `build_only` cores, so `LOGO_WHILE_BUILD_ONLY` names c64scpu, and the one logo was generated by calling the script's functions; the contact sheet is not regenerated (it would need the diag logo first). (3) The DDR2 window reaching `0x03080000` (offset `0x02080000`) is not exercised by any existing core's application (the C128 uses `0x01C00000`); the application's read-back after the write is the check on the board.

### 5.6 Build step 4 (2026-11-05, branch `t182-step4`): registers and speed (coordinator proposal, nothing here is an Alex decision)

Design: the CPU's address, the class-table BRAM address and the cache BRAM index are sampled in EVERY clk2x from `a_out` (multicycle 2, hold 1); a cycle is `settled` 2 clocks after the enable, the decision (hit, class, wait) is combinational from `cls`/`cq` at the 3rd clock, so a hit serves a cycle in 3 clk2x. 20 MHz enable = accumulator +203 per clk2x (the clock of the enable counts too: `acc - 437` at the enable), tick at >= 640 (spacing 3 or 4, never less; measured: smallest gap 3, 0 too close); waits (EPROM +3, SuperRAM cell/row, write buffer) are taken off the accumulator at the enable. I/O class cycles (`sync_cyc`) arm for a PHI2 cycle (address on the bus at ph <= 12 or after this cycle's fall, else the next cycle), data at the PHI2 fall, the enable in the clock after `n_d[1]` (2 clocks after `n_ev`). `$D07A/$D07B/$D072/$D073/$D0B2/$D0B8`, optim, SIMM registers as `scpu64mem.c:684-817`; `fast_o` is STATUS b1.
Measured (builder, `run_speed.sh`, Verilator, report lines): P2 (SRAM loop 20 MHz) 1238 PHI2 cycles against 1237.7 modelled; P3 (1 MHz) 25125 = model; P3/P2 = 20.29; display on/off at 20 MHz 1238 / 1238 (BA does not stretch SRAM code), at 1 MHz 26501 / 25125; 1,000 x `LDA $D020` 2002 PHI2 cycles (display off, border value read back = $FE), 2170 with the display on (BA holds I/O). S11: 30 register readbacks in four register states equal the model written from `scpu64mem.c:634-821`. Controls NOFAST, NOSYNC, BASTALL, NOHWGATE fail as they must. `run_emu`, `run_base`, `run_map` green on the same RTL.
**I/O cost, D8 (fixed 2026-11-05, coordinator proposal):** was 2002 PHI2 cycles per 1,000 reads (VICE 1000): the path fall -> arm took 32-33 clocks against the 32 from the PHI2 fall to the next rise, so every access missed one PHI2 cycle. Two clocks removed, the address still goes onto the C64 bus no later than the PHI2 rise (ph 12): (1) `fwd` is combinational from `arm_go` (`bus_up = armed&&!a_skip || arm_go&&!skip_now`, `skip_now` = ph > 12 and the fall not yet passed), no longer one register later; (2) the CPU's enable follows `n_d[1]` in the same clock (`bus_go` takes `armed && n_d[1] && !a_skip`, no `bus_ok` register first): din_r is two periods old at the CPU's registers instead of three, the recipe of `build_mono.tcl` says so for din_r/rdy_r/irq_r/nmi_r and the port registers into the CPU (setup 2 / hold 1; spec 5.4 f). Measured (builder, `run_speed.sh`): 1,000 x `LDA $D020` take **1006** PHI2 cycles (display off, P5; 1005 as P0 before any raster wait; 1092 with the display on, BA), VICE 1000; `IO_MAX` in `check_speed.py` is 1020. The 6 cycles are the accesses (0.6 %) whose address is ready one clock after the PHI2 rise at the phase where the loop meets it: they take the next PHI2 cycle; not removed.
**Bus check (new, S4):** the bench samples the C64 bus at the machine's clk (where the chips look): address and R/W are equal at the PHI2 rise and at the fall of every cycle (147,959 cycles, 0 changing inside the high phase), and the 250 x `STX $D020` of P7 (X = 250 .. 1 at 20 MHz) show their bytes at the fall in order, each once. Control `ctl_lateaddr` (`SCPU_MUT_LATEADDR`: the address goes out whenever the class is known): 17 cycles change inside the high phase, S4 fails. `ctl_nosync` now fails on a value (P0: 446 PHI2 cycles for 1,000 reads, minimum 1000) instead of a hang: the I/O loop is the program's first phase, before the first raster wait.
**Flat build (builder, `vivado-env:2024.1`, under `/cache/heavy.lock`, `build_mono.tcl` started directly, the tree of commit 5bad9cd66, one pass, ~12 min):** WNS **+0.001 ns**, TNS 0.000 (0 failing of 53,995 endpoints), WHS +0.051, routing errors 0; 22,741 LUT (69.76 %; 21,954 as logic), 58.5 BRAM tiles (42 RAMB36 + 33 RAMB18), 7,905 slices (96.99 %). The recipe logged `cpu regs 221, sampled-input regs 11, port regs 116, consumer regs 25, smem regs 188`. The worst setup path (0.001 ns, two of them) is in the HDMI output (`hout/mtim/xact0_reg` -> `hdmi_enc` packet picker, `pix_mmcm`), not in a path of this core; no path through the new `fwd`/`core_ce` logic is near the top. Slack margin is a fact of the router stopping at 0, not of this step: step 3 had +0.098 ns.
**S11 as the spec asks:** four tables of the 16 registers `$D0B0-$D0BF` (`LDA $D0B0,X` loop) in the four register states (after reset; hwenable on with optim changed; hwenable, system 1 MHz, soft 1 MHz and DOS extension on; hwenable off again) = 64 entries, plus the 30 targeted readbacks after writes = 94 readbacks, all equal the model of `tools/gen_c64scpu_speed.py` (`Vice`, written from `scpu64mem.c:634-821`; every read ORs in `optim & 7`; `$D0B6` = bit 7 emulation mode); control NOHWGATE fails them (hwenable-gated registers ignore the gate).

### 5.7 Build step 5 (2026-11-05, branch `t182-step5`): mirroring of SCPU RAM writes into the C64 RAM (coordinator proposal, nothing here is an Alex decision)

Oracle: `scpu64mem.c` (`mem_mirrors` :81-86, `mem_set_mirroring` :1472-1478, the tables :1312-1346, the stores :277-400), `scpu64cpu.c` (`scpu64_clock_write_stretch` :244-254, `wait_buffer` :138-149). Read from the code, not from comments.
**Which writes are mirrored (VICE).** Only writes of bank 0 that VICE's table maps to RAM (`mem_write_tab` entries `ram_store*`/`zero_store*`: every page whose class is not I/O or colour RAM; this includes writes under the BASIC/KERNAL/character ROM, which land in RAM) and whose page lies in the active range. The range is `mem_mirrors[m]`, `m = (optim&1) | (optim&4 ? 2:0) | (optim&0x40 ? 4:0) | (optim&0x80 ? 8:0)`; high byte = first page, low byte = last page, entry 0 = none: m 0,1 `$80-$BF`; 2 `$00-$3F`; 3 `$02-$3F`; 4,5 `$40-$7F`; 6,7 `$C0-$FF`; 8,9 `$04-$07`; 10,11 none; 12,14 `$00-$FF`; 13,15 `$02-$FF` (reset `0xC7` = 15). `$D074-$D077` set `optim = (a&3)<<6`, `$D0B3`/`$D0B4` its low / high bits (all need hwenable, step 4). Writes to `$0000/$0001` already reach the C64 RAM through the CPU-port cycle (scpu_pport); `$FF00` has VICE's extra REU stretch (`ram_hi_store_mirrored`), not modelled (the core has no REU: out, to be confirmed with Alex). Not mirrored: banks 1-$FF, I/O (`$D000-$DFFF` with I/O visible), colour RAM (it is on the C64 bus in every mode, `WK_BOTH`), writes the class sends to the bus anyway (`RC`, `UM`, `OP`).
**The write buffer belongs to the mirrored write.** In VICE only the mirrored (and internal-RAM) stores call `scpu64_clock_write_stretch`; an unmirrored bank-0 store (`ram_store`, `zero_store`) costs nothing at 20 MHz. Step 4 applied the buffer to every bank-0 SRAM write (`wr_sram0`); that was wrong for unmirrored pages and is corrected here: the buffer is set by a mirrored write only. Its cost as VICE counts it: a mirrored write first waits for the buffer (`buffer_finish`, 0.475 PHI2 cycles into its cycle), then sets `buffer_finish` = this PHI2 cycle + 1 (+1 if the accumulator is already late in the cycle), half 0.475. A loop of back-to-back mirrored writes therefore runs at one write per PHI2 cycle (about 20 CPU cycles per write at the limit); the next I/O cycle waits for the same boundary (step 4, unchanged).
**How a mirrored write becomes a C64 bus write.** At 20 MHz the CPU's own SRAM write is done as before (cache, write-through). The mirror byte (address 16 bit, data) goes into a one-entry queue (`mir_pend`); a second mirrored write cannot start while it is occupied (counted in `DEV_CYC`; the write buffer already keeps the next one at least to the next PHI2 boundary + 0.475, so the queue is free again by then). The queue is driven as a C64 bus write cycle by the PHI2-synchronous path of step 4: at the start of the next PHI2 cycle (or in the current one when it is still before ph 8) the address, R/W = write, the data and the PLA lines `000` (RAM, whatever the class) are on the bus before the PHI2 rise, stay through the fall and are released two clocks after it (`n_d[1]`, the same release as an I/O write). The CPU's own sync cycles (I/O, colour RAM) do not arm while the queue is pending or driving (`mir_busy`); they would have to wait for the write buffer anyway. If the VIC owns the bus at the fall (`aec` low: the badline's c-accesses) the write did not land: it stays in the queue and is driven again in the next PHI2 cycle (the real machine waits for the bus; same data, idempotent). At 1 MHz the mirrored write is a plain `WK_BOTH` cycle (SRAM and bus in the CPU's own PHI2 cycle, no queue). The C64 RAM and the VIC's slots in block RAM are written by the existing RAM path (`c64scpu_ramd`, write FIFO); the VIC never reads the DDR2.
**Proof (S5).** `cores/c64scpu/sim/run_mirror.sh`: our own 65816 program (`tools/gen_c64scpu_mirror.py`, no ROM byte) at 20 MHz; the oracle in the generator is the `mem_mirrors` table above, written down independently of the RTL. (1) 1,024 mirrored stores (screen fill) take the PHI2 cycles the VICE accounting gives (literal transcription of `wait_buffer` / `write_stretch` in the generator, +-3 %), the same 1,024 stores in an unmirrored mode take the plain 20 MHz time; (2) in nine modes (reset, `$D0B3`, `$D077`, `$D076`) 7 probe pages each (`$02 $07 $20 $50 $90 $B0 $E0`, one distinct address and byte per mode and page, under ROM too) are in the C64 RAM exactly for the pages in the mode's range and untouched (0) elsewhere; (3) the picture: text rows written through the mirror with the display on (badlines, the retry) and the probe cells of page `$07` (rows 19-24) read back through the VIC from the character ROM (`check_screen.py`); (4) colour RAM filled in an unmirrored mode shows in the picture. Controls (each must fail): `SCPU_MUT_NOMIRROR` (no mirror), `SCPU_MUT_MIRRWIN` (range window shifted by one page), `SCPU_MUT_WBALL` (the step-4 buffer on every bank-0 write: the unmirrored loop is too slow), `SCPU_MUT_NORETRY` (a write lost to the VIC is not repeated: the picture of the display-on phase has holes).

**Results (builder, `trx64-sim-env:4`, Verilator, `run_mirror.sh`, tree of the merge with `main` 6cf7c5670, report lines):** `PASS+PASS+PASS mirror (RAM PASS, picture PASS, cycles PASS)`; `S5 mirrored: 1,024 mirrored stores: 1025 PHI2 cycles, VICE model 1025`; `S5 unmirrored: ... 316 PHI2 cycles, VICE model 315`; `S5 ratio: mirrored / unmirrored = 3.24`; `S5 retry reached: 238 mirror writes held off by the VIC (aec low at the PHI2 fall) and driven again`; `S5 retry lands: 8482 mirror writes accepted by the queue, 8482 landed on the C64 bus`; `PASS check_screen: 1000 of 1000 cells`; `ce| 1661879 CPU enables, smallest gap 3 clk2x cycles, 0 too close`. Controls, all failing as they must: `ctl_nomirror`, `ctl_mirrwin` (RAM and picture fail, cycles pass), `ctl_wball` (cycles fail: unmirrored 1025 PHI2 against 315, ratio 1.00), `ctl_noretry` (picture fails at row 7 col 13..15, and `8482 accepted, 8324 landed`).

**Correction (2026-11-05, bench hole):** the `RAM PASS` above checked only about 201 of the 333 probe bytes (144 mode probes + 189 stack slots): `tb_c64scpu` held `+EXPECT` in 1600 characters (200 entries) and `$value$plusargs` dropped the leading part, the mirror-mode probes at the front were never compared. The buffer is now 8192 characters, the tb fails if the string fills it, and `run_mirror.sh` requires the `ok` count to equal `"probes"` in `expect.json`. Rerun on the builder: `333 of 333 probe bytes in the C64 RAM equal the model`, `PASS run_mirror`, controls as above (`ctl_nomirror`, `ctl_mirrwin` RAM FAIL; `ctl_wball`, `ctl_noretry` RAM PASS, failing by cycles/picture).
**Bench bugs found on the way (fixed in the bench, not the RTL):** the wait for line `$32` also matched line `$132` (`D012` low byte only) and ran in the vertical blank, so no write ever met a badline and `NORETRY` could not fail; the waits check `D011` bit 7 now. `check_speed.py` counted each marker write twice since step 5 (the 65816's own write line and the mirrored copy on the C64 bus both print `mark|`); it takes the `|frame` line only.

### 5.8 Build step 6 (2026-11-05, branch `t182-step5`): interrupts, vector reroute, native-mode program (coordinator proposal, nothing here is an Alex decision)

Oracle: `scpu64_interrupt_reroute()` (`scpu64mem.c:1749-1752`). **Vector pull:** a read with VPB low from page `$FF` of bank 0 whose class is KERNAL/ROM-1/KERNAL-table, in native mode or with hwenable, system 1 MHz or DOS extension set, reads the SuperCPU ROM at `$F8:vector` (not the SRAM of bank 0); otherwise the SRAM. NMI is acknowledged by the `$FFFA` pull (emulation) and the `$FFEA` pull (native).
**Proof (S9, S3 native block):** `cores/c64scpu/sim/run_irq.sh`, our own synthetic SuperCPU ROM and program (`tools/gen_c64scpu_irq.py`, no ROM byte), 800 ms of machine time. Per phase the handler count after 100,000 PHI2 cycles: CIA 1 / CIA 2 timer A every 4,000 cycles = 25 +-1, VIC raster IRQ 5..6, in emulation mode (rerouted vector), with hwenable off (SRAM vector) and in native mode (`$FFEE`/`$FFEA`); ten RESTORE pulses = exactly 10 NMIs, BRK x3 (`$FFE6`) and COP x2 (`$FFE4`) exactly 3 and 2. S3: native mode from SuperRAM `$02:0000`, 16-bit ADC/SBC, MVN, stack at `$0FFF`, `[dp],Y` in two banks, XBA, long addressing, JSL/RTL, 8-bit ADC after SEP; the 34 bytes of the block are computed in the generator by hand. Report lines: `dump|0330|19 19 05 19 19 19 00 00` (25, 25, 5, 25, 25, 25 handler runs), `9 C64 RAM bytes (verdicts, exact counts) and 34 SuperRAM bytes of the S3 block equal the model`, `PASS run_irq`. Controls fail as they must: `ctl_noreroute` (`SCPU_MUT_NOREROUTE`, 0 of 9 RAM bytes, S3 block 1 of 34) and `ctl_emuonly` (`SCPU_MUT_EMUONLY`, XCE as NOP: 3 of 9, 1 of 34).
**Bug 1 (bench):** the handlers clobbered A, which the interrupted wait loop polls through: every wait ended at the first IRQ (2 handler runs instead of 25). The handlers keep A now.
**Bug 2 (RTL), `cpu65816_c64scpu.v` `irq_latched`:** it was the level sampled at the PHI2 fall (`clk_phi ? irq : irq_r`). At 20 MHz the 65816 runs the whole handler tail (acknowledging `D019` write, PLA, RTI) inside the PHI2 cycle of the write; the sample still held the old level and the VIC raster IRQ was taken again (8 handler runs instead of 5). Now `clk_phi ? irq : (irq_r & irq)`: the low phase also drops the level the moment the line itself falls. The low phase can only lower the sampled level, never raise it, so an interrupt that rises mid-cycle is taken at the next fall as before; NMI is an edge and stays as it was.
**Flat build (builder, `vivado-env:2024.1`, container `t182s6-vivado`, `build_mono.tcl` started directly with the `mono_env` of `core.toml`, tree of the merge 6cf7c5670, one pass):** WNS **+0.042 ns** (worst setup path `ult/i_cpu/i_dmem/addr_i_reg[16]` -> `ai2c/own_reg`, clock `cpu_mmcm`, 10 logic levels, 9.730 ns of 10 ns: the host's RISC-V data address into the I2C ownership flag, not a path of this core), TNS 0.000 (0 failing of 54,110 endpoints), WHS +0.024, routing errors 0; 22,920 LUT (70.31 %; 22,133 as logic), 22,867 FF (35.07 %), 58.5 block RAM tiles (42 RAMB36 + 33 RAMB18; 78.00 %), 30 DSP, 7,895 slices (96.87 %). Step 4 was 22,741 LUT, 7,905 slices, WNS +0.001: steps 5 and 6 cost 179 LUT. The recipe logged `cpu regs 221, sampled-input regs 11, port regs 116, consumer regs 25, smem regs 242`. The timing-relevant change of step 6 (`irq` combinational into `irq_latched`) did not show in the top paths.

### 5.9 Step 7 design (coordinator proposal, not decided) (2026-11-06, branch `t182-step7`, reading only: no RTL, no app code, no run)

Everything below is read from the code of the tree at `906bc3501` and from VICE trunk (`~/Development/C64/Tools/vice/vice/src/`); where it contradicts §2/§3/§5.4b, the code wins. Nothing here is an Alex decision; 6 questions at the end.

**1. What the flat core has today.** The REU and the cartridge slot are **already in the build**, not new blocks: `mono_env` has `TRX_REU=1`, `build_mono.tcl:55` drops `NO_REU`, `c64_top_c64scpu.v:1302-1345` instantiates `c64_reu` (icepi's, `vendor/icepi/c64_reu.v`, with Gideon's size mask as an input `reu_size`), `c64_core_c64scpu.v:570` instantiates `cart_slot_c64u` (the descriptor-driven `c64_cartridge` model of icepi + our flash/EEPROM responders, `cores/c64u/cart_slot_c64u.v`) unconditionally, and `layer2_c64scpu.v:427` the DDR2 fill `cart_fill_c64u`. The arbiter (`c64_bus_arbiter_c64scpu.v:370-430, 546-610`) has IO1/IO2/ROML/ROMH, GAME/EXROM and the REU/cartridge IO2 arbitration of the C128 fork. The 22,920 LUT / 58.5 tiles of §5.8 contain them. The flat host copy has the app's registers (`mono/host/ultimate_sys.vhd:1139-1143` CART_TYPE `+5`, REU_ENABLE `+8`, REU_SIZE `+9`; `:1186-1190` BUS_INTERNAL/EXTERNAL `+0x2B/+0x2C`; page 4 descriptor). The C128 flat core has the same blocks and **no** app side (`coreapp/cores/c128.cc:6`: "Not in this descriptor: cartridge port, REU"; T155 is planned, not built); the C64 side was forked from the C128 (`cores/c64scpu/PROVENANCE.md`), which is a copy of `cores/c64u`. The benches never exercise them: all use `NO_REU` (`sim/build_machine.sh:15`, `run_*.sh`), and the testbench has no REU memory model and no cartridge image in DDR2.
The map side is also there: `scpu_cfgtab` has the classes UM/RL/RH/IO/OP, the index takes the real pins `{~game_pin, ~exrom_pin}` (`scpu_mem.v:114`, fed from `vic_game/vic_exrom` incl. the cartridge and `ext_ultimax`, `c64_top_c64scpu.v:648`), RL/RH/UM/IO reads and UM/IO writes are bus cycles (`scpu_mem.v:182-185`, `sync_cyc` waits for PHI2), RL/RH writes go to SRAM as in VICE (`scpu64meminit.c:329-339`, "write hook preset, ram"). So a cartridge behind the 65816 is most likely a bench job, not RTL (not yet proven: no bench ever ran it).

**2. What does not work today (found in the code; each one is a gap step 7 has to close).**
- (a) **REU DMA does not halt the 65816.** `cpu_rdy` has `!reu_dma_active` (`c64_top_c64scpu.v:686`), but the CPU's `rdy_in = rdy_latched | wr_cyc | mem_free` (`cpu65816_c64scpu.v:147`): writes are never held (a 65816 makes many in a row: MVN, pushes, STA loops) and at 20 MHz every SRAM/SuperRAM/ROM cycle is `mem_free` (`scpu_mem.v:391`). The CPU keeps running and, on a sync cycle, drives the C64 bus against the REU's `reu_dma_addr` (`c64_bus_arbiter_c64scpu.v:459-462` lets the DMA win). T2 already says the halt must be a gated CE, not RDY.
- (b) **The same defect hits the app's DMA** (`ext_dma_*`, Mount & Run, PRG inject, `c64_go`): it only starts in "`cpu_paused && !cpu_we`" (`c64_top_c64scpu.v:1995`), i.e. a CPU read held by RDY; at 20 MHz in SRAM code that never occurs. Step 8's open "Mount & Run" needs the same hold.
- (c) **`$FF00` trigger never fires from the 65816's RAM writes**: `.ff00_trigger(addr_a == 16'hff00)` (`c64_top_c64scpu.v:1341`) looks at the C64 bus address, and a bank-0 RAM store at 20 MHz is a MEM cycle that does not reach the bus (`cpu65816_c64scpu.v:190`, `addr` is 0). VICE triggers on every write to `$FF00` in all three RAM store variants (`scpu64mem.c:347-394`, `reu_dma(-1)` plus `scpu64_clock_write_stretch_io_start/long`).
- (d) **REU DMA data goes to the wrong store.** VICE's DMA bytes go through the CPU's own bank-0 map (`mem_dma_store/mem_dma_read`, `scpu64mem.c:431-455`: the write table, so SRAM, plus the internal RAM when the page is mirrored); the core's REU writes the C64 RAM `RC` only (arbiter above), while the CPU reads SRAM (`scpu_mem.v:173-175`, `sram()`): after a fetch to `$0800` the program would read stale SRAM. S8's "stash from SRAM bank 1" is not expressible either: the REU's host address is 16 bit, DMA sees bank 0 (and bank 1 only where the configuration table maps `R1`).
- (e) **DDR2 overlap for REU sizes above 8 MB.** The REU window is `0x01000000-0x01FFFFFF` (`mono/top.v:316`, `REU_BASE` 0x800000 words); `c64scpu_ddrx.v:15-16` puts the C64 ROMs at `0x01C00000` and the C64 RAM backing at `0x01C20000`. REU size 6 (8 MB) ends at `0x017FFFFF`; size 7 (16 MB) overlaps both (the C128 has the same: §3 T4 "its REU is smaller than 16 MiB"). The cartridge image shares the block (ROM_SEL 1; 1 MB EasyFlash, 8 MB EF3 fit below `0x01C00000`).
- (f) The halt has more parts than the enable: `arm_go` (`scpu_mem.v:356`) and the mirror drive (`ma_drv`, queue `mp_v`) are CPU-owned C64-bus users that today do not look at any hold; and with the CPU held its last `cpu_we` stays, which blocks the app DMA's start condition (b).

**3. The SuperCPU in front of the port (VICE).** (i) Hold: in 1 MHz mode `check_ba_read/write` steals the cycles of BA-low, REU included (`scpu64mem.c:150-162`); in fast mode only an I/O-class access steals them (`scpu64cpu.c:85-98` `maincpu_steal_cycles` runs `reu_dma_start`, called from `scpu64_clock_read_stretch_io` `:178-190`; and the `$FF00` store calls `reu_dma(-1)` itself), and the transfer then advances `maincpu_clk` byte by byte (`reu.c:819-846`), DMA bytes carry no wait states (`dma_in_progress`). So in the original model the CPU does not move during the transfer, but fast-mode SRAM code runs on until its next I/O access (lazy start); S8 as written ("no bus cycle between the first and the last byte") is the stricter, immediate halt. VICE's REU entry is marked "FIXME find a better place" only for where the BA callback is registered (`:207-208`), not for the DMA itself. (ii) Cartridge: ROML/ROMH/Ultimax reads and writes are I/O-class (`scpu64_roml_read/store` etc., `scpu64mem.c:1171-1214`, `scpu64meminit.c:297-339`): PHI2-synchronous with the I/O wait states (accu 11.5 M / 16.8 M / 17.6 M, `scpu64cpu.c:107-150, 178-242`), banks: the cartridge is seen in bank 0 only, at the pages the configuration table says (`$8000-$9FFF` ROML, `$A000-$BFFF` or `$E000-$FFFF` ROMH, Ultimax `$1000-$7FFF`, `$A000-$CFFF`, `$E000`); there is no mirroring of the cartridge into SRAM; cartridge ROM is not visible in banks 1+ (the SRAM/SuperRAM take them). The `$DE00-$DFFF` range is I/O (`scpu64io_df00_store`, `:1139-1165`). The SCPU ROM's boot map hides an Ultimax cartridge's reset vector until `$D0B6` is written (§2 S12: whatever the original does).

**4. Cost.** Source of the block figures: the hierarchical synthesis report of the calibration build C (the C128 as shipped, the same blocks and sources), `cores/c64scpu/trial/reports/build_trial_C/utilization_hier_synth.rpt:303` `c64_reu` 993 LUT / 195 FF / 0 tiles, `:354` `cart_slot_c64u` 973 / 793 / 1 RAMB36 + 1 RAMB18, `:371` `slot_port_c64u` 122 / 213, `:372` `cart_fill_c64u` 215 / 156 (88 of it LUTRAM); the routed hierarchy of the C128 (`cores/c128/mono/reports/b_r5/utilization_hier.rpt:283,324`) says 974 and 958. Together **about 2,300 LUT, 1,360 FF, 1.5 tiles, already inside the measured 22,920 LUT / 58.5 tiles / WNS +0.042** (§5.8; the c64scpu build kept no hierarchical report). New logic of step 7 is an estimate (no measurement; the Vivado run of 7d is the measurement): 7a hold gate, hold-ack, `arm_go`/`ma_drv` terms about 40-80 LUT, 10-20 FF; 7b `$FF00` decode (one 16-bit compare, a class change) plus a DMA write/read port into the SRAM store and its cache line update about 100-250 LUT, 0 tiles; 7c none expected. **Total +150 to +330 LUT (under 1 % of 32,600), 0 tiles.** Against S14: 22,920 + 330 is 71.3 % of 85 % allowed (27,710), 58.5 of 74 tiles. The real constraint is not area: 7,895 slices = **96.87 %** and WNS +0.042 ns (worst path is the host's RISC-V address, not this core's); a new term in `core_ce`/`arm_go` is a high-fan-in path on `clk2x`.

**5. Proposed sub-steps (each ends in a bench or an app test; runs only verify).**
- **7a Hold** (S8 first half). One `dma_hold = reu_dma_active | ext_dma_cyc | (app pause)` into `scpu_mem`: withholds `core_ce`/`tick`, `arm_go` and `ma_drv`; a hold-ack (no armed cycle, `mp_v` empty) is fed to the REU's BA input so its first DMA cycle waits for the CPU to be off the bus; `cpu_we` reads 0 while held (fixes 2(b)). Bench `cores/c64scpu/sim/run_reu.sh` (`C64SCPU_SIM_DEFS` without `NO_REU`, a REU memory model on the `reu_cmd_*` port in the testbench): program in SRAM at 20 MHz and at 1 MHz, a `STA` store loop running while a 16 KiB stash/fetch/swap/verify runs; proves: no CPU `core_ce` and no CPU C64-bus address between the REU's first and last DMA cycle (counted in the bench), data equal, REU registers read back. Controls: `SCPU_MUT_REUNOHALT` (RDY only = today's behaviour; must fail on the store loop), `SCPU_MUT_REUARM` (`arm_go`/`ma_drv` ungated, must fail on the bus-collision count), a REU with the transfer length +1 (the data check must see it).
- **7b DMA through the SuperCPU map** (S8 second half; needs Q1). `$FF00` write = bus cycle with the PLA at RAM (like the 1 MHz mirror, `scpu_mem.v:244-250`) in every bank-0 RAM class; REU/app DMA bytes read/write the SRAM store (and `RC` when the page is mirrored) via the existing byte-write path to the cache (`sw_req`), the CPU being held. Bench extends `run_reu.sh`: fetch to `$0800`, the CPU then reads it from SRAM; stash from SRAM-only pages; trigger by `$DF01` and by `$FF00` (immediate, CMD bit 4 = 0); mirrored and unmirrored range. Controls: `SCPU_MUT_DMARC` (DMA to `RC` only: the CPU reads stale), `SCPU_MUT_NOFF00` (trigger never fires: transfer count 0).
- **7c Cartridge behind the 65816** (S12). Bench `sim/run_crt.sh`: our own test cartridges (image made by a generator, not a real cartridge) loaded as the app does (descriptor words + image in the expansion block of the testbench's DDR2 model): Normal 16K (type 0x01: code in ROML runs, ROMH read, marker in SRAM), Magic Desk (0x28: a `$DE00` write switches the bank, the next ROML read shows the other bank), Normal Ultimax (0xA1: `$E000` vector area and `$1000-$7FFF`). Proves ROML/ROMH reads are PHI2-synchronous bus cycles at 1 MHz and 20 MHz (cycle count per read, pinned from this run), IO2 writes hold the I/O wait. Controls: `SCPU_MUT_NOGAME` (GAME forced high), existing `SCPU_MUT_NOSYNC` (cartridge data wrong), a cartridge of the wrong type (descriptor 0, marker absent).
- **7d Flat build** (builder): both together with the REU and CRT paths exercised, hierarchical report kept this time (`mono/reports/s7`), numbers into this section; includes the DDR2 decision of Q2.
- **7e App** (can run beside 7a-7c). No new registers: the contract is the existing one (`C64_CARTREGS_BASE`: `+5` CART_TYPE, `+8/+9` REU, `+0x0E` sampler; `C64_IO_BASE+0x2B/0x2C`; page 4 descriptor `0x040`, flash responder `0x4000-0x4038`, ROM_SEL `0x4030`; `coreapp/cores/c64_regs.h:40-69`, `hw.cc:93-95`). In `c64scpu.cc`: capabilities `cartridge`, `reu` (now `{iec, drive_a}`, `:369`); the operations `c64_cart_run / save / remove` and the mount slot "Cartridge" as `c64.cc:503-581, 522-549` (they include the flash write-back service of T154 for the 9 flash types); the settings "RAM Expansion Unit", "REU Size", "REU Preload (+Offset, Image)" and the operations "Load into REU" / "Save REU memory" now in `trx64u_ops.cc:104, 138-201` (host-owned C64 code: moved to a shared file or called from the SCPU descriptor; the REU window arithmetic there assumes 16 MB, `:181`); REU and cartridge exclusive as in the C64 core (one block). App tests in `tests/coreapp` against the page-E/page-4 model with the usual sed controls, then the RISC-V build and `ci/apptests.sh` on the builder. Board (batched with step 9): a REU program with the SuperCPU at 20 MHz, a real `.crt` of each type Alex names, EasyFlash save.

**6. Questions for Alex (not decided; one per turn when it is his).**
1. **Answered, D10: as VICE.** REU DMA: follow VICE (bytes go through the CPU's bank-0 map into SRAM and, when mirrored, the C64 RAM; `scpu64mem.c:431-455`) or keep today's C64-RAM-only DMA? (VICE is the only oracle; the real hardware's behaviour is not in the repository; D8 says as close to the original as possible.)
2. **Answered, D11: B (16 MB, the backing moves).** REU size: 16 MB overlaps the C64 ROM/RAM backing at `0x01C00000` (`c64scpu_ddrx.v:15-16`); cap the setting at 8 MB, or move the ROM/RAM backing (changes the T10 `ROM_BASE` contract and the step-1 ROM path)?
3. **Answered, D12: B (VICE's lazy halt; the app's DMA holds at once).** Halt: S8 as written (the CPU stops at once, no bus cycle during the transfer) or VICE's lazy start in fast mode (SRAM code runs on until its next I/O-class access)? The first is simpler and also serves the app's DMA; the second is closer to VICE and needs a pending-trigger state.
4. **Answered, D13: A (all of the C64 core's types).** Cartridge types: all 19 descriptors of `c64_cart_desc.h` (the C64 core's list, including flash write-back and EasyFlash 3) or a subset? Not started by the C64 app either: UCI `$DE1C`, Final Cartridge, Epyx, Zaxxon, Super Games, Super Snapshot (`c64_cart.h:1-8`).
5. **Answered, D14: A (keep the blocks in the build, close the gaps).** Reuse: the REU and cartridge model that are already in the build (icepi/descriptor, about 2,300 LUT, no Gideon VHDL; his registers are only the app's contract) as they are, or VICE-modelled RTL for the REU's timing? The facts: the REU's DMA byte rate is the icepi state machine's (one C64 cycle per byte, as the C64 core), VICE's REU advances the clock the same way; no measurement says they differ.
6. **Answered, D15: A (Mount & Run is part of step 7).** Scope of 7: REU + cartridge + the app's DMA hold (2(b), Mount & Run of step 8) in one go as "step 7 complete", or is Mount & Run its own step? (Per the rules: nothing is left out silently; this list is the proposal.)

### 5.10 The real SuperCPU ROM in simulation: the B flag hang (2026-11-08, branch `t182-realrom`)

Board finding of release main-2bd67963 (VICE `scpu64` 64 KiB, KERNAL 901227-03): blue screen, no text, keys dead, BASIC vectors `$0300-$030B` = 0. Every earlier bench ran only synthetic ROMs; the new local bench `cores/c64scpu/sim/run_realscpu.sh` (ROM files outside the repository, `C64_ROMDIR`, `+SROMLOG2=16`, `+PCHIST` opcode-fetch histogram, `+STKTRACE=<addr>` last stack writes before a fetch, `+SRAMDUMP`) runs the real files.
- **Path (read from the ROM image, not from comments):** reset vector `$FFFC` = `$FC90` -> `JML $F800FC` -> `JML $F880C1`: SEI, CLC, XCE, `REP #$38`, S=`$01FF`, three `MVN` copies of 8 KiB from `$F8:0100` (BASIC), `$F8:2100` (KERNAL), `$F8:4100` (KERNAL shadow variant, 3 bytes differ) to bank 1 `$A000`, `$E000`, `$6000`; `SEP #$30`; `$D07E/$D0B6/$D077` writes, `$C1` to `$D0B3/$D07B/$D073`, `$D07F` (hwenable off); `JSL $F88148` (SuperRAM size probe through `[$02]` and `$F6:0000`, `$D27B-$D27F`); then `PHB`, `LDA $FFFC`, `DEC`, `PHA`, `SEC`, `XCE`, `RTL` -> KERNAL reset `$00:FCE2` in emulation mode (hwenable off: vectors from the KERNAL copy: IRQ `$FF48`). The KERNAL's RESTOR (`$FD15`) is patched to `JSR $F38B` (copies 208 bytes of `$FBE0-$FCAF` to bank 1).
- **Divergence (sim, ROM-only fetch histogram):** reset, RAMTAS, CINT, `JMP ($A000)`, `$E394`, banner and `$A480` each run once; afterwards `$FF55` (`JMP ($0316)`, the BRK branch of the IRQ entry) runs on every IRQ and `$FF58` never; `$FE66` (RESTOR, IOINIT, CINT, `JMP ($A002)`) and BASIC's `$E37B` repeat for ever, the screen is cleared each time. Stack before the first `$FF48`: pushed P = `$32` (B = 1). The board's stack top `$01FC-$01FF` = `17 31 94 E3` is the same push: an IRQ at `$E394` with P = `$31`; its `$0300` vectors are 0 because the loop restarts BASIC before `JMP ($0300)` can work.
- **Cause:** `cores/snes/rtl/65C816` upstream `P65C816.vhd` (copied to `cores/c64scpu/cpu/s816_cpu.vhd:453`) pushes `P(4) or (not GotInterrupt and EF)`; P(4) is 1 all through emulation mode (XCE, RTI/PLP set it), so the term meant to give B = 0 for IRQ/NMI never did.
- **Fix:** `tools/scpu_vendor816.py` patches it to `P(4) and not (GotInterrupt and EF)` (BRK, COP, PHP push 1 as before; native mode pushes the X flag as before). S9 (`tools/gen_c64scpu_irq.py`) now also ORs bit 4 of the stacked P of every emulation-mode IRQ/NMI handler into `$0319`, expected 0.
- **Evidence:** unfixed CPU, `run_realscpu.sh` 1300 ms: pushed P `$32`, `$FF55` 146 times, empty screen. Fixed CPU: pushed P `$22`, screen `**** C=64 SCPU64 ROM V0.07 ****`, `16M RAM SYSTEM  38911 BASIC BYTES FREE`, `READY.`. The S9 check (pushed B = 0 on an IRQ) and the base benches have not run yet; they run with the next sim run of `c64scpu`.
- **Not proven:** the board (flash/bitstream not touched); timing of the 16 MB size probe on the board; keyboard input after READY; the C64-side KERNAL/BASIC/CHAR files from the SD are not used by the 65816 at all (it runs the ROM's own copies), only the character ROM matters.
- **Board pass (Alex, 2026-11-10, package main-13972dca):** the real SuperCPU ROM boots to READY on the board: "**** C=64 SCPU64 ROM V0.07 ****  16M RAM SYSTEM  38911 BASIC BYTES FREE" (photo). The B flag fix (5ad15cb4c) holds on hardware. Keyboard and BASIC work: `PRINT 1+1` gives 2 (second photo). Still open: a SuperCPU program.
- **D17 (Alex, 2026-11-10, translated: "build it. I only have Metal Dust to test, as a d81"):** the drive A of the SCPU core offers the 1581
  (`.d81`) as the C128 does: the gateware already has Gideon's `mm_drive` (1541 or 1581 by the app, `layer2_c64scpu.v:472`), so this is app
  only - `coreapp/cores/c64scpu.cc` takes the C128's 1581 parts (file types, 1581 ROM pick from the user's own ROM files, JiffyDOS 1581
  optional). Board proof: Metal Dust (`.d81`) loads and runs.
  - **Built (2026-11-10, branch scpu-1581, app only):** `c64scpu.cc` has a second `DriveRomPick` (`drv81`, kind `drive.put_rom1581`, 32 KiB only, quiet), applied on every
    change of the JiffyDOS setting like the 1541's; ROM rows `1581 drive` (`1581.rom`, `1581.bin`, `dos1581.rom`) and `JiffyDOS 1581 ROM` (`jiffydos-1581.bin`,
    `jiffydos1581.bin`, `jiffydos_1581.bin`, `jiffydos-1581.rom`) in `/SD/cores/c64scpu/roms/` (or the shared roms folder); file type `.d81` (Mount, Mount Read Only,
    Browse; no unlinked mount, as the C128's), task `Create blank D81`. The drive library (`cbm1541`) switches the mm_drive to the 1581 on a `.d81` mount, as for the
    C128; the register base is the library's, the gateware's is the same 0x10020000 (`layer2_c64scpu.v:472`, `layer2_c128.v:478`). Tests: `test_c64scpu` (descriptor rows,
    1581 ROM reaches the drive after a `.d81` mount, a 16 KiB file is no 1581 ROM), goldens `menu_c64scpu`, `keywalk`, `ids`, `romreq_table.inc` regenerated (only SCPU
    lines). `tools/dev.sh test` and `riscv` pass. Not proven: the board (Metal Dust `.d81`).

### 5.11 The CMD SuperCPU ROM V1.4 (128 KiB) does not start: the port ignores `$00` on the SuperCPU (2026-11-10, branch `t182-cmdrom`)

Board symptom (Alex, 2026-11-10): `scpu-dos-1.4.bin` as `scpu64.rom`: light-green border, black screen; V0.07 (64 KiB) boots. `run_realscpu.sh` now takes 64 and 128 KiB files (256 KiB refused: the bench image holds 128 KiB; review 2026-11-10) (`+SROMLOG2` = log2 of the size, as `coreapp/cores/c64scpu.cc:189` writes it).
- **Bench artifact first (not the board cause):** the first 128 KiB run looped on BRK at `$00:FC9C` (`pcl|00fc9c|00`, 47162 fetches). `tb_c64scpu.v` pulsed RESTORE on every 65816 write to `$00:0B08` (run_irq's hook); the ROM's RAM test writes `$00:0B08`, so a NMI hit while the boot map was already off (`c816|74|00d0b6|W|00`) and `$FCA4` read the zeroed SRAM. `+NMIDBG` showed `nmi|...|0|0|0|1` (only `ext_restore`). The hook is now `+RESTOREHOOK` (run_irq.sh passes it).
- **Cause (ours):** with the hook off the screen shows `SUPERCPU INITIALIZATION ERROR: 01-0`. The ROM's RAM test (`$F8:038A-03C7`) sets `$01` = `$E4` (`c816|89|000001|W|e4`) and never writes `$00`, then compares `$00:D000-DFFF` with the pattern `$5555`; `c816|1057052|00d012|R|3e` (the VIC raster, not RAM) fails it. VICE's SuperCPU takes the memory configuration from the written value alone: `scpu64mem.c:230-236` + `:273` (`pport_store(addr, value & 7)`, reset `mem_pport = 7`, `:1388`), the direction register is not in `mem_config` (`:213`). `scpu_pport.v` (the 6510 port) gave `pout[2:0] = (latch & dir) | (pin & ~dir)` and with `dir` = 0 (reset) the PLA stayed at 111 (I/O visible). V0.07 never relies on this.
- **Fix:** `scpu_pport.v`: `pout[2:0]` and `pout3_q` are the latch alone (bits 5:3 as before).
- **Evidence after the fix (builder, `run_realscpu.sh` 3000 ms):** V0.07 unchanged, `scr|    **** C=64 SCPU64 ROM V0.07 ****`, `16M RAM SYSTEM  38911 BASIC BYTES FREE`, `READY.`, `pch|00e5d4|2113708` as before. CMD 1.4 passes the RAM test and the error screen no longer appears, but reaches READY at 10 s sim time (next item).
- **Regression benches after the fix (builder, 2026-11-10, trx64-sim-env, tree = `git archive be2750ffd`):** `run_base.sh` PASS (boot, ctl_nobasic, ctl_nochars fail as they must), `run_map.sh` PASS, `run_irq.sh` PASS, `run_emu.sh` PASS, `run_speed.sh` PASS, `run_mirror.sh` PASS (final line `PASS run_<name>` each; no `^FAIL` line outside the `ctl|` controls). No bench failed, so the pport change needed no further fix.
- **CMD 1.4, 10 s (`run_realscpu.sh 10000`, builder, 2026-11-10):** `cycles: 9864262, frames: 501`, `PASS tb_c64scpu`, screen `SUPERCPU DOS 1.40 (C)1996 CMD`, `16MB CPU RAM    38911 BASIC BYTES FREE`, `READY.`. The raster wait at `$00:1485` ends (`pch|001485|1153110` fetches, then READY); the machine idles in `$00:14AB-14AE` (5691531) and `$00:E5CD-E5D4` (2267742).
- **2026-11-18 - board steps closed as done (shipped and in use):** CMD ROM 1.4 (the SuperCPU boots the CMD ROM V1.40), S7 (JiffyDOS: the switch, the
  JiffyDOS 1541 ROM in drive A, the load speed by hand), S4 (20 MHz against 1 MHz) - Alex 2026-11-15: "Under A there is nothing I would not class as DONE!
  That has been in the builds for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has
  been running for ages, we had this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
