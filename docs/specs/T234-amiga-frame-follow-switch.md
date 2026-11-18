# T234 — The Amiga cores' frame follow (T188) is a setting, off by default; off gives the standard HDMI rows of before T188

**Status:** DONE (board pass by Alex 2026-11-18: S4, S5 and the A500); merged into main (1b8f69485) 2026-11-17 late; board: A1200 Off and
On checked by Claude (JTAG) 11-17. Proposed 2026-11-17.
**Repo:** TRX64U-MultiCore — the Amiga cores' own host copies (`cores/amiga/mono/host/`, `cores/amiga1200/mono/host/`: `hdmi_timing.v`,
the mode rows, `tools/hdmi_modes.py`), the cores' register blocks, the setting in the core app (through the generic settings path),
the manual (11.6, 11.7).
**Number:** T234 (TODO.md table).
**Origin:** Alex 2026-11-17: "since we brought in the other picture clock, the Amiga acts up" (translated), then, asked with options
(switch default off / take T188 back / wait for the tester): "Switch, default off".
**Depends on:** T188 (docs/specs/done/T188-amiga-frame-lock.md, D1-D5 and §5), T107 (A1200 output modes).
**Milestone:** M2.

---

## §1 The problem

Since T188 was merged (2026-11-13, eba7f582f) an Amiga core at 50 Hz drives HDMI with its own row set and steers the frame length:

- rows outside the standard timing: 1080p50 as 2638 x 1125 at 148.150412 MHz (-0.235 % against CEA VIC 31's 148.5 MHz, hfp 526
  instead of 528), 720p50 as 1978 x 749, 576p50 as 867 x 625 (T188 D4, in the cores' copies since eba7f582f; the first T188 rows of
  2026-11-10 - 1135 lines, +1.05 %, 1960 and 880 pixels a line - were replaced by D4 before that merge);
- a line-count loop that changes every output frame by -8..+16 lines to follow the core (D3, D5).

T188's S4 ("every screen keeps the picture, no re-sync, audio stays") has no board record. Since then: a tester cannot use 1080p50
with the Amiga core at all, and *Worms DC* on the A1200 test build shows no picture and no sound (GitHub Discussion #94, 2026-11-17);
HDMI carries the sound, so a screen that loses the sync loses both. T188 has no switch (D2: automatic), so nobody can turn it off to
check.

- **P1** A screen that does not take the T188 rows or the changing frame length loses picture and sound.
- **P2** There is no way to turn the follow off, for a test or for a screen that needs it.
- **P3** What T188 fixed (the hitch every ~6 s in vertical scrollers, GitHub #48) must stay available.

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1, P2 | Setting `Off` (default, also on a fresh start with no stored settings): rows 0..2 of both Amiga cores are the host's standard rows of before T188, and the frame length never changes | `tools/hdmi_modes.py check` / the `hdmi_timing` bench of both cores with the switch off: PASS line naming the standard rows, `len_bad=0`, every frame the same length | sims (CI tier of `hdmi_timing`) |
| S2 | P3 | Setting `On`: T188's behaviour unchanged (follow PASS lines as in T188 §5: no repeat, no skip at 49.60..50.20 Hz) | the same bench with the switch on | sims |
| S3 | P2 | Changing the setting takes effect like a change of the picture mode (one re-sync), without a core reset; the value is stored with the other settings | app unit (setting -> register write, stored, read back after restart) + ue2emu picture of the row | NAS units, ue2emu |
| S4 | P1 | Board, `Off`: the A1200 at 1080p50 and 720p50 on Alex's screen keeps picture and sound; *Worms DC* starts with picture and sound | board, Alex or a tester | board |
| S5 | P3 | Board, `On`: *Pinball Dreams* scrolls without the 6 s hitch | board | board |
| S6 | — | Both Amiga cores route, WNS >= 0 | CI `large:amiga`, `large:amiga1200` report lines | CI |

No mutation tests (T174 D5.4).

## §3 Decision

- **D1 (Alex 2026-11-17, "Switch, default off", translated).** One setting `Frame follow` (`Off`, `On`; default `Off`) in both Amiga
  cores. Replaces T188 D2 ("automatic, no setting").
- **D2** `Off` = the host's standard rows 0..2 (as before T188 (the host's rows; the cores' copies at 91c9dd419^1)) and the follow loop disabled; `On` = T188 as built (Amiga rows,
  loop with the D5 limits). The 60 Hz rows are not touched by either.
- **D3** The value goes to the core through its register block at once, like a picture-mode change (the app re-applies the mode); no
  core reset.
- **D4** The row is set up through the generic settings path (TOML / generic module); no growth of `coreapp/cores/amiga*.cc` without a
  `tools/d24_ledger.txt` line and Alex's OK (T192 D24, T213 D4).
- **D5 (Claude as advisor, Alex may overturn)** The row sits with the cores' other picture rows on the F5 settings page; the
  ux-review checks it in ue2emu.

## §4 Out

- Changes to T188's control loop or its limits.
- Other cores' modes; the shared host table.

## §5 Build log

**2026-11-17 - built on t234-amiga-follow (1a755a05c, 3e816c5ae, d28b2f4f7, 22f59bedf), merged into main (e8db511c0) on Alex's "merge and build"; S1-S3 proven in sims/units, S4/S5 board and S6 CI open.**
- **Gateware:** `mode_req.v` of both Amiga copies (equal): register `fol` (write 0x4F02 bit 0, read 0x4F0C, power-up Off), version byte 4. With Off a request for rows 0..2 runs the new rows 12..14 of the generated `hdmi_modes.vh`, the host's own 50 Hz rows of before T188 (`tools/hdmi_modes.py amiga-gen`; `amiga-check` and `selftest` 29 of 29 hold them equal to the host's); `hdmi_timing` unchanged (its follow already applies to m < 3 only). A change of the setting changes the effective row, so the CDCE is rewritten like a mode request, no core reset (D3). The shared `host/` is untouched.
- **App (no per-core C, `check_d24.py` rc 0):** a generic `Frame follow` row (Off/On, default Off) on F5 Video after Smoothing, added by `picture_extend` when the gateware reports version >= 4; `follow.apply` writes the register and waits for the re-tune; `picture_start` applies a stored value before the mode.
- **Evidence (builder, both cores, same lines):** `PASS hdmi_timing (12 modes, 4 refused: exact, align on every row)`; rows 12..14 with Off `fe=[0..0] fe_end=0 len_bad=0/338` at 49.60, 49.920128 and 50.20 Hz; T188's `PASS hdmi_timing follow (no repeat, no skip at 49.60 .. 50.20 Hz, ...)` unchanged; `PASS modepath (... Frame follow Off and On)`; `gwstatus: 72 checks, 0 failed`; `scale: 329 checks, 0 failed`; render `296877 checks, 0 failed`; `tools/dev.sh riscv` rc 0.
- **Cost (estimate, no Vivado yet):** about 80-120 LUT and 5 FF per core, no BRAM/DSP.
- **Open:** S4/S5 board, S6 Vivado; pictures are render-bench pictures (ue2emu has no model of version 4); open for Alex: the T224 safe graphics mode does not force Frame follow Off; a stored On costs one more re-sync at start.

**2026-11-17 evening - board failure, gateware reverted on main.**
- **Board (Alex, package main-6fdc2234, home monitor):** the A1200 gives no picture, the menu does not come either. The core runs; its log (REST
  /v1/report): "[amiga] frames core 230 out 0 at 10000 ms" (the core's frames count, the HDMI output's do not), "[core] frame follow: the gateware
  did not take Off" twice (video_gw_set_follow_io: pix_locked and running never both true within 1 s), "[TRX UI] pixel layer: no flip".
  Alex: "fette regression".
- **Cause not found by reading** (mode_req, hdmi_modes.vh rows 12..14, cdce_mode, the settle/gt_rst chain in top.v, hdmi_timing all read
  consistent). The benches covered hdmi_timing and modepath only, not the start path on the board (cdce_mode I2C write, the 50 ms settle, the GTP
  reset, mmcm_pix, the first frames into pix_layer).
- **Revert:** the twelve gateware, generator and bench files of the merge (cores/amiga{,1200}/mono/host/{mode_req.v,hdmi_modes.vh,hdmi_timing.v},
  tools/hdmi_modes.py, host/sim/hdmi_timing/*, host/sim/modepath/*) are back at e8db511c0^1: version byte 1, power-up row 2, T188 always on, as on
  the board of 2026-11-13. The core app's generic Frame follow row stays; it appears only on gateware that reports version 4 (none now). Manual
  11.6.8/11.7.3 and the README sentences removed. `tools/hdmi_modes.py check`: "PASS hdmi_modes amiga: rows 0..2 run the Amiga frame ...";
  selftest "27 of 27 mutations caught".
- **Redo:** on a branch, with a test of the whole start path that counts output frames, before any board step.

**Redo (Alex 2026-11-17 late, after the revert):** asked "That is exactly what we wanted as an on/off switch, right?" - yes; shown the design
below: "Try it. I am sleeping. You have to start the A1200 in the host yourself" (translated). The switch the right way round:
- **Off (default):** rows 0..2 are again exactly the host's standard rows of before T188 (the cores' copies at eba7f582f^1), the power-up
  state too (vm_mode / mode_pix 2): the default is the old path byte for byte.
- **On:** T188's Amiga rows as new rows 12..14 with the follow loop (follow on m 12..14 instead of m < 3); used only while On.
- **Proof before Alex sees it:** the A1200 started from the host by Claude on the board, both settings; the core's log shows HDMI output frames
  ("[amiga] frames core N out M", M > 0) and the menu.

**2026-11-17 late - redo built and checked on the board (branch t234-switch: 5cd3bbbc1, 3f67dce29; package t234-switch-3f67dce2).**
- **Off = the old path:** rows 0..2 are the host's again, power-up row 2; `tools/hdmi_modes.py check` "rows 0..11 are the host's (Frame follow Off,
  the default); rows 12..14 run the Amiga frame 49.920128 Hz within 1 ppm", selftest "28 of 28 mutations caught". Board (Alex, photos): AmigaVision
  at 1080p50, the menu with Frame follow Off.
- **The cause of both failures today (T234 Off before, the redo's On after):** the Amiga copies' modules said `include "hdmi_modes.vh"`; Vivado runs
  in host/, so host/hdmi_modes.vh (9 rows, no 12..14) was read instead of the Amiga copy: rows 12..14 had CDCE word 0 and hm_valid 0. JTAG with
  On before the fix: "GTP down, pixel MMCM unlocked, 0 frames/s". The Amiga table is now `hdmi_modes_amiga.vh` (both copies, FROM, the generator).
  Consequence: T188's own rows (2638 x 1125 etc.) never ran on the board before; T188's board result of 11-13 was the follow loop on the host rows.
- **Board after the fix (Claude, A1200 started from the host web page, JTAG `tools/trxdbg.sh mode`, switched over REST `follow.apply`):**
  On: "GTP ready, pixel MMCM locked, 50 frames/s, 2967552 pixel clocks per frame" (= T188's 2638 x 1125); Off: "GTP ready, pixel MMCM locked,
  50 frames/s, 2969856 pixel clocks per frame" (= 2640 x 1125); live both ways, no core reset, no "did not take" in the log. Stored back to Off.
- **Not checked:** the A500 core on the board (same change; the host menu cannot be reached over REST from a running core); picture and sound of
  T188's rows on a screen (S5, Pinball Dreams); Worms DC (S4). The core app's "[amiga] frames core N out 0" counter reads 0 although HDMI runs
  (JTAG: 50 frames/s) - a wrong read-out, not a dead output.
- **Wrong turn, taken back:** a start-path change of T235 (b23a54ecd, a stored unnamed Picture row would run at the screen's named rate) rested on
  a wrong guess (the screen takes 1080p50 although its EDID does not name it); reverted (dd0512243) before any board use.

**2026-11-18 - board pass (Alex, page "Board Test 11-18", cores of t234-switch 3f67dce2 on /SD/cores since 11-17 22:23, host main-13c2cde9).**
- S4: the A1200 with Frame follow Off at 1080p50 and at 720p50, a few minutes with sound: picture and sound stay; Worms DC with Off: picture and
  sound (GitHub Discussion #94).
- S5: Pinball Dreams with Frame follow On: no hitch in the scrolling; picture and sound stay (T188's rows 2638 x 1125 on a screen for the first
  time, see the redo above).
- The A500 (amiga.cor): Frame follow Off, On, Off - a picture in both settings, the menu works. Alex: "both Amigas good !!!" (translated).
- S6: runs 2420 / 2421 (`vivado large:amiga`, `large:amiga1200`) green on t234-switch.
- Still open outside T234's scenarios: the core app's "[amiga] frames core N out 0" read-out (wrong while HDMI runs).
- Found by the review before the push of 2026-11-18: the benches amiga1200_hdmi_timing and amiga1200_modepath (host/sim/hdmi_timing,
  host/sim/modepath, ci/sims.txt) were put back by the revert of 11-17 and still test T188's first layout (rows 0..2 bent, version 1); the
  redo was proven by `tools/hdmi_modes.py check` and the board. They fail when they run (nightly: reported, not fatal - amiga1200 is
  experimental). ci/sims.deps now names hdmi_modes_amiga.vh. Alex chose the second (2026-11-18, translated: "drop them? is that
  possible?"): the two rows are out of ci/sims.txt, ci/sims.lane and ci/sims.deps. The bench scripts stay (host_hdmi_timing, host_modepath,
  c128_hdmi_timing use them); the A1200's copy is checked by `tools/hdmi_modes.py check` in the lint and by the board.
