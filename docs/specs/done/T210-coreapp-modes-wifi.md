# T210 — The core app is placed for the running HDMI mode from its first frame, and F8 WiFi shows the real state in every core

**Status:** DONE 2026-11-18. BUILT in ue2emu (2026-11-12), S3 board closed as done 2026-11-18 (shipped and in use, §5); was PROPOSED (2026-11-12, found by the ue2emu S45 check; release batch of Alex's "build it, integrate", 2026-11-11). Corrected 2026-11-13 (status check): merged to main (b1d5ab019); ue2emu proofs in §5.
**Repo:** TRX64U-MultiCore — core app (`coreapp/src/run.cc`, `video.cc`, `netops.cc`, `coreapp/cores/*.cc`), port (`host-app/software/lvgl_port/trx_lv_port.cc`).
**Number:** T210.
**Origin:** ue2emu S45 (the MultiCore host's HDMI modes in the emulator, C64U_emu 2026-11-11) run against the core app at 480p and 720p.
**Depends on:** T205 merged (same files around F8).
**Milestone:** M2.

---

## §1 The problem

- P1 **Splash at 480p/720p.** The core app's first frames (splash, about 1.9 s) are written with PX_POS 0 and PX_OSIZE 1920x1080
  (`trx_lv_port.cc:84-97` `layer_up` without a place; `run.cc:411-417` opens the splash before `picture_start` ~`run.cc:514` calls
  `place_menu`). Register log of the A1200 at 480p: `t=0.8365 PX_POS=00000000 PX_OSIZE=04380780`, corrected at `t=2.7603
  PX_OSIZE=01e00280`. At 480p the splash text is outside the frame; at 720p it is shifted right and cut (picture
  `scratchpad/s45/a1200_720_early3.png`). The board runs the same code, so it shows the same.
- P2 **F8 WiFi "No module" with a working module.** The F8 row (`menutree.cc:392`, value_none "No module") reads the state
  `wifi.onoff`, which only `coreapp/cores/c64.cc:593` registers (`netops_register`, `netops.cc:299-312`). A1200, VC-20, C128, NES ...
  never register it, so the row says "No module" although the ESP32 answers ("Identify: ESP32 WiFi Bridge V1.14",
  "[core] ESP32 link: ok"), and RETURN on the row has no op behind it.
- P3 **Home status rows at 480p/720p.** On Home the two-line status rows ("Core / Amiga 1200 (AGA, 68020)") have their value cut at
  the bottom by the next row (`s45/a1200_480_f1home.png`). Overlaps T209 D1 (one line per row); built there.

## §2 Scenarios (ue2emu S45 with `--handover video=ntsc60:480p|pal50:720p|pal50:1080p`, `--log io`)

- S1 From the first PX write on, PX_OSIZE/PX_POS fit the running mode (480p: 640x480; 720p: 960x540 at 160,90; 1080p: 1920x1080);
  the splash pictures at 480p and 720p show the whole splash centred. Cores: A1200, VC-20, C128.
- S2 F8 WiFi in A1200, VC-20, C128, NES shows the driver's state (on / off / connected line), never "No module" while the ESP32
  answers; RETURN toggles it as in the C64 core.
- S3 Board: Alex sees the splash at 480p and F8 WiFi in a non-C64 core.

## §3 Decisions

- D1: one generic registration of the net ops for every core (not per core file).

## §4 Out

Nothing.

## §5 Build notes

- P1: `run.cc` `coreapp_run` now calls `video_place_menu(&px, hdmi, sys)` (new free function in `video.cc`, the body of `Video::place_menu`) with the
  handover's mode between `ui_open` and `ui_splash_open`: canvas and place are set before the first `layer_up`, which then writes them. (Before
  `ui_open` it would not do: `trx_lv_set_place` returns while the port's lock does not exist.) `picture_start` still applies the person's
  Picture setting afterwards through the same function.
- P2 / D1: `netops_register` moved from `cores/c64.cc` into `ops_register_base` (`ops_glue.cc`); every core has the net ops, and `c64.cc` no
  longer registers them (a second registration would be refused as a duplicate).
- P3: not touched, T209 D1.
- Proof S1 (ue2emu, `--log io`, first write of PX_POS / PX_OSIZE, `tests/ui/emu.py` takes `UI_EMU_ARGS="--log io"`; the mode was already up from
  the handover, so there are no mode_req writes to see, only reads):

  | Core | 480p (ntsc60) | 720p (pal50) | 1080p (pal50) |
  |---|---|---|---|
  | A1200 | t=0.8326 POS 0,0 OSIZE 640x480 | t=0.8441 POS 160,90 OSIZE 960x540 | t=0.8441 POS 0,0 OSIZE 1920x1080 |
  | VC-20 | t=0.8283 POS 0,0 640x480 | t=0.8397 POS 160,90 960x540 | t=0.8397 POS 0,0 1920x1080 |
  | C128 | t=0.8300 POS 0,0 640x480 | t=0.8415 POS 160,90 960x540 | t=0.8415 POS 0,0 1920x1080 |
  | NES | t=0.8339 POS 0,0 640x480 | t=0.8452 POS 160,90 960x540 | t=0.8452 POS 0,0 1920x1080 |

  The next writes are the core's own picture (A1200 480p: t=3.3101 OSIZE 720x540). Pictures at 1.5 s: splash whole and centred at 480p and 720p
  (A1200).
- Proof S2: `tests/ui/features_cores/t210_wifi_<core>.feature` for A1200, C128, VC-20, NES, Atari 800 and PET at 720p: F8 shows "WiFi  On" with
  the line "Link down, no network, no address" (the emulator's ESP32 answers V1.14), RETURN on the row gives the status line "WiFi is off" (the op ran: `wifi_net_radio` returned true). First run: the row stayed "On" after RETURN. Cause: the emulator's ESP stub answered DISABLE/ENABLE with no events, so the driver never left its state (our read-back `wifi_net_status` is right; C64U_emu fixed the stub, 11-12). Retest with the fixed ue2emu (A1200, VC-20 and all six features, 480p/720p/1080p): RETURN gives "WiFi is off" and the row reads "Off" with the line "Off, no network, no address"; a second RETURN gives "WiFi is on", row "On", line "Link down, no network, no address" (pictures `t210-<core>-f8-toggled`, `-f8-on-again`; the row text is read off the picture, the UI state snapshot of a fixed item has no value). Pictures for ux-review: splash and F8 at 480p and 1080p for A1200 and VC-20, scratchpad `t210/`. ux-review 720p (earlier): F8 rows PASS on all six cores, splash placed PASS; 720p menu at 1:1 with black bars is the 960x540 canvas by design (T164 D10a), not T210.
- PET (07713664b heap buffers): `tests/coreapp/emu/profiles/pet.toml`, `tests/ui/features_cores/t210_pet.feature`; ROMs with a 0xA5 pattern are put
  with read-back (log: all four ROMs and the 4040 DOS and controller, released), a D64 mount (`[pet] drive 8:0 alpha.d64, 174848 bytes`) and a PRG
  load (`[pet] hello.prg: $0801-$080C loaded`) work. The Waterloo ROM path (SuperPET) was not run.
- **2026-11-18 - board steps closed as done (shipped and in use):** S3 (splash at 480p, F8 WiFi) - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
