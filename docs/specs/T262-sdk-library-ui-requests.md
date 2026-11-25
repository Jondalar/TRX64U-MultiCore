# T262 — A library builds lists, choices and tabs at run time with the SDK's own menu tools, and three base defects of the menu are gone

**Status:** PROPOSED (2026-11-25), asked by TRX64-Ultimate (FW session) with Alex's go to them (translated): "ask for it, but so that
it moves the SDK forward generically".
**Repo:** TRX64U-MultiCore — the SDK base's menu engine (`coreapp/src/menuctl.cc`, `picklist.*`, `ctx.h`), the library API
(`app/sdkpkg/api.md`, `libload`), the record format (`tools/descrec.py`), the SDK release.
**Number:** T262.
**Origin:** the FW session's message of 2026-11-25: a UX review in ue2emu of trx64u with SDK 0.4.6, while reworking the online
sub-app's UI (source and fields per source, results, Beyond64 as a third source). File:line refs are to the SDK 0.4.6 base sources
(`base/src/coreapp/src/menuctl.cc` = MC). Not yet read here: every claim below is theirs until §5 confirms it in the code.
**Depends on:** T239.3 (the SDK, the library API, record 6), T256 (overlay service: same release train).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a library or sub-app author (first TRX64-Ultimate's online sub-app; then every core with a list of results, files or
  history), and the person at the menu.
- **What happens today** (reported, 0.4.6):
  - A variable list needs N predeclared nodes ("Result 1..50") bound to per-slot states, the empty ones hidden.
  - The base's choice list (`ops_ask_list`, `picklist`, `ctx.h:62`) is not in `api.md` for libraries and caps at 40 rows; a
    library steps through values with RETURN instead.
  - Tabs are fixed at the menu merge; a tab whose rows are all hidden stays on the strip.
  - A `job` node is captioned like a file write ("Building the image 0%", MC:2117, MC:3284).
  - The F3 search tab (id "search", MC:4760) takes no nodes: a library cannot offer itself as a search source.
  - A `[[key]]` row cannot add itself to the foot legend.
  - Defects: a Run after a fetch keeps the menu open (`pend_tick` drops `run_action`'s result, no R_CLOSE, MC:2559); focus is
    lost when the focused row hides; every RETURN on an op row blanks the content about 250 ms and 2 of about 150 keys were lost
    during redraws (ue2emu at real time, board unconfirmed).

Parts:
- **P1 Runtime list rows:** a node kind whose rows come from the library at run time (count; per row title, value, tone,
  side-panel lines; RETURN calls an op with the row index; paging or "more" by a callback).
- **P2 Choice list for libraries:** the pick list in the library API (or a node key `pick = "@state"`), scrollable, no 40 cap.
- **P3 Tab visibility at run time:** a `[[tab]]` key `show = "@state"`.
- **P4 Busy caption for job nodes:** the node's label or a busy text; a percentage only when the op reports progress.
- **P5 Search providers:** libraries register as sources of the F3 search, with their fields; one form, a Source row.
- **P6 Key rows in the foot legend.**
- **P7 Defect: Run after a fetch closes the menu.**
- **P8 Defect: focus moves to the next visible row when the focused row hides.**
- **P9 Defect: no blank content and no lost key on RETURN of an op row.**

## §2 Scenarios

```gherkin
Scenario: S1 a list of 200 results without predeclared nodes          # P1: unit of the menu engine + ue2emu picture
Scenario: S2 a choice of 47 values, scrollable                       # P2: unit + ue2emu
Scenario: S3 a tab appears after a search and goes when cleared        # P3: unit
Scenario: S4 a job without progress shows its label, no percentage     # P4: unit
Scenario: S5 one F3 search form with This device and a library source  # P5: unit + ue2emu
Scenario: S6 a key row shows in the foot legend                        # P6: unit
Scenario: S7 Run after a fetch: the menu closes, the machine runs      # P7: unit (pend_tick) + ue2emu
Scenario: S8 the focused row hides: focus on the next visible row      # P8: unit
Scenario: S9 150 RETURNs on op rows: no blank frame, every key taken   # P9: ue2emu at real time; board by Alex
```

## §3 Decisions

Open, one at a time with Alex:
- **Q1** Which parts, in which SDK version? Proposal: the defects P7-P9 first, as 0.4.8; P1-P4 and P6 as 0.5.0 (new API: minor
  version); P5 after them.
- **Q2** Names for P1-P3 (proposal: node kind `list`, node key `pick`, tab key `show`).

## §4 Out

(to be decided with Alex)

## §5 Build notes

(filled while building)
