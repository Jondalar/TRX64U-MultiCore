---
name: core-move
description: Move a core out of the shared core app (coreapp/cores, coreapp/src/table.cc) into its own app (cores/<id>/app: sdk.toml, gateware.toml, lib) on the SDK (T239.3 D55, T239.5 D13, D15). Use when Alex says a core moves, before touching table.cc, coreapp/cores/<core>*, a core's gateware.toml or sdk.toml. Run tools/core_move.py first; this runbook is for what a script cannot decide.
---

# Moving a core to its own app

Source of the method: the VIC-20 (T239.3 V-1..V-5) and the A1200 (T213.3, 27 commits). Spec: `docs/specs/T239.3-app-core-sdk.md` D55; the rule that
a moved core leaves the old app is T239.5 D13. A core moves only when Alex says so (after his board test of the previous one); the dry run of D55
changes nothing.

## 1. Order

1. `python3 tools/core_move.py plan <core>`: the files that move, the shared files that cannot, the libraries, the RTL source list, the test lines.
2. `python3 tools/core_move.py dryrun <core>`: TOML A from the RTL, rtl_check, the sources compiled against its driver table, in a scratch folder. Read
   `table.md` there. Fix what is an SDK gap in the SDK (once, for every core), what is the core's in the core.
3. Branch from `t239.3`. `python3 tools/core_move.py apply <core> [--full-scan]`: `git mv` of the core's files to `cores/<id>/app/lib`, `gateware.toml`
   (`rtl_check.py emit`), `sdk.toml`, the row out of `table.cc` and `cores.h`; then the gates. `--full-scan` reads every byte of the 64 KiB window
   (minutes); without it only the first 4 KiB and the pages the headers name (seconds). Move with the full scan: a register that no header names and
   that lies beyond 4 KiB is missed otherwise.
4. Work the judgement list (section 3), the gates (section 4), the matrix (section 5), then the spec (section 6).

## 2. What the script has already decided (check it, do not redo it)

- `gateware.toml`: addresses, widths, directions are the RTL's. Names come from the old register header by offset; every other register is `r_<offset>`
  with `# TODO meaning`. The blocks are cut by holes of 4 bytes and at the first 16 bytes (the standard block); the module of a block is a placeholder.
  Dense regions (more than 64 registers in one block: a hand-over to the host's pixel layer, a RAM) are not emitted: the notes in the file's head name
  them. The A1200's `pixel.nframes` (0x101A60A6) is such a case: describe the one register by hand.
- `sdk.toml`: `base_blocks` = the VIC-20's and the A1200's common set (cart_regs, the ICAP blocks, partition, audio_master; iec_status with cbm1541);
  the libraries are the shared ones whose headers the core's code includes, with their `needs`.
- The core's code is the include closure of `coreapp/cores/<id>.cc` inside `coreapp/cores`. A file that another core's closure reaches too is listed
  as SHARED and stays (`hk_defaults.h`, `tape_deck.*`, `tape_pulses.*`, `drive_rom_pick.h`).

## 3. Judgement (what broke or hid in the VIC-20 and the A1200)

1. **Registers by number.** The moved sources include headers that name offsets as literals. Name them through `drvtab.h` as `amiga1200_regs.h` does
   (`AM_X = DRV_<block>_<prop>_ADDR - DRV_ident_ADDR`); the core's own code writes no I/O literal (`rtl_check.py lint-src`). Prove: the old and the new
   names have the same values (the A1200: all 59).
2. **Names and blocks of `gateware.toml`.** Split the emitted blocks into capabilities (the A1200's 17: machine, video, rtc, floppy, hdf, mbox ...),
   name the registers, set the widths the RTL's bytes do not tell (an 8-bit register next to another 8-bit register is one run for the emitter), runs
   with `count` and `stride` (floppy mounts every 0x10 bytes), the `module` of each block, the four characters of `[identity]`. Directions: the emitter
   writes what the RTL does; narrow a direction only for a reason (a write-only strobe that reads back).
3. **The block set the shared drivers need.** `hw.cc` names `ident.caps`, the host's blocks of `base_blocks`, and the drive through
   `#ifdef DRV_HAS_drive_a`. A driver that serves a block only some bitstreams have and does not test `DRV_HAS_<block>` breaks for a core without it
   (T213.3: `hw.cc`'s drive accessors). The dry run's table lists such files; the fix is in `coreapp/src`, with the test of api.md 6.17.5.
4. **`rtl_check` source list.** A core with `core_sources.tcl` uses it, unless the script has a filter the checker cannot decide (`ni $a12_own`): then a
   list `cores/<id>/app/rtl_check_sources.tcl` names the files once (the A1200's). A core without one gets a list from `core_toml.py sources`: remove the
   files the real build leaves out (a copy of a module the core replaces). Run the case in `tests/rtl_check/test_rtl_check.py` with the counts of the
   first run; a pattern the checker does not know (`Unsupported`, file and line) is a gap in `tools/rtl_check.py`, not a reason to leave the block out.
5. **Tests shared between cores.** Units of `tests/coreapp/run.sh` that name the core: regroup into the TOML B's `tests` by matrix step (unit, service,
   ui, api; `libtab.py tests`), built as the core's app (`a12app`, `vc20app`: its include path, its driver table, `TEST_APP_<CORE>`). A shared test file
   that has the core's cases in it keeps the old core's twin cases under the guard (T213.3: the A500 twin in `test_start` for the same-ID cases, the NES
   cases in `test_scale`, `a12_scale`). Every moved unit must pass once on the new build and the shared unit must still pass without the core.
6. **The native builds.** `tests/coreapp/srclist.sh` and `tools/menusim/run.sh` take the own cores from `core_toml.py lanes`; the render bench walks them
   with the board's table; the UI suite (`tests/ui`) runs a core with its own app on `COREAPP_<core>` and refuses the shared image for it. Goldens that
   hold the core's rows are regenerated by the unit that owns them, not edited.
7. **`test_cor_pack`.** The switch-off case of D25 (a core without `desc_record`) names a core that has not moved; once the last one has, the case needs
   another construction. The moved core's `.cor` carries `desc.toml` as record 6: `cor_pack` check.
8. **The old pipeline.** `core_toml.py list-own-app` (and `ci/trxapps.sh`, `ci/package.sh`) leave a moved core out of the shared image loudly, not silently:
   the integration wiring (T239.5 D13) must deliver its `.cor` from the matrix before `t239.3` merges.
9. **Comments the move makes wrong.** Search `coreapp`, `tests`, `docs`, `app/sdkpkg/api.md`, the core's descriptors and `mono/` scripts for the core's
   old paths (`coreapp/cores/<id>`, "the shared app", "table.cc"), the old record's description and the pipeline text. Fix them in the same commit.
10. **D24.** No core name in `coreapp/` or `uicore/`; `tools/check_d24.py` stays green; a shrunk per-core file shrinks its ledger line
    (`tools/d24_ledger.txt`), a removed one loses it.
11. **desc.toml.** It names registers as `"block.property"`; the values come out of the TOML A (6.14.20), so the number stands once.
12. **Hosts of the shared app that call the core by name.** `grep -rn "<id>_desc\|\"<id>\"" coreapp uicore host-app tests tools ci` after the move.

## 4. Gates (all green before the first matrix run)

`python3 tools/core_move.py gates <core>` runs: `rtl_check.py check` against the RTL; the core's sources and the SDK's driver sources compiled natively
(`g++-16 -fsyntax-only`, the core's own `drvtab.h` first); `tools/check_d24.py`; `tools/core_toml.py lanes` (the core must be listed as `<id> dfx|large`).
By hand besides: `python3 tools/check_todo.py`, `tests/rtl_check/test_rtl_check.py`, the units of the core's `tests` keys (one run), the menu simulator
(`tools/menusim/run.sh`) for the core's F5 page. No `tools/dev.sh` per step (CLAUDE.md): the coordinator runs the RISC-V build and the test units once at
the end.

## 5. The matrix (T239.5 D8): steps 1-13 must be green for the core's lane

1 lint; 2 unit tests; 3 what has changed; 4 comparison (TOML A against the RTL); 5 what must be in the app; 6 build (link from the SDK); 7 complete and
fits (`cor_pack --sizes`, the heap figures); 8 tests mock bitstream -> service; 9 service -> UI; 10 service -> API; 11 package (the `.cor` with its own
app); 12 emulator tests (the `.cor` under the host app); 13 deliver. Steps 8-10 are the units the TOML B names per step.

## 6. What goes into the core's spec (§5)

The commits by phase (library, TOML A, TOML B, tests, removal from the shared app); the emitted TOML A's numbers (blocks, registers, named, `# TODO`
left) and what the hand work changed; the SDK gaps the dry run found and where they were fixed; the shared files that were split or copied; the units
regrouped (old name -> new name, one PASS line each); the gates' output; what Alex tests on the board. A part that is not built is a decision with
Alex, written in §4 before the merge.
