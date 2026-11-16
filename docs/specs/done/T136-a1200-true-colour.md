# T136 — The A1200 shows AGA's full 24-bit colour on HDMI

**Status:** PROPOSED (2026-10-28), building on branch `t136-a1200-colour`. 2026-11-13 status check: DONE: S1-S5 passed (S5 measured on the board, S4 WNS +0.380 ns run 1017), merged to main (Merge t148-core-folder, 6dc840362).
**Repo:** TRX64U-MultiCore — the A1200 large core only (`cores/amiga1200/`), its own copy of the pixel layer
(`cores/amiga1200/mono/pix_layer.v`), the core application (`coreapp/`). The host is not touched.
**Number:** T136.
**Origin:** Alex, 2026-10-28, after the finding below: "Oha. Versuch den aga mal als branch mit den echten Farben. Das
testen wir nachher mit." ("Oha. Try the AGA as a branch with the real colours. We test that later along with the rest.")
**Depends on:** T97 (A1200 core), T85 (Amiga frame buffer), T93 D9 (video service of the core app).
**Milestone:** M3.

---

## §1 The problem

AGA has 24-bit colour registers (256 of 16.7 M colours, HAM8). The A1200 core throws most of that away before HDMI:

- `cores/amiga1200/rtl/nanomig.v:1361-1363`: `r_in = red[7:4]` (and green, blue): Lisa's 8 bits per channel are cut to 4.
- `cores/amiga1200/rtl/amiga_fb.v`: packs the 4 bits into RGB565 by repeating the top bits, so 12 bits reach the DDR2.
- `host/pix_layer.v` reads RGB565 only (`PX_CTRL` b3:1, format 1; every other value switches the layer off).

So the A1200 shows at most 4,096 colours; AGA gradients and HAM8 pictures show bands. The A500 core (OCS/ECS) is not
affected: 12 bits is its native depth.

- **P1** the chipset's 8 bits per channel reach the frame buffer.
- **P2** the pixel layer of the A1200 build shows a 32-bit pixel.
- **P3** the core application places and announces the bigger buffers.
- **P4** area and timing of the A1200 build.

## §2 Scenarios

```gherkin
Scenario: S1 the frame buffer holds 8 bits per channel
  Given the A1200 core's frame buffer bench with a picture whose colours have non-zero low nibbles
  When  a frame is written
  Then  every pixel word in the DDR2 model is {8'h00, R, G, B} with the chipset's 8-bit values
  And   control: the 4-bit cut put back -> the low nibbles read 0 and the bench fails

Scenario: S2 the A1200's pixel layer shows format 2
  Given the pixel layer bench against cores/amiga1200/mono/pix_layer.v, format 2, alpha 255
  When  a frame of 32-bit pixels is scanned out
  Then  every output pixel equals the stored R, G, B exactly
  And   format 1 (RGB565) gives the same output as host/pix_layer.v (the host bench passes unchanged against the copy)

Scenario: S3 the core application asks for format 2
  Given the A1200 descriptor (4 bytes per pixel)
  When  the video service writes its set
  Then  PX_CTRL carries format 2, PX_STRIDE is 720 x 4, and the A500 descriptor still writes format 1 and 720 x 2

Scenario: S4 the A1200 build fits
  Given the A1200 mono build in CI
  Then  it routes with WNS >= 0 (report line "WNS mono amiga1200")

Scenario: S5 board: no bands
  Given the A1200 with an AGA picture that has a 256-step gradient (Workbench 3.1 backdrop or a copper gradient)
  When  shown on HDMI
  Then  the photo shows no 16-step bands; the same picture on the run 1016 build shows them
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | `cores/amiga/sim/run_fbwin.sh` (A1200 build of amiga_fb) | `ci/sims.txt` |
| S2 | P2 | pixel layer bench run against the A1200 copy | `ci/sims.txt` |
| S3 | P3 | `tests/coreapp` video test | CI app stage |
| S4 | P4 | Vivado report of the large stage | CI `large` |
| S5 | P1-P3 | photo at the board, by Alex | board session |

## §3 Decision

- **D1 Own copy of the pixel layer in the A1200 build** (Alex, 2026-10-28: "Lass uns mal mit a versuchen. Und falls es gut
  ist dann schauen testen wir es im Host auch. Haben nur gar keinen dfx Core mit soviel Farben." - "Let's try (a). If it is
  good, we look at testing it in the host too. We just have no DFX core with that many colours.").
  `cores/amiga1200/mono/pix_layer.v` is `host/pix_layer.v` plus format 2, recorded as `pix_layer.diff`
  (`tools/check_wrapper_fork.sh`, as the A1200's other copies). The host's file is unchanged.
- **D2 Format 2** = one little-endian 32-bit word per pixel, B in bits 7:0, G 15:8, R 23:16, bits 31:24 ignored. The line
  buffer doubles to 512 x 128 bit (two lines of up to 256 words; the width limit stays 960 pixels, 240 words in format 2;
  one more block RAM). The colour key is off in format 2.
- **D3 The buffers move out of the core's DDR2 window.** 3 x 720 x 540 x 4 bytes do not fit the 3 MB the window has for them
  (0x1D00000-0x1FFFFFF; the rest of the window is chip/fast RAM, Kickstart, floppy images, HDF staging). They go to DDR2
  0x4000000, 2 MB apart (0x4000000-0x45FFFFF; a power of two because `amiga_regs` takes the shown buffer from the address bits, FB_SHIFT 21). No code in this repository uses 0x4000000-0x4FFFFFF; T20 §5.1 marks it
  "for the ring later" in the C64 line, which does not run in the A1200 build. The application cannot reach it (its CPU
  sees 64 MB, its DDR2 window ends at 0x2000000), so it does not clear these buffers (`video.cc` already skips them).
- **D4 The core application** gets bytes per pixel in its video descriptor (2 default, 4 for the A1200) and writes format
  and stride from it.

## §4 Out

- The host's `pix_layer.v`. Decided 2026-10-28 after the board test (Alex, translated): "I agree, apart from the Amiga
  AGA core (and that one is large) I cannot think of a built core. It gets interesting again with SNES and Neo Geo. And
  maybe Atari ST." And: "Though those will rather be large cores too, I think." A large core brings its own copy
  (as the A1200). Not now; it comes back with the first DFX core that needs more than 16 bits per pixel
  (`docs/other-cores.md`).
- The A500 core (12 bits is OCS/ECS's depth).
- Analog video (discussed 2026-10-28, not decided).

## §5 Build notes

- 2026-10-28, branch `t136-a1200-colour`, benches on the builder (trx64-sim-env:4):
  - S1 `run_fbwin.sh`: "ok  : T136: the border colour has 8 bits per channel (0x000044)"; control NANOCUT=1 (top 4 bits
    only): "FAIL: T136: the border colour has 8 bits per channel (0x000044)", as required. The bench's DDR2 model grew to
    64 MB so the window and the buffers at 0x4000000 do not alias.
  - S2 `cores/amiga1200/sim/run_pixlayer32.sh`: "format 1: the copy prints the host's 15 hashes", "format 2: PASS"; (retired 2026-10-29 by T107 D8: the fork no longer prints the host's hashes; `run_pixscale.sh` checks both formats against a model)
    control (format 2 widened from 16 bits): "FAIL frame 2 pixel (2, 0): 000429, want 010025", as required.
  - S3 `tests/coreapp` test_video: 91 checks, 0 failed; controls video_bpp_stride and video_bpp_fmt fail as required.
  - Block RAM: the last A1200 report before this (run 997) used 46.5 of 75 tiles; this adds 2 (the line buffers of
    amiga_fb and of the pixel layer double). S4 and S5 open.
- 2026-10-28, board, S5 measured instead of seen (Alex: the step is too fine for the eye): Workbench background set to
  200/200/200 in Palette prefs; the frame buffer read over JTAG (`trxdbg.sh ddrpeek 2023348 8`, row 100, and `2069848`, row 300)
  holds `C8C8 00C8` per pixel: {x 00, R C8, G C8, B C8}, the full 8-bit value 200. The 4-bit path gave 0xCC (top nibble repeated).
  S1-S3 and S5 passed; S4: A1200 mono WNS +0.380 ns (run 1017).
