# T105 — the picture a board comes up in is not ours to decide

**Status:** IN PROGRESS (2026-10-16). Asked for by Alex on 2026-10-16 after a tester outside this 2026-11-13 status check: OVERTAKEN by T107 (the host stays at 1080p, each core picks its mode); mode choice moved to F8 (T139). Row and todo say dropped.
house installed `trx64mc-main-51e89821` and got a black screen: "Wieso bootet unser Test Host nicht
hoch bei jemand anderem?"

---

## §1 The problem

**Who:** anyone who installs a MultiCore bundle on a board that is not Alex's.

**What happened.** The host application and the core application each set the scan mode with two
hard-coded values:

```c
SetVideoPll(e_PAL_50, 0);
SetVideoMode1080p(e_PAL_50, e_1920x1080);     // hostapp.cc:47-50, coreapp.cc:29-35
```

1920 x 1080 at 50 Hz is CEA mode 31. Televisions usually accept it. PC monitors over HDMI often list
no 50 Hz mode at all and show nothing. The tester saw exactly that: ethernet lights on, the power LED
answering shift-lock, and a black screen on every key he pressed. Another HDMI screen was the whole
fix, which is also the measurement that closed it.

**Two things made it worse than a wrong default.**

- **There was no way out.** Gideon's firmware has `CFG_SYSTEM_MODE` and `CFG_HDMI_RESOLUTION` as
  settings and defaults to 480p/576p (`u64_config.cc:1124`). Ours had neither a setting nor a key.
- **A dark board cannot be told anything.** Our file browser lists only `.cor`, so the updater cannot
  even be started to install a cure, and the boot menu is empty after a fresh install by decision
  (T84: the menu belongs to the user), so nothing starts by itself either.

**And one fact that is not a bug but has to be written down:** this bitstream has **no analog video
pins at all**. `host/top.v` has 52 ports, none of them composite, S-video or a DAC, and
`TRX64-Ultimate/designs/c64_basic/top.v` is the same. The pixel layer the menus draw on sits in
scaler → overlay → `pix_layer` → `c64_text` → HDMI encoder (`host/top.v:1401-1420`). A 1702 or any
other CRT will never show a TRX64 menu, with any core.

**Parts (MECE):**
- **P1 Choose:** a board with no stored choice comes up in a mode the attached screen can show.
- **P2 Keep:** a choice the user made survives a power cycle and reaches the core application.
- **P3 Escape:** a screen that shows nothing can still be changed, blind.
- **P4 Say so:** the release notes name the limit and the way out.

## §2 Scenarios

```gherkin
Scenario: S1 a screen that lists no 50 Hz mode is not given one (P1)
  Given an EDID whose CEA block lists VIC 16, 4 and 3 and no 50 Hz mode
  When  the chooser is asked for a PAL machine
  Then  it does not answer 1080p
  And   the same EDID for an NTSC machine answers 1080p
  Bench: tests/hostapp/test_videomode.cc; control vm_always1080 (a chooser that always says 1080p)

Scenario: S2 a monitor without a CEA block gets a VESA mode (P1)
  Given an EDID with no extension block
  Then  the chooser answers 1024 x 768, not 576p/480p
  Bench: tests/hostapp/test_videomode.cc

Scenario: S3 nothing answered on the DDC lines (P1)
  Given no EDID at all, or 256 bytes of zeroes
  Then  the chooser answers 576p/480p, the mode a television least often refuses
  Bench: tests/hostapp/test_videomode.cc

Scenario: S4 the choice survives and travels (P2)
  Given boot.cfg holds video=1,4
  When  the host application starts and then starts a core
  Then  the host applies NTSC 60 Hz on 1024 x 768
  And   the hand-over carries the same pair into the core application
  Bench: tests/hostapp/test_bootcfg.cc (round trip), tests/coreapp/test_handover.cc (the field)

Scenario: S5 the way back from a dark screen (P3)
  Given the boot menu is up in a mode the screen cannot show
  When  V is pressed
  Then  the next resolution is applied at once and written to boot.cfg
  And   pressing it VID_HDMI_COUNT times comes back to where it started
  Bench: tests/hostapp/render/render.cc

Scenario: S6 the release says it (P4)
  Then  INSTALL.md names the HDMI-only limit, the EDID choice, the V key and the boot.cfg line
  Bench: tools/test_release_notes.py
```

## §3 Decision

- **D1 Ask the screen, do not guess.** `video_mode_from_edid()` reads the CEA-861 Video Data Block and
  picks the best mode we can drive that the screen lists: 1080p, else 720p, else 576p/480p. An EDID
  without a CEA block is a DVI/PC monitor and gets 1024 x 768. No EDID at all gets 576p/480p.
- **D2 The choice lives in `Flash:/boot.cfg`** as `video=<system>,<hdmi>`, beside the menu it belongs
  to - one file the user already owns, reachable over FTP, and no second store to keep in step.
- **D3 The hand-over carries it** (`Handover::video`, what was `reserved`; 0 = nobody chose). A core
  must not come up in a different mode from the menu the user just used. The field is packed so that
  an old hand-over reads as "not chosen" and the core asks the screen itself.
- **D4 One blind key.** `V` in the host menu steps through the resolutions, applies each at once and
  saves it. It is in the footer legend and in INSTALL.md.
- **D5 The limit is documented, not worked around.** There is no analog video to add in software.

## §4 Out

- **Analog video.** It is not in the bitstream and this spec does not add it.
- **The core application gets no V key.** It takes the mode from the hand-over; the place to fix a
  picture is the host menu, which is where the user comes from.
- **Automatic PAL/NTSC.** The system mode stays the user's; only the resolution is chosen for them.
- **Authentication for FTP or any later web interface.** The FTP server is already open on a private
  network; that is a separate decision, not this one.

## §5 Decisions taken while building

(filled while building)

- **2026-10-20:** the mode this spec chooses from the screen is only a request on this host: its output is fixed 1920 x 1080
  (`host/overlay.v:8`, `host/vic_scaler.v`), so a screen that takes only 576p still gets 1080p. Applications therefore lay out for
  1080p in every mode ([T107 5.6](T107-application-owns-the-picture.md)); whether a real 576p / 720p output is wanted is open.
