# T235 — The Picture list always offers every resolution at 50 Hz and at 60 Hz; the modes the screen names are marked, not the only ones

**Status:** PROPOSED (2026-11-17).
**Repo:** TRX64U-MultiCore — the generic core application only (`coreapp/src/picture.cc` `picture_modes`, the Picture page in
`menuctl.cc`/`run.cc`, its unit `test_picture`), the manual's Picture paragraph. No gateware, no per-core C.
**Number:** T235 (TODO.md table).
**Origin:** Alex 2026-11-17, looking at the A1200's Picture list on a monitor whose EDID names no 50 Hz mode ("59.8 Hz" on every row,
foot "This screen names no 50 Hz modes; 60 Hz modes are listed."): "wouldn't it make sense to always offer PAL (50) / NTSC (60) with
the resolution? Because the monitor here at home can do 1080p50" (translated); order: "before 234".
**Depends on:** T187 D5 (the Picture list, the 8-second confirm), T107 (the mode table: every row has a 50 Hz and a 60 Hz clock).
**Milestone:** M2.

---

## §1 The problem

- **P1** The list shows only the modes the screen's EDID names (T187 D5). Many screens take 50 Hz without naming it; on them a 50 Hz
  machine (Amiga, C64 PAL, ...) can only run at 60 Hz and judders (one frame in six shown twice, T188 table).
- **P2** The person cannot tell from the list which modes the screen says it takes and which it may still take.

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | With an EDID that names only 60 Hz modes (the photo's screen), the list holds every resolution of the core's table at 50 Hz and at 60 Hz | `test_picture` (happy path, the function the board uses) | NAS/builder units |
| S2 | P2 | Rows the EDID names carry a mark; the others carry none and are selectable; the foot line says what the mark means | `test_picture` + ue2emu picture | units, ue2emu |
| S3 | P1 | Picking an unnamed mode applies it with the existing 8-second confirm; no confirm (black screen) reverts it, LEFT/ESC reverts it at once | `test_picture` `through_the_menu` (existing path) | units |
| S4 | P1 | With no EDID at all the list is the same full list, no row marked | `test_picture` | units |
| S5 | P1 | Board: the A1200 at 1080p50 on Alex's home monitor, picked from the list, confirmed, picture and sound | board, Alex | board |

No mutation tests (T174 D5.4).

## §3 Decision

- **D1 (Alex 2026-11-17):** the list always offers every resolution at 50 Hz and at 60 Hz; T187 D5's "only the modes the screen names" is
  replaced. The safety is T187 D5's 8-second confirm, unchanged.
- **D2 (Claude as advisor, Alex may overturn; ux-review checks it in ue2emu, Alex sees the pictures before the merge):** one row per
  resolution and rate (576p 50, 480p 60, 720p 50, 720p 60, 1080p 50, 1080p 60, the VESA rows at 50 and 60), the rate in the right
  column as today; rows the screen names carry a short mark beside the rate; the foot line explains the mark instead of "This screen
  names no 50 Hz modes"; `Auto` keeps its rule (from the screen). Only modes of the core's own table are listed.
- **D3:** generic code only (T192 D24, T213 D4: `tools/check_d24.py` rc 0); every core gets it.

## §4 Out

- The host's F8 Picture row (T187 D5's part 2, not built there either).
- New modes or clocks; `Auto`'s choice.

## §5 Build log

(empty)
