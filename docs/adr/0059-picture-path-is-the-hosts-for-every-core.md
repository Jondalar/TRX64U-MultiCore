# 0059. The picture path from the sources to the pins is the host's, for every core

Date: 2026-10-31

Status: Accepted (Alex, 2026-10-31, [T107](../specs/T107-application-owns-the-picture.md) D11 and [T164](../specs/T164-host-package-2.md) D6/D7; extends [ADR-0057](0057-every-application-owns-the-video-mode.md))

## Context

ADR-0057 gave every application the choice of the video mode. On this host the choice changes nothing on the screen: the timing is wired
into `host/vic_scaler.v:99-108`, `host/overlay.v:8` ignores the mode registers, the CDCE writes of an application are swallowed in
`host/app_i2c.v`, and the pixel layer works in a 1920 x 1080 frame (T107 §5.6). Only the A1200 large core has modes and a scaler, as its own copies
of five host files (`cores/amiga1200/mono/{top,overlay,pix_layer,bling_io}.v`, `hdmi/*`); the C128 carries a diff of `top.v`.

A DFX core cannot own the pixel path: pins, GTP, clock buffers and MMCMs stay static (`host/build.tcl:211-216`), and the contract has no pixel clock
(`contract/layer2_ports.txt`). A scaler inside each core would be written, benched and routed N times in `pb_l2` (T107 §5.7: 135-290 LUT per core for
nearest sampling) and would need a pixel-clock input and about 30 timing ports, a contract bump.

Alex, 2026-10-29 (translated): "No, sorry. When I change it at the board, the output has to change ... otherwise I do not need the parameters.
And it works, see Gideon." And 2026-10-31: "you could build the HDMI + scaler topic into the host and generalise it so that all cores have the gateware
(via the host, or as an include in large cores)." Then: "I want us to get to HDMI and the scaler in the host... it takes too long for me because
too much hangs on it."

## Decision

Layer 1 owns everything between the picture sources and the HDMI pins: the mode timing generator, the CDCE sequence, `mmcm_pix`, the pixel layer
with the scaler, the menu overlay, the freeze text, the encoder, the TMDS repeater, the GTP. A core delivers a picture and nothing else: a DDR2 frame
buffer (the pixel layer's registers set by its application) or the VIC raster (`pix_*`). An application, the host's or a core's, asks for a mode by one
register write (overlay window `0x4F00`) and reads one status register (`0x4F04`); the application, not the gateware, decides which modes exist
([ADR-0062](0062-the-screen-decides-which-modes-are-offered.md)).

## Consequences

- One host change with a new host key: every `.cor` is rebuilt, `contract/host_pin.txt` is re-pinned. The contract keeps its 247 ports.
- `pb_l1` grows by about 1,100 LUT ([ADR-0063](0063-pb-l1-grows-cores-that-do-not-fit-become-large.md)).
- The A1200 forks go ([ADR-0061](0061-one-video-out-for-the-host-and-the-large-cores.md)).
- The VIC raster path stays for C64-type cores; its modes are limited by the frame lock (T164 §3a Q4).

## Links

[T164](../specs/T164-host-package-2.md) §3b G1-G9, [T107](../specs/T107-application-owns-the-picture.md) §5.7, §5.8, [ADR-0057](0057-every-application-owns-the-video-mode.md)
