# T205 — Every core has its settings in one place: F5 (core settings first, then Video, Audio, Input)

**Status:** PROPOSED (2026-11-11).
**Repo:** TRX64U-MultiCore — the generic core app (`coreapp/src/menutree.*`, the area/tab tables), so every core (T192.3 D31: the menu is in every core).
**Number:** T205.
**Origin:** Alex, 2026-11-11, at the board in the VC-20 core (photos of F5, F6, F8), translated: "what kind of UX mess is that? 3 times settings ...
that makes no sense." On the proposal: "Yes, that makes sense. Though F5 = CORE specific settings must be. Because the menu is now in EVERY core, right?"
**Depends on:** T187 (core app UX rework, merged), T192.3 D31.
**Milestone:** M2.

---

## §1 The problem

Today (host app 0.2.0+5cc93ede, VC-20 core):
- P1: settings live in three areas: F5 "<core>" (tabs SUMMARY, SETTINGS: machine and storage), F6 SETTINGS (tabs VIDEO, AUDIO, INPUT), F8 SYSTEM
  (actions, but also settings: "Menu opens at", WiFi).
- P2: F5's first tab SUMMARY is a second Home.
- P3: a drawing fault: in F5 SETTINGS the value column stood one row below its names on one photo (JiffyDOS row, "On" one row lower), half a row on
  another.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | F5 is named after the core; its tabs: the core's own settings (first, named after the core), Video, Audio, Input; every core | core app test over every descriptor + keywalk golden | apptests |
| S2 | P1 | F6 is gone from the rail and the keys; F8 holds only actions (Reload core, Return to host menu, Bug report) and WiFi; "Menu opens at" sits in F5's core tab | core app test | apptests |
| S3 | P2 | SUMMARY is gone from F5; what it showed is on F1 Home | core app test | apptests |
| S4 | render bench `ui_list_probe` 0 failed (before the fixes); in the emulator no picture shows a value off its name's baseline (ux-review, 28 pictures). Alex's "value one row low" (P3) is NOT reproduced; one real fault found and fixed on the way: `trx_row_retext` returned at once for a heading row, so a heading in the same slot of a window that moved by pages kept the old group's text (`trxview.cc`, now rewritten in place). Not claimed to be the board's cause |
| S5 | emulator pictures (real coreapp image, tests/ui features `features_cores/*_shots`, `features_a1200/shots|longname`): VC-20, C128, Atari, A1200 F1/F5 (core, Video, Audio, Input, Hotkeys)/F8 + 62-char name in DF0; ux-review PASS (minor findings below). No 480p picture: ue2emu gives a 1920x1080 frame with the canvas in its top-left quarter whatever `video=` says (emulator finding) |
| S6 | All | board: VC-20 F5 tabs, F8, values aligned | by hand | board (Alex) |

## §3 Decision

- **D1 (Alex, 2026-11-11):** one settings area per core: F5, first tab the core-specific settings, then Video, Audio, Input; F6 goes; F8 is
  system actions only; SUMMARY moves to Home.
- **D2 (Alex, 2026-11-11, "T205 comes before, so into the release", translated):** T205 goes into the next test release.

## §4 Out

- **Open, Alex to decide:** the TRX64U core's own F6 (`coreapp/cores/trx64u/menu.toml`, a description-written route) is untouched by T205; it still has its F6 area.

## §5 Build notes

Built on `t205-settings` (2026-11-11). Default route: F5 = core tab (id `settings`, named after the core), Video, Audio, Input; F6 gone; Summary gone, its
media/config rows are on Home (session panel); "Menu opens at" in the core tab. A route tab may have no name (named after its area): `descrec.cc` and
`tools/descrec.py` agree. Contributed tabs without a number are numbered by place among the tabs shown (empty ones are dropped).

| # | Evidence |
|---|---|
| S1, S2, S3 | `tools/dev.sh test` rc=0, all groups incl. amiga; `test_contrib`, `test_configs`, `test_uiapp` updated; keywalk/ids/menu goldens regenerated (grep: F6 and F5/summary gone, `F1/home` gains media + config rows) |
| S4 | render bench `ui_list_probe` (row y/height/name/value per drawn row, 5166 selection states, 18 cores, 480p/720p/1080p): 0 failed. It does not reproduce Alex's "value one row low" (P3): the cause on the board is NOT explained by the bench; in-place path `trx_row_retext` read once, nothing found |
| S5 | pictures `ux-{a1200,vc20,c128,atari}-F1/F5/F8-*` (render bench, 1080p + 480p) exist; ux-review verdict FAIL, see below |
| S6 | Alex at the board, open |

ux-review round 1 FAIL (render-bench pictures), fixed: long Home values are cut in the middle (`fit_value`, label keeps up to 45% of the row); the stale F5 label
came from `static_key` hashing the route array's pointer but not its titles (a bench process rendering many cores reused one array; the product starts one
core per process, so it could only hit if a route changed in place) - now area keys, titles and tab titles are in the key; the scroll counter counts settings.
Round 2 (emulator pictures, T207 merged): PASS. Open, minor (not decided): Core row shows internal ids "(VC20)", Application "built"; hotkey key bar says
"<- -> CHANGE" on every row; F8 WiFi "No module" wording; ".." instead of "..." in the model's own name cut; "40/80 DISPLAY" case. Alex's: "Joystick ports"
placement, Drive rows under MACHINE, one-tab strip in F8, TRX64U core's own F6, T207 list behind the prompt.

Render bench (2026-11-11): 2385 checks, 0 failed. The T207 hotkey block failed on `hot.m->learning()` because the bench took the page's first row as the Reset
row; since T205 the Hotkeys page starts with the modifier row, so RETURN asked for a modifier, not the key (bench wrong, product right: `menuctl.cc`
`EK_SETTING` -> `hk_learn_item` -> `begin_learn`). The bench now finds the Reset row by its name. The Amiga 1200 UI features (`tests/ui/features_a1200/`) followed the
new layout (F5 tabs A1200/Video/Audio/Input, drive rows on Home, "Menu opens at" in the F5 core tab); `tests/ui/run.sh` 110 passed, 0 failed.
