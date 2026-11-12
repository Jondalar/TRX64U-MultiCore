# T<NN> — <what is true when this is done, as one sentence>

**Status:** PROPOSED (YYYY-MM-DD).
**Repo:** TRX64U-MultiCore — and which part lives where (host, a core, the app, tools, CI).
**Number:** T<NN> (registry: the table in `TODO.md`, next free number from there — never from memory).
**Origin:** who asked, when, in their words if there are any (Alex's quotes translated).
**Depends on:** specs, decisions or releases this needs, or "nothing".
**Milestone:** M1 | M2 | M3.

---

## §1 The problem

Written for the reader who opens this in three months and remembers nothing.

- **Who** has the problem: Alex at the board, a core, the host, the application, a CI stage.
- **What they are trying to do** — the use case, in their terms, not the system's.
- **What happens today** instead — concretely: the register, the report line, the WNS, the
  picture, the number. Read from the code and measured, not from a comment or an older spec.
- **Why it matters** — what it costs, what it blocks, what goes wrong unnoticed.

One worked example with real addresses, commands or report lines beats three paragraphs.

**Break the problem into parts, MECE:** the parts do not overlap, and together they cover the
whole use case. Number them (P1, P2, …) — usually the steps of the use case, plus anything that
cuts across them (area, timing, the partition contract, an old defect). §2 is checked against
this list.

## §2 Scenarios — what proves it is solved

The acceptance, as behaviour. Each scenario names the bench or the board step that runs it; a
scenario without one is a wish. Write these BEFORE the design: they are the contract the design
has to meet, and the first place a reader looks.

```gherkin
Scenario: <one behaviour, from the outside>
  Given <the state it starts from>
  When  <what happens: the write, the key, the swap, the reset>
  Then  <what is observed — exact and checkable: a number, a line, a picture>
  And   <what must NOT happen: nothing moved while halted, no DDR2 access outside its windows, …>
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | … | `cores/<core>/sim/<name>/run.sh` | `ci/sims.txt` (tier 1/2) |
| S2 | P2 | … | Vivado report line of the core stage | CI `bitstream` |
| S3 | P3 | … | by hand on the board, with a photo — why it cannot be a bench | board session |

Rules:
- Every part of §1 has at least one scenario, and every scenario solves exactly one part. A part
  without a scenario is unproved; a scenario without a part solves a problem nobody had.
- **Negative controls only for flash and persistence operations** (T174 D5.4, Alex 2026-11-08): there a control
  has to bite (T88 §5.10 - four runs compared a dead hash for weeks). Everywhere else: no controls.
- A scenario that needs the board, real ROMs or a real disk image says so and is run by hand.
  Nothing third-party is checked in except where Alex has allowed it in writing.
- Area and timing are scenarios like any other: name the budget (`pb_l1` 13,000 LUT, `pb_l2`
  18,000 LUT, 50 BRAM) and the report the number comes from.

## §3 Decision

D1, D2, … — what is built, each with the reason. Name files and modules. Where a choice was
weighed, say what lost and why, in one line. A decision of Alex's is quoted.

## §4 Out

What this spec deliberately does not do, and why. Anything not built is listed here — by decision
with Alex, before the work, not by omission afterwards.

## §5 Decisions taken while building

Filled in while building: every choice the spec left open, what was chosen, and the measurement
that decided it.

---
<!--
Closing: Status → DONE (date) + which scenarios are green where; the TODO row gets ✅ and a
history entry, in the same commit as the code.
-->
