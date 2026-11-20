---
name: fpga-gateware
description: Working method for FPGA gateware in TRX64U-MultiCore (C64U / U64 Elite II, XC7A50T, Vivado 2024.1, GitHub CI, the bitstreams on the private builder) - host (layer 1) and cores (layer 2, DFX partials), VHDL/Verilog changes, routing and timing failures, LUT and block-RAM budget, simulation benches, CI runs, releases and board tests over JTAG. Use before changing anything under host/ or cores/, before pushing a run-triggering commit, when a Vivado run or a bench fails, and before handing a build to Alex for the board.
---

# FPGA gateware in TRX64U-MultiCore: how we work

Started with the T79 DFX work (2026-10-11/12), carried on through T94-T121 (to 2026-10-23). The machine is the
evidence: the code, the Vivado reports, the bench output and the board. Comments and prose are third choice.

## 0. The tree and its rules (T94)

- `host/` = layer 1 (floorplan, boot ROM, `core.tcl`), **frozen**: no change without Alex's explicit go;
  `bash tools/host_pin.sh --check` before every commit. `contract/` = the partition's ports (247) and version.
- `cores/<name>/` = one core: `layer2_<name>.v`, `core_sources.tcl` (sets `l2_dcp`), `core.toml` (id, version, contract,
  app, `experimental`). `cores/common/` = IEC bus and 1541 drive shared by several cores.
- Vivado runs with `host/` as working directory: paths in Tcl are relative to it.
- The host is routed with the reference core `c64ref` (build only, never shipped); every other core against the cached host
  checkpoint.

## 1. Read, compare, plan. Runs only verify

A Vivado run costs 10-30 min on the builder; a wrong guess costs the next one too. Before a push:

1. List what the change touches: every module, every signal across the DFX edge, every RAM.
2. Read each consumer of a changed signal (who samples it, with which valid/ack/enable, in which clock). Write the
   comparison down (signal, consumer, needs, what the change gives).
3. Estimate the cost: LUT/FF per module, which pblock it lands in.
4. Plan the check: which report line, bench line or board observation proves it.

Never iterate by trying directives or flags at random ("lotto"), never "poke" on the board to find a cause. If a run
fails, read the report first, name the cause, then change one thing. Alex expects the cause from the code, not a
series of experiments.

## 2. Budget and density

- `pb_l2` (core): 18,000 LUT, 50 RAMB36 tiles, 80 DSP. Routing against the fixed host is reliable up to about 80 % LUT;
  above that a result depends on the build. `pb_l1` (host): 13,000 LUT; it routed at 75-78 %, not at 82 %.
- Routing failures were always density inside a pblock, never the number of partition pins.
- Measure before guessing: per-module utilisation from `utilization_hier_synth.rpt`, compared with the last green run.
- Gate control at the edge (request, enable, valid, ack, toggles, resets), not wide data buses whose consumers already
  qualify them.

## 3. Inference and language traps (all seen on this design)

- **A second write to a RAM array turns it into FFs or LUTRAM** (30k FFs once). One write port per RAM; a write address
  different from the read address gave LUTRAM even with `ram_style "block"`. Split a wide RAM into byte RAMs with one
  write each rather than one RAM with byte enables written from two places (A1200 floppy buffer: Vivado tied WEA to 0
  and the core read zeros - T97).
- **VHDL-2008 constructs fail in Vivado here**: `core_sources.tcl` reads VHDL without `-vhdl2008`. `process (all)`
  stopped the VC-20 core (CI 947); GHDL benches run in 2008 mode and do not see it. Check new VHDL with
  `ghdl -a --std=93c -fsynopsys -fexplicit -frelaxed-rules` in the container before pushing.
- Verilog: a signal used before its declaration is an implicit net. Lint with yosys (`-noautowire` / `check -assert`).
- Holding a register in reset blocks software that writes and polls it: reset once on an edge, do not hold.
- A partial bitstream reinitialises the core's block RAM: ROMs and RAM start empty; the application writes the ROMs.

## 4. Timing

- A miss of a few ps on a path you did not touch is placement noise: post-route `phys_opt_design` (the core stage does
  this; zxnext went from -0.107 to +0.015 ns that way).
- A real miss on a path you added gets a register stage (ZX Next: z80n `ir` -> cache RAM address, -0.401 ns, fixed by
  holding the request capture and reading the RAMs from it - T116 S12).
- Read the worst path (`Source`, `Destination`, `Logic Levels`) before deciding which case it is.
- "A few ps" is noise only on a path you did not touch AND with few levels. -0.019 ns through 14 levels (9 CARRY4) in logic
  you changed is a real path (SNES `snes_cubic`, run 1093); retries will not save it.
- A combinational iteration loop through a DSP into its own enable (multiply -> add -> compare -> next z, one iteration per clock) cannot meet 100 MHz: pipeline the loop and interleave
  independent contexts so the throughput stays one iteration per clock; take the decision (compare, done) off the loop and let a context run one junk iteration until it arrives (ddt_mandel,
  run 1101 WNS -2.304 ns, 13 levels with 6 CARRY4 and a DSP; T168 S5).
- Observation logic (peak meters, counters, debug registers) taps values that are already registered, never the end of an
  arithmetic chain: the SNES hand-over peak on the cubic's output (saturation + |x| + compare in one clock) cost -0.141 ns
  (run 1091); it now takes the registered pair in two stages (|x|, then the maximum).
- Before adding a pipeline clock, find the consumer's latency budget in the code (SNES: the hand-over waits 12 clocks after
  a new pair, the cubic went from 8 to 9). Cheap stages: register a module's inputs locally when one source register fans
  out to two instances (`r2` -> `cl`/`cr`); split a multi-term sum over two clocks when the result is only delayed anyway
  (`b1` -> `b2` was a pure delay).
- The large-core flows (`cores/*/mono/build_mono.tcl`) retry (phys_opt, second placement) only when WNS < 0, so a +0.009 ns
  result is kept as is (Neo Geo). Read the margin, not only the sign.

## 5. DFX specifics (7-series)

- Partition pins are routing nodes, not flip-flops. The edge is the contract (`contract/`); a core never adds a port.
- The host must not trust the core while a partial loads: the decoupler gates requests, enables, acks, toggles,
  resets and pins; state that locked onto the old core relocks after release.
- Every non-C64 core draws into a DDR2 frame buffer of the host's pixel layer through `e_*` (RGB565, `*_pixw`); the
  C64 cores (`c64ref`, `c64u`, `c128`, `c64scpu`) use the host's vic_scaler (T119). A core that leaves `pix_*` idle still feeds the scaler a free
  running dummy raster so it locks.

## 6. Simulation benches

- Benches run on the builder only (container `trx64-sim-env:4`, `nice -n 15`, every run in `timeout`, only when
  `docker ps` there shows no `GITEA-ACTIONS-*` container, at most 2). **Nothing heavy on the Mac.** Leave nothing running.
- Every bench has a control (mutation or `ctl_*`) that MUST fail; a bench that cannot fail proves nothing.
- `ci/sims.sh` reads the bench's verdict from its output: a control's own lines carry the prefix `ctl| ` or its
  expected FAIL reads as the bench's (CI 940). The CI log shows only the last 25 lines of a bench.
- `ci/sims.txt` budgets are for 16 benches at once on the builder: a bench that takes 3,100 s alone times out at 3,600 s
  under that load (atari_machine, CI 945). Split long benches into lines of 2-4 modes and declare each in `ci/sims.deps`.
- A bench that tests a helper module with synthetic input does not test the chip in front of it: say what it drives
  (C264 sound: the averaging was benched, the board was silent).
- Host-side app tests (`tests/coreapp/run.sh`) apply `sed` mutations as controls; a pattern that no longer matches
  makes the control **vacuous and stops CI** (CI 946, 948). Change the control in the same commit as the code it mutates.
- Benches that can run in parallel never share a build directory: the four `apple2_video*` parts used one GHDL work dir and
  overwrote each other's library (link errors `undefined reference to work__a2_ram__...` and timeouts, nightly 1086).
- A wave of bench TIMEOUTs while agents also use the builder is load: run two of them alone (`tools/benches.sh <bench> -f`)
  and compare with the limit before touching RTL (nightly 1086: alone 251-1053 s against limits of 900-2400 s).
- BSD and GNU `diff` place hunks differently: compare by applying a diff and comparing the result, not diff text
  (Amiga fork check, CI 939). The sim image has no `patch`.

## 7. CI operations

- Only the coordinator pushes, only when no run is in progress or waiting; check the previous run's result first.
- `.github/workflows/pipeline.yml` ignores `**.md`, `docs/**` and `.claude/**`: a push that changes only those starts no run;
  dispatch `pipeline.yml` by hand when one is needed.
- **An experimental core's failure makes the run red too** (T100 D9.6). After every run read the bitstream log of each changed
  core: `=== core <name>: WNS <x> ns` (and no `core <name> FAILED`). Then read the sims summary table, not only the job colour.
- Job logs: `gh run view <run> --log`, or `gh run view --job <id> --log`. The Vivado logs and reports come with the bitstream
  entries the builder publishes (`bit-<entry>` in the builds repository); the package artifact carries them too.

## 8. Releases and the board

- Every push's pipeline publishes a pre-release `<branch>-<sha8>` in the builds repository: the release bundle (T239.1 D42).
  A release is made by hand on GitHub with Alex's go; only Alex creates tags (T239.1 D28). Tell Alex which `.cor` changed
  against the previous release (SHA256SUMS diff) - "rebuilt" is not "new". A change in the generic core app changes every
  blueprint core's `.cor`.
- Never power-cycle the board; Alex does. Never touch pins. Flash writes are allowed.
- JTAG reads disturb nothing: `tools/trxdbg.sh console follow` shows the app's console (ROM search, menu timings,
  resets). "Return to host" reloads the host bitstream; a reader then sees zeros - reconnect in a retry loop.
- **A memory read is not a picture.** Claim "works" only after Alex's photo.
- The picture can be cut at all four edges by the monitor's overscan, at random per start and for every picture path
  (the HDMI timing is identical for all cores, T118); the AVI infoframe sends `SCAN_INFO` "no data".

## 9. Hygiene

- Every piece of work starts as a spec section; small commits; spec, TODO and history in the same commit;
  `python3 tools/check_todo.py`; stage by name, never `git commit -a`.
- English in code, commits, specs; German with Alex. No remarks about people in specs or commits.
- Clean room: never open Gideon's UltiSID, Copper, lp_filter or sampler VHDL. No ROM or disk image in the repository
  (the exceptions: `CLAUDE.md`, "Clean room and licences").
- QC every agent result against code and decisions before merging: agents claim tests they did not run, leave stale
  controls, and miss language-mode and tool differences between the Mac and CI.
- 2026-10-31 (T140 D24): a boundary scan names pin groups by behaviour, not by function; a board result (the luma pin shows the composite word) beats the scan - keep pin-to-word maps in one small module with a swap parameter and a bench that reads the balls from the XDC. A bench that only checks burst phase never sees a V sign error: decode (U, V) against the burst like a TV does. Moving the VIC's RAM to DDR2 fails on phi1 (16 clocks) against a 15-27 clock fetch; only the CPU may wait.
