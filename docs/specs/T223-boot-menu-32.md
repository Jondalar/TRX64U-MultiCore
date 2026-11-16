# T223 — The boot menu holds 32 entries, reads only the rows on screen, says when it is full, and shows configs on large cores (open)

**Status:** PROPOSED (2026-11-15). D1, D2 and P4 built; P3 waits for Alex (Q1).
**Repo:** TRX64U-MultiCore — host application (`host-app/software/hostapp/`). Nothing in `host/` (gateware, boot ROM) changes.
**Number:** T223 (free on origin/main 2026-11-15).
**Origin:** Alex, 2026-11-15 (translated): "Why are there still only 16 cores? We wanted 32. And they do not all have to be cached the same, only the visible ones." / "How come I see '1 config' at Apple II and not at the SuperCPU?" / "when you want to add a 17th there is also a stupid error".
**Depends on:** T101 (lazy reader), T198 (configs), T209 (menu rows). No spec records 32 (T101 P2 and T198 mention 16).
**Milestone:** M1.

---

## §1 The problem

- **P1 — 16 entries.** `BOOT_MAX 16` (`bootcfg.h`). Dependants, all sized by it: `BootCfg::e[]`, `bootcfg_parse` (`bootcfg.cc:61`, extra `core=` lines dropped), `bootcfg_add` (`bootcfg.cc:134`), the host core's `g_ent/g_pend/g_mig_tried` (`host_core.cc`), `icon_c[BOOT_MAX][1024 px]` + `icon_c_ok` (`hostui.cc:176`), the web snapshot arrays (`webui.h:32-42`), the text buffers `BOOT_MAX*260+128` (`hostui.cc`, `host_core.cc`), the config-count cache (`host_core_cfgov.cc:21`), the file-ops reference lists. Memory at 32: icons 64 KiB (was 32), entries about 8 KiB, four text buffers about 8.4 KiB each; all static. The boot ROM and the gateware do not read boot.cfg (grep of `host/` finds no reader), so nothing host-pinned limits it.
- **P2 — everything is read, not what is seen.** `hc_lazy_next` (`host_core.cc`) takes the default, the selection, then every entry in order: state (stat + header) then icon. With 32 that is 32 header reads and 32 icon loads at start, most of them rows nobody sees. Old entries without mtime also get a full hash check (`migrate_one`), but only once their state is known.
- **P3 — no config badge on a large core.** The badge count is `r->configs = !full && st != ES_UNKNOWN ? hc_cfgov_count(...) : 0` (`host_core_rows.cc:179`). A large `.cor` is `full` (`hc_is_full`, `host_core.cc:695`, "full firmware" in the version column), so the SuperCPU row never counts its `/SD/cores/c64scpu/configs/` (two files on the board). The Apple II is a small core, so it counts. The start path agrees with the exclusion: the config name is handed over by `core_set_start_config` (`host_core.cc:583`) only on the staged-core path; the full-firmware branch jumps before it (`legacy_jump`, above that line). Whether a large core's own application reads a handed-over config is outside this repository (Q1), so the badge was not turned on.
- **P4 — the message for a full menu.** Adding a 17th entry (F3 browse "A", also the firmware add) showed `"cannot add: the menu is full or the name has a '|'"` (`host_core_flow_fileops.cc:142`, old text): two causes in one sentence, no count, no way out.

## §2 Scenarios

| # | Solves | Scenario | Proof |
|---|---|---|---|
| S1 | P1 | 32 entries added through the browser stay in boot.cfg: 32 `core=` lines read back from the emulated flash | `tests/hostapp/emu_host.sh` T223 section (PASS line; boot.cfg dump `m32-boot.cfg`) |
| S2 | P1 | The menu with 32 entries: top page, scrolled page, last page, paging footer ("n-m of 32") | ue2emu pictures `t223-menu-top/scrolled/last` |
| S3 | P1 | Entry 17-32 boots (RETURN on a row of the second half starts it) | PASS 11-15: row 20 RETURN, log `core identified ... /USB0/cores/c20.cor` (§5) |
| S4 | P1 | The countdown default beyond 16 starts after the countdown | PASS 11-15: auto set to row 20 (F5, D), countdown, log `core identified ... c20.cor` (§5) |
| S5 | P1 | The editor (F5) lists and edits 32 entries, last page | ue2emu picture `t223-editor-last` |
| S6 | P2 | With 32 entries only the default, the selection and the rows on screen are read; scrolling reads the rows that come into view | PASS 11-15: `[host] entry N ... state read` lines (§5) |
| S7 | P3 | A large core's row shows the config marker (D4/D6) | ue2emu pictures `b_menu` (large row with "C"/"config") |
| S9 | D4 | RIGHT on a large core opens the overlay with its configs and "Boot safe graphics mode"; a foreign .cfw/.ue2 still gets none | ue2emu picture `b_overlay_large`; `hc_ecfg_ok` (code) |
| S10 | D4 | A large core started with a config: the host writes `<medium>/trx64-start.txt` (log "[host] start file ... written"), the core app reads it once, deletes it and starts with the config (log "[core] start file ... read and deleted: config \"Demo\"" and "[core] config Demo:") | ue2emu log of the large start (amiga1200 large .cor, our core app) |
| S11 | D4 | The file is gone after that start: a later plain start of the same core reads the (always written) file with an empty config, deletes it, and applies no config | ue2emu: second start of the large core from the menu (RETURN): core log "start file ... read and deleted: config \"\"", no "[core] config" line, no file left on the medium |
| S12 | D4 | A large core started with "Boot safe graphics mode": the core app logs "[core] safe graphics mode: N video settings overridden" | ue2emu log |
| S13 | D5/D6 | The boot menu row is a table: name | auto | config marker | pictogram | version; the columns keep their place on rows without a mark | ue2emu pictures `b_menu` 480p/720p/1080p, marker "C" and "config" |
| S14 | F1-F3 | Direct boot on: F1 subtitle "Pick a core." and the foot without the countdown words; while the menu is full the browser hint says "Menu full: remove one in F5 first."; the full note fits one line at 480p | pictures `a_menu_direct_on`, `a_full_message_browser`, `a_note_full` |
| S8 | P4 | The 33rd add is refused with "Menu full (32/32): remove one in F5 first." (shortened 11-15 to fit one line at 480p) in the note, nothing written | ue2emu picture `t223-full-message`, boot.cfg still 32 lines |

## §3 Decision

- **D1 (Alex, 2026-11-15):** 32 boot entries. `BOOT_MAX 32`.
- **D2 (Alex, 2026-11-15):** "they do not all have to be cached the same, only the visible ones." The lazy reader reads the default and the selected row always, then only the rows of the current window (`hc_lazy_window`, set by the screen from `mfirst/MROWS` or `efirst/EROWS`; outside the menu and the editor: none). Rows that scroll into view are read then. Entries never in view stay "unknown"; they still start (the start verifies by itself, T101 S10).
- **D3:** the full-menu note says the menu is full and how many it holds, and what to do; the other refusal (a `|` or line break in the name) keeps its own text.
- **D4 built (2026-11-15):** see §5 "D4-D6".
- **D4 (Alex, 2026-11-15, Q1 "T223 ja", after the mock-ups h5a/h5b; answers Q1; named D1 in the coordinator's note):** large cores get configs and the RIGHT overlay too (including T224's "Boot safe graphics mode"); the host hands the choice over through a small file on the SD before the full-firmware start, and the large core's core app reads it (no gateware). Not built yet.
- **D5 (Alex, 2026-11-15, translated: "but please like a table in the boot menu view: corename / Auto / config / picto"; named D2 in the coordinator's note):** the boot menu row becomes aligned columns: core name | Auto | config | status pictogram. Not built yet; open: whether the version column stays (T221 D13/D16 put an L/P badge next to the version), being asked with mock-ups first. Amends T209 D16/D20 if the version goes.
- **D6 (Alex, 2026-11-15, after mock-ups t1a/t1b, translated: "Boot menu: b, but please do not waste the column on the config name. A \"config\" or a \"C\" is enough"):** the version column with the L/P badge stays (t1b). The config column is narrow: a short marker ("C" or "config") on rows that have configs, nothing otherwise; no config name in the row. RIGHT still opens the overlay.

## §4 Out

- More than 32 entries.
- A different cache policy for icons (the icon cache stays one slot per entry; only reading is windowed).
- Config badge on large cores until Q1 is answered.

## Open questions for Alex

- **Q1:** A large core is started as full firmware, and the host hands over no config name on that path. Should a large core take a config (the host would hand it over as for small cores, and the large core's application must read it - TREX/TRX64-Ultimate side), or should the SuperCPU row stay without a badge because the start cannot use it?

## §5 Built

- `bootcfg.h`: `BOOT_MAX 32`. `host_core.cc/.h`: `hc_lazy_window`, `hc_lazy_next` honours it. `hostui.cc`: `lazy_window()` before each lazy step and busy check. `host_core_flow_fileops.cc:142`: new full-menu message. `tests/hostapp/emu_host.sh`: T223 section.
- `tools/dev.sh riscv`: ok (2026-11-15).
- Evidence (2026-11-15, `tools/dev.sh emu` on the builder, `tests/hostapp/emu_host.sh` T223 section, whole script PASS): 33 `.cor` copies on the stick, 33 A presses in the real browser: boot.cfg read back from the flash image holds 32 `core=` lines. Pictures (emulator, 960x540 canvas; the emulator offers no 720p/1080p mode): `/tmp/claude-501/m32pics/t223-{full-message,added-32,menu-top,menu-scrolled,menu-last,editor-last}.png` (also in `.ci/dev/host-menu-32/emu/`). Seen: the note "Boot menu is full: it holds 32 of 32 entries. Remove one in Edit menu (F5) first." (S8), "1-7 of 32" and "26-32 of 32" (S1, S2, S5).
- Not checked: S3 (boot of entry 17-32), S4 (countdown default beyond 16), S6 (the emulator console does not carry the host's `[host] entry N` lines, so the windowed reading is not measured; the code path is `hc_lazy_next`), S7 (blocked on Q1), 720p/1080p, the web UI with 32 entries. No app tests run (pipeline).

- UX review (ux-specialist, 2026-11-15): FAIL on one point, R2: the browser's note line sits on the panels' bottom edge (the note slot of the browser, older than T223, not changed here). R6 wording fixed ("Edit menu (F5)"; pictures show the earlier wording "editor"). Proposals not built: hint "A adds it" while the menu is full (R10); "+/- PAGE" in caps (older). Question: does + page by 5 or 10 (first page 1-7, next 11-17)?

### Walk of S3, S4, S6 and the R2 fix (2026-11-15, branch host-menu-32, host-boot-rows and main merged)
`tools/dev.sh riscv` rc 0. Real host app in ue2emu at 480p60/720p60/1080p60, 33 `.cor` copies (the kit's VIC-20 bundle with this build's core app), pictures and logs in scratchpad `t209t223/menu32/<mode>/` (`m_added32`, `m_full_message_browser`, `m_direct_off`, `m_menu_top/scrolled/last`, `m_editor_last`, `m_s3_row20`, `m_s4_editor_auto20`). Flow: 33 A presses in the browser (32 `core=` lines read back: `boot.cfg: 32 cores`), direct boot off in F8.
- S3: boot menu, 19 times DOWN to entry 20, RETURN: the core application identifies `VC20 VIC-20, cor /USB0/cores/c20.cor` (all three modes).
- S4: F5, entry 20, D (auto), fresh start without a key: the countdown ends and the core application identifies `/USB0/cores/c20.cor` (all three modes).
- S6: with the menu open at 1080p (8 rows, direct boot off) 8 entries had `state read` before the first key (the default and the window); the later entries were read while paging (row 19 after 17 key presses, ...), 32 in all after the walk to the last page.
- R2: the browser's panels used `ph = CH - 90` and ended at `CH - 16`, under the note line at `CH - 26`. Now `ph = CH - top - 30` (ends 4 px above the note); the browser holds 12 rows on 960x540 (was 13; the 13th sat on the panel edge), the compact count follows the same height (`layout_counts`); render bench `BPH` and the row-count label adjusted. Picture: `m_full_message_browser` (all modes).
- Not run (pipeline only): render bench, apptests. Web UI with 32 entries not checked.
- ux-review (ux-specialist, 2026-11-15, 27 pictures): R2 of the browser PASS in all modes (`m_full_message_browser`, `m_added32`). New R2 FAIL at 480p, F8 Settings with direct boot off (`480p60/m_direct_off`): the help line wrapped to two lines onto the status line. Older text (T135), fixed here: "Menu first; the autostart below decides about the auto core." (`host_core_rows.cc`); picture `480p60/m_settings_directoff_fixed`. The "on" text ("The auto core starts at power-on without the menu. C= or fire ...") is also longer than one 480p line (not shown with a status line over it; not changed).
- Questions for Alex (proposals of the review, not built; pictures in `t209t223/menu32/<mode>/`): (1) the full-menu note is cut at 480p ("... Remove one in Edit m...", `480p60/m_full_message_browser`); A: "Menu full (32/32). Remove one in F5 first.", B: "Boot menu full (32). Free a slot in F5 Edit menu.", C: keep the text, action first. (2) `+`/`-` page by 10 (480p) / 9 (720p, 1080p) while the window holds 7/8, so rows are skipped (`m_menu_top/scrolled/last`); option: page by exactly the window. (3) the browser hint says "A adds it to the boot menu" while the menu is full; option: "Menu full: remove one in F5 first." (4) F5 info box takes a row at 480p: already T209 D19/question on branch host-boot-rows.

### Paging (2026-11-15)
Question 2 of the review was a bug (a page must move the window by exactly the rows shown): fixed on host-boot-rows (T209 "Paging fix"), merged here; the browser holds 12 rows here, bench numbers for the browser page cases adjusted (12 rows, 27 entries). Pictures `t209t223/menu32/page/`.

### D4-D6 and three small fixes: hand-over of 2026-11-15 (superseded: built and shown 2026-11-16, see the last section)
Stopped because the builder is held for Vivado: `tools/dev.sh riscv` queued for an app slot and never got one (my queued runs were killed). **Nothing of this section was compiled or run.** First action of the next agent: `tools/dev.sh riscv` on this branch and fix compile errors.

**Done (code, uncommitted before this commit's own paths, see git log):**
- Merge of `t224-picture-reset` into this branch (no conflicts).
- D4 host: `handover.h/.cc` `startfile_path/format/parse` (file `<medium of the .cor>/trx64-start.txt`, lines `TRXSTART1`, `cor=`, `config=`, `safe=0|1`); `host_core.cc` writes it just before `legacy_jump` for a large `.cor` (never a .cfw/.ue2) through the new `HcPlatform.start_file` (`hostui.cc core_start_file` -> `hostfs_save`), always, also plain (`config=` empty), so a stale file is overwritten; log line `[host] start file <path>: config "<n>", safe <0|1>: written|not written`. `hc_ecfg_ok(i)` (all entries but a foreign .cfw/.ue2) replaces the full-firmware gate for the row badge (`host_core_rows.cc`), the footer "RIGHT configs" and `cfgov_open` (`host_core_ui.cc`: `cfw_is_name`).
- D4 core app: `start.cc start_file_take` (called in `run.cc` right after `start_paths`): finds the file from the hand-over path's medium (no page path: `/SD`), loads, ALWAYS deletes it, drops it when `cor=` differs from the page's path; without a page path it adopts the file's `cor=` as `cor_path` and the core folder cfg (config folder lookup needs it). `run.cc`: config = page's, else file's; safe = page's or file's.
- D5/D6 boot menu table: `hostui.cc fill_mrow` fixed columns from the right: version | pictogram | config marker (`CFGM_W`) | auto | name; `HcEntryRow.cfgmark` ("C", or "config" when built with `-DHC_CFG_MARKER_LONG`, see `host_core_rows.cc`). The editor row keeps "N configs". L/P badge (T221 D13/D16) is not on this branch: its slot is not drawn; check `main` for it before merging. No column headers yet (ux-review decides).
- Fixes (coordinator): direct boot on: F1 subtitle "Pick a core." and the foot without "any key stops the countdown" (`host_core_rows.cc hc_menu_texts`, `hc_menu_foot`); full note "Menu full (32/32): remove one in F5 first." (`host_core_flow_fileops.cc`); browser hint "Menu full: remove one in F5 first." while the menu is full (`ADD_HINT` in `host_core_rows.cc`).
- Test written, not run: `tests/coreapp/test_handover.cc` (startfile path/format/parse).

**Next (missing):** (1) RISC-V build; (2) build twice for the marker: normal and `HOSTAPP_EXTRA=-DHC_CFG_MARKER_LONG`; (3) ue2emu scripts are prepared in scratchpad `.../scratchpad/t223b/`: `lib.sh`, `setupA.sh` (32 small entries -> `workA/flash0.bin`; then F1 with direct on/off, browser at c33 with the full menu, add -> note), `setupB.sh` (amiga1200 large .cor with our `coreapp.bin` via `cor_pack.py replace-app` + 3 VIC-20 entries, configs Demo/Alt, auto on c01). They only prepare the flash; the per-screen run scripts (480p/720p/1080p: F1 menu, RIGHT on the large row, DOWN, RETURN to start it with config Demo, grep the log for the S10-S12 lines; second start with RETURN for S11; "Boot safe graphics mode" row for S12) are still to write. Whether ue2emu starts a large .cor with the amiga1200 profile (`tests/coreapp/emu/profiles/amiga1200.toml`, add `symbols = "coreapp.elf"`) is unproven. (4) ux-review with the pictures; the "C" vs "config" pictures for Alex; pictures into `.../scratchpad/t223b/<screen>/`. (5) fill the S7, S9-S14 rows.

**Open:** where the file lives (medium root of the .cor, not literally "/SD": decided so ue2emu's USB stick works and a core on USB works; Alex to confirm); that a core app with no page path adopts the file's folder (changes where such a start reads its .cfg: only reached when the page is lost); the older render bench / web UI expectations of "N configs" in menu rows and of the old full-menu text were not searched beyond a grep of tests/ and docs/ (only comments name the old text: `tests/hostapp/emu_host.sh:148` and this spec's §5).

### Build, run and pictures (2026-11-16, branch host-menu-32, a525bc208 + the fix below)
- `tools/dev.sh riscv`: compiled at once, no compile error (hostapp, coreapp). Second host app with the long marker (`HC_CFG_MARKER_LONG` defined in BOTH `host_core_rows.cc` and `hostui.cc`: `CFGM_W` lives in `hostui.cc`; a build that defines it for the rows only clips the text to "onfi").
- **Bug found and fixed (S10):** `run.cc` called `start_file_take` BEFORE `start_core_folder`, which waits for the core's medium; the file lives on that medium, so `stat` failed silently and the config never arrived (host log "written", core log silent). Now `start_file_take` runs after `start_core_folder`. Same limit remains when the hand-over page is lost (no `cor_path`, no medium wait before the read).
- ue2emu: large .cor works with the profile: `symbols = "coreapp.elf"` is a top-level key (before `[ro]`), NOT under `[handover]`; the hand-over also needs a Kickstart (`roms/kick13.rom`, 512 KiB zeros are enough) or the core app stays on the "ROM files" page. The Amiga's menu hotkey is C= + RUN/STOP + RESTORE (`hold cbm+runstop`, `restore`), return to host menu = F8, DOWN, RETURN. Our `coreapp.bin` is packed into the real A1200 `.cor` with `cor_pack.py replace-app`.
- Evidence rows (logs `workB/run-<mode>.log`, same lines at 480p60, 720p60, 1080p60):
  - S7/S13: `<mode>/short_menu.png`, `long_menu.png` (rows Amiga large "full firmware" + VIC-20 auto, marker "C" resp. "config", columns name | auto | marker | pictogram | version); `a_menu_direct_on.png` (rows without marker: columns keep their place).
  - S9: `<mode>/run_overlay_large.png`, `run_overlay_large_demo.png`, `run_overlay_large_safe.png`: Plain start, Alt, Demo, Boot safe graphics mode. A foreign .cfw/.ue2 gets none: code only (`cfw_is_name`), not run.
  - S10: `[host] start file for /USB0/cores/amiga1200.cor: config "Demo", safe 0: written`, then `[core] start file /USB0/trx64-start.txt read and deleted: config "Demo", safe 0` and `[core] config Demo: /USB0/cores/amiga1200/configs/Demo.cfg`.
  - S11: second start (RETURN, plain): host writes the file with `config ""`, core `read and deleted: config "", safe 0`, no `[core] config` line; after the three starts no `trx64-start.txt` is left in the stick's root.
  - S12: large: `[host] ... config "", safe 1: written`, `[core] ... safe 1`, `[core] safe graphics mode: 3 video settings overridden for this start`. Small core (VIC-20): same line, `.cfg` md5 identical before/after (`runC.sh`).
  - S14: `a_menu_direct_on.png` ("Pick a core.", foot without countdown words), `a_menu_direct_off.png` (subtitle and foot with the countdown), `a_full_message_browser.png` (note "Menu full (32/32): remove one in F5 first." on one line at 480p; the info box says "Menu full: remove one in F5 first.").
- Pictures: `.../scratchpad/t223b/{480p60,720p60,1080p60}/`.
- ux-review (agent's own pass over the pictures, not the ux-specialist): PASS with notes. (1) 480p, Amiga row: the name is cut in the middle ("Amiga 12..., 68020)", "Amiga ...68020)" with the long marker) because the "full firmware" version text is wide; fine at 720p/1080p. (2) 480p foot of the boot menu wraps "+/- PAGE" to a second line (since the paging key, not new). (3) "C" fits every mode; "config" fits (60/52 px) but costs 34 px of name width at 480p. (4) direct-on 480p subtitle and foot as intended.
- Open: the start file lives at the root of the medium of the `.cor` (`/USB0/trx64-start.txt`), not "/SD" literally (Alex to confirm); "C" vs "config" for Alex.

### Fast-tier fixes on integ-1116 (2026-11-16)
The red fast tier came from T224 code (compile error in `test_settings.cc`, missing `core_set_start_safe` fake in `hostcore_stubs.cc`, a hand-over bit-flip loop that ran over the T221/T224 tail fields). Details in T224 §5; all three hand-over check words (config, host version, safe) cover their whole field.
- Two more reds, found when the run really used this worktree: `tests/hostapp/test_cfgov.cc` still counted the pre-T224 rows (now Plain start + 5 configs + the safe row = 7, the last cursor stop is the safe row, "old" is two steps up from it); `tools/host_cut_ledger.txt` had no line for `core_start_file` (T223 D4), which is platform glue like `core_cfg_save` (a `HcPlatform` callback) and stays in `hostui.cc`.
