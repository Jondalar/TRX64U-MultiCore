# T249 — c64scpu: no datasette

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: TAPE out of the c64scpu build, the Datasette port row out; branch t249-scpu-no-tape
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
- **D1 (P1):** `TRX_DEFS` in `mono_env` loses `TAPE`. `layer2_c64scpu.v` is not changed: its `else` branches tie the C2N pins
  and answer the two register pages of the block (data 0, ack 1).
- **D2 (P2):** the row "Datasette port", the op `c64scpu.set_dset` and the register name `SCPU_PIN_CTRL` go out of the core app.
- **D3:** branch `t249-scpu-no-tape` from `t248-scpu-depth`, so one pipeline run shows T248 and T249 together.
- T214 gave the c64scpu its datasette. From 2026-11-21 the c64scpu is not in the scope of T214 (recorded in T214 §5).

## §4 Out

- The `ifdef TAPE` code in `layer2_c64scpu.v` stays in the file; without the define it is not built.
- The other Commodore cores keep their datasette (C128, VC-20, C16/Plus4, PET).

## §5 Built

Nothing yet.
