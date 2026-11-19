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

Open (one line, as in the TODO table): Spec PROPOSED 11-19; tools copied (dis65816, emu816); S1 static read, S2/S3 sim, S4 board open

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
    before it runs at 20 MHz. Boom +2 holds `DEC $D0B6` (bootmap off, needs the hardware registers on) in its unpacked part.
- **Parts:**
  - **P1** What the programs do: SCPU registers in their order, speed, mirror mode, VIC bank and graphics addresses, how they are written
    (STA, long, MVN/MVP; at 1 or 20 MHz), SuperRAM use; whether the releases linked by Master share one loader.
  - **P2** Where the core parts from VICE: SRAM bank 0 against C64 RAM over the VIC's ranges while the intro shows, lost mirror writes;
    the comment's POKE line.
  - **P3** The fix, with Metal Dust (T208) unchanged.

## §2 Scenarios (proposed 2026-11-19; Alex's confirmation open)

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | Boom +4 and Boom +2 depacked and read: the items of P1 for each | `tools/emu816` (depack), `tools/dis65816` (listing) | Mac; program data only outside the repository |
| S2 | P2 | Boom +4 runs in the core's simulation with the real ROMs until the intro shows: SRAM bank 0 equals C64 RAM in the VIC's ranges | the `run_prgrun.sh` path (real ROMs) | local bench, never in CI |
| S3 | P2 | The comment's POKE line typed in BASIC: the machine returns to READY | same bench | local bench |
| S4 | P3 | Both Booms show their intro and run; Metal Dust unchanged | board step | board, Alex |

## §3 Decision

Open.

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
