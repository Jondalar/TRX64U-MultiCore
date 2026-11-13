# T198 — A core can be started from the boot menu with a saved config (media set + settings), picked in a small overlay

**Status:** IN PROGRESS (2026-11-10, Alex: "agent losstarten, code bauen" - build go). D1-D14 decided; Q13 open (centred overlay stands). Corrected 2026-11-13 (status check): BUILT, merged to main (b390802b3 and earlier); emulator and bench evidence plus ux-review PASS in §5; S17 board (save, overlay, start, power cycle) open; Q13 stands as is.
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp/`: boot menu rows, `host_core*`, `hostdecl.cc`, `handover.{h,cc}`,
`coreboot.cc`), core app (`coreapp/src/`: `mediamounts.cc`, `menutree.cc`, `menuctl.cc`, `settings.*`), mock-up `docs/mockups/T198-config-overlay.html`.
**Number:** T198 (registry: `TODO.md`).
**Origin:** GitHub issue #58 "Per core configurations" (Alex's own proposal, a tester: "a quick way to launch a pre-defined config on a
sub menu of a core"); Alex, 2026-11-10 (D1).
**Depends on:** T180 D5 (media sets, built), T148 (core folder and `.cfg`), T135 (direct boot, hand-over page), T192.1/T192.2 (intents; the host
UI is a client of `host_core`). Nothing is blocked.
**Milestone:** M2.

---

## §1 The problem

- **Who:** a tester or Alex at the boot menu who owns one core with several set-ups (Amiga 1200: a favourite hard-disk install, a demo
  collection, a blank Workbench; a C64 with a disk and a cartridge per game).
- **What they want:** pick the core *and* the set-up in one go, without starting the core, opening its menu, loading a media set and
  waiting for the reset the load causes.
- **Today (read from the code):** the boot menu has one row per `.cor` (`bootcfg.h` `BootEntry`: name, path, hash, size, mtime, hdr); RETURN
  starts the core plain. The hand-over page (`handover.h` `struct Handover`: path, core name/version, host key, video, id, time) carries no
  "what to start with". A media set is a text file `<core folder>/media/<name>.mnt` with the `Mount <slot>=...` keys only
  (`mediamounts.cc` `mediaset_save`, `mediaset_dir`); it is saved and loaded only from the running core's media tab ("Save media set...",
  "Load media set...", `menutree.cc` 162-163), and loading it resets the core. The core's settings live in `<core folder>/<name>.cfg`
  (`settings.h`, written at every change) and are not part of a set. Result: three menus and a reset between the user and the game.
- **Why it matters:** the boot menu is the first thing every tester sees (issue #58, one collaborator agreed); four entries per core
  ("Amiga 1200 -> my HD / demos / blank / none") would flood the 16-entry menu (`BOOT_MAX`).

**Parts (MECE, in the order of the use case):**

- **P1 Save:** in the running core's menu (F5 Summary) the user saves the current media and settings as a named config.
- **P2 List:** the configs of one core are listed, from the core folder, by the host (before the core runs) and by the core app.
- **P3 Boot menu overlay:** a key on a core row opens a small overlay with that core's configs; cursor picks; RETURN starts the core with
  it; ESC / LEFT closes; RETURN on the row itself still starts plain (D1).
- **P4 Start applies:** the core start applies the chosen config (media set + settings) before the core runs its first frame.
- **P5 Load while running:** the core menu loads a config of its own core (F5 Summary), as "Load media set..." does today.
- **P6 Manage:** rename, delete, mark one as default, per core.
- **P7 API:** the same operations as intents / `/v1` routes (T192), so a second UI or a script can list, start, save and manage configs.
- **P8 Failure and edges (cuts across):** a medium of the config is missing, the core has no folder, the card is read-only, a core with no
  config, a name that does not fit, the 16-entry limit, a stale hand-over page.

## §2 Scenarios

Bench = native test on the Mac/builder (`ci/apptests.sh`); emu = ue2emu script (`tools/dev.sh emu`); board = by hand on the C64U. Negative
controls (T174 D5.4) only where a flash/SD write is the point (S1, S8, S9).

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | Given the A1200 runs with DF0=Install.adf, HD0=folder DH0_WB31 and Fast RAM 8 MB, when the user picks F5 Summary > "Save as config..." and types "Workbench 3.1 HD", then `<core folder>/configs/Workbench 3.1 HD.cfg` holds the `Mount` keys of every mounted slot and the changed settings, and the status line says what was saved (count of media, count of settings). A name with `/ \ : * ? " < > \|`, empty, or over the limit is refused with the reason. Negative control: a read-only card gives the error line, no half file | `tests/coreapp/test_configs.cc` (save/refuse/read-only, control: file absent after a failed save) | apptests |
| S2 | P2 | Given three config files and one foreign file in `configs/`, when the host lists the core's configs, then it returns three names, sorted as the core lists them, never the foreign file; the count shown on the row equals the list length; an unreadable folder gives 0 and no error row | `tests/hostapp/test_config_list.cc` | apptests |
| S3 | P3 | Given the boot menu with the A1200 row selected and 3 configs, when the user presses the open key, then the overlay "Amiga 1200 - start with" shows the 3 configs and "Plain start", the cursor on the first, the footer shows the overlay's keys, and the rows behind do not change | emu script + `tests/hostapp/render` picture (mock-up as the target) | emu, ux-review |
| S4 | P3 | In the overlay UP/DOWN move the cursor (wrap off at both ends), the selected row is always inside the overlay when the list is longer than its window ("1-6 of 9" shown), LEFT or ESC closes it and the boot menu selection is unchanged | render bench (R1: longest list, last row selected) + `host_core` key-map test | apptests |
| S5 | P3 | RETURN on the core row with the overlay closed starts the core plain, exactly as before (same steps, same hand-over page without a config); the open key on a core with no config either does nothing or opens the overlay with "Plain start" only (Q7) | `tests/hostapp/test_directboot.cc` + new key-map test; emu | apptests, emu |
| S6 | P3, P4 | Given the overlay with "Demo collection" under the cursor, when RETURN is pressed, then the start overlay says "Starting Amiga 1200 - Demo collection", the hand-over page carries the config name (Q5), and the core comes up with those media | emu flow host -> .cor -> core, then board for the real A1200 | emu, board |
| S7 | P4 | The core app reads the config name from a valid hand-over page, applies the `Mount` keys and the settings, and the first frame already shows the mounts; a settings item marked "at next reset" is in effect (the core starts after the apply). A hand-over page without a config (or a stale one: sum/id check fails) starts plain | `tests/coreapp/test_configs.cc` + `test_handover.cc` (tail field, old reader, old host) | apptests |
| S8 | P4, P8 | Config names a file that is not there (stick removed): the core starts, the other media of the config are mounted, the message names the missing path and what to do; nothing is silently skipped (Q10). Negative control: no `.cfg` is rewritten with the missing slot emptied | bench with a fake FS | apptests |
| S9 | P6 | Rename, delete (asks twice, like DEL in F5 Edit menu) and "set as default" in the overlay change only that config's file / the default marker; a rename to an existing name is refused; delete of the default clears the default. Negative control: after each write the other configs are byte-identical, and a failed write leaves the old file | `tests/hostapp/test_config_manage.cc` | apptests |
| S10 | P5 | In the running core F5 Summary > "Load config..." lists the same configs as the host for that core and applies one the way a start does (D4 write back before a medium is let go, then reset) | `tests/coreapp/test_configs.cc` + emu | apptests, emu |
| S11 | P5 | The old rows "Save media set..." / "Load media set..." are gone or kept as decided in Q2; a set saved before T198 is still found (Q2) | bench over a folder with `media/*.mnt` | apptests |
| S12 | P7 | Every operation of S1-S10 is reachable as an intent with a stable id on the host (`config.list`, `config.start`, `config.rename`, `config.delete`, `config.default`) and the core (`config.save`, `config.load`, `config.list`); the result equals the key path; a start while a job runs is refused (T192.2-S5) | `tests/hostapp/test_host_core_intents.cc` (+ core app counterpart) | apptests |
| S13 | P7 | `GET /v1/status` (host) lists the entries with `configs: n`; a script starts "Demo collection" over the web route and the same overlay-less start runs | emu with `--control`, curl | emu |
| S14 | P8 | A core with no folder of its own (not started from a `.cor`, `cfg_path` empty): the core menu says "no folder, so no configs"; the host shows no badge | bench | apptests |
| S15 | P8 | 40 configs on a core: the badge says "40 configs", the overlay scrolls, the key response stays within one frame budget (R8: the listing is read once per menu generation and cached, not per key) | bench with a fake FS counting directory reads | apptests |
| S16 | All | Mock-up, then the built screens, pass the `ux-review` skill (R1-R9) at 480p and 1080p; the coordinator opens the pictures | `ux-specialist` run | before merge and board |
| S17 | P4 | The full line on the board: save in the A1200 core, return to the host menu, open the overlay, start it, the media are there; power cycle by Alex, the config survives | board, one session, with a photo | board |

## §3 Decision

- **D1 (Alex, 2026-11-10, translated):** "In the boot menu RETURN must stay 'start'; somehow else the list of saves opens as an overlay
  and I pick with the cursor which config I want to start."
  Consequence: no extra boot menu rows per config; no change to what RETURN does; the overlay is a second, optional step.
- **D2 (Alex, 2026-11-10, Q1: "Rechts"):** RIGHT alone opens the config overlay on a core row; no letter alias.
- **D3 (Alex, 2026-11-10, Q2: "Ja ersetzen"):** configs replace media sets: the two core-menu rows become "Save as config..." /
  "Load config...", old `media/*.mnt` files are listed as configs (converted on their first save); no separate media set rows remain.
- **D4 (Alex, 2026-11-10, Q3: "B"):** starting with a config is one-shot: its settings and mounts are applied in memory only; the core's
  `.cfg` is not rewritten, so the next "Plain start" is as before.
- **D5 (Alex, 2026-11-10, Q14: "C"):** while a core runs with a config, a setting changed or a medium mounted in the menu is written
  into that config (as the `.cfg` is written today in a plain start); the core's `.cfg` stays untouched.
- **D6 (Alex, 2026-11-10, Q4: "A"):** a config holds every setting of the core plus its mounts (a full copy, not a difference).
- **D7 (Alex, 2026-11-10, Q5: "A"):** "start with X" travels in a tail field of the host->core hand-over page (`config[48]` after `time_*`,
  outside the checksum like `id_sum`/`time_sum`); nothing is written to the SD for it. An old core app ignores the field (plain start).
- **D8 (Alex, 2026-11-10, Q6: "C"):** a core's default config is stored in its `boot.cfg` entry (D in the overlay writes it); the countdown
  and direct boot start that entry with its default config; RETURN on the row stays a plain start (D1).
- **D9 (Alex, 2026-11-10, Q7: "A"):** RIGHT on a core row without configs does nothing (no badge, no hint there, mock-up (a)).
- **D10 (Alex, 2026-11-10, Q8: "the folder is always made at the start, isn't it?" - yes: `corefolder_ensure`, `coreapp/src/corefolder.cc:117`,
  T148):** `configs/` lives in the core folder the core app makes at every start; only a read-only medium or a JTAG start without a .cor
  has none, and then there are no configs (no flash fallback). `corefolder_ensure` also makes `configs/`.
- **D11 (Alex, 2026-11-10, Q9: "Passt"):** at most 16 configs per core, a name of up to 32 characters (fits D7's `config[48]`), 8 rows
  visible in the overlay, the rest scrolls.
- **D12 (Alex, 2026-11-10, Q10: "A"):** a config whose medium is missing starts anyway: that drive stays empty and a message names the
  missing file; the config itself is not changed by it.
- **D13 (Alex, 2026-11-10, Q11: "also in F3 ... in F5 one should also be able to pick/set the config apart from the default, shouldn't
  one?"):** F3 File Browser: RIGHT on a .cor opens the same overlay and starts it once with the picked config. F5 Edit menu: each core entry
  gets a row "Default config" (none / the core's configs) - the same `boot.cfg` value D in the overlay writes (D8).
- **D14 (Alex, 2026-11-10, Q12: "Oben"):** "Plain start" is the first row of the overlay, the configs follow (the mock-up shows it last and is
  to be updated); the cursor opens on the default config if there is one, else on "Plain start".
- **D15 (Alex, 2026-11-10: "yes ok, we do your proposal"; UX review 2 question):** the F5 Edit menu rows carry the same "N configs" badge as the boot
  menu rows, at the same place, so it is visible before LEFT/RIGHT whether a core has configs.
- **D16 (Alex, 2026-11-11, board photo of host 0.2.0+5cc93ede; translated: "The garbled text under AmigaVision, what is it for? The whole sub line is useless. I give the names myself as I want."):**
  a config row has no second line. The meta line (mounted media, "all settings") is dropped completely, with its file reading; a row is the name plus the `default` badge.
  "Plain start" is one line too (the panel keeps one row height; its meaning is in the sub text/foot keys). The row pitch shrank accordingly (36 px, 30 compact).
- **D17 (Alex, 2026-11-11, "I give the names myself as I want", translated):** adding a bundle that is already in the boot menu refreshes its hash, size and header, never its name (`bootcfg_add` used to overwrite the name with the .cor header's).

Everything below is **proposed**, not decided (see §6 for the open questions):

- **Proposed — a config is one file** `<core folder>/configs/<name>.cfg` in the text format of the core's `.cfg` (`cfgfile_*`): the
  `Mount <slot>=...` keys of the media set plus the core-layer settings. A T180 media set is a config without settings. One format, one
  reader (`settings.cc` + `mediamounts.cc`), no second parser in the host; the host only needs the file names.
- **Proposed — key to open the overlay: RIGHT**, with the row showing a "N configs" badge and the footer "RIGHT configs". Reasons: LEFT is
  already "back" (NAVIGATION.md: left arrow = back), so RIGHT is the symmetric "into"; cursor keys are a complete navigation path
  (concept); no letter is taken (R and V are in use in the boot menu). The issue also names C; as an alias it costs nothing but a second
  name for one action (R6), so it is a question (Q1).
- **Proposed — the overlay** is a modal list over the boot menu, not a new page: title "<core name> - start with", rows "<config name>" +
  one line each (D16: no meta line), the last row "Plain start" (what RETURN on the core row does), a `default` badge for the
  marked one; footer keys UP/DOWN pick, RETURN start, <- back, D default, R rename, DEL delete (twice). Opening it stops the countdown.
- **Proposed — the host is a client of `host_core` (T192.2):** `host_core` owns `config.list/start/rename/delete/default` as intents with
  stable ids; the overlay is a declarative page of `host_core_rows.cc`; the key map is a table row (view, key -> intent), the footer is
  generated from it. No key literal in the view.
- **Proposed — hand-over:** a tail field after `time_*` in `struct Handover` (outside `handover_sum()`, own check word, the tail rule of
  `id_sum` / `time_sum`), `config[48]` = the file name without `.cfg`. Old cores read nothing and start plain; old hosts leave it
  empty. `coreboot.cc` fills it from the start request; `handover_fill` stays source compatible.
- **Proposed — the core app applies the config at start** before it builds the menu or runs the core: read the file, put the `Mount`
  keys through `mediamounts` (D4 + D1 of T180 for each slot) and the settings through `Settings::set`, exactly the code "Load config..."
  uses in the running core (P5), minus the reset.
- **Proposed — default config:** one config per core may be marked default; it is what direct boot (T135) and the countdown start for that
  core (Q6).
- **Proposed — wording:** one word, "config", for the file, the save row ("Save as config..."), the load row ("Load config...") and the
  overlay; "media set" disappears from the UI (Q2).

## §4 Out

- No config rows in the boot menu list, no new boot menu entry type, no change of `boot.cfg` entries (D1).
- No config for the Gideon / Commodore C64 cores and their bitstreams (`trx64g`, `c64ref`; untouched by rule) and none for a standalone C64U
  without hand-over (no core folder).
- No sharing of configs between cores, no import/export, no config in a `.cor` or a release package (media paths are the user's).
- No new CI or test infrastructure; no media files copied into a config (it holds paths, as sets do).
- Writing configs from the host (no running core) — the host lists, starts, renames, deletes and marks; it does not create one.

## §5 Build notes

- **Mock-up review (2026-11-10, `ux-specialist` against R1-R9, rendered in Safari at 1000 px):** first pass **FAIL**, fixed in the mock-up, not
  re-run. Findings: (1, R2) the dim layer covered the overlay's key bar, now the key bar sits above it; (2) caption wrong about what stays
  bright, corrected; (3, R5/R6) the count "3 saved" sat on "Save as config...", the key name "RETURN" on "Load config...", now the count on
  Load and no value on Save; (4, R6) "+ Plain start" read as "add a config", the "+" is gone; the position text said "1-4 of 4" against a "3
  configs" badge, now "3 configs"; (5) the RIGHT hint was on the row and in the key bar, kept in the key bar only; (6) one key label style ("LEFT",
  not "<-"); (7) meta lines 11 -> 12 px; amber "N configs" badge read as a warning, now violet. Met: R1 (selection visible), R3, R4, R9;
  R7/R8 not checkable on a mock-up. Footer fits one line (about 55-60 % of the width). Not drawn or checked: delete/rename prompts, a list
  longer than the window (S4 covers R1/R5), the no-config row (Q7), 480p/1080p from the render bench, real LVGL fonts. Questions for Alex
  from the review: Q12 "Plain start" first or last in the overlay (mock: last, below a separator); Q13 overlay centred (mock) or anchored to its row.

**Build, 2026-11-10 (branch `t198-configs`), core side:**

- `handover.{h,cc}`: tail field `config_sum` + `config[48]` after `time_time` (D7), `handover_set_config` / `handover_config`; outside `handover_sum()`; the check mixes
  `sum` and `id_sum`, so a config left in the page by another core's start reads as "". `tests/coreapp/test_handover.cc`: 59 checks (old reader, old host block, every flipped
  bit of the tail, 32 characters fit, 33 refused, stale tail of another block).
- `configlist.{h,cc}` (host-app dir, shared): name rules (D11), what in `configs/` and `media/` is a config, order (case-insensitive), `configs/` hides a same-named `media/*.mnt`
  (D3), path builders. The host (listing, overlay) and the core app both use it, so S2 and S10 compare one function's output.
- `mediamounts.{h,cc}`: the media-set code is now the config code: `config_save` (full copy of the settings through `Settings::export_config`, D6, plus one `Mount` key per
  mounted slot whether Persist is on or not), `config_load` (write-back first, mounts, then `Settings::rebind` to the config file, all settings in force, one reset),
  `config_start` (the host's name; the settings are read from the config's file). D4/D5 follow from one mechanism: the run's `Settings` object writes to the config's file
  (`CoreCtx::settings_path`), the core's `.cfg` is never opened for writing; `persist_on()` is true during a config run. A media set that is only a `.mnt` is copied (verified)
  into `configs/` at its first start or load, the `.mnt` stays. Ops `config.save` / `config.load`, states `config.count` (cached: one read per generation, S15) and
  `config.active`; the old `media.save_set` / `media.load_set` / `media.sets` are gone (goldens regenerate). `corefolder_ensure` makes `configs/` (D10).
- Decisions I had to take where the spec was silent (say if wrong): a Reset-to-host in a config run is refused (it would delete the config's file); a legacy `.mnt` converted at
  first use has no settings, so those fall back to the host layer/defaults, not to the core's `.cfg`; "Save as config..." / "Load config..." are in the Summary tab of every core, not only
  of cores with media slots (a config also holds settings); the config's own `Persist medium mounts` value is ignored while a config runs; the load-while-running puts every
  setting in force with `boot_apply_settings(all)` before the one cold reset.
- S15 says "40 configs"; D11 (decided later) caps a core at 16: with 40 files in the folder the list shows the first 16 (sorted) and `ConfigList::more` counts the rest.
- Tests (`tests/coreapp/test_configs.cc`, 197 checks; `test_media_mounts*.cc` renamed to the config rows/ops, 182 + 1434 checks): S1 save/refuse/read-only/16 limit, S7 start with a
  config (first release already has the mounts, `.cfg` bytes identical, changes land in the config, no name = plain, unknown name = plain with a line), S8 missing medium
  (message names `/usb0/b.adf`, config file bytes identical), S10 list == host's list, load = one reset, S11 legacy, S14 no folder, S15 2 directory reads for 50 polls + 30 keys.

**Host side, 2026-11-10 (third pass):**

- Build fixes: the web start command's field `cfg` clashed with the `cfg` macro of hostui.cc (now `config`); `tests/hostapp/run.sh` gave test_cfgov no line-editor include path;
  `config.delete` is `HAF_DESTRUCTIVE | HAF_FLOW` (the overlay's own second DEL is the question, like the Cores page); the F1/F3 RIGHT key is `config.list` (test_hostcore updated).
- `tools/dev.sh riscv`: ok (host app and core apps link). Tests, this pass: `test_cfgov` 42 checks (S2, S3-S5 key map of the menu and the overlay, S9, S12 intent ids, S15, D11, D13
  cycle), `test_hostcore` 796 (the config reaches `core_set_start_config` from an entry, from the one-shot arm of the overlay / countdown, and is gone for the next start: S5, S6, D8),
  `test_hostapi` (every declared action reaches the model by intent, incl. the five `config.*`), `test_webui` 155.
- Not covered by a host-side unit test: the overlay's drawing (render pictures, below), RETURN through `host_core_ui` (no test links that file; the emu flow S6 and the board).

- Render bench (`tests/hostapp/render/run.sh`, run on the builder through a copy of dev.sh's `.dev-run.sh`): **2123 checks, 0 failed**. It already drives the real hostui with keys:
  S3 (RIGHT opens, cursor on the default, rows behind unchanged), S4 (UP/DOWN, LEFT/ESC close), S5 (RETURN plain; RIGHT on a core without configs does nothing), S6 (RETURN in the
  overlay hands the name over once, "Plain start" hands over ""), S9 (default set/clear, rename incl. clash, delete asks twice, legacy `.mnt`), D13 (F3 RIGHT on a `.cor`, F5 RIGHT/LEFT
  steps the default config). Fixes this pass: the scenario's cores are real bundles (a fake file is `damaged`, the start refuses it); the rename clash closes the prompt (stray ESC
  removed); the bench's "no folder listed at start" counter ignores a core's own `configs/` and `media/` (S15's bounded read); the Browse footer lost "to menu"/"once" to make room for
  "RIGHT configs" (144 -> 131 characters, no footer overflows); the badge's room is taken from the name/path lines only on a row that shows it; no config folder is read before an
  entry's state is read (T101 S10); area budgets: F5 RIGHT 19000 px (the note line), F3 selection move +19000 (that note is cleared by the next key), F5 switch +1000.
- Goldens changed: `tests/hostapp/render/golden/hostrows.txt` (two footers, the new `defcfg|` line of the F5 page). Nothing else.
- Pictures: `/tmp/t198png/t198_{menu_badges,overlay_menu,edit_defcfg}_{480,1080}.png`, `t198_overlay_{browse,delete,renamed}_1080.png`. Findings from looking at them: the "Plain start" sub text
  is cut ("as RETURN on the ..."); in the F5 header the "Default config" value sits further right than the other three values.
- `tools/dev.sh test` ok, `tools/dev.sh riscv` ok (host and core apps).

**UX review fixes, 2026-11-10 (host side, after the first review of the pictures):**

- Overlay (`oc_*` panel in `hostui.cc`, drawn from `HcWifiPick.cfgov`): one-line rows (name; D16), the page behind dimmed (72 % `0x05080e`, the mock-up's value) down to
  the key bar, which shows the overlay's keys and stays bright; the in-box key line is gone; one verb, "pick" (the footer's `%/% pick`, the mock-up's word); the box is as high as its
  rows (5 rows minimum, 8 and then scroll); the `default` mark is the boot menu row's badge (filled, same size); the WiFi lists keep their panel unchanged.
- Meta line: superseded by D16 (rows are one line; the text of this paragraph, the reading of the files and `hc_cfgov_meta_of` are gone).
- Browse overlay (F3 on a .cor): the sub text names the file, "amiga.cor - 5 configs" (`HcCfgOv.browse`); from the boot menu it stays "N configs" as the mock-up shows.
- "RIGHT configs" is in the key bar only while the selected row (F1) or file (F3) has configs (`HCF_MENU_PLAIN`, `HCF_BROWSE_PLAIN`, `hc_menu_foot()`; D9: no hint there). The
  "N configs" badge stays on every row that has configs: the mock-up picture shows it on the unselected C64 row, its caption ("show only on the selected row") is about the
  hint; the picture was taken as the rule.
- Badge order of a boot menu row as the mock-up: default, N configs, status (the default badge moves left by the width of the N-configs badge when that shows).
- F5: "Default config" value at the same x as the other values; the core row label is "Default core" (no test outside T198 reads the label; `decision Default core:` in
  `emu_t135_direct.sh` is a log line of the direct boot, not this label); key hint "LEFT/RIGHT config of this core".
- Cost: the dimmer is one full-width blend (about 100 ms on the board, see the note at the start panel) and is drawn when the overlay opens, not per key; not measured on the board.
- Goldens: `tests/hostapp/render/golden/hostrows.txt` (the F5 footer). `render.cc` footer expectations of F1/F3/F5 follow the new words.
- Pictures: `/tmp/t198png/t198_*` regenerated (480p and 1080p, same names). `tools/dev.sh test` ok, `riscv` ok, render bench 2123 checks 0 failed.

- 2026-11-10 (UX review 2 of 2, fixes): (1) the panel height already followed the rows in `oc_fill` (5..8 rows, then scroll with "a-b of n" above 8 rows); now measured: `hostui_test_oc_geom`
  reads the panel, the selected row, the last drawn row and the message from the LVGL objects and the bench asserts selected row and message lie inside the panel, the message below the
  last row and clear of the selection (6 configs, 10 configs on the last row = scrolled, a rename refusal and a DEL ask over the full list). (2) On "Plain start" the key bar is
  "UP/DOWN pick  RETURN start  LEFT back" (no D / R / DEL). (3) The "default" badge text is centred in its box (boot menu row and overlay; F5 has no such badge). (5) (meta line separator: gone with D16.)

- 2026-11-11 (after the main merge, T192.3 D31 / T196 / T199): `coreapp:cross` was red on the renamed words only. Goldens regenerated in the build container
  (`CA_GROUP=cross UPDATE_GOLDEN=1 tests/coreapp/run.sh`): `ids.txt`, `keywalk.txt` and 14 `menu_*.txt` (media set -> config words, hashes follow); `ops_amiga1200.txt` edited by hand to
  the same three changes `ops_trx64u.txt` already had (config.save / config.load name 32, state config.count / config.active). `tools/dev.sh test` ok, `riscv` ok.
  Render bench: the "Plain start" key bar was dead code (the overlay overwrote the pick's keys with the F_CFG foot); now `HCF_CFG_PLAIN` in the foot table, assert follows ("start", "twice deletes").
  Render bench 2149 checks, 0 failed. Pictures `/tmp/t198png/t198_*.png` (6 configs, 10 configs last row scrolled "5-12 of 12", DEL ask message) opened: rows and message inside the panel.

- 2026-11-11 (D15, emulator flow, S13): (1) D15: `hc_row_entry` no longer limits `configs` to the boot menu style, so the F5 rows draw the same badge (same `fill_row`, same place); the
  object dump of the host page got a line `cfgbadge <row>|<text>` per visible badge. Render bench: with the F5 page open, `cfgbadge 0|4 configs`, `cfgbadge 2|1 config`, no badge on the row of a
  core without configs; `t198_edit_badges_480/1080.png`. `hostrows.txt` is unchanged on purpose: its tree has no config folders, so no badge shows in any row of the golden (the asserts above cover D15).
  Render bench 2153 checks, 0 failed; `tools/dev.sh integrate` ok (all controls, render, riscv, emu, tests/ui).
  (2) Emulator flow `tests/hostapp/emu_host.sh` (runs in `dev.sh emu`, PNGs come back to `.ci/dev/<branch>/emu/cfg-*.png`): stick with `vc20.cor` (3 configs: Alpha, Bravo, Charlie) and `vc20x.cor`
  (10: Config 01..10); F3 RIGHT on the .cor (overlay), A adds both, F1 (badges "3 configs" / "10 configs"), RIGHT (overlay of 3), D on Bravo, F5 (badges + "Default config  Bravo"), F1, the
  10-config overlay scrolled to its last row ("4-11 of 11"), RETURN on Charlie: the core application prints `[core] config Charlie: /USB0/cores/vc20/configs/Charlie.cfg` (S6: the hand-over page
  carried the name; the flow asserts that line). Result: `PASS T198 config overlay flow`. Limit: only the VIC-20 bundle exists in the kit as a DFX core, and it has no mount slots, so "the core
  shows the config's mounts" (S7) is not shown here (the A1200 mounts are proven by the coreapp bench and the board). (3) S13: `tests/hostapp/emu/cfg_web.py` (local emulator with a host port forward and the
  control port; the builder's container has no network, so it is not part of `dev.sh emu`; run by hand with `--img <dir with hostapp_emu.elf, coreapp.elf, coreapp.app>`): `GET /v1/status` entry has
  `configs: 3`, `POST /v1/boot index=0&config=Bravo` starts the core with the config (`[core] config Bravo:` line). **Bug found by it and fixed:** the web start cleared the armed config right after
  `web_apply`, but the intent only queues keys (`hostapi_pass`), so the core always started plain; the name was kept armed with a 5 s deadline (superseded by the path-bound arming of 2026-11-11 below). Before the fix the same script FAILED (no `[core] config` line), after: `PASS S13 GET /v1/status`, `PASS S13 POST /v1/boot`.
- **Overlay title (board 11-11, "Amiga 12 - start with" over the row "Amiga 1200 (AGA, 68020)"):** the render bench (1080 and 480) shows the whole name and `hc_cfgov_pick` returns it, so the cut is not reproduced; the title is now not capped at 28 characters (`HcWifiPick.title` 72, gcc truncation warning gone), a name that does not fit is cut with "..." before the kept " - start with" (`oc_fill`), and the bench checks the label's text and width (`hostui_test_oc_title`, `t198_overlay_longname_1080.png`).

(Open for this branch: board; the two picture findings above; S7 mounts shown by a core with slots in the emulator (needs an Amiga/other DFX .cor in the kit).)

## §6 Open questions (one at a time, in this order; none decided)

- ~~Q1~~ decided: D2.
- ~~Q2~~ decided: D3.
- ~~Q3~~ decided: D4.
- ~~Q14~~ decided: D5.
- ~~Q4~~ decided: D6.
- ~~Q5~~ decided: D7.
- ~~Q6~~ decided: D8.
- ~~Q7~~ decided: D9.
- ~~Q8~~ decided: D10.
- ~~Q9~~ decided: D11.
- ~~Q10~~ decided: D12.
- ~~Q12~~ decided: D14.
- **Q13** (not asked; the mock-up's centred overlay stands unless Alex says otherwise) Overlay centred or anchored to its row?
- ~~Q11~~ decided: D13.

---
<!--
Closing: Status -> DONE (date) + which scenarios are green where; the TODO row gets a check mark and a history entry, in the same commit as the code.
-->
- **UX review on ue2emu pictures (2026-11-11): PASS** (R1-R6, R9; R7 unverified: ue2emu shows the 960x540 canvas unscaled in the top-left quarter, R8 not timed). Open, low: the overlay title shows a wide gap before " - start with" in ue2emu (`cfg-browse-overlay.png`), not in the render bench; trimming the entry name did not change it, so the likely cause is the board font's space width (not measured). Board step S17 open.

**2026-11-11 pre-push review fixes.** (1) Web start: the armed name no longer lives 5 s globally. `web_pump` hands `hc_ui_web_config(path, name)` over (set on every web start, so a stale one is
overwritten); `HCA_WEB_START` arms it with `hc_start_config_set` only when the focused entry's path equals the bound path, around the synchronous `start_any` (the question included), and clears it when
`start_any` returns (started, refused, answered no or answered late: the question blocks, so there is no deadline and no silent drop). The `web_cfg_until` deadline is gone. (2) `config=` is checked in
`web_parse` with `config_name_why` (400 with `{"error":400,"reason":...}`, no cut to 32 characters; an empty value = plain) and against the entry's list in `web_pump` (`hc_cfgov_exists`, answer
"that entry has no config of that name"). (3) `config_save` over an existing config writes `<name>.tmp`, renames old -> `.bak`, new -> name, removes `.bak` (FatFs cannot rename over a file; a failed step
restores the old one and removes the temp); test in `test_configs.cc` (`mut_budget=1`: old config untouched; a successful save leaves no .tmp/.bak), controls `cfg_save_inplace`, `cfg_save_no_swap`, `cfg_load_rebind`
in `tests/coreapp/run.sh`. (4) Contract text: page tail (time fields, `config_sum`, `config[48]`) and "older readers ignore the tail" added to `docs/contracts/trx64u-firmware-handover.md`.
**Open:** the TRX64-Ultimate copy of that contract needs the same text (not edited from here; change only with both sides). Nit: delete / rename of a configs/ file now also removes the legacy
`media/<name>.mnt` of the old name so it does not reappear.

- **Config save, power loss (2026-11-11, review):** the swap (rename X.cfg -> X.bak, rename X.tmp -> X.cfg, remove X.bak) hid the config after a power loss between the renames (only X.bak + X.tmp) and a stale X.bak made every later save fail. Now `config_save` removes a stale `.bak` before the swap and says so when the restore rename fails; `configlist_fix_plan` (configlist.cc, pure) names the recovery: X.bak without X.cfg is renamed back, a stale X.bak beside X.cfg and every X.tmp are removed. Applied by the core app in `list_dir` (configs/ only, no extra read unless something changed) and by the host in `read_list` (FoFs has rename/remove). Tests: `test_configs.cc` (power loss, stale .bak, failed restore), `test_cfgov.cc`; negative control `cfg_no_recovery` in `tests/coreapp/run.sh`.
