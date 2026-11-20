# T241 — SuperCPU releases show on the C64SCPU core the picture their CPU wrote (GitHub #108)

**Status:** LATER (2026-11-20): the intro's cause is fixed (the RMW write, §5) and goes into `integration` (D3); Master's Boom in play
is open (S2). PROPOSED 2026-11-19 (Alex).
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

Open (one line, as in the TODO table): intro fixed (RMW write; into integration, D3); play: IRQ one instruction early, RTI 5 cycles (S2)

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

- **D2 (Alex, 2026-11-20): the fix does not go to `main`.** Translated: "T241 SuperCPU bug not to main please. I do not want to
  discourage [a contributor]." The branch `t241-rmw` keeps the fix and its benches (run 2535 green, §5); it is not merged and not deleted.
  The GitHub export holds T241 (Alex, 2026-11-20, translated: "Then do it with the drop list. I must write with him first."): its spec
  and todo files and its TODO rows (T241, T241.<n>) stay as GitHub `4febf035` has them, and a commit that changed only them is not
  exported (`tools/github_snapshot.py`, `HOLD_*`). The export stops on a message naming T241 or the issue #108, a new line naming
  T241 (or #108 in the docs and the release notes' text), a path naming T241, a `HOLD_CHECK` phrase, and a commit or merge that drops
  or moves a held row. 19 scenarios and 4 probes tested by an agent (2026-11-20). Alex lifts the hold.

- **D3 (Alex, 2026-11-20): T241 goes to GitHub and into `integration`; D2 is lifted.** Translated: "T241 can come along now; get it
  ready, i.e. a branch, and then into integration too, please." The branch `t241-scpu` on GitHub carries the fix of `t241-rmw` and the
  bench of `t241-bench` (S2); `integration` gets it by Alex's merge (T239.1 D15).

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
- Same line after setting Master's mirror mode `$D077` (mirror everything) under `SEI` (`$C000`: `SEI; STA $D07E; STA $D077; STA $D07F;
  CLI; RTS`, then `NEW`): **1716**, 8 % less than the C64 core. A real SuperCPU and VICE run 1 MHz at the C64's speed whatever the mirror
  mode, so this is a deviation of the core, in the mode Master's Boom plays in. Open: whether the CPU really runs faster (cycles the VIC
  takes, e.g. badlines, not taken from it) or the jiffy clock loses ticks (IRQs lost); a stopwatch tells them apart (the C64 needs 31.1 s).

### 2026-11-19: a contributor's patch on #108 (`scpu-d07x-ungated.patch`), read
- It drops the hwenable gate of `$D074-$D077` (`scpu_mem.v:430`: `optim` written whatever `hw`) and changes the bench's VICE model
  (`tools/gen_c64scpu_speed.py`, class `Vice`) the same way, so the bench keeps passing without checking VICE any more.
- VICE gates these writes (`scpu64mem.c:701-708`, `if (mem_reg_hwenable)`), and T182 decided "only with hwenable" (T182 line 258).
- Effect on what T241 found: Master's Boom sets `$D077` with the registers on (`$1087`), so nothing changes for it; the comment's POKE line
  would switch to "mirror `$8000-$BFFF` only" (the BASIC screen at `$0400` is no longer written to the C64 RAM) and still end in the warm
  start. A C64 program that writes the VIC's mirrors `$D074-$D077` (for example a loop over `$D000-$D0FF`) would change the mirror mode.
- Follow-up in the same mode (Alex, board): the timing line again, **1712** jiffies (28.5 s) against a stopwatch of about **30.2 s**
  (the C64 core: 31.1 s): the jiffy clock loses about 6 % of its ticks, and the program runs about 2-3 % faster than on the C64 (the
  stopwatch's error is a few tenths). Stores are right: `A=0:FORI=1TO5000:A=A+1:NEXT:PRINTA,I` prints `5000 5001`. Read-modify-write in
  zero page is right outside IRQs: 51,200 x `INC $FB` with carry into `$FC` (`$C100`) leaves `0 200`. (A `BREAK` and a `?SYNTAX ERROR`
  on the photos came from typing errors, not from the core.)
- So the deviation sits in what mirror-everything changes at 1 MHz around interrupts and VIC cycles: lost or skipped IRQ ticks, and
  cycles the VIC should take that the CPU keeps. Next: a bench in the core's simulation that shows where (S2 under D1).

### 2026-11-19/20: S2 - the mirror-everything bench (overnight agent) and its review (read-only agent)
- The bench: branch `t241-bench` (on Gitea, commits 88afc905f, f7fef04ae, ca3d5b062; on GitHub in `t241-scpu`, D3): `tools/gen_c64scpu_irqmir.py`,
  `cores/c64scpu/sim/{tb_irqmir.vh,run_irqmir.sh,check_irqmir.py}`. Four phases at software 1 MHz with the display on: A0/A1 reset mirror
  mode `$C7`, IRQs masked/on; B0/B1 mirror everything (`SEI; STA $D07E; STA $D077; STA $D07F`), IRQs masked/on. IRQ: CIA1 timer A,
  latch `$4025`, vector `$01:FFFE`. Two synthetic programs ("simple"; "kernal", shaped like the KERNAL IRQ and BASIC arithmetic).
- Measured, PHI2 cycles: simple A0 234,946 / B0 234,855, A1 791,791 / B1 792,062; kernal A0 361,968 / B0 362,019, A1 1,217,547 /
  B1 1,217,628. In every IRQ phase underflows = IRQ edges = vector pulls = handler entries = the jiffy byte (48 simple, 74 kernal).
  VIC cycles the CPU kept: 0. The bench's checker exits 1 on +1/+2-cycle start offsets of its model, not on an A/B difference.
- The review, with evidence: B really is mirror everything (`scpu_mem.v:430`, `:148`, `:158`; zero-page and stack stores go to the C64
  bus only in B, `fwd1 wk3` in the logs); software 1 MHz holds in all phases (`scpu_mem.v:107`, `:403`, `:410`). With the registers off
  the board's IRQ runs the plain KERNAL through `$01:FFFE` = `$FF48` (the boot copies `$F8:0359-$0374` to `$01:FFE4-$FFFF`); the CMD
  wrapper of §1 runs only on the rerouted path, with the registers on. The bench uses the same bus classes as that path.
- What the bench does not cover: no DDR2 back-pressure (the model accepts every write at once; `ram_hold`, `n_wlost`, `dev_cyc` never
  loaded); 61 cache misses against real BASIC and KERNAL conflict misses; and the board's mode A was never measured: CMD 1.40 writes
  `$D077` itself on its cold path (`$F8:038A` to `$F8:04A6`) when the DDR2 signature at `$01:6100` differs, so a power cycle gives `$C0`
  and a core reset with the DDR2 kept gives `$C7`. The 1871 run may already have been in `$C0`.
- Read from the RTL: no condition here makes B lose jiffies and run faster at once. Every board-only effect withholds `core_ce` or
  lowers RDY and only adds PHI2 cycles; an IRQ is lost only when its ack comes more than 16,422 cycles late, which slows the run by the
  same time. So the board pair "TI about 6 % slow, loop 2-3 % faster" needs another cause.
- Two leads, read next (readers A-C, 2026-11-20): (1) with the registers on, an emulation IRQ is rerouted to `$F8:FFFE` = `$FC94`, but
  `$E000-$FFFF` then reads the KS shadow, which the boot fills from `$F8:8000-$9FFF` and which is zero at `$FC00-$FCE1`: the IRQ would
  fetch `BRK`. The comment's POKE line runs with IRQs on (the warm start above), and the game's `$1087` sequence has no `SEI`. (2) The
  warm start runs IOINIT, which loads the NTSC latch `$4295` when `$02A6` = 0: TI at 57.8 Hz would give 1712-1716 jiffies in 30.2 s.

### 2026-11-20: readers A-C - the IRQ path with the registers on, the warm start, the latch, the game (read-only; listings outside the repository)
- Lead (1) above is wrong. RESTOR runs at every reset (KT `$FCF8`), every warm start (`$FE66`) and through `$FF8A`. It writes `$D07E`
  at KT `$FD15` and goes on in the shadow at `$FD18`; the loop at KS `$FD24-$FD37` copies `$F8:FBE0-$FCC7` to `$01:FBE0` and to the
  shadow at `$01:7BE0`. So KS `$FC94` holds `JML $00FC14` before any IRQ (read in the board's ROM at `$F8:9D24`). With the registers on, an IRQ runs the CMD handler of §1, as VICE does with this ROM
  (`scpu64mem.c:1741-1744`); an NMI goes through `$FC8C` to the same handler and `JMP ($0318)`. No deviation of the core was found on
  this path. The core has no RAMLink flag (`$DF7E`/`$DF7F`, VICE `scpu64mem.c:1152-1162`, `:1743`); it matters only with a RAMLink.
- The comment's POKE line, why the warm start: the line ends with `$D07E` (registers on). BASIC ends a direct-mode line with
  `JMP $E386` (`$F8:A854`). With the registers on, `$E000-$FFFF` reads the shadow. CMD 1.40's shadow holds `STA $D07F; RTS` at
  `$E383-$E386`, where its KERNAL holds `LDX #$80; JMP ($0300)`. The `RTS` returns through stale stack bytes. After a cold boot they
  lead, by reading, through `$A67A` (CLR) to KS `$E39D` = `$00`, a `BRK`. The `BRK` goes through `$FC94`; `$FF48` sees B=1 and jumps
  through `$0316` to `$FE66`, the warm start. VICE with CMD 1.40 takes the same path. VICE's own ROM V0.07 has a shadow equal to its
  KERNAL except `$FDFC-$FDFE`; its `$E386` is `LDX #$80; JMP ($0300)`, so BASIC prints `READY.` there. The two runs differ by the ROM,
  not by the core.
- Lead (2) above is wrong as written. The whole ROM writes `$02A6` once: CINT at KT `$FF68`, which only the cold start calls. A warm
  start runs IOINIT, which loads the latch the cold start chose. CINT is the stock routine (KT `$FF5B`, byte for byte): it sets the raster compare to line 311, clears the screen, waits for `$D012` = 0 and reads the compare
  flag. On a C64 at 1 MHz the clear takes more than a frame, so line 311 always passes before the poll ends. CMD 1.40 runs CINT at the
  switch's speed: its reset path in the shadow writes `$D07B` (KS `$FCFD`), and nothing writes `$D07A` before CINT. At 20 MHz the clear
  takes about 2,000 PHI2 cycles (reader B's estimate from the write rules, not measured). If the poll then starts before line 256, it
  ends at line 256 with the flag clear: `$02A6` = 0, and IOINIT loads the NTSC latch `$4295` (57.80 Hz). This is the ROM's code at the
  ROM's speed, so VICE would do the same; the result depends on the raster line at which CINT starts. Not known: whether a real
  SuperCPU hits it.
- Against the board numbers: the 1871 run fits the PAL latch. The 1716 run came after the `$C000` routine, which ends with the
  registers off and calls no IOINIT. Without a reset between the two runs the latch was still PAL. So the NTSC latch explains the 1716
  run only if a cold boot came between them (then 1712 jiffies take 29.62 s; the stopwatch read about 30.2 s). The order of resets on
  the board is not recorded.
- Master's Boom (reader C): the `$1087` sequence runs once per boot, with IRQs on and only CIA1 timer A enabled. An IRQ in its window
  (2-6 PHI2 cycles out of 16,422) runs the CMD handler and returns; it changes nothing. During play the game uses raster IRQs only. It
  never reads `$02A6`; its `CMP $D012` loops (`$1392`, `$1D52`, `$1DDF`) wait for multiplexer lines. At 1 MHz a cache miss costs a PHI2
  cycle only when it queues behind another DDR2 job; by count it cannot break the tightest raster deadline (the `$57` handler: about 29
  cache lines, slack at least 157 cycles). The glitch frames above (HUD with the playfield's X scroll, "PLAYER1" missing) match the
  `$D9` handler not running in that frame: one IRQ of the `$57` -> multiplexer -> `$D9` chain was lost. An overrun of the game logic
  would look the other way round (playfield in the HUD's scroll and colours, HUD right). The freeze is not explained.
- Next: one board run that measures, over a fixed number of PHI2 cycles (CIA2 timers chained), in the reset mirror mode and in mirror
  everything: CIA1 timer A underflows (counted by CIA1 timer B), IRQ handler entries (a counter in the `$0314` chain), the TI delta,
  loop iterations, `$02A6` and the latch. It tells lost IRQs from lost TI increments from an NTSC latch, and a faster CPU from a slower
  clock, without a stopwatch.

### 2026-11-20: reader D - the DMAgic intro read statically; cause found: no read-modify-write write in emulation mode
- The release (CSDb 5043): a BASIC start, a pure 6502 decruncher at the switch's speed, then the intro at `$0840`. The intro writes
  `$D07A` (software 1 MHz) and runs in emulation mode with the registers off, pure 6502 code. It uses VIC raster IRQs only: four
  handlers in a chain (lines `$10`, `$7A`, and `$88`/`$89` as a double IRQ with a jitter fix and timed splits). Each handler
  acknowledges with `ASL $D019`.
- The cause, read from the code: the 6510 writes the unmodified byte in the modify cycle of a read-modify-write instruction, then the
  new byte. VICE's 65816 does the same in emulation mode (`65816core.c:888-890` abs, `:1032-1034` abs,X, `:500-502` dp, `:584`
  dp,X). The core's 65816 makes that cycle an internal one without a write (`s816_mcode.vhd:139`, `:1036`; write enable only for
  OUT_BUS other than 000, `s816_cpu.vhd:464-469`). So `ASL $D019` writes only the shifted byte, whose bit 0 is 0. The VIC clears the
  raster flag only from bit 0 of a `$D019` write (`registers_scaled.v:1543-1544`). The IRQ is never acknowledged; it comes again after
  every `RTI` or `CLI`, and the chain runs back to back instead of at its lines. The VIC mode writes land on wandering lines: the
  logo's colour RAM over a text mode, the picture on #108.
- Reproduced on a stock PAL C64 (c64re runtime): the intro with its two acknowledging instructions moved to an address without
  effect, same cycles. At line 100, `$D011`/`$D018`/`$D016` change 37 times in 50 frames (unchanged intro: never). The frames show
  the kind of picture of the photo.
- Master's Boom acknowledges with `INC $D019` in its level engine (five places): the same cause fits its HUD faults, the stutter and
  the freeze. Not checked further.
- Why no bench caught it: every IRQ bench of this core acknowledges with a load and a store (`tools/gen_c64scpu_irq.py`,
  `tools/gen_c64scpu_mdust.py`).
- Ruled out for the picture (reader of the write path, 2026-11-20): the DDR2 write FIFO. The C64 RAM is four pinned block-RAM slots
  (`NSLOT(4)`, `PIN(1)`, `c64_mem_c64scpu.v:71`); every write lands in its slot in its own clock, and a refused FIFO push only misses
  the DDR2 backup (`c64scpu_ramd.v:129-135`, `:192-198`). Cache waits give at most a cycle of jitter in the intro's timed parts.
- The fix (on the branch `t241-rmw`; not merged, D2): in emulation mode the modify cycle of every read-modify-write instruction writes the byte read
  before, as VICE models the W65C816S; native mode and the cycle counts stay as they are. It goes into the generator
  `tools/scpu_vendor816.py`, which writes `s816_cpu.vhd`. Built on the branch `t241-rmw`; proof on the board: a routine that waits for
  the raster flag, runs `ASL $D019` and reads bit 0 of `$D019` back prints 0 on a C64 and 1 on this core today.

### 2026-11-20: the fix on branch `t241-rmw` - in emulation mode the modify cycle writes the byte read
- What changed: `tools/scpu_vendor816.py` (function `rmw_write`, one more item in its list of changes against upstream) makes
  `cores/c64scpu/cpu/s816_cpu.vhd`. `--check` prints "ok: cores/c64scpu/cpu is what tools/scpu_vendor816.py makes". The SNES copy
  `cores/snes/rtl/65C816` and the microcode `s816_mcode.vhd` are unchanged.
- The rule: with EF = 1, the row of a read-modify-write instruction with STATE_CTRL 110 and VA 00 is a write (`RMW_DW`). D_OUT is DR,
  the byte latched at the enable of the read before it. WE is 0, VDA is 1, the address increment is 0. The row's own increment is 01:
  without the override the write would go to the read's address + 1. With EF = 0 the cycle stays internal. VICE makes no store there
  either (`LOAD_LONG_DUMMY` is a clock without an access, `scpu64cpu.c:407`).
- The opcodes, from VICE: the store `if (reg_emul) STORE_LONG(ea, var)` is in the four load macros of the read-modify-write
  instructions, `65816core.c:500-502` (dp), `:584` (dp,X), `:888-890` (abs), `:1032-1034` (abs,X). The dispatch (`:2623-3624`) uses
  them for ASL, ROL, LSR, ROR, DEC, INC in all four modes and for TSB, TRB in dp and abs. `TRBTSB` (`:2412-2427`) calls the same load
  macro, so TSB and TRB write the byte read too. These are 28 opcodes: `$06 $0E $16 $1E`, `$26 $2E $36 $3E`, `$46 $4E $56 $5E`,
  `$66 $6E $76 $7E`, `$C6 $CE $D6 $DE`, `$E6 $EE $F6 $FE`, `$04 $0C`, `$14 $1C`. They are upstream's MLB list (`rmw` in
  `s816_cpu.vhd`); the condition uses that list. The accumulator forms access no memory.
- The rows, decoded from the 53-bit words (field order of `s816_pkg.vhd`, the generator's `FIELDS`) and walked state by state with
  EF = 1: each of the 28 opcodes visits exactly one row with STATE_CTRL 110 and VA 00. It is state 5 for dp and abs
  (`s816_mcode.vhd` lines 59, 75, 123, 139, 187, 251, 331, 395, 587, 651, 843, 907, 1611, 1675, 1867, 1931) and state 6 for dp,X and
  abs,X (lines 204, 268, 460, 524, 716, 780, 972, 1036, 1740, 1804, 1996, 2060). The row before it is the 8-bit read (STATE_CTRL 110,
  VA 10, increment 0). The row after it writes the new byte (increment 0, last cycle). Neither row changes AA, DX, AB or DBR
  (ADDR_CTRL 0, IND_CTRL bit 1 = 0, LOAD_DKB 0): the write goes to the read's address. The other rows with STATE_CTRL 110 and VA 00
  are PHA, PHY, PHX state 1 (lines 599, 743, 1767); their opcodes are not in the list.
- Cycles with EF = 1, from the walk: dp 5 (6 with DL not 0), dp,X 6 (7), abs 6, abs,X 7, as on the 6510 and in VICE. No state is added.
- The write on the bus, read in the code (no run). `cpu65816_c64scpu.v`: bus_cyc is VDA (`:142`), wr_cyc is set (`:143`), address
  and data go to the C64 bus when the class forwards the cycle (`:190-192`). In a write D_IN is D_OUT (`:209`), so DR keeps the byte.
  A write is not held by RDY (`:149`), as the write cycles of a 6510; the internal cycle was held. `scpu_mem.v` decides as for a store
  to the same address (`:174-235`): I/O is WK_BUS, PHI2-synchronous (`:240`), at 20 MHz under the BA hold of I/O writes (`:369-373`).
  Bank-0 RAM is SRAM through the cache's write-through (`:247`); in the mirror range at 1 MHz also the C64 RAM in the same PHI2 cycle
  (`:220-221`), at 20 MHz through the mirror queue and the write buffer (`:248-253`, `:341`). Hardware registers are WK_INT (`:426`).
  `$00`/`$01` is the port (`cpu65816_c64scpu.v:196-202`): an RMW on `$01` writes the port twice, as on a 6510.
- So the new cycle is an ordinary write: one cycle, the read's address, the byte read. At 1 MHz it takes the one PHI2 cycle the
  internal cycle took. At 20 MHz the instruction keeps its cycles, but the write costs what a store of its class costs, as in VICE
  (`store_long`, `scpu64cpu.c:353-361`; the VIC store `scpu64io_d000_store`, `scpu64mem.c:884-904`, stretches every store): one more
  PHI2-synchronous access for I/O, one more write-buffer wait for mirrored RAM. Unmirrored SRAM: the new byte waits for the DDR2 job
  of the byte before it (`wbusy`), counted in DEV_CYC, as every back-to-back SRAM write.
- The bench case (`tools/gen_c64scpu_irq.py`, `cores/c64scpu/sim/run_irq.sh`, new `cores/c64scpu/sim/check_irq.py`). Phases 7-9:
  emulation mode, hwenable off (the vector at `$01:FFFE`, as phase 3), a VIC raster IRQ at line `$80`. The handler counts in 16 bits
  and acknowledges with `ASL $D019` at software 1 MHz (the intro's case), `INC $D019` at 1 MHz (Master's Boom's case), `ASL $D019` at
  20 MHz (the other bus path). Each phase lasts 40,000 PHI2 cycles (2.03 PAL frames). Verdict `$0327`-`$0329` = `$AA` for 2..3 handler
  runs: each IRQ once a frame. Without the write the flag stays set: the IRQ is taken again in the last cycle of every RTI
  (`s816_mcode.vhd:538`, `s816_cpu.vhd:496`, `:508-510`), so the main program runs no further instruction. It stops in phase 7,
  `$0327`-`$0329` stay `$00`, and every later expectation fails.
- Write counts: `check_irq.py` counts the tb's `mark|n|cycle|frame` lines, one per completed 65816 write to `$00:0B00-0B03`
  (`tb_c64scpu.v:183-185`). Emulation mode, 20 MHz: `LDA #$41; STA $0B02; ASL $0B02` = 3 writes to `$0B02`, `$82` in the C64 RAM
  (+EXPECT). Native mode: `SEP #$20; LDA #$7F; STA $0B03; INC $0B03` = 2 writes to `$0B03`, `$80` at `$03:0030` (+DEXPECT). The
  native case is 8-bit, so it walks the same rows as the emulation case; only EF differs. The old core never gets here (0 writes): it stops in phase 7.
- The run is 900 ms of machine time (was 800): the program ends at about 755 ms by count (740,000 PHI2 cycles of phases). The two
  controls fail where they failed before (the rerouted phases, the native phases and the S3 block); `ok` now also needs the counts.
- Light checks run on the Mac: the generator writes its program (tail 1,398 of 2,048 bytes, native image 574 of 1,536); the new code
  read back with `tools/dis65816`; `check_irq.py` on two made-up logs (3 and 2 writes: ok; 2 and 2: FAIL); `sh -n run_irq.sh`.
- Not verified: no simulation, bench or Vivado run (sims run in the pipeline; the coordinator pushes with `[sims]`); the GHDL synthesis
  of the changed VHDL; the timing of the new paths (IR, EF, microcode word to RMW_DW, then to the address and to WE, rdy_in and the
  CPU's enable) against the multicycle constraints of `build_mono.tcl`; the bench itself (its expectations come from the program, not
  from a run); the board.

### 2026-11-20: run 2535 (branch `t241-rmw`, `f59163935`, `[sims]`)
- Sims of the core (tier 2): `c64scpu_irq` PASS (840 s; phases 7-9 and the write counts), `c64scpu_speed` PASS (335 s), `c64scpu_mdust`
  PASS (1,410 s), `c64scpu_mirror`, `c64scpu_map` and `c64scpu_emu` PASS. `c64scpu_base` FAIL after 13 s: a link error of two Verilator
  builds in one `obj` folder (`run_base.sh` took no build lock beside `run_emu.sh`), not this change; fixed on `main` (T182 5.12).
- Vivado (mono c64scpu): routed, WNS +0.076 ns, WHS +0.027 ns. The earlier c64scpu runs gave WNS +0.004 to +0.066 ns (runs 2013-2475),
  so the new paths cost no slack that shows. The `.cor` was packed (child run 2539).
- Not merged (D2): the branch keeps the fix.

### 2026-11-20: the T241 core on the board (Alex)
- Core `c64scpu_t241.cor` (run 2535, branch `t241-rmw`; ROMs and config copied from the `c64scpu` folder). Alex (translated): "That fixes
  the intro, yes, but not Boom itself. When I go into the game: it stutters, and after a short time it crashes." - "Stutter like unclean
  $D016 scrolling" - "Crash: stack?"
- So the missing RMW write was the cause of the DMAgic intro's picture. Master's Boom in play has another fault, open.

### 2026-11-20: S2 - Master's Boom in play, read and run (agent B; data, listings and traces outside the repository)
- Unpacked from the BASIC menu into play. The game loads through `$7BF8` (`SEI; JSL $201150; RTS`; MVN at 20 MHz with `$01 = $34`):
  `Z3`, `Z1`, `Z2`, `DE`, `0D` (the in-play IRQ chain, dispatcher `$112C`), `03`, `02` (engine, main loop `$42D1`), `01`, then after the
  title `09`, `DE`, `E0`, `08`, `07`, `06`, `05`, `04`, `00`; play starts at `$6BD8`. Play runs at 1 MHz, emulation mode, SuperCPU
  registers off, only 6502 opcodes (79 executed). The in-play image ran in a c64re sandbox as a stock PAL C64, the loader stub replaced
  by injecting the files.
- Reference, 532 frames of level 1: three raster IRQs per frame, lines 87, 145 and 217; SP 228-236 at every entry. The dispatcher reads
  `$D012` at `$1158`, 62 cycles after the IRQ sequence starts, and takes the mid handler `$136C` only for `$92` (146,
  `CMP $628E`); the earliest read lands at line 146 cycle 0 (92 of 532 frames): no margin. `$D016` is written at line 89 (playfield)
  and line 219 (`$18`, HUD); `$D018` never.
- Cause (read, then a proxy run): the core decides on an IRQ at the end of an instruction's last cycle with the live line
  (`cpu65816_c64scpu.v:103,111`, `cpu/s816_cpu.vhd:496,508-510`); VICE polls it before the last cycle (`65816core.c:129`
  `CHECK_INTERRUPT`, placed e.g. at `:851`, `:1498`, `:2230`). An IRQ that rises in an instruction's last cycle is taken one
  instruction early on the core. Then the dispatcher reads 145, takes the multiplexer path `$1D2A`, which writes `$D012 = $91` again,
  and the chain stops for that frame: no mid and no bottom handler (no `$D016 = $18` for the HUD, no game logic, no re-arm), no top
  handler in the next frame. That is the stutter with the HUD at the playfield's scroll. Once the main loop's spin (`$42D1`) puts the
  next IRQ on an instruction's last cycle again, it repeats every frame: the freeze. Not a stack fault.
- Proxy runs on the stock C64 with the dispatcher made shorter: 1 cycle gives stutter (89 of 710 mid dispatches read 145), 2 cycles
  give the freeze from the first such frame on (1116 frames on `$1D2A`, picture frozen, HUD at the playfield's scroll). On the
  reference run 112 of 532 mid IRQs meet the core's condition.
- Also read: RTI in emulation mode takes 5 cycles on the core (`cpu/s816_mcode.vhd:535-538`, `cpu/s816_cpu.vhd:152`), 6 in VICE
  (`65816core.c:2217-2240`): one cycle per IRQ.
- Not measured on the core yet. Confirmation: the core simulation with the in-play image (per IRQ: the cycle `irq` rose, the vector
  fetch, the `$D012` value read at `$1158`, the next fetch), or a small bench that steps the main loop's phase by one cycle per frame
  on the C64 and the C64SCPU core.
- Fix direction (not built): decide on the IRQ from the level at the end of the second-to-last cycle (register it at each `core_ce`,
  keep the low-phase drop for 20 MHz), and give the emulation-mode RTI its second internal cycle.

### 2026-11-20: to GitHub (D3)
- The branch `t241-scpu` on GitHub: Gitea `main` with `t241-rmw` and `t241-bench` merged (the spec's three conflicts took `main`'s newer
  status and the fix's sections of `t241-rmw`; the TODO row is `main`'s), carried over file by file onto `integration`. Every code file
  the branches change was the same on GitHub as on Gitea `main`; the text went through the export's rules (`tools/github_snapshot.py`
  `PATTERNS`, `NAMES`), which changed nothing.
- `run_irqmir.sh` stays out of `ci/sims.txt` on purpose and is listed in `ci/sims_excluded.txt` with that reason (the lint of the first
  PR run was red without it): it is the diagnosis bench of S2, with no negative control, not a proof.
- Review before the push (agent, 2026-11-20): no blocker. The code files equal the merged Gitea tree byte for byte; every added line of
  both branches is there; `tools/scpu_vendor816.py --check` is ok (the VHDL is the generator's); native mode and the cycle counts are
  unchanged (`RMW_DW` needs `EF = '1'`); the generators embed no ROM or program bytes. Fixed: the stale "local" of the bench branch.
- Not run for this step: no sim, bench or Vivado run (the pipeline builds the c64scpu core on the push; its sims run on `integration`).
