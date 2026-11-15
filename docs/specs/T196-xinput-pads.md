# T196 — An Xbox-style (XInput) controller on a USB cable works in the console cores (NES, SNES, Neo Geo), as a HID gamepad does

**Status:** BUILT, merged to main (3887b1dbf); S1-S4 native tests; S5 board PASS 2026-11-10 (Series Pro pad on a USB cable; HID pad and keyboard beside it not checked); S6 (third-party pad, X mode, #55) tester open, so not DONE. 2026-11-15 sync: the old 'BUILDING ... S5/S6 open' text removed.
**Repo:** TRX64U-MultiCore — the USB host driver the apps compile (`host-app/software/io/usb/`, our copy with patches) and the core app's pad
service (`coreapp/src/pad.cc`, `hidpad.cc`). No gateware. Host app: nothing (the host menu takes no gamepad).
**Number:** T196.
**Origin:** GitHub issue #55 (a tester, 2026-11): "Support XInput so we can use all kinds of controllers (Xbox) and don't have to buy special ones" -
a T50 pad (acegamer) in X mode is not recognised. Alex, 2026-11-09 (translated): "make a spec for it please ... XInput devices via USB cable ... that
should work out".
**Depends on:** T141/T181 pad service and mapping (`pad.h`, as built). Nothing else.
**Milestone:** M2.

---

## §1 The problem

- **Who:** a tester with an Xbox 360 / Xbox One / Series controller, or a third-party pad in "X" mode (T50 and most PC pads), on a USB cable.
- **What they want:** plug it in and play NES, SNES, Neo Geo (every core with a `PadSpec`) as with a HID pad: the menu names the pad, the
  button mapping (F8, capture) works, two pads are two players.
- **What happens today (code):** `UsbHidDriver::test_driver` (`host-app/software/io/usb/usb_hid.cc:1076`) claims only interfaces with
  `interface_class == 3` (HID). An XInput pad is a vendor-class interface: Xbox 360 wired and X-mode pads = class 0xFF, subclass 0x5D,
  protocol 0x01; Xbox One / Series = class 0xFF, subclass 0x47, protocol 0xD0 (GIP). No driver claims it, nothing reaches `pad_attach`, the menu
  shows no pad. It has no HID report descriptor, so `hidpad_parse` could not read it anyway.
- **Seen by the tester:** the T50 in S mode (HID) on Windows 11 reconnects constantly; in X mode our board does not see it. Our own HID path for the
  S mode is a separate question (§4).
- **Why it matters:** Xbox controllers are the pads most people own; the request is to not buy a special one.

Parts:
- **P1 Claim:** a USB driver claims the XInput interface (both kinds above) and reads its interrupt IN endpoint.
- **P2 Start:** the pad is woken as it needs: Xbox 360 sends reports at once (an LED packet is optional, sets the player ring); Xbox One / Series
  (GIP) sends nothing until the host writes the power-on packet to its OUT endpoint, and wants its "guide/announce" messages acknowledged.
- **P3 Report:** the fixed report layout (360: 20 bytes, buttons in bytes 2-3, triggers 4-5, sticks 6-13; GIP: message 0x20, buttons in bytes 4-5,
  triggers, sticks) becomes what the pad service already takes, so naming, mapping, capture, two players and REST (`pad.n.*`) work unchanged.
- **P4 Sticks and triggers:** the left stick also drives the D-pad directions (a deadzone), triggers count as buttons (a threshold) - as the
  generic HID path does for a hat or axes.
- **P5 Nothing else moves:** HID pads, keyboards, mice and the USB stick keep working; a keyboard + XInput pad together works.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | The driver claims class FF/5D/01 and FF/47/D0 interfaces only while a pad sink is set (console core), never without one (other cores, host); ignores other vendor classes (a USB stick's, an audio interface's) | native test of the driver's match function with descriptor fixtures | apptests |
| S2 | P3, P4 | A recorded 360 report and a GIP 0x20 message give the expected buttons, D-pad (pad and stick past the deadzone), triggers past the threshold | native test against the pad service, report fixtures | apptests |
| S3 | P3 | The pad appears in the core menu with its product name; F8 mapping capture takes an XInput button; two XInput pads = player 1 and 2 | core app native test through the same calls the driver makes | apptests |
| S4 | P2 | GIP: the power-on packet is written once after the claim; announce/guide messages are acknowledged; 360: the LED packet sets the player ring | native test of the init sequence against a model endpoint | apptests |
| S5 | P1-P5 | Board: Alex's Xbox Series controller (GIP) on a USB cable in the NES, SNES and Neo Geo core plays; a HID pad and a keyboard still work beside it | by hand | board (Alex) |
| S6 | P1-P5 | A third-party pad in X mode (T50, 360 protocol) plays | by hand | tester (#55) |
| S7 | P1, P5 | Diag measurement of a GIP pad (issue #55: presses missed): the diag core runs `xpad20`, `xpad4`, `xpad4noack` with a Series pad and shows VID:PID, protocol, bInterval, programmed interval, packets per command, ack bit / acks queued / OUT ok / failed, 0x20 sequence gaps and press/release edges per D-pad direction, A, B; it tells hypothesis A (20 ms polling loses reports) from B (ack/OUT path disturbs input) | board photos of the counters, 20 taps per button | board (Alex / tester) |

## §3 Decision

- **D1 (proposed):** a second USB driver beside `UsbHidDriver`, `UsbXinputDriver` (new file in our USB copy, registered through the same
  `FactoryRegistrator`), claims the vendor-class interface, runs the start sequence and polls the IN endpoint. It hands the pad to the pad service
  through the existing calls (`pad_attach` / `pad_report` / `pad_detach`) with a **synthetic HID report descriptor** that describes the XInput
  report as buttons, a hat and axes: `hidpad_parse` reads it like any HID pad, so P3 (naming, mapping, capture, players, REST) needs no change in
  `pad.cc`. GIP messages other than 0x20 are not passed on.
- Lost: a special XInput branch inside `pad.cc` (the patch proposed in #55): two report paths in the service instead of one.
- **D2 (Alex, 2026-11-09, translated: "I only have XBO Series, unfortunately no Xbox 360 devices any more"):** both protocols in this todo. GIP
  (Xbox One / Series) is the one Alex proves on the board (S5); the 360 protocol stays in for the X-mode pads of #55 and is proven by the tester (S6).
- **D3 (Alex, 2026-11-09, translated: "Only for the console cores!!!"):** XInput only in the console cores, today NES, SNES and Neo Geo (the cores
  with a `PadSpec`). The driver claims an XInput interface only when the running core app has a pad service (`usb_hid_set_pad_sink` set, as
  `pad.cc` does for those cores); in every other core (C64, C128, Amiga, ...) and in the host app the interface stays unclaimed, as today.
- **D4 (Alex, 2026-11-09, translated: "then we build ONE module for this stuff as an option to the app and solve it through the core + app
  capabilities"):** the pad input (the XInput driver beside the HID pad path) is one module of the core app, switched on by capability, not by a
  list of cores: a core that has a pad input names the capability `pad` in its descriptor's `caps` (`desc.h`, T104.1 Q10, `desc_has_capability`),
  and the module attaches only then. NES, SNES and Neo Geo get `pad` (D3 holds as the result); the Amiga gets it the day its gateware has a pad
  input (§4). Whether the module is a loadable library (T104) or compiled in and gated by the capability is decided while building, by size.

## §4 Out

- Wireless: the board has no Bluetooth; the Xbox wireless receiver (a separate USB protocol) is not part of this.
- Rumble.
- The T50's S-mode reconnect loop (a HID pad that drops) - a separate issue if it shows on our board.
- The host app and every core that is not a console core (D3): no XInput there, also not as a joystick.
- The Amiga cores (Alex, 2026-11-09, translated: "At most the Amiga too, instead of a CD32 pad ... we should consider that once we know how many
  LUTs it eats"): considered after this todo. The XInput driver itself is RISC-V code and costs no LUTs; the cost is on the Amiga's side, a pad
  input into the core (a `PadSpec` register pair and the CD32 pad's shift protocol on the joystick port, as far as the core does not have it).
  That LUT figure is measured first, then decided with Alex.

## §5 Decisions taken while building

- **Where the code lives (deviates from D1's "in our USB copy"):** `tools/hostapp_vendor.py copy` deletes every folder of `host-app/software/` and
  rebuilds it from `vendor.list`, so a new file in `io/usb/` would vanish at the next vendor run. The pure protocol is `coreapp/src/xinput.{h,cc}` (no
  USB, tested natively), the USB driver `coreapp/src/usb_xinput.{h,cc}` (class `UsbXinputDriver`, registered with the same `FactoryRegistrator` on
  `UsbInterface::getUsbDriverFactory()`), both in the coreapp Makefile only; the host app does not link them (D3 holds by construction there).
  `usb_hid.cc` / `usb_hid.h` are not touched.
- **Capability gate (D3, D4):** `run.cc` calls `usb_xinput_set_sink(&pad_sink, desc_has_capability(desc, "pad"))`; `test_driver` returns null unless both
  hold (`xinput_claim`). `pad` added to the caps of NES, SNES, Neo Geo (the three descriptors with a `PadSpec`); the test checks `pad` == has a `PadSpec`
  for the linked descriptors. D4's "loadable library or compiled in": compiled in, gated by the capability (about 1 KB of code; no library
  machinery for it).
- **Synthetic report (D1):** 5 bytes, 16 buttons + hat + X/Y (xinput.cc `synth_desc`). Button numbers = the generic numbering of pad.h: 1 A, 2 B, 3 X,
  4 Y, 5 LB, 6 RB, 7 Back, 8 Start, 9 L3, 10 R3, 11 Guide, 12 LT, 13 RT. A pad that enumerates as an Xbox pad therefore needs no `PadNumbering` entry.
  The left stick goes through X/Y (hidpad's 40 % deadzone), the D-pad through the hat (opposite directions cancel), triggers are buttons from 25 %
  (360: 64 of 255, GIP: 256 of 1023). The right stick is not used.
- **Output writes are queued:** the IN callback runs in the USB input task, which must not wait for the USB engine; the session's writes (power-on, LED,
  acks) go into a 4-entry queue that `install()` and `poll()` send with `UsbBase::bulk_out` on the interrupt OUT endpoint.
- **Byte values NOT verified on a pad (from the public descriptions named in xinput.h, to be proven at S5/S6):** the 360 LED pattern `0x06 + player`
  (xpad.c names 0x06-0x09 as "LED 1-4 on"); the GIP acknowledge layout `01 20 <seq> 09 00 <cmd> <opt without ack bit> <payload len lo> <hi> 00 00 00 00`;
  that the Series controller needs nothing beyond `05 20 00 01 00`; that `bulk_out` works on an interrupt OUT endpoint (it only builds an OUT
  transaction with the pipe's split control; nothing in `usb_base.cc` is bulk-specific, but no board has run it). A pad that needs more init packets
  (some third-party GIP pads do) is not handled.
- **360 start = `ukp.s xinput_init` (vendor/icepi):** the LED packet `01 03 02` on the OUT endpoint, then the vendor control read `C1 01 0100 0000 0014`
  (reply ignored; "some third-party pads need it", per its comment citing xpad.c). Both are sent (the control read in `install()` of the driver, setup
  bytes `xinput_360_magic`, tested as a constant only); the LED value is `0x06 + player` instead of icepi's 0x02 (steady lamp for the player; unproven).
- **Checked against Linux `drivers/input/joystick/xpad.c` (master, 2026-11-10, coordinator):** the GIP power-on `05 20 00 01 00`, the acknowledge layout (`xpadone_ack_mode_report`: `01 20 <seq> 09 00 07 20 02 00 00 00 00 00` for the guide message, the general form above), the 0x20 offsets (buttons bytes 4-5, triggers 6/8 LE16, left stick 10/12, Y inverted by xpad), the 360 LED commands `01 03 06..09` = player 1-4 steady: all as built. Added from it: xpad sends every One/Series pad (vendor 0x0000 entries of `xboxone_init_packets`) also the guide LED on `0A 20 <n> 03 00 01 14` and authentication done `06 20 <n> 02 01 00`, byte 2 counting up per packet; `XiSession::start` now sends all three. Still to prove on the board (S5): that the Series pad answers with input after these three.
- **Not done here:** nothing of S5/S6; no board run; `tools/dev.sh emu` not run (no screen changes).
- **Board pass S5 (2026-11-10):** Alex: "Xbox Series Pro controller via USB works 1A" (translated), on a USB cable. Build: branch
  `t196-xinput` 1035cb05a with main merged (0e320374f), core app packed with the `main-038d80c1` mono bitstreams into `nes.cor`,
  `snes.cor`, `neogeo.cor` (`tools/pack_large_cor.sh`, read back identical on the SD). Not checked separately: a HID pad and a keyboard beside it.

- **D9 (Alex, 2026-11-15, translated: "the measurement into the diag core, as an extra test, before we build any big gateware"):** a diag test file with
  `kind = "xpad"` (`[test]`: `name`, `id`, `version`, `poll_ms` default 20, `ack_input` default true) sets the XInput driver's poll interval and whether a 0x20
  message with the ack bit is acknowledged, shows the driver's counters once a second (dialog rebuilt every second; YES = finish and write
  `results/<id>_<stamp>.toml/.txt`, NO = zero the counters, ESC = stop) and puts the production values back (20 ms, ack on) when it ends. The pad is claimed with the
  interval that is set when it is plugged in, so the tester plugs the pad in after starting the test (the screen shows the programmed interval beside the wanted
  one). Shipped files: `xpad20` (20 ms, as production), `xpad4` (4 ms, the pad's own bInterval), `xpad4noack` (4 ms, no ack of 0x20; separates B from A). The
  diag core names the capability `pad` (no PadSpec: the pad service drops the reports) so the driver claims the pad there. Production in the other cores is unchanged.
- **Built (D9):** `XiStats`/`XiCfg` in `xinput.h/.cc` (counters in `XiSession::data`, the driver's callbacks and OUT writes in `usb_xinput.cc`, edges in
  `xi_stats_report`), `run_xpadtest` in `diagtest.cc`, hooks in `DiagEnv` wired in `portdiag.cc`. The OUT write is counted ok only when `bulk_out` returned the full
  length (it returns the bytes sent, 0 on a failure; the old `< 0` check in `flush()` never fires and is left as it was) and its longest duration is shown. A sequence
  gap is counted from the byte d[2] of 0x20 messages; 255 -> 1 is not counted (a pad skipping 0 on the wrap is not known). Interval units: 125 us (`poll_ms * 8`).
  No bench run (Alex 2026-11-15: no benches before a merge); proof is the RISC-V build and the board step S7.
- **Board step S7:** copy nothing: the diag core writes the three files to `/SD/cores/diag/tests/` at its start. Start the diag core, run `xpad20` from the file
  browser, then plug the Series pad in, tap Up 20 times slowly (also Down, Left, Right, A, B), photograph the screen, YES. The same with `xpad4`, then `xpad4noack`.
  Reading: press = release = 20 and no sequence gaps with 4 ms but fewer with 20 ms = A; losses also at 4 ms, or OUT failed / slow, or losses vanish with
  `xpad4noack` = B.
