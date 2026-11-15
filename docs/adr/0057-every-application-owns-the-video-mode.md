# 0057. Every application owns the video mode, not only the host

Date: 2026-10-16

Status: Accepted (extends [T105](../specs/done/T105-video-mode.md); the work is [T107](../todo/T107.md))

## Context

T105 gave the **host** application a video mode: asked of the screen over EDID, kept in `boot.cfg`, stepped
with the V key. A core inherits it through `Handover::video` and never touches it again.

That is too little, and the NES showed why. Measured on the board on 2026-10-16 (Zelda, Alex's photo):
the NES core draws 256 x 224 at the pixel layer's **largest possible** scale, 3 (`pix_layer.v:63`,
"b5:4 scale S (0 and 1: 1; 2; 3)" - the field is two bits wide). That is 768 x 672, and in a 1920 x 1080
frame it fills 40 % of the width and 62 % of the height. The rest is border.

Scaling further is a gateware change in a pinned host. Choosing a different output mode is not:

| mode | fill |
|---|---|
| 1080p 1920 x 1080 | 40 % x 62 % |
| **720p 1280 x 720** | 60 % x **93 %** |
| 576p 720 x 576 (scale 2 only) | 71 % x 78 % |
| 480p 720 x 480 (scale 2 only) | 71 % x 93 % |

The side border stays either way: the NES is 4:3 content and the screen is 16:9. The vertical border is
the part that is ours to fix.

Alex, asked whether the host should set the mode for the core or the core should set it itself:
"Moment .. dann sollten aber doch HOST und CORE apps jeweils den Modus wählen dürfen. DAs machen die
Gideon-based U1541FW apps doch auch so." (translated: then host and core applications should each be
allowed to choose the mode - that is what the Gideon-based U1541FW applications do too.)

He is right, and the base says so: in Gideon's application the mode is a setting of the **running
application**, "HDMI Scan Resolution" (`vendor/gideon/software/u64/u64_config.cc:391`), applied live when
it changes (`:1369-1380`). The boot loader does not decide it; whoever is running does.

Setting a mode is register work that needs no gateware: `SetVideoPll`, `SetVideoMode1080p` and
`U64_HDMI_REG` (`host-app/software/hostapp/videomode.cc:149-152`), which the V key already drives at run
time.

## Decision

**Every application owns the video mode while it runs** - the host application and every core
application alike.

- The mode code becomes shared: `videomode.{h,cc}` moves out of `host-app/software/hostapp/` into the
  application library under `app/`, where both binaries link it. It keeps the EDID path of T105.
- A core application comes up on the mode the host handed it, then applies **its own** choice: its
  setting if the user made one, otherwise the mode that fits its picture best. It may change the mode
  again at run time (an Amiga that switches PAL/NTSC or interlace needs exactly that).
- A core declares what it draws in its bundle - native size and preferred scale - so the first choice
  needs no code in the core. This is the line of [ADR-0056](0056-amiga-kickstart-per-chipset-no-disk.md):
  a property of the bundle, not a module.
- **The host re-asserts its own mode whenever a core returns to it**, whatever the core left behind. A
  crashed core must never be able to leave the board on a mode the host did not choose - that is the same
  rescue property T105 was written for.

## Consequences

- Pure application work. No host gateware change, so no new bitstream: the registers exist and are driven
  today. Changes to a core's choice ride in its `.cor`, not in a `.cfw`.
- Two writers to a host-owned resource. The rule above is what keeps that safe, and it has to be in a
  bench: the host's mode after a core returns is the host's.
- The NES goes to 720p and fills the height. The Atari and the C16 get the same treatment from their own
  numbers; the Amiga keeps 1080p until someone measures better.
- The 3 x ceiling of the pixel layer stays. Anything that needs more is a gateware task and a separate
  decision ([T105](../specs/done/T105-video-mode.md) §4 keeps it out).

## Correction (2026-10-20)

The premise that a smaller HDMI mode gives a picture more area does not hold on the host in this repository: its output is fixed
1920 x 1080 (`host/vic_scaler.v`, `host/overlay.v:8`). Applications still own the mode setting, but the layout is the 1080p layout in
every mode. See [T107 5.6](../specs/done/T107-application-owns-the-picture.md).

## Links

- [T105](../specs/done/T105-video-mode.md), [T107](../todo/T107.md), [T104](../specs/done/T104-generic-core-app.md),
  [ADR-0056](0056-amiga-kickstart-per-chipset-no-disk.md)
