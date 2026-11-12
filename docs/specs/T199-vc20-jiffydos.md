# T199 — The VC-20 core starts with JiffyDOS when the person chooses it, the KERNAL and the 1541 drive switched together

**Status:** IN PROGRESS (2026-11-10, Alex: "Todo, Spec, umsetzen direkt" - spec and build at once).
**Repo:** TRX64U-MultiCore — core app only: `coreapp/cores/vc20.cc` (descriptor), `coreapp/cores/drive_rom_pick.h` and `cbm1541_desc.h` (the
1541's ROM choice, as the SuperCPU core uses it), `tests/coreapp/`. No gateware.
**Number:** T199 (registry: `TODO.md`).
**Origin:** GitHub discussion #59 (2026-11-10): a tester renamed JiffyDOS files to the VC-20 core's expected names and got JiffyDOS, "only about
a 20% improvement in speed"; another comment asks for "a JiffyDos ROM and 1541 switch". Alex, 2026-11-10 (translated): "TODO, spec, build it
directly".
**Depends on:** T83 (VC-20 core), T104.1 D15 (the cbm1541 library), T182 D5 (the JiffyDOS drive ROM pick of the SuperCPU core, `drive_rom_pick.h`).
**Milestone:** M2.

---

## §1 The problem

- **Who:** a VC-20 user with JiffyDOS ROMs (VC-20 KERNAL and 1541 DOS), as on a real VC-20 with both chips swapped.
- **What they want:** a setting "JiffyDOS" Off/On; On starts the VC-20 with the JiffyDOS KERNAL and gives drive A the JiffyDOS 1541 ROM.
- **What happens today (code):** `coreapp/cores/vc20.cc` knows only BASIC, KERNAL, CHAR and the stock 1541 ROM (`roms[]`, `CBM1541_ROM`). JiffyDOS
  works only by renaming files over the stock names, which replaces the stock set and can switch one side alone (KERNAL without the drive or the
  reverse); with one side alone the transfer stays at the standard protocol - a likely reading of the tester's "20 %" (not measured).
- **Why it matters:** the two halves must always match; the stock files must stay usable.

Parts:
- **P1 Files:** two optional ROM rows: "JiffyDOS VC-20 KERNAL" (8 KiB) and "JiffyDOS 1541 ROM" (16 or 32 KiB), found by name in the core's `roms`
  folder beside the stock ones.
- **P2 Setting:** "JiffyDOS" Off/On in F5 Settings, taken at the next reset (the KERNAL is read at reset), stored in the core's `.cfg`.
- **P3 Switch together:** On puts the JiffyDOS KERNAL into the machine and the JiffyDOS 1541 ROM into drive A; Off the stock pair. A missing file
  of the pair leaves both on stock and says which file is missing (no half switch).
- **P4 Status:** the Cores and ROMs page lists the two rows as optional, so a missing JiffyDOS file is not an error for a stock start.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1, P4 | the two JiffyDOS rows are listed as optional with their file names; without them the core starts stock with no error | app test over a roms folder without the files | apptests |
| S2 | P2, P3 | setting On + both files: after the reset the KERNAL bytes the machine holds equal the JiffyDOS KERNAL file and drive A's ROM bytes equal the JiffyDOS 1541 file | app test (FakeIo reads back what was put) | apptests |
| S3 | P3 | setting On + only one of the two files: both stay stock, one line names the missing file | app test | apptests |
| S4 | P2, P3 | setting back to Off: after the reset both are stock again | app test | apptests |
| S5 | P2 | the setting survives a restart of the core (`.cfg`) | app test | apptests |
| S6 | All | board: VC-20 with JiffyDOS On, the start screen shows JiffyDOS, a LOAD from the 1541 is clearly faster than Off (stopwatch) | by hand | board (Alex) |

Test files are synthetic bytes (no ROM in the repository, clean room).

## §3 Decision

- **D1 (Alex, 2026-11-10):** build it, spec and build at once.
- Coordinator's proposal (as built, to be confirmed): file names as in the SuperCPU core's `roms[]` (`jiffydos-1541.bin`, `jiffydos_1541.bin`, ...)
  plus `jiffydos-vc20.bin` / `jiffydos_vc20.bin` / `jiffydos-vic20.bin` / `jiffydos_vic20.bin` / `jiffydos-kernal-vc20.bin` for the KERNAL; the
  1541 choice goes through `drive_rom_pick.h` like the SuperCPU core; the setting is taken at reset (WHEN_RESET), not live, because the
  KERNAL is only read at reset.

## §4 Out

- JiffyDOS for other drives (1540, 1571, 1581) and other VC-20 drives.
- Any JiffyDOS ROM in the repository, a `.cor`, a release or a package.

## §5 Build notes

2026-11-10, built on branch `t199-vc20-jiffy` (`coreapp/cores/vc20.cc`, tests in `tests/coreapp/test_vc20.cc`):

- Two optional `roms[]` rows (now 6): "JiffyDOS VC-20 KERNAL" (8 KiB; `jiffydos-vc20.bin`, `jiffydos_vc20.bin`, `jiffydos-vic20.bin`,
  `jiffydos_vic20.bin`, `jiffydos-kernal-vc20.bin`) and "JiffyDOS 1541 ROM" (names as the SuperCPU row). The stock 1541 row moved from the
  `CBM1541_ROM` macro to an own row (`vc20.put_drive_rom`) so both drive ROMs go through `drive_rom_pick.h`; the ".rom" file action
  "Set as 1541 ROM" calls `vc20.set_drive_rom` (the library's own set_rom would bypass the choice).
- Setting "JiffyDOS" Off/On, WHEN_RESET, operation `vc20.set_jiffy`. The KERNAL puts keep the bytes (stock written at once as before); the
  setting's operation runs in `boot_apply_settings` while the core is held (start, Reload ROMs, Reset) and writes the JiffyDOS or the stock KERNAL
  and gives the drive the matching ROM. Only a complete pair switches; a missing file gives one line naming it and both stay stock (the line
  is the operation's message and a console line).
- Tests (synthetic bytes), `test_vc20`: S1 (rows optional with their names; stock start without files, On changes nothing), S2 (On + both files:
  KERNAL aperture and drive ROM read back equal the files, after a cold reset and at a start with On), S3 (each of the two files alone: stock
  stays, the line names the other file), S4 (Off + reset: stock again; Off with files present: stock), S5 (`.cfg` round trip). Fixed along
  the way: `test_cbm1541` addressed the 1541 row as the last ROM (now index 3); the goldens `menu_vc20.txt`, `ids.txt` and `keywalk.txt` gain the JiffyDOS row (regenerated with UPDATE_GOLDEN=1).
- `host-app/software/hostapp/romreq_table.inc` (generated from the descriptors, checked by run.sh) gains the two rows.
- Not checked: S6 (board, Alex).

S6 board 2026-11-11 (Alex, host update 5cc93ede, vc20.cor b9e86830, the PAL JiffyDOS KERNAL and the JiffyDOS 1541 ROM in `/SD/cores/vc20/roms/`): JiffyDOS shows after Reload ROMs and a LOAD is faster ("works, is faster", translated). PASS.
