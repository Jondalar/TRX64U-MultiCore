# T177 — A key held at power-on starts a recovery image, even when the installed host shows no picture

**Status:** DECIDED (2026-11-02, Q1-Q4 answered by Alex); build not started.
**Repo:** TRX64U-MultiCore — the host boot application (`host-app/software/hostapp/`), possibly the boot ROM in the host bitstream
(`host/`), the installer/update tools (`tools/`), the manual.
**Number:** T177.
**Origin:** Alex, 2026-11-02, after tester release t164-c83a83c2 left the host menu black on testers' boards (installer carried
`c64_basic.bit`, see T164 history): "Spec: Recovery boot. When you switch on and press a key or combination, a recovery.cfw or
recovery.bit or so is booted." (translated)
**Depends on:** T135 (direct boot, C= / fire at power-on), T152 (update, flash slots), T137 (restore of the original firmware),
T49 (update packages).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a tester whose board runs a host that cannot show the menu (2026-11-02: host app `t164@c83a83c2` on a bitstream without
  the pixel layer; the log says `no pixel layer in this bitstream: the menu runs on the console only`).
- **What they want:** get back to a working firmware without a USB-Blaster, without a PC and without seeing anything on the screen.
- **What happens today:** without C= the default entry starts (here the old firmware, so the testers could reinstall from it). With
  C= the menu runs blind; Elrondo46 counted DOWN 11 times to start an entry he could not see. If the default entry were not a
  working firmware, or the host app itself did not start, only the Blaster is left (INSTALL.md "The way back").

Parts:
- **P1 Trigger.** A key or combination held at power-on that no one presses by accident and that differs from C= / fire (T135).
- **P2 What starts.** A recovery image: a file on the SD card or stick (e.g. `recovery.cfw` / `recovery.cor`), and/or the backup the
  installer wrote (`TRXBK_*.CFW`, the old-firmware `.cor`), and/or an image kept in the flash.
- **P3 Which layer decides.** The host app (works while the app starts, even with no picture), the boot ROM inside the host bitstream
  (works while the bitstream starts, even with a broken app), or the FPGA's own fallback (a second "golden" bitstream in the flash,
  works even with a broken bitstream at 0x0).
- **P4 Blind use.** No screen needed: a fixed key, a fixed file name, and feedback that does not need HDMI (case LED pattern, the
  1541-style drive LED, a sound).
- **P5 Safety.** The recovery never writes the flash by itself unless that is what the image is (a `.cfw` run through the normal
  update with its questions answered blind is not safe; a `.cor` start writes nothing).

## §2 Scenarios (draft; each names its proof once §3 is decided)

- S1 Recovery key held at power-on, `recovery.cor` on the SD card: the core starts, no menu, nothing written. Proof: host app bench
  (key sample at start), board.
- S2 Recovery key held, no recovery file, an installer backup exists: the old firmware's `.cor` starts. Proof: bench, board.
- S3 Recovery key held, host bitstream without pixel layer (the 2026-11-02 case): S1/S2 still work. Proof: board with that bitstream.
- S4 Key not held: direct boot and C= as in T135, unchanged. Negative control: a bench where the recovery key is sampled as held
  must fail S4.
- S5 (only with a golden image, P3 third option): flash 0x0 holds a broken bitstream; power-on still reaches the recovery image.

## §3 Decision

- **D1 (Q1)** The host app decides, at start, before the T135 direct boot decision and without needing a picture.
- **D2 (Q2)** Trigger: RESTORE and F1 both held at power-on. RESTORE needs a read path for the app (see Q2 note).
- **D3 (Q3)** It starts `recovery.cor` (SD, then USB), else the boot.cfg entry marked `backup` (the installer's old firmware).
  It writes nothing to the flash; the started image runs with the T152 flash protection off.
- **D4 (Q4)** Feedback: the case LED blinks 3 short times before the image starts; with no image found it blinks a long-short
  pattern (to be fixed in the build) and the normal start goes on.

## §3a Open questions

- **Q1 Layer (P3):** host app only, boot ROM, or a golden bitstream in the flash (costs one bitstream slot, ~1.6 MB of the flash
  drive, and Xilinx MultiBoot / fallback configuration)?
  **Answered (Alex, 2026-11-02): "Host app."** The host app reads the recovery key at start, before the direct boot decision
  and before any picture is needed. Boot ROM and golden bitstream are not part of T177 (a broken app or bitstream still needs the Blaster).
- **Q2 Trigger (P1):** which key or combination (e.g. RUN/STOP + C=, fire on port 2, RESTORE)?
  **Answered (Alex, 2026-11-02): "RESTORE held + F1."** Both held at power-on. Note for the build: RESTORE is not in the keyboard
  matrix; in `host/top.v` it reaches `ultimate_sys` as `restore_pin` (`restore_cpu_s2` = pin 10 or the bling keyboard's
  `bl_restore`, l. 946). Whether the host app can already read it (a register of Gideon's set) is the first thing to establish; if
  not, one status bit is added (host gateware change, T94 host pin).
- **Q3 Image (P2):** a user file (`recovery.cor` / `recovery.cfw` by name, where), the installer's backup, or both in an order?
  **Answered (Alex, 2026-11-02): "c, and never flash. The stuff only has to come up, but then WITHOUT flash protection!"**
  (translated). Order: `recovery.cor` (SD, then USB) first; if none, the installer's backup of the old firmware (the boot.cfg entry
  marked `backup`). The recovery itself writes nothing to the flash. The image it starts runs with the T152 flash protection OFF,
  so that the started firmware can reinstall itself or another firmware through its own updater.
- **Q4 Feedback (P4):** what tells a user with a black screen that recovery started?
  **Answered (Alex, 2026-11-02): "a"** - the case LED blinks a fixed pattern (3 short blinks) before the image starts.

## §4 Out

- Boot ROM and golden-image recovery (Q1): a broken host app or host bitstream still needs the USB-Blaster.
- Writing the flash from the recovery, and `.cfw` images as recovery targets (Q3).

## §5 Build notes

Not started.
