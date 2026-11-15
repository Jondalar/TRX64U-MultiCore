# T195 — The board's three buttons (UI_BUTTON[2:0], the header for a case button) work in the host and every core

**Status:** DONE 2026-11-15 (Alex 2026-11-15: "under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part.") Open board/scenario items are closed by Alex's decision, not separately recorded. Earlier status: BUILT, merged to main 2026-11-09 (`host/top.v` carries UI_BUTTON); S1 bench pass, S2 rests on code reading (no unit test), S3 Vivado line not recorded, S4 board (header button) open, so not DONE. 2026-11-15 sync: duplicate status text merged.
**Repo:** TRX64U-MultiCore — host gateware (`host/top.v`, an approved host change: `contract/host_pin.txt` is updated), every large
core's own `top.v` copy, the host app and the core app (what a press does).
**Number:** T195.
**Origin:** Alex, 2026-11-09 (translated): "This has to go in, because that is how the U64 Elite+ often does the reset. It has the rocker
there too, but many people use that" - about the pins `UI_BUTTON[2:0]` (P17/R18/N18), which none of our designs reads.
**Depends on:** nothing. Milestone M2 (finish plan wave 2, gateware, batched).

---

## §1 The problem

- **Who:** a person with a reset button wired to the board's button header (common on the U64 Elite / Elite II; the C64U has the
  rocker too), or who presses the board's own buttons.
- **What they want:** the button does what it does on Gideon's firmware: reset (and the other two buttons their functions).
- **What happens today (code):** `UI_BUTTON[2:0]` are in Gideon's XDC (issue 615) with pull-ups, but not in `host/top.v` nor in any
  large core's `top.v` (gap audit G19, `docs/gap-audit-gideon-app.md:47`). A press reaches nothing. The rocker works only because it
  goes through the ESP32 and the application (T04).
- **Why it matters:** testers with a header button think the reset is broken.

Parts:
- **P1 Pins:** the three inputs reach a register the application reads (synchronised, debounced), in the host and in every large core.
- **P2 Meaning:** which button is which, measured on the board and checked against Gideon's firmware (`vendor/gideon/software`, how it
  reads the buttons and what each does); our host app and core app do the same per button (reset = the core's reset path; the
  others as Gideon's, or as the rocker's events).
- **P3 Area and the host freeze:** the host change is small (3 inputs, a synchroniser, one register field), fits `pb_l1`, and every
  core's partition contract / large-core copy follows.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | A press (low) on each pin is seen in the register after the debounce, a bounce shorter than the window is not | sim bench of the input block | builder |
| S2 | P2 | The app maps each button to its action (reset, menu, ...) exactly as specified; a held button does not repeat the action | app unit test | apptests |
| S3 | P3 | Host and each large core route and meet timing; `pb_l1` within budget | CI vivado report lines | CI |
| S4 | P2 | Board: a button on the header resets the running core (C64 trx64u, A1200, C128); the other two buttons as specified; the rocker still works | by hand, board (needs the physical button) | testers with a header button (Alex, 2026-11-09: "I cannot test it .. I have no cable with 3 buttons", translated); named in the test build's notes |

## §3 Decision

- **D1 (Alex, 2026-11-09):** the pins go in, host and cores.
- **D2 (Alex, 2026-11-09, replaces the first wording):** first "Core menu öffnen .. das ist \"freeze\" doch immer in allen cores, wie die Wippe
  nach oben tippen einmal kurz" (translated: "Open the core menu .. that is what 'freeze' always is in all cores, like tapping the rocker up
  once briefly"); then, on the freezer: "Ahhh .. Freezer: da würde das Action Replay freeze menu aufgehen falls eines geladen ist. Das soll
  einfach mit \"menu\" machen" (translated: "Ahh .. Freezer: that would open the Action Replay freeze menu if one is loaded. It should simply
  do 'menu'"). So: the LEFT button opens our core menu, the same action as the middle button and the rocker's Menu event, in every core and
  the host. It never triggers a cartridge freezer (Action Replay etc.), even when one is loaded.
- **D3 (coordinator, technical):** 20 ms debounce on press and release; a held Reset button holds the core in reset (level, like a real C64
  reset button); IOSTANDARD/PULLUP from Gideon's `c64u.xdc` (`:206` LVCMOS33, `:452-454` pins, `:515-517` PULLUP). **Middle = Menu** (the
  rocker's Menu path), **Right = Reset** (`c64_reset_n` path). The rocker's Freeze event (0x81) moves from ITU bit 7 to bit 5, decided by
  evidence (see §5): bit 7 is the right button as in Gideon's firmware, where `bootrom.c:334` reads it at boot as Lock.
- Not decided here and not built: nothing in the app consumes the rocker Freeze event; see §4.

### Design (phase 1, 2026-11-09; the button map is D2/D3 above, the left button joins Menu instead of Freeze)

**Gideon's side (code, `vendor/gideon`):**
- Gateware: `fpga/io/itu/vhdl_source/itu.vhd:31` port `buttons(2:0)`, read at `:192-193` as `buttons & "00000"`, register ITU+0x0A
  (`itu_pkg.vhd:17`); no inversion and no debounce in the ITU (active high in the register; the pins have PULLUP in the XDC, so the
  top must invert - the top is not vendored). Reset/Freeze of the cartridge logic are not in the vendored VHDL.
- Software: `system/itu.h:21,93-96` bit 5/6/7 = button 0/1/2; `io/c64/c64.cc:2139-2151` and `io/overlay/overlay.h:160-167`: rising
  edge of **button 1** = menu (`userinterface.cc:431-432,511-514`); no debounce besides the edge. `portable/riscv/bootloader_u64ii.c:201-202`:
  bit 7 (button 2) set at boot = "Lock", the app is not started. `filesystem/blockdev_flash.cc:168` and `components/config.cc:60`: button 0
  at boot = safe mode / no flash disk, only in the non-`U64` build branch. `io/c64/c64.cc:143,183,390`: "Button order" writes
  cart register 0xA; default item 1 = "Freezer|Menu|Reset" (left to right), i.e. right = Reset, middle = Menu, left = Freeze, as Alex's P4 fact.
  So Reset and Freeze are done by cartridge gateware, not by software.
- Index: 2 = right = Reset, 1 = middle = Menu, 0 = left = Freeze (boot-lock index 2 fits the right button; the physical pin order on P4 is
  checked in S4).

**Ours today:** `host/ultimate_sys.vhd:765,904-906` ITU already has `buttons`: 0 tied '0', 1 = rocker Menu (ESP event 0x82, 200 ms hold,
`:895-898`), 2 = rocker Freeze (0x81). Rocker Reset (0x83) pulls `esp_reset` = `c64_reset_n` (`:1606`) -> host `core_reset_req` (`host/top.v:1312`,
contract port `layer2_ports.txt:34`, every core has it). Apps: `coreapp/src/hw.cc:458-463` `hw_menu_button_pressed()` (edge of ITU_BUTTON1); host
app uses Gideon's `checkButton`. Nothing in the apps reads bit 5/7 except the two boot checks above. Rocker Freeze on bit 7 collides with the
boot lock meaning.

**Design:**
- New module `host/ui_buttons.v`: per pin 2-FF synchroniser (cpu_clk), active low -> high, one shared 1 ms tick (100 MHz / 100000, 17 bit),
  per-button 5-bit counter: level must be stable 20 ms before the output follows (press and release). Estimate ~40 LUT, ~35 FF
  (0.3 % of the 13,000 `pb_l1` LUT; no BRAM, no DSP). No new register address: the outputs OR into the existing ITU register.
- `ultimate_sys.vhd`: new in port `ui_btn(2:0)` (debounced, active high). `buttons(0) <= rocker_freeze`,
  `buttons(1) <= ui_btn(0) or ui_btn(1) or rocker_menu` (D2: left = Menu), `buttons(2) <= ui_btn(2)` (rocker Freeze moves from bit 7 to bit 5, so bit 7 = Reset
  = Gideon's boot-lock meaning exactly; no app reads bit 7 or 5 for freeze today). Reset: `c64_reset_n <= '0' when ... or esp_reset /= 0 or ui_btn(2)='1'`
  (level, as Gideon's cartridge reset; held button = held reset, no repeat action).
- `host/top.v`: `input wire [2:0] UI_BUTTON`, instance of `ui_buttons`, wire to `ult`. The DFX contract needs **no** new port: the buttons end in
  the host's ITU, Reset rides the existing `core_reset_req`. So `layer2_ports.txt`/`contract/VERSION` stay.
- `host/pins.xdc`: PACKAGE_PIN P17/R18/N18, IOSTANDARD (take from `c64u.xdc` bank), PULLUP (now only in unread `c64u.xdc:452-454,515-517`).
- Apps: none change. Menu is the existing edge on ITU button 1 (`hw_menu_button_pressed`, Gideon's host app); left and middle both drive it, a held
  button gives one edge. Reset needs no software. Nothing reads bit 5 or 7 for the rocker Freeze (checked 2026-11-09: `hw.cc`, `host-app`, `bootrom.c`).
- Files: `host/top.v`, `host/ultimate_sys.vhd`, `host/ui_buttons.v` (new), `host/pins.xdc`, `host/layers.txt` (new file in the `host:` list),
  `contract/host_pin.txt` via `tools/host_pin.sh --update` (approved host change, Alex D1), no app file. The
  12 large cores carry own copies, each: `cores/<id>/mono/host/{top.v,ultimate_sys.vhd,pins.xdc,ui_buttons.v}`, ids amiga, amiga1200, c128, c264, c64scpu,
  cpc, diag, neogeo, nes, snes, zx, zxnext (`tools/check_large_copies.py` must pass; each also needs its `mono/core.toml` sources list checked).
  The mono top.v differs from host/top.v (own `top.v.diff` for c128), so the edit is per core, not a copy.
- Benches: S1 `host/sim/tb_ui_buttons.v` (iverilog via `ci/sim_iv.sh`): press on each pin seen after 20 ms, bounce < 20 ms ignored, release debounced, Reset
  level reaches `c64_reset_n`; negative control not required (not flash). S2 app unit test of the edge/hold logic in `coreapp` apptests; ue2emu `--trx-io on`
  gets the pin as a script command if it has one (check, else rocker-event path only).
- Board S4 (needs the button on P4, by hand, Alex): after load: C64 core: right = reset to READY, middle = menu opens (host and in a core), left = menu too (never a freezer); each of A1200 and C128: right = reset; rocker still resets/opens menu; hold right at power-up = boot Lock (as Gideon), release and boot normally.

Open questions 1-4 of the proposal: answered by D2/D3 (1 yes, 2 Menu, 3 level, 4 20 ms).

## §4 Out

- The rocker's Freeze event (0x81) reaches ITU bit 5 and nothing reads it (as before on bit 7): no app action is built for it. Whether the rocker up
  should open the menu like the left button is not decided here (Alex's D2 sentence about the rocker describes his firmware).
- Boot with the right button down = Lock (bootrom.c:334) is Gideon's behaviour and is now reachable through the pin; a Reset button stuck at
  power-up locks the boot. Not changed.
- No ue2emu command for the pins (the emulator has no pin model for them).

## §5 Decisions taken while building

- **Built 2026-11-09 (branch t195-buttons, local, not pushed).** `host/ui_buttons.v` takes ultimate_sys's 1 ms tick (`tick_1kHz_o`, already out in
  `top.v` as `drv_t1k`), so no own divider: yosys `synth_xilinx` says 12 LUT + 3 CARRY4 + 24 FF (spec estimate was ~40 LUT / ~35 FF); against the
  13,000 LUT of `pb_l1` that is 0.1 %. Real figures come from the CI's `=== T79 pb_l1 | Slice LUTs` line (not run yet, S3 open).
- **Rocker Freeze to bit 5, by evidence (2026-11-09):** after D2 the left button is Menu, so nothing needs bit 5 for a pin. Bit 7 must stay free
  for the right button because `host/bootrom/bootrom.c:334` reads it at boot as Lock (Gideon's meaning); the rocker Freeze on bit 7 locked the boot
  if the event arrived in that window. Readers of the ITU button register: `bootrom.c` (bit 7), `coreapp/src/hw.cc:458` (bit 6 only),
  `host-app` `blockdev_flash.cc:168` / `config.cc:60` (bit 5 at boot: safe mode; reached only by a rocker Freeze event in that window, no pin
  drives bit 5), `itu.c:92` (a getter), Gideon's `c64.cc`/`overlay.h` (bit 6). No reader treats bit 5 or 7 as the rocker's Freeze, so the move
  needs no app change.
- **S1 (debounce):** `sh host/sim/ui_buttons/run.sh` -> `PASS tb_ui_buttons errors=0` and `PASS host_ui_buttons (debounce 20 ms both ways, wiring top.v / ultimate_sys.vhd / pins.xdc)`
  (15 ms bounce and 10 x 3 ms chatter are ignored, a press shows after 25 ms and not at 17 ms, release likewise, a 10 ms release glitch keeps
  the button, all three together). The wiring half is grep on the files, the same pattern as the `esp_ctl` bench; it proves the text, not the routing.
- **S2:** no app code changed. Menu from the pins is the existing edge on ITU button 1 (`hw_menu_button_pressed`); a held button is one edge (bench: btn
  stays 1, no second transition). No unit test of the edge helper exists or was added; the claim rests on the gateware bench and the code reading.
- **Large cores:** the same edits in the 12 copies (`tools/check_large_copies.py`: ok, 12 cores); `cores/{c128,c64scpu,diag}/mono/top.v` and their
  `top.v.diff` carry them too; every `build_mono.tcl` reads `$hd/ui_buttons.v`; `host/build.tcl` and `host/layers.txt` list it; `contract/host_pin.txt` updated.
- **Benches run locally (Mac):** esp_ctl, bling_lamp, host_gain, apple2_pins, c1541leds_c264, amiga1200_bling PASS. `c128_nonet` says FAIL ("the fork") on
  the Mac because `ghdl` is missing; it fails the same on the parent commit.
