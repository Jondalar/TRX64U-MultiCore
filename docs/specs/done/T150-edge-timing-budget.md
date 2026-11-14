# T150 — A host change never costs a core its timing: the host/core edge is registered and budgeted

**Status:** WON'T DO (2026-11-14, Alex: "Won't do. So it is closed.", translated). Won't do. Earlier status: PROPOSED (2026-10-28), asked by Alex. Status check 2026-11-13: PROPOSED stays; D3 dropped (Alex 10-28), Q1 void; D1/D2/D4 not built (§5 empty, no bench, no contract entry).
**Repo:** TRX64U-MultiCore — the host (`host/`: the register stages at the partition pins; a host change, Alex approves the
build, `host_pin` in the same commit), the contract (`contract/`), the CI timing check (`ci/bitstream_layers.sh`,
`ci/bitstream_mono.sh`, `ci/lib.sh`), every DFX core (rebuilt once).
**Number:** T150.
**Origin:** Alex, 2026-10-28, after run 1032 (T149 in the host) broke the VC-20's timing (translated): "How can that happen?
That is what we do the reference and everything for!" To the proposal (register the edge, budget it, check the margin in CI,
as a spec before T149 is built again): "God, yes".
**Depends on:** T79.1 (the contract; its "register stages at the edge" rule says "may"), T133 (caches: the data-side address
that now crosses the edge), T115 (c64ref routes the host).
**Milestone:** M3.

---

## §1 The problem

Run 1032 (T149: a new SD block in the host) re-routed the host with c64ref. The host met timing (WNS +0.147 ns), then the DFX
cores were routed against that host and the VC-20 failed:

```
Slack (VIOLATED) : -0.981ns   cpu_mmcm, requirement 10.000ns
Source:      ult/i_cpu/i_dmem/addr_i_reg[9]/C       static, pb_l1/pb_cpu, SLICE_X49Y104
  net (fo=41) 6.318 ns  -> boundary ->              SLICE_X9Y75 (pb_l2)
  LUT1, CARRY4, CARRY4, LUT6, LUT4: 3.6 ns
Destination: l2/drv/i_drive/param_0_reg[10]/CE      reconfigurable, pb_l2
```

Run 1031 (same core, the host before T149): VC-20 WNS +0.033 ns; a path from the same address register into the overlay had
+0.144 ns. C128 large core: +0.044 (runs 992-1008), +0.004 (1017, 1030), then lost in run 1032 (its drive path, a separate
cause, T149 D5).

- **P1 No register at the edge.** The RISC-V data-side address leaves a flop in `pb_cpu` and reaches the core's decoders as one
  net of fan-out 41 across the partition; the core's address compare hangs on the same cycle. Where the partition pins land
  depends on the host's route, so any host change moves the wire.
- **P2 No budget.** Nothing says how much of the 10 ns the host side may use. c64ref proves only that c64ref's own logic fits
  behind the pins; a core with a deeper decode behind the same pin is not checked until it is built.
- **P3 No margin check** (not addressed: D3 dropped). CI turns a core red only at WNS < 0; +0.004 and +0.033 were reported green. The warning was in the
  reports and was not read.
- **P4 Which channels.** Every signal of the edge has to be classed: registered on the host side, on the core side, or
  exempt (T79.1: cartridge pins, IEC levels), with the reason.

## §2 Scenarios

```gherkin
Scenario: S1 the register bus crosses on a flop
  Given the host built with T150
  Then  every register-bus signal (address, write data, read/write strobes, read data, ack) leaves a flop placed next to its
        partition pin on the sending side (report: the driver of each partition pin is an FD*, listed per channel)
  And   control: one channel left unregistered -> the check names it and fails

Scenario: S2 a host change does not move a core's timing
  Given T149's SD block put back into the host (the run-1032 change) on top of T150
  When  every DFX core is routed against that host
  Then  every core meets timing (VC-20, the core that failed, named in the report)

Scenario: S4 the register bus still works
  Given one extra cycle on each side of the edge
  Then  the core application's register benches and the board (every core starts, its menu, a disk mounts) work as before
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1, P4 | Tcl check over the partition pins after route, control | CI host |
| S2 | P1, P2 | the run-1032 change rebuilt on T150 | CI |
| S4 | P1 | coreapp benches, ack/timeout bench of the register bus; board | CI, board |

## §3 Decision (proposed)

- **D1** Every channel of the edge is registered on the sending side, at the pin (T79.1's "may" becomes "must"); exempt only
  the cartridge pins and IEC levels, each listed in `contract/` with its reason.
- **D2** The register bus takes the cycle of D1 through its `ack` (no protocol change); the read path the same.
- **D3** ~~CI margin gate~~ **Dropped (Alex, 2026-10-28, translated): "Stop the thresholds. The stuff has to run somehow. If we
  put in walls/gates like that, we will sacrifice features to keep them."** WNS >= 0 stays the only gate.
- **D4** A contract version step: every core is rebuilt once against the new host; T149 into the host (if wanted) only after.

## §3a Open questions

- ~~**Q1** The margin values of D3.~~ Void with D3 (first answered "let us try it", then dropped the same day).

## §4 Out

- The C128 drive path (`l2/drva/i_drive/i_cpu` -> `i_cia1/data_out_reg`): inside a flat core, not the edge; its own fix.

## §5 Build notes

(filled in while building)
