# T196 — An Xbox-style (XInput) controller on a USB cable works in the console cores (NES, SNES, Neo Geo), as a HID gamepad does

**Status:** PROPOSED (2026-11-09, Alex).
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

(empty)
