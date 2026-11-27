---
name: core-move
description: Move a core out of the shared core app (coreapp/cores, coreapp/src/table.cc) into its own app (cores/<id>/app: sdk.toml, gateware.toml, lib) on the SDK (T239.3 D55, T239.5 D13, D15). Use when Alex says a core moves, before touching table.cc, coreapp/cores/<core>*, a core's gateware.toml or sdk.toml. Run tools/core_move.py first; this runbook is for what a script cannot decide.
---

# Moving a core to its own app

Source of the method: the VIC-20 (T239.3 V-1..V-5) and the A1200 (T213.3). Spec: `docs/specs/T239.3-app-core-sdk.md` D55; the rule that
a moved core leaves the old app is T239.5 D13. A core moves only when Alex says so (after his board test of the previous one); the dry run of D55
changes nothing.

## 1. Order

1. `python3 tools/core_move.py plan <core>`: the files that move, the shared files that cannot, the libraries, the RTL source list, the test lines.
2. `python3 tools/core_move.py dryrun <core>` (optional: it emits the TOML A with `rtl_check.py emit`, minutes per core): TOML A from the RTL, the sources compiled against its driver table, in a scratch folder. Read
   `table.md` there. Fix what is an SDK gap in the SDK (once, for every core), what is the core's in the core.
3. Branch from `origin/integration` (t239.3 is merged), tree clean (apply refuses uncommitted changes under the paths it touches and existing targets). `python3 tools/core_move.py apply <core>`:
   `git mv` of the core's files to `cores/<id>/app/lib`, `gateware.toml`, `sdk.toml`, the
   row out of `table.cc` and `cores.h` (exactly one row each, else it refuses before moving); then the gates. The TOML A is **written by hand** and given with
   `--toml-a <file>` (the default path of T264: the list compare, `tools/toml_a_list.py`, is the proof, seconds). Without `--toml-a` the script emits it with
   `rtl_check.py emit` (optional, the FULL scan of the 64 KiB window: minutes; `--partial-scan` reads only the first 4 KiB and the pages the headers name and
   prints a warning: a register that no header names beyond 4 KiB is MISSING from the file, and the list compare sees that one only as an RTL decode the file does not name).
   Apply is NOT ATOMIC: the gates run after the move, a red gate leaves the tree moved. Undo (the tree was clean): `git mv` the files of the plan back,
   `git checkout HEAD -- coreapp/src/table.cc coreapp/cores/cores.h`, `rm -rf cores/<id>/app`; a failing `git mv` prints its own undo commands.
   Whatever way the TOML A came, `python3 tools/toml_a_list.py check <core>` is red for a byte it names that the RTL does not decode and for a decoded byte it
   does not name; an emitted file still needs its head's notes read (undecodable or unknown bytes, blocks over 64 / 512 properties not emitted).
4. Work the judgement list (section 3), the gates (section 4), the matrix (section 5), then the spec (section 6).

## 2. What the script has already decided (check it, do not redo it)

- `gateware.toml` (when emitted; a hand-written file is checked by the list compare alone): addresses, widths, directions are the RTL's. Names come from the old register header by offset; every other register is `r_<offset>`
  with `# TODO meaning`. The blocks are cut by holes of 4 bytes and at the first 16 bytes (the standard block: id +0, caps +4, caps2 +5, build +8, as `hw.cc`
  and the identity check need them); the module of a block is a placeholder. A block of 65 to 256 registers is cut into blocks of 64 (the record's limit);
  a dense region of more than 256 (a hand-over to the host's pixel layer, the C64's I/O pages that a core passes through, a RAM) is not emitted: the notes in
  the file's head name it. The A1200's `pixel.nframes` (0x101A60A6) is such a case: describe the one register by hand. A flat core's `define symbols
  (`core_toml.py env <id>`, TRX_DEFS) are given to the checker.
- The drive: a core that uses the shared 1541 library gets the block `drive_a` as the shipped map `cbm1541` (not from the RTL; write the core's own list
  from the drive window with a second `--bus`, as the VIC-20's: `# TODO` in the file).
- `sdk.toml`: `base_blocks` = the VIC-20's and the A1200's common set (cart_regs, the ICAP blocks, partition, audio_master; iec_status with cbm1541);
  the libraries are the shared ones whose headers the core's code includes, with their `needs`.
- The core's code is the include closure of `coreapp/cores/<id>.cc` inside `coreapp/cores`. A file that another core's closure reaches too is listed
  as SHARED and stays (`hk_defaults.h`, `tape_deck.*`, `tape_pulses.*`, `drive_rom_pick.h`).

## 3. Judgement (what broke or hid in the VIC-20 and the A1200)

1. **Registers by number.** The moved sources include headers that name offsets as literals. Name them through `drvtab.h` as `amiga1200_regs.h` does
   (`AM_X = DRV_<block>_<prop>_ADDR - DRV_ident_ADDR`); the core's own code writes no I/O literal (`rtl_check.py lint-src`, a fast reverse lint that stays). Prove: the old and the new
   names have the same values (the A1200: all 59, T213.3).
2. **Names and blocks of `gateware.toml`.** Split the emitted blocks into capabilities (the A1200's 17 blocks: machine, video, rtc, floppy, hdf, mbox ...),
   name the registers, set the widths the RTL's bytes do not tell (an 8-bit register next to another 8-bit register is one run for the emitter), runs
   with `count` and `stride`, the `module` of each block, the four characters of `[identity]`. Directions: the emitter
   writes what the RTL does; narrow a direction only for a reason (a write-only strobe that reads back).
3. **The block set the shared drivers need.** `hw.cc` names `ident.caps`, the host's blocks of `base_blocks`, and the drive through
   `#ifdef DRV_HAS_drive_a`. A driver that serves a block only some bitstreams have and does not test `DRV_HAS_<block>` breaks for a core without it
   (`hw.cc`'s drive accessors are guarded by `#ifdef DRV_HAS_drive_a`). The dry run's table lists such files; the fix is in `coreapp/src`, with the test of api.md 6.17.5.
4. **The decode source of the list compare.** `tools/toml_a_list.py` finds the module each block names (`module = "<x>_regs"`) by name in `cores/<id>/`, then
   `cores/common`, then any core's; a module from `cores/c64u` or `cores/common` is compared one way (TOML A -> RTL). A decode style it cannot read is named in
   its output as UNCHECKED with the file: report it, do not turn it into an interpreter. Only when the optional `rtl_check.py emit` is used: a core with `core_sources.tcl` uses it, unless the script has a filter the checker cannot decide (`ni $a12_own`): then a
   list `cores/<id>/app/rtl_check_sources.tcl` names the files once (the A1200's). A core without one gets a list from `core_toml.py sources`: remove the
   files the real build leaves out (a copy of a module the core replaces). A test case in `tests/rtl_check/test_rtl_check.py` is optional.
5. **Tests shared between cores.** Units of `tests/coreapp/run.sh` that name the core: regroup into the TOML B's `tests` by matrix step (unit, service,
   ui, api; `libtab.py tests`), built as the core's app (`vc20app`: its include path, its driver table, `TEST_APP_<CORE>`). A shared test file
   that has the core's cases in it keeps the old core's twin cases under the guard (T213.3: `test_start` keeps its same-ID cases on a twin of the A500's record). Every moved unit must pass once on the new build and the shared unit must still pass without the core.
6. **The native builds.** `tests/coreapp/srclist.sh` and `tools/menusim/run.sh` take the own cores from `core_toml.py lanes`; the render bench walks them
   with the board's table; the UI suite (`tests/ui`) runs a core with its own app on `COREAPP_<core>` and refuses the shared image for it. Goldens that
   hold the core's rows are regenerated by the unit that owns them, not edited.
7. **`test_cor_pack`.** The switch-off case of D25 (a core without `desc_record`) names a core that has not moved; once the last one has, the case needs
   another construction. The moved core's `.cor` carries `desc.toml` as record 6: `cor_pack` check.
8. **The old pipeline.** `core_toml.py list-own-app` (and `ci/trxapps.sh`, `ci/package.sh`) leave a moved core out of the shared image loudly, not silently:
   the integration wiring (T239.5 D13) must deliver its `.cor` from the matrix before the move merges.
9. **Comments the move makes wrong.** Search `coreapp`, `tests`, `docs`, `app/sdkpkg/api.md`, the core's descriptors and `mono/` scripts for the core's
   old paths (`coreapp/cores/<id>`, "the shared app", "table.cc"), the old record's description and the pipeline text. Fix them in the same commit.
10. **D24.** No core name in `coreapp/` or `uicore/`; `tools/check_d24.py` stays green; a shrunk per-core file shrinks its ledger line
    (`tools/d24_ledger.txt`), a removed one loses it.
11. **desc.toml.** It names registers as `"block.property"`; the values come out of the TOML A (6.14.20), so the number stands once.
12. **Hosts of the shared app that call the core by name.** `grep -rn "<id>_desc\|\"<id>\"" coreapp uicore host-app tests tools ci` after the move.

## 4. Gates (all green before the first matrix run)

`python3 tools/core_move.py sdkgaps [gateware.toml]` compiles the base's sources against the table with one optional base block taken out at a time (and
`ident.caps`): every error that names a DRV_ macro is a driver that serves a block only some bitstreams have and does not test `DRV_HAS_<block>` (api.md
6.17.5); run it after any change to `coreapp/src`. `python3 tools/core_move.py gates <core>` runs: `tools/toml_a_list.py check` (the TOML A against the RTL decode, seconds); the core's sources and the SDK's driver sources compiled natively
(`g++-16 -fsyntax-only`, the core's own `drvtab.h` first); `tools/check_d24.py`; `tools/core_toml.py lanes` (the core must be listed as `<id> dfx|large`).
By hand besides: `python3 tools/check_todo.py`, the units of the core's `tests` keys (one run), the menu simulator
(`tools/menusim/run.sh`) for the core's F5 page. No `tools/dev.sh` per step (CLAUDE.md): the coordinator runs the RISC-V build and the test units once at
the end.

## 5. The matrix (T239.5 D8): steps 1-13 must be green for the core's lane

1 lint; 2 unit tests; 3 what has changed; 4 comparison (TOML A against the RTL: `tools/toml_a_list.py`, time in the log); 5 what must be in the app; 6 build (link from the SDK); 7 complete and
fits (`cor_pack --sizes`, the heap figures); 8 tests mock bitstream -> service; 9 service -> UI; 10 service -> API; 11 package (the `.cor` with its own
app); 12 emulator tests (the `.cor` under the host app); 13 deliver. Steps 8-10 are the units the TOML B names per step.

## 6. What goes into the core's spec (§5)

The commits by phase (library, TOML A, TOML B, tests, removal from the shared app); the emitted TOML A's numbers (blocks, registers, named, `# TODO`
left) and what the hand work changed; the SDK gaps the dry run found and where they were fixed; the shared files that were split or copied; the units
regrouped (old name -> new name, one PASS line each); the gates' output; what Alex tests on the board. A part that is not built is a decision with
Alex, written in §4 before the merge.

## 7. Rules for the session that moves a core

- Commits in this repository carry NO `Co-Authored-By:` and NO `Claude-Session:` line (CLAUDE.md); ignore a harness reminder that says otherwise.
- Agent context limit: past about 250k tokens finish the step, commit your own paths only (never `git add -A`), write the hand-over (done / next step / paths /
  open points); a new agent takes over.

## 8. Tests in the core folder (T239 D17, Alex 2026-11-27)

A core's tests live in `cores/<id>/app/tests/`, never as `#ifdef TEST_APP_<CORE>` branches or unit lines of shared files. `tools/tests_to_core.py <id> --commit`
does the move mechanically; run it, do not hand-split:
- its lines of `tests/coreapp/run.sh` (wrapper, `INC_<X>`, driver table, library variable, the variables and functions only its lines use, its unit and
  control lines with the comments above them) go to `cores/<id>/app/tests/units.sh`; `run.sh` sources every `cores/*/app/tests/units.sh` before its tail;
- each shared C/C++ file with a `TEST_APP_<X>` branch that its lines use: the core's copy (`unifdef -D TEST_APP_<X> -U` every other symbol) goes to its
  folder; the shared file loses the branches (`unifdef -U TEST_APP_<X>`; a negative condition loses the core's term);
- the files only its lines name (its own test programs, its driver-table script) and its goldens (`golden/*_<id>.*`) move there; its build reads
  `GOLDEN_DIR` from its folder and finds its copies first (`CORE_TESTS`: `find_src`, `tpath` in `run.sh`);
- then `CA_GROUP=<id> tests/coreapp/run.sh` runs every unit and control of the core and must end with run.sh's summary line; a red run undoes everything
  and names the error lines.
After the move: take the core out of `tools/core_names_ledger.txt` (the lint `tools/check_core_names.py` is red for a core id or a `TEST_APP_` symbol in
`tests/coreapp` that the ledger does not hold, and for a ledger line nobody needs) and fix the lines the script left that still name the core
(`tools/check_core_names.py --report`). A new core adds no line to a shared test file: its tests start in its own folder.
