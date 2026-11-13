# T203 — Every host start leaves a short record on the SD card, so a start that went wrong can be read afterwards

**Status:** IN PROGRESS (2026-11-11, Alex: "yes", translated, to the proposal of a start log file for issue #69).
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp/`: `hostui.cc` direct boot phase and countdown, `menukeys.cc`, a small new writer),
`tests/hostapp/`.
**Number:** T203 (registry: `TODO.md`).
**Origin:** GitHub issue #69 (fire held at power-on, the default core still starts). The tester uses a real Tac-2 in port 1; it worked at first and
fails later. The host's start lines (`direct boot: decision ...`, `hostui.cc:3340`) go only to the console and the bug report's log buffer, which
are gone once the default core starts, so the failing start cannot be reported. Alex, 2026-11-11: build the start log file.
**Depends on:** T146/#69 direct boot (`directboot.cc`, `hostui.cc` `direct_phase`).
**Milestone:** M2.

---

## §1 The problem

- **Who:** a tester whose board starts the default core although fire (port 1) or C= was held; Alex and Claude reading the report.
- **What they want:** after such a start, a file on the SD card that says what the host saw: when boot.cfg was read, how often the keys were
  sampled and when fire / C= was first seen, the decision, whether the countdown was stopped, and what started the core.
- **What happens today (code):** `STARTLOG` is `printf` (`app/view/startlog.h:13`); nothing is written to a medium during the start.
- **Why it matters:** the failure is intermittent and only seen on testers' boards; without a record each guess costs a release round.

Parts:
- **P1 Record:** per start, one block of lines: host app version, time since power-on of each step (boot.cfg read, key window start/end, decision),
  key samples (count, first sample with fire / C= low, raw port-1 byte of the first and last sample), DirectIn fields, the decision, whether the
  countdown was stopped or ran out, and what finally started a core (direct start, countdown, a key, the network) with its time.
- **P2 File:** `startlog.txt` on the SD card (folder next to the host's other files on the SD), the last 10 starts kept (oldest dropped),
  written after the core start is decided and before the core is loaded; never written to the flash; a missing or read-only SD skips it silently.
- **P3 Report:** the bug report (F8 Support) includes the file's content, so a tester can attach one file.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | a start with fire held in the key window: the block names fire seen at sample n, decision "Menu: fire on port 1 was held", countdown stopped | host app test over `direct_phase` with the key model | apptests |
| S2 | P1 | a start with no key and direct boot on: decision START, "started by: direct start" | host app test | apptests |
| S3 | P1 | boot.cfg read late (after the window) with fire held: the block shows the late read and the decision | host app test | apptests |
| S4 | P1 | menu shown, countdown runs out: "started by: countdown" with its time | host app test | apptests |
| S5 | P2 | 11 starts: the file holds the last 10; no SD / read-only SD: no error, start unchanged | host app test with the medium model | apptests |
| S6 | P3 | the bug report contains the start log | host app test | apptests |
| S7 | All | board: Alex holds fire at power-on 5 times, the file shows 5 blocks matching what happened | by hand | board (Alex) |

## §3 Decision

- **D1 (Alex, 2026-11-11):** a start log file on the SD in the normal host (no separate debug build), alongside reproducing #69 at home.

## §4 Out

- Logging to the flash.
- Any change of the start decision itself (that follows from what the file shows).

## §5 Build notes

**Built 2026-11-11** (host app only; the flash is never written).

- **Where:** `/SD/report/startlog.txt` (`STARTFILE_DIR`: the folder the bug reports already use on the SD card). A missing SD card (`make_dir` fails)
  or a read-only one skips the file silently; the block stays in RAM and the next write (a later event) tries again, as the same start's block.
- **Code:** `startfile.cc/.h` (pure: the record, the block text, the file with rotation), `menukeys.cc` ends each sample with `startrec_key(joy, row)`,
  `hostui.cc` `direct_phase` calls `startrec_init / window_open / window_close / decide` and writes once after the decision, before the core is
  loaded (`slog_flush` -> `hostreport_startlog_store` in `hostreport_live.cc`, which holds the SD access). Nothing is written during the key window.
- **Later "started by":** the block is written again (same number, replaced) with one more line `t=<ms> started by: countdown` or
  `... menu (key or network request)`; a network start and a key start are not told apart (both pass `start_any`). A direct start that a key cancels
  or that fails adds `direct start cancelled / failed: ...`. A boot.cfg read after the decision adds `boot.cfg read AFTER the decision`.
- **Keep:** the last 10 blocks, file at most 8192 bytes (oldest blocks go first), one block at most 768 bytes (a line that does not fit is dropped whole).
- **Report (P3):** F8 Support / REST report gets a section "Start log" with the file's content (`hostreport_make` reads it; `HOST_REPORT_MAX` 24576 -> 32768).
- **Format:** one block per start, `=== start #N | host app <hash> built <date> | clock <date or "clock not set"> ===`, then the lines of the decision
  (times are ms since the host began). Example (S1, as `test_startfile` prints it):
```
=== start #1 | host app abc1234 built 2026-11-11 | clock 2026-11-11 14:05:09 ===
t=     0 ms boot.cfg read: cores 3, default 1, direct 1, countdown 5
key window: 500 ms from t=0 ms, closed at t=240 ms
keys: 25 samples, raw port 1 first 0xFF last 0xEF
  fire: low in 1 samples, first at sample 25 (t=240 ms)
  C=: never seen
input: cfg 1, direct 1, cores 3, default 1, marker 0, C= 0, fire 1, default file 0
t=   240 ms decision: Menu: fire on port 1 was held (countdown stopped)
```
- **Proof (builder, `tools/dev.sh test`, hostapp tier fast):** `test_startfile: 133 checks, 0 failed` (S1-S5), `test_report` `123 checks, 0 failed`
  (S6, with its existing negative controls). `direct_phase` itself needs the board (hardware keys, FreeRTOS), so the tests run the same calls in its order
  against a fake clock, a fake key matrix and a model SD card; the wiring inside `direct_phase` is read, not run, until S7.
- **Size:** `hostapp.app` 1022620 -> 1026524 bytes (+3904, after the review fixes).
- **S7 (open, Alex):** hold fire at power-on 5 times with the Tac-2 in port 1; `/SD/report/startlog.txt` must hold 5 new blocks that match
  what happened (fire seen or not, decision, started by). Not run.
- **Review fixes:** `out_max` = `HOST_REPORT_OUT_MAX` (report max + 1 KB, was 28672 < 32768); a loaded file without a header at its start loses what
  precedes the first header (no more stuck store); a truncated block line is not stored; `startrec_cfg` returns whether it added a line (flush only then);
  body 1024 bytes with 160 reserved for "started by". Tests: `test_report` (max log + max start log fit with headers), `test_startfile` (headerless 8 KB, full body, cfg bool).

S7 board 2026-11-11 (Alex's power-on with fire held, host update 5cc93ede; the host app reports 029e5a9, the same content built first on the
t201-av-v2 branch): `/SD/report/startlog.txt` read over FTP, block `=== start #1 | host app 029e5a9 built 2026-11-11 16:45 | clock clock not set ===`,
`fire: low in 2 samples, first at sample 1 (t=380 ms)`, `decision: Menu: fire on port 1 was held (countdown stopped)`. PASS.
