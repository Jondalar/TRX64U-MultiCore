# T153 — The UI is an SDK with three written contracts and a BDD test suite that runs it in ue2emu

**Status:** PROPOSED (2026-10-29); D4-D6 built and run the same day (§5), the contract lint (D7.1) and the host app are not. Status check 2026-11-13: BUILT and merged into main 2026-10-29 (94f31c833): D4-D6, Q1-Q7 all answered (§3a); contract lint D7.1 (tools/check_ui_contract.py absent) and host-app migration not built.
**Repo:** TRX64U-MultiCore — widget layer `app/view/` (shared by both applications), layout templates and controller `coreapp/src/`, content `coreapp/cores/<core>/menu.toml` + `tools/descrec.py`, tests `tests/ui/`.
**Number:** T153 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-29 (translated), after the new TRX64U menu in ue2emu ("very well done, looks consistent"): "Can you send a second agent that prepares BDD tests for the UI via the emulator? It should now be: Generic UI SDK, API and ABI clearly described, then a test suite SDK. And with that we then build the host app, switch TRX64 to it, and in perspective the other cores too."
**Depends on:** T104.2 (the declarative UI, branch `t104-2-board`), T104.1 (contribution model: ops and states), T104 (generic core app). Related: T131 (API and SDK for outside developers — this spec is its UI half).
**Milestone:** M3.

---

## §1 The problem

The UI of the generic core application (T104.2) works and is consistent, but it exists only as code and as screenshots someone looked at.

- **Who:** Alex, who reviews the UI by eye in ue2emu; the next sessions that move the host app (T101), the TRX64 line and the other cores onto the same UI; later outside developers (T131).
- **What they want:** build a menu for a new core (or the host) by writing a `menu.toml` and nothing else, and know at once when a change broke a page, a key or a letter.
- **What happens today** (read from the code, not from comments):
  - The widget layer `app/view/trxview.h` is a C++ API of ~60 functions linked into each application (coreapp and hostapp). It has no document; the contract is spread over comments that name prototypes (`lvgl_ui_project/*.xml`) which are not loaded at run time.
  - The layout templates are the functions `build_desk`, `build_split`, `build_status`, `build_library`, `build_sheet`, `build_timeline`, `build_terminal` in `coreapp/src/screens.cc`. Their slot names (`head`, `main`, `mainfoot`, `side`, `sidefoot`, `bar`, `media`, `machine`) are string literals in that file; the compile-time checker (`tools/descrec.py`, `LAYOUTS`) accepts eight layout names, the renderer knows five behaviours.
  - The content schema is the TOML subset `tools/descrec.py` compiles into the menu record (`.cor` record 6; magic `TRXD`, version 3). Its keys are checked at pack time, but their *meaning* is in `coreapp/src/menuctl.cc`, `screens.cc`, `contrib.cc` — a key can pass the checker and mean nothing (findings F1-F5 below).
  - There is no automated test that a key does what the spec says. The only checks are the pack-time ones and `tests/coreapp/` (model benches without the UI). A page regression is found when Alex looks.
- **Why it matters:** the host app and four more cores will be put on this UI. Without a written contract and a regression suite every move re-opens decisions (which letter wins, what Left does on a button) and the first change to `menuctl.cc` can silently break all of them.

**Findings from reading the code (these are facts of the repository on `t104-2-board` HEAD 566b815ed):**

- **F1 — `apply = "settings.noop"` on 62 of 187 `core_setting`s.** The operation is a registered no-op (`settings_ops.cc` `op_noop`): the value is stored in the `.cfg` and read where it is used. Each of the 62 was looked up in the source (`coreapp/src`, `coreapp/cores`, `app`, `host-app/software`):
  - 58 are read: `Recent 1..8` and `Favorite 1..16` (`menuctl.cc` builds the key with `"%s %d"`, `libfiles.cc`), `FTP 1..4 Alias/Host/Port/User/Password/Folder` (`netfs.cc:244`, key built as `"FTP %d %s"`), `Wi-Fi SSID`, `Wi-Fi Password`, `Stream VIC/Audio/Debug to`, `Debug Stream Mode` (`netops.cc`), `REU Preload Image`, `REU Preload Offset` (`trx64u_ops.cc`), `Home Directory`, `Enter Home on Startup` (`menuctl.cc`, `libfiles.cc`).
  - **4 were read nowhere: `Splash`, `Save Changed Cartridge`, `Temp Auto Cleanup`, `Temp Subfolders`.** Since (Alex, 2026-10-29): `Splash` is read at start (`run.cc`: Off closes the start screen once the settings are known; the screen itself opens before the settings can be read, so it shows for the first steps), the two `Temp` rows are gone from `menu.toml` (the behaviour is built in, always on), `Save Changed Cartridge` stays as it is (being clarified). The menu shows and stores them; nothing acts on them. (`STATUS.md` of the UI build already says the second one is "stored" only.) They are dead rows until an op reads them, or they are hidden (Q4).
  - The checker cannot tell the 52 from the 4: `settings.noop` passes `check_ops`. A noop that is read through a composed key cannot be found by any grep for the literal key either. → Q4.
- **F2 — layout names the checker accepts but the renderer does not distinguish.** `LAYOUTS = home, desk, split, settings, status, library, timeline, terminal`. `screens_render` branches on `desk`, `library` (+ the sheet while the chooser is open), `status`, `timeline`, `terminal`; everything else, **including `home` and `settings`**, runs `build_split`. `screens.h` documents `home` as a separate screen; it is not. → Q3.
- **F3 — tab `ui` keys that are read by nothing.** `UI_TAB_KEYS` lists `left`, `label`, `frame`, `bar`; they appear only in the *signature hash* of `screens.cc` (`more[]`), never as a value that is drawn. `nodes_first` is read in `menutree.cc` only. A typo-free but meaningless key passes the pack check.
- **F4 — node kinds the checker lets through that the loader refuses.** `kind = "setting"` and `kind = "widget"` compile (`NODE_KINDS`), but the merge refuses them (descrec.py header). The compile step does not say so.
- **F5 — the letter rules are checked in two places with two different scopes.** `descrec.py` checks per page that no two `[[node]]` claim the same `key`/`bkey` letter and that it is one upper-case letter; the *core-wide* meaning of `key` (a hotkey that acts on every page where the page does not claim the letter) is resolved at run time in `MenuCtl::hotkey` (page pass first, then the whole tree). Two `key` letters on *different* pages are legal and the first in tree order wins on a third page. Sheet letters of `[[filetype.action]].key` are a separate namespace (also checked, `sheet_keys`). Nothing checks `key` against a letter the *page* claims by `bkey` on a page other than the one it is declared on — that is the rule P2 relies on and it is not tested anywhere.
- **F6 — the footer, the status line and the confirm question are three different channels** (`footer_legend`, `status()`, `confirm_q_` shown as the row of the flat list). A test cannot read them from pixels; the controller has no function that returns "the whole screen as data".

- **F7 — the pack tool's own reader rejects what its compiler writes.** `compile_desc` packs the sheet letter of a `[[filetype.action]]` into bits 15:8 of the action's flags; `parse_desc` (`tools/descrec.py`, the reader `check_ops` uses) tests `fl & ~1` and raises "a bad file type action". `tools/gen_menu_rec.py` (and `--check`, the lint stage's comparison of `menu_rec.inc` with the TOML) is therefore red on `t104-2-board` 566b815ed for `menu.toml` itself; the C++ reader (`descrec.cc:363`) reads the letter. Found while preparing a mutation control for the suite; the fix is one expression (`fl & ~0xFF01`), not made here.
- **B1 — Discard leaves the unsaved mark.** `settings.discard` calls `Settings::load()`, which re-reads the `.cfg` and the values come back, but `dirty_` is not cleared there (`settings.cc`: only the constructor and `write_cfg` clear it). After Discard the top bar still shows UNSAVED and Discard and Save stay enabled. The suite's scenario "after Discard nothing is unsaved any more" fails on it; the fix is `dirty_ = false;` in `load()` (not made here).

**Parts (MECE; §2 is checked against this list):**

- **P1 — The widget layer has no written API.** What a caller may pass, what it returns, what is cached.
- **P2 — The layout templates have no written slot contract.** Which slot a node can name, what each template draws from it, what happens to a node in a slot the template does not have.
- **P3 — The content schema (TOML) and its ABI to C++ are only half checked.** Fields, defaults, pack-time checks, the op/state names and argument types, the record format and its version (F1-F5).
- **P4 — The behaviour (keys, focus, letters, dialogs) is nowhere executable.** No test can fail when it changes.
- **P5 — The tests cannot see the UI.** There is no read-only way to ask the running application what is on the screen, except a screenshot.
- **P6 — The next consumers are not planned.** Host app, TRX64, other cores need a sequence that does not touch the C64 menu while it is being proven.

---

## §2 Scenarios — what proves it is solved

Run by `tests/ui/run.sh` (T153 D5): ue2emu headless with the TRX64U coreapp, `.feature` files in `tests/ui/features/`. Every step type has a negative control (`tests/ui/controls/`) that has to fail; `run.sh --controls` runs them and fails if one passes.

```gherkin
Scenario: S1 area keys go to their areas                                   # P4
  Given the menu is open
  When  I press F6
  Then  the area is "Settings" and the page title is "TRX64 behavior"
  And   I press F3 and the page title is "Library"

Scenario: S2 digits are tabs of the area, left arrow is one step out       # P4
  Given the menu is open on F6
  When  I press 3
  Then  the tab is "Audio"
  When  I press left-arrow
  Then  the page is the first tab; a second left-arrow goes to F1; on F1 it closes the menu

Scenario: S3 RUN/STOP closes from anywhere                                 # P4
  Given the menu is open on a settings page with a sheet or a question up
  When  I press RUN/STOP
  Then  the menu is closed

Scenario: S4 a setting changes with Right and the Save/Discard letters follow    # P4
  Given Auto Save Config is Off and F6/1 is open
  When  I change a setting with Right
  Then  the top bar shows UNSAVED and the buttons Discard and Save are enabled
  When  I press D
  Then  the setting has its saved value again and the buttons are greyed
  And   (negative) pressing S with nothing unsaved leaves the state and shows the button's reason

Scenario: S5 Defaults asks first                                            # P4
  When  I press F on a settings page
  Then  a question is open ("Set every setting of this core to its default?"); left-arrow cancels it and changes nothing

Scenario: S6 a page letter beats a core hotkey, a hotkey acts where the page does not claim it   # P4 (F5)
  Given the page F5/1 claims E (Eject disk) by bkey and the core has P (Pause) by key
  When  I press E on F5/1 then P on F5/1 then R on F5/1
  Then  E runs the page button, P runs the core hotkey, R (page: Reset drive; core: Reset) runs the PAGE's button

Scenario: S7 the Library opens, shows a sheet, marks files                 # P4
  Given the menu is open on F3/1 with a folder that has a disk image
  When  I move to the image and press Right
  Then  the sheet of the file is open and lists its items with their letters
  When  I press left-arrow then SPACE on a file
  Then  the file is marked and the cursor is on the next row

Scenario: S8 the Home desk lists the media rows                            # P4
  Given the menu is open on F1
  Then  the layout is "desk" with the rows Disk, Cartridge, Program in slot media and the machine rows in slot machine

Scenario: S9 the pack check refuses two elements with one letter           # P3
  Given a menu.toml in which two nodes of one page claim bkey "S"
  When  I run tools/descrec.py compile on it
  Then  it exits non-zero with "the letter S is claimed by"

Scenario: S10 the state query tells the truth                              # P5
  Given the menu is open
  Then  GET /v1/ui/state returns the area, tab, layout, items, footer and dialog that S1-S8 assert on
  And   (negative) a step that asserts a wrong value fails with the expected and the actual value, and a screenshot is attached

Scenario: S11 the contract documents match the code                        # P1, P2, P3
  Then  every slot name, ui key and layout name in §3 D1-D3 appears in the table the checker uses; a name in the document that the checker does not know fails the lint
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1-S3 | P4 | navigation | `tests/ui/features/navigation.feature` | `tests/ui/run.sh` (ue2emu on the Mac) |
| S4-S5 | P4 | settings, bar letters, Defaults | `tests/ui/features/settings.feature` | same |
| S6 | P4 | letter context rule | `tests/ui/features/letters.feature` | same |
| S7 | P4 | Library | `tests/ui/features/library.feature` | same |
| S8 | P4 | Home desk | `tests/ui/features/home.feature` | same |
| S9 | P3 | pack-time letter check | `tests/ui/features/pack.feature` (no emulator) | same, and `tools/test_descrec.py` |
| S10 | P5 | state query + step controls | `tests/ui/controls/*.feature`, `run.sh --controls` | same |
| S11 | P1-P3 | contract lint | `tools/check_ui_contract.py` (T153 D7, later step) | CI lint stage (later) |
| S12 | P6 | host app on the SDK | — (a later spec) | — |

Rules: a step that asserts must be able to fail (§3 D6); a feature file does not compare pixels; a screenshot is only an attachment of a failed step.

---

## §3 Decision

The SDK is three layers with one contract each; the test SDK is a fourth thing that reads the running UI as data.

### D1 — Layer (a), the widget layer: `app/view/trxview.{h,cc}` (API, source level)

A C++ API on top of LVGL 9, linked into each application. **No ABI**: nothing outside the repository calls it (the app ABI of T131 is the `svc` table; the UI is not in it, Q1). Canvas 960 x 540 (`SW`, `SH`), two skins (`TRX_SKIN_PANEL`, `TRX_SKIN_TERMINAL`), seven colour schemes for the panel skin.

| Group | Functions | Contract |
|---|---|---|
| shell | `trx_shell(scr, info, route, area, tab)` -> content pane; `trx_shell_same`, `trx_shell_forget`, `trx_content_w/h` | Draws rail (148 px), top bar (36), tab strip (38), footer (28). Rebuilt only when area, tab, a chip or the activity changed (`trx_shell_frames()` counts rebuilds, `trx_shell_partials()` partial redraws). The pane origin is (0,0). A caller that cleans the screen itself must call `trx_shell_forget()`. |
| route | `TrxRoute{areas[<=8]}`, `TrxArea{key "F1".."F8", title, tabs[<=6], shifted}`, `TrxTab{n 1..5 printed, title, view, arg}` | Pure data. `shifted` puts the area under the SHIFTED heading. |
| rows | `TrxRow`, `trx_row/_at/_select/_retext`, `trx_status_row`, `trx_setting_row`, `trx_kv_row`, `trx_action_row` | A focusable widget is built with four selection strips as its first children; `trx_row_select(obj, on)` moves the focus without building anything. `trx_row_retext` returns false when the structure differs (then build again). |
| controls | `trx_btn(style TB_SECONDARY/PRIMARY/WARN, selected, disabled)`, `trx_link`, `trx_chip`, `trx_state_chip`, `trx_source_chips(flags: bit0 absent, bit1 focus)` | Same focus rule as rows. `disabled` is the greyed button. |
| containers | `trx_card(raised)`, `trx_panel`, `trx_head(crumb,title,chip)`, `trx_kicker`, `trx_live_box` | `trx_live_box` is filled with the colour key `0x0841` (RGB565) so the core's picture shows through. |
| window | `trx_window(total, sel, first, rows)` -> `{first,count,total}` | The list scroll rule: the selection stays inside; stateless. |
| text | `trx_txt`, `trx_text_w`, fonts from `TrxTheme` | Montserrat (panel) / UNSCII (terminal); no font binary is added. |
| splash | `trx_splash_open/_step/_close/_up` | Start-up only. |

Findings to fix as part of the document (not by this spec's code): the header comment of `trxview.h` still points at the two prototype directories as if they were loaded; the code is the contract (the prototype is a design reference).

### D2 — Layer (b), the layout templates: `coreapp/src/screens.{h,cc}` (slot contract)

A tab names a layout in its `layout` field (`name[;key=value;flag...]` in the record; `ui_name()` is the first word). A node names its slot by the **group** of its anchor: `anchor = "F6/trx64/bar"` -> page `F6/trx64`, slot `bar`. A node with no group is in slot `main`. `in_slot()` is the only place that matches; **a node in a slot the template has is drawn; in a slot it has not, the node exists (it takes cursor focus and its letter works) but is not drawn.** This last sentence is today's behaviour, not a designed one (Q2).

| Layout | Drawn by | Slots (kind drawn) | Tab `ui` keys read | Fixed texts |
|---|---|---|---|---|
| `desk` (Home) | `build_desk` | `head` (buttons), `media` (rows), `machine` (rows) | — (the title is the tab name) | kickers MEDIA, MACHINE |
| `split` | `build_split` | `head` (buttons), `main` (rows), `mainfoot` (buttons), `side` (values/buttons), `sidefoot` (a link), `bar` (buttons) | `title`, `crumb`, `chip`, `side` (side panel kicker) | crumb defaults to `AREA / TAB` |
| `settings` | `build_split` (the same code as `split`; F2) | the same | the same | — |
| `home` | `build_split` (F2) | the same | the same | — |
| `status` | `build_status` | `head`, `main` | `title`, `crumb`, `chip` | the first information line in the large font |
| `library` | `build_library` (+ `build_sheet` while a file's action list is open) | none: rows come from the browser | `title`, `sheet` (`Label|@state` for the Drive A style sheet line), `sheetx`, `marks` (`A=state,C=state,P=state`: the badges) | SELECTED panel |
| `timeline` | `build_timeline` (the pixel layer's own screen, no frame) | none | `pos`, `info`, `glyph`, `track`, `hint` | — |
| `terminal` | `build_terminal` (trxmon's 80 x 40, every key is the monitor's) | none | — | — |

Item kinds the controller hands to a template (`MenuCtl::ItemKind`): `IK_BUTTON` (op node), `IK_ROW` (state node with `chip` or `off`), `IK_VALUE` (plain state node), `IK_HEADING` (group node), `IK_SETTING` (a setting; switch when it has exactly two values that read off/on (Disabled/Enabled, Off/On, No/Yes, False/True; `is_switch`)), `IK_MEDIA`, `IK_TASK`, `IK_INFO`, `IK_FIXED`. A `hide`/`hidden` item is not drawn and takes no focus.

Controller contract (`menuctl.cc`; this is the behaviour the suite fixes):
- Area keys `F1`-`F8` go to the area (an area the core has not: nothing); a second press of the same key stays; a different area returns to the tab it was left on.
- Digits `1`-`8` select the tab by its printed number (nothing if none).
- Up/Down/PageUp/PageDown/Home/End move the focus over the focusable items of the page; the focus starts on the first focusable item (`settle`).
- Left: in a question or a sheet it cancels/closes; on a setting with choices or a number it changes the value down (a switch changes on RETURN and SPACE only: Left and Right do not touch it, and Left on it is back one level); in the browser it goes up a folder; on a button of a screen it is **back one level** (`KEY_ESCAPE`): the first tab of the area, then F1, then (on F1) close. Right: on such a setting it changes the value up; in the browser it runs the file's first action; on a button it moves down.
- RUN/STOP, F10, ScrLock, Menu close from anywhere except inside the monitor; DEL only deletes inside a text.
- Letters: a page letter (`bkey` of a button, `key` of a row) acts only on its page and beats a core hotkey (`key` of any page) of the same letter; a core hotkey acts on any page that does not claim the letter. Inside a sheet the sheet letters of the action list act; in a prompt/question no letter acts. A greyed element shows its `why`/`off` text in the status line instead of running.
- A question (`ui.confirm`) takes the screen as a flat row; RETURN confirms, Left cancels, nothing else acts.

### D3 — Layer (c), the content schema and the ABI to C++

The TOML schema is what `tools/descrec.py` accepts (`compile_desc`); an unknown key anywhere is an error (`_only`). Types: s string, i integer, b boolean (only `true` is legal in `ui`), l list, t table.

| Table | Field | Type | Default | Meaning | Pack-time checks (descrec.py) |
|---|---|---|---|---|---|
| `[[area]]` | `key` | s | required | `F1`..`F8` | regex `F[1-8]` |
| | `name` | s | none | the area's title; also the core's short title (`F5`) | — |
| | `shifted` | b | false | under the SHIFTED heading | bool |
| | `[[area.tab]]` | l | 1..6 tabs | | 1 to 6 tabs |
| `[[area.tab]]` | `id` | s | required | what anchors name | `[a-z0-9_]{1,31}`, unique per area |
| | `name` | s | required | tab title | — |
| | `widget` | s | required | `info browser media settings tasks system nodes` | in the list |
| | `groups` | l of s | none | groups a settings/tasks/media tab lists | names `[A-Za-z0-9_]+`; only for those widgets |
| | `lines` | l of s | all | information lines of an info tab (`Core Application Network Media ROMs Libraries`) | each once; only `info` |
| | `n` | i | next free | printed number | 0..99, unique |
| | `layout` | s | none (flat list) | see D2 | in `LAYOUTS` (F2: three of them are the same) |
| | `ui` | t | none | layout parameters | keys in `UI_TAB_KEYS`; values text without `;` and without edge spaces; needs a `layout` |
| `[[node]]` | `anchor` | s | required | `Fn/tab` or `Fn/tab/group` (group = slot) | regex |
| | `kind` | s | required | `op state group` (`setting widget` compile, merge refuses: F4) | in `NODE_KINDS` |
| | `label` | s | required | | — |
| | `op` / `state` | s | none | op name; or the state shown | **`check_ops`**: the op exists, every argument bound once, types fit |
| | `bind` | l of t | [] | `{arg, file|int|str|ask|text}` (exactly one) | name exists, kind fits the type, int in range |
| | `order` | i | 0 | sort within the slot | fits 16 bits |
| | `when_state` | s | always | `name`, `name == v`, `name != v` (`empty` = no value); grey when false | state name exists |
| | `capability` | s | none | the node exists only if the core has it | — |
| | `id` | s | op/state | what `hide`/`rename` name | — |
| | `closes_menu` | b | false | the menu closes after the op | — |
| | `ui` | t | none | keys below | keys in `UI_NODE_KEYS`; `tone` in `muted cyan violet amber coral`; `goto` `Fn` or `Fn/tab`; **`key`/`bkey` one upper-case letter, unique per page** |
| `ui` of a node | `primary` `link` `rename` `job` `nokey` | b | false | primary = the violet button; link = a link style; job = runs stepped; nokey = no hotkey letter shown | `true` only |
| | `goto` `sub` `chip` `tone` `off` `why` `confirm` `label` `hide` `ask` `ask_state` `pick` `view` `none` `empty` | s | none | `sub`/`chip`/`label`/`hide`/`none`: text or `@state`, `@state|fallback`, `@set.Key`, `@info.Line`, `link.Line`; `off` = greyed with that reason; `why` = reason while `when_state` fails; `confirm` = question | text |
| | `key` | s | none | a **core hotkey**: acts on any page that does not claim the letter, boxed `[X]` at the row's edge | letter |
| | `bkey` | s | none | the **page letter** of a button, shown as a keycap before its label | letter |
| `[[task]]` | `group label op bind closes_menu ask ask_state pick_dir pick_label` | | | a button of a tasks tab | `ask_*` need `ask`; one `text` binding if `ask` |
| `[[media]]` | `group name state eject_op eject_bind` | | | a slot of a media tab | state exists |
| `[[filetype]]` | `ext inner_ext action[] inner[]` | | | what the Library offers per extension | lower-case `.ext`, unique; at least one action |
| `[[filetype.action]]` | `label op bind closes_menu ask ask_off size_in key` | | | `key`: the sheet letter | letter/digit upper case, unique per filetype |
| `[[core_setting]]` | `key group label values|list|range(+unit) default when apply base text secret maxlen empty show_state` | | `when = "live"`, `default` = 0 or the value's label | a row of Settings | `values` >= 2 non-empty; `default` in range; `apply` takes exactly one `value` int/enum covering base..base+n-1 (`_setting_problem`) |
| `[poll]` | `op period_ms` | | | called every period | no-arg op; 1..60000 |
| top | `hide = [ids]`, `[[rename]] id label`, `[[tab]] area id name widget groups n` (contributed tabs), `[lists]` | | | merge-time edits | — |

**ABI between TOML and C++.** There are exactly three ways the TOML names C++:
1. **Operations**: `op = "name"` etc. The application exports them as `r.add_op(name, ArgSpec[], n, handler)` (`ops.h`); argument types `AT_INT lo..hi`, `AT_BOOL`, `AT_ENUM labels`, `AT_STRING maxlen`, `AT_PATH` (519 = `FS_PATH_MAX` - 1, a folder path and a FAT long name; 255 before T180 #38), `AT_SLOT` (31), `AT_BLOB`. The export is the golden `tests/coreapp/golden/ops_trx64u.txt`, one line per op (`op<TAB>name<TAB>arg:type:lo:hi:maxlen:labels|...`) and per state (`state<TAB>name`), produced by `tests/coreapp/ops_export.cc`; `tools/gen_menu_rec.py` checks the TOML against it (the lint stage uses `--check`). An op that returns a code uses `OpErr` (`OPE_OK 0`, `-1 ARG`, `-2 NOT_FOUND`, `-3 IO`, `-4 FORMAT`, `-5 BUSY`, `-6 UNSUPPORTED`, `-7 FAILED`); the handler's message is shown in the status line.
2. **State values**: `r.add_state(name, fn(CoreCtx*) -> const char*)`; a null return is "no value". A name containing `$slot` is a per-drive declaration, unchecked until the core's capabilities are known.
3. **Settings**: `[[core_setting]]` rows are the settings store. `apply = op` is called with `value = base + index` when the value changes and `when = "live"`; `settings.noop` means "nothing to write to the machine, the value is read where used". (F1.)

**The record** (the binary the TOML compiles to): little endian, magic `TRXD` (0x44585254), version 3 (2 is read), 176-byte header, 17 fixed tables (`ORDER` in `descrec.py`, entry sizes `ESIZE`), one string table, maximum 0x20000 bytes, table limits `LIMITS`. Written by `tools/descrec.py`, read by `coreapp/src/descrec.cc`, checked against each other by `tools/test_descrec.py` and `tests/coreapp/`. A new field is a new version, and a reader accepts the versions it knows (min 2).

### D4 — The state query (the test SDK's one hook into the application)

> **Replaced (T192.3 D24, Alex 2026-11-09):** the v1 state described below (`uistate.{h,cc}`, `rest_ui_state`, `?v=1`) is deleted. `GET /v1/ui/state` is the UI service's snapshot (T192.1 contract 2, `coreapp/src/uiapp.cc`); the 49 scenarios read it (mapping old -> new in T192.3 section 5). Gate: `tools/check_no_v1_ui.py`.

A **read-only** query on the running application, `GET /v1/ui/state`, answered with the JSON of D4a. It changes nothing: `MenuCtl` belongs to the menu loop (`run.cc`), no other task may read it while a key is handled, so the REST task (`rest.cc`, route `/v1/ui/state`, hook `rest_ui_state`) raises a request and waits (at most 3 s, then 504), and the menu loop builds the snapshot at the end of its next pass (`uistate_service`) — in the menu-open and the menu-closed branch. Nobody asking costs nothing. The answer is built into a static buffer of 11,000 bytes (the REST output buffer is 12,288); a page with more rows than fit says `"truncated":true` and `focus` still names the row with the cursor. Without the hook (the native benches) the route answers 501. ue2emu reaches the application with `--net user --hostfwd tcp:127.0.0.1:<port>:80`. The files: `coreapp/src/uistate.{h,cc}`, plus `MenuCtl::dialog()`/`tab_id()` and `ui_shell_info_now()` (the top bar and footer exactly as the render draws them).

D4a — the JSON (`"v":1`; a false flag or an empty text is left out, a reader takes absent as false/empty): `seq` (grows with every key the menu took and every open and close), `menu_open` (when false, nothing else follows), `unsaved`, `area{index,key,name}`, `areas[{key,name}]`, `tab{index,n,id,name}`, `tabs[{n,name}]`, `layout` (`list` for a tab without one), `screen`, `title` (as drawn: the layout's `title`, else the tab name), `crumb`, `topbar{activity,chips[]}`, `footer{left,legend}`, `status{text,bad}`, `hint`, `dialog{kind: none|confirm|ask|text|address|pick|busy|sheet|viewer, text}`, `selected` (index), `library` (bool). A page that is not the Library has `items[{i,label,sub,chip,bkey,value,slot,group,kind,enabled,dim,primary,toggle,on,focused}]`, `focus{label,enabled,value}` and maybe `truncated`; the Library has `lib{path,rows_total,active_chip,chips[{name,absent,active}],media_level,medium{name,ready,size,contents,access} (only on the media level "/", the medium under the cursor),rows_first,rows[{i,name,ext,sub,primary,badge,dir,marked,more,focused}] (a window of 24 around the cursor, none while a sheet is open),sheet{file,items[{label,key,focused}]}}`. Labels are the labels as drawn: a button's letter (the keycap before its label) is the item's `bkey`.

### D5 — The test SDK: `tests/ui/` (Python 3 standard library only)

- **Gherkin subset**: `Feature`, `Background`, `Scenario`, `Given/When/Then/And/But`, comments. No outlines, no tables (add when a suite needs one). Files in `tests/ui/features/*.feature`.
- **Runner**: `tests/ui/runner.py` — parses, matches each step line against a regex table in `tests/ui/steps.py`, runs, prints one line per step, exits non-zero on the first failing scenario step (`--keep-going` for all). A failed step prints expected and actual and writes a PNG (`png` of the control port) as an attachment under `tests/ui/out/`.
- **Driver**: `tests/ui/emu.py` — starts ue2emu headless from a prepared stick/flash directory, speaks the control protocol (`key`, `wait`, `png`; the protocol is documented in the emulator's `docs/specs/S08-frontend-control.md`), queries the REST state, waits for `seq` to move after a key (no fixed sleeps), stops the emulator.
- **Steps (as built, `tests/ui/steps.py`)**: getting there: `the menu is open[ on <F-key>[ tab <n>]]` (opens it with the menu button, closing it first with RUN/STOP when it is up), `I press <key>[ <n> times]` (`F1`-`F8`, a digit, a letter, `left-arrow`, `RETURN`, `SPACE`, `RUN/STOP`, `cursor-left/right/up/down`, `home`, `ctrl+a/c/v/n`; waits until the state's `seq` moved and the menu came to rest), `I move to "<label>"`, `I move to the file "<name>"`, `I set "<setting>" to "<value>"`, `I step "<setting>" up|down <n> times`, `I go up to the top of the medium "<m>"`. Asserting: `the area is`, `the area key is`, `the tab is`, `the tab number is`, `the page title is`, `the layout is`, `the focused item is`, `the footer shows`, `the footer on the left shows`, `the status line is|says`, `the top bar shows|does not show`, `the activity is`, `a question is open: "<text>"`, `no dialog is open`, `the dialog is "<kind>"`, `the setting "<k>" is "<v>"`, `the button "<l>" is enabled|greyed`, `the button "<l>" has the letter <X>`, `the item "<l>" is in slot "<s>"`, `the item "<l>" shows`, `the page has the items`, `the page has no item`, `the menu is closed|shown`, the Library's `the library is at`, `the library lists`, `the file "<n>" is [not ]marked`, `the sheet is open for`, `the sheet lists "<l>" with the letter <X>`, `no sheet is open`; and the pack steps (`a menu description with the page`, `the page has the button "<l>" with the key|bkey <X>`, `I pack it`, `packing is refused with`, `packing succeeds`).
- **Pack steps** run `tools/descrec.py` on a generated TOML (no emulator).
- **Cost**: the emulator starts once per feature file on a fresh stick and a fresh flash image (a feature is one session; a scenario starts with `Given the menu is open`, which closes and reopens the menu); the Mac runs one emulator at a time (`nice -n 15`). The image is built on `builder` (`tests/ui/build.sh`).

### D6 — Every step can fail

For each step type `tests/ui/controls/` holds a `.feature` whose assertion is wrong on purpose (wrong title, wrong focus, wrong footer, wrong setting value, wrong button state, a dialog that is not there, ...). `run.sh --controls` runs them and **fails if any of them passes**. A step is only added together with its control.

### D7 — Later steps (the plan Alex named; each its own spec, this spec does not build them)

1. **Contract lint** (`tools/check_ui_contract.py`, S11): the tables of D2/D3 as data next to the checker; a name documented but unknown (or known but undocumented) fails the lint.
2. **Host app on the SDK**: a `menu.toml` of its own for the host (boot menu, core list, updater), the host app's screens become templates + nodes; the host exports its ops and states under the same registry (T104.1) and gets its golden export. The first consumer, because it has the fewest pages and no C64.
3. **TRX64 onto it**: the TRX64 emulator/line switches to the same record and layer; only its ops/states are new.
4. **The other cores** (Amiga, Atari, ZX, CPC, ...): each has a `menu.toml` and a `.feature` for its own pages; the common pages (settings bar, Library) are the TRX64U features reused with the core's own page names.

---

## §3a Open questions (one at a time; recommendation each)

- **Q1 — Is the widget layer part of the outside SDK (T131) or internal?** Answered 2026-10-29 (Alex: "machen wir so"): internal. Outside apps change content (TOML) and ops, not widgets; the stable surface for them is D3 plus the `svc` table. Exposing `trxview.h` would freeze 60 functions that were just rebuilt.
- **Q2 — A node in a slot its template does not draw.** Answered 2026-10-29 (Alex: "yes, validate when packing and do not build!", translated): refused at pack time, the pack fails. Today it takes focus and its letter works but it is invisible. As recommended: the template refuses at pack time — the checker knows the slot table of D2 per layout and an unknown slot is an error ("F6/trx64/baar"). Cheap, closes the typo class completely.
- **Q3 — `home` and `settings` are `split` under three names (F2).** Answered 2026-10-29 (Alex: "ja"), as recommended: drop `home` from `LAYOUTS` (the TRX64U menu uses `desk`, `split`, `settings`, `library`, `timeline`, `terminal`), keep `settings` as an alias of `split` that only changes the default crumb/bar expectation, document both in D2. Remove dead names rather than build the screen `screens.h` describes.
- **Q4 — The 4 unread settings and the noop rule (F1).** Recommendation: add a second no-op op `settings.stored` (same behaviour) used only for settings that a library reads by key, and make `settings.noop` illegal in the checker; the 4 unread rows are either wired (Splash: the splash step; Save Changed Cartridge: the cartridge write-back; Temp*: the temp cleanup) or hidden by `show_state` until they are. Decided per row 2026-10-29 (T104.2): Splash wired, Temp* built in, Save Changed Cartridge wired by T154.
- **Q5 — `left`/`label`/`frame`/`bar` tab keys (F3).** Answered 2026-10-29 (Alex: "ja"), as recommended: remove from `UI_TAB_KEYS` (and from `more[]` in the signature). No menu.toml uses them (checked by compiling: the 16 tab `ui` tables of the TRX64U menu use only `title`, `side`, `pos`, `info`, `glyph`, `track`, `hint`, `sheet`, `sheetx`, `marks`, `nodes_first`).
- **Q6 — Where do the tests run in CI?** Answered 2026-10-29 (Alex: "ja"), as recommended: the Mac and the builder run `tests/ui/run.sh` by hand/pre-merge for now; a CI stage later (the emulator binary is not built in this repository's CI, and the coreapp build is heavy). Hobby scale: no gate.
- **Q8 — Fix B1 and F7 now?** Both are one expression (`dirty_ = false;` in `Settings::load()`; `fl & ~0xFF01` in `parse_desc`), both found by this suite, both outside this spec's brief (UI behaviour, a pack tool). Recommendation: yes, in their own commits on `t104-2-board` before it merges, B1 turning the red scenario green and F7 putting `gen_menu_rec.py --check` back in the lint stage.
- **Q7 — Is the state query shipped in the release image?** Answered 2026-10-29 (Alex: "Ja bleibt" - "Yes, it stays"), as recommended: yes, read-only, behind the same REST server and its existing access rule; it exposes only what is on the screen. If Alex wants it out of release builds, a compile flag `COREAPP_UI_STATE` (default on) is one line.

---

## §4 Out

- The host app migration, TRX64 and the other cores (D7 steps 2-4): later specs; decided with Alex before they start.
- Pixel / golden-image comparison: deliberately not (Alex: assert on UI state). Screenshots are failure attachments only.
- Gherkin tables, scenario outlines, tags: when a suite needs them.
- Testing hardware-only functions (power, Wi-Fi, SID, streams): they are board steps (`docs/archive/board-tests/board-tests-mc.md`).
- Changing UI behaviour. Where the contract and the code disagree (F1-F6) this spec records the finding; fixing is Q2-Q5 and needs Alex.
- A second emulator or a headless LVGL build: the ue2emu run *is* the test (the real application image).

## §5 Decisions taken while building

**Results, 2026-10-29** (`tests/ui/run.sh`, ue2emu 0.6.0 `ccb71aa..`, image built from this branch on `builder`, `t104-2-board` 566b815ed + D4):

| Scenario | Where | Result |
|---|---|---|
| S1, S2, S3 | `features/navigation.feature` (11 scenarios; adds F2, F4 monitor, area returns to its last tab) | green |
| S4 (held change, Discard, Save, the S-with-nothing reason) | `features/settings.feature` (9 scenarios) | green except "after Discard nothing is unsaved any more": **red, B1** |
| S5 | same | green |
| S6 | `features/letters.feature` (7) | green |
| S7 | `features/library.feature` (7) | green |
| S8 | `features/home.feature` (5; includes mounting a disk from the Library and seeing it on the desk) | green |
| S9 | `features/pack.feature` (5, no emulator) | green |
| S10 | `controls/ui.feature` (41) + `controls/pack.feature` (3) | 44 of 44 fail at their last step, as they must |
| S11, S12 | — | not built (D7) |

Run time: all features 52 s (6 emulator starts, ~300 steps), controls 34 s. Suite total 44 scenarios: 43 green, 1 red (B1).

**Sensitivity check (a mutation, not kept):** `bkey = "S"` of the Save button on F6/1 changed to `"V"` in `menu.toml`, the record regenerated (by hand: F7 blocks `gen_menu_rec.py`), the image rebuilt: `settings.feature` went from 1 red to 4 red (the letter of Save, Save writes, Save with nothing to save, plus B1); put back and rebuilt.

**Decisions taken while building:**
- The route is `GET /v1/ui/state`, not `/v1/ui:state` (the `:` form is the alias table's Gideon-compatible style; this one is ours and a plain path).
- The menu is opened with the control port's `button` (the menu button), closed with RUN/STOP; the C64's left arrow key is `larrow`, the cursor keys are `left/right/up/down` (SHIFT variants handled by the emulator's key table).
- Key waits are on the state's `seq`, not on time: a press that does not move `seq` is a failed assertion ("the menu did not take the key").
- A negative control must fail at its **last** step; the runner calls a control that fails earlier a wrong-reason failure (found while building: five controls failed at their setup step).
- The Library keeps its folder and its cursor between visits (`last_dir_`, `browser_sel_`): the suite goes `home` / up to the top of the medium rather than assuming a fresh Library. Same for areas: an area key returns to the tab it was left on, so settings scenarios say `F6 tab 1`.
- Settings are changed through the UI (`I set ... to ...` presses Right/Left), so Auto save config is a real precondition of each scenario.
