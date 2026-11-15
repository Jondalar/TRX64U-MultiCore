# T224 — A core stuck in "Setting the picture" can be recovered, and the host can reset a core's video settings

**Status:** PROPOSED (2026-11-15).
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
| S3 | P2 | Reset of a core's video settings from the host leaves every non-video key and every `Mount` line of `<core>.cfg` untouched | host app bench on a cfg text (pure function) | pipeline apptests |
| S4 | P2 | After the reset the core starts with Picture = Auto, Scale = Integer, Smoothing = Off | S3's cfg through the core app's `Settings` load | pipeline apptests |
| S5 | P2 | The action is reachable and shows what it did | ue2emu pictures + `ux-review` | ue2emu |

## §3 Decision

Not decided. P1: no code until Q1 is answered (the fix must follow a verified cause). P2: not built until Q2..Q5 are decided by Alex.

- D1 (Alex 2026-11-15, Q2, after mock-ups h1a-c, translated: "I thought with the configs? RIGHT on the core and there is \"Boot safe graphics mode\" or so"): the rescue sits in the boot menu's config overlay (RIGHT on a core entry, T198), as an entry "Boot safe graphics mode". Options a-c of Q2 are replaced by this. What the entry does (this start only, or reset the stored settings) is asked next.

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
