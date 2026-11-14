# T98 — The Amiga core can say why its sound crackles, instead of being guessed at

**Status:** PROPOSED (2026-10-14).
**Status check 2026-11-13:** MERGED into T85 (2026-10-18, 871766a26; TODO row T98 is merged): nothing of it is built; the spec stays as the design for the crackle measurement.
**Repo:** TRX64U-MultiCore — `cores/amiga/rtl/trx/amiga_audio.v` and `amiga_core.v` (the status
outputs), `cores/amiga/sim/run_audio.sh` (the bench), the core application (T93, outside).
**Number:** T98 (registry: the table in `TODO.md`).
**Origin:** Alex on the board, 2026-10-14: "a slight scratching is still audible … so a bigger FIFO?"
(translated), after confirming the C64 core's sound is fine on the same output.
**Depends on:** T85 (the audio FIFO with rate adaption).
**Milestone:** M3.

---

## §1 The problem

Alex hears a faint crackle from the Amiga core on the board. The obvious move — make the FIFO
bigger — is a guess, and an expensive one: the buffer is the latency, 5.3 ms today, and doubling it
is audible in a game.

What exists today, read from the code and measured on 2026-10-14:

- The FIFO does what it was built for. `run_audio.sh` runs eight stall patterns through it under
  Verilator; all eight pass with 0 errors, with the machine running between 100.000 % and 97.500 %
  of nominal. Its checks include the one that would catch a click: no output step larger than the
  test sine's own slope.
- The core counts underflows and overflows itself (`st_under`, `st_over`) and knows its fill level
  (`st_fill`) — and **all three are left unconnected** where the audio block is instantiated
  (`amiga_core.v`). Nothing reaches the application, so the board cannot say whether the buffer ever
  runs dry.
- Therefore nobody knows which of three different faults is being heard: the buffer running dry, the
  mixing of Paula's four channels clipping, or something in the host's own 48 kHz path.
- The host path was ruled out by hand: the C64 core's sound on the same output is clean (Alex,
  2026-10-14). That is one of the three gone, by measurement, not by argument.

Why it matters: the crackle is the one thing between this core and "it sounds right", and every fix
anyone proposes costs something real — latency, area, or a rebuild of the application.

**Parts (MECE):**

- **P1 Visibility.** The board can read how often the buffer ran dry or overflowed, and how full it
  is.
- **P2 The mix.** Whether the sum of Paula's channels saturates or wraps at full volume.
- **P3 The verdict.** With P1 and P2 measured on the board, name the cause and fix that one thing.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: the counters reach the application (P1)
  Given the Amiga core running on the board
  When  the application reads the audio status
  Then  it reports the number of underflows, of overflows and the current fill level
  And   the numbers are zero in a run in which nothing stalls

Scenario: the counters count what they claim (P1)
  Given the audio bench with a stall pattern that is known to empty the buffer
  When  the run ends
  Then  the underflow counter equals the number of underflows the bench itself observed
  And   a run without stalls leaves both counters at zero

Scenario: the mix does not wrap (P2)
  Given four Paula channels at full volume with the loudest waveform
  When  the bench sums them
  Then  the output saturates at the end stops and never wraps around
  And   the sample stream has no step larger than the signal's own slope

Scenario: the cause is named (P3)
  Given the counters on the board during the music Alex used to hear the crackle
  When  he plays the same piece
  Then  the counters say whether the buffer ran dry, and the verdict follows from the numbers
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | counters reach the application | board session: read the status while music plays | by hand |
| S2 | P1 | counters count what they claim | `cores/amiga/sim/run_audio.sh`, extended | `ci/sims.txt` |
| S3 | P2 | the mix does not wrap | a full-scale case in the same bench | `ci/sims.txt` |
| S4 | P3 | the cause is named | board session with the counters visible | by hand |

The negative that has to bite: S2's "a run without stalls leaves both counters at zero" — a counter
wired to a constant would pass every other check in this list.

## §3 Decision

- **D1 The three status values leave the audio block** and reach the application through the Amiga
  core's existing status register path, next to the memory counters that are already there.
- **D2 Nothing about the FIFO size is decided here.** Enlarging it is a fix for one of the three
  causes and costs latency; it is decided in §5 once P3 has an answer.
- **D3 The bench grows the two checks of S2 and S3**, because the bench is where a counter is proved
  to count and the mix is proved not to wrap.

## §4 Out

- No resampler change. The rate adaption is measured and passing; it is not the suspect.
- No change to the host's audio path. It was ruled out on the board with the C64 core.
- No new audio features (no mixer menu, no per-channel volume).

## §5 Decisions taken while building

Empty — nothing built yet.
