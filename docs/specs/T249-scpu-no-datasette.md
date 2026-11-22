# T249 — c64scpu: no datasette

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-22: S1, S2 PASS (integ-0.2.1), S3 by reading, keywalk golden new; open: the merge
**Repo:** TRX64U-MultiCore — `cores/c64scpu/mono/core.toml`, `coreapp/cores/c64scpu.cc`, `coreapp/cores/c64scpu_regs.h`.
**Number:** T249.
**Origin:** Alex, 2026-11-21 (translated): "And then, before you optimise further: the datasette out of the core, there is no
SuperCPU software for the datasette."
**Depends on:** T214 (it gave the c64scpu its datasette setting), T248 (the branch this starts from).
**Milestone:** M2.

---

## §1 The problem

- **What is built today:**
  - The define `TAPE` in the `mono_env` of `cores/c64scpu/mono/core.toml` builds the cassette port block `c64u_c2n_l2`
    (`cores/c64scpu/layer2_c64scpu.v`, the `ifdef TAPE` at line 509): file playback, recording and the C2N pins.
  - Build 44875582642 (`utilization_hier.rpt`): `c2n` 369 LUT, 215 FF, 1 RAMB18.
  - The core app shows the setting "Datasette port" (Off (file only) / External datasette), `coreapp/cores/c64scpu.cc:68`. Its
    op `c64scpu.set_dset` writes `PIN_CTRL` (0x078).
  - The c64scpu has no `.tap` file type (`coreapp/cores/c64scpu.cc:395-402`), so nothing feeds the file playback.
- **Why it goes:** no SuperCPU software comes on tape (Alex). The chip has 96.7 % of its slices in use, and route delay is 79 % of
  the worst path into the CPU (T248 §5).
- **The parts:**
  - **P1** The c64scpu bitstream has no cassette port block.
  - **P2** The core app of the c64scpu has no datasette setting.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1) no cassette block in the bitstream
  Given the next c64scpu build of the pipeline from branch t249-scpu-no-tape
  When its utilization_hier.rpt and its timing summary are read
  Then there is no c64u_c2n_l2 instance, and the design meets timing

Scenario: S2 (P2) no datasette row
  Given the core app with the c64scpu descriptor
  When the settings of the core are listed
  Then no setting is named "Datasette port", and no op c64scpu.set_dset is registered   # unit test tests/coreapp/test_c64scpu.cc, menu simulator picture of the settings

Scenario: S3 (P2) an old settings file still loads
  Given a c64scpu.cfg written before this change, with a "Datasette port" entry
  When the core app loads its settings
  Then every other setting gets its saved value, and the "Datasette port" entry is not read   # code: coreapp/src/settings.cc:103-107 reads each row by its own key
```

## §3 Decision

- Alex, 2026-11-21 (translated): "the datasette out of the core, there is no SuperCPU software for the datasette."
- **D1 (P1):** `TRX_DEFS` in `mono_env` loses `TAPE`. `layer2_c64scpu.v` builds its `else` branches: they release the C2N pins
  (the IOBUFs of `mono/top.v` get enable 0) and answer the two register pages of the block (data 0, ack 1). The C64 reads READ
  idle and SENSE high. The capability word names `CAPAB_C2N_STREAMER` and `CAPAB_C2N_RECORDER` only with `TAPE`. No code reads
  these bits today; the word names only blocks the core has (review, 2026-11-21).
- **D2 (P2):** the row "Datasette port", the op `c64scpu.set_dset` and the register name `SCPU_PIN_CTRL` go out of the core app.
- **D3:** branch `t249-scpu-no-tape` from `t248-scpu-depth`, so one pipeline run shows T248 and T249 together.
- T214 gave the c64scpu its datasette. From 2026-11-21 the c64scpu is not in the scope of T214 (recorded in T214 §5).

## §4 Out

- The `ifdef TAPE` code in `layer2_c64scpu.v` stays in the file; without the define it is not built.
- The other Commodore cores keep their datasette (C128, VC-20, C16/Plus4, PET).

## §5 Built

- **2026-11-21, branch `t249-scpu-no-tape`** (from `t248-scpu-depth`, D3):
  - `36bd6abb`: P1, `TAPE` out of `mono_env`; P2, no "Datasette port" row, no op `c64scpu.set_dset`, no `SCPU_PIN_CTRL`;
    the S2 unit checks; the D24 ledger shrinks.
  - `ea2586e5`: the review's fixes (below).
- **Review (one agent): FAIL** before the push: the goldens (`menu_c64scpu`, the ids) and the manual (11.1.15, 11.20.2)
  still had the Datasette row. Fixed in `ea2586e5`; with it, the capability word names the C2N streamer and recorder only
  with `TAPE` (D1, `cf1c0e48`).
- **S1 PASS** (run 44880276432, green; T241 S2, T248 and T249 together): no `c64u_c2n_l2` in `utilization_hier.rpt`; the
  design meets timing. WNS +0.093 ns (T248's run 44875582642: +0.432 ns); the T248 line "smem->cpu worst setup slack"
  +0.334 ns (`b_a_reg[9]` -> `P_reg[1]`; it was +0.648 ns). LUT 22,952 (70.40 %), 392 fewer than run 44875582642.
- **S2:** the menu simulator's picture of the c64scpu settings has no Datasette row (kept outside the repository). The
  unit result is not read from a log: the apptests job of run 44880276432 skipped the coreapp units ("will run: nothing").
- **S3:** by reading `coreapp/src/settings.cc:103-107` (each row is read by its own key). Not run.
- `tools/dev.sh test` failed (rc 1): tree-sitter is missing in the dev container. Not fixed; the pipeline's apptests job
  ran green.
- **Open:** the S2 unit result from a log; the merge, together with T248 and T241 S2.
- **2026-11-21, `integ-0.2.1`:** T249's two commits (`6b3e7bbc`, `121360a2`) are on `t241-scpu-fix` without T248
  (T241 §5), not on `t248-scpu-depth` as D3 said. `tests/coreapp/golden/keywalk.txt` was not made again for the row this
  removes (the review): `CA_GROUP=cross UPDATE_GOLDEN=1 tests/coreapp/run.sh` moves 85 lines, all of them "C64 with
  SuperCPU" (F1, F3, F5, F8); no other core's line moves.
- **S2 PASS (2026-11-22):** the unit result from a log. Tree `572ea62e` (`integ-0.2.1`), Mac, `tests/coreapp/run.sh`:
  "test c64scpu: cached PASS 634753d90fa22773"; "119 test programs passed, 215 parallel controls (JOBS=2), every negative
  control failed as it must". The apptests job of run 44948262543 ran no unit ("tier fast, base HEAD~1 ... will run:
  nothing").
