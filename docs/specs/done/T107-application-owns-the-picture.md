# T107 — the person can choose the picture of every core application, and the picture follows

**Status:** DONE 2026-11-15 (Alex 2026-11-15: "under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part.") Open board/scenario items are closed by Alex's decision, not separately recorded. Earlier status: IN PROGRESS (2026-10-16): built and benched on the Mac, board step open. 2026-11-13 status check: still IN PROGRESS. **2026-11-15 sync:** D7/D8/D11 are BUILT in T164 (host/pix_layer.v phase accumulator and SMOOTH, `hdmi_out #(.SMOOTH(1))` at host/top.v:1400, EDID filter `video_modes_offered` in coreapp/src/picture.cc:120; T164 merged, package main-b8b6bcf7) and seen on the board 2026-11-02..04 (host V key, A1200, C128 picture, Auto = host mode, T164 todo); the 720-wide UI is built as the compact layout (T164 D10a); a revert question exists (T164 Q7, board seen 11-04), so §4's "no confirm timer" is overtaken. Sony cause found (§5.9, pixel repetition). Not recorded: a board retest of the NES picture at 720p (the original board step).
**Repo:** TRX64U-MultiCore — the core application (`coreapp/src`), the shared video library (`app/video`), benches in `tests/coreapp`.
**Number:** T107 (table in `TODO.md`).
**Origin:** Alex at the board, 2026-10-16 (translated): "Wait .. then surely the HOST and CORE apps should each be allowed to CHOOSE the mode. The Gideon-based U1541FW apps do it that way too." Then: "Where is the resolution? That built for hours." and, on the Atari, "No resolution on the Atari either." And: "Gideon's apps can switch the screen, in mode and in resolution - please let all the other cores do that too. Scaling must then adapt." And: "also for the Atari 800 ... the resolution is the same as with the NES, there too we should be able to take 720p etc."
**Depends on:** [ADR-0057](../../adr/0057-every-application-owns-the-video-mode.md) (the application owns the mode), [T105](T105-video-mode.md) (the mode machinery: EDID, `video_apply`, the hand-over), T104 (the generic application and its settings).
**Milestone:** M1.

---

## §1 The problem

What shipped first (ADR-0057) is only the AUTOMATIC half: each application computes a mode from its own pixels (`Video::wanted_hdmi()`) and the host puts its own back on return. Nobody can choose. Gideon's applications let the person switch mode and resolution (`u64_config.cc:391`); ours do not, and that omission was never decided with Alex beforehand.

Seen at the board: the settings tables of the Atari (`coreapp/cores/atari800.cc`) and the NES have no picture entry. Behind it a second defect: the start-up path (`run.cc`) applied a mode but left the layer where the descriptor had put it for 1920 x 1080. The NES descriptor has `x0 = 576, y0 = 204`; at 720p `576 + 768 > 1280` and the picture is cut at the right and bottom.

Parts (MECE):

- **P1 The choice exists.** Every core application offers a "Picture" setting without each descriptor declaring it, with Auto and every mode the board drives.
- **P2 The choice is applied.** Choosing sets the HDMI mode in the system (50 / 60 Hz family) the application is already in, live.
- **P3 The picture follows the mode.** Scale and position are recomputed for the new frame: whole-number scale up to the layer's cap of 3, centred, the core's own offset kept, never off the frame.
- **P4 The start-up path is the same path.** The mode taken at start (stored choice, else Auto) goes through the same layout; the NES at 720p is not cut.
- **P5 The choice is kept.** It persists like every other setting: `.cfg` beside the `.cor`, host layer, default.
- **P6 Room.** The merged settings still fit `SETTINGS_MAX`; a core that leaves no room is refused with a reason, not overflowed.

## §2 Scenarios

```gherkin
Scenario: S1 the layout maths
  Given a picture of w x h source pixels, a mode, a system and the core's offset
  When  video_layout() is asked
  Then  scale is the largest of 1..3 that fits, the position is the centre plus the offset (scaled), inside the frame;
        a picture too big even at scale 1 is at 0, 0; the 576p / 480p frame is 576 lines under PAL, 480 under NTSC

Scenario: S2 the NES start-up (the case that was broken)
  Given the host handed over 1080p PAL and nothing is stored
  When  the application starts
  Then  the mode is 720p, scale 3 at x0 = 256, y0 = 24 (not 576, 204), and PX_POS holds (24 << 16) | 256

Scenario: S3 every core has the entry
  Given every descriptor of the table and the made-up test core
  When  the application extends it
  Then  the last setting is "Picture" = Auto + the six modes, default Auto, WHEN_LIVE, the descriptor's own items unchanged, and it shows in the menu

Scenario: S4 choosing moves the picture
  Given the layer is shown
  When  the person picks another mode (also through the menu key)
  Then  video_apply() runs once with the SAME system, the layer goes off, size / position are written, the layer comes on; choosing it again writes nothing

Scenario: S5 Auto is stable
  Given a core whose picture was moved to a smaller mode by a choice
  When  Auto is chosen
  Then  it answers what the core's own numbers ask for (Atari: 1080p again), not what the shrunk scale would ask for

Scenario: S6 the choice is kept
  Given Picture was set to 1080p and written to the .cfg
  When  the application starts again
  Then  the value is read back, the mode is 1080p and x0 is 576

Scenario: S7 no room
  Given a core with 15 settings of its own, and one with 16, and one that declares "Picture"
  Then  15 is extended to 16 items; 16 and the reserved key are refused with a reason naming Picture
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P3 | `tests/coreapp/test_picture.cc` `layout_table()` | `tests/coreapp/run.sh`, `ci/lint.sh` |
| S2 | P4 | `test_picture.cc` `nes_startup()` | same |
| S3 | P1, P6 | `test_picture.cc` `extension()` | same |
| S4 | P2, P3 | `test_picture.cc` `runtime_change()`, `through_the_menu()` | same |
| S5 | P3 | `test_picture.cc` `runtime_change()` (Atari) | same |
| S6 | P5 | `test_picture.cc` `nes_startup()` (second rig on the same file system) | same |
| S7 | P6 | `test_picture.cc` `extension()` | same |
| S8 | P1, P2 | by hand on the board: NES at 720p, a choice through the menu, restart, host back in its own mode | board session (open) |

**Negative controls** (each breaks one expression in a copy of the product source, builds the same bench, and must come out red; `run.sh` runs them with `env -u UPDATE_GOLDEN`, an empty variable is still true to `getenv()`): `pic_start_nolayout` (the start-up path no longer calls `set_mode` - the NES case), `pic_x0` (position does not follow), `pic_cap3`, `pic_bias`, `pic_clamp`, `pic_toobig`, `pic_576`, `pic_pref` (Auto from the shrunk scale), `pic_same` (re-applies the mode it is already in), `pic_value` (off-by-one in the mode), `pic_room`, `pic_limit`, `pic_key`, `pic_live`, `pic_menu` (the entry missing from the menu).

## §3 Decision

- **D1 The entry belongs to the application, not to the descriptors.** `coreapp/src/picture.{h,cc}`: `picture_extend()` builds, at start-up, a copy of the core's `CoreDesc` whose `settings` are the core's own followed by "Picture" (Auto, `video_hdmi_name()` of every `VID_HDMI_*` mode, default Auto, `WHEN_LIVE`). `CoreCtx::desc` points at that copy, so the menu (`menuctl`, `menutree`), the boot sequence and `Settings` read the merged list with no change to any of them. The table's own descriptors stay const and untouched. (Alex asked for the merged array in `CoreCtx`; carrying it in the descriptor copy `CoreCtx` already holds is the same thing with no touched call site.)
- **D2 Apply.** `picture_apply()`: value 0 = Auto = `Video::wanted_hdmi()`, value n = mode n - 1; `video_apply(sys, hdmi)` in the system already in (`video_current`, PAL 50 when nothing says), only when the mode differs from the one the picture is in; then `Video::set_mode(hdmi, sys)`.
- **D3 Layout.** `video_layout()` (`app/video/videomode.cc`): largest whole scale 1..3 that fits, centred, plus the core's own offset. The offset is kept in SOURCE pixels, derived once from the descriptor (`Video::init_bias`): the Atari's 14 hires pixels (426 - 384 = 42 at scale 3) follow into every mode. (~~`video_frame_size()` knows the 576p / 480p setting is 576 lines under PAL~~: void since 2026-10-20, the frame is 1920 x 1080 in every mode, 5.6.)
- **D4 Auto does not drift.** `Video` remembers the descriptor's own scale for `wanted_hdmi()`; `set_mode()` overwrites the working scale.
- **D5 Start-up.** `picture_start()` (called by `run.cc`) takes the stored value (Auto without one), applies the mode only if it differs from what the host handed over, and goes through `set_mode()`. The boot sequence applies every setting again; `picture_apply()` sees the same mode and writes nothing.
- **D6 Limit.** `desc_check` allows 15 settings of its own (`SETTINGS_MAX` 16) and reserves the key "Picture"; `picture_extend` refuses the same cases at run time.
- **D7 (2026-10-29, Alex, after GitHub issue #7): the output really switches.** "No, sorry. When I change it at the board, the output
  has to change ... otherwise I do not need the parameters. And it works, see Gideon" (translated). This reverses the 2026-10-20
  correction ("simply 1080p always"): the Picture item must change the HDMI mode the screen receives, in the host's gateware
  (timing per mode and pixel clock; today `host/vic_scaler.v` drives 1080p only). Host change: new host key, every core rebuilt.
- **D8 (2026-10-29, Alex, after GitHub issue #8): a "Scale" setting per core**, values **Integer** (as today: whole-number scale,
  sharp, black border), **Fit** (largest that keeps the aspect, not whole-number) and **Fill** (whole screen, stretched); the
  cap of 3 goes, so a small picture (Amiga lores 320 x 256) gets x4. Alex: "if you recommend a, then a" (translated). Fit and
  Fill need a non-integer scaler in the host's gateware; to be built together with D7 (one host change).
- **D7/D8 condition (2026-10-29, Alex):** both have to be weighed against the host's FPGA room first (mode timing, pixel-clock
  switching, a non-integer scaler in layer 1); the estimate per part (LUT, FF, BRAM, DSP against the host pblock) comes before the build.
- **D9 (2026-10-29, Alex): the SID comes out of the reference core first.** "then let us take the SID out of the C64ref core
  first" (translated). `c64ref` is `build_only` (never shipped); it only gives the host a partner to be routed with. Its SID with
  audio is 2,976 LUT, 3 RAMB, 20 DSP of 11,809 LUT (host `2208e1ad`, `host.utilization_hier.rpt`: `c64_trx_sid_0`). The audio
  ports of the partition stay driven (a small stand-in), so the host's audio path is still timed. **The reference core is part
  of the host key** (`ci/lib.sh:272-277`, 80 `cores/c64ref` lines in `contract/host_pin.txt`): the change moves the host key,
  every core is rebuilt and the `.cor` files of release 922deb78 no longer start on the new host; it therefore goes in together
  with the next host change (D7/D8, T152), not alone. Media (drive 2,337 LUT, cartridge ~1,100) may follow.
- **D10 (2026-10-29, Alex): prove D7 and D8 in the A1200 first, then move them into the host.** "Can you try out HDMI and scaler in
  the Amiga1200 core, so we build and so on? And then we pull it over into the host and build that with C64ref once" (translated);
  on the host key and the release: "that does not matter". The A1200 is a large core (flat build, its own `pix_layer` copy), so the
  trial changes no host, no key, no other core. Order: mode timing with Gideon's repeat factor ("gearbox": 5 for 480p/576p, 2 for
  800x600 and 1024x768@50, `apps/ultimate/software/u64/hdmi_scan.cc:65-92`), pixel clock per mode, Scale Integer (no cap) and
  Fit/Fill (nearest); board test by Alex; then one host build with the slimmer `c64ref` (D9).
- **D11 (2026-10-31, Alex): the screen decides what is offered, the host holds the default, every core its own.** Translated:
  "My point is to offer nothing in the menu that the monitor/TV cannot do. And when the EDID changes, switch to a fitting mode
  automatically. The host may store one default mode and the cores read the value from the host config, but each core should have its
  own settings. So once we are fine with the cores for now and I can retest, you could build the HDMI + scaler topic into the host and
  generalise it so that all cores have the gateware (via the host, or as an include in large cores). Of course with the other host
  extensions and moving the boundary to the ref core, and the rebuild. And yes, one core or another may become a large core through it."
  - **D11a Offer only what the screen takes.** The Picture/HDMI rows of the host menu and of every core menu list only modes the EDID
    names (CEA Video Data Block, the detailed timings of the base block, the standard timings of a monitor); with no EDID, the safe set
    of T105 D1. The V key steps through the same filtered list.
  - **D11b Follow the screen.** A new EDID (hot plug, another screen) is read again; if the running mode is not in its list, the
    application switches to the best fitting mode at once, in the host and in a running core's menu.
  - **D11c Host default, per-core override.** The host keeps one default mode in its settings (T163 store); a core without its own
    choice takes it (hand-over `video`, T105 D3); a core's own Picture/Scale settings stay in its `.cfg` (P5) and win.
  - **D11d One implementation for all cores.** Mode timing and scaler (D7/D8, proven in the A1200) move into the host for the DFX
    cores and into a shared include for the large cores; built with the other items of T164 and the slimmer reference core (D9), one
    rebuild of host and cores. A core that no longer fits beside the bigger host becomes a large core (accepted).
  - **When:** after Alex's retest of the current cores (SNES, Neo Geo, A1200 USB keys).
- Seen 2026-10-29: the TRX64 monolith also drives 1080p only (`TRX64-Ultimate/designs/c64_basic/top.v:10-12`); Gideon's application
  in `trx64g` writes mode registers that this gateware does not read.

## §4 Out

- **A confirm-or-revert timer** ("keep this mode? 15 s") for a mode the screen refuses. Not built. A chosen mode is stored and applied at every start; a screen that does not take it stays dark in the core application (the host returns to its own mode, T105's blind key cycles there). Needs a decision with Alex.
- **Changing the 50 / 60 Hz family** from this item. The Picture item keeps the system the application is in.
- **The host application's own entry** (T105/T106 own it).
- **Applications that are not this generic one** (the Amiga has its own, T102): they need the same item; not part of this change.
- **Bundle-declared picture size** (T104's descriptor work): the numbers still come from `VideoDesc` in the core table.

## §5 Built, measured

- **Board, Sony TV, 2026-10-31 (list B; Alex at the TV, translated).** A1200 `.cor` of main-76817bbe, Picture setting from Auto: Auto (1080p) and
  1080p picture; 576p, 720p, 800x600, 1024x768, 1280x1024 "signal not supported". Gideon 3.15a on the same TV: 1080p, 576p ("stretched wide",
  4:3 on 16:9) and 720p picture, the three PC modes not. **Cause, read from the code** (`cores/amiga1200/mono/hdmi_timing.v:5-7`, `top.v:1440-1470`,
  `hdmi_modes.vh`): every mode but 1080p is sent as pixel repetition on a ~148.5 MHz TMDS clock (gear 2 for 720p, 5 for 576p, 3/2/1 for the PC
  modes, AVI PR = gear - 1). CEA-861 allows no repetition for VIC 4/19 (720p) and none of factor 5 for VIC 17 (576p): the sink sees 2560 x 720 and
  refuses; the Samsung (a monitor) took 720p anyway. Gideon changes the TMDS clock (74.25 / 27 MHz). **Fix for D11 (the host):** repeat each
  TMDS *bit* g times in the serializer (the GTP line rate stays above its minimum), TMDS clock = pixel clock, AVI PR = 0; then 720p is 148.5 / 2 =
  74.25 MHz and 576p 135 / 5 = 27 MHz on the wire. A1200 Scale (Integer, Fit, Fill) at 1080p: "look perfect". NES: picture (the host's 1080p);
  Alex: "it needs the scaler too" (D11, all cores). The host menu's V key changes nothing on the screen (5.6: the host is always 1080p); the
  manual (T169) must say so. EDID of the Sony not saved: F4 in trx64u's menu opens the full-screen monitor tab, Diagnostics is not reachable from it.

- 2026-10-16: `tests/coreapp/run.sh`: 17 test programs, 1,063 checks, 0 failed; 97 negative controls, all red as they must. `test_picture`: 200 checks. `tests/hostapp/run.sh` and `tests/coreapp/render/run.sh` still green.
- Numbers checked in `layout_table()` (worked by hand from the frames): NES 1080p 3 / 576 / 204; 720p 3 / 256 / 24; 576p PAL 2 / 104 / 64; Atari 1080p 3 / 426 / 108; 720p 2 / 284 / 72; 576p PAL 1 / 182 / 144.
- Found while reviewing the first draft of `video_layout()`: (1) it centred without the Atari's 42 px offset, which would have moved the Atari picture at 1080p; (2) `wanted_hdmi()` read the working scale that `set_mode()` overwrites, so Auto on the Atari after a smaller mode would have answered 720p; (3) a 576p picture was centred in a 480-line frame; (4) the menu's video line printed the descriptor's scale, not the live one. All four are fixed and have a control.
- Open: S8 on the board.
- 2026-10-30 (CI run 1051/1052, large-amiga1200, WNS -0.517 ns on pix_mmcm): `pxl/vacc_reg -> pxl/rq_tag_reg/R` and `-> la_reg[19]/D` (12-13 levels, 8 CARRY4: vacc + f_vd -> vq -> m_next compare -> `la + stride`, all in one clock). `mono/pix_layer.v` now registers the vertical step in stages (vq/vacc_n; m_next and the stride; the m_next compare); they are read at cx 16/24, eight clocks after vacc/m last change. hacc was not on the list and is unchanged. Behaviour identical: pixscale and hdmi_timing benches.

### 5.6 Correction (2026-10-20): the host's frame is 1920 x 1080 in every mode

**Cause.** D3 laid the picture out in the frame of the mode the application had picked (NES at 720p: scale 3 at 256 / 24). On
this host the mode changes nothing the pixel layer can see: `host/vic_scaler.v:99-108` has the 1080p timing wired in,
`host/overlay.v:8` ignores the HDMI timing registers (0x4000), the CDCE writes of `SetVideoPll` are swallowed
(`host/app_i2c.v`), and `host/pix_layer.v:6` takes the layer position in pixels "of the 1920 x 1080 frame". So the NES sat at
(256, 24) of a 1080p frame: left of centre, in the upper part, right half black (Alex, Zelda II, 2026-10-19). The premise of
T107 and ADR-0057 ("a smaller mode gives the picture more area") does not hold on this host; `docs/gap-audit-gideon-app.md` G10
had said "output is fixed 1080p" all along.

**Decision (Alex, 2026-10-20):** "No, simply 1080p always" (translated). `video_frame_size()` answers 1920 x 1080 whatever mode and
system it is given, so `video_layout()` (and with it `Video::set_mode`, the start-up path and the Picture item) produce the 1080p
layout in every mode: NES 3 / 576 / 204, Atari 3 / 426 / 108, Apple II 2 / 400 / 156. D3's "576 lines under PAL" is void.

**Not changed, deliberately:** the Picture setting, `wanted_hdmi()` and `video_apply()` stay; they now only tell the screen's mode
register what the application wants and move no pixel. Real mode switching needs a host change (frozen) and is a decision for
Alex; until then Auto's 720p for the NES buys nothing. The scale cap of 3 stays: the NES is 768 x 672, 62 % of the frame height.

**Bench.** `tests/coreapp/test_picture.cc`: `layout_table()` now holds the 1080p numbers for every mode and system;
`fixed_frame()` checks NES, Atari and Apple II against the 1080p layout for 576p, 720p, 800 x 600, 1024 x 768 and 1280 x 1024 in PAL and
NTSC, `video_frame_size()` for both systems, and the Picture item driven to every mode. Before the fix: 92 checks red (e.g.
`fixed frame 256x224 mode 1 sys 0: got 3 at 256,24, 1080p has 3 at 576,204`); after: 326 checks, 0 failed; the full `tests/coreapp/run.sh` rc 0 with 253 controls red as they must. Controls `pic_x0`, `pic_start_nolayout` and `pic_pref` now bite on descriptors whose scale or offset the layout corrects; `pic_frame`
(`video_frame_size` follows the mode again) fails; `pic_576` is gone with D3's 576-line frame.

### 5.7 Estimate of D7 and D8 against the host's room (2026-10-29, the "D7/D8 condition"; estimate only, nothing built, no Vivado run)

**Baseline, measured.** Host entry `host-2208e1ad` (the host in the board's flash; built at 840f0b75, CI run 1033 hit its cache), builder cache
`.../multicore/bit/host-2208e1ade5420b7b/host.utilization_pb_l1_opt.rpt` and `host.utilization_hier.rpt` (read-only mount), log line
`=== T79 pb_l1 | Slice LUTs 11013 / 14200 (77.56 %)`:

| pb_l1 | used | pblock | % |
|---|---|---|---|
| LUT | 11,013 | 14,200 | 77.56 |
| FF | 14,963 | 28,400 | 52.7 |
| Block RAM tiles | 24.5 (17 RAMB36 + 15 RAMB18) | 25 | **98.0** |
| DSP48 | 9 | 60 | 15 |

Whole device: 22,439 LUT (68.8 %), 56.5 of 75 tiles, MMCM 4 of 5, PLLE2 2 of 5, BUFGCTRL 16 of 32, 4 GTP of 4.
Comparable modules (hier report, LUT / FF / RAMB36 / DSP): `scaler` (vic_scaler) 170 / 133 / 4 / 0; `ovl` 399 / 297 / 3 (+1 RAMB18) / 0;
`pxl` (pix_layer) 569 / 1,304 / 2 / 3; `txt` 206 / 462 / 0 / 0; `hdmi_enc` 622 / 867 / 0 / 0; `gtp` 314 / 343; `mctl` 124 / 205.

**The room that can be used, not the raw number.** Raw free: 3,187 LUT, 0.5 tile. Routed so far: 75 % (9,820), 77 % (10,047), 77.6 % (this one);
not routed: 79.9 % (11,023 / 13,800, T133 2026-10-27, the same wall as run 1006) and 82 % (skill, section 2). So the host is already at the
density that routes. At 78 % there are **about 60 LUT** left, at 80 % (never shown for pb_l1) about 350. Block RAM: **0.5 tile**, every new buffer
must be zero-BRAM. DSP is the one resource that is free (51 of 60). Everything below that does not fit in 0-350 LUT needs a bigger pb_l1, which
takes slices from pb_l2 (T133 floorplan B; the reference core c64ref is 12,138 of 16,800 there; what the shipped cores need would have to be checked first).

**What the code says (the facts the estimate rests on).**
- The 1080p timing is hard in `host/vic_scaler.v:99-108` and drives a *virtual* cx/cy (active at cx 768.., cy 45.., with a jump over the unused part of the
  blanking). `overlay.v:152`, `pix_layer.v:227,275-282,307`, `c64_text.v:207-247` and the encoder (`hdmi` parameters in `top.v:~1408`, islands at
  `SCREEN_X_END + 14 mod FRAME_WIDTH`, `vendor/icepi/hdmi/hdmi.v:94-107`) all work on those virtual counters. A mode switch can therefore keep the
  downstream chain and change only the generator and the constants 1920 / 1080 / 45 (`overlay.v:142-143`, `pix_layer.v:243,281`).
- The pixel clock is not a free clock. Chain: dot4x -> `mmcm_ref` x 125/104 (`top.v:195-204`) -> CDCEL824 over `cdce_i2c.v` (N/M 55/14, or 990/263) ->
  GTP REFCLK (the wizard is built for 1.5 Gb/s with N = 20, M = 1, D = 4 and no DRP, `build.tcl:83-96`) -> usrclk = pixel / 2 -> `mmcm_pix` (VCO 12 x usrclk, `top.v:1342-1349`)
  -> `pix_clk`. The output frame is locked to the VIC frame by the exact ratio (6875/1456), the scaler's ring depends on it.
- `app_i2c.v:26-29` swallows the app's writes to 0x64 unless `cdce_open` is set (the host app sets it only before a hand-over). Gideon's own table
  (`apps/ultimate/software/u64/hdmi_scan.cc:81-94`) is the same 8-byte block as `cdce_i2c.v:byte_at` (N 250, 91 for 1080p50) but for **his** reference clock,
  not ours: the N/M values have to be recomputed for 125/104 of dot4x, and his modes also set a `gearbox` (1 for 480p/576p, 2 for 800 x 600) and an internal PLL.
- Clock primitives in layer 2 are kept static by the flow (`build.tcl:211-216`: "Run 682 failed placement with them inside"); the contract has no pixel clock
  among its clocks (`contract/layer2_ports.txt`: sys, cpu, ui, mclk, sid2x, c64_gclk...). A DFX core cannot own an MMCM, a BUFG or a GTP.
- The `pix_layer` fetcher is line based out of DDR2 (`pix_layer.v:296-340`, line buffer `lb` 256 x 128 = two slots of 128 words, 2 RAMB36 used at half depth).
  No frame buffer exists or is needed. The A1200 build has its own forked copy (`cores/amiga1200/mono/pix_layer.v`, `.diff`), and every large core builds
  the host's `top.v` flat, so any host gateware change is also a change of c128, A1200, ZX Next (the C128 has 1.5 of 75 tiles left, T133).

**Estimates** (counted from registers, adders, comparators, muxes of the modules named; low / high; no synthesis):

| part | LUT | FF | BRAM | DSP | clocks | basis |
|---|---|---|---|---|---|---|
| 1 mode timing: table of 12 rows (6 modes x 50/60) as ROM or app registers, runtime compares replacing the constants, cx/cy jump and wrap, polarity | 150-280 | 100-220 | 0 | 0 | 0 | the current timing + virtual counters are ~80 LUT in `scaler` (170 total); runtime compares for 6 fields x 12 bit ~ 6 LUT each, a 12 x ~100 bit table ~ 100 LUT |
| 1a downstream frame size: overlay centring and box size, pix_layer limits, c64_text scale per mode | 80-160 | 40-80 | 0 | 0 | 0 | the 4 x 1280 x 800 text area does not fit 720p, 1024 x 768, 800 x 600 |
| 1b encoder: VIC code, audio N/CTS rate (32 bit) per mode, `MAX_PACKETS` 7 -> 3 (a 480p line has 138 blanking clocks) | 60-120 | 40-100 | 0 | 0 | 0 | 12 x 32 bit ROM + AVI case |
| 1c VIC path: 4 x / 4.5 x becomes a rational step (nearest, two accumulators) | 60-120 | 60-100 | 0 | 0 | 0 | `acc`/`acc_t` exist (`vic_scaler.v:118-123`); x needs the same |
| **D7 timing total (1 + 1a + 1b + 1c)** | **350-680** | **240-500** | 0 | 0 | 0 | |
| 2a CDCE: `v60` bit -> mode index, 12 x 4-byte ROM | 60-120 | 10-20 | 0 | 0 | 0 | `cdce_i2c.v` today 1 LUT-ROM of 8 bytes |
| 2b mode_ctl: app register, sequence (hold I2C, write CDCE, wait), status | 60-150 | 50-100 | 0 | 0 | 0 | the DRP part of `mode_ctl` is `mctl` 124 LUT / 205 FF |
| 2c `mmcm_pix` DRP reconfiguration (VCO must stay 600-1200 MHz: x12/6 holds only for pixel >= 100 MHz, so M and O change per mode, O = M/2) | 120-250 | 80-150 | 0 | 0 | 0 MMCM, 0 BUFG (BUFG gated during the switch, like `bg_sys`); a second MMCM + BUFGMUX instead: +1 MMCM (the last), +2 BUFG | one more DRP FSM and a 6-8 word table per mode |
| 2d GTP line rate per mode (wizard has no DRP; PLL N = 20 only for refclk 80-165 MHz, VCO 1.6-3.3 GHz) | 80-200 | 60-150 | 0 | 0 | 0 | DRP or TXRATE path, reset sequence |
| **D7 clock total (2a-2d)** | **320-720** | **200-420** | 0 | 0 | 0 | |
| **D7 total** | **670-1,400** | **440-920** | **0** | **0** | **0 new** | |
| 3 Integer without the x3 cap: `f_s`, `hsub`, `sub` 2 -> 4 bit, field in `PX_CTRL` b5:4 wider (app and A1200 copy follow) | 15-40 | 10-25 | 0 | 0 | 0 | `pix_layer.v:229,262-270,310-318` |
| 4a Fit/Fill, nearest: both repeat counters become phase accumulators (num/den per axis, 4 x ~14 bit), registers, CDC copies | 120-250 | 150-280 | 0 | 0 | 0 | app computes num/den (no divider in gateware); scale >= 1 only (the fetch-ahead of line m + 1 in `pix_layer.v:322-336` assumes m advances by at most 1) |
| 4b Fit/Fill, bilinear (additional to 4a): 2 line slots -> 3-4 (512 x 128 still two RAMB36), two reads per 8 pixels, 3 + 3 lerps | 350-650 | 450-800 | 0-1 | 6 | 0 | the alpha blend is 3 DSP (`pix_layer.v:405-412`); DDR2 traffic unchanged (same lines) |
| **D8 total, nearest (3 + 4a)** | **135-290** | **160-305** | **0** | **0** | **0** | |
| **D8 total, bilinear (3 + 4a + 4b)** | **485-940** | **610-1,105** | **0-1** | **6** | **0** | |
| **D7 + D8 nearest** | **805-1,690** | **600-1,225** | 0 | 0 | 0 | against 60-350 usable: 2.3 x to 28 x too much |

Density this would give at the current pblock: D8 nearest alone 11,148-11,303 = 78.5-79.6 %; D7 alone 11,683-12,413 = 82.3-87.4 %; D7 + D8 nearest
11,818-12,703 = 83-89 %. D7 is past the point that did not route in every case; D8 nearest is at the edge.

**Open risks inside D7, not LUT.** (1) GTP line rate: 10 x pixel clock is 270 Mb/s for 480p / 576p and 400 Mb/s for 800 x 600 at 40 MHz; the GTP minimum is
500 Mb/s (DS181 as remembered; **not checked in this repository, to be read before any build**). Ways out: 1440 x 480p / 1440 x 576p with pixel repetition 2 (CEA VIC 14/15, 29/30, 54 MHz = 540 Mb/s),
which the sink must accept; 800 x 600 has no CEA code, so no signalled repetition. (2) Frame lock: each of the 12 modes needs its own N/M for exactly htot x vtot / VIC frame;
modes off the standard clock (576p 27.07, 720p50 74.43, 1024 x 768 at 50 Hz) are as far off as today's 1080p (148.87 vs 148.5) or further. (3) Whether CDCE N/M exist for all 12 inside its VCO ranges. (4) MMCM / GTP reconfiguration is
board-only to verify (no model of the GTP in the benches). (5) A sink that refuses the mode: out of scope as before (§4).

**The split: what could live in the core instead.**

| part | host | core (layer 2) | verdict |
|---|---|---|---|
| mode timing generator, HDMI encoder, CDCE, `mmcm_pix`, GTP, DRP | yes | impossible: pins, GTP, clock buffers and MMCMs stay static (`build.tcl:211-216`), no pixel clock in the contract | host only |
| integer scale (3) | 15-40 LUT once | per core 15-40 LUT x every core, plus a pixel clock port | host |
| nearest Fit/Fill (4a) | 120-250 LUT once | the cores that use `pix_layer` have no pixel stream; their picture is a DDR2 frame the host reads. A core-side scaler would have to write a finished 1080p frame back: 1920 x 1080 x 2 B = 4.1 MB per frame, ~250 MB/s written and the same read at 60 Hz, through the same arbiter | host |
| VIC-native path (1c) | 60-120 LUT | the contract's `pix_color`, `pix_x`, `pix_y` are the VIC's raw 4-bit stream (`layer2_ports.txt:234,247-248`); a core could emit a finished RGB frame, but then needs output timing (de/hs/vs/cx/cy, ~30 new ports, version bump, every core rebuilt) and a pixel clock it cannot own | host |
| bilinear (4b) | 350-650 LUT + 6 DSP once | the same per core, in pb_l2; pb_l2 has 80 DSP and BRAM that small cores do not use | only if room in pb_l1 cannot be made; N x the cost |

Hybrid (host keeps timing and clock, core scales): needs the new ports above, a pixel-clock input in layer 2 (clock loading of a partial region from a variable clock), and a scaler in each of
the ~12 DFX cores, tested N times; large cores are flat builds of the host's `top.v` and gain nothing. Cost in pb_l2: 135-290 LUT (nearest) per core, 485-940 (bilinear), against once in the host.

**Result.** Integer without the cap (3) is free. Fit / Fill with nearest sampling (4a) is small but sits on the density edge. D7 as a whole (670-1,400 LUT) does not fit the host as it is
built: it needs a bigger pb_l1 (about 1,000-1,800 LUT more pblock, taken from pb_l2) or room freed inside the host, and it carries feasibility questions (GTP minimum rate, 12 frame-locked clock ratios) that no LUT count answers.
Bilinear costs 6 DSP and ~350-650 LUT more and is not worth its density; DSP is the only resource that is free. Nothing of this can move to the cores without a bigger contract change and N copies.
Decisions for Alex (one at a time, none taken here): the pb_l1 / pb_l2 boundary, which modes D7 keeps, nearest or bilinear.

### 5.8 D10 trial in the A1200 large core (2026-10-29, built, not synthesised; benches below)

**What was built (A1200 files only; `tools/host_pin.sh --check` green, the host is unchanged).** (1) `cores/amiga1200/mono/hdmi_timing.v` + generated
`hdmi_modes.vh`: counters of 12 modes (576p/480p, 1280x720, 1920x1080, 800x600, 1024x768, 1280x1024, each at 50 and 60 Hz) replace `vic_scaler`;
(2) Gideon's repeat factor ("gear": 5 for 480p/576p, 3 for 800x600, 2 for 720p and 1024x768, 1 otherwise; `hdmi_scan.cc` read-only): the TMDS
clock is g x the pixel clock, every pixel lasts g clocks, `ce` marks the last clock, all pixel-side registers of overlay and pix_layer move on `ce`, the
encoder fork (`mono/hdmi/`) takes frame size, island positions and PR = g - 1 from the generator; (3) `tools/hdmi_modes.py` derives the CDCE N/M/P/Q/R per
mode (rules checked by reproducing the two blobs `cdce_i2c.v` ships byte for byte), checks every limit and writes `hdmi_modes.vh` (`check`, `selftest`: 5
mutations caught); `cdce_mode.v` writes them over I2C; the app's request (byte at overlay `0x4F00` = family * 6 + hdmi) re-programs the synthesizer, the
transmitters are reset (50 ms), the GTP re-locks, `mmcm_pix` (now x14 / 7 of usrclk, VCO 645-1042 MHz) follows; (4) `pix_layer.v` scales by nearest
sampling with a phase accumulator per axis to `PX_OSIZE` (any output size, source <= 2 x output): the application chooses Integer (largest whole multiple,
no cap of 3), Fit (aspect kept) or Fill; `PX_CTRL` b5:4 is not read. Application: `VideoDesc::hdmi_modes` (set by the A1200 descriptor only), Picture goes
to the register (`video_apply_gateware`) instead of the firmware calls, `video_layout_scaled`, item **Scale** (Integer default = today's look) added by
`picture_extend` for that core only; `scale.apply` is registered by the A1200's own `register_ops`, so the exported operation list of every other
application is unchanged.

**Frame lock.** The A1200 has no VIC frame to lock to: its picture is a DDR2 frame the layer reads line by line, so the output rate is free. The rates the
tool prints come from the CDCE ratio (e.g. 1080p 50.124 / 59.827 Hz, 720p the same, 800x600 50.000 / 60.035, 1024x768 50.017 / 60.000, 1280x1024 49.998 /
60.001, 576p/480p 50.124 / 59.822). Worst pixel-clock errors: 800x600@60 +580 ppm, 1024x768@50 +342 ppm; the others under 65 ppm (a screen takes both).

**Open risks, board only.** (1) GTP line rate is 0.92 to 1.49 Gb/s for all 12 modes because of the repeat factor (the 270 Mb/s of 480p x 10 never occurs);
the minimum of the GTP/wizard setting was not looked up here, the wizard is built for 1.5 Gb/s with no DRP: **a rate far below that may not lock**; the
board test says. (2) `mmcm_pix` and the GTP re-lock after a change are not modelled. (3) The audio clock regeneration counts the TMDS clock (`hm_rate`); 5x/3x
repetition changes the packet timing, the sink may refuse audio in 480p/576p. (4) A screen that refuses a mode stays dark (§4).

**Benches.** iverilog on the builder (`trx64-sim-env:4`), all green, every bench with controls that fail. `run_hdmi_timing.sh`: 12 modes exact (clocks per frame
htot x vtot x gear, pixels, active pixels, hsync/vsync position, width, polarity, every pixel held g clocks, encoder constants, `ph_e`, frame rate equals
the table, e.g. mode 0: 864 x 625 x5 = 2,700,000 clocks, 50.124 Hz); 4 controls fail (hsync one pixel wide, pixel phase one clock short -> `clocks per frame got
2160000 want 2700000`, `ph_e` off by one, vertical front porch one line short). The first draft's "pixel enable one clock early" control did not fail (it
only shifts the phase): replaced. `run_pixscale.sh` (`tb_pixscale.v`, every output pixel of five frames Fill / Fit / x1 / 3/4 / Integer against
floor(x * SW / OW) with the DDR2 model and background clients): 1080p50 gear 1 (960 x 540) 4,185 lines shown, 720p60 gear 2 (320 x 180, x4) 2,475,
480p60 gear 5 (320 x 240) 1,860, 800x600@60 gear 3 (400 x 300) 2,325, 1280x1024@60 gear 1 (640 x 400) 3,324, and 1080p with RGB565: 0 late lines and
0 line errors each; controls fail (`hshort`: `shown in part: 639 of 640 pixels`, `vstep`: wrong source line, `noce`: layer without the pixel enable at gear 5).
`tests/coreapp/test_scale.cc`: 122 checks, 11 controls (Integer cap, Fit, Fill, bias, gateware path, PX_OSIZE, Scale item, mode change, view window, start in
1080p/50 Hz, A500 without the operation). `tests/coreapp/run.sh` ran to its last section (1,009 controls red as they must; the final romreq control uses
GNU `sed 0,/re/` and cannot run on macOS, as before), `tests/hostapp/run.sh` and `tests/loader/run.sh` green. **Not run:** Vivado (no synthesis, no timing: the
forked `top.v` is the biggest risk, CI builds it after the push), the board.

**Board test list for Alex (the A1200 `.cor` of the next CI run; the host stays as it is).**
1. Boot the A1200: picture as before (Scale Integer, 1080p, x2 = 1440 x 1080 centred). Menu Video page shows Picture and Scale.
2. Picture: 1280 x 720, 800 x 600, 1024 x 768, 1280 x 1024, 576p/480p in turn: the screen must change to that size (the monitor's info line), the Amiga
   picture stays centred, the menu readable. Note which modes stay black (GTP/clock) and which have no sound.
3. Scale: Integer / Fit / Fill in 1080p and 720p: Fit 720 x 540 -> 1440 x 1080 (1080p) / 960 x 720 (720p), Fill the whole frame stretched.
   A lores screen (320 x 256 mode, if the Workbench has one) at Integer x4.
4. Switch the mode back and forth ten times (1080p <-> 720p <-> 480p): no hang, the picture returns each time.
5. 60 Hz: Amiga in NTSC (or the system setting) with 1080p, 720p, 480p: 59.8 Hz, no flicker.
6. Sound in each mode that comes up (the Paula output through HDMI).

### 5.9 Board, first look (2026-10-30, Alex, A1200 `.cor` of run 1056)

- Picture 1920 x 1080 (Auto): picture and menu on the monitor. 800 x 600 (chosen, app console `[core] picture: 800 x 600 (chosen)`,
  read over JTAG): the monitor reports "600x600 @67Hz" (both 4/3 off the table's 800 x 600 at 50 Hz: gear 3, TMDS 104.83 MHz).
  Every other mode (720 x 576, 1280 x 720, 1024 x 768, 1280 x 1024): the monitor finds no valid HDMI signal. Alex (translated):
  "only the 1080p and the 600x600 show something, the rest is not recognised as valid HDMI". The benches (hdmi_timing: counters
  per mode) do not see the clock chain (CDCE programming, GTP reference, MMCM, gearbox), so they could not catch this. Open, cause
  not known.
- **Second look (same day).** 1280 x 720 at 50 Hz: picture correct, monitor "1280x720 / 50p" (the first no-signal was switching
  faster than the monitor syncs). 800 x 600: the Kickstart picture is correct; "600x600 @67Hz" is the monitor's label for a
  non-standard timing. 1024 x 768 at 50 Hz: no picture even after waiting (Alex: "1024x768 no picture"). 576p and 1280 x 1024 not
  looked at again. Gideon's table (`apps/ultimate/software/u64/hdmi_scan.cc`) has the same 1024 x 768 at 50 Hz timing (54.1632 MHz,
  1344 x 806, gear 2) but programs 8 bytes of the external PLL per mode (`ext_pll_1024x768x50` = 57 00 01 01 01 f0 2b 6c, "31 / 9",
  output dividers changing per mode: 01 01, 02 02, 03 03); `cdce_mode.v` writes 4 registers (0x18-0x1B) per mode. Lead, not proved.
- **PC modes, board numbers and what the code says (2026-10-30, `tools/trxdbg.sh mode`, `hdmi_meas_s` in `mono/top.v`).** Frames/s in a 1 s
  clk50 window and pix_clk cycles per vsync-to-vsync, `[23:8]` so +-256: 1080p 50 fps, 2,969,856 (= 2640 x 1125, correct); **800 x 600 asked: 50 fps,
  2,166,528 = 2 x 1344 x 806, the clocks of table row 4 (1024 x 768, gear 2), not row 3's 3 x 1040 x 672 = 2,096,640**; **1024 x 768 asked: 50 fps,
  1,843,200 = row 5's 1688 x 1092 (1280 x 1024), not row 4's 2,166,528**. Both are one row too high. The counter is not a clock measurement:
  `hm_pc` counts pix_clk edges between two rising `s_vs` of `hdmi_timing`, so the count is the frame length of the row the generator runs, whatever the
  TMDS frequency is. **The frame-lock assumption is refuted by the code:** `hdmi_timing.v` has no input from the Amiga's picture (counters `hc`/`vc` run free
  from `rst`, `vtot_m1` is the mode's own), `vic_scaler` is not in this build; 50 fps is the clk50 window over a 50 Hz row, not a lock.
  Both the clock (50 fps at 108.33 MHz; row 3's own clock would give 48.4 in that count) and the timing are therefore row m+1, i.e. `vm_mode` was m+1.
  **What was checked and is not the cause:** (1) the tables: `tools/hdmi_modes.py` now decodes `hm_cdce(m)` and `hm_row(m)` of the committed `.vh`
  alone (N, P, Q, R -> M integer -> REF x N/M; gear x htot x vtot) - all 12 modes land on their own frame rate (800 x 600: 104.832 MHz / 2,096,640 =
  50.000 Hz; 1024 x 768: 108.363 / 2,166,528 = 50.017 Hz; 1280 x 1024: 92.162 / 1,843,296 = 49.998 Hz); the control (every slot's PLL bytes from the
  next mode) fails with "mode 3: the CDCE gives 108.363 MHz, the row runs 2096640 clocks per frame = 51.684 Hz". (2) The wiring: `vm_mode` feeds
  `cdce_mode.mode` and, through `vm_p`, `hdmi_timing.mode` without offset; `cores/amiga1200/sim/run_modepath.sh` (overlay 0x4F00 -> vm_mode -> vm_p ->
  hdmi_timing with the pixel clock stopped and the lock lost, all 132 request pairs) passes and its two controls (request + 1, vm_p not following) fail.
  (3) The application: `tests/coreapp/test_scale.cc` drives all 12 Picture values through the item's own path and checks the byte written to 0x4F00
  (`family x 6 + hdmi`) and the label of each value - 241 checks pass; new control `sc_idx` (`video_mode_index` + 1) fails 15 checks. (4) hdmi_timing: its
  own bench measures the frame of every row from the vsync edges (CI `hdmi_timing`). **So no offline step shows a +1; the cause is still not found.**
  Not ruled out: a request byte that is not the one the application means at the bus (a second writer into overlay block 4, a byte-lane on the bridge),
  or a difference between the built gateware and these sources. The read-out added for this (`0x100035`: last byte at 0x4F00, requests seen, `vm_mode`,
  `vm_p`, the row `hdmi_timing` runs; `tools/trxdbg.py mode`) tells the two apart in one read after one mode request and is the next board step; it needs a
  build of this branch. The AVI InfoFrame carries PR = gear - 1 (800 x 600: 2) with VIC 0; a sink may read that differently from the 3 x repeated pixels, which
  would explain the label "600x600 @67Hz" (600 = 2400 clocks / 4) and is a second, separate point.
- **CDCE lead decoded (2026-10-30, `tools/hdmi_modes.py check`, datasheets CDCEL824 SCAS945A / CDCE913 SCAS849I).** The board part
  is a CDCEL824 (`host/top.v`): registers 0x14 = 0x57 send Y1 and Y2 through Pdiv2, which `cdce_mode.v` writes as 1, so the pin
  frequency is fVCO. Decoded back from the committed bytes (REF 37.894 MHz): 576p50 25/7 135.336 MHz range 01; 720p50 and 1080p50
  55/14 148.869 range 01; 800x600@50 841/304 104.832 range 00; **1024x768@50 163/57 P3 Q22 R50 108.363 range 00 (+342 ppm)**;
  1280x1024@50 197/81 92.162 range 00; 576p/480p60 305/92 125.627 range 01; 720p/1080p60 676/173 148.071 range 01; 800x600@60 13/4
  123.155 range 00 (+580 ppm); 1024x768@60 271/79 129.991 range 01; 1280x1024@60 239/85 106.549 range 00. Every mode: M integer,
  P = 4 - int(log2 N/M), 16 <= Q <= 63, VCO inside 80..200 MHz, the range bits as the datasheet's thresholds, output within 342 ppm
  of the TMDS clock (mode 4 and 800x600@60 are the two worst: the search limits R to the CDCE913's 51, the CDCEL824 allows 511).
  **So the register values do not explain mode 4**: it is formally as valid as the working mode 3 (same Q 22, same range bits, same
  P). Gideon's 1024x768@50 bytes decode to 31/9 P3 Q27 R5 range 00, Pdiv 1 - the same shape (VCO about 108 MHz, range 00, Pdiv 1) - but
  against a reference he changes per mode (`SetInternalPLL`: e_INTPLL_1x, 6_5, ...), so his N/M cannot be compared with ours; his other
  modes use Pdiv 2/3 and R up to 247. What this rules out: a wrong range bit, a VCO outside the chip, a wrong P/Q/R packing. What
  it does not rule out: the fractional part (R/M = 0.88 in mode 4, 0.13 in mode 3), the +342 ppm, or a failure after the CDCE (GTP,
  mmcm_pix, the sink rejecting 1024 x 768 at 50 Hz). Decision needs one board read in mode 4 (read only, JTAG `hdmi_meas_s`:
  `{gt_ready, pix_locked, video_locked}`, frames per second, pixel clocks per frame): a lock bit low or a wrong count = clock chain;
  all locked and 50.017 Hz = the sink. The check now decodes the bytes (not the search that made them) and has controls for the range
  bits, P, N and the VCO limit (`tools/hdmi_modes.py selftest`, 9 of 9).
- **The menu** is a 4:3 picture scaled with the mode and placed at the left: on 16:9 modes the right quarter stays black, on 800 x 600
  it fills the screen. Open: centre it like the machine's picture. **And it is not laid out again when the mode changes while it is
  open** (Alex: "and lvgl does not render again"): 720p -> 1080p left a 960 x 720 menu in the top left of the 1080p frame. Also open (UI): the values sit half a row below their labels, and
  "at the next reset" repeats the "(at next reset)" of the label.
- **Decisions (Alex, 2026-10-30, translated).** On smoothing for Fit/Fill (+350-650 LUT; A1200 at 23,721 of 32,600 LUT, 72.8 %,
  WNS +0.170 ns in run 1056): "well, for both then. Since the core has to be done anyway - please": bilinear smoothing for Fit AND
  Fill in the A1200, together with the Integer fault (Workbench hires text has uneven columns even at Integer: the capture into the
  frame buffer, not the scaler). On the menu: "Our UI is laid out for 540p. It should always look good there and at multiples of it":
  the overlay scales by the integer factor max(1, floor(vact / 540)), centred (the coordinator wrote "720 x 540" here; the canvas is
  960 x 540, see the fix below).

- **Board, 2026-10-30 15:22 (Alex, photo, `.cor` of run 1060):** Kickstart 3.1 insert-disk screen at 1080p50, Scale Integer: sharp, even pixel
  edges, nothing blurred. The blur seen before in 720p came from the monitor scaling 720p up, not from the core. Not covered by this: Workbench hires text
  (the uneven columns above), which still needs its own look.

- **Board, 2026-10-30 15:38 (Alex, photo, 720p):** the menu sits at 960 x 540 (factor 1, 720 / 540 = 1.33) centred with black around it, as the
  540 rule says. **Decision (Alex, "Ja" to the proposal):** keep it so now (small, sharp); in the host app rework (T163 D3) the menu is drawn natively
  at the mode's resolution (1280 x 720 large and sharp), which also answers the open question of modes narrower than 960.

- **Board, 2026-10-30 evening (Alex stepping Picture with RETURN, A1200 `.cor` of run 254, JTAG 0x100034/0x100035 after each step):**
  the request path is right in every step - the byte at 0x4F00, vm_mode, vm_p and the row hdmi_timing runs agree, and clocks per frame are
  exactly the row's gear x htotal x vtotal: 576p50 byte 0, 2,699,776 (864 x 625 x 5), monitor "unsupported mode"; 720p50 byte 1, 2,969,856
  (1980 x 750 x 2), picture; 800x600@50 byte 3, 2,096,640 (1040 x 672 x 3), picture shown as "600x600@67Hz"; 1024x768@50 byte 4, 2,166,528
  (1344 x 806 x 2), "unsupported mode"; 1280x1024@50 (gear 1): "unsupported mode" (read-out missed, already stepped on). The earlier "one
  row high" reading (2,166,528 for 800x600) was taken after a further step and is withdrawn. So the gateware does what is asked; the sink
  (Samsung) refuses pixel repetition beyond what CEA allows for the VIC (576p VIC 17 with PR 4; VIC 0 with PR 1/2) and/or the non-VESA
  50 Hz rates of the PC modes (1280x1024@50 has no repetition and is refused too).

- **Same screen with Gideon's firmware 3.15 (2026-10-30, Alex, photos):** SD (480p/576p) PAL shows a picture, 720p too; the PC modes
  (800x600, 1024x768, 1280x1024) give no picture there either. So the PC modes are the screen's limit; 576p is ours (gear 5 against his
  output, which this screen takes). **Decision (Alex):** test the modes again on the Sony TV (HDMI up to 1080p, SCART) before changing the
  mode list.

**Fix, built 2026-10-30 (branch `t107-menu`).** Causes: the menu and the core's picture take turns on the one pixel layer, and the A1200's layer scales
to `PX_OSIZE`, which only the picture wrote (`coreapp/src/video.cc` `write_set`); the menu's first picture (`host-app/software/lvgl_port/trx_lv_port.cc`
`layer_up`) wrote `PX_POS` and no `PX_OSIZE`, so the 960 x 540 menu canvas was stretched into whatever box the picture last had (720p Fit: 960 x 720; 1080p
Integer: 1440 x 1080, right quarter black) and a mode change from the menu (`picture.cc` `picture_apply` -> `Video::set_mode`, layer off, nothing written)
left it there.
- **Rule (Alex, 2026-10-30, translated): "Our UI is laid out for 540p. It should always look good there and at multiples of it."** The menu canvas is
  shown at a whole-number factor only, `max(1, active height / 540)`, never fractional, never stretched, centred horizontally and vertically in the mode's
  active area, the rest black; evaluated again with every picture mode change and when the menu opens. `Video::place_menu()` computes it on every
  `set_mode` (also with the layer off) and hands it to `PxPort::menu_place()`; the board's `TrxPx` passes it to `trx_lv_set_place()`, which `layer_up`
  writes with every opening and which moves a menu that is up (layer off, geometry, on). Only a core with `VideoDesc::hdmi_modes` calls it; every other
  core keeps `TRX_LV_X0/Y0` and the layer's own scale.
- Result with today's canvas (960 x 540): 1080p x2 = 1920 x 1080 (fills the frame); 720p x1 = 960 x 540 at 160, 90; 1024 x 768 x1 at 32, 114; 1280 x 1024 x1
  at 160, 242; 576p x1 at 0, 18. **Open, for Alex:** the canvas is 960 wide, not 720: in 800 x 600, 576p and 480p (narrower than 960) its right
  part is cut by the frame (seen in `a1200-settings-800x600.png`). The factor rule as given (720 x 540: 1080p = 1440 x 1080 with 240 px black each side, x1
  everywhere else) needs the shared view (`app/view/trxview.cc` `SW 960`, 43 places in the view and the core app) laid out for 720 x 540: a UI task of its
  own, not done here.
- Items: (a) the value column was 4 px from the row top while the label's line is 14 px against the caption's 11: `trx_row_at` now centres the value on the
  label's line (`value_y`); (b) a setting with `WHEN_RESET` gets its second line "at the next reset" from `when` (`menuctl.cc` row), so the descriptors no
  longer say it in the label: the A1200 (6 + Kickstart), C264, C128, VC-20 items got a `label` without the suffix; the key (the `.cfg` key, REST, the
  operations) is unchanged, so stored settings still load. One source: `when`.
- Benches: `tests/coreapp/test_scale.cc` `menu_places()` (per mode: integer factor, centred origin, back to 1080p, Scale does not move it, a core without
  gateware modes gets none), `test_desc.cc` (no reset-time label says reset/reload); controls `mn_skip` (relayout skipped), `mn_origin` (origin not
  centred), `mn_frac` (fractional factor), `mn_label` (a suffix back in a label) fail as they must; the render bench composes the menu per mode
  (`a1200-settings-*.png`, centred factor checked) and checks label and value middle (control `TRX_VALUE_TOP`). Not covered by a host bench: the register
  writes of `trx_lv_port.cc` (board only). The patch files `host-app/patches/lvgl_port_trx_lv_port.*.patch` were stale against the copies and are
  regenerated.
