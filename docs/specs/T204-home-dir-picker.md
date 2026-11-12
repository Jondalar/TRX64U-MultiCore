# T204 — The core app's "Home directory" setting is picked with a folder browser, not typed

**Status:** PROPOSED (2026-11-11).
**Repo:** TRX64U-MultiCore — the generic core app (`coreapp/src/home.cc`, the folder browser of T122/T128).
**Number:** T204.
**Origin:** Alex, 2026-11-11, at the board in the VC-20 core (Settings, Home directory, a text field showing `/SD/VIC20_`), translated:
"Minor enhancement: a small picker, i.e. an overlay here that starts from /. Or from the last saved choice in this core."
**Depends on:** nothing (the folder browser exists: `pick_dir`, first row "Use this folder", T122 §5 / T128).
**Milestone:** M2.

---

## §1 The problem

- P1: the Home directory of every core (`home.cc`, `HOME_DIR_KEY`, kind 1 = text, 60 chars) is typed by hand on the C64 keyboard; a typo
  gives a folder that does not exist.
- P2: where the picker starts: the saved Home directory of this core if it exists, else `/`.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | RETURN on Home directory opens the folder browser; "Use this folder" stores that folder as the setting (live, as today's text) | core app test | apptests |
| S2 | P2 | the browser opens at the saved Home directory; a missing or empty one opens at `/` | core app test | apptests |
| S3 | P1 | ESC / <- leaves the setting unchanged | core app test | apptests |
| S4 | P1, P2 | the picker as pictures | render bench + ux-review | apptests |

## §3 Decision

- **D1 (Alex, 2026-11-11):** a folder picker overlay instead of the text field, starting at the last saved choice of this core, else at `/`.
- **D2 (Alex, 2026-11-11, "after ... that will be polishing", translated):** built after the next test release.

## §4 Out

## §5 Build notes
