# T109 — a core that makes sound is heard, and silence cannot come back unseen

**Status:** PROPOSED (2026-10-16). 2026-11-13 status check: IN PROGRESS, not PROPOSED: the Atari and A1200 audio benches run in CI (ci/sims.txt) and the lint keeps the registry (S1, S2, S3, S5 built); S4 (boundary bench) and S6 (board, Alex's ears) open; silence not found.
**Repo:** TRX64U-MultiCore — the Atari core (`cores/atari800/`), the Amiga 1200 configuration
(`cores/amiga1200/`), the benches under `cores/*/sim/` and the registry `ci/sims.txt`.
**Number:** T109 (registry: the table in `TODO.md`).
**Origin:** Alex at the board, 2026-10-16: Centipede runs on the Atari — "Spiel läuft ! aber kein Ton"
(translated: the game runs, but no sound) — and earlier the same session, "ich hab keinen Ton mit dem
Amiga 1200" while the A500 core sounds fine.
**Depends on:** [T88](T88-atari800-core.md), [T85](../todo/T85.md), [T102](T102-amiga1200-app.md),
[T98](../todo/T98.md) (the Amiga's FIFO counters, which this does not replace).
**Milestone:** M3.

---

## §1 The problem

Two cores are silent on the board, and in both cases everything else about them works.

- **Atari 800 XL.** Centipede runs from its cartridge: picture, CPU, DDR2 window, cartridge logic and
  the application's file action all hold. No sound.
- **Amiga 1200.** The A500 core has sound; the A1200 core has none. The two are built from **one source
  file** and differ in exactly three generics — `CHS_INIT=16` (AGA), `MEM_INIT=3` (2 MB chip RAM),
  `HDF_SECT=1` (`cores/amiga1200/core_sources.tcl:22`) — and the audio path is wired identically
  (`cores/amiga/layer2_amiga.v:263`). So the cause is one of those two machine settings, not the
  application.

What is already known about the Atari's path, read from the code, not assumed:

- The host takes a core's audio unconditionally: `snd_l <= pause_mclk[1] ? 0 : audio_l`
  (`host/top.v:935`), muted only by `c64_mute = c64_halted | app_s2[1]` (`:1209`) — the machine holding,
  or the application leaving `C64_STOP` set. Neither is true while a game runs, and `close_menu()` clears
  `C64_STOP` (`coreapp/src/run.cc:222`).
- The core's side is wired: POKEY → `a8_audio` on `mclk` → `audio_l/r` (`cores/atari800/trx/a8_core.vhd:410-413`),
  and `mclk` is connected at the partition (`layer2_atari800.v:249`).
- One asymmetry at the boundary: the Atari declares the contract's `audio_sample_en`
  (`contract/layer2_ports.txt:19`) and **never uses it** — one occurrence in the file, the port itself,
  against two in the Amiga. The Amiga resamples on it; the Atari averages on `mclk` instead. That may be
  legitimate, and it is the only difference there is.

**And the reason nobody noticed:** the benches that cover sound are **not in the pipeline**.
`cores/atari800/sim/machine/run.sh` measures the zero crossings of `audio_l` while a tone plays
(`tb_a8_machine.vhd:239-259`), and `cores/amiga/sim/run_audio.sh` measures underflow and pitch — and
`ci/sims.txt` lists neither. Only `atari_gate` runs for the Atari. So the one measurement that would have
said "this core went quiet" has never run automatically.

**Parts (MECE):**

- **P1 The core makes sound at its own boundary.** `audio_l/r` of the partition carry the machine's
  sound. For the Atari the bench exists and is not run; for the A1200 configuration no bench exists at all.
- **P2 The sound crosses the partition into the host.** Nothing tests the boundary itself for any core
  other than the C64: whether the host's register of `audio_l` on `mclk` sees what the core drives, and
  whether `audio_sample_en` matters.
- **P3 The sound reaches the speaker.** HDMI and line out on real hardware. Only a person can say this.
- **P4 Silence cannot come back unseen.** Every core that makes sound has a bench that measures it, and
  that bench runs in CI.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the Atari's POKEY reaches the partition
  Given the Atari core in the machine bench with a tone programmed into POKEY
  When  the machine runs for a known number of frames
  Then  audio_l crosses zero at the tone's frequency, within tolerance
  And   with the tone off there are no crossings at all
```

```gherkin
Scenario: S2 the A1200 configuration is as loud as the A500 one
  Given the Amiga core built twice - OCS with 512 KB, and AGA with 2 MB chip RAM
  When  the same Paula registers are programmed in both
  Then  both produce the same number of zero crossings within tolerance
  And   a difference says which of the two generics did it
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | the Atari's tone | `cores/atari800/sim/machine/run.sh boot`, the existing `audio_mon` | **added to** `ci/sims.txt` |
| S2 | P1 | A500 against A1200 | **a new bench.** `run_audio.sh` drives `amiga_audio` alone - the FIFO - and knows nothing of Paula or the chipset generics, so it cannot bisect this. The comparison belongs at the machine level, beside `run_l2check.sh`, with `CHS_INIT`/`MEM_INIT` as its parameters | `ci/sims.txt`, once written |
| S3 | P1 | the control: a core that drives silence | a build with the audio output forced to zero must fail S1 and S2 | both benches |
| S4 | P2 | the boundary carries it | a bench that drives a known waveform on the core's `audio_l` and checks the host's `snd_l` one `mclk` later, with and without `audio_sample_en` moving | a new host bench |
| S5 | P4 | the registry is complete | a check that every core with an audio bench has it listed in `ci/sims.txt`; the control is a core whose entry is removed | `ci/lint.sh` |
| S6 | P3 | the board hears it | Centipede on the Atari, a module or a game on the A1200 — Alex, with his ears | board session |

Every part has a scenario; S3 and S5 carry a control that has to be able to fail.

## §3 Decision

- **D1 The two benches go into `ci/sims.txt` first, before anything is changed.** A measurement that
  nobody runs is not a measurement. This also answers "when did it break": the first run tells us whether
  the Atari is silent in simulation too, which splits P1 from P2 in one step and needs no board.
- **D2 The A1200 is bisected by its generics, not by guesswork.** Measured while writing this:
  `run_audio.sh` builds `amiga_audio` on its own (`verilator --top-module amiga_audio`), so it tests the
  FIFO and nothing upstream of it - it cannot see Paula at all. The bisect needs a machine-level bench
  that takes `CHS_INIT` and `MEM_INIT`, which is new work, not a flag on an existing script.
- **D3 The boundary gets a bench of its own (S4).** It is the one place in the chain that has never been
  tested for any core but the C64, and it is where `audio_sample_en` either matters or does not.
  **Not a host question, though - corrected 2026-10-16.** It was first written down as "the host may
  have to be touched, which is pinned", and that was an overreach: the **Amiga core is audible on the
  board through the same host, the same partition and the same HDMI encoder**, so the host's side is
  proved by use. What is left is specific to the built Atari core. Read and cleared since: the Verilog
  wrapper wires `audio_l/r` exactly as the Amiga's does (`layer2_atari800.v:45,258` against
  `layer2_amiga.v:49,263`), and the synthesis globs `trx/*.vhd`, so `a8_audio.vhd` is in the built core.
  That leaves what a simulation cannot see because it drives the signal itself: `mclk` reaching the core
  over a partition pin on the real board.
- **D4 `ci/lint.sh` keeps the registry honest (S5).** The reason this was missed is structural, so the
  fix is structural. **Built 2026-10-16:** every `cores/*/sim/**/run*.sh` must be in `ci/sims.txt` or in
  the new `ci/sims_excluded.txt` with a reason, and the check fails on a bench that is in neither. It
  found nine more scripts nobody runs; each now carries the reason it is out, and two of them -
  `vc20/sim/run_boot.sh` and `c264/sim/run_boot.sh` - say out loud that they need ROM sets the
  repository does not have.

## §4 Out

- **The Amiga's crackle.** That is [T98](../todo/T98.md) — the FIFO counters are unconnected
  (`cores/amiga/rtl/trx/amiga_core.v:259` leaves `.st_under() .st_over() .st_fill()` open), so the board
  cannot say why it scratches. A different problem on a core that is not silent.
- **Sound quality, filters, mixing levels.** This spec is about audible or not audible.
- **The C64 core.** It has sound, it has benches, nothing here changes it.
- **The NES.** Not yet reported either way; it joins when someone listens.

## §5 Decisions taken while building

### 5.2 The Atari's POKEY is not where the silence is either (2026-10-16)

`cores/atari800/sim/machine/run.sh boot` on the builder, the whole core under GHDL:

```
tone: 60 periods, 989.73 Hz (programmed 989.6 Hz)
ok   the POKEY tone is the programmed frequency within 1 %
...
PASS tb_a8_machine
```

So P1 is answered for the Atari as it was for the A1200: the sound is there at the core's own boundary,
at the right frequency. Centipede runs on the board with no sound while the Amiga core is audible, and
the host registers any core's `audio_l` unconditionally (`host/top.v:935`, muted only by `c64_halted` or
the application's `C64_STOP`, neither of which is set while a game runs). That leaves **P2** - the
boundary itself - as the only place left, and it is the one place no bench has ever covered. S4 is now
the next piece of work, not a precaution.

Two things measured on the way that belong elsewhere:

- `ok   F5 pulls CONSOL OPTION low` - the console keys are already reachable from the keyboard the
  application drives, so switching BASIC off ([T108](T108-atari800-media.md) P4) needs no gateware
  change: the application holds the key across the cold start.
- The bench costs 21 min 41 s of CPU and more than 1,800 s of wall clock, so it is a tier 2 entry with
  an hour, not a tier 1 one with half of it. Run 63's sims stage went red on exactly that.

### 5.1 The A1200's gateware is not where the silence is (2026-10-16)

`cores/amiga/sim/run_l2.sh` already takes the three generics of the A1200 core as environment variables
and already has an audio case: the test ROM points AUD0LC at a constant sample, sets AUD0LEN, AUD0PER
and AUD0VOL and turns on AUD0EN, and the bench reads the partition's `audio_l` / `audio_r`
(`tb_l2.sv:365-368`, `gen_testrom.py:148-155`). So the bisect needed no new bench after all, only the
right parameters.

| run | `audio_l` |
|---|---|
| OCS, 512 KB chip (the A500 core's power-up) | 2,048,256 |
| `CHS_INIT=16 MEM_INIT=3 HDF_SECT=1` (the A1200 core's) | **2,048,256** |
| `CHS_INIT=16` alone | 2,048,256 |
| `MEM_INIT=3` alone | 2,048,256 |

**The A1200 configuration produces exactly the same tone as the A500 one.** Neither AGA nor 2 MB of chip
RAM silences Paula in this gateware, so P1 is answered for the A1200 and the search moves on: the
application's register bytes are the same ones this bench proves working (`cores/amiga1200/app_regs.txt`,
`MEM=03 FAST=00 CPU=00 CHIPSET=10 FLOPPY=04 IDE=21`, re-checked the same day), which leaves what was
supposed to be making a sound at all. Alex's A1200 had booted Kickstart 1.3 **with an empty drive**,
because ADR-0056 had just taken the automatic disk away - an idle Workbench-less 1.3 plays nothing.

The run with the A1200 generics is now part of `run_l2check.sh`, so this cannot quietly change.

**A correction worth keeping**, because it nearly became a finding: the first bisect used `MODE=audio_aga`
and friends to keep the log files apart. `MODE` is not a label - `run_l2.sh:19` hands it to
`gen_testrom.py`, which only emits the audio code for `MODE == 'audio'`. Those runs therefore measured a
machine that was never asked to play anything and reported silence for all three configurations. The
numbers above are from runs with `MODE=audio` and the generics beside it.

<!-- filled in while building: measurements with the report line they came from -->
