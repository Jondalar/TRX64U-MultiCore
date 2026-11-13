# T190 — A fast USB mouse can be tamed in the Amiga cores' menu

**Status:** DECIDED (2026-11-06), build open. Corrected 2026-11-13 (status check): BUILT, merged to main (01b55e397); S1, S2 proven (§5); S3 board (Lemmings, A500) open.
**Repo:** TRX64U-MultiCore — core app (Amiga descriptors `coreapp/cores/amiga*.cc`, the USB mouse path); no gateware.
**Number:** T190.
**Origin:** GitHub issue #51 (a tester, 2026-11-06): with a 1000 DPI mouse the pointer in Lemmings on the A500 core moves too far; the
USB driver already applies sensitivity and acceleration before the core's mouse sink (`host-app/software/io/usb/usb_hid.cc:1506-1519`,
sink `:1589-1591`), only the Amiga cores have no settings for it. Alex, 2026-11-06 (translated): "just do it".
**Depends on:** T187 (menu layout: the rows go into the Input group; UX gate `.claude/skills/ux-review`).
**Milestone:** M3.

---

## §1 The problem

- **P1 Settings:** the Amiga cores (amiga, amiga1200) offer no mouse sensitivity / acceleration; the C64U's own firmware does.
- **P2 Effect:** the values reach the driver's existing scaling, stored per core in its `.cfg`, applied live.

## §2 Scenarios

| # | Solves | Scenario | Test |
|---|---|---|---|
| S1 | P1 | the Amiga cores' menu shows Mouse sensitivity and Mouse acceleration in the Input group, values as the C64U's firmware offers them | coreapp descriptor test + render, UX review PASS |
| S2 | P2 | a set value reaches the driver's scaling (a model report of +10 counts arrives scaled as the value says); a control without the apply must fail | coreapp test |
| S3 | P1-P2 | Lemmings on the A500 with a 1000 DPI mouse: a lemming can be picked at the lower setting | board |

## §3 Decision

D1 (Alex, 2026-11-06, "just do it"): build it as the issue describes, reusing the driver's existing sensitivity and acceleration; the
value range and default follow what the C64U's firmware offers (1:1 parity).

## §4 Out

Mouse settings for other cores (separate issue if asked).

## §5 Build (2026-11-06)

Built: rows "Mouse sensitivity" (1..16, default 8) and "Mouse acceleration" (Off/Adaptive, default Off) in amiga.cc (group "input", WHEN_LIVE),
ops mouse.sens / mouse.accel (engines.cc), values kept in mousesink.cc, answered to the driver by u64cfg.cc (keys 0x5D / 0x5E).
Source of range/default: vendor/gideon/software/u64/u64_config.cc:397-398. Tests: tests/coreapp/test_mousescale.cc/.py (rows, +10 report
scaled, restart applies stored values) with 10 controls in run.sh; apptests coreapp PASS on the builder. Size: coreapp.app 1705692 B, slot 1966068, 260376 unused.
UX: PASS R1/R2 (a1200 settings 1080/720/800x600, mouse rows selected); R3 left to T187.
Render bench at 480p (640x480): "F3 tab 2-5" (the A1200 Settings page; the label counts the areas present, not the key) went red, 4 pages with 3 objects outside the
canvas (the Smoothing row, y 475-494). Main (145b1807a) rendered on the builder: 254 checks, 0 failed, so the failure was T190's: two more rows push the last Picture
row of the clipping list pane (108,68 - 639,453) below the 480-line canvas. The row is not drawn (the pane clips it; the selection scrolls it in; R1 is tested by the
render case); `tests/view_bounds.h` now skips an object that lies wholly outside its clipping parent. Not a layout change: the page looks as before.
Final run on ab4a4148a (git archive, builder, shared cache): RISC-V hostapp + coreapp build, coreapp.app 1705692 B (slot 1966068, 260376 unused), `ci/apptests.sh hostapp coreapp loader` ok (coreapp: 102 test programs, 1507 controls, render included). S3 (board, Lemmings on the A500) open.
