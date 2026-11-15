# 0060. Modes below 1080p repeat TMDS bits, not pixels

Date: 2026-10-31

Status: Proposed (the method is this design's; Alex's go of 2026-10-31 covers HDMI in the host; the method is proven only on the bench until the Sony / Samsung board step)

## Context

Our GTP transmitters need a line rate of at least 0.8 Gb/s (the wizard's PLL: N = 20, M = 1, D = 4, REFCLK 80-165 MHz; `host/build.tcl:83-96`),
while 480p / 576p have a 27 MHz pixel clock (270 Mb/s of TMDS data). The A1200 trial of 2026-10-29 (T107 §5.8) solved it with **pixel repetition**
("gear" 2 for 720p, 3 for 800 x 600, 5 for 480p / 576p) on a TMDS clock g times the pixel clock, with PR = g - 1 in the AVI InfoFrame.

Board, Sony TV, 2026-10-31 (Alex; T107 §5): 576p, 720p, 800 x 600, 1024 x 768 and 1280 x 1024 "signal not supported", 1080p fine; Gideon 3.15a on the same
TV shows 1080p, 576p and 720p. Cause, read from `cores/amiga1200/mono/hdmi_timing.v:5-7`, `top.v:1440-1470`, `hdmi_modes.vh`: CEA-861 allows no pixel
repetition for VIC 4 / 19 (720p) and none of factor 5 for VIC 17 (576p); the sink sees 2560 x 720 and refuses. The Samsung monitor took 720p anyway.
Gideon changes the TMDS clock itself (74.25 / 27 MHz).

## Decision

The TMDS clock on the wire is the pixel clock, PR = 0. The GTP line rate stays `10 x refclk` (0.92-1.49 Gb/s in the 12 rows of the A1200 table), and
`host/tmds_rep.v` repeats each TMDS **bit** g times in the serializer path: in `pix_clk` cycle t of a pixel the 10 line bits j = 10 t + k carry symbol bit
floor(j / g). The clock lane goes through the same repeater with the symbol `0000011111`. `pix_clk` and the pixel enable `ce` stay as in the A1200; the
encoder's counters return to pixel units.

## Consequences

- 720p is 74.25 MHz and 576p 27 MHz on the wire, the form the Sony accepts from Gideon's firmware. No VIC code needs a repetition the standard forbids.
- About 140 LUT for the repeater (estimate); the A1200's `g * cx` position logic and the PR fields leave the encoder fork.
- Audio N / CTS count the pixel clock; `MAX_PACKETS` falls to 3 (480p has 138 blanking pixels).
- Not provable offline: how a sink's receiver takes a 1.35 Gb/s transmitter holding every bit for five bit times. The board step decides; if a sink
  refuses, the fallback is to lower the TMDS clock per mode (Gideon's route, a PLL setting per mode), which the table can express.
- The GTP's own minimum line rate is not stated in this repository; the check is added to `tools/hdmi_modes.py` from DS181 before the build.

## Links

[T164](../specs/done/T164-host-package-2.md) S3, S4, §3b G3-G4, [T107](../specs/done/T107-application-owns-the-picture.md) §5, §5.8
