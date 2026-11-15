# T225 — In the Amiga cores a USB gamepad can stand in for Amiga game port 2, as a one-button joystick, a two-button joystick or a CD32 pad, with a saved per-pad button mapping

**Status:** PROPOSED (2026-11-15, Alex).
**Repo:** TRX64U-MultiCore — gateware of `cores/amiga` and `cores/amiga1200` (minimig `userio.v`: CD32 pad enable and the pad bits
into `_joy2`, one register the app writes), the core app (pad service, Input tab of both Amiga cores, the mapping dialog, saving).
**Number:** T225.
**Origin:** GitHub #22 (CD32 pad on the C64U joystick port): proven impossible on 2026-11-15 (pins 5/9 are no FPGA pins, T175),
#22 closed. Alex 2026-11-15 (translated): "can we give the Amiga cores the USB pad as a replacement", then: "a menu option 'map
Port 2 to USB controller' on the Input tab, then one chooses what to map: source is the found gamepad (controller), target
Joystick 1 button, Joystick 2 button, CD32 gamepad, and a dialog to wire the buttons. The result must be saved, i.e. the ID of the
gamepad (whatever USB port) and the mapping; remap = save again."
**Depends on:** T156/T141/T181 pad service and mapping (as built), T196 (XInput pads).
**Milestone:** M2.

---

## §1 The problem

- P1: the Amiga cores take no USB gamepad at all; game port 2 comes only from the C64U joystick port (`_joy2`).
- P2: CD32 games need the CD32 pad protocol (7 buttons shifted out over pins 5/9); the C64U port cannot do it (T175), and the minimig
  logic for it is switched off (`cores/amiga/rtl/src/minimig-aga/userio.v:91` `wire cd32pad = 1'b0`).
- P3: the user must choose per pad what it is to the Amiga (1-button, 2-button or CD32) and wire its buttons, and find it again
  next time with the same pad on any USB socket.

## §2 Scenarios

- S1 Input tab of `amiga` and `amiga1200`: row "Map port 2 to USB controller" (Off / On); with On, the found pad as source and the
  target type (Joystick 1 button / Joystick 2 buttons / CD32 gamepad). Proof: ue2emu pictures.
- S2 mapping dialog: wire each target button (and directions) to a pad button; Proof: ue2emu pictures.
- S3 saving: pad ID (VID:PID, independent of the USB socket) + target type + mapping stored; after a restart the same pad gets the same
  mapping on any socket; remapping overwrites it. Proof: ue2emu restart picture, board.
- S4 gateware: with the mapping On, the core's port 2 reads only the USB pad (the C64U joystick port is off for port 2, D1); in CD32 mode the minimig CD32 shift logic gives
  the 7 buttons. Proof: Vivado green both cores; board.
- S5 board: a CD32 game reads all 7 buttons from a USB pad (Xbox pad); a 1-button and a 2-button game work with the same pad.

## §3 Decision

As Alex said in "Origin".

- D1 (Alex 2026-11-15, asked: should a real joystick on the C64U socket still work for port 2 while port 2 is mapped to the USB
  pad? Answer: "Off"): mapping On = port 2 comes from the USB pad only; mapping Off = port 2 from the C64U joystick port as today.
- D2: where the mapping is stored follows the existing pad-mapping storage of the pad service (T156/T181), keyed by the pad's
  VID:PID; the builder reports in §5 where that is.

## §4 Out

- Port 1 (mouse port) stays as it is.

## §5 Built
