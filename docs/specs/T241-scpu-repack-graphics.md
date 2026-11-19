# T241 — SuperCPU releases show on the C64SCPU core the picture their CPU wrote (GitHub #108)

**Status:** PROPOSED (2026-11-19, Alex). Investigation started: the 65816 tools are copied into `tools/` (§5).
**Repo:** TRX64U-MultiCore — the core `cores/c64scpu/` (`scpu_mem.v`: register file, memory map, mirror queue; `scpu_cache.v`), its
benches `cores/c64scpu/sim/`; the debugging tools `tools/dis65816` and `tools/emu816`.
**Number:** T241.
**Origin:** GitHub issue #108 (a tester; release 0.2.0, C64U, core c64scpu): "A large number of demo/repacks for SuperCPU are buggy",
"Texture bugs, thinks bad memory management"; programs CSDb 5043 (Boom +4 [scpu ramdisk], DMAgic, 1999) and CSDb 210475 (Boom +2 [1581],
SuperCPU release, linked by Master). Comment on the issue: "poke 53375,0 : poke 53364,0 : poke 53374,0 not pass in the core, but pass in vice".
Alex, 2026-11-19 (translated): "Do you have an idea why that could be?" - "It could be that Master packed them" - "copy the tools! We need them
for debugging."
**Depends on:** T182 (the core), T208 (mirror queue, BA write hold), T217 (the PRG start bench `cores/c64scpu/sim/run_prgrun.sh`).
**Milestone:** M2.

Open (one line, as in the TODO table): D1 static read of Master's Boom first (agent A); tools copied; S2/S3 sim only to confirm; S4 board

---

## §1 The problem

- **Who:** testers who run SuperCPU releases (cracks, repacks) on the C64SCPU core.
- **What happens:** the photo on #108 shows the DMAgic intro of Boom +4. The outline of the logo and of the picture is there; the pixel
  data (character set or bitmap) is wrong. Read as: the VIC shows C64 RAM that does not hold what the 65816 wrote into its SRAM. Not measured.
- **Known from the code (2026-11-19):**
  - The registers equal VICE line by line: `$D074-$D077` act only with the hardware registers on (`scpu_mem.v:430`, VICE `scpu64mem.c:701-708`);
    the mirror table and its index (`scpu_mem.v:148-160`, VICE `scpu64mem.c:81-86` and `:1472-1475`); the speed (`scpu_mem.v:107`, VICE
    `scpu64mem.c:734`, `:742`). In the comment's order (`$D07F`, `$D074`, `$D07E`) both ignore the `$D074` write. What remains is "hardware
    registers on": 20 MHz whatever the switch, the map index bit `hw`, the vector reroute (`scpu_mem.v:140`).
  - "Hardware registers on" sends every emulation-mode IRQ through the SuperCPU ROM's handler (vector `$F8:FFFE` = `$FC94`), read with
    `tools/dis65816` on 2026-11-19. Metal Dust (T208) never ran this path (native mode, its own vectors). The handler depends on the ROM:
    - CMD 1.40 (`scpu-dos-1.4.bin`, 128 KiB, SHA-256 `6a13f11188990a5a...`; the board's `/SD/cores/c64scpu/roms/scpu64.rom`, Alex:
      "the original"): `JML $00FC14`; saves `$D0B2` and `$D0BC`, `STA $D073`, `STA $D0BF`, `STA $D07F` (registers off), KERNAL IRQ `$FF48`
      with its return at `$FCB0`: `STA $D07E`, `$D0BC` and `$D0B2` restored, `RTI`. The program's IRQ code runs with the registers off.
    - VICE's own replacement ROM V0.07 (64 KiB, SHA-256 `1ca7732d7d61d1e6...`, what VICE ships): `JML $008054`; builds code on the stack
      and switches `$D07E`, `$D0B7`, `$D0B6`, `$D07F` around the KERNAL's `JMP ($FFFE)`.
    VICE run with its own ROM and the board with CMD 1.40 do not run the same handler.
  - A deviation from VICE, read from the code: a write to `$D200-$D3FF` (the SuperCPU's I/O RAM) lands in VICE only with the hardware
    registers on (`$D27E` always) and only in the SuperCPU's SRAM (`scpu64mem.c:838-855`); here it lands always (`scpu_mem.v:193`) and,
    because its class is a RAM write inside the mirror window, also in the C64 RAM at `$D200-$D3FF` (`scpu_mem.v:220`). ROM V0.07 writes
    there in its reset and RAM test (`$F8:815D-81EE`), not in the IRQ path; CMD 1.40's IRQ path above writes none of it.
  - Boom +4's packed file holds `SEI; STA $D07A; LDX #$FF; TXS` as plain bytes (a program start; `$D07A` = software 1 MHz); the decruncher
    before it runs at 20 MHz. A byte scan found `DEC $D0B6` in Boom +2's file; S1 showed it is inside the packed stream, not code: the program never writes
    `$D0B6`.
- **Parts:**
  - **P1** What the programs do: SCPU registers in their order, speed, mirror mode, VIC bank and graphics addresses, how they are written
    (STA, long, MVN/MVP; at 1 or 20 MHz), SuperRAM use; whether the releases linked by Master share one loader.
  - **P2** Where the core parts from VICE: SRAM bank 0 against C64 RAM over the VIC's ranges while the intro shows, lost mirror writes;
    the comment's POKE line.
  - **P3** The fix, with Metal Dust (T208) unchanged.

## §2 Scenarios (proposed 2026-11-19; Alex's confirmation open)

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Master's Boom +2 depacked and read (D1): the items of P1, each compared with the core's code and VICE | `tools/emu816` (depack), `tools/dis65816` (listing) | Mac; program data only outside the repository |
| S2 | P2 | Only to confirm what S1 leaves open (D1): Boom runs in the core's simulation with the real ROMs until the intro shows: SRAM bank 0 equals C64 RAM in the VIC's ranges | the `run_prgrun.sh` path (real ROMs) | local bench, never in CI |
| S3 | P2 | The comment's POKE line typed in BASIC: the machine returns to READY | same bench | local bench |
| S4 | P3 | Both Booms show their intro and run; Metal Dust unchanged | board step | board, Alex |

## §3 Decision

- **D1 (Alex, 2026-11-19): read first, simulate only to confirm; Master's Boom only.** Translated: "Wait - and what if you did NOT have
  the sim, but disassembled the game code, i.e. A, and then looked at what happens? That is how we found what was stuck in Metal Dust
  too." - "And it is enough to understand Master's Boom, so only one Boom for now." Master's release is CSDb 210475 (Boom +2 [1581],
  SuperCPU); the photo on #108 shows the DMAgic intro of CSDb 5043, which the tester names beside it ("or").

## §4 Out of scope

Open.

## §5 Building

### 2026-11-19: the 65816 tools copied (Alex: "copy the tools! We need them for debugging", translated)
- `tools/dis65816` (flow-following W65C816 disassembler with e/m/x tracking and a KickAssembler rebuild proof) and `tools/emu816` (W65C816
  instruction-set interpreter with a 6502 mode and an REU model; no SuperCPU registers, no VIC, no timing), copied from another of Alex's
  projects. Left out: that project's program configs and its run script. Changed: no absolute paths (the GitHub export refuses `/Users/...`);
  `--verify` finds KickAssembler by `--kickass`, `$KICKASS_JAR` or `/Applications/KickAssembler/KickAss.jar`.
- Unit tests in this repository: emu816 95 tests OK, dis65816 45 tests OK (Mac).
- Listings and depacked bytes of a program are its data: written outside the repository; `/analysis/` (dis65816's default output) is in
  `.gitignore`.

### 2026-11-19: Master's Boom on the board (Alex, video IMG_0501.MOV, kept outside the repository)
- Core c64scpu, CMD 1.40 ROM. This release has no intro. The game starts
  and plays (the score rises from 0003346 to 0004474 in the last 15 s), but "it stutters hard and then everything crashes".
- Read from the frames (1 and 3 per second, the last 24 s): the top HUD changes from frame to frame - the "PLAYER1" label above the score
  is missing in some frames, the HUD text is doubled sideways in others (the HUD row shown with the playfield's X scroll). At about 111.8 s
  of the video the picture freezes: the HUD boxes change colour (the raster colour splits stop), one enemy sprite stays over the HUD line,
  nothing moves. Read as: the raster IRQs come at the wrong time, then the IRQ chain dies or the CPU runs off.
- So the symptom of this program is IRQ timing and a crash, not wrong pixel data like the DMAgic intro of the photo on #108. Agent A
  (S1) got the observation and reads the game's IRQ side with the same weight.

### 2026-11-19: the comment's POKE line on the board (Alex)
- At READY, `POKE 53375,0:POKE 53364,0:POKE 53374,0` clears the screen and prints `READY.`; the cursor blinks and typing works.
  A cleared screen with `READY.` is BASIC's warm start (the path of STOP/RESTORE or a BRK through `$0316` = `$FE66`: RESTOR, IOINIT,
  CINT, `JMP ($A002)`), not the plain `READY.` under the typed line. This is what "not pass in the core" means.
- Read with it: after `$D07E` the next emulation-mode IRQ goes through the CMD 1.40 handler (`$FC94` = `JML $00FC14`), and with the
  hardware registers on `$E000-$FFFF` reads class KS (SRAM bank 1 `$6000-$7FFF`, the kernal shadow) instead of KT (bank 1
  `$E000-$FFFF`): `scpu_cfgtab.txt` rows `0x07` and `0x27`, equal to VICE (`scpu64_kernalshadow_read` = `mem_sram[0x8000 + addr]`,
  `scpu64mem.c:406-410`). Somewhere on that path the core reaches the warm start; the handler wrote `$D07F` before the KERNAL IRQ, so
  BASIC goes on with the registers off. Where exactly is open (S3).
- Alex on CSDb 5043 (translated): "the problem in the other version is, in my opinion, the intro". The two releases show two symptoms:
  the DMAgic intro's pixel data (photo on #108), Master's game timing and freeze (video above).

### 2026-11-19: the DMAgic intro (CSDb 5043) on the board (Alex, video, kept outside the repository)
- Alex (translated): "The intro is 'broken' .. in my opinion that points to a timing error with the VIC."
- Read from the frames (2 per second, 8.8 s): the outline of the "DMAgic" logo stays; its fill and the whole picture below change from
  frame to frame (bars, dots, a black block, half-drawn rows). Wrong data that stays put would look the same in every frame; this moves.
- A mechanism in the core that fits, read from the code, not measured: the 65816's memory is the DDR2 behind an 8 KiB cache; a cycle whose
  line is not in the cache withholds the CPU's enable (`ok`/`mem_ok`, `scpu_mem.v:392-404`), counted in DEV_CYC ("the deviation from the
  original", register page `0x064`, `c64scpu_regs.v`). At 1 MHz the enable is the PHI2 pulse (`tick = ce_raw`), so a withheld enable costs
  a whole PHI2 cycle; the real SuperCPU (SRAM) and VICE have no such wait. Code that races the beam drifts against the VIC. No application
  path reads DEV_CYC or the cache counters today.

### 2026-11-19: S1 - Master's Boom read statically (agent A; listings and data outside the repository)
- Stage 1 `boom scpu!` is a Pucrunch stage (depacked in `tools/emu816`, 6502 mode, from `$080D` to `JMP $510A`). Stage 2 reads `boom datas`
  through the KERNAL (OPEN/CHKIN/BASIN) and depacks it with `STA [$FD],Y` into SuperRAM banks `$02-$09`, then runs `$1087`:
  `STA $D07E; STA $D077; STA $D07F` (registers on, mirror everything, registers off; checked in the bytes),
  `$D011 = $1B`, BASIC menu "(M)IT/(O)HNE INTRO". The RAM-disk loader copied to bank `$20` moves each file into bank 0 with MVN at 20 MHz
  (`STA $D07B`, `$01 = $34`), then `STA $D07A`: the game and all its IRQs run at 1 MHz, emulation mode, hardware registers off. No access
  to `$D200-$D3FF`.
- IRQs (level engine "02", setup at `$56A4`, checked in the bytes): RAM vectors `$0314 = $5748`, `$FFFE = $5743`; per frame an IRQ at line
  `$57` (sprite pointers, `$D016` playfield scroll), 1-7 sprite multiplexer IRQs, and one at line `$D9` (`$588C`) that sets `$D016 = $18`
  for the HUD and then runs the whole game logic inside the IRQ until `RTI` at `$5B35`. It has to end before line `$57` of the next frame,
  about 182 lines (about 11,500 PHI2 cycles at 1 MHz). The CMD ROM's IRQ path is not used while the game runs.
- Fits the video: if the core's 1 MHz CPU is slower than a 6510, the logic IRQ overruns, the next frame's IRQs come late (HUD with the
  playfield's scroll, multiplexer rows late, a sprite left on the HUD) until the chain dies. Mechanism candidate, read not measured: the
  withheld enables of DEV_CYC (entry above). Open: a measurement of DEV_CYC / the cycles from `$588C` to `RTI` per frame on the core.

### 2026-11-19: 1 MHz timing on the board (Alex)
- `T=TI:FORI=1TO5000:A=I*I+A:NEXT:PRINTTI-T`, PAL, screen on: C64 core of Gideon's firmware 3.15a **1865**; this core after `POKE53370,0`
  (software 1 MHz) **1871** (+0.3 %). Plain code at 1 MHz runs at the C64's speed here: the withheld enables do not make it slow. The
  mechanism of the DMAgic entry above is not the cause for code like this; the run had the reset mirror mode (`$0200-$FFFF`) and no sprites.
