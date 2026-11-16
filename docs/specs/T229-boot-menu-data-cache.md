# T229 — The boot menu's table is cached like its picture: complete at the first frame, checked against the files in the background

**Status:** DECIDED (2026-11-16): Q1-Q6 answered by Alex, in the coming release. Build order: S1 measurement (startlog) and the row cache (D1-D10), the build phase after S1's board numbers.
**Repo:** TRX64U-MultiCore — host application (`host-app/software/hostapp/`, `tests/hostapp/`). No gateware.
**Number:** T229.
**Origin:** Alex, 2026-11-16 (translated): "About the boot menu ... can it not be faster still? If we put the table (currently 16 lines)
into a cache file after the first time? And then compare the hashes of the .cor files with the cache asynchronously? Today the user is
still 'blocked' for 2-3 seconds and can do nothing." On the coordinator's proposal of size+mtime instead of hashes and a cache file:
"So like the picture, only cache the data too." (D1: the row data is cached the way T146 caches the menu picture.)
**Depends on:** T146 (menu picture, `menusnap.cc`), T101 S10 (lazy reader, start check), T223 D2 (only visible rows are read), T198
(configs), T221 D13 (version, L/P badge), T135 (direct boot: no menu, not touched).
**Milestone:** M3.

---

## §1 The problem

Read from the code (not from comments), `host-app/software/hostapp/` at `dce0b67f2`.

**What the start does today.** `startup()` (hostui.cc ~4000-4050): picture (T146) or splash -> `build_ui()` -> `lazy_reset()`
(every entry `ES_UNKNOWN`, `g_pend=1`) -> `draw()` = first LVGL frame. So the first frame is built from boot.cfg alone: name only.
Rows show the text tile instead of the icon, an empty version (`hc_entry_ver`), the "check" pictogram (`hc_row_entry`, `pic =
HCP_CHECK` for `ES_UNKNOWN`) and no config mark (`configs = 0` while `ES_UNKNOWN`, host_core_rows.cc:182).

**What one row costs (code, no board number yet; the console prints `[host] entry N <path>: state|icon read X ms`, hostui.cc:584,
console only).** The reader (`lazy_pass` -> `lazy_step` -> `hc_lazy_step`, host_core.cc:848) does one unit per main-loop pass, default
row first, then the selection, then the rows of the window (`hc_lazy_next`):

| Unit | File operations (code) |
|---|---|
| state, partial `.cor` (`hc_probe_state`, host_core.cc:747) | `hc_is_full`: open + `cor_probe` (header, manifest); then `startcheck_entry`: `stat` + open + `cor_probe` again (startcheck.cc:16). Two probes per partial entry. |
| state, large `.cor` | `hc_is_full` probe; `startcheck_firmware` stat; `large_version`: a third open + probe (host_core.cc:734); on "changed" `probe_full` as well |
| state, `.cfw` | `stat` only |
| icon, partial (`probe_icon`, hostui.cc:500) | open + read of 2048 icon bytes + SHA-256 of them (`load_icon` -> `cor_read_part`, verify on) |
| icon, large | `load_full_icon`: probe again (a further open) + icon read |
| per redraw | `need_full` after every unit that touches a visible row or the default (hostui.cc:589): `update_menu()` refills ALL visible rows, `hc_row_entry` per row; the first time a row's state is known, `hc_cfgov_count` reads the core's `configs/` directory (host_core_cfgov.cc:102, cached per generation) inside the draw; then LVGL renders |
| migration (`migrate_one`, host_core.cc:819) | only an entry whose boot.cfg line has no mtime (an older boot.cfg): one full hash of the whole file, atomic, after 1500 ms without a key (`LAZY_MIG_QUIET_MS`). A `.cor` that CHANGED after adding is NOT hashed at menu time: it shows `ES_CHANGED` (`startcheck_trusts` compares size, mtime, header digest, stored hash). Once stored, migration never runs again for that entry. |

So a visible row is at least two units (state, icon) = 2 loop passes, 3-5 file opens, 2 full-row-set redraws. For 9 visible rows (T209)
that is about 18 passes, each followed by a redraw of the table. A key in the queue waits for the unit in progress plus the redraw
after it (`lazy_pass` runs after the keys of the pass, hostui.cc:4257, and `apply_updates` + `trx_lv_handler` follow in the same pass).

**What the picture does and does not do.** T146 saves the last settled frame (`Flash:/menu.snap`, 92 KB on the board of 2026-10-29)
and shows it before LVGL builds (`snap_try_show`). Two facts of the code that matter here:
1. The picture shows settled rows (icons, versions, pictograms). LVGL's first frame is built from `ES_UNKNOWN` rows (above), so when
   it replaces the picture the table steps back to tiles/blank versions/"check" pictograms and then fills in row by row. The picture
   is a promise the first real frame does not keep.
2. While the picture is up, keys are only collected (`snap_poll_keys` between the start phases, `menu_getch`, T146 D4); nothing acts
   on them until `build_ui()` and the first `draw()` are done. Board log of 2026-10-29 (T146 §5): picture at t+3075 ms, "menu built"
   t+5565, first frame t+6870. **A data cache does not shorten this build; if the 2-3 s Alex feels are this phase, T229 as proposed
   does not remove them** (Q1).

**Parts (MECE):**
- **P1 Complete at the first frame.** The first LVGL frame already shows what the picture showed: icons, versions, pictograms, config
  marks, L/P badges (T223 D7); no step back, no fill-in row by row.
- **P2 Truth.** A row never stays wrong: a file that changed, was replaced or is gone is noticed by a check against the files in the
  background, and only that row is re-read.
- **P3 Keys win.** The background check never delays a key by more than a stated bound (no unit, no redraw longer than X ms between
  key checks; the 1500 ms-quiet migration included).
- **P4 Cost.** An unchanged entry costs one `stat` in the background (no open, no probe, no icon read, no redraw); a changed entry
  costs today's two units.
- **P5 The cache file.** Where it lives, when it is written (not per unit: wear), what invalidates it, what a missing or broken one
  does (today's path), and its size.
- **P6 Measured baseline.** The time the user is blocked is measured on the board, per phase, before anything is built on a guess
  (decides Q1).

## §2 Scenarios

```gherkin
Scenario: S1 the baseline is measured on the board
  Given a board with a partial .cor, a large .cor and a .cfw in boot.cfg, menu start (C= held), no cache yet
  When  the host app starts and the lazy reader has gone idle once
  Then  /SD/report/startlog.txt carries: the t+ times of picture shown / menu built / first frame / reader idle, one line per
        read unit "row N state X ms icon Y ms redraw Z ms", and the longest main-loop pass in ms (= the longest a key waited)
  And   the numbers say which of the three phases the 2-3 s are (build, reader+redraw, migration)

Scenario: S2 the table is complete at the first frame
  Given a valid cache of 9 visible rows (partial, large, .cfw; one with configs) and the files unchanged
  When  the menu starts
  Then  the first LVGL frame shows icon, version, L/P badge, config mark and pictogram of every row exactly as the cache holds them
  And   the bench counts 0 calls of hc_probe_state, 0 opens, 0 cor_probe for them; the background only stats

Scenario: S3 a changed file is found and only its row is re-read
  Given a valid cache and one .cor replaced (size or mtime differs)
  When  the background check reaches it
  Then  that row goes through today's state + icon units and shows the new state (ES_CHANGED against boot.cfg, new version/icon)
  And   the other rows are not opened and not redrawn

Scenario: S4 a missing file
  Given a valid cache and one entry's file gone
  Then  its row shows the missing state after the background check; the others are untouched

Scenario: S5 a missing or broken cache is today's path
  Given no cache file / a wrong magic / another format version / another host app version / a bad CRC / a truncated file
  When  the menu starts
  Then  today's lazy path runs; no crash, no wrong row; the cache is written new after the reader has gone idle
  And   control (persistence): a loader that ignores the format version or the CRC makes the bench fail

Scenario: S6 keys win during the check
  Given 32 entries cached (T223 D1) and the background check running
  When  a key is queued at any point
  Then  it is handled before the next unit starts; no verify unit opens a file (one stat) and no unit takes longer than X ms
        (Q4); the migration of an entry without mtime is cut into pieces of at most X ms or does not run while a key is near

Scenario: S7 write only on change, only when settled
  Given the reader idle and the rows unchanged for 2 s
  Then  the cache file is written once when its content differs from the file, never per unit, never while a unit is pending
  And   control (flash/persistence): a writer that rewrites an identical file, or writes mid-read, makes the bench fail

Scenario: S8 boot.cfg changes (move, rename, remove, add, default, T198 config)
  Given a valid cache
  When  entries are moved, renamed, removed or added
  Then  every entry whose path and boot.cfg line (hash, size, mtime, header digest) are unchanged keeps its cached row (found by
        path, so a move re-reads nothing); a new or changed entry is read; the file is written new after the settle

Scenario: S9 on the board, end to end
  Given the S1 board and the cache written by the previous start
  When  the menu is started again (C= held) and after a return from a core
  Then  startlog.txt shows reader idle at least as early as in S1 and the table at first frame equals the picture (photo);
        the longest main-loop pass is within X ms; replacing a .cor on the SD card shows its row new on the next menu start
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P6 (P3) | board; startlog lines of D9 | board (hand, Alex starts; I read the file over FTP) |
| S2 | P1, P4 | hostui render bench + `tests/hostapp` cache test (counts of probe/open) | CI app stage |
| S3 | P2, P4 | `tests/hostapp` cache + lazy test | CI app stage |
| S4 | P2 | `tests/hostapp` | CI app stage |
| S5 | P5 | `tests/hostapp` cache round trip with mutated headers, with control | CI app stage |
| S6 | P3 | `hostui_test_lazy_pass` (nkeys) + unit-duration counter, ue2emu key flow | CI app stage, ue2emu |
| S7 | P5 | `tests/hostapp`, with control | CI app stage |
| S8 | P2, P5 | `tests/hostapp` on `hc_*` with a moved/renamed/removed entry | CI app stage |
| S9 | P1, P2, P3, P6 | board with startlog and a photo | board |

(Agents run no app tests, CLAUDE.md 2026-11-13: the CI stage runs them; an agent builds RISC-V and makes the ue2emu pictures.)

## §3 Decisions (all PROPOSED, Alex decides)

- **D1 (Alex's wording)** The boot menu's row data is cached like the menu picture and used at once; the check against the files
  runs in the background.
- **D2 What a record holds (per entry).** Identity: path, and boot.cfg's own fields for the line (hash, size, mtime, header digest) as
  they were when read. Directory entry at read time: size, mtime (what the background `stat` compares). The row: `EState`, `full`,
  the `CorInfo` fields the row and badges read (name, short name, version, contract, origin, large/id), the config count, and the
  icon in the form the screen draws (2048 bytes RGB565, or "none"). Filled from what `hc_row_entry` / `hc_einfo` / `icon_c` hold;
  nothing is computed anew. Upper bound by construction: 32 x about 2.4 KB = about 77 KB, file header and CRC included; the T146
  picture adds 92 KB beside it.
- **D3 Where.** `Flash:/menu.rows` beside `menu.snap` and `boot.cfg` (same mount, same driver, readable before the SD card is: T146
  made the same choice). Not the SD card: the host may start before it is mounted and never owns it. Space: the flash drive window
  is 0x400000-0xFE8000 (T152 spec); this file is a user of it, not a free pool. What is left is not computed here: the start logs the
  FAT's free clusters once beside the first write (S1/S9), that number is what counts. The T139 driver erases a sector per write, so
  the write rule of D6 applies.
- **D4 Start.** In `startup()` after boot.cfg is loaded and before `lazy_reset()`: load the file, check header (magic, format version,
  host app version hash as `snap_key_now` uses `APP_VERSION_HASH`, record CRCs). For every entry whose path and boot.cfg fields match a
  record: take the record into the entry's state, icon buffer and config-count cache, mark it `g_pend = verify` (a new phase), not
  `1`. Entries without a valid record: `g_pend = 1`, today's path. The T146 picture (kept) and the first LVGL frame then agree (P1).
- **D5 Background check (verify unit).** One entry per pass, in today's order (default, selection, window rows; T223 D2 stays: rows out
  of view are checked when they scroll in). A verify unit = `fs.stat(path)` and compares size and mtime with the record: equal ->
  entry done, no redraw; different or missing -> `g_pend = 1` (today's state unit, then icon), only that row redraws. No open, no
  probe, no hash, no icon read at menu time. The full hash stays where it is: at core start (T101 S10, `hc_start_sequence` with the
  trusted boot.cfg entry; to be re-read in the code when building, not taken from this text). Weaker than today at menu time:
  `startcheck_trusts` also compares the header digest and the stored hash; a file replaced by one of the same size and same FAT
  mtime (2 s granularity) shows its old row until the start check (Q2).
- **D6 Write.** After a full read and after every change of the row data or of boot.cfg: when the reader has gone idle, the menu is
  unchanged for the T146 settle time (2 s), and the encoded file differs from the file on the drive (CRC compare as `snap_save`
  does) -> write once. Never per unit.
- **D7 Invalidation.** A format version in the header, `APP_VERSION_HASH`, per-record CRC and a file CRC. Per entry: path and the
  boot.cfg line fields must match, else that record is dropped alone (T146 drops the whole picture on any boot.cfg change; the row
  cache does not need to). A bad header or file CRC drops everything: today's path.
- **D8 Keys win.** Verify units are one `stat`; the redraw after a verify that found nothing is none. The bound X on any unit between
  key checks is proposed as 100 ms and set from S1's numbers (Q4). A unit in progress is not interrupted (FAT calls are atomic);
  the migration (whole-file hash) is the one unit that can be seconds: it is cut at chunk boundaries with a key check (`cor_full_check`
  already takes a progress callback and `cor_abort`) or kept behind a longer quiet (Q5). Redraw: `need_full` after a unit only when
  that unit changed what a row shows (compare the new `HcEntryRow` and icon with what was drawn), not after every unit.
- **D9 The measurement (S1), cheapest: no JTAG.** The start record `startrec_event` (startfile.h; needs `startrec_decide`, which the
  menu path calls in `direct_phase`, to be confirmed in code) and `slog_flush` already write `/SD/report/startlog.txt` (8192 bytes
  max, readable from the board's web report page and by FTP). Add, when the reader first goes idle: one summary line with the four
  `STARTLOG` times (picture shown, menu built, first frame, idle), one line per unit "row N state X ms icon Y ms redraw Z ms" (the
  timings `lazy_step` already takes for its printf), and the longest main-loop pass. About 40 lines, about 2 KB. Needs the SD card
  present (else nothing is written, the next call tries again). No JTAG session, no asking Alex to attach anything: he starts the
  host with C= held; I read the file by FTP.
- **D10 Missing or broken cache.** Today's path, unchanged; the file is written new after the first idle (S5).

## §3a Open questions (asked one at a time, in this order)

- **Release scope (Alex 2026-11-16, asked "T229 in this release or after?", answer "in the release"):** T229 is built for the coming
  release; the release waits for it.


1. **Q1: answered 2026-11-16 (Alex, "both, measure first (recommended)"):** S1 first (board startlog per phase), then the row cache AND the build phase, so that keys act early. Question was: **Which part do the 2-3 s blocked seconds belong to?** The code says keys are queued, not acted on, until the first LVGL frame
   after `build_ui()` (2.5 s on the board of 2026-10-29), and a data cache does not touch that. Do you want T229 as the row cache
   only (D1-D10), with the build phase decided after S1's numbers, or is the build phase part of this task?
2. **Q2: answered 2026-11-16 (Alex, "size + time is enough (recommended)"): D5 holds.** Question was: **Size + mtime only at menu time (D5)?** Accept that a same-size, same-2-s-mtime replacement shows its old row until the start
   (the start check catches it), in exchange for no opens and no hashes in the background?
3. **Q3: answered 2026-11-16 (Alex): file on Flash beside the picture ("Flash, next to the picture (recommended)", D3 holds); config count by a stat of the `configs/` directory's mtime ("stat on the folder (recommended)").** Question was: **Cache file on `Flash:` (D3), and the config count with a stat of the `configs/` directory's mtime or cached until the
   next verify of its core?**
4. **Q4: answered 2026-11-16 (Alex, "100 ms, adjust after the measurement (recommended)").** Question was: **The bound X for a unit between key checks:** 100 ms to start with, then set from S1?
5. **Q5: answered 2026-11-16 (Alex, "as today after 1.5 s of quiet"): the migration stays as it is.** Question was: **The migration (whole-file hash of an entry without mtime):** chunked with key checks, or left behind 1500 ms of quiet as today?
6. **Q6: answered 2026-11-16 (Alex, "all in the background (recommended)"): after the visible rows every other cached entry is stat-checked.** Question was: **Rows out of view:** verified only when they scroll in (T223 D2), or all cached entries are stat-checked in the background?

## §4 Out (nothing here decided yet; Alex fills it)

- The compact modes' missing picture (T146 / T164 D10b), the core menus (T176).
- Direct boot (T135): no menu, no cache read.

## §5 Build notes

(filled in while building; the measurement lines of S1 go in with the startlog excerpt they came from.)
