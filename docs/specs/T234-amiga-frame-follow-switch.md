# T234 — The Amiga cores' frame follow (T188) is a setting, off by default; off gives the standard HDMI rows of before T188

**Status:** BUILT, MERGED into main 2026-11-17 (Alex: "merge and build"); Vivado (S6) and board (S4, S5) open. Proposed 2026-11-17.
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
| S1 | P1, P2 | Setting `Off` (default, also on a fresh start with no stored settings): rows 0..2 of both Amiga cores are the host's standard rows of before eba7f582f, and the frame length never changes | `tools/hdmi_modes.py check` / the `hdmi_timing` bench of both cores with the switch off: PASS line naming the standard rows, `len_bad=0`, every frame the same length | sims (CI tier of `hdmi_timing`) |
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
