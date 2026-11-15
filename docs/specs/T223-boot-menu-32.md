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
| S7 | P3 | A large core's row shows its config count | blocked on Q1 (Alex decides) |
| S8 | P4 | The 33rd add is refused with "Boot menu is full: it holds 32 of 32 entries. Remove one in Edit menu (F5) first." in the note, nothing written | ue2emu picture `t223-full-message`, boot.cfg still 32 lines |

## §3 Decision

- **D1 (Alex, 2026-11-15):** 32 boot entries. `BOOT_MAX 32`.
- **D2 (Alex, 2026-11-15):** "they do not all have to be cached the same, only the visible ones." The lazy reader reads the default and the selected row always, then only the rows of the current window (`hc_lazy_window`, set by the screen from `mfirst/MROWS` or `efirst/EROWS`; outside the menu and the editor: none). Rows that scroll into view are read then. Entries never in view stay "unknown"; they still start (the start verifies by itself, T101 S10).
- **D3:** the full-menu note says the menu is full and how many it holds, and what to do; the other refusal (a `|` or line break in the name) keeps its own text.

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
