# T224 — A core stuck in "Setting the picture" can be recovered, and the host can reset a core's video settings

**Status:** BUILT on branch t224-picture-reset (2026-11-15), not merged; P1 closed, P2 decided D1-D3 and built.
**Repo:** TRX64U-MultiCore — core app (`coreapp/src/picture.cc`, `run.cc`), host app (boot menu), the A1200 gateware copy of `mode_req.v` (read only here).
**Number:** T224 (TODO.md: T221 was the last row on main).
**Origin:** Alex, 2026-11-15 (translated), photo of the start screen "TRX64 / Amiga 1200 (AGA, 68020) / 093e598 2026-11-13 15:55 / Setting the picture", bar at about a quarter: "The Amiga does not get out of there any more ... We need something so that we can 'reset' a core's video settings from the HOST." Then "Ja" to one spec covering both.
**Depends on:** T164 (mode request 0x4F00), T107 (application owns the picture), T188 (Amiga rows 0..2), T148 (core folders).
**Milestone:** M1.

---

## §1 The problem

`/SD/cores/amiga1200/amiga1200.cfg` held `Picture=576p / 480p`, `Scale=Integer`, `Smoothing=On`. The A1200 (app build 093e598, 2026-11-13) then stays on the start screen at step 2 of 4, "Setting the picture", with no way out except editing the SD card. Claude set `Picture` back to `1280 x 720` on the SD.

- **P1 The hang.** What stops the start in that step, and what a start must do instead when a stored Picture does not come up.
- **P2 No way back from the host.** A person whose core does not start (or starts with an unusable picture) has to edit `<core>.cfg` by hand. The host can start a core but cannot clear that core's video settings.

### Read so far (code, not comments)

- The step is `cs_step(1, ...)` at `coreapp/src/run.cc:510`; the next step marker is `cs_step(2, ...)` at `run.cc:533`. In between: `picture_start` (`picture.cc:507`), `new Freeze`, `new MenuCtl`.
- `picture_start` -> `apply_mode` -> `video_apply_gateware` -> `video_gw_apply_io` (`app/video/videomode.cc:218-236`): writes `GW_REQ`, polls every 10 ms, returns false on the refused bit or after `GW_APPLY_TIMEOUT_MS` = 1000 (`videomode.h:124`). **This wait is bounded.** On false, `picture.cc:546-549` keeps the previous mode and goes on; `Video::set_mode` (`video.cc:169-191`) and `video_layout_scaled` (`videomode.cc:106-147`) contain no loop. `picture.cc` and `video.cc` have no `while` at all.
- The A1200 gateware (`cores/amiga1200/mono/host/mode_req.v`, `c64_type = 0`): any row with `hm_valid` is accepted (row 0 = 576p50 is valid), so the request is not refused; whether the pixel clock of row 0 (27.050460 MHz, T188 D4) locks on the board is not proven anywhere: T188 §5 says "S4 checks 576p first" and S4 has no recorded pass.
- Integer scale of the A1200's 720 x 540 picture (`coreapp/cores/amiga.cc:328`) in 576p (720 x 576) is 1x, fits; in the 480p row (640 x 480) it does not fit (`videomode.cc:127-131`: s is raised to 1, the frame is cropped, no hang).

**Not found by reading:** an unbounded wait in the app between step 1 and step 2. The cause of "does not get out" is therefore NOT verified. It is either a stall below the app (a bus access that never completes while the HDMI path is retuning, gateware) or a state the log shows and the code does not (the app is running but the picture shows no change: step 2 is drawn on a layer the sink does not show). The board log of the failing start decides (`[core] picture chosen: ...`, `[video] the gateware did not bring ...`, `phase ...` lines of STARTLOG) - see Open question Q1.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | Stored `Picture` whose apply returns false (refused or not locked in 1 s): the start goes on, picture stays in the previous mode, and the stored value is shown as Auto with a note | unit test of `picture_start` with a fake gateware IO that never reaches the row (existing `tests/coreapp` fakes) | pipeline apptests (written only after Q1/Q2) |
| S2 | P1 | A1200, `Picture=576p / 480p`, `Scale=Integer`, `Smoothing=On` starts to the menu | board: Alex's A1200 + log | board, by hand |
| S3 | P2 (D1) | The config overlay (RIGHT on a core row) has the last row "Boot safe graphics mode" after "Plain start" and the configs, also on a core with no config; the cursor reaches it, it is not a config (no default, rename, delete) | `tests/hostapp/test_cfgov.cc` + ue2emu pictures 480p/720p/1080p | apptests, emu, ux-review |
| S4 | P2 (D1) | A full firmware (.cfw, large core) gets no overlay and no safe start: RIGHT does nothing, no footer hint (no hand-over page is written for it) | code: `cfgov_open` gates on `hc_is_full`, `hc_menu_foot` on `hc_efull`; board by hand | review, board |
| S5 | P2 (D2) | RETURN on that row starts the core once: the hand-over page carries the flag (tail field `safe_sum`), the core application overrides Picture, Scale, Smoothing (the ones the core has) and logs `[core] safe graphics mode: N video settings overridden`; no confirm | `tests/coreapp/test_handover.cc` (flag, noise, other block) + `tests/hostapp/emu_t224_safe.sh` (log line, three screens) | apptests, emu |
| S6 | P2 (D2, D3) | Nothing is written for it: `<core>.cfg` is byte-identical after the start; a later save of the .cfg or "Save as config" in that session does not store the safe values; a value the person changes in that session is stored as always; the core's other video settings are not touched; the next normal start uses the stored values | `tests/coreapp/test_settings.cc` (override block) + emu script (cfg after the run equals before) | apptests, emu |
| S7 | P2 | One-shot: the flag is cleared after the jump and by every start path (RETURN, countdown, web) that does not set it | code: `hc_start_safe_set(false)` after the start, `g_start_safe` cleared in `hc_start_sequence`; `start_safe` cleared in `core_jump` | review |

## §3 Decision

Decided (D1-D3, 2026-11-15); P1 closed by the board result (§5). Earlier: Not decided. P1: no code until Q1 is answered (the fix must follow a verified cause). P2: not built until Q2..Q5 are decided by Alex.

- D1 (Alex 2026-11-15, Q2, after mock-ups h1a-c, translated: "I thought with the configs? RIGHT on the core and there is \"Boot safe graphics mode\" or so"): the rescue sits in the boot menu's config overlay (RIGHT on a core entry, T198), as an entry "Boot safe graphics mode". Options a-c of Q2 are replaced by this. What the entry does (this start only, or reset the stored settings) is asked next.
- D2 (Alex 2026-11-15, "A"): "Boot safe graphics mode" starts the core ONCE with safe video values (Picture Auto, Scale Integer, Smoothing Off); the stored settings in `<core>.cfg` stay untouched, the next normal start uses them again. So nothing is written (Q5 void) and starting is the action itself (no confirm: Q4 void).
- D3 (Alex 2026-11-15, Q3 "A"): only the three host-owned values Picture, Scale, Smoothing are overridden for that start; the core's own `group = "video"` settings stay as stored. All questions of P2 are answered; build next.

### Open questions for Alex (one at a time)

1. **Q1 (P1):** Can you give me the serial/REST log of one failing A1200 start with `Picture=576p / 480p`? Without it the cause stays unproven. Fallback if you cannot: I add a start guard that is independent of the cause - if the stored Picture is not up 1 s after the request, the core app writes `Picture=Auto` to the cfg, says so in the splash, and goes on (a stall below the app would still not be cured by this).
2. **Q2 (P2) where the action sits:**
   a) Boot menu row action: a key on the selected core entry (e.g. `V`) opens "Reset video settings of <core>?".
   b) F5 editor of the entry: a line "Reset video settings" under the entry's configs.
   c) A key held at start (like the other held-key rules in `directboot.h`): holding e.g. `V` while the core starts applies the reset for that start.
3. **Q3 (P2) which keys:** (a) `Picture`, `Scale`, `Smoothing` (the three the core app owns, `picture.cc:80-125`), or (b) every key of the core's own `group = "video"` too.
4. **Q4 (P2) confirm:** a one-line confirm dialog, or act at once and show "Video settings reset" as a chip.
5. **Q5 (P2) what is written:** delete the three lines from `<core>/<core>.cfg` (the core falls back to its defaults) or write the default values explicitly.

Mock-up of option a) with a confirm (text):

```
 TRX64  /  Boot                                    11:42
 +-------------------------------------------------+
 |  Amiga 1200 (AGA, 68020)             amiga1200  |
 |  C64 Ultimate                        c64u       |
 |  ...                                            |
 +-------------------------------------------------+
   Reset video settings of Amiga 1200?
   Picture, Scale and Smoothing go back to Auto,
   Integer, Off. Nothing else changes.
        [ Enter  Reset ]      [ Esc  Cancel ]
```

## §4 Out

Nothing decided yet. Gateware changes (A1200 576p lock) are out of this spec until Q1 shows they are the cause.

## §5 Decisions taken while building

2026-11-15: research only, no code. The core app path from step 1 to step 2 has no unbounded wait (cites in §1); P1's cause is open, see Q1.
- 2026-11-15, P1 board result (Alex): the hang at "Setting the picture" with `Picture=576p / 480p` happened with the game-capture device in the HDMI path; "without the game capture" the A1200 starts and everything works ("Also klappt prima alles"). The core and the app are not the cause; P1 closes without a code change. Not explained: why the app stayed in the start step while the capture was attached (the mode apply is bounded at 1 s by reading); not pursued.

### T224 build (2026-11-15, D1-D3)

- **Carrier:** the hand-over page (T198 D7 way): a tail field `safe_sum` after `config[48]` in `struct Handover` (`handover.h`), outside every earlier check word, set by `handover_set_safe`, read by `handover_safe`. No new mechanism, no gateware. An older core application ignores it (plain start); an older host leaves it 0.
- **Host:** `host_core_cfgov.cc` rows = Plain start, configs, "Boot safe graphics mode" (cursor `l.n + 1`, `hc_cfgov_on_safe`); `host_core_ui.cc` `HCA_CONFIG_START` arms `hc_start_safe_set`, `host_core.cc` `hc_start_sequence` hands it to `core_set_start_safe` (`coreboot.cc`), `core_jump` writes it and clears it.
- **Core app, how the three values are applied without being stored:** `Settings::override_value` (`settings.cc`) keeps a per-item override `ovr_[i]` beside the stored layers. `value()` answers it, so `picture_start`, the menu and the scaler see Picture Auto (index 0), Scale Integer (`VID_SCALE_INTEGER` = 0), Smoothing Off (0). Every file writer (`write_cfg`, hence set/save/mount_put/mounts_clear/Auto Save, and `export_config` for "Save as config") builds from `stored_value()` (core layer, else host layer, else default), never the override. `Settings::set` clears the override of that item before it stores, so a Picture/Scale/Smoothing change by the person in the session is stored like always (also the Picture revert after an unconfirmed mode: it writes the value in force, as before). `load()` and `rebind()` (Load config in the running core) drop all overrides, which ends safe mode. `picture_safe` (`picture.cc`) is called from `run.cc` after the settings are read and before `picture_start`; a core that lacks Scale or Smoothing (no gateware scaler) gets only the items it has.
- **Large cores (T223 Q1):** started as full firmware (`e.full`, `legacy_jump`), no hand-over page, so the flag cannot travel: the overlay does not open on them (`cfgov_open` returns on `hc_is_full`) and the boot menu footer shows no "RIGHT configs" for them (`hc_efull`).
- **Changed on the way (needs Alex's nod):** T198 D9 said RIGHT on a core without configs does nothing. The rescue must be reachable on every core, so the overlay now opens with no config ("No configs yet", Plain start, the rescue row) and the boot menu footer shows "RIGHT configs" on every non-full row (before: only rows with configs). The F3 browser footer is unchanged (still by config count); RIGHT works there on every non-full core.
- **Evidence:** `tools/dev.sh riscv` ok (hostapp.app, coreapp.app). ue2emu (`tests/hostapp/emu_t224_safe.sh`, VIC-20 with stored `Picture=1280 x 720, Scale=Fill, Smoothing=On`): PASS at 480p60, 720p60, 1080p60; log line `[core] safe graphics mode: 3 video settings overridden for this start`, the core picked `Picture auto` (576p / 480p, 1280 x 720, 1920 x 1080 per screen), the stick's `vc20.cfg` unchanged after the run. Pictures in the session scratchpad `t224/safe-<mode>-{menu,overlay,overlay-safe,core,core-menu}.png`. Unit tests were written, not run by this agent (rule: tests in the pipeline).
- Not shown: the core's one-line notice ("Safe graphics mode: ...") appears as a status line when the core menu opens; the ue2emu picture of the menu does not show it (status gone or not drawn at the time of the shot) - not claimed as proven.
- **ux-review (2026-11-15, ux-specialist):** PASS. Opened: 480p overlay/overlay-safe/menu/core/core-menu, 1080p overlay-safe; not opened: 720p and the other 1080p pictures, the 10-config overlay (code argues R1: `listwin_fix` total `l.n + 2`). Notes for Alex, one each: (1) the hint says "Integer scale" (internal wording): name the result instead? (2) the boot menu foot says "RIGHT configs" on a core with no config: rename to "start options"? (3) the scroll counter "1-8 of 12" counts Plain and the rescue row too. The core pictures show no safe-mode effect (the log line is the proof).
