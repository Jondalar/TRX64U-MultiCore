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
| S3 | P1 | Entry 17-32 boots (RETURN on a row of the second half starts it) | open: not walked, see §5 |
| S4 | P1 | The countdown default beyond 16 starts after the countdown | open: not walked, see §5 |
| S5 | P1 | The editor (F5) lists and edits 32 entries, last page | ue2emu picture `t223-editor-last` |
| S6 | P2 | With 32 entries only the default, the selection and the rows on screen are read; scrolling reads the rows that come into view | emulator console lines `[host] entry N ... state read` in `m32.log` (only visible N before the scroll) |
| S7 | P3 | A large core's row shows its config count | blocked on Q1 |
| S8 | P4 | The 33rd add is refused with "Boot menu is full: it holds 32 of 32 entries. Remove one in the editor first." in the note, nothing written | ue2emu picture `t223-full-message`, boot.cfg still 32 lines |

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
- Evidence (2026-11-15, `tools/dev.sh emu` on the builder, `tests/hostapp/emu_host.sh` T223 section, whole script PASS): 33 `.cor` copies on the stick, 33 A presses in the real browser: boot.cfg read back from the flash image holds 32 `core=` lines. Pictures (emulator, 960x540 canvas; the emulator offers no 720p/1080p mode): `/tmp/claude-501/m32pics/t223-{full-message,added-32,menu-top,menu-scrolled,menu-last,editor-last}.png` (also in `.ci/dev/host-menu-32/emu/`). Seen: the note "Boot menu is full: it holds 32 of 32 entries. Remove one in the editor first." (S8), "1-7 of 32" and "26-32 of 32" (S1, S2, S5).
- Not checked: S3 (boot of entry 17-32), S4 (countdown default beyond 16), S6 (the emulator console does not carry the host's `[host] entry N` lines, so the windowed reading is not measured; the code path is `hc_lazy_next`), S7 (blocked on Q1), 720p/1080p, the web UI with 32 entries. No app tests run (pipeline).
