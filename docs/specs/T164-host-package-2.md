# T164 — the next host package: one list of everything that waits for the host

**Status:** DESIGN COMPLETE (2026-10-31), waiting for Alex's answers to §3a (design, budget and plan: §3b-§3d, `docs/todo/T164.md`). Proposed 2026-10-30, asked by Alex. Go on 2026-10-31 (translated): "I want us to get to HDMI and the
scaler in the host... it takes too long for me because too much hangs on it." Then: "OK, then spec, plan, refinement, ADRs, and go."
**Repo:** TRX64U-MultiCore — `host/` (gateware, layer 1), `host-app/`, `contract/` where a core feeds the host.
**Number:** T164.
**Origin:** Alex, 2026-10-30 (translated): "We have to touch the host again anyway, don't we? Do we have the overview of what else?" Then:
"and the host to-do list -> include NTP with the network, so the file system gets proper timestamps!"
**Depends on:** T163 D3 (the host app rework comes after the open core and HDMI work).
**Milestone:** M2.

---

## §1 The list

The host is frozen (`contract/host_pin.txt`); these items were each parked "with the next host change". Each keeps its own spec; this one
only collects them so nothing is forgotten and the LUT budget of pb_l1 (13,000, today about 11,000) is planned once.

**Gateware (`host/`)**
- **T135 D11 (2026-10-31):** the CI's last stage builds one MultiCore installer and checks its content; `trx64_update_*` is no longer made here
  (only `trx64g.cor`/`trx64u.cor` from TRX64-Ultimate); with it the installer fixes B2 (#6, #15) and the restore icon (D8b).
- **T152 D9 (2026-10-31):** the flash gate (`host/flash_gate.vhd`) gets a lock opcode that passes only the lock pattern (TB, BP 101, SRL, never SRP);
  then `HOSTAPP_FLASH_LOCK` goes to 1 (today the host blocks every status-register write, so the lock under large cores cannot take).
- **T107 D7/D8 + D11 (2026-10-31):** HDMI modes and scaler (Integer, Fit, Fill, smoothing) for every core, from the A1200 trial;
  menus offer only what the EDID names, a new EDID switches to a fitting mode, host default + per-core override; DFX cores through
  the host, large cores through a shared include; cores that no longer fit become large cores. Board 2026-10-30: the
  Samsung refuses pixel repetition beyond CEA and the 50 Hz PC modes (Gideon 3.15 too); retest on the Sony TV first (T107 §5.9).
- **T107 D9:** the SID out of c64ref (moves the host key).
- **T160 D1:** master volume (gain on the cores' audio, `mix_l/r`).
- **T159 D5:** the power lamp (LED 71) for every core in `bling_io.v`; floppy / hard disk activity from the core through the contract; G17.
- **T135 D6a (decided 2026-10-30, "Ja"):** the port lock keeps the two lines PHI2 / DOTCLK running while RESET is held (a Kung Fu Flash restarts itself
  without a clock: board 2026-10-30, boundary scan shows the port static, the cartridge's LED flickers).
- **T152:** the ESP32 boot pins (WIFI_BOOT / WIFI_DIS), board step S8.
- **T120:** the "release" comments in `host/build.tcl`, the rvlite fix.

**Host application (`host-app/`)**, sorted by Alex's rule (2026-10-30: "by added value, and what is already done at the top"):
1. **Issue #14** (done, merged 3c876683): direct boot waits for a late USB stick; only needs shipping.
2. **T163 settings:** our own store instead of Gideon's config pages (FTP came up off after a 1.1.0 run), Gideon's values read once.
3. **T137 P3 restore:** from a .cfw/.ue2 back to a single-core install - the way out of MultiCore for every tester.
4. **NTP:** real timestamps on everything the host writes (small).
5. **T163 UI rework:** the generic app as framework, the menu drawn at the mode's resolution (D4), FTP shown truthfully and switchable.
6. **T162 bug report file** (F8, `/SD/report/`, `GET /v1/report`; Q1-Q2 open).
7. **T152 S8 ESP32 flashing** (rarely needed; needs the gateware pins).
8. **T139** rest of renaming and settings.

### 1.1 The gateware package in parts (HDMI, scaler and the items that ride on the same host key)

**Who / what / today.** Alex at the TV and the monitor, with any core. Today the host's picture chain is 1080p only: the timing is wired into
`host/vic_scaler.v:99-108`, `host/overlay.v:8` and `host/pix_layer.v:6` work in a 1920 x 1080 frame, the CDCE writes of the application are
swallowed (`host/app_i2c.v`), so the Picture item and the host's V key change nothing on the screen (T107 §5.6). Only the A1200 large core
has modes and a scaler (T107 §5.8, its own copies of five host files); on the Sony TV of 2026-10-31 its 1080p and Scale (Integer, Fit, Fill)
"look perfect", but 576p, 720p and the PC modes are refused: the trial repeats **pixels** (gear 2 for 720p, 5 for 576p) on a ~148.5 MHz TMDS
clock, which CEA-861 does not allow for VIC 4/19 and 17 (T107 §5, "Board, Sony TV"). Every other core has no modes at all, and each
host-side item below waits for the same host key (`contract/host_pin.txt`, every `.cor` rebuilt).

Parts (MECE; §2 is checked against this list):

- **P1 The mode changes the signal.** A request for one of the 12 modes (6 x 50/60 Hz) moves the timing generator, the CDCE, the GTP and the
  pixel clock; the host application, a DFX core's application and a large core's application use the same request.
- **P2 The sink accepts the signal.** TMDS clock = pixel clock, no pixel repetition (AVI PR = 0), the right VIC code and aspect, the audio clock
  regeneration (N/CTS) of the pixel clock, per mode; the GTP line rate stays inside what the GTP and its fixed PLL dividers take.
- **P3 Every core's picture reaches the output at any mode.** DFX cores draw into the DDR2 frame buffer that `pix_layer` scans out (T119); C64-type
  cores hand the host the VIC raster that `vic_scaler` locks to; the host's own menu (`overlay`) and the freeze text (`c64_text`) follow the
  mode's frame.
- **P4 The scaler.** Integer (no cap of 3), Fit and Fill for every core, smoothing for Fit and Fill (T107 D8, Alex 2026-10-30), the numbers
  computed by the application, no divider in gateware.
- **P5 Which mode is chosen.** Menus offer only modes the EDID names; a new EDID switches to a fitting mode; the host keeps one default and
  every core its own choice; 50 and 60 Hz families; no EDID at all.
- **P6 One implementation.** The five large cores (amiga1200, c128, snes, neogeo, zxnext) get the same gateware from the same source as the
  host; the forks (A1200 `mono/{top,overlay,pix_layer,bling_io,hdmi/*}`, C128 `mono/top.v.diff`) go.
- **P7 The other gateware that waits for the host key:** T152 D9 flash-gate lock opcode, T107 D9 SID out of c64ref, T160 D1 master volume,
  T159 D5 power lamp and activity, T135 D6a clocks under the port lock, T152 ESP32 boot pins, T120 (the "release" comments, the rvlite fix of T97 §5.13).
- **P8 Room.** `pb_l1` is 14,200 LUT with 11,013 used (77.56 %) and 24.5 of 25 block RAM tiles (run 1104, host `2208e1ad`, unchanged since
  run 1033); 79.9 % did not route (T133). Every item above has to fit, or the boundary to `pb_l2` moves and some cores become large cores
  (accepted by Alex, T107 D11d).
- **P9 One rebuild, one proof.** Host, the 12 DFX cores and the 5 large cores rebuilt once; contract and host pin; the installer (T135 D11);
  the board steps on the Sony TV and the Samsung monitor.

## §2 Scenarios

Each item's own spec carries its scenarios. Added here (the scenarios come before the design in §3; every one names the bench or the board
step and a negative control that has to be able to fail; benches run on the builder in `trx64-sim-env:4`, `nice -n 15`, in `timeout`).

```gherkin
Scenario: S1 NTP timestamps
  Given the host app with the network up and SNTP enabled
  When  it writes a file (a backup, boot.cfg)
  Then  the FTP listing shows today's date and time in the chosen time zone
  # board: FTP LIST after a write; control: SNTP off -> the old fixed date

Scenario: S2 every mode has the right frame (P1)
  Given the timing generator in each of the 12 modes
  When  it runs for five frames
  Then  clocks per frame, active pixels, hsync / vsync position, width and polarity, and the frame rate equal the table row;
        the pixel enable is high once per pixel and the counters change only then
  # bench host/sim/hdmi_timing (from cores/amiga1200/sim/run_hdmi_timing.sh); control: vertical front porch one line short -> FAIL

Scenario: S3 the bit stream is the same pixel stream at the pixel rate (P2)
  Given the repeater in front of the GTP words with the gear g = 1, 2, 3, 5 and a known symbol sequence on the three data lanes
  When  the 20-bit words are unpacked into the serial bit stream
  Then  taking every g-th line bit (the middle one) gives back the 10-bit symbols one per pixel; the clock lane is 5 ones and 5 zeros per
        PIXEL (period 10 g line bits); no symbol is sent twice
  # bench host/sim/tmds_rep; controls: pixel repetition (the whole symbol twice) -> FAIL; the phase table off by one at g = 3 -> FAIL

Scenario: S4 the InfoFrame tells the truth (P2)
  Given the encoder in each mode
  When  the bench parses the AVI InfoFrame and the audio clock regeneration packet of one frame
  Then  PR = 0, the VIC code and the aspect bits are the row's (17 / 19 / 31 / 2 / 4 / 16, 0 for PC rows), N = 6144 and
        CTS x 128 x 48000 / N is the PIXEL clock of the row within 100 ppm; at least one audio packet fits every line of the lowest
        blanking (480p60, 138 pixels)
  # bench host/sim/hdmi_enc; controls: PR = g - 1 -> FAIL; CTS from the TMDS clock instead of the pixel clock -> FAIL

Scenario: S5 the tables are exact (P1, P2)
  Given tools/hdmi_modes.py
  Then  every row's CDCE bytes decode to the pixel clock x g within the tool's tolerance, the CDCE and mmcm_pix limits hold, the TMDS clock
        g x f_pix lies in 80..165 MHz (GTP PLL N = 20), rows marked "locked" give the VIC frame period EXACTLY (rational arithmetic) and the
        committed host/hdmi_modes.vh equals the generated one
  # python3 tools/hdmi_modes.py check / selftest, in ci/lint.sh; controls: the selftest mutations (each must be caught), one new per rule

Scenario: S6 a request reaches the pixel chain (P1)
  Given the host with the clock stopped for the retune
  When  the application writes every mode after every mode to the request register, and a byte above 11
  Then  vm_mode, the CDCE bytes, vm_p and the row hdmi_timing runs are the requested one; the byte above 11 changes nothing; the status
        register reads magic, version, running and the mode in use
  # bench host/sim/modepath (from cores/amiga1200/sim/run_modepath.sh); controls: request + 1 -> FAIL; vm_p not following -> FAIL

Scenario: S7 the scaler puts every pixel where the maths says (P4)
  Given pix_layer with the DDR2 model, a 960 x 540 and a 320 x 256 source, in each mode at the gear of the mode, Integer, Fit and Fill
  Then  every output pixel of five frames is source pixel floor(x * SW / OW) of line floor(y * SH / OH); no late line; with smoothing on
        (if built, §3a Q3) every pixel is the two-line / two-pixel weighted mean within one LSB
  # bench host/sim/pixscale (replaces the A1200's run_pixscale.sh, its controls kept: hshort, vstep, noce); smoothing weights swapped -> FAIL; built, see §5 WP5

Scenario: S8 the VIC raster stays locked in 1080p (P3)
  Given vic_scaler fed by a PAL (63 x 312) and an NTSC (65 x 263) raster model and the timing of 1080p50 / 1080p60
  When  1,000 frames run
  Then  the output frame period equals the VIC frame period (no relock, no ring overrun line); the window sits where it does today
  # bench host/sim/vicscale; control: the pixel clock one part in 10,000 off -> a slip is counted within the run -> FAIL
  # (rows other than 1080p for this path: §3a Q4)

Scenario: S9 the menu and the freeze text follow the frame (P3)
  Given the overlay and c64_text in each mode
  Then  the 960 x 540 menu canvas is shown at the whole factor max(1, floor(vact / 540)), centred, never cut, never fractional; the freeze text
        scale is min(4, hact / 320, vact / 200) and the text fits; nothing is drawn outside the active area
  # bench host/sim/ovl_geom and tests/coreapp/render/run.sh per mode (a1200-settings-*.png); controls: factor rounded up -> FAIL; origin not centred -> FAIL

Scenario: S10 only what the screen takes is offered (P5)
  Given EDIDs: a television (CEA block with 1080p50, 720p50, 576p, 1080p60 ...), a monitor with 1024 x 768 / 1280 x 1024 at 60 Hz in the
        detailed and standard timings, a DVI monitor without CEA block, no EDID, a corrupt one
  Then  the Picture / HDMI rows of the host menu and of every core menu, and the V key, list exactly the table rows the EDID names (and the
        no-EDID list of §3b G11); no row the EDID does not name is ever offered or applied from a menu
  # tests/coreapp/test_edid.cc, tests/hostapp; captured EDIDs of the Sony and the Samsung as fixtures (S18 gets them); controls: SVD ignored,
  # detailed timing ignored, "no EDID" lists everything -> each FAIL

Scenario: S11 the screen changes (P5)
  Given the running mode is in the list of the first EDID
  When  hot-plug goes low and high with an EDID that does not name it
  Then  within 1 s the application applies the best fitting mode once, in the host and in a running core's menu; the same EDID again applies nothing
  # tests/hostapp and tests/coreapp (HPD and EDID faked); controls: no switch -> FAIL; switch on every poll -> FAIL

Scenario: S12 default and override (P5)
  Given host.cfg with a default mode, a core .cfg with and without its own Picture, and a default the screen does not name
  Then  the core's own choice wins when offered, else the host default when offered, else the best fitting; the handover carries what is up
  # tests/coreapp test_picture.cc; controls: order swapped -> FAIL

Scenario: S13 the large cores build from the host source (P6)
  Given amiga1200, c128, snes, neogeo and zxnext after the forks are gone
  Then  each builds with host/ unchanged (c128 with its AVID diff), WNS >= 0, and the mode request of S6 reaches their pixel chain
  # CI large stage (Vivado); host/sim/modepath is the same bench; control: tools/host_pin.sh --check fails when a core edits a host file

Scenario: S14 the port lock runs the clock (P7, T135 D6a)
  Given the port lock set and RESET held (no core released)
  Then  SLOT_PHI2 and SLOT_DOTCLK toggle at the C64 rate, every other port line is idle, the port's RESET is low, and core_decouple still forces both low
  # bench host/sim/portlock; control: today's behaviour (both forced low) -> FAIL;  board: a Kung Fu Flash starts at the first core start

Scenario: S15 the flash gate takes exactly the lock (P7, T152 D9)
  Given the gate in front of the W25Q128 model
  When  the lock sequence (TB = 1, BP = 101, SRL = 1) and then a status write with SRP set, another BP value, or a second status byte, are sent
  Then  the lock sequence reaches the chip and reads back `34 03`; every other status write goes out as 0x9F; SRP never reaches the chip
  # bench host/sim/flash_gate (extend); controls: gate passes every 0x01 -> FAIL; SRP not tested -> FAIL;  board: T152 S2 / S5 / S6b / S7 / Q5

Scenario: S16 volume, lamp, ESP32 pins (P7)
  Given the host gain stage, the host bling_io and esp_ctl
  Then  unity passes a sample unchanged, 0 is silent, 255 saturates and never wraps; the lamp (LED 71) is red while a core runs and flickers on
        drive_leds_n; WIFI_BOOT / WIFI_DIS are high-Z until armed and then follow the control bits
  # host/sim/gain (from amiga1200_gain), host/sim/bling (from the A1200 bench), host/sim/esp_ctl (tb_esp_ctl of TRX64-Ultimate t26-esp-ram);
  # controls: gain bypassed, floppy flicker removed, polarity swapped -> each FAIL

Scenario: S17 the SID is out of the reference core and the host still routes (P7)
  Given c64ref without its SID and the stand-in that drives audio_l / audio_r
  Then  contract/layer2_ports.txt --check passes, the partition's audio outputs have drivers, c64ref routes against the new host, and its pb_l2 LUT is
        about 2,900 below the 11,809 of run 1104
  # CI host stage (utilization_pb_l2.rpt of c64ref); control: audio ports left undriven -> the port check or the DRC FAILS

Scenario: S18 the host fits (P8)
  Given the Vivado host stage with every item of P7 and the HDMI package
  Then  pb_l1 LUT <= the density that routed (77.6 % of the pblock, §3c), block RAM tiles <= the pblock's, WNS >= 0 on pix_clk at the highest TMDS
        clock (149 MHz) and the other clocks, and every DFX core still routes against the host (pb_l2 densities of §3c)
  # CI report lines; control: a copy of the budget check fed the 82 % report of the skill's history must FAIL

Scenario: S19 the screens (P1, P2, P4, P5, P9)
  Given the Sony TV and the Samsung monitor, a DFX core (NES), the A1200 and a C64-type core
  Then  1080p, 720p, 576p/480p are accepted and show a sharp picture on the Sony; the Samsung also takes 800 x 600 and 1024 x 768 at 60 Hz;
        Integer / Fit / Fill look as in the A1200 of 2026-10-30; sound in every mode that shows a picture; unplug and plug changes the mode as S11
  # board session of §3d with photos; control: the old A1200 .cor (pixel repetition) on the same TV in the same session still refuses 576p / 720p,
  # so the TV is a valid judge
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | NTP (list) | FTP LIST after a write | board |
| S2 | P1 | `host/sim/hdmi_timing` | `ci/sims.txt` tier 1 |
| S3 | P2 | `host/sim/tmds_rep` | tier 1 |
| S4 | P2 | `host/sim/hdmi_enc` | tier 1 |
| S5 | P1, P2 | `tools/hdmi_modes.py check` / `selftest` | `ci/lint.sh` |
| S6 | P1 | `host/sim/modepath` | tier 1 |
| S7 | P4 | `host/sim/pixscale` | tier 2 |
| S8 | P3 | `host/sim/vicscale` | tier 2 |
| S9 | P3 | `host/sim/ovl_geom`, `tests/coreapp/render/run.sh` | tier 1, `tests/coreapp/run.sh` |
| S10 | P5 | `tests/coreapp/test_edid.cc`, `tests/hostapp` | app tests, `tools/prepush.sh` |
| S11 | P5 | `tests/hostapp`, `tests/coreapp` | app tests |
| S12 | P5 | `tests/coreapp/test_picture.cc` | app tests |
| S13 | P6 | CI large stage, `host/sim/modepath` | Vivado, tier 1 |
| S14 | P7 | `host/sim/portlock`; board | tier 1; board |
| S15 | P7 | `host/sim/flash_gate`; board | tier 1; board |
| S16 | P7 | `host/sim/{gain,bling,esp_ctl}` | tier 1 |
| S17 | P7 | CI host stage | Vivado |
| S18 | P8 | CI host stage reports | Vivado |
| S19 | P1-P5, P9 | board session | by hand, Alex |

Every part has a scenario (P1: S2, S5, S6, S19; P2: S3, S4, S5, S19; P3: S8, S9; P4: S7, S19; P5: S10, S11, S12; P6: S13; P7: S14-S17; P8: S18; P9: S13, S19).

## §3 Decision

- **D1 (Alex, 2026-10-30):** NTP goes into the host app with the network.
- **D3 (Alex, 2026-10-30): three host app packages.** A = 1, 2, 4, 6 (issue #14, T163 settings, NTP, T162 bug report), started now with
  agents in parallel. B = 5, 8 (T163 UI rework, T139 rest), with: "for 5 it must be clear again that the present menu structure and what
  things are called is good" - **AK1:** the old UI structures mapped in the generic app; **AK2:** the build accepted by Alex in the
  emulator. C = 3, 7 (T137 P3 restore, T152 S8 ESP32 flashing).
- **D4 (Alex, 2026-10-30): gateware order.** Port lock with the clock running (T135 D6): yes. Master volume (T160): yes. `build.tcl` /
  rvlite (T120): yes. Power lamp for every core (T159 D5): discuss again what has to live where. ESP32 pins (T152): measure on Gideon
  3.15a first. HDMI modes/scaler (T107 D7/D8) and the SID out of c64ref (D9): after the Sony TV tests (2026-10-31).
- **D5 (Alex, 2026-10-30: "then from the other package update/restore? You can build that without core/gateware"):** package C starts
  now as host app work only. In: T137 restore (R) and backup from branch `t137-updater` brought onto main; T152 `.txu` update (D3/D5),
  flash protection under large cores (D4/D6/D7, software through the host's SPI flash access), ESP32 inspect (D8, read-only). Out until
  the host gateware package: T152 S8 ESP32 flashing (needs the WIFI_BOOT/WIFI_DIS pins). Order: T137 restore first (it carries the
  shared flash layer, T152 D2), then T152 on top. Board-only: T152 Q5 (Gideon's firmware under the lock-down).
  **State 2026-10-31:** T152 `.txu` update, flash protection and ESP32 inspect are built in the host app (T152 section 5); the CI package writes
  `trx64host_update_<commit8>.txu`; open: its board steps (`docs/archive/board-tests/board-tests-pending.md`) and S8.
- **M4 NOT measured yet (2026-10-30, correction):** a boundary-scan SAMPLE taken that evening was believed to show Gideon 3.15a, but
  the board was in OUR host (Alex: "the firmware was not running at all, I was in the host"). What it shows is our host: `WIFI_BOOT` L13
  and `WIFI_DIS` M13 not driven (oe 0, read 0), `WIFI_RTS` N14 driven 0, `WIFI_TXD` P14 driven 1. Gideon's levels (run and download)
  are still to be measured with his firmware actually running.
- **M4, RUN state measured (2026-10-30 evening, boundary scan, Gideon 3.15a really running, BASIC screen, REST answers):** `WIFI_BOOT` L13
  DRIVEN 0 (oe 1), `WIFI_DIS` M13 DRIVEN 0 (oe 1), `WIFI_RTS` N14 driven 0, `WIFI_TXD` P14 driven 1, `WIFI_CTS` L14 / `WIFI_RXD` N13 inputs;
  `SLOT_PHI2` toggles, `SLOT_RST_O` 0. So Gideon drives BOOT and DIS low in normal running, where our host leaves them floating (they read
  0 there too). The download state is still open (needs his firmware to enter download mode).
  Tried 2026-10-30: Gideon 3.15a has no "Flash into ESP32" entry for an .ESP file (built `/SD/esp/C64U_ESP32_g315.ESP` from the three
  images in his .cfw: bootloader 0x0, partition table 0x8000, application 0x10000, each hash checked); his updater flashes the ESP32 only
  after rewriting the FPGA flash, which would replace our host. So the download state is proven on our own host instead, without writing
  the ESP32: drive BOOT high and pulse DIS (the inference: BOOT=1 pulls GPIO0 low, DIS=1 holds EN low), then SYNC the ROM loader and read
  its chip id / MAC; no answer = wrong polarity, try the other way. Read-only on the ESP32's flash.
- **M4 answered (found 2026-10-30 in TRX64-Ultimate branch `t26-esp-ram`, board test 2026-10-10 late evening, `docs/todo/T26.md`):**
  "ROM loader reached from our core on the first try (derived pin polarity right, board stays powered)"; nothing was written to the ESP32.
  Polarity: `WIFI_BOOT` high = ESP32 GPIO0 low (ROM loader at the next reset), `WIFI_DIS` high = ESP32 held in reset, both low = normal
  run (matches Gideon 3.15a driving both low, measured above). Take `designs/c64_basic/esp_ctl.v` of that branch (high-Z until armed at
  0x10060910, follows uart_dma flowctrl bits 4/5, INV bits, bench tb_esp_ctl) into the host, not a new design.
- **D2 (Alex, 2026-10-30, translated):** "Restore from a .cfw back to installing a single core? And flashing the ESP32 too": both are part
  of this package (T137 P3, T152 Q3/S8).

### 3.1 Decisions that carry this design (all Alex's, quoted; the design below is built on them)

- **D6 (Alex, 2026-10-31): HDMI and the scaler go into the host now.** "I want us to get to HDMI and the scaler in the host... it takes too
  long for me because too much hangs on it." Then: "OK, then spec, plan, refinement, ADRs, and go." This is the explicit go that
  `contract/host_pin.txt` needs for a host change.
- **D7 (Alex, 2026-10-31, T107 D11): the screen decides, the host holds the default, every core its own, one implementation.** "My point is
  to offer nothing in the menu that the monitor/TV cannot do. And when the EDID changes, switch to a fitting mode automatically. The host may
  store one default mode and the cores read the value from the host config, but each core should have its own settings... you could build the
  HDMI + scaler topic into the host and generalise it so that all cores have the gateware (via the host, or as an include in large cores). Of
  course with the other host extensions and moving the boundary to the ref core, and the rebuild. And yes, one core or another may become a
  large core through it." (T107 D11a-d are the sub-decisions; §3b G11-G13 are their design.)
- **D8 (Alex, 2026-10-29, T107 D7/D8): the output really switches mode; Scale = Integer, Fit, Fill, without the cap of 3.** "No, sorry. When I
  change it at the board, the output has to change ... otherwise I do not need the parameters. And it works, see Gideon." "if you recommend a,
  then a."
- **D9 (Alex, 2026-10-30, T107 §5.9): smoothing for Fit AND Fill.** "well, for both then. Since the core has to be done anyway - please."
  (Decided for the A1200; built in `host/pix_layer.v` by T164 WP5 behind the build parameter `SMOOTH`, §3a Q3 puts it in the first host build.)
- **D10 (Alex, 2026-10-30, T107 §5.9 / T163 D4): the menu.** "Our UI is laid out for 540p. It should always look good there and at multiples
  of it." In the host app rework the menu is drawn natively at the mode's resolution ("Ja").
- **D10a (Alex, 2026-11-01): modes narrower than 960 px get their own layout.** Board: the core-app menu was cut at the right at 800x600 (ddt_novic);
  the view is fixed at 960x540 (`SW`/`SH` in `app/view/trxview.cc`, `coreapp/src/ui.cc`, `VIDEO_MENU_W`). Asked between A a compact layout for
  < 960 wide (480p, 576p: 720 wide; 800x600), B a 960 canvas panned to the cursor, C a fractional scale, D accept the cut: "A." The shared view
  takes its width/height from the mode; below 960 wide it uses a compact layout that is never cut (S9 holds for every mode).
- **D10b (Alex, 2026-11-01): the menu snapshot in the compact modes is deferred.** The T146 menu snapshot is 960x540; in 480p, 576p and
  800x600 the first picture at start is the splash. Asked whether to save a snapshot per mode: "That is pure app work, we can add it later.
  Defer it." Out of T164; an app follow-up.
- **D10c (Alex, 2026-11-03, GitHub issue #24): a core's picture "Auto" follows the host.** Tester (release t164-c83a83c2): the host menu
  at 720p, the core menus (Amiga) come up at 1080p because a core's "Auto" means the screen's preferred mode. Proposed: "Auto" in a core
  means the mode the host is set to; a core's own choice overrides it. Alex: "Yes." (translated). Built 2026-11-03, see §5 "D10c".
  **Amended (Alex, 2026-11-04, board, package main-7fda7e1f):** 1.3 passes, "but it just says Auto, so one does not necessarily know what it
  is ... an idea would be to write (AAAA x YYYY from host) in brackets with the Auto" (translated). So the core's Picture value reads
  "Auto (1280 x 720 from host)", the size being the host's stored mode the core uses; when the host stored none, "Auto (1920 x 1080 from
  screen)" or what the core picked.
- **D11 (Alex, 2026-10-29, T107 D9): the SID comes out of the reference core first.** "then let us take the SID out of the C64ref core first".
  It goes in with this host key (the key moves anyway).
- **D12 (Alex, 2026-10-30, §3 D4 above): the gateware order.** Port lock with the clock running: yes. Master volume (T160): yes. `build.tcl` /
  rvlite (T120): yes. Power lamp for every core (T159 D5, "for all cores as far as it fits ... except TRX64 and Gideon/Commodore 1.1.0"). ESP32
  pins (T152): measure first - measured and answered (M4 above), so the pins go in.
- **D13 (Alex, 2026-10-30, T107 §5.9): test the modes on the Sony TV first, before the mode list changes.** Done on 2026-10-31: the Sony refuses 576p / 720p / PC modes of the A1200 trial
  (pixel repetition); the fix is the design of G4.

## 3a Refinement: open questions for Alex, one per line, in the order they block

Asked one at a time when Alex answers; each has my recommendation. Nothing below is decided.

1. **Q1 - the boundary (blocks the floorplan and every Vivado run).** The items of §3c put `pb_l1` at about 12,100 LUT (85 % of 14,200; the
   densest that ever routed is 77.6 %). To route it, about 1,400 LUT of slices move from `pb_l2` (16,800 to about 15,400); then the A500 core
   `amiga` (14,298 LUT, 85.1 % today, the densest `pb_l2` result) becomes a large core and `nes` (13,181 LUT; 85.5 % of the smaller block) is at the edge. *Recommendation:* yes;
   move the slices (not the block RAM and DSP columns), let the CI say which cores still route, and make the ones that do not large cores one
   by one, as you accepted.
   **Decided (Alex, 2026-10-31): "Yes."** (translated)
   **Cut chosen (Alex, 2026-10-31), floorplan page of WP19 with cuts A, A', B, C:** "Yes, go straight to A" (translated): the lower-row edge
   moves from X55 to X50 (`SLICE_X50Y0:SLICE_X55Y49`, about 1,200 LUT to `pb_l1`), with DSP column X1 rows 0-19; `pb_l2` keeps 40 DSP, so
   `ddt_mandel` (48 DSP) no longer fits as a partial core.
   **Second cut (Alex, 2026-11-01), after run 1117 failed host placement at 12,802 LUT of 15,400 (83.1 %, `[Place 30-487]`, 10-36 slices short):**
   "The host MUST have all these features. And it must build. So only the reference core, and with it the room for partial cores, can get
   smaller." (translated). Slices X0-5 of rows 0-99 move to `pb_l1` (17,800 LUT, the host at about 72 %); `pb_l2` is 13,200 LUT, block RAM
   and DSP unchanged (50 tiles, 40 DSP). `nes` (13,181 LUT) and `amiga` (14,298) no longer fit and become large cores (Q1).
   **Overnight order (Alex, 2026-11-01):** "Build host + refCore. Whatever does not fit becomes a large core. Build all cores. We test tomorrow."
   (translated)
   **Third cut (coordinator, 2026-11-01, under the overnight order):** the X0-5 island of the second cut gave WNS -6 ns in run 1119 (the
   main cause was a clock-group pattern, WP19 result in §5); `pb_l2` became one rectangle, X0-49 of rows 0-99 (13,600 LUT, 40 block RAM
   tiles, 40 DSP), the host an L with the top-left strip (17,400 LUT). cpc, c264 and zx (block RAM) became large cores too.
   **Console flat builds (Alex, 2026-11-01), after neogeo did not place in run 1122:** "For the game consoles in the large build, out:
   everything we do not need of the UART, so basically network like WiFi. Anything towards C64 hardware still in there?" and, on `c64_text`,
   "well, out then" (translated). For nes, snes and neogeo the flat build leaves out the WiFi UART (`uart_dma`, 515 LUT, 1 tile), Ethernet
   (`ethernet_interface`, RMII, CRC, packet port, about 730 LUT, 1.5 tiles), the VIC raster path (`vic_scaler`, 101 LUT, 4 tiles) and the
   C64 freeze text (`c64_text`, 260 LUT); the app sees their absence (capabilities) and does not wait on them.
   **Large cores take copies, not the shared host include (Alex, 2026-11-01)**, after run 1126 rebuilt every large core for a host-only change
   and cpc / nes / snes lost timing: "If that now needs a new core every time, then no include!" and, on pinning a host commit instead,
   "Copies instead." (translated). Each large core carries its own copy of the host files it builds (`cores/<id>/mono/host/`), taken once
   from t164 with the hdmi_timing fix; a host change no longer moves a large core's key. A fix reaches a large core only by copying it in on
   purpose. This replaces WP18's "large cores use the shared include".
   **A1200 onto the host copy now, whole (Alex, 2026-11-01, at the board: A1200 without Scale rows, picture shifted, Picture 1024 x 768 not
   switching):** asked why the A500 works and the A1200 not, "that should ideally be one include for all large cores", then on doing WP18 now
   instead of an app fallback: "Yes please. NO slices! I want complete, whole things." (translated). The A1200 drops its pre-T164 forks
   (top, pix_layer, hdmi_timing, overlay, bling, cdce_mode, hdmi/*, modes) and builds from the same host copy as the other large cores plus
   its own parts; every picture function (modes, Scale, Smoothing, status 0x4F04), volume, lamp and keys as on the host; proven by benches,
   Vivado and the board. No app fallback for the old A1200 gateware.
2. **Q2 - the JTAG debug master in the tester host (a lever of 734 LUT).** `jtag_dbg` 696 + `rv_dbg` 38 LUT are in the host; ADR-0020 says a release
   goes without them, the HDMI board work needs them (`hdmi_meas`, `trxdbg`). *Recommendation:* the first T164 host keeps them, the host of the
   release set after the Sony / Samsung steps is built without (`NO_JTAG_DBG`), which lowers the boundary move of Q1 to about 430 LUT.
   **Decided (Alex, 2026-10-31): "Yes."** (translated)
3. **Q3 - smoothing (bilinear for Fit and Fill, +350-650 LUT, 6 DSP, no block RAM).** Decided for the A1200 (D9) but not built; in the host it would put
   `pb_l1` at about 90 %. *Recommendation:* not in the first host build; built and benched in the A1200 first (as you said), then added to the
   host as a second step once the density of the first build is known (WP20).
   **Decided (Alex, 2026-10-31), against the recommendation:** "No, build it in, I want to know what the cut between host + partial vs
   large core does. Changing it afterwards is rather fewer LUTs, I think?" (translated) Smoothing is in the first host build, behind one
   build parameter (off = nearest only), so the first Vivado runs measure the worst-case boundary and switching it off later is one flag.
   WP20 moves into the main line (after WP5).
4. **Q4 - C64-type cores (the VIC raster path through `vic_scaler`).** Its output must stay frame-locked to the VIC; that is exact only for 1080p
   (both families) and, by the ratios worked out in G5, for 720p and 576p/480p50; 480p60 and the PC modes cannot lock. No DFX core ships on this
   path (c64u / trx64 are source trees, c64ref is build-only, vc20 and zx go to the frame buffer, T119). *Recommendation:* this package gives
   C64-type cores 1080p50/60 only (as today, and nothing for them to break); the lock rows for 720p/576p are a later step if a DFX C64 core is
   ever shipped.
   **Decided (Alex, 2026-10-31):** "trx64u and trx64g are full cores, large. They do their own thing and we only start them. Leave them
   alone." (translated) They are large cores with their own picture path and are not touched by this package; the VIC raster path stays
   1080p only (no shipped core uses it).
5. **Q5 - the PC modes at 50 Hz.** VESA's established and standard timings have no 50 Hz entry for 800 x 600, 1024 x 768 or 1280 x 1024 (EDIDs name them at 60 Hz); the Sony and
   Gideon 3.15 refuse PC modes. With the EDID filter (D7) the three 50 Hz rows would never be offered. *Recommendation:* drop the three 50 Hz PC rows from the table
   (9 rows: 576p/480p, 720p, 1080p at 50 and 60, and the three PC modes at 60); frees 3 ROM rows and 3 CDCE searches.
   **Decided (Alex, 2026-10-31): "Yes."** (translated)
6. **Q6 - the 50 / 60 Hz family for frame-buffer cores.** Today the family follows the machine (PAL 50, NTSC 60) and "changing the family from
   Picture" is out (T107 §4). A PAL Amiga on a monitor that lists only 60 Hz would be dark or off the list. *Recommendation:* for frame-buffer
   cores the family is free (the frame buffer repeats or drops frames), the list shows the machine's family first and marks the other with its
   Hz; C64-type cores stay on their family (the lock).
   **Decided (Alex, 2026-10-31), simpler than the recommendation:** after "I think that is super confusing", the rule offered was: HDMI
   follows the machine (PAL 50 Hz, NTSC 60 Hz) as today; only when the EDID has no mode in the machine's family does the application take
   the other family silently (frames repeated or dropped); no Hz choice in any menu, only resolutions. Alex: "for now, yes" (translated).
7. **Q7 - confirm-or-revert for a mode the screen refuses.** T107 §4 left it out "needs a decision with Alex". With EDID filtering the risk is small
   but not zero (a screen that lies). *Recommendation:* build it (15 s, any key keeps, else back to the previous mode) in the host app and the
   core app, application work only (WP16, WP17).
   **Decided (Alex, 2026-10-31): "Yes."** (translated)
   **Amended (Alex, 2026-10-31), on WP17's question whether a web change asks too:** "15 seconds is too long. And yes, someone must always
   confirm for now. Confirming later over the web too is only app work, so it does not matter for the gateware." (translated); 8 s ("Passt"):
   every mode change (menu, V key, web page, REST) asks on the screen and reverts after 8 s without a key; the WP16/WP17 timers change from 15 s.
   **Amended (Alex, 2026-11-04, board photos of main-7fda7e1f):** the question stands only as a line at the foot of the page ("Picture:
   800 x 600. Press any key to keep it (2 s), else it goes back."). Alex (translated): "here too the call to action is at the bottom. No popover
   comes at all any more." So: the question is a popover box in the middle of the screen with its countdown and, after a blank line, its key
   line inside the box (T152 D10), in the host app and the core app.
8. **Q8 - contract version.** No port is added (the activity uses `drive_leds_n[0]` floppy and `[1]` hard disk, ports that exist; the gateware is
   probed by a new status register in the overlay window, not by a port). *Recommendation:* `contract/VERSION` stays 2; the host version rises
   (`<contract>.<n>`), the host key moves, every `.cor` is rebuilt; the convention for `drive_leds_n` goes into `contract/README.md`.
   **Decided by the coordinator (2026-10-31, technical, told to Alex):** as recommended; VERSION stays 2.
9. **Q9 - the list without an EDID.** T105 D1 asked for "the safe set"; the code answers 576p/480p when nothing answers. *Recommendation:* with no
   EDID (or a corrupt one) the lists show the six television rows (576p/480p, 720p, 1080p, both families), default 576p/480p, so the blind V key
   still has something to cycle; the PC rows only with an EDID that names them.
   **Decided (Alex, 2026-10-31): "Yes."** (translated)
10. **Q10 - the aspect flag for 576p/480p.** Gideon's 576p shows "stretched wide" on the Sony (4:3 content on 16:9). *Recommendation:* signal
    4:3 (VIC 17 / 2) for these two rows and let the screen pillar-box; the picture inside is 4:3 for every core we have.
    **Decided (Alex, 2026-10-31): "Yes."** (translated)
11. **Q11 - the "rvlite fix" of T120.** The list item points at T97 §5.13 (an interrupt on a `mul` is trapped twice, branch `t97-rvlite`, merged into
    `host-s9-rvlite`, not into main as far as `git log` shows). *Recommendation:* take it into this host key as the list says (zero LUT), after
    WP14 confirms the patch applies to the vendored RISC-V.
    **Decided by the coordinator (2026-10-31, technical, told to Alex):** as recommended.

## 3b The design (proposed; derived from the code; each item names its question)

**G1 Ownership (ADR-0059).** Everything from the picture sources to the pins is layer 1: one module `host/hdmi_out.v` holds the timing generator, the
CDCE sequencer, `mmcm_pix`, the pixel layer, the menu overlay, the freeze text, the encoder, the TMDS bit repeater, the GTP and the HDMI side of the
audio crossing and the measure registers. `host/top.v` instantiates it; so does every large core (G9). A core delivers a picture and nothing else: a
DDR2 frame buffer (`e_*` + the pixel layer's registers, set by its application) or the VIC raster (`pix_*`, `v60`). There is no pixel clock in the
contract and a DFX core cannot own an MMCM, a BUFG or a GTP (`build.tcl:211-216`), so none of this can be a core's.

**G2 The request and its status (all cores, the same register).** The application writes the overlay window `0x10144F00` (the A1200's request, now in
`host/overlay.v`): value `family * 6 + hdmi` (0-11, above is ignored). New: reading `0x4F04` returns `{magic 0x48, version, gt_ready, pix_locked,
running, mode in use, requests seen}`, so every application (and the JTAG read-out `0x100035`) can tell the gateware from an old host without a
capability bit (the CAPAB word is shared with Gideon's applications). The sequence is the A1200's, proven on the board 2026-10-30: crossing into
clk50, hold 8 clocks, `cdce_mode` writes the 4 PLL registers twice, `i2c_done` falls, GTP reset 50 ms, refclk moves, GTP relocks, `mmcm_pix`
follows, `pix_rst` makes `hdmi_timing` take the mode. `video_apply_gateware` polls the status (`running` and the mode) with a 1 s timeout instead
of waiting a fixed 400 ms. `mode_ctl` keeps the System Mode (the dot4x of the C64 families); its `cdce_v60` output becomes the family bit of the
row C64-type cores are held to; frame-buffer cores never change the System Mode.

**G3 The mode table (`host/hdmi_modes.vh`, generated by `tools/hdmi_modes.py`, one file for the host and all large cores).** Per row: VIC code,
aspect, gear g, timing, polarity, the pixel clock in Hz, the CDCE bytes, a `locked` flag. The A1200 table (`cores/amiga1200/mono/hdmi_modes.vh`) is
the start; changes: `rate` is the **pixel** clock (`f_tmds / g`, for N/CTS), 800 x 600 / 1024 x 768 / 1280 x 1024 at 50 Hz depend on Q5, and the
tool proves for the `locked` rows that the frame equals the VIC frame by rational arithmetic (G5). The TMDS clock `f_tmds = g * f_pix` must lie in
80..165 MHz (the wizard's PLL N = 20, M = 1, D = 4 gives the line rate `10 * refclk`: 0.8-1.65 Gb/s); all 12 rows of the A1200 table are 92-149 MHz,
i.e. 0.92-1.49 Gb/s, the range the A1200 board test of 2026-10-30 already ran. The GTP's own minimum line rate is not in this repository (the
wizard fixes the dividers); WP1 reads DS181 and adds the check. Free rows (not `locked`) assume the PAL dot4x reference; the C64 families change
dot4x, so C64-type cores get only `locked` rows (whose ratios are independent of the reference).

**G4 TMDS bit repetition (ADR-0060).** The A1200 trial sends each pixel g times at a TMDS clock g times too fast. The host keeps the same clocks
(`pix_clk` = refclk = `f_tmds`, `usrclk = pix_clk / 2`, `mmcm_pix` x14/7) and the same pixel enable `ce`, but a new `host/tmds_rep.v` sits between
the encoder's three 10-bit symbols and the GTP word registers: in `pix_clk` cycle t (0..g-1) of a pixel it emits the 10 line bits j = 10t + k,
k = 0..9, each being symbol bit floor(j / g). The line rate is unchanged (10 per `pix_clk`), the wire shows one symbol per `g * 10` line bits =
the pixel clock; the clock lane (today a constant `00000 11111`) goes through the same repeater with the symbol `0000011111`, so its period is 10 g
line bits. Gears 1, 2, 3, 5 (4 is not used). The encoder runs with a clock enable: its counters are in pixels again, `e_cx = g * cx + phase`,
`sx_e`, `fw_m1` and the PR fields of the A1200 fork go away; the AVI InfoFrame says PR = 0; `MAX_PACKETS` becomes 3 (a 480p line has 138 blanking
pixels; 48 kHz audio needs about 3.05 samples per 15.7 kHz line, one packet carries 4) - S4 proves it in the lowest blanking. The encoder is forked
into `host/hdmi_enc/` (`hdmi.v` with `ce` and runtime frame size, `packet_picker.sv`, the AVI file), patch files kept like the A1200's, vendored
`vendor/icepi/hdmi` untouched. Risk (board only): a sink sees a 270 Mb/s symbol stream driven by a 1.35 Gb/s transmitter (fast edges); Gideon's
firmware does the same class of thing with other means.

**G5 Timing and the VIC path.** `hdmi_timing.v` of the A1200 (448 LUT) becomes the host's generator. `vic_scaler` loses its own counters and
becomes ring + address + palette on `hdmi_timing`'s counters (it already works on virtual cx / cy); the one thing it needs is the frame lock, which
becomes an input to `hdmi_timing` (`align`: restart the frame at line 0 once, at the VIC's line ALIGN, as `frame_seen && !locked` does now).
Locked rows, worked by hand, to be proven by the tool: 1080p50 2,970,000 TMDS clocks per frame at 148.869143 MHz; 720p50 (1980 x 750 x 2) the same
count, same CDCE bytes (the A1200 table already has the same bytes for both); 576p50 (864 x 625 x 5) 2,700,000 at 135.335585 MHz, CDCE ratio 25/7 =
10/11 of 55/14 exactly; 60 Hz: 1080p60 2200 x 1125 = 2,475,000, 720p60 1650 x 750 x 2 the same; 480p60 858 x 525 x 5 = 2,252,250, ratio 91/100
of 990/263 = 9009/2630, N > 4095: **not exactly lockable** at CEA timing (a longer line, htot a multiple of 263 pixels, would be, a timing the
tool may search for). Per Q4 this package keeps the VIC path at 1080p; the rows above are the room for a later step.

**G6 The scaler.** The A1200's `pix_layer.v` (nearest sampling with a phase accumulator per axis to `PX_OSIZE`, any size, the vertical step in
stages for the timing of run 1051) becomes the host's single copy, with the host's still capture and `c64_text` interface kept; its fork and
`.diff` go. Measured in run 1104 against the host's: 801 vs 569 LUT, 1,295 vs 1,304 FF, RAMB36 2 vs 2, DSP 3 vs 3 (the 232 LUT include the
A1200's 32-bit format of T136). Scale mode (Integer, Fit, Fill) is the application's: `video_layout_scaled` computes `PX_OSIZE` and the position;
no divider in gateware. Smoothing (Q3) adds two line slots in the same 512 x 128 buffer and 6 DSP.

**G7 Overlay and text.** `overlay.v` with `ce` and `hact/vact/xact0/yact0` (the A1200 diff, 370 vs 399 LUT measured): the menu canvas at the whole
factor (D10), the box limited to min(1280, hact) x min(800, vact). `c64_text.v` takes the same geometry and scales 4, 3 or 2 (S9).

**G8 Audio.** `hdmi_rate` is the mode's pixel clock from the table (the host's two-entry case on `vmode` goes away); the 48 kHz tick and CTS follow
from it; the I2S side and the crossing `snd_hold` -> `hdmi_snd` are unchanged. T160: `snd_l <= pause ? 0 : sat24(audio_l * mix_l / 128)` with the
A1200's `trx_gain` (37 LUT, 72 FF, 2 DSP measured); `mix_l/mix_r` must reset to unity (128) and the generic application gets the write path (WP11).

**G9 Large cores (ADR-0061).** snes, neogeo and zxnext already build `host/top.v` unchanged (their `build_mono.tcl` read the host's files), so they
get the gateware by building the new host. c128 keeps `mono/top.v.diff` (AVID pins, caps) rebased onto the new `top.v`. amiga1200 deletes its
copies (`mono/top.v`, `overlay.v`, `pix_layer.v`, `bling_io.v`, `hdmi/*`, `hdmi_timing.v`, `cdce_mode.v`, `hdmi_modes.vh`, its `trx_gain` and `VOL_CFG`
move to the host's) and reads the host's; what stays is the core. The "include" Alex named is therefore the host's own source plus the generated
`host/hdmi_modes.vh`. `tools/host_pin.sh --check` and the wrapper-fork check guard it.

**G10 Contract (Q8).** No port is added. `drive_leds_n` is documented as `[0]` floppy, `[1]` hard disk, `[2]` unused (active low); the A1200 drives `[0]`
already. `contract/VERSION` stays 2, the host version rises, `contract/host_pin.txt` is re-pinned after the merge (`tools/host_pin.sh --update`, covered by D6).

**G11 EDID and the list (D7a).** `app/video/edid.{h,cc}` (pure, host-testable): parses the CEA short video descriptors (VIC -> row, including the 4:3
and 16:9 codes), the base block's detailed timings (pixel clock, active and blanking within 1 % of a row), the established and standard timings
(800 x 600, 1024 x 768, 1280 x 1024 at 60), and vetoes by the range-limits descriptor (max pixel clock, line and frame rate) and the HDMI vendor
block's max TMDS clock. The result is a row mask (`video_modes_offered()`). The Picture / HDMI rows of the host menu, `picture_extend()` of every core,
the web UI and the V key all take this mask. No EDID: Q9.

**G12 Follow the screen (D7b).** Both applications poll `U64_HDMI_HPD_CURRENT` and the EDID checksum about every 500 ms; on a change they re-read,
and when the running mode is not in the new mask they apply `best(mask)` once (same family, 1080p > 720p > 576p > PC, the order of
`video_mode_from_edid`); a core's menu is rebuilt; the same EDID never re-applies. Q7 adds the revert timer.

**G13 Default and override (D7c).** Order: the core's own `Picture` in its `.cfg` if offered, else the host default if offered, else `best(mask)`.
The host default is a key of the T163 store (`/Flash/trx/host.cfg`, `[Video] mode=... system=...`, the boot.cfg `vid_*` values read once);
`Handover::video` keeps carrying what is up. `Scale` (Integer / Fit / Fill) is a per-core setting like Picture (T107 D8).

**G14 Applications.** `video_apply_gateware` becomes the only path on a host that answers `0x4F04` (the old firmware calls stay for a Gideon-based
host); `VideoDesc::hdmi_modes` (set by the A1200 descriptor only) is replaced by the probe; `picture_extend` adds Picture and Scale to every core
whose picture goes through the frame buffer; the host app's V key and Picture row call the same code.

**G15 The other items.**
- *T152 D9 (flash gate):* a class `k_lock` in `flash_gate.vhd`: the WRSR opcodes (0x01, 0x31) are passed only when their data bytes are the lock
  (SR1 = 0x34: TB = 1, BP = 101; SR2 = 0x03: SRL, QE; SRP and any other value never), and WREN (0x06) is passed only for that one write; everything
  else stays as T44. `HOSTAPP_FLASH_LOCK` goes to 1.
- *T135 D6a (port lock):* in `top.v:1628-1630`, while `port_lock && !core_decouple` PHI2 and DOTCLK come from two dividers of `sys_clk` (dot4x / 32
  and / 4) instead of 0; every other line stays forced idle and the port's RESET stays low.
- *T159 D5 (lamp, activity):* the A1200's `bling_io` fork (149 vs 66 LUT) replaces the host's; LED 71 is the lamp; `drive_leds_n[0]` flickers it
  off, `[1]` flickers blue; `case_leds` unchanged; `LED_CASE3n` stays 0 (G17 disproved).
- *T152 ESP32 pins:* `esp_ctl.v` of TRX64-Ultimate `t26-esp-ram` (`designs/c64_basic/esp_ctl.v`, high-Z until armed, polarity measured there and in M4)
  with the two ports `WIFI_BOOT` (L13) and `WIFI_DIS` (M13) in `top.v` and `pins.xdc`; the register in the host's IO map.
- *T107 D9 (SID out):* `cores/c64ref` loses `c64_trx_sid_0` (2,976 LUT, 3 RAMB, 20 DSP of run 1104's 11,809) and drives `audio_l/r` from a stand-in;
  the 80 `c64ref` lines of `host_pin.txt` change.
- *T120:* the three "release" comments of `host/build.tcl` (lines 32, 57, 64) and `vendor/icepi/ORIGIN.md`, the rvlite fix (Q11).

**G16 The 50 / 60 Hz families (Q6).** Row = `family * 6 + hdmi`. C64-type cores: family = `v60` (System Mode), rows `locked` only. Frame-buffer
cores: the list contains both families, the machine's first. The CDCE reference follows the System Mode (dot4x), which only C64 cores change
(`coreapp/src/u64cfg.cc:248` is the only writer of `C64_VIDEOFORMAT`).

## 3c Budget: LUT, FF, block RAM, DSP of `pb_l1` and what it does to `pb_l2`

Baseline (run 1104, host `2208e1ad`, last CI result of the host; `host.utilization_pb_l1_opt.rpt`, `host.utilization_hier.rpt`): **11,013 LUT of
14,200 (77.56 %)**, 14,963 FF of 28,400, **24.5 of 25 block RAM tiles (98 %)**, 9 of 60 DSP; WNS +0.142 ns. The "13,000" in the skill and the
template is the older figure; the floorplan-B block is 14,200 (`build.tcl:250-252`). Nothing above 77.6 % routed for `pb_l1` (79.9 % did not,
T133).

| item | LUT | FF | RAMB36 | DSP | basis |
|---|---|---|---|---|---|
| mode timing generator (`hdmi_timing` 448 / 212) less `vic_scaler`'s own counters (about -80 / -50), plus `align` | +370 | +160 | 0 | 0 | A1200 run 1104 hier; the -80 is an estimate (170 total of `scaler`) |
| CDCE per mode (`cdce_mode` 92 vs `cdce_i2c` 67), request sequencer and status register `0x4F04` | +55 | +30 | 0 | 0 | measured +25; the glue is an estimate (the A1200's `vm_*` in `top.v`) |
| pixel layer with the scaler (`pix_layer` 801 vs 569) | +232 | -9 | 0 | 0 | measured, A1200 run 1104 vs host run 1104; includes T136 format 2 |
| menu overlay with the mode frame (`overlay` 370 vs 399) | -29 | +3 | 0 | 0 | measured |
| encoder with `ce`, PR 0, runtime frame (`hdmi` 641 vs 622 in the fork; no `g * cx` terms) | +20 | 0 | 0 | 0 | measured +19 for the fork; the removed terms are an estimate |
| TMDS bit repeater `tmds_rep` (4 lanes x 10 outputs, select by g and t) | +140 | +60 | 0 | 0 | estimate 100-180: about 30 LUT a lane; no measurement possible before the module exists |
| freeze text geometry per mode (`c64_text` 206) | +40 | +30 | 0 | 0 | estimate 20-60 |
| **HDMI and scaler subtotal** | **+828** | **+274** | **0** | **0** | range +650 to +1,050 |
| T152 D9 lock opcode in `flash_gate` (167 LUT) | +45 | +30 | 0 | 0 | estimate 30-70 |
| T160 master volume (`trx_gain` 37 / 72 / 2 DSP) | +40 | +72 | 0 | +2 | measured, A1200 run 1104 |
| T159 D5 lamp and activity (`bling_io` 149 vs 66) | +83 | +82 | 0 | 0 | measured |
| T135 D6a clocks under the port lock | +20 | +20 | 0 | 0 | estimate 15-40 (two dividers, a mux on two pins) |
| T152 ESP32 pins `esp_ctl` | +60 | +40 | 0 | 0 | estimate 40-80 (the module is in another repository) |
| T107 D9 SID out of c64ref, T120, rvlite fix | 0 | 0 | 0 | 0 | none in `pb_l1`; `pb_l2` side below |
| **Package total, without smoothing** | **+1,076** | **+518** | **0** | **+2** | **12,089 LUT = 85.1 %** (range 11,800-12,400) |
| bilinear smoothing for Fit and Fill (Q3) | +350..+650 | +450..+800 | 0 | +6 | T107 §5.7 estimate 4b, not built anywhere |
| with smoothing | about 12,590 | | | | 88.7 % |
| lever: host without `jtag_dbg` + `rv_dbg` (Q2) | -734 | | | | measured, run 1104 hier |

*Block RAM:* 0.5 tile left. The package adds none: the scaler's line buffer is already two RAMB36 in both copies (512 x 128 in the A1200's, the host's
256 x 128 is two RAMB36 at half depth), smoothing fits the deeper half. The only RAM it could free is `vic_scaler`'s ring (4 RAMB36), which Q4 keeps.
*Timing:* host WNS is +0.142 ns today; the A1200's `pix_layer` was pipelined for `pix_mmcm` (run 1051 -0.517 ns, fixed); the repeater is one mux level.

**What `pb_l1` needs.** At 77.6 % (the densest that routed) 12,089 LUT needs a block of 15,580, i.e. **+1,380 LUT**; over the range of the
estimates (858-1,363 LUT for the package) about +1,100 to +1,750. Without `jtag_dbg` (Q2): 11,355 LUT needs 14,630, i.e. **+430**. With smoothing and jtag: about +2,000.
The slices come from `pb_l2`; the block RAM and DSP column ranges stay as they are (`build.tcl:250-252` names them explicitly), so only slice
columns move. Which columns, and whether the DSP and carry geometry allows it, is the floorplan step (WP19 of `docs/todo/T164.md`), not a number I
can give from the reports.

**`pb_l2` after the move (run 1104, 16,800 LUT, 50 tiles, 60 DSP).**

| core | LUT | % of 16,800 | % of 15,420 (move 1,380) | % of 16,370 (move 430) | block RAM tiles |
|---|---|---|---|---|---|
| amiga (A500) | 14,298 | 85.1 | 92.7 **no** | 87.3 **no, above any routed** | 18 |
| nes | 13,181 | 78.5 | 85.5 at the edge | 80.5 ok | 11 |
| c64ref now / after SID out | 11,809 / about 8,850 | 70.3 / 52.7 | 76.6 / 57.4 | 72.1 / 54.1 | 32 (about 29) |
| atari800 | 7,110 | 42.3 | 46.1 | 43.4 | 29.5 |
| cpc | 7,032 | 41.9 | 45.6 | 43.0 | 46.5 |
| vc20, zx, c264, apple2 | 5,216-5,479 | 31-33 | 34-36 | 32-34 | 18.5-42 |
| ddt_mandel, ddt_novic, null | 2,107-4,912 | 13-29 | 14-32 | 13-30 | 0-26.5 |

The highest density that ever routed in `pb_l2` is the A500 at 85.1 %; so **`amiga` (A500) is the core that loses any shrink**, `nes` survives
down to about 15,500 (85 %). DSP: `ddt_mandel` uses 48 of 60 and is not touched by a slice-only move; block RAM tiles (cpc 46.5 of 50) are
not touched either. Moving the SID out of c64ref lowers the reference partner to about 53 %, which makes the host easier to route but frees
nothing for the others (their sizes set the block). The large twin of the A500 case exists: the A1200 (23,916 LUT, 73 %), c128 27,670 (85 %,
75 of 75 tiles), neogeo 26,872 (82 %), snes 22,003, zxnext 20,314 in run 1104 are flat builds and grow by the host delta (about +1,076 LUT,
+3.3 % of the device); the C128 has no block RAM left (75 of 75). The A1200 has most of it already; for the other four the delta is the full +1,076 LUT, which puts c128
(84.9 %) at about 88 % and neogeo (82.4 %) at about 86 % of the device: whether they still route is open until the combined run.

## 3d The plan

The work packages, their benches, dependencies and the board steps are in [`docs/todo/T164.md`](../todo/T164.md) (kept there because it is a
working list, not a decision). The ADRs are [0059](../adr/0059-picture-path-is-the-hosts-for-every-core.md),
[0060](../adr/0060-tmds-bit-repetition-instead-of-pixel-repetition.md), [0061](../adr/0061-one-video-out-for-the-host-and-the-large-cores.md),
[0062](../adr/0062-the-screen-decides-which-modes-are-offered.md), [0063](../adr/0063-pb-l1-grows-cores-that-do-not-fit-become-large.md).

## §4 Out

- A battery-backed clock.
- Moving HANDOVER_ADDR (0x03EFF000): not needed, TRX64 Ultimate copies the page before anything reuses it (T148 D6).
- Nothing else is out by my choice. What I would have left out is a question in §3a: smoothing in the first host build (Q3), modes other than 1080p for
  C64-type cores (Q4), the 50 Hz PC rows (Q5), the family switch for frame-buffer cores (Q6, T107 §4 had it out), the revert timer (Q7, T107 §4 had it
  out). Out by earlier decisions and unchanged: the Amiga's own application (T102), the host UI at the mode's native resolution (T163 D4, package B).

## §5 Decisions taken while building

### S1 NTP (D1), built 2026-10-30
- **What was missing.** The host app already linked Gideon's network settings (`SNTP Enable`, `TimeZone`, `Time Server 1-3`, defaults on / Western Europe / time.windows.com, time.google.com, pool.ntp.org), lwIP's SNTP client, the software clock `rtc_dummy.cc` (last set time + FreeRTOS ticks) and `get_fattime()` on it. The one piece not compiled in was `network/sntp_time.cc` (`start_sntp()`, `sntp_time_received()`): `start_sntp()` was the weak empty function of `network_interface.cc`, so the clock stayed at 1970.
- **Change.** `sntp_time.cc` copied from the submodule unchanged (`vendor.list`, PROVENANCE) and added to `target/u64ii/riscv/hostapp/Makefile`. It reads the existing settings store (`networkConfig.cfg`), so T163's move of the host settings is transparent. It runs whenever an interface comes up and when the network settings are effectuated. `rtc_dummy.cc` (patch `io_rtc_rtc_dummy.cc.patch`): a clock that is not set reads 1980-01-01 00:00:00 in `get_fat_time()` instead of a negative year wrapped into 2097 (the old "Dec 31 2097").
- **Core app.** Not touched: `coreapp` links no `sntp_time.cc`; its stamps are out of D1.
- **Proof.** `tests/hostapp/test_ntp.cc` (in `tests/hostapp/run.sh`) builds the real `rtc_dummy.cc` and `sntp_time.cc` against stubs for FreeRTOS, lwIP and the store: unset clock -> 1980; SNTP answer in Western Europe summer (+2), winter (+1), Phoenix (no DST), zone change without a new answer, ticks advance the clock, SNTP off starts nothing. Six negative controls (SNTP-off ignored, zone ignored, server 3 missing, time dropped, unset year wraps, UTC instead of local) each go red. ue2emu (`--net user`, host app built on the builder, 60 s emulated): DHCP 10.0.2.15, `Setting timezone: CEST-1CET,...`, then `--> Time Received: 1793385491 (current TZ = CEST-1CET,...)`, the real time from the internet. Open: the board run (FTP LIST after a write; control SNTP off).
- **Zone table fixed (Alex, 2026-10-30: "can't we just tie the rule to the zone?" - it already is: each zone's POSIX TZ string carries
  its rule and newlib's `tzset`/`localtime` applies it; FreeRTOS has no zone handling. Then "do it").** Gideon's `timezones.cc` gave
  every zone with summer time the US rule (`M3.2.0`, `M11.1.0`): Europe an hour off for about three weeks in March and a week in
  October/November, Australia and New Zealand in the wrong half of the year, Iran and Tonga a summer time they no longer have, Hawaii
  at UTC-11 and Western Europe with its names swapped. Our copy (`host-app/software/io/rtc/timezones.cc`, MODIFIED, patch in
  `host-app/patches/`) has the IANA tzdata rule of each zone in POSIX form; order and count unchanged (the setting is an index).
  Greenland's real change is at -1:00 local, which newlib's parser does not take; it is `/0` there, one hour late once a year.
  Proof: `tests/hostapp/test_tz.cc` (in `tests/hostapp/run.sh`), 49 instants around the EU, US and southern changes:
  `test_tz: all passed`; Gideon's table: `test_tz: 20 FAILED`; control `tz_us_rule` (Western Europe with the US rule) fails.

### WP1 mode table tool (G3, G4, G5), built 2026-10-31
- **Built.** `tools/hdmi_modes.py` got a host edition (`host-gen`, `host-table`, `host-check`, `host-selftest`; `check` and `selftest` run both editions, the
  A1200 edition and `cores/amiga1200/mono/hdmi_modes.vh` are byte-identical to before). It writes `host/hdmi_modes.vh`: the 9 rows of Q5 (index
  `family * 6 + hdmi`, rows 3-5 of family 0 do not exist: `hm_valid` 0, every function 0), functions `hm_valid`, `hm_row` (same 105-bit layout as the A1200),
  `hm_aspect` (AVI M1:M0), `hm_locked`, `hm_rate` (the PIXEL clock `f_tmds / g`, for N/CTS), `hm_cdce` (PAL reference, frame-buffer cores), `hm_cdce_lock`
  and `hm_rate_lock` (only the locked rows, for the reference of the row's family). Wired into `ci/lint.sh` ("hdmi modes (A1200 + host)"); `contract/host_pin.txt` updated.
- **Rules checked per row.** TMDS clock 80..165 MHz (CDCE output), pixel clock >= 25 MHz, gear in {1, 2, 3, 5}, GTP PLL VCO 1.6..3.3 GHz at N = 20, GTP line rate
  `10 * f_tmds` inside 0.5..3.3 Gb/s, `mmcm_pix` VCO 600..1200 MHz, CDCE bytes decoded back from the file (M integer, P, Q, R, range bits, output within
  1000 ppm), CEA raster of the VIC (1, 17, 4, 19, 16, 31), AVI aspect (Q10: 4:3 for VIC 1 and 17, 16:9 for the HD rows, none for the PC rows), blanking.
  **GTP line-rate minimum:** DS181 (7 Series FPGAs DC and AC Switching Characteristics, GTP transceiver performance) 0.5 Gb/s, PLL VCO 1.6..3.3 GHz; the
  wizard's fixed dividers (N = 20, D = 4, line rate `10 * refclk`) make the VCO bound the tighter one, 0.8 Gb/s. The numbers are from the data sheet as
  known to the author; the PDF is not in the repository and was not opened for this entry - the board step or a reader with DS181 must confirm them.
- **Lock proof (G5, Q4).** `N/M = C / V * 104/125` as `Fraction`s, `C = g * htot * vtot` TMDS clocks, `V` dot4x clocks per VIC frame (PAL 63 x 312 x 32, NTSC
  65 x 263 x 32); the CDCE setting is the exact ratio (scaled while N <= 4095, M <= 511 and the register rules hold). Result: 1080p50 55/14, 1080p60 990/263
  (NTSC reference, `hm_cdce_lock`), also exact: 720p50 55/14, 576p50 25/7, 720p60 990/263, 800x600@60 78/25; not lockable: 480p60 (N > 4095, as worked in G5),
  1024x768, 1280x1024. Only the two 1080p rows carry the flag (Q4). Note: for 60 Hz a frame-buffer core runs the PAL reference, so the row's free
  `hm_cdce` (676/173) differs from `hm_cdce_lock` (990/263, NTSC reference); the consumer picks by who owns the row.
- **VIC code of 480p.** The row is 640 x 480 at 25.175 MHz, which is CEA VIC 1 (4:3); Q10 says "VIC 17 / 2", but VIC 2 is 720 x 480 at 27 MHz. The tool
  checks the raster against the VIC and keeps VIC 1.
- **Controls.** `selftest`: 25 host mutations, all caught (gear/clock out of range, each of TMDS ceiling and floor, line-rate minimum, mmcm VCO, HDMI minimum,
  a 50 Hz PC row added, a row dropped, aspect, wrong VIC, PC aspect, blanking, lock flag on 480p60 and on 720p50, VIC frame one line longer, rate as TMDS
  clock, lock bytes N + 16, NTSC lock row on PAL bytes, `hm_locked` on 720p50, the next row's CDCE bytes, aspect in the file, stale file, Q/R register swap).
  Last lines: `PASS hdmi_modes host: 9 rows inside every limit ...` and `host selftest: 25 of 25 mutations caught`.
### S14 port lock runs the clock (WP10, T135 D6a), built 2026-10-31
`host/portlock_clk.v` replaces the three assigns of PHI2 / DOTCLK / RESET in `top.v`: under `port_lock` a 5-bit counter on `sys_clk` (dot4x of
the System Mode, so PAL and NTSC follow `mode_ctl` without a table) gives PHI2 = dot4x / 32 (high in counts 1..16, as `slot_port`) and
DOTCLK = dot4x / 4; RESET stays low; `core_decouple` forces both low. Hand-over in both directions, per line: wait until the line in use is low,
hold the output low for one full low time (16 / 2 clk), switch while the new source is low - no runt, a longer first low phase only.
Every other port line is unchanged (still `port_off ? idle`). Bench `sh host/sim/portlock/run.sh` (`ci/sims.txt` `portlock`, tier 1): PAL
`PHI2 985.2 kHz, DOTCLK 7882.0 kHz` `PASS portlock 31718 ps`, NTSC `PHI2 1022.7 kHz, DOTCLK 8181.7 kHz` `PASS portlock 30557 ps`; 80 release
phases (core parked / running) and the re-lock: no pulse shorter than a full one. Controls (`MODE=ctl`): `CTL_OLD` (forced low)
`FAIL portlock: 37 errors`, `CTL_RAW` (switch without the hold) `FAIL portlock: 10 errors`. The other port lines are not in the bench (their
lines in `top.v` did not change); board: S9 / S14 by Alex (a Kung Fu Flash starts at the first core start). Estimate 15-40 LUT (counter, two
small hold counters, three muxes); synthesis number follows with the Vivado run.
### WP12 T159 D5 power lamp and activity (S16 lamp part), built 2026-10-31
- **Host:** `host/bling_io.v` is the A1200's fork (`cores/amiga1200/mono/bling_io.v`), header comment made generic; `host/top.v` feeds it the way the
  A1200's `top.v` does: `drive_act` = `cled_s2[2]` (`!drive_leds_n[0]` synchronised into `cpu_clk`), `hd_act` = `!drive_leds_n[1]`, `lamp_en` =
  `caseled_set && caseled_sel[3:0] == 4'd4` (the app's "LED Load Indicator" On). `cled_s1/s2` are declared before the instance. `case_leds` and
  `LED_CASE3n = 0` unchanged. No port, contract VERSION stays 2 (Q8); the convention is in `contract/README.md`.
- **A1200 fork kept (WP18 deletes it):** `mono/bling_io.v` is now byte-identical to the host's file and `mono/bling_io.diff` is empty, so
  `check_wrapper_fork.sh` stays green; `amiga1200_bling` still passes (nine controls).
- **Bench:** `host/sim/bling/run.sh` (`bling_lamp` in `ci/sims.txt`, lane host): `tb_bling_lamp.v` decodes the WS2812 frame (16 states: Off dark / amber,
  On red steady, floppy flicker, HD blue flicker over floppy, SHIFT LOCK yellow, 10-clock pulses held). Controls: `lamp_stuck`, `no_flicker_floppy`
  (activity ignored), `no_flicker_hd`, `hd_not_split`, `hd_below_floppy`, `no_hold`, `enable_ignored`, `no_priority`, and four mutations of `host/top.v`
  (`drive_act`, `hd_act`, `lamp_en` tied off, `drive_leds_n[0]` not into `cled_s1`) that the wiring check must catch. Result: `PASS tb_bling_lamp errors=0`,
  all 8 controls `ctl| ok ... failed`, 4 wiring controls ok, `PASS host_bling_lamp`. `tb_bling_io` ties the new inputs off and passes.
- **LUT:** 149 vs 66 (+83), the figure of §3c; no Vivado run here.
- **Open for the cores:** `drive_leds_n[1]` is the 1541's motor LED in `c64u`, `c128`, `c264`, `c64ref`, `trx64` (`drive_l2.vhd` `leds_n` = activity, motor,
  dirty): with the lamp on those cores would flicker blue on every motor run. D5 excludes TRX64 and the Gideon core; `c128` / `c264` have to mask bit 1 in
  their `layer2` (`{leds_n[2], 1'b1, leds_n[0]}`) before they enable the setting. `contract/host_pin.txt` is not re-pinned (WP14, at the end).

### WP13 T152 S8 ESP32 boot control (S16 esp_ctl part), built 2026-10-31
- **Host:** `host/esp_ctl.v` is `designs/c64_basic/esp_ctl.v` of TRX64-Ultimate `t26-esp-ram` (read-only, header adapted, logic unchanged: pin = uart_dma
  bit XOR polarity bit, high-Z until ARM). `host/ultimate_sys.vhd`: new region `r_esp` at 0x060910-0x06091F (CPU 0x10060910, free until now; `r_wifi` ends at
  0x06090F), ports `esp_we/re/addr/wdata/rdata` (`esp_rdata` has a default of 0, so `tb_dma_bus`, `tb_input`, `tb_usb` are untouched) and `wifi_boot` /
  `wifi_enable`, which are uart_dma's `boot` / `enable` outputs (flow control bits 4 / 5, were `open`). `host/top.v`: ports `WIFI_BOOT`, `WIFI_DIS`,
  the `esp_ctl` instance on `cpu_clk` / `cpu_rst`, `assign WIFI_BOOT = esp_oe ? esp_boot_o : 1'bz` (same for DIS). `host/pins.xdc`: L13 / M13, LVCMOS33, no pull.
  `host/build.tcl` reads `esp_ctl.v`. The mono tops of the large cores are not touched.
- **Register map for the host app (T152 S8):** +0 R/W b0 ARM, b1 INV_BOOT, b2 INV_DIS (reset 0); +1 R b0 level on WIFI_BOOT, b1 on WIFI_DIS, b2 driven;
  +2 R 0xE5; +3 R version 1. Flow: write ARM, then the app's `ModuleCtrl` modes through the WiFi UART's flow control (OFF 3 = BOOT and DIS high, BOOT 1,
  RUN 0). Polarity as measured in M4: BOOT high = ROM loader at the next reset, DIS high = ESP32 in reset, both low = run. Not armed, nothing is driven,
  so a build without the app's S8 flow behaves as before (the pins were unconstrained). A `cpu_rst` (also `machine_ctl[2]`) disarms again.
- **Bench:** `host/sim/esp_ctl/run.sh` (`esp_ctl` in `ci/sims.txt`, lane host, tier 1): `tb_esp_ctl.v` is the other repository's bench plus an ESP32 model on the
  pins (EN held by DIS, GPIO0 sampled by BOOT when DIS falls) and the app's sequence RUN -> OFF -> BOOT (ROM loader) -> OFF -> RUN, a reset in the loader
  state freeing both pins within one clock, and an edge counter on the undriven pins. Controls: seven mutations of `esp_ctl.v` (always driven, BOOT / DIS
  polarity inverted, reset does not disarm, INV bits ignored, wrong ID, BOOT / DIS swapped) and ten of the wiring (no `oe` gate in `top.v`, uart_dma bits
  not fed, port missing, `boot` / `enable` open in `ultimate_sys.vhd`, no region, no read path, wrong address, either XDC pin moved).
  Result (builder, `trx64-sim-env:4`): `PASS tb_esp_ctl errors=0`, 7 + 10 controls `ctl| ok`, `PASS host_esp_ctl`.
- **LUT:** estimate +40..80 (§3c), no Vivado run here. The flashing flow in the app (T152 S8 UI) was built later: T152 spec 5.7. **Board:** the ROM-loader sync of M4
  was proven by the other repository's core; the host's path (this register, then ModuleCtrl) is a board step. `contract/host_pin.txt` is re-pinned
  with `tools/host_pin.sh --update` in this commit.
### WP11 T160 D1 master volume in the host (S1 / S2), built 2026-10-31
- **Gateware.** `host/trx_gain.v` (the A1200 trial module, moved): `out = sat24(in * vol / 128)`, one DSP per channel, volume byte through two flip-flops and
  accepted when two reads agree. `host/top.v`: `trx_gain mgain` between the core's `audio_l/r` and `snd_l/r` (`snd_l <= pause ? 0 : gain_l`), so HDMI and
  I2S both carry the gained sample; the pause cut stays after the gain. The register is **not** `mix_l/mix_r`: those are `max(main_gain)` of the app's
  UltiSID mixer bytes (0x1005_00 / 01 / 04 / 05, reset 0x80), which the Ultimate app also writes and which already reach the SID mixer through `sid_gains` -
  applying them again would count a C64 core's volume twice. A new byte `master_vol` in `ultimate_sys.vhd`: `U64_AUDIO_MIXER + 0x50`
  (`io_req.address = 0x100550`), write only, reset `X"80"` = unity, new output port (the other benches' instances leave it open). The mono build lists of
  snes, zxnext, neogeo and c128 read `trx_gain.v`; c128's copies of `top.v` / `ultimate_sys.vhd` are not rebased here (WP18), so the C128 has no master
  volume until then. The A1200 keeps its own `trx_gain` / `VOL_CFG` until WP18 (its host copy is not read by its flat build).
- **Bench** `host/sim/gain` (`ci/sims.txt` `host_gain`, tier 1): `tb_gain.v` is the A1200 bench against `host/trx_gain.v` (unity exact for 0, +-1, the extremes
  and 200 random samples, 0 silent, 64 half, 32 quarter with the channels apart, 255 = 1.992x and saturating on both signs); the wiring check greps `top.v` and
  `ultimate_sys.vhd` (gain instance on `audio_l/r`, `snd_l/r` from the gain, `master_vol` wired, reset `X"80"`, address 0x50). Controls: `TRX_GAIN_MUT_BYPASS`
  (`9 FAIL lines`), four mutations of `top.v` (`snd_l` / `snd_r` from `audio_l/r`, volume tied to 0x80, `master_vol` left open) and three of `ultimate_sys.vhd`
  (reset 0, address 0x51, output tied to unity), each caught by the wiring check. Last line: `PASS host_gain (unity exact, 0 silent, half, saturation; the
  bypass control and the wiring controls fail)`. LUT/FF/DSP: the A1200 measurement (37 / 72 / 2), no Vivado run here.
- **Application.** `CoreIo::master_volume(uint8_t)` (default: ignore; `hw.cc` writes `U64_AUDIO_MIXER + 0x50`). `coreapp/src/volume.{h,cc}`: the application adds a
  "Volume" item (41 steps 0..200 %, default 100 %, `WHEN_LIVE`, `apply = volume.apply`, group `audio`) after Picture to every core whose descriptor has no
  "Volume" of its own (the A1200 until WP18) and no `CoreDesc::own_volume` (set on the C64 descriptor: its mixer, T160 P4); the byte table is the A1200's
  (`volume_byte`, checked step for step against it). The operation writes the register, the .cfg keeps the value, the start sequence writes it again
  (`regset_apply`). `MenuCtl::change_volume` takes the generic item as well as a bound one.
- **Feedback on every page.** The status row exists on the list pages only (board finding 2026-10-31). `MenuCtl::toast()` keeps the line "Volume 110%" for 2 s
  (`toast_text()`, `toast_expire()`, `toast_clear()` on menu close); `ui_toast_render` draws it as a card in the overlay container (like the job bar) at the
  foot of the screen over whatever page is open; the main loop polls `toast_expire()`. The status row still gets the line.
- **Tests.** `tests/coreapp/test_volume.cc` (S2): bytes, generic item added / not added (A1200, `own_volume`), step -> register byte, .cfg and restart, USB keys,
  clamps, a toast after the key on each of F1..F8, expiry at 2000 ms, `toast_clear`. Controls `vol_gen_nowrite`, `vol_gen_table`, `vol_gen_none`, `vol_gen_own`,
  `vol_gen_dup`, `vol_no_toast`, `vol_toast_stays`, `vol_generic_keys`. The render bench has a block for F1..F7 (card pixels at the foot change with the
  key, gone after expiry) and the control `UI_NO_TOAST`.
- **Board (by Alex, with the next host):** S3 on a non-A1200 core (SHIFT+/- audible on HDMI and analogue, line visible on Home / Library).

### WP3 TMDS bit repeater (G4, S3), built 2026-10-31
- **Built.** `host/tmds_rep.v` (clk = `pix_clk`, usrclk, rst, `ce`, gear, three 10-bit symbols -> four 20-bit GTP words incl. the clock lane): in clock t of a
  pixel the 10 line bits k are symbol bit `(10 t + k) / gear`, an AND-OR of constant indices per (gear, t, k); the clock lane takes the symbol
  `00000_11111` through the same path. Own phase counter `ph` (0 in the first clock after `ce`, which is high in the last clock of a pixel as in
  `hdmi_timing.v`; held at 7 when `ce` is missing, then zeros). The word pairing is the A1200's (older clock in the low half), without its extra
  `sym_pair` stage (80 FF less); lane polarity (the swapped blue lane) stays in the caller. The caller (WP4) must change the symbols on the edge that
  ends the `ce` clock. Gear 4 and any other value send zeros.
- **Bench** `host/sim/tmds_rep` (`ci/sims.txt` tier 1, lane host): unpacks the words into the serial bit stream and checks, for gear 1, 2, 3, 5, 3 (each
  after a reset that changes the gear), 3000 random pixels, 4 lanes: every line bit against the symbol (`bit j / gear`), the middle line bit of each
  group of `gear` against the symbol (the decode of S3), no symbol twice in a row. Controls (sed mutations of the module, each must fail on a check): one
  line bit off per symbol, phase slipped by one clock, pixel repetition, phase table off by one at gear 3 (caught by the full line-bit compare only,
  the middle-bit decode misses it). Last line: `PASS tmds_rep (gears 1 2 3 5 3 exact, 4 controls failed)`.
- **Size.** yosys `synth_xilinx` (xc7, module alone, symbols as inputs, no Vivado): 223 LUT (17 LUT2, 28 LUT3, 37 LUT4, 49 LUT5, 92 LUT6, 20 MUXF7, 7 MUXF8,
  1 CARRY4), 131 FF. §3c carries +140 LUT / +60 FF; this is +80 LUT over it (not Vivado; the FF figure includes the 80 word FF the A1200 has too).

### WP4 encoder fork (G4), built 2026-10-31
- **Built.** `host/hdmi_enc/` holds the six changed files of `vendor/icepi/hdmi` (`hdmi.v`, `packet_picker.sv`, `auxiliary_video_information_info_frame.sv`,
  `audio_clock_regeneration_packet.sv`, `packet_assembler.sv`, `tmds_channel.sv`) with one `*.diff` against the vendor file each; headers and licences kept,
  `vendor/icepi/hdmi` untouched (`ORIGIN.md`, `PROVENANCE.md` say so). Changes: `ce` on every register (the disparity of `tmds_channel` too; `audio_sample_en`
  is one pulse per sample), `cx` / `cy` in pixels with the frame as ports (`sx_e`, `fw_m1`, `sy_e`, `ey_e`), the island at cx 4 / 12 / 14 / 14 + 32 n in every
  mode, `MAX_PACKETS` 3, `aspect` port into the AVI InfoFrame (PR stays 0, no `pr` port), ACR counts CTS in pixels, X in the NULL / ACR packets is 0.
- **Found by the bench, fixed in the fork.** The vendor module decides guard band, preamble and island windows on `cx` but they leave the encoder one pixel
  after the pixel data and the sync bits (four register stages against three): the guard band overwrote the first active pixel and the sync bits ran one
  pixel ahead of the island. The windows now use the next `cx`; the bench checks pixel values and sync at the pixel.
- **Blanking numbers (the table, not the spec text).** The tightest row is 576p50 with 144 pixels (480p60 is the 640 x 480 row: 160). The island ends at
  cx 112, the video preamble starts at B - 10 = 134 (22 pixels spare, 4 are the minimum). Three packets are the limit there: (144 - 30) / 32 = 3.
- **Bench** `host/sim/hdmi_enc` (iverilog, builder, tier 1, ~17 min with 4 jobs; `ci/sims.txt`, `.deps`, `.lane`): a generator with the counters of `hdmi_timing`
  drives all 9 rows at their gear with `ce`; the bench decodes the three TMDS channels of the third frame: symbols change only after a `ce`; preamble, guard
  bands, control periods with the sync bits and every video pixel (TMDS decoded) at their cx; every island (guards, 3 packets, BCH parity of header and
  subpackets, ends 4 + 10 clocks before the video); AVI (checksum, PR 0, VIC and aspect of the row), ACR (N 6144, CTS x 128 x 48000 / N within 100 ppm of the
  pixel clock for every packet and the mean), audio sample packets (no sample lost or repeated), one AVI / SPD / audio InfoFrame per frame, one island per line.
  Result: `hdmi_enc PASS: 9 rows decoded, 4 controls failed as they must`. Controls on 576p (gear 5): AVI PR = 4 -> "AVI PR"; ACR counting TMDS clocks ->
  "CTS is not the pixel clock"; `tmds_channel` without `ce` -> "symbol changed between two pixels"; `MAX_PACKETS` 4 -> "island does not leave" (4 control + preamble).
- **For WP8.** Input contract = the bench generator: `cx = 0` is the first pixel after the active area, all inputs held for a pixel; `sx_e = B`, `fw_m1 = htot - 1`,
  `sy_e = Bv`, `ey_e = vtot`; `video_rate` = `hm_rate`, `aspect` = `hm_aspect`; latency 3 pixels. Not instantiated in `host/top.v` yet (WP8).

### WP2 timing generator (G5), built 2026-10-31
- **Built.** `host/hdmi_timing.v` is the A1200's generator reading `host/hdmi_modes.vh` (9 rows, `hm_row`/`hm_rate`/`hm_locked`); the A1200 copy is untouched
  (WP18). `rate` is now the PIXEL clock (`hm_rate`), the A1200's was the TMDS clock. New: `align` (clk pulse): on a row with `hm_locked` (the two 1080p rows,
  gear 1) the frame restarts at the active origin (`hc <= xact0`, `vc <= yact0`; the pixel after the one registered at the pulse clock is cx = xact0, cy = yact0), on every other row
  it is ignored; `aligned` (set by the first accepted pulse, cleared by rst). It acts only at a `ce` clock, which is every clock on the gear-1 rows.
  **A mode outside the table** (`hm_valid` 0: family 0 with hdmi 3..5, 12..15) is refused: `valid` is 0, `running` and `ce` never go high (the design names
  no other behaviour; the consumer, WP7/WP8, must not request one).
- **Proof.** `host/sim/hdmi_timing/run.sh` (`ci/sims.txt` tier 1 `host_hdmi_timing`, builder, `trx64-sim-env:4`): all 9 rows (clocks, pixels, active pixels,
  per-pixel cx/cy/de/hsync/vsync position, width, polarity, pixel held gear clocks, `sx_e`/`fw_m1`/`sy_e`/`ey_e`, `ph_e`, frame rate pixel clock / pixels against
  the table, 0.1 %), the expected geometry written out in the bench, not read from the .vh; align pulse (held `gear` clocks) at cx 500, cy 300 on all 9
  rows, one frame checked after it; the 7 refused modes. Controls (each fails on a check): `hs_wide`, `ph_short`, `ph_e_off`, `vfp_short`, `align_off`
  (mode 8), `align_ungated` (mode 6, a non-locked gear-5 row jumps), `align_origin` (mode 2, off by one pixel), `refuse_off` (mode 3 runs).
  Last line: `PASS hdmi_timing (9 modes exact, align on 9 rows, 7 refused, 8 controls failed)`.
- **LUT.** Not synthesised here (no Vivado in this package); the cost over the A1200's 448 / 212 is the `align` load of `hc`/`vc` (two 2:1 muxes on 23 flops)
  and `valid`; the G5 budget line (+370 / +160 for the whole item) is unchanged until WP19 measures.
### C128 copy: vtot_m1 second stage (run 1142), built 2026-11-02
- **Report lines (run 1142, main b2f22c7c, only the C128 large core red):** `WNS -0.095 ns` after post-route phys_opt (AlternateReplication, AggressiveFanoutOpt); worst path in `pix_mmcm` (6.667 ns)
  `hout/mtim/row_reg[13]/C -> hout/mtim/vtot_m1_reg[8]/D` (8 levels: CARRY4 4, LUT2 3, LUT4 1), `vtot_m1_reg[9]` -0.077, `[10]` -0.040. The other large cores meet the same source: density in the C128.
- **Change (`cores/c128/mono/host/hdmi_timing.v`, only this copy; `host/` and the other copies are untouched):** `vtot_m1 <= yact0 + vact - 1` from the registered `yact0` / `vact` (a two-term sum)
  instead of `r_vfp + r_vsync + r_vbp + r_vact - 1` from `row`. `vtot_m1` settles one clock later (chain: m, row, yact0/vact, vtot_m1). Its only reader is `vc == vtot_m1` at the end of the first line,
  hundreds of clocks after `running` (which waits 9 clocks after rst falls); rst is held 6 clocks by the bench, longer in the hardware.
- **Proof.** `ci/sims.txt` `c128_hdmi_timing` (`HOSTDIR=cores/c128/mono/host`, the same bench as the host's): 9 modes exact (clocks, pixels, active pixels, cx/cy/de/hs/vs, sy_e/ey_e), plus `vtot_m1 == vtot - 1` at the first
  edge with `running` (new bench check, all copies), a run with rst held 2 clocks (mode 9 passes), controls that must fail: `vtot_off1` (vtot one line short); `vtot_early` (running at wait_n 0 with rst 2, mode 9: vtot_m1 not yet valid; mode 2 would not show it, the reset mode primes the chain).

### S15 flash gate lock class (WP9, T152 D9), built 2026-10-31
- **Change.** `host/flash_gate.vhd` got `g_lock` (default false, `ultimate_sys.vhd` sets true). 0x50 is forwarded and arms the gate; 0x01 / 0x31 are held like a
  checked opcode until the value byte is known. 0x01 goes out only when armed, once per reset, with SR1 = `001101xx` (SRP 0, SEC 0, TB 1, BP 101; bits 1:0 are read-only
  in the chip); 0x31 only when armed, once, with SR2 = `000000x1` (SUS, CMP and LB1-3 clear, SRL set, QE either way). Anything else of 0x01 / 0x31 goes out as 0x9F and
  counts as a block. Any other opcode after 0x50 drops the arm. After a lock write the pin is cut high at once (`cut`, released when SSn rises), so the third byte of a
  16-bit SR1 write (which would write SR2: CMP, SRL, LB) is clocked with the pin high and never reaches the chip. Register 0x17 = `000000 lk2 lk1` (which lock writes passed);
  register 0x16 (allowed count) counts them. LB1-3 are one-time-programmable on the chip, so a chip whose SR2 already has LB set cannot be locked: the gate refuses the write,
  the app reads it back as not set and refuses the core start (nothing is written).
- **App.** `HOSTAPP_FLASH_LOCK` default 1 (`flashlock.h`); the F8 row and the lock before a large core are in the board build. The toggle stays (0 hides both). The stored
  setting still defaults to off (T152 D9 text); whether it goes back to on (T152 Q4) is not decided here. `tests/hostapp/test_flashlock.cc` checks that the app's own lock
  frames are exactly what the gate passes (`gate_viol == 0`).
- **Bench.** `host/sim/flash_gate/tb_flash_lock.vhd` + `flash_model.vhd` (g_lock: SR1/SR2, volatile enable, 16-bit write, SRL lock-down, SRP+SRL counted as a violation), in
  `run.sh` for g_wren_pass false/true and the three model variants: case 0 the app's lock, 1 SR1 values (SRP, SEC, other BP, TB clear), 2 SR2 values (CMP, LB, SUS, SRL clear),
  3 no / spent / double enable and a second sequence, 4 third byte and values missing, 5 SRP after SRL, 6 random frames. Controls are ten mutants of the gate (sed on a copy);
  each has to turn its case red: SRP unchecked, BP/TB/SEC unchecked, every 0x01 passes, CMP unchecked, LB unchecked, SR1 once missing, SR2 once missing, enable not needed,
  enable survives a read, no cut. Result line: see the commit message of the bench run.
- **Not done here.** Board: T152 S2 / S5 / S6b / S7 / Q5 with the lock on (expected status `34 03`). `tools/flashgate_test.py` stage 1 still expects 0x50 / 0x01 / 0x31 to be
  blocked: it runs against the T44 test gate (`g_lock` false), not the host's.
### WP6 overlay and C64 text follow the mode (G7, S9), built 2026-10-31
- **`host/overlay.v`:** the A1200 diff applied (ports `ce`, `hact`, `vact`, `xact0`, `yact0`; every pixel-side register under `if (ce)`), the 0x4F00 mode-request
  register of that fork left out (the host's own request path is G2). Deviation from the G7 text, which S9 contradicts (a 960 x 540 canvas at factor 2 does not fit
  min(1280, hact) x min(800, vact)): the box may cover the whole active area (hact x vact). SY is the largest whole factor 1..4 that fits the height, SX the
  largest that fits the width but at most 2 x SY (keeps the 2 x 1 of the 12 x 23 settings). Centred, cut at the right / bottom when larger than the active
  area (never shifted), and the blend is gated by `de` so nothing is drawn outside the active area.
- **`host/c64_text.v`:** parameters `XS` / `YS` replaced by `ce` / `hact` / `vact` / `xact0` / `yact0`; scale ST = min(4, hact / 320, vact / 200), origin
  (xact0 + (hact - 320 ST) / 2, yact0 + (vact - 200 ST) / 2); the sub-line and sub-pixel counters run to ST - 1; pixel side under `if (ce)`.
- **`host/top.v`:** until `hdmi_timing.v` is in the host, both instances get `ce = 1` and the 1080p frame (1920 x 1080 at 768 / 45) as constants, which
  gives the former 1920 x 1080 behaviour for the C64 text (4, 1280 x 800 at 1088 / 185) and the overlay (WP2/WP3 replace the constants with the timing's outputs).
- **Bench:** `host/sim/ovl_geom/run.sh` (`ovl_geom`, tier 2, lane host): `tb_ovl_geom.v` runs each of the nine rows with a frame generator of the
  `hdmi_timing.v` convention (gear 1/2/3/5), a 960 x 540 canvas in the overlay, and the C64 text with every even C64 pixel column lit; it checks edges and pixel
  counts against values computed from the row alone, the lit-column pattern (ST wide), `de` against the counters, nothing outside the active area, and that
  the outputs change only after a clock with `ce`. Result: all nine `OVL_GEOM mode N PASS` (e.g. 1080p: overlay 2 x, 1920 x 1080; text 4; 720p: overlay 1 x at
  160 / 90, text 3; 576p: overlay cut at 720, text 2), `OVL_GEOM ALL PASS`. Controls (each FAILs): `c_noce_gear2` / `c_noce_gear5` (ce ignored in both
  modules, 720p and 576p), `c_1080_in_720` (modules told the 1080p frame), `c_round_up` (factor / scale rounded up), `c_ovl_not_centred`, `c_txt_not_centred`.
- `tests/coreapp/render/run.sh` passes unchanged (108 checks, controls fail as they must): it renders the application's menu and does not touch the gateware, so
  the per-mode `a1200-settings-*.png` of S9 belong to the application work that draws at the mode's size (D10), not to this package.
- **LUT:** not measured (no Vivado here); the A1200 figure for the overlay diff is 370 vs 399; the text layer gains registers for scale and origin (not measured).


### WP7 mode request and status (G2), built 2026-10-31
- **Built.** `host/mode_req.v` (new): the request `0x4F00` (`family*6+hdmi`), the crossing into clk50, the accept / refuse check, the 8-clock hold (`vm_rst`), `mode_pix`
  (the mode for `hdmi_timing`, crossed into the pixel clock) and the read-only status `0x4F04..0x4F09`: magic `0x48`, version 1, flags `{last request refused, 0, 0,
  c64_type, row in use locked, running, pix_locked, gt_ready}`, the mode `hdmi_timing` runs (`mode_q`), requests written (clk_sys) and processed (clk50, so equal = the refused
  bit is current). `host/cdce_mode.v` (new) is the A1200's with a `lock` input: `lock = 1` writes `hm_cdce_lock`, `lock = 0` `hm_cdce`. A request with bits 7:4 set, above 11 or
  not a row of the table (`hm_valid`; family 0 with hdmi 3..5) is refused: no hold, no CDCE write, nothing changes. A C64-type core (`c64_type = 1`) is held to the System
  Mode's family (`fam_v60` = `mode_ctl.cdce_v60`: the family part of the request is replaced) and accepts locked rows only (Q4: 1080p); a change of `fam_v60` restarts the
  CDCE on the other family's row without a request. `mode_ctl.v` is unchanged (it already outputs `cdce_v60`); `overlay.v` is not touched (WP6 owns it): `mode_req` takes the
  same `we / addr / wdata` and answers with `rhit / rdata` one clk_sys after `addr`, which WP8 muxes over `overlay.rdata`.
- **Not wired (WP8).** `host/top.v` still has `cdce_i2c`; WP8 swaps it for `cdce_mode` + `mode_req`, feeds `c64_type` (core class), `gt_ready`, `pix_locked`, `hdmi_timing.running / mode_q`.
  Status note for the application: `running` is stale while the pixel clock is away, so poll `pix_locked && running && mode == requested` (not `running` alone).
- **Bench.** `host/sim/modepath/run.sh` (`ci/sims.txt` tier 1 `host_modepath`): path with a frame-buffer core (0..15 after every other, 0x10 / 0xFF / 0x12 / 0x8C; accepted
  requests reach `vm_mode`, `mode_pix` and `hdmi_timing` with the clocks per frame written out; refused ones change nothing and never raise `vm_rst`; the status
  register read after every request), path with a C64-type core (only rows 2 and 8, family from `fam_v60`, family change restarts), and the decoded CDCE I2C bytes of every
  row (two transactions `C8 14 08 57 00 01 01` + 4 bytes, written out in the bench, the locked rows 2 / 8 with `hm_cdce_lock`: row 8 `3D E0 F3 CD`). Controls (each fails on a
  check): `plus1`, `stale` (mode_pix not following), `accept_invalid`, `mode_stale` (status mode), `count_stale` (status processed count), `c64_any`, `wrong_row` (CDCE row +6),
  `lock_ignored`. Last line: `PASS modepath (fb and C64-type paths, i2c bytes of 9 + 2 rows, status register, 8 controls failed)`.
- **LUT.** Not synthesised (no Vivado here). Estimate: the two crossings, two 8-bit counters, the 6:1 status mux and the check: about 60-90 LUT with the CDCE sequencer's +25 measured in
  the A1200 (§3c line +55 / +30): within the line until WP19 measures.
### WP15 EDID, the row mask, best row and the no-EDID list (G11, S10), built 2026-10-31
- **Code:** `app/video/edid.{h,cc}` (pure). `edid_parse` reads the base block (established 800 x 600 / 1024 x 768, standard timings incl. the 0xFA
  descriptor, detailed timings, range limits 0xFD) and every CEA extension (short video descriptors with the native flag, detailed timings, HDMI vendor
  block max TMDS clock) into `EdidInfo{valid, has_cea, mask, native, max_tmds_mhz}`. `video_modes_offered()` is the row mask (bit m = family * 6 + hdmi,
  rows 3-5 never); `video_modes_best(mask, sys)` is Q6 (machine's family first, 1080p > 720p > 576p/480p > PC with 1024 x 768 first, the other family
  only when the family has nothing); `video_modes_default()` is Q9 (no/corrupt EDID, or a CEA block naming none of our rows: the six television rows,
  default 576p/480p of the machine's family). `video_mode_from_edid()` (old single-family answer, same signature) moved from `videomode.cc` to `edid.cc`
  and answers from the mask; both Makefiles of the RISC-V apps got `edid.cc`.
- **Decisions taken in the code (to be confirmed):** (1) A detailed timing names a row by active size, frame rate within 1 Hz and pixel clock within 5 %,
  not the 1 % of G11: VESA 800 x 600 is 1056 x 628 at 40 MHz against our 1052 x 650 at 41.05 MHz (3.5 % in the frame height), 1280 x 1024 is 1688 x 1066
  at 108 MHz against 1688 x 1052 at 106.5 MHz (1.3 % / 1.4 %); a 1 % rule would drop both from every real monitor. (2) Range limits veto only rows not named
  by a short video descriptor (CEA timings are outside their scope); the TMDS limit vetoes every row. (3) 640 x 480 in the established timings is not
  mapped to row 6 (it would beat every PC row in the order and put a monitor on 480p). (4) A checksum error in any block read makes the whole EDID corrupt
  (Q9 list). (5) A valid EDID that names nothing: 1024 x 768 60 Hz when it has no CEA block (the T105 rule), the television list when it has one.
- **Risk for the board capture (S18):** a television that sets the established bits for 800 x 600 / 1024 x 768 would offer those PC rows; the Sony is said to
  refuse them (Q5). The fixture decides whether rows from established/standard timings count when a CEA block is present.
- **Test:** `tests/coreapp/test_edid.cc` (S10; synthetic EDIDs: television, 60-Hz-only television, 50-Hz-only television, PC monitor without CEA,
  real 1080p / 800 x 600 / 720p50 / 1280 x 1024 detailed timings, range-limit and TMDS vetoes, corrupt base / extension checksum, short read, no EDID,
  lengths walking out of the block; plus the table against `host/hdmi_modes.vh`: raster, pixel clock, VIC). 160 checks; 12 controls in `tests/coreapp/run.sh`
  (`edid_svd`, `edid_dtd`, `edid_interlaced`, `edid_stdtiming`, `edid_established`, `edid_native`, `edid_range`, `edid_tmds`, `edid_checksum`,
  `edid_family`, `edid_none_all`, `edid_table`). `tests/hostapp/test_videomode.cc` got block checksums; its control `vm_always1080` now breaks the family choice
  in `edid.cc`. RISC-V build (`u64-build-env:ci`, builder): hostapp and coreapp build, coreapp.app 1547664 of 1966068 bytes.
- **Not in WP15:** the menus, the V key and the hot-plug follow (WP16 / WP17 call `video_modes_offered` / `video_modes_best`); fixtures of the Sony and
  the Samsung (after the board capture).

### WP17 host app: the Picture row, the V key, the web page, the host default, hot-plug and the revert timer (G11-G13, S10-S12, Q6, Q7, Q9), built 2026-10-31
- **Code:** `hostapp/hostvideo.{h,cc}` (policy, pure, hooks for the screen and the gateware) and `hostvideo_board.cc` (the hooks: hot-plug line, DDC read, request
  register `0x4F00`, status `0x4F04..09`; the T163 store `[Video]` with `Mode` and `System`, id `0x48535401`). `hostui.cc`: F8 has one row `Picture` (from the screen
  + the offered resolutions; the system row is gone, Q6), the V key steps `hv_step()` over the offered rows, `picture_tick()` runs the hot-plug follow (about 500 ms,
  once per EDID digest, nothing while unplugged) and the Q7 timer (15 s, any key keeps and stores, no key goes back and stores nothing, V during the question steps on
  from the confirmed mode); the web page and `/v1/video` take only `hdmi` (-1 = from the screen), list only the offered resolutions and answer with `offered` in the JSON.
  `hostapp.cc` starts with `hv_start(false)`, the menu with the stored default (`hv_start(true)`); the pick is default if offered, else `video_modes_default` (best row, Q9
  for no or a corrupt EDID). boot.cfg `video=` is read once: `hv_load_default` moves it into host.cfg when host.cfg holds no mode and `try_load_cfg` writes `video=-1,-1`.
- **Gateware probe:** `0x4F04 == 0x48` and `0x4F05 >= 1` at the first use. Without it the host stays as before: `video_apply` (old path), no question, the choice is stored at once.
  With it the request is written, the host waits for processed == written (500 ms), reads the refused bit, then for running / pixel clock / transmitter and `mode == row` (2 s) and
  sets `U64_HDMI_ENABLE`. A refusal or silence leaves the mode and the state as they were.
- **Decisions taken in the code (to be confirmed):** (1) Q7 applies to the web change too (nobody at the screen: it goes back after 15 s; the answer says so; amended 2026-10-31: 8 s, see Q7). (2) The hot-plug
  follow is not a question (the old mode is not in the new list). (3) The family is the stored `System` (PAL unless boot.cfg's old line said NTSC); the other family is taken
  only when the screen names no row of it (G16), so a PAL host on a television that names 1080p50 and a PC row at 60 Hz does not list the PC row. (4) Stored but not
  offered keeps the default and shows it as `(not offered, <mode> up)`. (5) The `Protect Flash from updates` default stays off (coordinator, 2026-10-31: it waits for the
  board proof of the lock, T152 S2/S5); WP17 did not change `flashlock_board.cc`. The manual (8.5.1) still says the default is on.
- **Test:** `tests/hostapp/test_hostvideo.cc` + `run_hostvideo.sh` (S10-S12, Q7, the gateware path against a model of `mode_req.v`): 69 checks, 17 controls (mask ignored, family fixed,
  no follow, every poll, no cadence, hpd ignored, default ignored / unoffered, boot always, no revert, window, keep not stored, stored early, prev overwritten, probe ignored,
  refusal ignored, no timeout), all red as they must. `test_webui.cc` (offered rows only, no `sys` field, JSON `offered`; control `web_offers_all`), the render bench of F8
  (one `Picture` row, V over the Q9 list, web change and a refused row). Manual: 3.6, 4.1.3, 4.4.3, 8.1, 8.4, 8.9.
- **Not in WP17:** the board run (needs WP8 gateware), the core app side (WP16), the menu scale for the host at other output sizes (gateware, WP12).


### WP16 core application: gateware mode path, Picture / Scale / Smoothing, follow the screen, confirm (G12-G14, S10-S12, Q6, Q7), built 2026-10-31
- **Gateware detection and the mode path.** `app/video/videomode.{h,cc}`: `GwIo`-based `video_gw_probe_io` (magic `0x48` at `0x4F04`, version >= 1),
  `video_gw_up` (`pix_locked && running && mode == row`, refused only when `0x4F08 == 0x4F09`), `video_gw_apply_io` (writes `0x4F00`, polls every 10 ms,
  1 s timeout, a refused request ends at once); board wrappers `video_gateware_present()`, `video_gateware_row()`, `video_apply_gateware()` (now `bool`; the
  fixed 400 ms is gone), `video_edid_sig()` (hot-plug line + the two bytes 0x7E/0x7F of block 0). `VideoDesc::hdmi_modes` is retired: `Video::set_gateware()`
  is set from the probe, the A1200 descriptor lost its flag. A host whose window does not answer the magic keeps the firmware path (`video_apply`) and gets
  Picture alone, as before.
- **Items.** `picture_extend(d, out, store, env)` (`PictureEnv{mask, gateware, sys}`): Picture lists Auto and `video_modes_hdmi_list()` (new in `edid.{h,cc}`:
  the machine's family row when the mask names it, a PC mode by its 60 Hz row, the other family only when the machine's has no row at all; resolutions
  only, no Hz in any label). Scale (Integer / Fit / Fill) and Smoothing (Off / On, Q3) for every core with a frame buffer (`video.w != 0`) on a gateware
  host; `scale.apply` / `smooth.apply` are registered by `picture_register` (the A1200-only registration is gone). Smoothing is `PX_CTRL` b6 (WP5's layer),
  written with every layer set; the size is always `PX_OSIZE` on the gateware host (b5:4 only for an old host).
- **Order (D11c, G13).** `target()`: the core's own Picture if the list has it, else the host's default (the mode handed over) if the screen takes it, else
  `video_modes_best()`. Without the gateware Auto stays "what the picture fits in" (ADR-0057, the frame is 1080p there). The mode a gateware core starts from
  is the status row, not a guess (a large core's own copy starts at 1080p50; a DFX core's follows the host).
- **Follow the screen (G12).** `picture_service()` (called every pass of the loop in `run.cc`, menu open or not): every 500 ms `video_edid_sig()`; a new
  signature is read once (`video_read_edid`; a silent screen is asked again), the list rebuilt, `Settings::remap()` keeps stored choices by label (nothing
  written), and when the running row is not in the new mask `video_modes_best()` is applied once. Low hot-plug changes nothing; the same signature is never
  read or applied twice. `u64cfg.cc`: a System Mode change no longer calls `video_apply` when the gateware answers (WP7 moves the family).
- **Q7.** A mode changed from the Picture item with the menu open starts a 15 s wait (`picture_confirm_*`, `ui_message` countdown in `run.cc`): any key keeps
  it (and is spent on that), else the previous mode and the previous Picture value (written back to the `.cfg`) return. Not for REST / web changes with the
  menu closed (nobody could see the question), not for hot-plug, not for a mode that did not come up.
  **Superseded by the amendment of 2026-10-31 (§3a Q7), as built in `t164-q7`:** the wait is 8 s and every change asks, with the menu closed too
  (`picture_apply` no longer looks at `menu_open`; `run.cc` raises the layer with `ui_show()` for the question and puts it back with `ui_hide()` or the paused tag).
- **Tests (`tests/coreapp`).** `test_gwstatus.cc` (27 checks, 9 controls `gw_*`, last line `gwstatus: 27 checks, 0 failed`), `test_pictfollow.cc` (S10 lists on a television, a 60-Hz-only one, a PC
  monitor, none, a corrupt EDID; S11 hot-plug low / high, same EDID, silent screen, remap; S12 order; Q7; status at start; 98 checks, 17 controls `pf_*`),
  `test_scale.cc` rewritten for the gateware host (Scale / Smoothing on every frame-buffer core, none on an old host, `PX_CTRL` b6 written), golden
  `ops_trx64u.txt` got `scale.apply` and `smooth.apply`. Render bench: the A1200 settings page with its extended descriptor, and `nes-picture-page-pc.png`
  (Picture list of a PC monitor, Scale Fit, Smoothing On).
- **Open for the board / other packages.** WP17 builds the host side of D11c (`host.cfg` default, Picture row, V key);
  the A1200 mono gateware must answer `0x4F04` (S13, forks gone) or its core application falls back to the firmware path; the mask is read once at start, so
  a screen that is off at start gives the Q9 list until it is plugged in.
### WP14 T107 D9 SID out of c64ref, T120, rvlite fix (Q11), built 2026-10-31
- **T107 D9:** `cores/c64ref/trx_sid/` (engine, audio, tables) is deleted. `c64_top_c64ref.v` drives the stand-in where `c64_trx_sid_0` was: `sid_hit` low
  (the arbiter returns the open bus; the sockets still answer), no turbo fast path, `pot_dis` low, `sid_audio_sample_word_0/1` = 0, so `audio_l/r` of the
  partition carry silence (still through `c64ref_keep` and `sid_sock_audio_c64ref`). The partition ports (`sid_cfg_*`, `sid_cv_*`, `sid_mix_gains*`, `smp_*`)
  stay. `core_sources.tcl` no longer reads `trx_sid` or its `.mem` files and no longer defines `SID_MULTI` (CAPS3 b0 reads 0: no 8-SID engine in this core).
  `tools/contract_ports.py --check`: `contract: c64ref matches (247 ports)`. Verilator lint of `c64_core_c64ref` with the file list of `core_sources.tcl`
  (VHDL cores black-boxed): the only error is the black-boxed `proc_core`, nothing from the SID. `contract/host_pin.txt` re-pinned (`host_pin.sh --update`, then
  `--check`: unchanged, 323 files): the 10 `cores/c64ref/trx_sid` lines are gone, `c64_core_c64ref.v`, `c64_top_c64ref.v`, `core.toml` and `core_sources.tcl` changed. Other WPs' host files must be
  re-pinned again at the last merge. No Vivado here: the LUT / RAMB / DSP figures of §3c (-2,976 / -3 / -20) are from run 1104, not measured.
  c64ref has no bench of its own in `ci/sims.lane` (`tools/benches.sh c64ref` names no bench); the benches that read the SID are the trx64 ones and do not read this tree.
- **T120:** already done before this package: `b8519a15d` made the `ORIGIN.md` and `host/build.tcl` wording neutral (the three "must never be in a release" comments
  are gone; what is left is "Not part of a release build" at the vc20 / amiga blocks, which states a fact about the build). Nothing to change; the pin follows the host key.
- **Q11 rvlite fix:** patch 0007 (`tools/patches/gideon/0007-rvlite-mul-hold-only-for-a-multiply.patch`, commit `1cbfbf072`) and the bench `host/sim/rvtrap` are
  already on `main`, so on `t164`. `tools/vendor_gideon.sh` applies it cleanly to the vendored RISC-V (`execute.vhd` has the `reg_write = '1'` term in `mul_inst`).
  Bench `host/sim/rvtrap` on the builder with the freshly vendored RTL (seeds 1-3 x periods 300/900/2500 x waits 0/3): `PASS: rvtrap, 18 runs, 0 errors`; controls `MODE=ctl`: `ctl| all controls failed as required` / `PASS: rvtrap_ctl, every mutation failed the bench` (`mret_nop`, `trap_twice`, `csr_ena_high`). The CI bench cache marks `rvtrap`, `rvtrap_ctl`, `rvtrap_dc` as cached PASS (inputs unchanged by this WP).

### WP5 pixel layer with scaler and smoothing (G6, Q3), built 2026-10-31 (WIP hand-over)
- **Built.** `host/pix_layer.v` is the A1200 copy (nearest, `PX_OSIZE`, `ce`, `hact/vact/xact0/yact0`, format 2) with the host's still capture,
  flip, key, alpha and status unchanged; fork and `.diff` of the A1200 are gone (A1200 `top.v` uses `#(.SMOOTH(0))`, `build_mono.tcl` reads the host file;
  `check_wrapper_fork.sh` drops the pix_layer check). `host/top.v` and `cores/c128/mono/top.v` tie `ce=1`, 1920x1080 at 768/45 until WP8 connects the mode.
  Other monos (zxnext, neogeo, snes) use `host/top.v`. `contract/host_pin.txt` updated.
- **Smoothing** (parameter `SMOOTH`, default 1; `PX_CTRL` b6, b5:4 are not read any more): bilinear, weights from an exact fraction recurrence
  (`pix_wrec`, one serial division per axis per frame, no multiplier for weights), 6 DSP, ONE line-memory read port (two lines read word by word into
  two 128-bit holding registers, so no extra block RAM: RAMB36 stays 2). Per axis only while source <= output; vertical only if a line is <= 128 words
  (format 2: W <= 512); the colour key tests the nearest source pixel.
- **Bench** `host/sim/pixscale` (`run.sh`, `job.sh`, `model.py`, `mutate.py`, `tb_pixscale.v`): Python model; nearest exact, smoothing within 1 LSB.
  Verified (first full run, 14 runs incl. 480p/800x600/1024x768/1280x1024, format 1+2, SMOOTH 0 build): all pass; controls hshort, vstep, noce, wswap,
  vswap, vslot, tlate, smoothoff fail on a check; nolast and nobread failed with X pixels (model.py fixed to count those). Host `pixlayer` and `still60` benches pass.
- **Estimate** (Yosys xc7, not Vivado): SMOOTH=0 989 LUT / 1629 FF / 3 DSP (A1200 copy 983, Vivado 801); SMOOTH=1 1630 LUT / 2276 FF / 9 DSP;
  Vivado-calibrated delta about +520..+590 LUT, +650 FF, +6 DSP, 0 RAMB36.
- **Open / next step.** (1) The final rerun after the fix of `model.py` and the unique builder dir (`/var/tmp/t164wp5-alexmine`, started 20:2x, result in
  `ps/*.rc`; command `OUT=/src/ps JOBS=12 sh host/sim/pixscale/run.sh`) was not finished: confirm 14 pass + 10 controls fail, put the last lines here; earlier
  results came from a builder dir another agent also wrote to. (2) Set the `pixscale` timeout in `ci/sims.txt` to about 7200 and fix its comment. (3) Apps still
  write `PX_CTRL` scale in b5:4 (`coreapp/src/video.cc`, host-app lvgl patch, `app/video/videomode.cc`): they must write `PX_OSIZE` for every core before this host ships.
  (4) Other mono tops (c128 copy) need the real mode signals from WP8.

### WP8 hdmi_out integration (G1, G5, S8, S6), built 2026-10-31 (branch t164-wp8)
- **Built.** `host/hdmi_out.v` holds `mode_req` (its read mux over the overlay's rdata on `rhit`), `cdce_mode` (`rst = cdce_rst | vm_rst`, `lock = c64_type`),
  `mmcm_pix` (the A1200's x14 / 7 from usrclk: the TMDS clock of every row), `hdmi_timing`, `vic_scaler` on its counters, `overlay`, `pix_layer`, `c64_text`
  (all on `ce`, the mode's `hact / vact / xact0 / yact0`), the encoder fork with `ce = pce | pix_rst` (so its reset reaches the registers while the timing is
  held), `tmds_rep` and `hdmi_gtp` (lanes: channel 2, 1, ~channel 0, clock lane), the audio crossing into pix_clk, the measure register and the dfx relock.
  `host/top.v` instantiates it and loses the HDMI block, `cdce_i2c`, the `sym_pair` stage; `host/build.tcl` and the `build_mono.tcl` of snes / neogeo /
  zxnext read the new files (`hdmi_out, hdmi_timing, mode_req, cdce_mode, tmds_rep, trx_gain, portlock_clk, esp_ctl`, the forked encoder in `host/hdmi_enc`
  plus the three unforked vendor files). c128 and amiga1200 keep their forks (WP18). `host/cdce_i2c.v` stays in the tree, unused.
- **`vic_scaler` EXT.** Parameter `EXT = 1` (hdmi_out): no counters of its own; `t_de / t_hs / t_vs / t_cx / t_cy / xact0 / yact0` from `hdmi_timing`, `en` = the
  row is 1080p (`hm_locked`), output `align` (once per lock, cleared by rst / relock / !en, `frame_seen && !locked && en`) to the timing. The VIC line of the
  window follows `t_cy` (three hold clocks after an align, because `t_cy` jumps). `EXT = 0` (default) is the old module for the large cores' tops until WP18
  (their benches `scaler`, `hdmigeom`, `zx_*` are unchanged). On every other row the timing's black picture goes into the overlay: the scaler's three clocks
  of delay would shift the pixels against `ce` of a repeated row.
- **`c64_type`** is a port of `hdmi_out`; the DFX host ties it to 0 (no DFX core ships on the VIC path, Q4, and the host cannot know the class of the core
  in the partition). A large C64-type core's top must set 1 (WP18 for c128). With 0 a C64 core in the partition is not held to its System Mode family.
- **Bench `host/sim/vicscale`** (S8, `ci/sims.txt` tier 2): hdmi_timing + vic_scaler (EXT) against a PAL (63 x 312) and an NTSC (65 x 263) raster model whose dot
  clock and the TMDS clock are in the exact ratio of the locked rows (6875 / 1456, 61875 / 13676, time unit 1 fs). The model writes `{marker, frame[2:0]}`.
  Checks per output frame: one VIC frame in the picture (all window pixels carry the tag of the lock frame), the tag rises by one per output frame, the
  window (1600 of 1920, centred, markers at its edge pixels and first / last line), the output frame period (clocks), one align pulse, the lock never falls,
  and the timing position at every VIC frame event is the same (slip count). 60 Hz: raster lines 256..262 are overwritten by lines 0..6 of the next frame in
  the 64-line ring (263 is not a multiple of 64) before the window reads them; the bench expects that as it is (border lines). Controls: `slip` (TMDS clock
  1 part in 10,000 slow, row 2) and `slip60` fail with "slipped against the VIC frame", `noalign` (the timing ignores align) fails on a colour index.
  Result with 6 frames per row: `PASS vicscale (6 frames of 1080p50 PAL and 1080p60 NTSC locked, no slip; 3 controls failed as they must)`.
  **Deviation to decide with Alex:** S8 names 1,000 frames. In iverilog a frame of 2.97 M clocks takes about 45 s, 1,000 frames about 12 h per row, so `run.sh`
  defaults to 100 frames (`FRAMES=1000` runs the full scenario; the position check is exact, the 1e-4 control is caught in the first frames).
- **Bench `host/sim/hdmi_out`** (`ci/sims.txt` tier 2): `hdmi_out` as a whole in 1080p50 (VIC pattern through the palette, 1600 x 1080) and 576p50 (gear 5,
  a 640 x 480 RGB565 picture from a DDR2 model through `pix_layer`, after the request 0x4F00 = 0 through the overlay window). The Xilinx primitives and
  `hdmi_gtp` are models (`models.v`); the bench serialises the four lane words, finds pixels by the clock lane, takes the middle bit of each `gear` group
  (lane 2 inverted) and decodes control periods, guards, video, TERC4 islands (WP4's parser: BCH, AVI PR / VIC / aspect, ACR against the pixel clock, audio
  samples in order, one island per line). Status registers 0x4F04..07 are read back. Controls (mutated copies of `hdmi_out.v`): `vicsel`, `novic`, `noaspect`,
  `nolane`, `fwlong`, `rdmux` (1080p) and `ceenc`, `gear1`, `nolane`, `novic`, `fwlong` (576p).
  **Result (WP8b, 2026-10-31/11-01, builder, iverilog, 6 parallel runs, ~1.5 h each):**
  `hdmi_out mode 2 PASS` (as above). `hdmi_out mode 0 PASS`: `mode 0: vic 17 aspect 1 gear 5 rate 27067117  video pixels 414720 islands 625 packets/frame acr 21
  audio 240 avi 1 spd 1 ainfo 1` (the 640 x 480 picture, pixel for pixel). Two bench faults found and fixed first (not RTL): (1) the first 576p run showed black for the
  whole picture because the bench wrote the pixel layer registers before the pixel clock ran; `pix_layer` commits through a toggle (`cfg_tgl`) that an even number of
  writes cancels while `clk` is stopped, so the layer stayed at its defaults (`f_en 0`); the bench now writes after the mode change (an app writes with the clock
  running; a write burst with the pixel clock stopped is lost: noted, not an RTL fault of this WP). (2) ACR expected from the sample count (957) gave 19 against 21
  packets; now from the sample packets sent (240 x 4 / 48 = 20, +-1).
  Controls that failed as they must: novic (AVI VIC), noaspect (AVI picture aspect), nolane (video pixel), rdmux (status magic) in 1080p; ceenc, nolane, novic in 576p.
  **`fwm1` / `fwnox` are equivalent mutants, not controls (WP8c, 2026-11-01).** Both run to `mode 0 PASS`: with `fw_m1` short by the x offset, `video_field_end`
  (`cx == fw_m1 && cy == ey_e - 1`) fires 144 pixels early in the last active line, after that line's island and before the next one; it only sets
  `data_island_sync` and clears the InfoFrame-sent flags (`packet_picker`), nothing a decoded pixel or packet depends on, and `cxn` (the look-ahead column) is not
  read. Dropped. Replaced by `fwlong` (`e_fwm1 = m_xact0 + m_hact`, one too long): `cx` never equals it, no field end, `data_island_sync` is never set, no island is
  sent; it must fail with "data islands in the frame" (both modes; `run.sh` `expect fwlong`). Total 11 controls as before.
  **vicscale 100 frames:** the WP8 run stopped at the bench's own 1e15 fs watchdog (48 / 57 of 100 frames, FAIL timeout, not a lock failure); the watchdog now scales
  with FRAMES (25 ms a frame). Controls at 40 frames: slip, slip60 ("slipped against the VIC frame", 40 of 41), noalign ("colour index 11, want 10") failed as they must.
  S8's 1,000 frames is decided as a board check (frame-lock counter), not a sim.
- **pixscale (WP5 hand-over).** Rerun in a private builder dir: `PASS pixscale (10 runs: nearest exact, smoothing within 1 LSB, the build without smoothing;
  controls failed)`; all 10 controls fail (hshort, vstep, noce, wswap, vswap, vslot, nolast, nobread, tlate, smoothoff). `ci/sims.txt` timeout 7200, comment fixed
  (4 modes, not 7).
- **Estimate** (Yosys `synth_xilinx -flatten`, `hdmi_out` alone with the GTP black-boxed, SMOOTH = 1, not Vivado; ABC reported an error but the statistics
  were printed): about 3,900 LUT (+ 26 RAM32M / RAM64M, 261 SRL), 4,350 FF, 15 DSP48E1, 9 RAMB36 + 1 RAMB18, 451 CARRY4. Yosys overshoots Vivado
  (WP5: 1,630 vs 801-based delta); the Vivado number is WP19.
- **Open.** Result lines of the two long benches (above); `tools/benches.sh host` once before the merge into main (Alex, 2026-10-31); `c64_type` for c128 in WP18;
  Vivado WP19. Apps still write `PX_CTRL` b5:4 (WP5 open point 3, WP17).

### WP19 floorplan for the first Vivado run (part 1: numbers and cut A), prepared 2026-10-31 (branch t164-wp19)
- **Numbers read** (last host in the builder cache, `host-39bdd119` = commit 66b65d2e; root-only, read through a `docker run -v`):
  `host.utilization_pb_l1_opt.rpt`: **11,002 / 14,200 LUT (77.48 %)**, 14,973 / 28,400 FF, **24.5 / 25 block RAM tiles**, 9 / 60 DSP;
  `host.utilization_pb_l2.rpt` (c64ref): 12,138 / 16,800 LUT, 32 / 50 tiles, 21 / 60 DSP; WNS +0.147. Cores: the newest `<core>.utilization_pb_l2.rpt` of each;
  per-module split from `host.utilization_hier.rpt`.
- **Estimate of the new `pb_l1`** (Vivado-measured where a report exists, else the §3c estimate or yosys):

| item | LUT | basis |
|---|---|---|
| old host | 11,002 | `host.utilization_pb_l1_opt.rpt` |
| HDMI + scaler subtotal (§3c +828) with `tmds_rep` at 223 instead of 140 | +908 | §3c, WP3 yosys |
| smoothing (`SMOOTH`=1; WP5 calibrated +520..590) | +555 | WP5 |
| flash lock, volume, lamp, port clock, ESP pins (45 + 40 + 83 + 20 + 60) | +248 | §3c |
| **new `pb_l1` need** | **12,713** (range 12,400-13,300) | 89.5 % of the old block |
| cross-check: `hdmi_out` yosys 3,900 x 0.81 (WP5 ratio) = 3,160, less the 2,017 of the modules it replaces (`hdmi` 622, `cdce_i2c` 70, `pix_layer` 562, `overlay` 398, `vic_scaler` 166, `c64_text` 199), plus 248 | 12,393 | WP8 yosys, run hier |

  FF about 16,200-16,500 (capacity 28,400 -> 29,600 after the move). **Block RAM: no change** (`hdmi_out` 9 RAMB36 + 1 RAMB18 = scaler 4 + overlay 3 + pix_layer 2 and the overlay's RAMB18),
  `pb_l1` stays at 24.5 / 25: any new RAM in the host fails the placement. DSP 17-23 of 60 (9 old, +6 smoothing, +2 volume; yosys `hdmi_out` 15 would give 23).
- **Cut A built** (`host/build.tcl`, floorplan B; geometry from the prjxray tile grid: BRAM columns at 7|8, 47|48, 57|58, DSP columns at 11|12 and 53|54):

| | old | new |
|---|---|---|
| `pb_l2` slices, rows 0-49 | `X0Y0:X55Y49` | `X0Y0:X49Y49` (X48, X49 clear of BRAM column 1 at 47|48, as rows 50-99) |
| `pb_l1` slices, rows 0-49 | `X56Y0:X65Y49` | `X50Y0:X65Y49` |
| `pb_l2` DSP | `DSP48_X0Y0:Y39 DSP48_X1Y0:Y19` (60) | `DSP48_X0Y0:Y39` (40) |
| `pb_l1` DSP | `DSP48_X1Y20:Y59 DSP48_X0Y40:Y59` | `DSP48_X1Y0:Y59 DSP48_X0Y40:Y59` |
| `pb_l1` LUT / `pb_l2` LUT | 14,200 / 16,800 | 15,400 / 15,600 (+-1,200 = 6 columns x 200; the model reproduces `pb_l1`'s 14,200 exactly) |
| `pb_l1` density at 12,713 | 89.5 % | **82.6 %** (12,400: 80.5 %; 13,300: 86.4 %) |

  BRAM ranges, `pb_cpu` (`X34Y100:X59Y149`, DSP X1Y40:Y59) and the upper rows are untouched; `contract/host_pin.txt` updated. **`pb_l1` at 82.6 % is above the 77.6 % that routed
  (79.9 % did not, T133)**: cut A does not reach the 75 % of the brief; that is the decision of Alex above, and the run says whether it routes. The levers left are the
  JTAG master (-734 LUT, to 77.8 %) and `SMOOTH=0` (-555, to 78.9 %).
- **`pb_l2` after the move** (15,600 LUT, 31,200 FF, 50 tiles, **40 DSP**; core numbers from the newest `<core>.utilization_pb_l2.rpt`; densest `pb_l2` that routed 85.1 %, ~80 % the guide):

| core | LUT | % of 15,600 (was of 16,800) | margin | FF % | tiles | DSP | verdict |
|---|---|---|---|---|---|---|---|
| amiga (A500) | 14,298 | 91.7 (85.1) | 1,302 | 37 | 18 | 8 | **does not fit** (above the 85.1 % that routed) |
| nes | 13,181 | 84.5 (78.5) | 2,419 | 19 | 11 | 0 | at the edge (just under 85.1 %) |
| c64ref, SID out (WP14) | 8,854 | 56.8 (52.7) | 6,746 | 33 | 29 | 1 | fits (run 1104: 11,830 = 75.8 %) |
| atari800 | 7,110 | 45.6 | 8,490 | 21 | 29.5 | 2 | fits |
| cpc | 7,032 | 45.1 | 8,568 | 15 | 46.5 | 0 | fits (46.5 / 50 tiles, unchanged) |
| vc20 | 5,479 | 35.1 | 10,121 | 11 | 18.5 | 0 | fits |
| zx | 5,388 | 34.5 | 10,212 | 11 | 42 | 0 | fits |
| c264 | 5,272 | 33.8 | 10,328 | 11 | 43 | 0 | fits |
| apple2 | 5,216 | 33.4 | 10,384 | 17 | 36.5 | 0 | fits |
| ddt_mandel | 4,912 | 31.5 | 10,688 | 17 | 26.5 | **48 of 40** | **does not fit (DSP)** |
| ddt_novic | 3,995 | 25.6 | 11,605 | 3 | 2 | 11 | fits |
| null | 2,107 | 13.5 | 13,493 | 0 | 0 | 0 | fits |

  Flat builds (`cores/<id>/mono/`, c64u and trx64 source trees) are not in `pb_l2`. Reports older than the newest host (amiga1200, c64u, trx64: capacities 18,000 / 36,000) were not used.
  No core was converted to large. The reported `pb_l2` capacity (16,800) is 1,600 LUT below the nominal rectangle count (18,400, same offset in the 18,000 of the older floorplan; unexplained):
  the CI line shows the real figure.
- **What the CI run must show:** `=== T79 pb_l1 | Slice LUTs` (expect ~12.7 k of 15,400) and `DSPs` (17-23 of 60); `Block RAM Tile` 24.5 / 25; `=== T79 pb_l2 | Slice LUTs` available 15,600,
  `DSPs` available 40, `Block RAM Tile` 50 (lower = the BRAM column fell out, T133); host and c64ref/null WNS; `Number of Nodes with overlaps` in the router (climbing past ~2,000 = no route, stop it);
  `[Place 30-410]` clock-region messages for X1Y0; then per core `<core>.utilization_pb_l2.rpt` and WNS for `nes`, `amiga`, `atari800`, `cpc`.
- **Open.** Vivado (part 2, the coordinator's push); `amiga` and `ddt_mandel` become large cores (or stay out) as the run shows, not decided here.
### WP19 result: host + c64ref route, run 1122 (2982e0327), 2026-11-01

- Run 1117 (cut A, `pb_l1` 15,400): placement failed, 12,802 LUT = 83.1 %, `[Place 30-487]` 10-36 slices short.
- Run 1119 (X0-5 island to the host, 17,800): WNS -6.155 ns. Cause: `host/pins.xdc` looked for `gtp/*gtpe2_i/TXOUTCLK`, the GTP is `hout/gtp`
  since WP8; `set_clock_groups` dropped (`[Vivado 12-4739]`) and every asynchronous crossing was timed. Fixed in 7a292c00e.
- Run 1121 (98af3636c: `pb_l2` one rectangle X0-49 rows 0-99, the host an L with the top-left strip; clock groups fixed): host WNS -0.149 ns,
  snes -0.168, zxnext -0.299, all `hout/mtim/hact` -> `hdmi_enc` video_preamble/guard, 11-12 levels at 150 MHz; neogeo synthesis failed on its
  overlay stub (WP6 ports). Fixed in 2982e0327 (window edges and line test registered, windows taken on cx; stub ports); `hdmi_enc` bench
  `PASS: 9 rows decoded, 4 controls failed as they must`.
- **Run 1122:** `=== T79 pb_l1 | Slice LUTs* | 12802 | ... | 17400 | 73.57 |`, block RAM 24.5 of 35, DSP 20 of 80;
  `Post Routing Timing Summary | WNS=0.138 | TNS=0.000 | WHS=0.050`; `bit-host done in 12m27s`. `pb_l2`: 13,600 LUT, 40 tiles, 40 DSP
  (c64ref 9,212 LUT = 67.7 %, 27 tiles). Every DFX core and every large core green except `neogeo`. Large cores (WNS): amiga 0.028 (after
  ExtraTimingOpt), amiga1200 0.133, c128 0.237, c264 0.181, cpc 0.172, nes 0.202, snes 0.068, zx 0.104, zxnext 0.107.
- **Open: neogeo** flat build does not place (`[Place 46-13]` more than 5 % unplaced; block RAM 72 of 75, the host package's LUT on top of
  its 82 %). Needs a decision with Alex (what leaves its flat build). Without JTAG debug the shared XDC's `jtag_tck` group would be empty and
  drop all clock groups again; that must be fixed first if that is the way.

### WP19 part 2: `nes` and `amiga` become large cores, written 2026-11-01 (branch t164-wp19b)
- **Why** (Alex, 2026-11-01, Q1 "Second cut" and "Overnight order"): pb_l2 is 13,200 LUT (floorplan B); `nes` (13,181 LUT) and `amiga` (14,298) no longer fit and are built flat, as `zxnext` was
  (commit c123e0bc9, T133).
- **nes** (first commit): `cores/nes/core.toml` moved to `cores/nes/mono/core.toml` (`delivery = "cfw"`, `mono_env = ["TRX_FEATURES=jtag"]`), `cores/nes/core_sources.tcl` (the DFX recipe) removed, new
  `cores/nes/mono/build_mono.tcl` (the flow of `cores/zxnext/mono/build_mono.tcl`: `host/top.v` and `layer2_nes` renamed to `layer2` in one design; the machine's SystemVerilog and T65 read as `core_sources.tcl` did;
  `nes_timing.tcl` sourced after `synth_design`; the host's SD, no T149 IOB swap, the NES has no SD) and `mono/build.sh`. `host/layers.txt` ignores `cores/nes` whole; benches `nes*` owned by `large:nes`;
  lane block rewritten by `ci/lanes.py write` (job `large-nes`, no `bit-nes`). No module of the NES's gen/ or trx/ has the name of a host module or a Gideon entity (checked by name).
- **amiga** (second commit): same steps. `cores/amiga/core.toml` moved to `cores/amiga/mono/core.toml`, new `mono/build_mono.tcl` (file list of `cores/amiga/core_sources.tcl`: Minimig-AGA, NanoMig, `trx/`,
  fx68k, `INFER_DPRAM`; microcode files copied to `host/`; the Amiga's `trx_afifo` read as `amiga_trx_afifo` against the host's, as in the A1200 flat build; host `top.v`, the A500's generics unchanged) and `mono/build.sh`;
  lane `large-amiga`; `host/layers.txt` ignores `cores/amiga` whole. `cores/amiga/core_sources.tcl` stays: `host/build.tcl` (pinned) still sources it for `TRX_CORE=amiga`, so its file list exists twice. `ci/test_lanes.sh`
  controls moved to `bit-atari800` and `large:amiga`. The `amiga*` benches that run the A500 are `large:amiga`; `amiga_cfg`, `amiga_cpu020`, ... stay with `amiga1200`.
- **cpc**: same steps (T164 Q1 cut 3: pb_l2 has 40 block RAM tiles, the CPC needs 46.5). `cores/cpc/core.toml` moved to `cores/cpc/mono/core.toml` (delivery cfw, `mono_env` jtag), `core_sources.tcl` removed (nothing pinned reads it), new `mono/build_mono.tcl` (the file list of the old recipe: Gate Array, CRTC, PPI, MMU, `gen/u765.sv`, the Spectrum's Z80 and AY, `zx/shared`, `trx/`; `layer2_cpc` renamed to `layer2`) and `mono/build.sh`; lane `large-cpc`; `host/layers.txt` ignores `cores/cpc`; the six `cpc_*` benches are `large:cpc`. `cores/zx` stays a DFX core for now (its sources fire both lanes); module names of the CPC's files checked against the host and Gideon's tree: no clash. `rtl/NOTICE` names the new recipe.
- **zx**: same steps (pb_l2 has 40 block RAM tiles, the Spectrum needs 42). `cores/zx/core.toml` moved to `cores/zx/mono/core.toml`, `core_sources.tcl` removed, new `mono/build_mono.tcl` (ULA, AY, `gen/t80pa.v`, `shared/`, `trx/`; `layer2_zx` renamed to `layer2`) and `mono/build.sh`; lane `large-zx`; `host/layers.txt` ignores `cores/zx`; the eight `zx_*` benches are `large:zx`. No module of its files has the name of a host module or a Gideon entity (checked by name). `rtl/NOTICE` names the new recipe.
- **c264**: same steps (pb_l2 has 40 block RAM tiles, the C264 needs 43). `cores/c264/core.toml` moved to `cores/c264/mono/core.toml`, new `mono/build_mono.tcl` (T65, TED, colour table, the `trx/` files; the host list already holds `cores/common/iec_bus.v`; `layer2_c264` renamed to `layer2`) and `mono/build.sh`; lane `large-c264`; `host/layers.txt` ignores `cores/c264` whole. `cores/c264/core_sources.tcl` stays because pinned `host/build.tcl` sources it for `TRX_CORE=c264` (header says so; its file list exists twice, as the Amiga's). `ci/test_plan.sh`: the two scenarios that used c264 as the second DFX core use atari800. The `c264_*` benches are `large:c264`. No module clashes with the host or Gideon's entities (checked by name; T65 is not in Gideon's tree).
- **Open for Alex:** `cores/nes/rtl/NOTICE` (and zxnext's) says the host and the other cores are separate works in separate partial bitstreams; a flat build links host and core in one bitstream. Not changed here.
- **Not run:** no Vivado here. The flat build has to show host + NES in one design: LUT, BRAM (the host's 24.5 of 25 tiles plus the NES's 11), WNS.
### Q7 amendment, 8 s and every change asks (WP16 / WP17 app fix), built 2026-10-31 (branch t164-q7)
- **Host app:** `HV_CONFIRM_MS` 8000 (host menu, V key, web page already asked through `hv_user_set`); notes and the web page text say 8 s.
- **Core app:** `PICTURE_CONFIRM_MS` 8000; `picture_apply` asks for every changed mode, not only with the menu open. With the menu closed `run.cc`
  raises the overlay layer (`ui_show()`, the layer the pause tag uses) for the "Keep this picture?" message and takes it away at the end (`ui_hide()`,
  or the paused tag when the machine stands).
- **Manual:** 3 (first start), 4.4 (boot menu) and 8.4.8 say 8 s.
- **Test:** `run_hostvideo.sh` control `q7_window` now sets 15 s and fails; `test_pictfollow.cc` asks with the menu closed, 8 s, and the controls
  `pf_menu` (ask only with the menu open) and `pf_15s` (15 s) fail. Not shown: the closed-menu layer in `run.cc` is board code (no bench draws it); Alex looks at it.

### Q7 prompt not on the screen after a picture change (host app), found and fixed 2026-11-02 (branch t164-picnote)
- **Report (Alex, board test of package t164-c83a83c2):** after V in the File Browser or the Picture row in F8 the line "Picture: <mode>. Press any key to keep it (N s),
  else it goes back." is not visible, at no resolution; the mode itself changes.
- **Cause:** `canvas_apply()` in `hostui.cc` (T164 D10a, commit `5ed22c966`, 2026-11-01). A picture change that changes the menu canvas (960 x 540 to the
  mode's own size below 960 wide, or back) builds every widget again (`lv_obj_clean(scr)`, `build_ui()`, `build_start()`), and `build_ui()` creates the container
  of each view hidden (`view_box()`). The start-up path shows the view afterwards (`draw()` -> `show_view()`); `canvas_apply()` only set `need_full`, which refills
  the labels (`refresh_current()`) but never shows the container. The page in view, and with it its message line, stayed hidden until a key changed the view
  (`go()`). The line was set and drawn into a hidden page. The bench could not see it: `hostui_test_mode()` called `show_view()` itself after `menu_place()`, and the
  V-key check read the line's text out of `note`, which was right all along. Earlier packages had no canvas change in the V path, so the line was visible.
- **Fix:** `canvas_apply()` ends with `show_view(view)`; `hostui_test_mode()` no longer shows the view for the product (it only refills it), so every bench that
  changes the mode now goes through the product path. `picture_tick()` kept its "second shown" in a function-local static that outlived the question; it is a file
  static (`pic_shown`) that `picture_set()` sets, so a second question whose count equals the last one of the first is not skipped.
- **Test (`tests/hostapp/render/render.cc`, `picnote()`):** a model of the mode-request gateware (`g_gw_model`: the host answers 0x4F04..09, so the change waits for
  its key) and a fake EDID. For the File Browser (V) and Settings (Picture row), at 1080p (960 x 540 canvas) and at 800 x 600 (canvas 800 x 600, a canvas change
  from 960 x 540): the line must say "Picture: ... Press any key to keep it (8 s), else it goes back.", its rectangle in the frame (`hostui_test_note_rect()`,
  hidden containers count) must hold more than 150 pixels that are not the rectangle's background, the count must run ("(5 s)" after 3 s), and a key must give
  "Picture kept" and store the mode. Controls in `run.sh`: `picblank` (no `show_view` in `canvas_apply`), `picnobnote` and `picnosnote` (the File Browser's / the
  Settings page's line never set on the label); each must go red on a `PICNOTE` check.
- **Not covered:** the board's pixel layer (`trx_lv_set_place`, PX_OSIZE) is stubbed in the bench; Alex looks at it in the board test (V in the File Browser from a
  compact mode and back).

### The host's picture is not remembered over a reboot (host app), found and fixed 2026-11-03 (branch t164-picsave, GitHub #24)
- **Report (tester on t164-c83a83c2, Sony screen with 1080p and 720p; Alex: "that's a bug"):** on every host boot V has to be pressed until 720p is up; the cores keep
  their own mode, only the host menu forgets. Alex: if V does not adjust the resolution in the settings, it should.
- **Cause:** `hostui.cc` `try_load_cfg()` was the only caller of `hv_load_default()` (the read of host.cfg `[Video]` into `st.def_hdmi`), and the start used the stored
  choice only as `hv_start(cfg_loaded, ...)`, `cfg_loaded` meaning "/Flash/boot.cfg was read". The store is host.cfg's since T163/WP17, boot.cfg has nothing to do with it,
  but a board where /Flash/boot.cfg cannot be read at the start (none yet: V and the Picture row never create one, only a menu edit does; or the flash disk not up
  yet) never read the stored choice: `def_hdmi` stayed -1, the pick was the best row of the screen (1080p), and `hv_keep()` wrote 720p into host.cfg on every
  V + key all the same, to be ignored on the next boot. The write side is right (the same label round trip `Mode=1280 x 720` is benched with the real ConfigManager,
  `test_hostcfg.cc`); the other tester's board log shows the read side working because its boot.cfg loads. Reading from code and benches only: not seen on the
  tester's board (no log from it); the symptom matches (the settings of other stores and the cores' own modes survive).
- **Fix:** `hv_start(use_saved=true)` reads the stored default itself when `hv_load_default()` has not run in this boot (`HvState.loaded`); `hostui_run` starts with
  `hv_start(true, ...)` instead of `hv_start(cfg_loaded, ...)`. `hv_load_default()` still moves a boot.cfg `video=` over when host.cfg holds none (`try_load_cfg`).
  The F8 Picture row, V and the web page all end in `hv_user_set()` -> `hv_keep()` -> host.cfg, so one fix covers them; the F8 row shows `def_hdmi` and now agrees with
  what V kept. hostapp.cc's first start stays `hv_start(false)` (the screen's best row before anything is read).
- **Test:** `test_hostvideo.cc` `picsave()`: power-on model (`hv_start(false)`, then `hv_start(true)` with no `hv_load_default`), V + key then reboot comes up 720p;
  V twice (576p, 720p) + key then two reboots 720p and nothing stored by the start; no key goes back, nothing stored, reboot 1080p. Controls in `run_hostvideo.sh`:
  `picsave_no_lazy_load` (the bug put back: 3 checks red), `picsave_keep_not_stored` (6 red). `test_hostcfg.cc`: all seven modes x both systems survive a reboot.
- **Not covered:** the board (Alex: V to 720p + a key, power-cycle, the host menu comes up 720p, with and without /Flash/boot.cfg).

### Console flat builds: slim host part (Q1 "Console flat builds"), built 2026-11-01
- **Coordinator correction (2026-11-01):** the WiFi UART stays. Alex: "everything of the UART we do NOT need"; the ESP32 UART carries the Menu / Freeze button events, so `uart_dma`, `esp_ctl` and the core application's `hw_rocker_init` are as on `t164` (no `NO_LINK`, no `board_has_esp_link`). The Q1 paragraph above lists the WiFi UART as out; this section is what is built.
- **Defines (default off, the DFX host and every other build keep the same logic):** `NO_NET` (Ethernet only) and `NO_C64VIDEO`. `mono_env` of `cores/{nes,snes,neogeo}/mono/core.toml` gets `TRX_DEFS=NO_NET NO_C64VIDEO`.
- **`NO_NET` (`host/top.v`, `host/ultimate_sys.vhd`):** `ultimate_sys` has a generic `g_net` (integer, default 1; `top.v` passes 0 under the define). With 0 `ethernet_interface` is not instantiated (`g_eth` generate); region `0x060800` still acks with 0, DMA port idle, interrupts 0.
  `top.v`: no `rmii_transceiver`, no MAC / packet-port arbiter, no `pktport_host` (the core's packet port gets no grant, `pp_ready` 1); `RMII_TX_EN` / `RMII_TX_DATA` driven 0. The pins stay: `RMII_REFCLK` is the root of `rmii_clock` and of the host's PLL, so the clock group in `pins.xdc` keeps its object; RX, MDIO / MDC pins, ETH LED and debug counters stay.
  `HOST_CAPS` loses bit 24 (`CAPAB_ETH_RMII`): `rmii_interface.cc` starts no driver and registers no interface, `netops.cc` answers "No interface". The application needs no change.
- **`NO_C64VIDEO` (`host/hdmi_out.v`):** no `vic_scaler` (`vic_sel` 0, `align` 0, `sc_locked` 1, `x_text` 0) and no `c64_text` (pixel layer to the encoder, text port reads 0). Overlay, pixel layer, palette RAM and encoder stay.
- **Checks (2026-11-01):** yosys read/hierarchy/proc/check of the host's Verilog with the defines off / on: no new undriven or multiply driven wire, defines off give the cell statistics of `t164`. GHDL elaborates `ultimate_sys` with default and `-gg_net=0`. Vivado not run.
- **All builds:** `ethernet_interface` sits one generate level deeper (`ult/g_eth.i_eth`); no XDC or Tcl names it. `contract/host_pin.txt` is updated for `top.v`, `ultimate_sys.vhd`, `hdmi_out.v`.

### Large cores take copies of the host files (Q1 "Large cores take copies, not the shared host include"), built 2026-11-01
- **Why (Alex, 2026-11-01):** run 1126 rebuilt every large core for a host-only change and cpc / nes / snes lost timing. "If that now needs a new core every time, then no include!" / "Copies instead."
- **What is copied:** for each of the ten large cores (amiga, amiga1200, c128, c264, cpc, neogeo, nes, snes, zx, zxnext) the host files its `build_mono.tcl` read, taken from `host/` at `aa45528c2` (t164, hdmi_timing fix in) into `cores/<id>/mono/host/`:
  `top.v` and the Verilog / VHDL of the host, `pins.xdc`, `ddr2_pins.xdc`, `gtp_pins.xdc`, `usb_pins.xdc`, `hdmi_gtp.v`, `hdmi_enc/` (six forked files and its licences), `hdmi_modes.vh` (the cores that include it), `mem/ovl_font*.mem`, `gideon_vhdl.tcl`
  (61 files in the full set; amiga1200 54 since WP18 (the A500's set), c128 44: it has its own top / timing / overlay forks in `mono/` and reads fewer host files). `mono/host/FROM` names the commit, the edits and the files. Edits to the copies: `gideon_vhdl.tcl` carries `$hd/` on its local VHDL names, `overlay.v` reads its fonts from the copy's `mem/`.
- **Not copied (decided):** `vendor/icepi`, `vendor/gideon`, `cores/common`, `cores/trx64/trx_*` and `drv_dbg.v`, `tools/vivado_lib.tcl` stay shared: they are no part of `host/`, and the key still covers them. c128's own `ultimate_sys.vhd` / cache package replace the host's, so the host's `ultimate_sys.vhd` is not copied for c128. amiga1200 has its own `hdmi_modes.vh` (mono/), the host's is deliberately absent (so the include finds the A1200's).
- **Build:** `build_mono.tcl` sets `hd = ../cores/<id>/mono/host` and prefixes every host file with it, `include_dirs` gets `$hd` where it had `.`, `lsearch` keys for stubs / replaced files carry `$hd/`. Vivado's working directory stays `host/` (output directory, `../vendor`, `../tools`, `../cores`); no file is read from it (checked below).
  `cores/amiga1200/sd/sd_mono.tcl` (amiga1200 and zxnext) reads `$hd/ultimate_sys.vhd`; A1200's `check_wrapper_fork.sh` no longer has a `bling_io` fork to check (WP18); the fonts of `overlay.v` are read from the copy's `mem/`.
- **Key and plan:** `ci/lib.sh mono_key` = the descriptor's sources (the copy included) + `shared:` without its `host/` files + the variant; no `host:` section and no reference core. `ci/plan.py`: a large core is planned by its sources and the shared files, no longer by `host`; `ci/targets.txt` loses `large:*` from `expand host`. amiga1200's sources leave out `cores/amiga/mono` (the A500's copy).
- **Lint:** `tools/check_large_copies.py` (selftest with four faults): `FROM` exists and lists the folder, `build_mono.tcl` sets `hd` and names no live `host/` file by its bare name, the descriptor's sources cover the copy. The copies are not compared with `host/` (they may differ, the point of the decision); `tools/host_pin.sh` stays about `host/`.
- **Checks (2026-11-01):** a Tcl harness (stubbed Vivado commands, empty `host/` as the working directory) runs each `build_mono.tcl` and records every file `read_verilog` / `read_vhdl` / `read_xdc` / `-include_dirs` get: every path exists, none is bare, and the set equals the old script's over the real `host/` (the copy prefix and `.` -> `$hd` aside), for all ten cores with their `mono_env`.
  `ci/test_plan.sh` D11 (host/top.v: no large lane; `cores/nes/mono/host/top.v`: large-nes only; `cores/common/iec_bus.v`: every large core; the old `plan.py` / `lib.sh` fail D9-1 / the large flow), `ci/test_large_flow.sh` (host change keeps the key, a change in the core's copy moves it; the old `mono_key` fails it).
- **Open:** no Vivado run of the changed scripts yet (the harness proves the paths, not the elaboration); a host fix (e.g. the next hdmi_timing change) now reaches a large core only by copying the file in and updating `FROM`. Benches under `cores/*/sim` and `ci/sims.deps` still name `host/` files for some large-core benches; they read the shared host, not the copy.
- **C128 no HDMI signal (board, 2026-11-01, bundle t164-774497b1):** cause: `cores/c128/mono/host/overlay.v` and `c64_text.v` (host copies of WP6) got the pixel enable `ce` (and `hact` / `vact` / `xact0` / `yact0`) as inputs, `cores/c128/mono/top.v` connected them only on `pix_layer`; on `ovl` and `txt` the ports stayed open (run 1128 log: `[Synth 8-7071] port 'ce' of module 'c64_text' is unconnected ... top.v:1424`), an open input is 0, every `if (ce)` register of the picture path froze, de / hs / vs stopped. Fix: `ce(1'b1)` and the fixed 1080p active area (1920 x 1080 at 768 / 45, the same as on `pxl`) on `ovl` and `txt`. Needs a board retest (no bench elaborates this top).

### Constraint fail-fast in the host builds, built 2026-11-01
- **Why:** run 1119 routed the host for 91 min at WNS -6 ns because `host/pins.xdc` `set_clock_groups` matched nothing (`CRITICAL WARNING: [Vivado 12-4739] set_clock_groups:No valid object(s) found`); Vivado drops the whole command and times every asynchronous crossing. Only the log grep after a core build (`ci/bitstream_layers.sh`) looked for it.
- **Built:** `trx_check_constraints <where>` in the new `host/trx_constraints.tcl`, sourced by `host/build.tcl` and `host/core.tcl` only: `get_msg_config -id {Vivado 12-4739} -count` must be 0, else `error` (names the id and the count). A failing counter query prints a WARNING and does not pass silently. Called right before the first `opt_design` (after read_xdc and synthesis; `core.tcl` opens the host checkpoint, so it only sees what its own session raised). `{Common 17-55}` is not checked (false-positive risk, Alex/coordinator 2026-11-01).
- **Large cores:** not covered. `tools/vivado_lib.tcl` and `cores/<id>/mono/build_mono.tcl` are inputs of their keys, and a large core must not rebuild for a host/infra change (Alex 2026-11-01); a large core gets the check only when its copy of the host files (`mono/host`) and its `build_mono.tcl` are refreshed on purpose. The old log grep stays for cores.
- **Check:** `ci/test_constraint_check.sh` (in `ci/lint.sh`): both scripts source the file and call the proc before the first bare `opt_design`; the proc runs under `tclsh` against a fake `get_msg_config` (0 passes, 1 stops and names `12-4739`); control: `host/core.tcl` without the call is caught. `ci/test_large_flow.sh` and `ci/test_plan.sh` pass: a host-only change moves no large-core key.
- **Open:** no Vivado run yet, so it is not proven that the counter holds the messages at that point of the flow. The host key and the DFX core keys move (host content).

### WP18 the A1200 onto the host copy, whole (Q1 "A1200 onto the host copy now, whole"), built 2026-11-01 (branch t164-wp18-a1200)
- **What the A1200 had of its own, diffed against `host/top.v`.** `mono/top.v` was `host/top.v` of the HDMI-mode retrofit (T107 D7): the `vm_*` request
  sequencer, `mmcm_pix`, the encoder instance with pixel repetition, `hdmi_gtp`, `hdmi_meas` and `hdmi_req_s` in the open, `SMOOTH(0)`, no `trx_gain`,
  `esp_ctl`, `portlock_clk`, `NO_NET`. Its `layer2` instance, port list and every other line matched the host's; nothing in it was the A1200's. So no
  parameter and no adapter is needed in the top: the A1200 builds the host copy's `top.v` unchanged, as the A500 does, with the same 54 files
  (`cores/amiga1200/mono/host` is byte-identical to `cores/amiga/mono/host` apart from `FROM`, `cmp` over both trees). The picture functions come with it: `hdmi_out`
  (request 0x4F00, status 0x4F04 magic 0x48), the 9 rows, Scale and Smoothing through `PX_OSIZE` / `PX_CTRL` b6, the overlay, `trx_gain`, `bling_io`
  (its fork was already identical to the copy's file, `bling_io.diff` was empty), keys.
- **Removed.** `mono/top.v`, `overlay.v` (+ .diff), `bling_io.v` (+ .diff), `hdmi_timing.v`, `cdce_mode.v`, `hdmi_modes.vh`, `mono/hdmi/*` (three encoder forks and
  their diffs); in the core `rtl/trx_gain.v`, `VOL_CFG` (0x0A9, `amiga_regs`, the gain instance in `amiga_core`; the `.diff` files regenerated, `check_wrapper_fork.sh`
  ok for all ten pairs, its `bling_io` check dropped); in the application the A1200's own Volume item (`b_vol`, `vol_names`, `AM_VOL_CFG`): the generic Volume
  row (host `trx_gain`) now serves the A1200 like every core; no app fallback for the old gateware. Benches: `amiga1200_gain` (replaced by `host_gain`),
  `hdmi_timing` and `amiga1200_modepath` (their A1200-fork versions) deleted.
- **Kept (the A1200's own).** `layer2_amiga` with the three generics (`CHS_INIT 16`, `MEM_INIT 3`, `HDF_SECT 1`) as parameter defaults, TG68K / 68020 (`ENABLE_TG68K`), AGA,
  Zorro III, the forks of the shared Minimig files (`rtl/*.diff`), the 25 MHz SD with IOB flops (`sd/sd_mono.tcl`: `ultimate_sys.vhd` of the copy with `i_sd` swapped in the
  build directory), its `core_sources.tcl` list. `top.v` feeds `drive_leds_n[0]/[1]` to `bling_io` and `case_leds` exactly as before (T159; after the merge of `t164-led` the
  Amiga's `layer2_amiga.v` carries the LED itself, the three regsubs of `build_mono.tcl` are gone).
- **`NO_C64VIDEO`** (`core.toml` `mono_env`): the A1200 is no C64 and had neither `vic_scaler`, `pal_ram` nor `c64_text` before; `hdmi_out` leaves them out as in the console builds.
  `NO_NET` is not set (the A1200 had Ethernet).
- **Benches.** `host/sim/hdmi_timing` and `host/sim/modepath` take `HOSTDIR` (default `host`), and the A1200 runs them on `cores/amiga1200/mono/host` (`amiga1200_hdmi_timing`,
  `amiga1200_modepath`, own build directory); `amiga1200_bling` (lamp, 9 controls), `amiga1200_motorwire` (drv_led / drv_hd to case LED 1 and the lamp, 3 controls) and
  `amiga1200_motorled` now read `mono/host/top.v` and `mono/host/bling_io.v`. `tools/hdmi_modes.py`: the A1200 edition has no file left, `check` keeps its limit and decode
  checks (selftest still catches all its mutations). `tests/coreapp`: `test_volume` (the A1200 gets the generic item; a core with its own Volume item keeps it, new
  case), golden `menu_amiga1200.txt`, `test_desc` (one setting more than the A500, not two), control `vol_reg` removed.
- **Checks.** Tcl harness (stubbed Vivado commands, empty `host/` as the working directory) runs `build_mono.tcl` of the A1200: every file read exists except `vendor/gideon`
  (not checked out in the worktree), defines `ENABLE_TG68K INFER_DPRAM NO_C64VIDEO NO_STREAMS NO_UCI`. `tools/check_large_copies.py`: ok (10 cores); `ci/lanes.py check`: ok;
  `tools/prepush.sh`: lint ok.
- **Area (estimate, no Vivado).** Run 1104: 23,916 LUT flat. Host package items the A1200 did not have (spec 3c): `tmds_rep` +140, `flash_gate` lock +45, `esp_ctl` +60, port clock +20,
  request glue +55, `hdmi_out` share about +30 = about +350 LUT; smoothing (`SMOOTH(1)` in the host top) +350..650 LUT, +6 DSP. Total about 24,300 to 24,900 LUT (74.5 to 76.4 %
  of 32,600), below the c128 (85 %) and neogeo; block RAM unchanged (the smoothing line slots fit the same buffer). Not a placement risk by the numbers; timing of `hdmi_out` in the
  flat design (the pixel clock paths are the A500's) is for the Vivado run.

### WP18 the C128 onto the host copy (branch t164-c128 = t164 + `t140-d24` merged), built 2026-11-01
- **Merge.** `git merge --no-ff t140-d24` into t164: conflicts in `coreapp/src/hw.cc` (t164's `master_volume` and t140's `video_format` are both `CoreIo` overrides: both kept),
  `coreapp/src/picture.h` (t140's `picture_set_system` declaration stays beside t164's `picture_service` / confirm block, which is untouched) and `ci/sims.deps` (the `neogeo_pad` line
  and the two c128 lines: both kept). `build_mono.tcl`, `build.sh`, `top.v` and the specs merged without a conflict; this section reworks the first three.
- **What the C128 had of its own in `top.v`, diffed against the host copy's `top.v`** (`diff cores/amiga/mono/host/top.v cores/c128/mono/top.v`, 354 lines): the old fork was `host/top.v` of
  before WP8: `cdce_i2c`, `mmcm_pix`, `vic_scaler` + `overlay` + `pix_layer` + `c64_text` + `hdmi` encoder + GTP glue in the top with a fixed 1080p area, no `esp_ctl`, no `portlock_clk`,
  no `trx_gain`, `HOST_CAPS` without Ethernet and an `ultimate_sys` with a hand-cut Ethernet block. Of that, only two things are the C128's: the A/V port (`AVID_*` ports, `c128_avout`,
  `c128_avpins`, the forwarded DAC clock) and the Ethernet cut, which the host has as `NO_NET` / `g_net` since the slim console builds.
- **Built.** `cores/c128/mono/host` is the A500's copy again (`FROM`: aa45528c2) minus nothing: `top.v`, `ultimate_sys.vhd`, `hdmi_out.v`, `hdmi_timing.v`, `mode_req.v`, `cdce_mode.v`,
  `tmds_rep.v`, `portlock_clk.v`, `esp_ctl.v`, `hdmi_modes.vh`, `hdmi_enc/` in, `cdce_i2c.v` out (`check_large_copies.py` ok). `mono/top.v` is the copy's `top.v` plus the A/V port
  and `c64_type(1'b1)` (the C128 is a C64-type core: the request is held to the System Mode's family and to the 1080p VIC-locked rows, `mode_req` follows `fam_v60` after a System Mode
  change); `mono/top.v.diff` is regenerated against `mono/host/top.v` (36 lines). `mono/ultimate_sys.vhd` and its `.diff` are deleted; the build passes `g_net = 0` through the define
  `NO_NET` (`core.toml` `TRX_DEFS`). `build_mono.tcl` reads the A500's file list (with `$c128/mono/top.v`), no `ultimate_sys` swap. The overlay stays the stub
  (`mono/stubs/overlay_stub.v`, now with the WP6 ports of `host/overlay.v`, the file of the Neo Geo): the generic core application draws on the pixel layer, so the 3.5 RAMB36 of the real
  overlay are not spent; the menu on the HDMI picture is the pixel layer's (decision for the person who wants the real overlay back: below).
- **Kept.** The fix of 8e9424645 (`ce` and the active area on `ovl` / `txt`) is gone with the instances: they are inside `hdmi_out` now, which drives `ce = pce` and the mode's area; the
  stub has `ce` as an input and uses it. The core's own `cache_cfg_pkg.vhd`, `c128_avout` / `c128_avpins` / `c128_yc`, `NO_JTAG_DBG` (`--jtag`), the variant defines.
- **Benches.** `c128_nonet` now runs the copy's `ultimate_sys.vhd` with `g_net => 0` (tb generic map), guards: `g_eth` generate holds `ethernet_interface`, `uart_dma` outside it, `core.toml`
  defines `NO_NET`, `top.v` passes `g_net(NET_ON)`, `NET_ON = 0` and capability `4E40202` under `NO_NET`; controls: ack removed, Ethernet IRQ high, no `uart_dma`, `NET_ON = 1` in the top
  (all four fail as they must). `c128_avout` reads `mono/host/pal_ram.v` (the copy the build uses; `ci/sims.deps` follows). On the builder (`trx64-sim-env:4`, clone of the commit with the
  submodule at its pin): `c128_avpins` PASS, `c128_variant` PASS, `c128_scan` PASS, `c128_avout` PASS (controls BADINC, VINV_OLD fail as they must), `c128_nonet` PASS.
  Tcl harness (stubbed Vivado commands, `host/` as the working directory) runs `build_mono.tcl` of the C128 with its `mono_env`: every file read exists except `vendor/gideon` (not checked out
  in the worktree).
- **Area (estimate, no Vivado).** Run 1079: 27,670 LUT (84.9 %), block RAM 75 of 75. Since: JTAG debug master out -690 (T140 D24), RAM in the DDR2 + pin counters about +540 (T140 D25/D27),
  the host package items the old fork did not have `tmds_rep` +140, `esp_ctl` +60, `portlock_clk` +20, request glue +55, `trx_gain` about +60, `hdmi_out` share +30 (about +365 together; `cdce_mode`
  replaces `cdce_i2c`, counted in the glue); smoothing was already in (`pix_layer` default `SMOOTH = 1` in the old instance). Sum about 27,900 LUT (85.6 %). Block RAM: 75 - 14 (D27's
  estimate; Vivado decides) = 61 of 75; the real overlay would take 3.5 of the 14. The host package adds no block RAM (spec 3c). Not a placement risk by these numbers; they stand on
  two unmeasured estimates (D25/D27 freed tiles, LUT).
- **App side (builder, `u64-build-env:ci`, clone of the commit with the submodule at its pin).** `ci/trxapps.sh` rc=0: RISC-V host app and core app build (core app 1,568,028 bytes, 398,040 of the slot unused), apptests hostapp / coreapp / loader PASS, render benches PASS (1129 s), fs64 benches. The first run stopped at the RISC-V build (`picture_set_system` called t140's `apply_mode(c, ...)`, t164's is `bool apply_mode(sys, hdmi)`), the second at `test_c128` ("a Picture change after NTSC keeps NTSC": t164 builds the Picture list per machine family `g_msys`, a System Mode change must move it): `picture.cc` has `set_machine_sys` (shared with `picture_start`) and the list-changed event for `picture_service`. A System Mode change does not ask the Q7 confirmation (the HDMI row stays, only the family follows `video_format`); that is a decision for Alex if he wants it asked. `tools/prepush.sh` lint ok, `check_large_copies.py`, `ci/lanes.py check`, `check_todo` ok. No `check_wrapper_fork.sh` exists in the tree.

### D10a the menu in the modes below 960 wide (Alex 2026-11-01), built 2026-11-01
- **Canvas.** `app/video/trxcanvas.h` (pure, one function for both applications and the benches): a mode 960 or more wide keeps the 960 x 540 canvas at the whole factor
  `max(1, floor(vact / 540))`, centred (D10); a mode narrower than 960 gets a canvas of its own size at factor 1: **576p 720 x 576, 480p 640 x 480 (NTSC60 row: 640 wide, not 720; the
  PAL50 row is 720 x 576), 800 x 600**. Centre offsets are 0 there; the canvas is the frame, nothing is cut, nothing is scaled. 1024 x 768 and 1280 x 1024 keep 960 x 540 (factor 1).
- **Gateware: no change.** The canvas size is the application's: `pix_layer.v` takes `PX_SIZE` (w <= 960, line buffer), `PX_STRIDE` and `PX_OSIZE` from the app (all three exist since WP5);
  `overlay.v` is the Ultimate app's text overlay (a canvas of the mode's own size is already "at 1" there, WP6) and does not draw our menu. No host key change from this item.
- **Port** (`trx_lv_port.cc`, `trx_lv_set_canvas(w, h)`): the display is laid out for the canvas again (`lv_display_set_resolution` + `lv_display_set_buffers`), the buffers keep their
  960 x 540 x 2 bytes (every canvas has fewer pixels; 800 x 600 = 480,000), the stride is the canvas width (`PX_STRIDE`, `PX_SIZE` written with the first picture); the layer goes off
  until the next flush, which draws the whole screen. Before `trx_lv_open()` it only sets the size the display is created with.
- **View** (`trxview.cc`): `trx_canvas_set/w/h`, `trx_compact()`; `SW`/`SH` of the shell, the overlay container, the splash (redrawn for a new canvas, `trx_splash_relayout`) and the toast/dialog
  cards follow the canvas. Compact measures of the shell: rail 108 (148), top bar 34 (36), tab strip 34 (38), footer 26 (28); a top-bar chip that would run into the title is left out;
  a panel kicker that would run into its chip ends in dots; the footer legend is cut to the canvas with dots.
- **Core app.** `Video::place_menu` tells the canvas first (`PxPort::menu_canvas` -> `ui_set_canvas`: view, port, display, the next render builds the frame again), then the place. Side
  panels 190 / 170 / 240 (260 / 230 / 330) in the compact layout, the Timewarp bar is the frame less 32 (at most 768) and centred, dialogs and the bar/toast cards are the canvas less 20 a side.
- **Host app** (`hostui.cc`): every measure is a function of the canvas. Compact: navigation 132 (170) with 14 px names, header 34, a footer of **two lines** (the key legends are not cut:
  the longest is 110 characters), page title and the sub line at the top (each page has a shorter sub line for the compact layout, so that it fits whole; the dots stay as the last resort), the Boot menu shows 4 / 5 / 6 rows (5), the editor panel has one setting a line, the File Browser
  gives the list and the manifest panel half of the width each (the value column starts after the longest key, measured; values are cut with dots, keys never) and 11 / 14 / 15 rows (13), Settings 32 px per row, Cores and ROMs 16 .. rows (17), the format
  page is the column less 20 wide, the colour test has 4 columns; start/confirm/rename pages are centred in the canvas. A canvas change (`canvas_apply`: V key, stored mode, hot-plug follow)
  cleans the screen and builds every widget again (`ui_built`); state (selection, windows, notes) stays.
- **Decided here, open for Alex:** the menu snapshot (T146, `menu.snap`) is the 960 x 540 canvas: **in the compact modes it is not saved and the first picture at start-up is the splash**.
  Supporting it needs the canvas known before the mode is (the file key holds the stored mode, a stored "from the screen" is unknown until the screen was asked).
- **Benches.** `tests/view_bounds.h`: every visible object of the active screen lies inside the canvas, and no label is cut by its parent (2 px at the sides, 3 below: a font taller than its row).
  Core app (`tests/coreapp/render/run.sh`): `modes_menu` renders every page of the A1200 menu (areas F1-F8 x tabs 1-5, the Library in a folder) in 1080p, 720p, 800 x 600, 576p, 480p and checks it
  against the canvas of the mode; `a1200_menu` checks canvas, factor and centring per mode; `tests/coreapp/test_scale.cc` rows for 800 x 600 / 576p and the canvas told before the place;
  controls `TRX_CANVAS_FIXED` (the old fixed 960 view), `mn_fixed960`, `mn_nocanvas`, `mn_origin`, `mn_frac`. Host app (`tests/hostapp/render/run.sh`): `modes_pages` walks menu, editor, browser (files
  and media level), network, settings, Cores and ROMs, colour test and the start, confirm, rename and format pages in the five modes; control `cvfix`. The bench's default start mode is 576p
  (no EDID, Q9), so most of the old checks now run on the 720 x 576 canvas; the snapshot scenario selects 1080p first.
- **Polish and integration proof, 2026-11-01 (d10p, on `t164` after the T167 5.10 / T137 D11 / D10a merges).** The panel's key column is the width of the longest key (measured, `key_words` in `build_browse`),
  the panel takes half of the width, and `host` in the top bar follows the measured width of `TRX64` with a gap; each page's sub line has a shorter compact text (no ellipsis at 480p / 576p); the core app's
  panel kicker is one line (a label with a width but no height wrapped into the row below instead of ending in dots, `trx_panel`) and reads "Session" in the compact layout. `tests/view_bounds.h` counts a key label
  that its long mode would end in dots as cut (`keys` argument; the host bench names the file panel's twelve keys); control `keycut` = the old 62 px column. The host render bench's `put()` took a failed `fopen` for
  granted: the mutant of control `t139_del_once` deletes the folder `dst`, `put` wrote into a null `FILE` and the control died of SIGSEGV (found with an ASan build of the mutant: `render.cc` `put` <- `scenarios_t139`),
  not by a check; `put` is a CHECK now, and `_control` / `cvfix` in `tests/hostapp/render/run.sh` turn a signal (exit >= 128) into a red run of their own, so that no control can pass by a crash.
  Run on the builder before the merge of main (RISC-V host app + core app build ok; `ci/apptests.sh` all three suites pass; view render 20 checks; host render main 825 checks, 0 failed; `t139_del_once` fails by 18 checks, `keycut` by 3,
  no crash). The core app render exit 1 of that run (its log was not kept; the cause below is reproduced, not read from it): `tests/lvgl_objs.sh` built the shared LVGL objects in the cache directory itself (`rm -rf` first), so two benches that start together on a cold cache
  deleted each other's files (`cc: can't create lv_os.o`, rc 1); reproduced with two parallel calls (rc 1 and 0, the second one's error is that line), the
  bench alone passes (195 checks, 0 failed, on the branch before and after the merge). Now each call builds in its own temp directory and moves it into place when it is the first to finish (both calls rc 0).
  In the merged tree `wp17_place_rounded_up` fails by 2 checks as it must (no stop any more).


- **Boot in a mode whose factor differs from the first one (boot720), 2026-11-04 (branch `t164-boot720`).** Board finding (Alex, main-7fda7e1f, Samsung, 720p stored): after a reboot the splash, the saved menu picture and the menu are drawn
  too big (top-left part only), a live switch is right. Cause found in the code: the pixel layer is opened with the 1080p fallback placement (`trx_lv_port.cc` `layer_up()`, `place_set` false: `PX_OSIZE` = canvas x `TRX_LV_SCALE`
  = 1920 x 1080), and the only thing that replaces it is `menu_place()` -> `trx_lv_set_place()`. `startup()` (`hostui.cc`, "Setting the screen mode") had `menu_place()` inside `if (!direct_video_done)`: when the direct phase
  had put the stored mode up (`direct_phase`: `hv_start(true, ...)`, text line) and then left for the menu (default file missing / changed / refused, key, failed start), the mode was up but the layer kept the 1080p
  size for the whole session, until a live switch wrote a different place (`canvas_apply` returns early when only the factor differs, so nothing else relayouts). The normal path (no direct phase) runs `hv_start` + `menu_place` and is
  right in the code. Not measured on the board: no log of that boot was kept, so that Alex's boot took the direct path is read from the code (it fits all three symptoms: splash and snap are drawn before any placement,
  and the menu never gets one), not seen. Fix: `picture_sync()` (the mode `hv_state()` says is up -> `menu_place()`) right after `trx_lv_open()`, before `snap_try_show()` / `trx_splash_open()` (`trx_lv_set_place` before the first flush is kept and used by `layer_up`; `canvas_apply` returns before `ui_built`), and `menu_place()` after the `if` in the mode step, always. A saved picture is refused for another mode (its key holds the stored mode) and is never saved for a compact canvas, so it is not shown at a wrong size. Proof: render bench `splash_startup` first models a stored 720p put up by the direct phase
  (`hostui_test_direct_video(true)`) and checks that the place was 160,90 960 x 540 already when the splash was first drawn (probe at step 0) and at the end; control `boot720_noearly` (the early call removed, the old order) fails by those checks. The coordinator's QC found that the first version placed the menu only.
  **The direct-boot text line ("Starting <name> - hold C= for the menu", `ovtext.cc`) is not the pixel layer:** it is the overlay (`host/overlay.v`), whose scale the gateware chooses by itself from the box and the mode's active area (`sx0 = 4 if 4*w <= hact, else 3, 2, 1`; `sy` the same on the height; `w = cpl*cw` = 40 x 8 = 320, `h` = rows x 9). 320 x 4 = 1280, so in 720p it is drawn at 4 x 4 and fills the whole width, in 1080p also at 4 x 4 (cap) but only two thirds of the width. It follows the mode that is up already (no placement register, no stale 1080p value), so it is not the boot720 fault; that it looks too big in 720p is the rule of the gateware (the cap of 4 is reached in both). Not changed: a smaller line in 720p would need the app to widen the box (e.g. 41 columns -> 3 x 3) against that rule, or a gateware change; for Alex to decide.

### D10q labels whole at 800 x 600 and in every mode; Auto names its mode (Alex 2026-11-04, board of main-7fda7e1f), branch t164-d10q
- **Finding.** At 800 x 600 the host menu still ended texts in dots: "File Brow..." (side bar), "Protect Flash from upda..." and "Inspect .cfw/.ue2 for ES..." (F8 rows). D10p measured the file panel's
  keys only and `view_bounds` named only those keys, so nothing else was ever looked at; the side bar (fixed 132 px, name 88 px) and the F8 label column (fixed 210 px) were cut in every compact mode.
- **Host.** `nav_w_compact()` (`hostui.cc`): the compact navigation is as wide as its longest name needs (key box 42 + the name measured in `F_SMALL` + 6) and at least 132; `NAV_W` is that in the compact
  layout (`nav_name[]` moved above it). `set_names[]` holds the F8 row labels by kind; `build_settings` measures the longest (`F_TEXT`) for the compact label column (`nw` = width + 4, value column after it) and
  keeps 290 / 330 on the full canvas. The measure is the text, never a number from the screen: every compact mode gets the same rule (480p, 576p, 800 x 600); 720p, 1024 x 768 and 1080p use the 960 x 540 canvas, which had room.
- **Core app, Auto (D10c amended).** The first value of the Picture item is a text that `picture_start` (and a screen change in `picture_service`) rewrites from `target(c, 0, ...)`: "Auto (1280 x 720 from host)"
  when the mode Auto asks for is the one the host stored in `host.cfg` `[Video] Mode`, else "Auto (W x H from screen)" (what the core picked from the EDID, or the mode the host had up when it stored none);
  `picture_extend` leaves plain "Auto" until the picture starts. The stored label stays findable: a .cfg, "Load Settings" or a web page that says "Auto" or another host mode's note ("Auto (1920 x 1080 from host)") is
  found by a second pass that compares the labels without their trailing note (`cfgfile_label_head_equal`, `Settings::label_index`, `settings_ops.cc` `find_value` and `load_item`), so no stored Auto is dropped. The write path stores plain "Auto" (`Settings::write_cfg` cuts the note of the Picture item's value): the note is for the display and goes stale with the host's mode (Alex 11-04, QC). Check in `test_picture.cc` `host_auto()` (save Auto, the file says `Picture=Auto`, no bracket); control `pic_autostore` writes the long text.
- **Bench, host (`tests/hostapp/render`).** The mode loop has six modes (1080p, 720p, 800x600, 1024x768, 576p, 480p: the first four stay on the 960 x 540 canvas or its own size as `trx_canvas_for` says). `view_bounds` is
  given as keys the file panel's twelve, the five navigation names (`hostui_test_nav_name`) and the eight F8 labels (`hostui_test_set_name`): a label that its long mode ended in dots counts as cut, in every mode and on every page.
  A failing mode prints `FAIL MODES <mode>: n page(s)`. Controls `setcut` (the old fixed 210 px label column) and `navcut` (the old fixed 132 px side bar): each must go red and must have a red line `FAIL MODES 800x600`
  (`CTL_EXPECT` in `run.sh`, part of the control's cache key).
- **Bench, core app (`tests/coreapp/render`).** `modes_picture_row`: the NES (a frame-buffer core with Picture, Scale, Smoothing) in all six modes, once with a `host.cfg` that stores the mode ("Auto (<mode> from host)") and once
  without ("Auto (... from screen)"); the Picture row must read exactly that (the D10c check) and `view_bounds` gets the value as a key (not cut to dots, inside its row). Control `TRX_AUTO_PLAIN` (the old plain "Auto") must go red.
  `tests/coreapp` `test_picture.cc` `host_auto()`: the text before the start, with host 1080p / 576p, "From the screen", no file; a stored "Auto", another host mode's note and a lower-case form still find value 0 (layer core).
  Controls `pic_autoplain` and `pic_autohead` (no second pass).
- **Result (builder, `u64-build-env:ci`, `ci/trxapps.sh` with the CI cache mounted, tree of branch t164-d10q at bcd08435).** RISC-V host app and core app link (hostapp.app 873,204 B, coreapp.app 1,639,888 B);
  `apptests`: hostapp cached PASS (same key, passed at e59d6999), coreapp 88 test programs, 1272 controls, every control failed as it must; loader PASS. Host render: `render bench: 1203 checks, 0 failed`; `control setcut: fails as it
  must (3 check(s) red)`, `control navcut: fails as it must (3 check(s) red)`, `control keycut: fails as it must (3 check(s) red)`. Core app render: `coreapp render: 250 checks, 0 failed`, `BOUNDS <mode> picture row (host|screen) ... 0 outside, 0 cut`
  in all six modes; `control_TRX_AUTO_PLAIN` red (12 checks). `pic_autoplain` fails as it must (4 checks red), `pic_autohead` (3 checks red). Pictures: `modes-800x600-settings.png`, `modes-800x600-browser.png`, `modes-<mode>-picture-row.png`.
- **F8 value column (QC 11-04).** The Picture value is in the dots check too (keys "from the screen: <mode>" and "screen: <mode>"). It found "from the screen: 576p / ..." cut at 480p (value column 206 px): the compact page now says "screen: 576p / 480p" (`COMPACT` in the `SK_HDMI` row). Control `valcut` (value column pushed right by 260 px) goes red with `FAIL MODES 800x600` (2 checks red). Result of the last run (tree 10e22bac): `trxapps rc=0`, hostapp.app 873,228 B, coreapp.app 1,640,016 B, apptests all three suites PASS, `control valcut: fails as it must (2 check(s) red)`, `control navcut: ... (3 check(s) red)`; the `set_writes_all` control follows the new write line.
- **Not done.** The "not offered" form of the F8 Picture value is not measured. The board test: 800 x 600 F8 page and side bar, a core's Picture value (board-tests 1.3, 1.8).

### WP8c large-core benches onto the copies; hdmi_out controls (branch t164-wp8b2 = t164 + t164-wp8b), 2026-11-01
- **Merge.** `t164-wp8b` (3 commits) merged into the current `t164`: conflicts only in `TODO.md` and `docs/todo/T164.md` (the T164 line, t164's text kept); the
  bench files (`host/sim/hdmi_out/*`, `vicscale/tb_vicscale.v`) merged clean.
- **Large-core benches read their core's own copy (T164 "large cores take copies").** `run_l2.sh` (A1200 build, `TG68K` set -> `cores/amiga1200/mono/host`, else
  `cores/amiga/mono/host`), `run_sdc.sh` (per part), `run_lat.sh` (`cal_seq`, `ddr2_lean`, `ddr2_port`), `zx/sim/run.sh` (`vic_scaler`, `pal_ram`, `ddr2_port` ->
  `cores/zx/mono/host`), `amiga1200/sim/motorwire` (`case_leds`), `c128/sim/nonet` (GHDL: the copy's `gideon_vhdl.tcl` with its `$hd` names and the copy's VHDL),
  `tools/check_layer2_pins.py` (`HOSTDIR`, used by `zx`, `cpc`, `zxnext`, `snes` `run_pins.sh` -> their copy's `top.v`). `ci/sims.deps` follows: the keys of these benches
  hold the copy's files, no `host/` file; `@neogeo` lost `host/ddr2_port.v` (the bench reads it nowhere, comments only). Left on purpose: `host/sim/mig_ui_model.v`
  (a bench model built into no core; 22 benches) and the `HOSTDIR` bench dirs `host/sim/hdmi_timing|modepath` of `amiga1200_hdmi_timing|modepath` (the script is the
  host's, its files the copy's). `ci/test_plan.sh` D9-7: no large-lane bench declares a `host/` file except those; control (a bench with `host/top.v`) fails. Plan:
  `ci/plan.py` already gave a host-only change no large lane (D10-1, D11: `large == []`), so no large bench is planned; the key now does not move either.
- **`ci/sims.sh --check-deps`** (a copy of the repository holding only the declared files) found three lines that missed a file, fixed: the zx benches read
  `vic_scaler` / `pal_ram` (declared for two only), `amiga_cfg` and `amiga1200_l2` run builds without `TG68K` (the OCS copy of `ddr2_port.v`).
- **Merge with `main` and the finish, 2026-11-04 (builder, `trx64-sim-env:4`, `ci/sims.sh -t 2`, load 20-40 from other jobs, so the 5400 s of `tools/benches.sh` and the 9000 s of the `hdmi_out` line were not enough: own runs with a longer outer limit).**
  `git merge main`: conflicts only in `TODO.md`, `docs/todo/T164.md` (the T164 status line, both texts joined) and this file (both §5 blocks kept). Main's convention (`ctl| ` on every line a control prints, `ci/sims.sh` reads any bare
  FAIL) was missing in `host/sim/hdmi_out/run.sh`: the controls' logs and verdict lines now carry `ctl| `. Three faults found by the runs, all fixed here: (1) `cores/cpc/sim/run_pins.sh` control DEVICE `sed` no longer matched (T159 renamed the line to
  `wire z_running, z_drv_led;`; the bench failed with "the mutation did not change the wrapper", also on `main`); (2) `cores/c128/sim/nonet/run.sh` resolved the copy's relative names (`../cores/common/drive_*.vhd`) against the copy, not `host/`
  (`ghdl: cannot open .../mono/host/../cores/common/drive_mem.vhd`, FAIL in 0 s); (3) `ci/sims.deps` `c128_nonet` lacked those drive files, `host/sim/usb/{unisim_sim,tl_string_util_pkg}.vhd` and `mono/core.toml` (found with the sandbox run, `ci/sims.sh --check-deps c128_nonet`: PASS 23 s). `ci/test_plan.sh` D9-7 takes `host/sim/usb/*` (GHDL bench models, in no core) as sim models like the `host/sim/*.v` ones.
  `hdmi_out`'s control `vicsel` now fails with "the VIC lock never came" (the bench waits for the VIC lock before it checks a pixel), no longer with "video pixel": the expectation follows; `ONLY=<controls>` runs single controls.
- **Results (all on this branch's code; PASS = the bench's own verdict line).**
  `hdmi_out` in two runs (`MODES=0`, `MODES=2`, 8 jobs each): `hdmi_out mode 0 PASS`, `hdmi_out mode 2 PASS`. Controls (each must fail): ceenc `FAIL (419432 errors)`, gear1 `FAIL timeout`, nolane0 `FAIL (481375 errors)`, novic0 `FAIL (1 errors)`,
  fwlong0 `FAIL (5005 errors)`; vicsel `FAIL the VIC lock never came` (single-control run `MODES=2 ONLY=vicsel`), novic `FAIL (1 errors)`, noaspect `FAIL (1 errors)`, nolane `FAIL (2861981 errors)`, fwlong `FAIL (9005 errors)`, rdmux `FAIL (4 errors)`;
  every one reported `failed as it must`. Last line of the `MODES=0` run: `PASS hdmi_out (... 11 controls failed as they must)`. `vicscale`: PASS 7894 s (`-f`; controls slip, slip60, noalign inside).
  Large-core benches: PASS amiga_sdc, amiga1200_sdc, amiga_lat, amiga1200_l2, amiga_cfg, amiga_cpu020, amiga_fbwin, amiga_motorpath, amiga1200_motorpath, amiga1200_motorwire; amiga_l2 PASS 1234 s, amiga_fastpat PASS 1067 s, amiga_z3 PASS 1390 s
  (first run TIMEOUT under load, rerun alone); zx_cpu, zx_machine, zx_video, zx_io, zx_snap, zx_pause PASS, zx_pixw PASS 1773 s, zx_switch PASS 1221 s (both rerun after TIMEOUT), zx_pins, zxnext_pins, snes_pins PASS; cpc_pins PASS (after fix 1);
  c128_nonet PASS with `ok   control eth_ack|eth_irq|nouart|nonet_top` (after fixes 2, 3); neogeo_mover, p68k, video, dl, audio, speed, pad, pixw PASS, neogeo_sprites cached PASS.
  `ci/test_plan.sh`: `D9-7 ok`, `plan test ok`. `tools/prepush.sh` (lint on the builder): `prepush: lint ok`.
  Not rerun: benches of these cores whose scripts and declared files this branch did not touch (the cpc, zxnext, snes, c128 bench bodies; their keys follow `ci/sims.deps` only where listed above).
  The `hdmi_out` line in `ci/sims.txt` has 9000 s: both modes together exceeded it under a load of 30 (TIMEOUT); each mode run alone (8 jobs) finished within 2.5 h at that load. Left as it is; the nightly runs it on the idle builder.

### D10c a core's picture "Auto" follows the host (Alex 2026-11-03, issue #24), built 2026-11-03 (branch t164-coreauto)
- **Cause.** The gateware path already took the mode the host handed over (D11c). Without it (an old host window, or a core whose gateware does not answer `0x4F04`: the large Amiga)
  `target()` asked `Video::wanted_hdmi()` - the mode that shows this core's picture largest, 1080p for most - so a host at 720p and a core menu at 1080p.
- **Path chosen: the file, not the hand-over.** The hand-over's `video` is the mode the host had up; it cannot tell "From the screen" (Auto stays as before) from a stored resolution, and it
  is the screen's best row when the host fell back. `picture_start()` reads `/Flash/trx/host.cfg` once (`read_host_choice`, `coreapp/src/picture.cc`): the flash disk is readable from the core
  app (`fsif.h` refuses writes only), the file is the host's `[section]` text, parsed by the shared `cfgfile_parse`; `[Video]` `Mode=` is matched by label against `video_hdmi_name()`
  (the labels the host writes). No gateware change, no change to the hand-over page or its contract (so DFX and large cores alike), no host file touched.
- **Order of `target()` for Auto.** A core's own choice (as before) > the host's own resolution from host.cfg when the screen takes it (gateware path and without it) > on the gateware path the
  mode the host handed over > as before (best row; without the gateware the core's wish). "From the screen", no file, an unreadable file, a `Mode` outside `[Video]` and a resolution the screen does
  not take all mean "as before". A core that draws its own picture keeps the screen's mode. The 8 s confirm rule is untouched (`picture_apply` still asks for every change).
- **Label.** "Auto" stays "Auto": the value is stored in the core's `.cfg` by label, a new label would drop every stored Auto; an old `.cfg` loads and now means the host's mode.
- **Open.** The host agent's `t164-picsave` fix decides what the host writes to `[Video] Mode`; this reads the same file and key, no coupling beyond that. Board: host at 720p, A1200 / large Amiga
  menu comes up at 720p.
- **Tests (`tests/coreapp`).** `test_picture.cc` `host_auto()` (no gateware: host 1080p + NES Auto -> 1080p where the NES alone takes 720p; host 576p wins over a 720p hand-over; core's own choice
  overrides and run-time Auto returns to the host's; old `.cfg`; "From the screen", no file, no section, other section -> as before; own-picture core unchanged),
  `test_pictfollow.cc` `d10c_host_auto()` (gateware: host 720p + Auto -> 720p with an empty hand-over; file over hand-over; own choice 1080p; "From the screen" / no file; host's mode not on the screen).
  Controls `pf_hostcfg_gw`, `pf_hostcfg_read`, `pic_hostcfg`, `pic_hostcfg_store`, `pic_hostcfg_own`, `pic_hostcfg_nofile`.
- **Night 1144 (2026-11-03), pixscale and hdmi_timing.** `pixscale` `FAIL (rc 2)`: rc 2 in `job.sh` = the run did not finish (iverilog failed, or vvp hit its own 4500 s timeout before `DONE`) - the 1080p runs under the night's load, not a mismatch; the failure report also aborted (`tail -3`, rejected by the builder's `tail`, under `set -e`) and hid the other runs: now `tail -n 3` and rc 2 is named in the report. `hdmi_timing`: `amiga1200_hdmi_timing` and `host_hdmi_timing` both ended at exactly 1500 s = the `timeout 1500` of the `all` run in `host/sim/hdmi_timing/run.sh` (no `FAIL mode` line, no PASS). The check of a3244a202 is right for the 4-term copies: on the builder, alone, with `-f`, host 774 s, amiga1200 777 s, c128 892 s all PASS (an `all` run is 9 modes in series, about 13 minutes unloaded; the night ran 16 benches at once). run.sh now says when the log has no verdict.
  Second cause of the same red: `pixscale` printed each control's `FAIL step ...` line bare (`control hshort| FAIL ...`), which `ci/sims.sh` reads as the bench's own FAIL (`FAIL (output)`, builder run 1834 s with all 10 runs passing): now `ctl| control <name>: ...`. `hdmi_timing`'s controls already used `ctl| `.

### Q7 amendment of 2026-11-04: the picture question is a box (built on branch `t152-d10d11`)

- Board photos of main-7fda7e1f showed the question only as a foot line. Host app (`hostui.cc` `pq_update`/`pq_hide`, built in `build_start`): while a change waits (`hv_state()->pending`) a
  box stands in the middle of the canvas: "Keep this picture?", "Picture: <mode>", "In N s the previous picture comes back.", a blank row, "Press any key to keep it" (T152 D10 layout). The foot note
  now says only "Picture: <mode>" (the question is the box); the box goes with the key (the key is still spent on keeping), the revert, a hot-plug follow or a mode change that does not wait. V
  during the question still steps on (the box's picture line follows). hostvideo.cc is untouched (8 s, store on key, revert without one): `run_hostvideo.sh` unchanged.
  Core app (`ui.cc` `ui_picture_question`, called from `run.cc`'s `picture_tick`): the message box text is now the countdown, a blank line, then the key line (it was key first).
- Proof: host render bench `picnote_case` (4 cases: File Browser and Settings, 1080p and 800x600): box shown, title/text/key, key row at least 20 px under the countdown and inside the box, box centred
  (within 2 px) and on the canvas, "In 5 s" after 3 s, box gone and "Picture kept" with the key, mode stored. Control `picnopq` (box never shown, the foot line only) fails by 16 checks (on picture-line checks);
  `picnobnote`/`picnosnote` still fail by 2. Core app bench: the message text ends with the blank row and the key line and `view_bounds` is clean in 1080p, 720p, 800x600, 576p, 480p; control
  `UI_PQ_KEY_FIRST` (the old order) fails by 5 checks. Results: host render `1202 checks, 0 failed`, core app render `200 checks, 0 failed`, apptests all three PASS.

### D10r 1280 x 1024 and 1024 x 768 keep the sharp 960 x 540 box (Alex 2026-11-05, issue #32)
- Issue #32: at 1280 x 1024 the menu is a 960 x 540 box in the middle of the screen (D10a). Proposed: a 640 x 512 canvas at factor 2, which fills the screen
  with no new buffer, LUT or DDR2 (327,680 pixels, under `TRX_CANVAS_MAX_PX`), but doubles the pixels and needs the compact layout.
- Alex (translated): "no, I find this is better than pixel mush". `trx_canvas_for()` stays as it is; nothing to build.
