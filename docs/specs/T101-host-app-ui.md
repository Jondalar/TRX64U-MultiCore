# T101 — The core-neutral host app scrolls, shows their icons, asks before it deletes, and lists only what the person added

**Status:** BUILT, native benches green, links (2026-10-15); board step S8b open (deploy freeze, Alex). 2026-10-25: S10 removes the scan of the media (P1, S1, S2, D1-D3 below are history) and the start check reads no part (a+b). 2026-10-26: S10 changed again - the start checks nothing; the menu comes up from boot.cfg alone and each entry is read afterwards, lazily (S10b).
**Repo:** TRX64U-MultiCore — `host-app/software/hostapp/` (own code), `tests/hostapp/` (benches), no gateware.
**Number:** T101 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-15: "send an agent off to build the neutral host app. With LVGL UI, please look in
the TRX64-Ultimate folder and at the host app. We have mocks, ideas and experience now." (translated)
**Depends on:** T84 (the lean host app, `.cor`, boot.cfg: this is its next step, not a second app), T94 D1 (layout).
**Milestone:** M3.

---

## §1 The problem

The host app of T84 exists, runs on the board and starts cores (`docs/specs/T84-hostapp.md`). Read from the
code (`hostui.cc`, 1,580 lines), what a person cannot do with it today:

- **P1 Find cores.** (Superseded 2026-10-25, S10: Alex wants NO search. The menu shows only what he adds by hand.) The menu only knows what the user added by hand: F3, walk the folders, RETURN. A fresh stick
  with `/cores/*.cor` and an empty flash disk shows "No cores yet". The task says the app "finds `.cor` files on the
  stick and in flash"; nothing in the code scans anything.
- **P2 More cores than fit.** `BOOT_MAX` is 16 entries, but menu and editor are fixed windows of `MROWS = 6` rows
  and no scrolling: entry 7 can be added and never seen or selected. Worse, the sixth menu row (y 386..442)
  already lies on the note line (y 396) and the countdown (y 426): the layout holds five.
- **P3 Recognise a core.** The mock shows an icon per entry; the bundle carries an optional 32x32 RGB565 icon
  (`cor_pack.py`, record 4) and the host reads none of it: every row shows the first four letters of the name.
- **P4 No accident.** DEL in the editor removes the entry at once and writes `boot.cfg`. The drop-in line's UI
  (`TRX64-Ultimate/docs/lvgl_ui_project`, "Interaction contract") gives every persistence-affecting action a
  confirmation; the host's remove has none.
- **P5 Keep the neutral line.** Whatever is built stays in the host: no C64 type, no core-specific branch.
  Cross-cutting, so it is checked, not built.
- **P6 The words of the mock.** The mock (`.claude/trx64-site/bootmenu-mock.html`) says "hash ok", "default",
  "contract N != host M", and its footers name the keys; the app says "ready", "contract" and other footers.
- **P7 Nothing to see while it starts** (Alex, 2026-10-23: "What takes so long until the host app is there? Do we do things
  before rendering the picture? Then it should get a startup bar or a splash or something.", translated). The console
  lines before the first menu frame carry no time, so nobody can say where the seconds go. Read from the code: the host
  app's first frame is `draw()` at the end of `hostui_run()`, after `try_load_cfg()`, a second `video_start()` (re-applies
  the PLL and resets the HDMI encoder even when the mode is the one already up), `build_ui()`, `probe_all()` (opens and
  reads every configured bundle for its badge and icon) and `scan_found()` (lists every medium, opens every bundle found).
  The core application is the same shape: `coreapp_run()` shows its first frame only when a message or the menu opens,
  so settings, picture mode, libraries (`<stick>/trx/lib`) and the ROM search all run on a black layer; the ROM wait loop
  can stay there for as long as the stick takes. Either application, during that time, looks like a dead board.
- **P8 "Checking cores" takes ages at every start** (Alex, 2026-10-25, T115 section 7: hash and check only when an entry is added; the start
  checks "a+b"). Read from the code before S10: `probe_all()` opened every entry (`cor_probe` + the icon; for a `.cfw` `cfw_probe`, which reads from
  the start of the image up to the first bitstream, `CFW_SCAN_MAX` = 4 MiB, and walks every bitstream's packets), then `scan_found()` listed every
  root and the folder `cores` of each and did the same again for every `.cor` and `.cfw` it found, the ones already in the menu included. The
  work grew with the files on the media, not with the menu.

## §2 Scenarios — what proves it is solved

All benches are native (x86/ARM Mac, no board): `tests/hostapp/test_discover.cc` and the render bench
`tests/hostapp/render/run.sh`, which since this spec runs the real `hostui.cc` over a real directory tree of
real `cor_pack.py` bundles (the firmware's file layer stubbed with POSIX). Board steps are marked.

```gherkin
Scenario: S1 the scan finds the bundles on every medium (P1)
  Given roots Flash, Usb0, SD; Usb0/cores/a.cor, Usb0/b.cor, Flash/cores/c.cor, SD/readme.txt, Usb0/old/deep/d.cor
  When  discover_cors runs
  Then  it returns exactly /Flash/cores/c.cor, /Usb0/b.cor, /Usb0/cores/a.cor, sorted by path
  And   NOT d.cor (two folders down; the browser is for that) and no duplicate when a root is listed twice

Scenario: S2 an empty menu shows what the scan found, and one key adds it (P1)
  Given an empty boot.cfg and the tree of S1 as real bundles (vc20.cor valid, c2.cor contract 2)
  When  the menu is built, then A is pressed on the valid found row
  Then  the row text dump shows two "found" rows (one "contract"), after A boot.cfg holds the path with the
        full-check hash, it is the default, and the found list no longer contains it
  And   A on the contract-2 row changes nothing and the note says why; RETURN on a found row calls the
        start with NO trusted hash (a core that was never fully checked is not trusted)

Scenario: S3 the list scrolls (P2)
  Given 9 entries in the menu and 5 found
  When  DOWN is pressed to the last row, then UP back
  Then  every step the selected row is inside the window of 5 rows, the position reads "10-14 of 14" at the end,
        and no row overlaps the note or the countdown (row bottom <= note top, asserted from the object geometry)
  And   listwin_fix is pure and tested on its own: it never moves a window whose selection is inside it

Scenario: S4 the icon of the bundle is shown (P3)
  Given a bundle with a 32x32 icon (a known pattern) and one without
  When  the menu is built
  Then  the row of the first has an image whose first pixel equals the pattern's, the other keeps the text tag
  And   a bundle whose icon hash is wrong shows the text tag (a damaged icon is not drawn)

Scenario: S5 removing asks first (P4)
  Given the editor with three entries
  When  DEL is pressed once, then another key, then DEL; then DEL twice in a row
  Then  after the first DEL nothing changed and the note says "DEL again"; the other key disarms it, the next
        DEL only arms again; DEL DEL removes exactly that entry and boot.cfg is written once
  And   the negative can fail: the bench counts boot.cfg writes (a single-DEL remove would show 1 write too early)

Scenario: S6 the mock's words are on the screen (P6)
  Given entries in each state
  When  the row text dump is read
  Then  states read "hash ok", "changed", "missing", "contract 0 != 1", "other host", "damaged"; the footers
        equal the mock's key list; compared with fixed expected strings in the bench

Scenario: S7 neutrality (P5)
  When  tests/hostapp/neutral.py scans the host app's own files for machine words (c64, vic, sid, cia, cartridge,
        basic, vc-20, amiga, atari, kernal, sprite) in code and strings
  Then  no hit; comments and the render bench's test block are not code the board runs
  And   `neutral.py --selftest` plants a word in a temp file and must see it (a scan that cannot fail proves nothing).
        Limit: a word boundary, so Gideon's identifiers `Keyboard_C64` / `keyboard_c64.h` (the C64U's own
        keyboard matrix, T84) do not match; they are firmware names, not a model of a machine

Scenario: S8 it links and fits (all)
  When  tools/build_hostapp.sh runs
  Then  hostapp.bin is built and linked; its size is reported next to the 618.4 KB of the T84 build
  And   S8b on the board (by hand, Alex; the deploy freeze applies, nothing was flashed): empty menu with a stick
        shows the found rows, A adds, the scroll list and the icons work with the C64U's keyboard; the log line
        `[host] scan: N bundles ... N ms` gives the real cost of the scan
```

Scenario: S9 something is on the screen before the slow work (P7)
  Given the host app or a core application starts on the pixel layer
  When  LVGL is open and nothing of boot.cfg, the screen mode, the menu, the media or the ROMs has been touched
  Then  the first frame is the splash: TRX64, what runs (host, or the core's name), the build, an empty bar
  And   every later step (host: boot.cfg, screen mode, cores in the menu; core app: settings, picture, libraries, ROMs)
        changes only the bar and its label (fewer than 12,000 px a step, the whole screen is 518,400), and names itself
  And   the host app's first menu frame comes before the media are listed (`scan_pending`); until the scan the empty list
        says "Looking for cores on the media ..." instead of "No cores yet"
  And   the splash leaves nothing behind: the first menu frame equals a full redraw (0 px differ)
  And   a message (a missing ROM file) takes the splash away, because the message is the pane's
  And   the same screen mode is not applied twice (`[video] this mode is up already`)
  And   every start-up line the apps print carries `[t+N ms]`, one line per phase, so the board console shows where the time goes
  Negative controls (each must turn the bench red): the scan before the first frame (`scanfirst`), no splash (`nosplash`),
        a splash whose steps do not move the bar (`nobar`)
  Test  render bench `SPLASH` lines and checks (host: `tests/hostapp/render/run.sh`; core app: `tests/coreapp/render/run.sh`)
  Board S9b (Alex, by hand, the deploy freeze applies): the console shows the timestamped phases; the splash is on the screen
        before `[t+... ms] phase Reading boot.cfg` finishes

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | scan | `tests/hostapp/test_discover.cc` | `tests/hostapp/run.sh` (native) |
| S2 | P1 | found rows, add, start-once | render bench, `tests/hostapp/render/run.sh` | native |
| S3 | P2 | scrolling window | `test_listwin.cc` + render bench geometry | native |
| S4 | P3 | icons | render bench | native |
| S5 | P4 | remove confirmation | render bench | native |
| S6 | P6 | wording | render bench | native |
| S7 | P5 | neutrality grep | `tests/hostapp/run.sh` | native |
| S8 | all | link and size; board | `tools/build_hostapp.sh`; board step by hand | container; board session |
| S9 | P7 | splash, progress, scan after the first frame | both render benches (`SPLASH`), controls `scanfirst`, `nosplash`, `nobar` | native |
| S10 | P8 | no search; no entry file is opened before the first frame; lazy read after (S10b) | `tests/hostapp/test_startcheck.cc`, render bench (S2, S10 lines), controls `fullhash_at_start`, `listing_at_start`, `ignore_mtime`, `ignore_header`, `probeatstart`, `listatstart`, `lazyall`, `lazykey`, `nohint` | native |

```gherkin
Scenario: S10 the start searches nothing and reads no part (P8; Alex 2026-10-25, T115 section 7 "a+b")
  Given a boot.cfg with entries added by hand, and bundles and firmware images lying on the media that are NOT in it
  When  the host app starts
  Then  the menu lists exactly the boot.cfg entries; no folder is listed, no file outside the entries is opened
  And   per .cor entry the start reads (a) the directory entry (size, mtime) and (b) the package header (header, manifest, record
        headers: under 2.2 KB) and the 2 KB icon; per .cfw entry only (a); no part of a package is read or hashed
  When  an entry's size, mtime or header digest differs from what adding stored, or its file is gone
  Then  the row says "changed" (still startable, the start then checks every part) or "missing"; it is never dropped; R re-checks it
  When  an entry comes from an older boot.cfg (no mtime)
  Then  the full check runs once at that start, boot.cfg gets size, mtime and header digest, and the next start does not read a part
  Negative controls: the full hash back into the start, a directory listing back into the start, the mtime or the header digest not
        compared, the migration at every start - each turns a check red
```

## §3 Decision

- **D1-D3 are history (2026-10-25).** Alex: "es zeigt bitte NUR noch an, was ich manuell hinzufüge, AUF KEINEN FALL .cfw oder .cor Files, die es
  irgendwo findet. Es sollte eh GAR NICHT nach diesen Files aktiv suchen" (translated: show ONLY what I add by hand, never .cfw/.cor files it
  finds somewhere, it should not search for them at all). `scan_found`, `discover_cors`, the found rows, `A`, the idle rescan and the
  web "found" list are gone; `discover.cc` keeps only the file-name test of the Add core browser. D10 below replaces them.
- **D1 Scan (`discover.cc/.h`, pure).** Roots from `hostfs_list("/")`; in each root the `.cor` files and the
  folder `cores` (any case) one level down. No deeper: the F3 browser is for that. At most 12 bundles, sorted.
  It takes the directory lister as a function pointer, so the bench runs it over a fake tree.
- **D2 When.** At start, when the set of media changes (the 3 s status tick that already compares them), and on
  `R` in the menu. Never inside a key's hot path. Every found file gets `cor_probe` (header and manifest only)
  and its icon; a bundle already in the menu is not listed twice.
- **D3 One list in the menu.** The menu rows are the boot menu entries followed by the found ones (badge
  "found", the line under the name says "A adds it to the menu"). RETURN starts the row (found: once, fully
  checked, no trusted hash); A adds a found row (the same full check as F3 does, shared function `add_bundle`).
  The editor lists only real entries.
- **D4 Window.** Five rows in menu and editor (the geometry that fits above the note), a window that follows
  the selection (`listwin.h`, pure), a position label "a-b of n" in the title line.
- **D5 Icons.** The icon record is read in `probe` with its SHA-256 check, kept in a per-entry 2 KiB buffer
  (28 entries, 56 KiB BSS), drawn as an `lv_image` RGB565 32x32 in the row's icon box. No icon, or a bad hash:
  the text tag as before.
- **D6 Confirm.** DEL in the editor arms ("DEL again to remove <name>"), the second DEL on the same entry
  removes; any other key disarms. The drop-in UI's rule for persistence-affecting actions, in the smallest form.
- **D7 Words.** Badges and footers follow the mock (§2 S6).
- **D8 Bench.** The render bench's stubs become real: a POSIX tree under a temp directory, `cor_pack.py`
  bundles, writes of boot.cfg recorded. Test hooks stay inside `HOSTUI_RENDER_TEST`.

- **D9 Splash and order (P7, S9).** One shared widget in the view layer (`trx_splash_open/step/close` in `app/view/trxview.cc`,
  drawn on LVGL's top layer so the menu built underneath cannot show through; the host's and the core app's splash are the
  same code). Drawn once; a step is a bar value and a label, rendered at once with `lv_refr_now`. The host app opens LVGL
  first (`startup()` in `hostui.cc`), then reads boot.cfg, applies the screen mode, builds the menu and probes the
  configured entries, then closes the splash and draws the menu once; the media scan runs in the loop right after that
  frame. The core application moves `ui_open()` to the top of `coreapp_run()` and names its steps; a message closes the
  splash. `video_start()` does not apply a mode that `video_current` already holds. `app/view/startlog.h` stamps
  every start-up line with the milliseconds since the scheduler started (5 ms ticks). Not moved: the libraries and the
  ROM search of a core application stay before the release of the core (a core must not run without its ROMs, T93 D4);
  USB enumeration, the network and the FTP daemon already run on their own tasks and are not waited for.

Scenario: S10b the start checks nothing, the entries are read after the first frame (Alex, 2026-10-26)
  Given a boot.cfg with 13 entries, some of an older boot.cfg (no stored check)
  When  the host app starts
  Then  boot.cfg is the only file loaded before the first menu frame: no entry is opened, stat'ed or read, nothing is migrated or saved,
        no folder is listed; the rows show the placeholder state "?" and the neutral icon; the default entry can be started
  When  the main loop runs
  Then  one phase of one entry is read per pass (phase 0: directory entry and package header = the state; phase 1: the icon), the default
        entry first, then the selected one, then the rest; a pass that handled a key, or follows one within 200 ms, reads nothing
  And   an entry of an older boot.cfg is migrated (full check once) only when nothing else waits and no key was seen for 1.5 s
  When  a core is started from the menu
  Then  `core_stage` verifies it while it loads (SHA-256 of every part unless size, mtime, header and stored hash all match the entry);
        a missing, changed or damaged file is refused with the reason and "Press R on the entry in the menu to check it again" 

- **D10 Start check (S10; `startcheck.cc/.h`, pure; T115 section 7 "a+b").** Adding (F3, RETURN on a file; `R` on a row re-does it) is the only
  full check: every part read and hashed (`cor_full_check`); the entry stores the partial's hash, the file size, the directory entry's
  mtime (FAT date << 16 | time) and the header digest (`CorInfo::hdr_digest`, FNV-1a over the 20 header bytes and the manifest, which holds
  the SHA values of every part). boot.cfg line: `core=<path>|<name>|<hash>|<size>|<mtime>|<digest hex>`; the first three fields are the old
  format, so the coreapp's round trip through `bootcfg_*` keeps what it does not know. A start does, per entry, `startcheck_entry`: one
  stat (a) and `cor_probe` (b); equal to the stored values = "hash ok" and the stored hash stands in for reading the parts
  (`startcheck_trusts`, also the rule `core_stage` uses); different = "changed", startable, the start checks every part. A `.cfw` entry: (a)
  only; different = parsed once (`cfw_probe`) and shown as "changed" or "unusable". An entry without mtime (older boot.cfg) gets the full
  check once in `migrate_entries()` at the first start and is saved; one whose stored hash or size does not match the file is NOT accepted
  (it stays "changed"). No entry is ever dropped by a check. Open, not decided here: how a `.ue2`/`.cfw` is handled in the long run
  (T115 section 7); nothing about it changed except that nothing looks for it.

Kept from the drop-in line's app, not built here: nothing is copied (its UI is the C64 application's; the
rules taken are the confirmation, the "state before actions" order and the flat list).

## §4 Out

Left out by the builder of this spec; each line is a question for Alex before it counts as decided.

- **O1 Update from the host app** (`.cfw`, T84 "Decision 2026-10-14"): Alex said later.
- **O2 REST `GET /v1/cores`**: the host app has no HTTP server; needs its own spec (and the T93 blueprint
  decides what the core apps offer).
- **O3 Sixtyfour display font**: not in the tree, the converter is not run here; Montserrat stays.
- **O4 Network setup** (DHCP/static, Wi-Fi through the ESP32): the page shows state only, as in T84.
- **O5 Rename of an entry, per-core settings, a start-screen step time column of the mock**: not asked,
  not in P1-P6.
- **O6 The "other host" / contract check of a *found* bundle beyond the badge** is the same as for added ones.
- **O7/O8 are void since S10: nothing scans.** (O7 Deeper scan, folders two down) and a scan of the stick on every F1: cost and clutter, the browser exists.
- **O8 A rescan for a stick that is inserted while the root name does not change**: the tick sees only root names;
  `R` covers it.

## §5 Decisions taken while building

Built 2026-10-15. What the benches printed (`tests/hostapp/render/run.sh`, native, LVGL v9.6 and the real `hostui.cc`):

```
render bench: 38 checks, 0 failed          (S2 found rows / add / start-once, S3 scroll, S4 icons, S5 confirm, S6 words)
double buffer sync: 0 of 40 steps stale     (the T84 check, unchanged)
test_discover: ok                            (S1, S3 window; tests/hostapp/run.sh)
neutral selftest: ok / neutral: ok           (S7)
```

Negative controls that were made to fail: the scan with "any subfolder" instead of "cores" fails S1 (mutant of
`discover.cc` run against `test_discover.cc`: `test_discover: 7 FAILED`; the first version of the test did not fail
because its tree listed `cores` first - the tree was reordered so the mutant is caught). S2 asserts the contract-2 found
row is refused and that start-once passes no trusted hash while a configured entry does. S5 counts boot.cfg writes
(0 after DEL, DEL+key, DEL+UP+DEL; 1 after DEL DEL). S4 asserts a bundle with a damaged icon byte keeps the text tag.

`tools/build_hostapp.sh` (u64-build-env, `JOBS=2`, nice): links, `hostapp.bin` 645,380 B against 633,280 B of the T84 build
(+12,100 B). The icon buffers (28 x 2 KiB) are BSS, not in the image. No new warnings (the deprecated `lv_obj_set_flag`
ones are the existing style).

Choices the spec left open:
- **Window of 5, not 6.** Measured from the object geometry: the sixth menu row (y 386..442) overlapped the note (y 396)
  and the countdown (y 426) already in T84; the bench asserts rows_bottom <= note_top < countdown_top at every scroll step.
- **Rescan trigger.** Media change (the 3 s tick that compares the root names), `R`, and, while nothing has been found,
  every third tick: a stick that is still being mounted shows its root late, and a rescan every 9 s while nothing is
  found costs a few directory reads outside any key. Once something is found the idle rescan stops.
- **Found rows are not "hash ok".** They have not been through the full SHA-256 check (T84: checked when added), so the
  badge says "found"; the check happens on A (same function as F3's add, `add_bundle`) or at a start-once.
- **Start once from a found row passes no trusted hash**, so core_stage hashes every part.
- **Icons:** read in `probe` and `scan` with the hash check of `cor_read_part`; shown for bundles of another contract
  or host as well (the badge says why they cannot start).
- **`hostui_test*` hooks** stay inside `HOSTUI_RENDER_TEST`; the board build does not contain them.
- The render bench is not in `ci/lint.sh` (it compiles all of LVGL natively, minutes): `tests/hostapp/run.sh` (scan,
  window, neutrality, .cor, boot.cfg) is.

Untouched on purpose: `cores/`, `contract/`, `host/*`; the tree already held uncommitted changes under `cores/amiga*`
from another session when this work started; none of them is in the T101 commits.

- **2026-10-22, Add core on the board photo:** a label has to be one line high to be cut with dots (`line1()`), the folder title uses the UI font, the hint is one key per line, and a scrolling key overflowed LVGL's 32 invalid areas into the whole screen (`lv_refr.c:339`); the first fix saved the three columns' rectangles first (`browse_rows_area_first()`), which the area work below replaced (the function is gone). Numbers and controls: `docs/todo/T101.md` history of the same day; the render bench holds the checks.

- **2026-10-22, areas: every key redraws what changes (Alex: "ok, then reduce the areas, put an agent on it", translated).**
  LVGL draws about 2.2 ms per 1,000 px on the board (instruction cache only, DIRECT into DDR2), so the invalidated area is the time.
  Board 5c24d90e: a selection move in the boot menu was 84,000 px, a page change the whole screen. Causes found in the code:
  - **`row_style()` restyled the row box (bg and border colour) of the old and the new row**, and `update_menu`/`update_edit` called it on
    all five rows on every full update, so any key that was not a plain move (D, +/-, a note, R, a countdown change) invalidated five
    full rows (up to 197,000 px for `D` in the editor). The selection is now four thin strips per row (2 px frame, 5 px bar on the left),
    shown or hidden (`marks_build`, `marks_show`); a move redraws two such marks, about 6,800 px. The browser's tinted bar `b_hl` is the same
    mark on 24 px rows.
  - **Style calls that change nothing still invalidate** (`lv_obj_set_style_border_color` of the badge, `set_pos`/`set_width` of the icon box,
    the four navigation entries on every view switch): now guarded by a compare, as `set_txt`/`set_col` already were.
  - **A label's box is redrawn whole when its text changes.** One-line labels that change while a view stays (names, paths, notes, manifest
    values, the countdown, the footer) are now as wide as their text, at most their column (`fit1()`, kept in the label's user data,
    applied in `set_txt`). The countdown second fell from 19,500 to about 8,000 px, a note from 12,700 to about 1,300 px.
  - **LVGL's invalid-area buffer** is `LV_INV_BUF_SIZE` in `lv_display_private.h` (`#ifndef`, default 32; the LVGL tree is the
    `apps/ultimate` submodule and stays untouched). `host-app/software/hostapp/lv_conf.h`, which the host app, the core apps and all native
    benches build with, defines 128: 17 bytes a slot (2,176 B instead of 544 B in the one display), `lv_refr_join_area()` is O(n^2) in the
    saved areas (at most 16,384 rectangle tests, microseconds). With the causes above gone the worst key measured holds 29 areas, so 32 was
    three short, not the cause; 128 is headroom (a longer manifest, an icon on every row). The saved-rectangles-first trick for the browser
    is removed (it cost 135,000 px where the labels alone cost 36,000).
  Visible change: the selected row is no longer tinted. It has a 2 px frame in the host colour and a 5 px bar on its left edge (menu, editor
  and the Add core list alike). Everything else looks as before. A tinted row cannot be moved without redrawing the two rows (84,000 px,
  ~190 ms); the frame costs 2 x 3,400 px.
  Bench (`tests/hostapp/render`, native; `px` = invalidated pixels of one key, `ms` = px x 2.2 ms per 1,000 px, an estimate from the board
  figure above, not measured on the board; "before" is the same bench against the code of `e8c7eceb` with only the test hooks added):

  | case | before px (ms) | after px (ms) | bound of the check |
  |---|---|---|---|
  | F1 selection move | 84,000 (185) | 6,784 (15) | 2 marks, 6,896 |
  | F1 page change down / up | 348,656 / 347,964 (767) | 54,428 / 54,058 (120) | 5 rows, 210,000 |
  | F1 countdown second | 19,500 (43) | 7,700-8,880 (17-20) | 10,000 |
  | F1 `R` (note) | 245,716 (541) | 1,312 (3) | 4,000 |
  | F1 chips change (medium or network) | 24,960 (55) | 24,960 (55) | holder, 25,600 |
  | F5 selection move | 66,000 (145) | 6,616 (15) | 2 marks, 6,704 |
  | F5 page change | 290,928 (640) | 39,516 (87) | 5 rows, 165,000 |
  | F5 `D` (default) | 197,568 (435) | 4,572 (10) | 3 rows, 99,000 |
  | F5 countdown +/- | 182,148 (401) | 648 (1) | 1 row, 33,000 |
  | F3 selection move | 23,334 (51) | 4,980 (11) | 2 marks + panel, 9,320 |
  | F3 scroll one row (13 rows) | 135,342 (298) | 36,418 (80) | one panel, 139,460 |
  | F3 scroll, 20 bundles of different sizes | 140,934 (310) | 25,924 (57) | one panel, 139,460 |
  | F1/F3/F5/F7 view switch | 419,548 (923) | 388,632-402,840 (855-886) | the view + 35,000 |

  A view switch repaints the whole content container (790 x 470 = 371,300 px), the two navigation entries and the footer line: that is the
  floor for a switch, so it stays near 880 ms. The chips are rebuilt as one holder when a medium or the
  network changes (once per event, never per tick: `refresh_status()` only reports a change).
  Controls (each puts one cause back and the bench goes red): `rowtint` (selection as tinted row, 10 checks), `widelabel` (labels at column
  width, 8), `thick` (marks 12 px thick, 19), `navall` (all navigation entries restyled, 3), `inv32` (the stock 32 slots: the slot check and the
  area counts, 2). The bench also compares, after every case, the shown buffer with a redraw of everything (a smaller area that leaves
  something behind shows as a differing pixel). `AREA_DUMP=1` in the environment prints the rectangles of every key.

**S9 start-up splash and time stamps (2026-10-23, host app; the core application follows below).**
Phases of `ultimate_main()` and `hostui_run()` as the code orders them, and what each waits for (read from the code, not measured yet:
the board lines `[t+N ms]` give the numbers):
1. `riscv_main.c` "-- Custom Hardware Init --", scheduler start (no stamp: before the tick runs).
2. `InitFunction::executeAll()`: network config, lwIP (`tcpip_init`, creates a task), RMII, FTP daemon object (its listen task prints
   "FTP server starting" by itself). Waits for nothing: no DHCP, no link.
3. `usb2.initHardware()`: clears the nano RAM and creates the USB task; the root hub, the hub, mouse, keyboard and stick are
   enumerated on that task at a higher priority than the UI task (`PRIO_POLL`, `PRIO_DRIVER` against `tskIDLE_PRIORITY + 1`), so the UI
   task is not waiting for them but is slowed while they run (`vTaskDelay(35 * i)` retries inside `init_device`).
4. `video_start(-1, -1)` in `hostapp.cc`: reads the EDID over I2C (DDC) when the hot-plug line is up, programs the PLL, resets the HDMI
   PLL. **Blocks, and the screen resyncs.**
5. `hostui_run()` (own task) before this change: `try_load_cfg()` (`hostfs_load`; may fail when the flash disk is not mounted, retried
   every 3 s), a second `video_start()` with boot.cfg's choice (EDID read again, **PLL reset again even for the same mode**),
   `trx_lv_open()` (two 1 MB buffers, `lv_init`), `build_ui()`, `probe_all()` (`cor_probe` + icon of every configured bundle: reads from
   the stick or SD), `refresh_status()` (root list), **`scan_found()`** (`discover_cors` over every root, `cor_probe` and icon of every
   `.cor` and `.cfw` found), then `draw()` = the first frame. Nothing waits for the network; only the countdown waits (BOOT_WAIT_MS =
   30 s for a default whose medium is not there yet), and it starts after the frame.
What changed: LVGL opens first and the splash is the first frame (step labels "Reading boot.cfg", "Setting the screen mode", "Checking
the cores in the menu"); a mode that `video_current` already holds is not applied again; the media scan moved behind the first menu
frame (`scan_pending`, the loop runs it before anything else, the hint says "Looking for cores on the media ..." until then); every
start-up line has `[t+N ms]` and each phase a line of its own. Bench (`tests/hostapp/render/run.sh`, builder, `u64-build-env:ci`):
`render bench: 166 checks, 0 failed`; `SPLASH step 0: 518400 px changed` (the whole splash once), `step 1: 1894`, `step 2: 2259 px`
(bar and label), `first menu frame vs a full redraw: 0 px differ`; controls `scanfirst` (2 checks red), `nosplash` (1), `nobar` (1);
the 15 older controls still red. `tests/hostapp/run.sh` and `tests/view/run.sh` (20 checks) green. Link: `hostapp.bin` 692,884 bytes,
`coreapp.bin` 824,888 bytes (builder, `make`, both link). Screenshots of the bench: `splash_0.png` .. `splash_2.png`, `splash_menu.png`.
Board S9b is Alex's: the numbers behind "what takes so long" arrive with the first run of this app.

**S9 the same for the core application (2026-10-23, Alex: "And the agent could also look at the CoreApp afterwards. So there too first the
picture, then read USB etc.", translated).** Order in `coreapp_run()` before: `start_paths`, mouse/keyboard set-up, `ui_open()` (LVGL
only, no frame), `Settings::load()` (a file on the stick or flash disk), `Video` and `picture_start()` (a second mode application when
the Picture setting differs from the host's), `lib_board_load()` (every `.u2a` of `<stick>/trx/lib`; a refusal waits up to 6 s for a
key), `boot_start()` (descriptor init, then the ROM search loop: **it does not leave until every required ROM is found**, polling at the
wait's own delay, 5 s after a refused set), core release. The layer showed its first frame only for a message or the menu, so all of
this ran on a black screen. Now `ui_open()` is the first call and the splash (the core's name, the build, steps "Reading the settings",
"Setting the picture", "Libraries on the stick", "ROM files, then the core"/"Starting the core") is the first frame; `ui_message()`
(a missing ROM, a library refusal) closes it because the message owns the pane; `BOOT_RUNNING` closes it before the layer is
suspended and the core's picture shown. Not moved: libraries and ROMs stay ahead of the release (a core must not run without its ROMs,
and the libraries register what the menu and executor use, T104.1 D12); USB enumeration and the network are tasks of their own already.
`coreapp.cc` and `run.cc` stamp every start-up line and each phase (`phase init functions done`, `core identified`, `Reading the
settings` ... `boot_start returned`). Bench (`tests/coreapp/render/run.sh`, builder): `coreapp render: 66 checks, 0 failed`; `SPLASH step
0-3: 788 / 1,575 / 1,601 / 1,708 px changed`; the three controls (cheap path, areas) still red; the splash PNGs are `atari-splash_0..3.png`
and `atari-splash_rom.png` (a missing-ROM message in place of the splash).


**S10b as built: the start checks nothing (2026-10-26; Alex, translated: "it still stands 11 seconds at 'checking the cores' .. and that is too much. We
take the check out, it is not bearable").** Why: on the board the start stood 11 s at "Checking the cores in the menu" with 13 entries although every
entry was migrated, about 0.85 s per entry (stat, package header and icon read over the SD). The a+b check below was already cheap per byte and still
too slow per entry. What changed in `hostui.cc`: `startup()` no longer calls `migrate_entries()`/`probe_all()` (both gone, with the per-entry splash
progress and `trx_splash_part` of 2026-10-26, which only served them); the splash has 3 steps ("Reading boot.cfg", "Setting the screen mode",
"Building the menu"). `lazy_reset()` marks every entry unknown (state "?", neutral icon, `startable` true); `lazy_pass()` is called once per loop
pass after the keys and does `lazy_step()`: one phase (`probe_state` = stat + header, or `probe_icon`) of one entry. A key in this pass or within 200 ms
(`LAZY_QUIET_MS`) skips the step. `migrate_one()` (old boot.cfg entries) runs last and only after 1.5 s without a key (`LAZY_MIG_QUIET_MS`). The
view switches (F1, F5) no longer re-read every entry; move, remove and a late boot.cfg call `lazy_reset()`, `R` marks the selected entry, the
3-second loop marks missing entries again (its medium may have come up). A start-up failure of a menu entry now says "Press R on the entry in
the menu to check it again" (`start_fail`). The `.cfw` path is as it was: `start_full(verify = !trusted)`, a menu entry is not hashed again.
Cost of a pass the loop cannot cut: one phase, i.e. one stat + header read (about 0.5 KB) or one 2 KB icon read; a key that arrives during it waits
for it. The board time of a phase is not measured yet (the log prints `[host] entry i <path>: state|icon read N ms` per phase); the migration reads
the whole file in one call and blocks keys for as long as the hash takes (seconds for a large package), once per old entry.

**S10 as built (superseded by S10b above where it speaks of the start): no search, the start checks "a+b" (2026-10-25; Alex: "Es sollte eh GAR NICHT nach diesen Files aktiv suchen", T115 section 7 "a+b").**
Files: `startcheck.cc/.h` (new, pure), `bootcfg` (two more fields), `cor` (`hdr_digest`), `hostfs` (`hostfs_stat`), `coreboot` (`core_stage` takes the
boot.cfg entry and uses `startcheck_trusts`), `hostui.cc` (`probe_entry`, `migrate_entries`, `recheck_entry`; `scan_found`, `found_filter`,
`add_found_*`, the found rows, `scan_pending`, `media_changed` removed), `webui` (no `found`, no `/v1/menu/add`), `discover.cc` (only the file-name
test). Points where the code is not the same as D10 reads at first sight:
- The icon is still read per entry (2,048 bytes with its SHA-256) for the row; it is the only part of a package the start touches.
- `refresh_status()` still lists the root of the file system ("/": the names of the mounted media) for the chips and the network page; it
  opens no medium and looks at no folder. The bench counts listings below "/" (`g_list_deep`): 0 at start.
- `R` in the menu is the re-check of the selected entry (the full check again, new size, mtime and digest stored); it replaced the rescan.
- A `.cfw` entry whose mtime or size differs is parsed once (`cfw_probe`) and shown "changed" (parse ok) or "unusable"; one that matches is
  not parsed (its name and logo come from the entry). An older `.cfw` entry is parsed once to prove it usable, then gets size and mtime.
- The old behaviour that an entry whose stored hash differs from the file shows "changed" is kept, also for entries without mtime: they are
  not migrated silently.

Measured (native; `tests/hostapp/run.sh`, `tests/hostapp/render/run.sh` on the builder, `u64-build-env:ci`):
```
bytes read: add 226136, start 476 (file 226136)            test_startcheck: one .cor entry, stub file system
start of one entry: 2517 of 9625 bytes read                render bench: header + manifest + record headers + the 2 KB icon
test_startcheck: ok   control fullhash_at_start (4 red), listing_at_start (2), ignore_mtime (1), ignore_header (2)
render bench: 173 checks, 0 failed; controls listatstart (2 red), fullatstart (7), nosplash, nobar and the 15 older ones still red
tests/coreapp/run.sh: test_libguard 124 checks, 0 failed (boot.cfg round trip keeps the new fields)
```
Link (builder, `make`, the trxapps stage's commands): `hostapp.app` 691,704 bytes of data (before: 692,884 on 2026-10-23), `coreapp.app` 855,628;
the slot takes 1,966,068. Start cost from the code path, per entry (estimate, not a board measurement; the board lines `[t+N ms]` of S9b give the
real time): a `.cor` entry read about 2.5 KB (476 bytes of header and manifest, the icon) in `probe_all` and again in `scan_found` (the scan
probed every file it found, the ones in the menu too) - now once; a `.cfw` entry: `cfw_probe` reads from the start of the image up to the first
bitstream in 4 KB blocks (the updater code in front of it is at least 236 KB: the magic sits at +0x39B54 in TRX64U rc1, T115 section 7), at most
`CFW_SCAN_MAX` = 4 MiB, then the packets of every bitstream in 512-byte windows, in `probe_all` and again in `scan_found` - now 0 bytes (a stat).
Per root: one directory listing for the root, one for its `cores` folder, each file system walk gone. One time only, for an entry of an older
boot.cfg: the full check (all parts, the file's size in bytes) at the first start, then never.

- **2026-10-22, layout resolution:** `UI_RES` 0 (960 x 540 at scale 2, default) or 1 (the same canvas at scale 1, centred); the UI code is the same, only `PX_CTRL` scale and `PX_POS` change. A canvas wider than 960 cannot be shown at scale 1: `host/pix_layer.v` has a line buffer of 2 x 1024 pixels (W <= 960). Details and numbers: `docs/todo/T101.md`.
