# Measuring the A/V port with the diag core (NTSC, S-Video, composite)

For a tester and the assistant that helps them. Issue: Jondalar/TRX64U-MultiCore#73. Spec: `docs/specs/T259-diag-av-port-measured.md`.

The C64U's A/V port has a triple video DAC (channels A, B, C) and two pins, SYNCn and FB. The MultiCore cores put out an
analogue picture only where we know how the board is wired. This core lets you find out, and tune the values, on your TV.

**Known (Alex's Sony, PAL, C64U to SCART RGB cable, 2026-11-26):** RGB works with the Names map (A = R, B = G, C = B), MSB first
on all three channels, SYNCn = Csync and FB = High. The preset `Sony-RGB` holds these values.

**Wanted from you:** NTSC on your 1702 through S-Video (luma, chroma) and composite: which channel carries what, the bit order,
and the values that give a stable colour picture.

This is a development build. The core has only run on Alex's board.

## 1. What you need

- A C64U with the MultiCore host installed, and the HDMI screen for the menu.
- Your NTSC monitor (Commodore 1702: luma and chroma inputs, composite input) and the C64 A/V cable.
- Optional: a C64U to SCART RGB cable and an RGB monitor; a monitor with component (Y/Pb/Pr) input.

## 2. Copy the files

The core looks for its files in a folder next to the `.cor` with the same name (`diag.cor` -> `cores/diag/`).

1. Copy `diag.cor` to `/SD/cores/` (or `/USB0/cores/`).
2. Copy the folder `SD/cores/diag/` of this zip to `/SD/cores/diag/` (or `/USB0/cores/diag/`), FTP or card.
3. Check: `cores/diag/tests/avprobe.toml`, `cores/diag/presets/NTSC-60.rix02.toml` and `cores/diag/panels/avout.toml` exist.

## 3. Start the core

1. In the host menu: F3, the medium, `cores`, `diag.cor`, S.
2. The core's menu opens (if not: F10). F1 Home shows the tasks "Pick a test and run it" and "Open a panel".

## 4. The channel probe (which channel is luma, which is composite)

1. Connect the 1702's **luma** input only.
2. "Pick a test and run it", `avprobe.toml`. Six steps: map Names, then Scan groups, each with channel A, B, C.
3. Each step asks: "Does the TV show a steady picture?" Answer Y or N. After Y: "Do the bars get brighter from left to right?"
4. Do it again with the **composite** input. (Chroma alone carries no sync; the probe cannot find it.)
5. The result is in `cores/diag/results/avprobe_<date>.txt`: the steps the TV locked on, the channel and the bit order.

Note: each question waits 30 s, then counts as no. If you cannot see the menu and the monitor at the same time, set
`timeout_s = 600` in `tests/avprobe.toml` on the card, or let your assistant answer over REST (section 8).

## 5. The playground (tune NTSC)

1. "Open a panel", `avout.toml`. LEFT / RIGHT change the selected row at once.
2. Load preset: `NTSC-60.rix02`.
3. Set Mode = "Test picture", Map and Groups to what the probe found (Groups names which channel carries Y, C and CVBS).
4. Look at the colour bars on S-Video and on composite. Change one row at a time: Subcarrier trim, V sign, Burst start,
   Burst end, Burst level, Chroma gain; then Sync level, Blank level, White level if the picture rolls or is too dark.
5. When the colours are stable and right: Save preset, give it a name (e.g. `1702-NTSC`).
6. If the 1702 also takes 50 Hz: the same with `NTSC-50.rix01`.

## 6. Optional: more than NTSC

- RGB cable and RGB monitor: run `avrgb.toml` (it sets SYNCn Csync and FB High), or load preset `Sony-RGB`.
- `avsync.toml`: what SYNCn and FB do on your input (32 steps; the answer per step is what the TV does).
- Component (Y/Pb/Pr) is not built in the core yet. Tell us if your monitor has that input; then it can be added.

## 7. What to send back (attach to issue #73)

- Everything in `cores/diag/results/` (`.txt` and `.toml` of each walk).
- Your saved presets from `cores/diag/presets/`.
- Photos of the picture with the preset you saved.
- Your monitor and cables, and which inputs you used.

## 8. For the assistant: driving the menu over REST

The core's menu answers on the board's network address (port 80, the core app; WiFi or LAN):

```
GET  /v1/ui/hello                      -> {"grant":true} when no token exists yet
POST /v1/ui/grant                      -> {"token":"..."} (also written to cores/diag/ui.token)
GET  /v1/ui/state                      header X-Trx-Token: the screen, the rows, "appdlg" (the open dialog), "dialog" (a question)
POST /v1/ui/intent                     header X-Trx-Token, JSON body:
     {"do":"key","key":"return"}       keys as named in the menu: up, down, right, return, f1, single characters
     {"do":"select","item":"<id>"}     an item id from the state
     {"do":"answer","q":"<q>","choice":"yes"}   answer the open question ("q" from the state's "dialog")
```

The person at the monitor says what they see; the assistant answers. A key pressed on the board's keyboard always wins.

## 9. Do not

- Do not run a firmware updater inside a firmware that the MultiCore host started (Commodore, Gideon). It overwrites the
  host application and the board no longer starts (issue #151).
