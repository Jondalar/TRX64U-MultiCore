# T206 — Every core that mounts a disk also offers "Run Disk" (mount and run), with the same action names

**Status:** PROPOSED (2026-11-11). Corrected 2026-11-13 (status check): BUILT, merged to main (475cdbcfe); S1, S2 proven by test_desc; PET added 2026-11-12; SCPU Run Disk now types through the key matrix (decision A, 2026-11-12, merged with 475cdbcfe). 2026-11-15 sync: S3 board is open for all of them, no pass recorded: SCPU, C128 (READY delay), PET (D64/D80/D82), Amiga Mount DF0 and reset; SCPU/C64 `Mount & Run` of a PRG still pokes RAM and has the same SCPU page-0 problem (§5, needs its own decision).
**Repo:** TRX64U-MultiCore — the generic core app, the file actions of the cores (`coreapp/cores/c64scpu.cc`, `c128.cc`, `cbm1541_desc.h`, ...).
**Number:** T206.
**Origin:** Alex, 2026-11-11, at the board in the SCPU core (F3 Library on a disk: Mount Disk, Mount Disk Read Only, Browse Disk, ... no run), translated:
"And when we do Mount, then please always also 'Mount Disk and run' or 'Run Disk'. That is inconsistent."
**Depends on:** nothing.
**Milestone:** M2.

---

## §1 The problem

- P1: the shared 1541 actions (`cbm1541_desc.h`, VC-20 and others) offer "Run Disk" (`drive.mount_run`) beside "Mount Disk"; the SCPU core's own
  tables (`c64scpu.cc` d64/g64/d81) have no run action; the C128's (`c128.cc`) has only one.
- P2: the names and the order of the disk actions differ between cores.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | every disk image type every core can mount (d64, g64, d71, d81, ...) lists "Run Disk" next to "Mount Disk"; it mounts and runs the first program | core app test over every descriptor | apptests |
| S2 | P2 | the disk actions have the same names and order in every core (one table per image type, or a check that compares them) | core app test | apptests |
| S3 | P1 | SCPU: Run Disk on a d64 starts the program (board) | by hand | board (Alex) |

## §3 Decision

- **D1 (Alex, 2026-11-11):** wherever a disk is mounted, "Run Disk" is offered too; the same names in every core.

- SCPU (Alex 2026-11-12, board: Run Disk ends at READY with nothing typed): the app's poke of the keyboard buffer lands in C64 RAM, the
  SuperCPU reads bank 0 from its own SRAM (scpu_mem.v 178-181; T182 step 7b open). Decision A (Alex): type through the core's keyboard
  matrix like a real keyboard instead of writing memory - app only, no new bitstream.

## §4 Out

- The PET core (T134, experimental) gets Run Disk too: Alex 2026-11-12, "the PET too please, run disk, yes" (translated). Built on `t206-pet-run` (see §5).

## §5 Build notes

Built 2026-11-11 (branch t206-rundisk):
- SCPU (`c64scpu.cc`): d64/g64/d81 use the library tables of `cbm1541_desc.h` (Run Disk included); the core got `c64scpu.type_script` (type_op) on the shared `cbm_run` job with the C64's BASIC numbers ($0801, TXTTAB $2B, buffer $0277/$C6, ready 1000 ms as c64.cc; not measured on this core). Inner files (.prg inside a d64/d81): Mount & Run uses `c64scpu.run_prg` (run_op, `cbm_run_queue`: reset, wait for READY, place, RUN/SYS), Real Run types `LOAD"NAME",8,1` via type_op; names and order from `c1541_inner_actions`.
- S1/S2 proof: `tests/coreapp/test_desc.cc` walks every descriptor (the C64 through its merged record): each CBM image type has the reference names in the reference order, `drive.mount_run` and a `type_op`; every other mounting image type has a "... and cold start / and reset" action. Run 11-11: 12 CBM image types and 13 others walked, green. `test_c64scpu.cc`: Run Disk on a d64 and a d81 types `LOAD"*",8,1` / `RUN` after BASIC is up, in pieces. Golden `menu_c64scpu.txt` regenerated on the builder (diff = the three filetype lines). RISC-V `dev.sh riscv` ok.
- C128 (`c128.cc`, second commit): d64/g64/d81 use the library tables; `c128.type_script` (type_op) on the shared `cbm_run` job; Run Disk is the shared `drive.mount_run`, so it types `LOAD"*",8,1` then `RUN` exactly as for the C64 (BASIC 7 `,1` loads to the file's own address, which for a C128 BASIC program is $1C01; plain `LOAD"*",8` would relocate a machine-code file wrongly; `RUN"*"` would need its own script and loses the ML case). Values and sources: BASIC 7 text start $1C01, TXTTAB $2D/$2E (VARTAB $2F, ARYTAB $31, STREND $33), keyboard buffer KEYD $034A (ten keys), count NDX $D0 - from the C128 Programmer's Reference Guide memory map; **no C128 KERNAL source or ROM is in this repository, so they were not checked against code**; the bench only proves the core uses them. READY: first look 2000 ms after the reset, then 500 ms settle, give-up 6000 ms after that (guesses, a 128-mode boot is slower than the C64's 1000 ms); Alex's S3 step measures them. C64 mode: a reset starts 128 mode (nothing holds the C= key), so Run Disk always lands in 128 mode; a machine found in C64 mode has TXTTAB $0801, never matches $1C01 and the start is dropped with the "did not reach READY" notice. No inner Mount & Run on the C128 images (no run_op: Run PRG needs the bank-0 relink of BASIC 7, not built; the C128 images list no inner files). `test_c128.cc`: reset happens, nothing typed before 2 s even when RAM already holds $1C01, text lands in $034A/$D0 (not $0277/$C6), C64-mode pointer types nothing and tells why.
- Amiga (`amiga.cc`): .adf got "Mount DF0 and reset" (`amiga.mount_reset`): `win_mount` slot 0, then `boot_cold_reset` (as "Reset Amiga": reloads the Kickstart when the choice names another file); a mount that fails resets nothing. The name follows the other machines' "... and reset" (CPC "Mount A: and reset"); it sits second, after "Mount DF0", so RETURN on an .adf still only mounts. Both Amiga records (A500, A1200) have it. `test_amiga500.cc`: action present at position 2, a missing file mounts and resets nothing, a good file resets once. DF1-DF3 have no reset variant (the Kickstart boots from DF0).
- `test_desc.cc` no longer has an exemption list: every core that mounts a disk fails the walk unless it has Run Disk or a "... and reset/cold start" action. The C64U record (menu.toml) has d64/g64 in the reference form and no d81 entry.
- S3 (SCPU and C128 Run Disk on a d64, Amiga Mount DF0 and reset on an ADF) is Alex's board step; it also measures the C128's and SCPU's READY delay.
- PET (`pet.cc`, branch t206-pet-run, 2026-11-12): d64/d80/d82 got "Run Disk" (`pet.mount_run`) after "Mount in Drive 8" / "Mount in Drive 8.1" (the PET has two drives of one unit, so it keeps its own mount rows and the names of the CBM reference tables do not apply). The op mounts the image in drive 8 (`drv_mount`, which resets by itself when the unit type changes), then queues the shared `cbm_run_queue_script` with `LOAD"*",8` and `RUN` (ten-key pieces; no `,1`: secondary address 0 loads to the start of BASIC; `LOAD`, not `DLOAD`, because BASIC 1/2 have no DLOAD). `test_desc.cc` no longer skips PET: it must have `Run Disk` on `pet.mount_run` and a `type_op`, and is not compared with the reference tables. Golden `menu_pet.txt`: the three `filetype .d64/.d80/.d82` lines gain `| Run Disk`, nothing else. Picture: `tests/ui/features_cores/pet_shots.feature` (new profile `profiles/pet.toml`, PET in `fixtures.py`/`emu.py`): the F3 sheet of `alpha.d64` lists Run Disk. NOT seen: the typed start running. ue2emu has no PET machine (the profile answers registers only), so READY and the program are not proven here; S3 for PET is Alex's board step (D80/D82 as well, and the READY delay: `ready_ms` 2200).
- SCPU typed start, decision A (branch t206-scpu-run, 2026-11-12): Run Disk (`c64scpu.type_script`) no longer pokes the keyboard buffer. Why: the 65816 reads bank 0 from the SuperCPU's own SRAM and mirrors a store to the C64 RAM only in the pages of `mir_tab` (`scpu_mem.v`; reset mode $C7 gives $02-$FF), so the application's `ram_write` of `$C6`/`$0277` (and the `$2B/$2C` read for READY) never meets the CPU's copy of page 0. Now: `CbmRunMachine::screen` switches the shared job to the key matrix (`CoreIo::matrix_keys`: `MATRIX_KEYB` bytes 0-7 at `U2P_IO_BASE + 0x300`, `host/ultimate_sys.vhd` page 001, OR-ed with the physical keys by `host/app_input.v` and carried to the core as `c64_keys` -> `layer2_c64scpu.v` `keys` -> `ext_keys`); keys are pressed 90 ms down, 90 ms up, L SHIFT leads a shifted key by 40 ms (the quote is SHIFT+2), RETURN waits for the screen at $0400 (page 4, mirrored, and what the VIC shows) to end in a lone `READY.` row before the next character. Other cores keep the buffer (`screen` 0). Proof: `test_c64scpu.cc` (key sequence, timings, shift lead, no buffer write, nothing before READY); the machine is not in ue2emu, so the board step S3 stays Alex's. Not fixed, found: Mount & Run of a PRG (`c64scpu.run_prg`, `cbm_run_queue`) places the program with `ram_write` (`cbm_prg_place`) and reads `$2B/$2C` for READY, so on the SCPU it hits the same page-0 / SRAM split (a BASIC program's pointers and the RUN typed into the buffer do not reach the CPU); it needs its own decision.
- t206-scpu-run finished 2026-11-12 (rebased on main 8a7fdd45e): `dev.sh test` (apptests fast tier, 8 coreapp programs incl. `test_c64scpu`, negative controls failed as they must) and `dev.sh riscv` (coreapp.bin 4567a921) pass on the builder. The cause is read from code (scpu_mem.v `mir_tab`, `cbm_run.cc` buffer poke), not measured on the board; whether the key matrix path types and starts the disk is S3, Alex's board step (also whether 90 ms down/up and the READY test at $0400 hold on the SCPU).
