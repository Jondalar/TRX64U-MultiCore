# Gap audit: Gideon's Ultimate app against our bitstream (M1 drop-in)

Date 2026-10-04. Scope: every hardware address, pin and capability bit that the app
(`software/ultimate-app`, branch trx64/main, `U64 == 2`) drives, against what
`designs/c64_basic` and `cores/*` do with it. Method: the register headers
(`system/u64.h`, `u2p.h`, `itu.h`, `iomap.h`, `io/c64/c64.h`), every user of their macros, every
`CFG_*` item of `u64/u64_config.cc` and `io/c64/c64.cc`, the capability checks, the
`C64_*` slot registers, Gideon's pin list (issue-615 XDC) against `top.v`'s ports. Our side from
the code only (`ultimate_sys.vhd` decode and register pages, `top.v`, `c64_core.v`, `cores/icepi`).
**Nothing here was run on the board**: every row is read from code; the ones that need a
board or Gideon-core measurement before a decision say so. Clean room kept: no UltiSID, Copper
or lp_filter source opened.

Severity: **M1** = drop-in blocker (a C64U owner's function that Gideon's core serves and ours
does not, or serves differently), **deg** = degraded (feature missing, machine usable),
**cos** = cosmetic or test-only, **n/a** = not applicable on the C64U / by design.
Size: S under a day, M a few days, L a task of its own.

How our side answers unmapped space (`ultimate_sys.vhd:787-830`): every address that no
`region` matches is `r_none`: acked, reads 0, writes dropped. The register pages
(0x10040000, 0x10100300, 0x10100400, 0x10180000, 0x10181000, 0x10181800) are a 256-byte RAM
with read-back (`:1106-1108`), except the bytes decoded explicitly (`:975-1150`, `:1160-1198`):
a write to them is accepted and has no effect.

## 1. Gaps not yet tracked (NEW)

| ID | Area | App side | Our side | User-visible effect | Sev | TODO | Size |
|----|------|----------|----------|---------------------|-----|------|------|
| G01 | REU image path | Every REU access of the app is at its own DDR address `REU_MEMORY_BASE 0x1000000` (`io/c64/c64.h:14`): load .reu / "Load into REU" (`filetype_reu.cc:117,127`), REU preload at boot (`reu_preloader.cc:104,135`), REST `runners:load_reu` (`api/route_runners.cc:249`), socket DMA REU write (`network/socket_dma.cc:173`), Save REU Memory (`c64_subsys.cc:666,854`), MP3-drive/GEOS detection (`c64.cc:1998,2024`), `attachment_reu.h` | The REU memory is at DDR2 byte 0x4000000 (word `REU_BASE 26'h2000000`, `top.v:281,315`), above the app's 26-bit window (64 MB, spec §3a), so the app cannot address it. The app's 0x01000000 range is plain heap-free DDR the REU never reads | .reu images, preload and Save REU act on memory the REU does not use: a REU loaded from the menu or REST is empty to the C64; Save REU writes garbage; Gideon's own layout (spec §3a) has the REU exactly at 0x01000000 | M1 | NEW (code-derived, board test: load a .reu, read from the C64) | S (REU_BASE to word 0x0800000; check T51's 16 MB "module" area and T21's "REU above 64 MB" plan) |
| G02 | REU enable and size | `C64_REU_ENABLE` (+08), `C64_REU_SIZE` (+09) written from `CFG_C64_REU_EN` (default **Disabled**) and `CFG_C64_REU_SIZE` (`c64.cc:377,408-416,1552,1672-1676,1724`; `C64::effectuate_settings` clears it next to a cartridge, `:376-378`) | Both bytes are page latches. `c64_top`'s `reu_present` is `flags[12]`, constant 1 in `C64_FLAGS` (`c64_core.v:27`; `c64_top.v:1415`); `c64_reu.v` has a fixed 22-bit expansion address (4 MB) | At the default configuration (REU disabled) a $DF00 probe still finds a 4 MB REU (GEOS, loaders, auto-detecting demos behave differently from Gideon's machine); the "RAM Expansion Unit"/"REU Size" items do nothing; the "never REU and a cartridge" rule is enforced in the app only | M1 | NEW (T60 is the size only) | S: `flags[12]` from `REU_ENABLE`, size mask from `REU_SIZE` (fold into T60) |
| G03 | CRT types without hardware, GeoRAM | The app accepts 30 CRT types (`c64_crt.cc:21-132`); `trx_cart_configure` finds a descriptor for 19 CART_TYPE bytes only (`trx_cart_desc.h:44-80`); for the rest "the cartridge stays off" (`trx_cart.cc:236-237`). GeoRAM Mode (`CFG_C64_REU_EN == 2`, `c64.cc:1630-1632`, `CART_TYPE_GEORAM`) has no descriptor either | `cart_slot` + T33 descriptors cover Normal 8K/16K/Ultimax, Simons, Ocean 8K/16K, C64GS, Megabyter, TwoMegabyter, EasyFlash, EFXL, Magic Desk 8K/16K, Dinamic, GMod2, C64MegaCart, AR/RR | Do not start (cartridge off, machine boots to BASIC): Final Cartridge III (3), KCS Power (2), Final I (13), Super Games (8), Epyx Fastload (10), Westermann (11), Zaxxon (18), Super Snapshot 5 (20), COMAL 80 (21), EXOS (44), Pagefox (53), Kingsoft Business Basic (54), Blackbox V3/V4/V8/V9 (64-66, 71), Atomic Power/Nordic (9, Nordic Replay subtype of 36), GeoRAM. VICE baseline already lists most (`docs/cart-vice-baseline.md`: "unsupported") | M1 (scope decision: which of these) | NEW (T33 non-goals name freezers and Epyx only; T16.2 does not list them) | L, a descriptor each; freezers/Epyx need dedicated models (PROP-002 §2.1) |
| G04 | Safe mode (hold RESTORE at power-on) | `U64_RESTORE_REG == 1` at boot = safe mode: defaults loaded, `/flash` filesystem not opened (`components/config.cc:48-50`, `filesystem/blockdev_flash.cc:160-162`) | Read is the constant 0x00 (`ultimate_sys.vhd:1182`); the RESTORE pin (`KBJOY_RESTORE`) exists in `top.v` and goes to NMI only | A tester with a broken setting (wrong System Mode, bad palette, a crashing config) has no factory-reset path except re-flashing; the same gesture on Gideon's firmware works | deg (propose M1) | NEW | S (read the synchronized RESTORE pin level at 0x10100402; must be the level at boot, before the app's NMI use) |
| G05 | Time of day | `RTC_TIMER_SECONDS` 32-bit at 0x10060400 (`io/rtc/rtc_dummy.cc:25,109,114`; the U64-II build uses `rtc_dummy.cc`, `target/u64ii/riscv/ultimate/Makefile:72`): read for every FAT/FTP/REST timestamp, written by SNTP and the Clock settings | `0x060400` is not decoded (`ultimate_sys.vhd:787-830`): reads 0, writes dropped | Clock stays at the epoch: files written to SD/USB carry 1970/1980 dates, FTP and `info` show 1970, SNTP "sets" nothing | deg | NEW (spec §3b lists it as "OPT (0 = 1970 dates)") | S (Gideon's open `fpga/ip/clock/real_time_clock.vhd` into the IO decode; the app sets it, nothing to calibrate) |
| G06 | DMA through the bus while the C64 runs | `C64_POKE/PEEK` and the DMA window at $D000-$DFFF with the machine **running** and `C64_DMA_MEMONLY = 0`: boot hotkeys (Ctrl-P/N/B/S/F via CIA1, `u64_config.cc:965-968`), `unlock_irq` ($D038 poke, `:1040`), SID detection (`:2267-2300`), `MENU_U64_POKE` when already stopped | While `C64_STOP` is 0 and `MEMONLY` is 0, an access to $D000-$DFFF is `dma_stub`: reads $FF, writes dropped (`ultimate_sys.vhd:914-917,945,1214-1216`); $D400-$D7FF reads are $FF by design (no SID, `:909-912`) | Boot hotkeys never work (Ctrl-P/N/B/S/F at power-on); UNLOCK poke and any other I/O write from the app while the C64 runs is lost. SID "detection" correctly finds nothing | deg | NEW (`gideon-menu-tree.md:754` lists the hotkeys as "fehlt" without the cause) | M for a real cycle-steal while running (REU-style RDY/BA path, T57's route); S for hotkeys only: app reads the matrix through MATRIX_KEYB |
| G07 | Serial bus routing (Serial Bus Mode, parallel cable, burst) | `U64_INT_CONNECTORS` 0x10100409 = `PARCABLE_ENABLE \| BURST_EN<<1 \| connector bits 4-6` (`u64_config.cc:1124-1128`: All Connected / C64<->Ultimate / DIN<->Ultimate / C64<->DIN) | Page-latch only (page 010). `iec_bus.v` has `port_off` fed solely by a JTAG register (`top.v:491,999`); CIA SP/CNT are tied high (`c64_top.v:637-639,694-696`), CIA2 PB is wired to the (absent) internal 1541 parallel cable only (`c64_bus_arbiter.v:330-333`, `c64_top.v:867-868`) | "Serial Bus Mode" is always All Connected: a drive on the DIN port cannot be separated from the C64 or from the internal drive; SpeedDOS parallel cable and burst-mode patch do nothing | deg | NEW | M (connector mux in `iec_bus.v` is S; burst needs CIA SP/CNT on the bus, L) |
| G08 | User port | `U64_USERPORT_EN` 0x10100405 (=3) and `U64_PWM_DUTY` 0x1010040D (0xD8) at every settings apply (`u64_config.cc:1097-1100`): user port power; the CIA2 port lines are the user port | Our top has no user port pins at all: Gideon's `CIA_2_PB[7:0]`, `CIA_2_PA2`, `CIA_2_PC`, `CIA_2_FLAGn`, `CIA_2_SP`, `CIA_2_CNT`, `CIA_1_SP`, `CIA_1_CNT`, `CIA_PWM[1:0]` are not in `top.v` (ports `:46-110`); the CIAs' PB/FLAG/SP/CNT are internal-only | No user-port device works (RS232/modem user-port interfaces, parallel printer cables, user-port MIDI, SpeedDOS cable); user-port power is not controlled | deg (propose M1) | NEW (T35 only "if a device comes along") | M (pins, level shifting enable, CIA PB/PA2/PC/FLAG/SP/CNT mux in `c64_top`, T20 state unaffected) |
| G09 | HDMI scan lines | `C64_SCANLINES` 0x10180000 (`u64_config.cc:1115`; item "HDMI Scan lines", default Enabled) | Latch only; `vic_scaler.v` has no line darkening (no `scan` anywhere in `designs/c64_basic`) | Picture is never line-darkened; setting does nothing | cos | NEW | S |
| G10 | HDMI resolution, DVI, cropper | `HDMI Scan Resolution` (480p/576p, 720p, 1080p, 800x600, 1024x768, 1280x1024, Auto EDID), `Digital Video Mode` (HDMI/DVI): `SetVideoMode1080p` + HDMI timing regs 0x10144000, cropper 0x10148000 (`hdmi_scan.cc:54,101,114`), `U64_HDMI_ENABLE` (`u64_config.cc:1119-1121`), external PLL blobs over I2C | HDMI timing regs, cropper and the CDCE writes are swallowed (`overlay.v:7-9`, `app_i2c.v` 0x64); the output is fixed 1080p50/60 (`top.v:1198-1280`, T40 §1 non-goal); `HDMI_ENABLE` ignored: always HDMI with data islands (`cores/icepi/hdmi`) | A monitor that cannot take 1080p (older 720p/4:3 displays, capture cards with fixed modes) gets no picture; DVI-only sinks get HDMI packets; Auto resolution never follows the EDID | deg | known as non-goal, not in TODO (T40 §1) | L (new pixel clock families, GTP rates; a 720p/576p/480p set at least) |
| G11 | Analog video | `Analog Video Mode`, `C64_PHASE_INCR` 0x10180005, `C64_VIDEOFORMAT` b2 RGB / b6 burst, 4 AVID pins (`u64_config.cc:1131-1134,1148,1161`) | No `AVID_*` ports in `top.v`; `C64_PHASE_INCR`, the RGB bit and the burst bit are latched and ignored (T40 §1, §6) | No analog/CRT video out of the C64U (composite/S-Video/RGB through the AV connector) | deg (n/a for HDMI-only users) | known as non-goal, not in TODO (T40 §1) | L |
| G12 | Drive sound, mixer sources beyond the SIDs | `setMixer` writes 10 sources x (R, L) at 0x10100500: UltiSID 1/2, sockets 1/2, sampler L/R, drive 1/2, tape read/write (`u64_config.cc:1356-1370`, items "Vol Drive 1/2", "Vol Tape Read/Write", "Vol Sampler"); drive samples at 0x00EC0000/0x00EB0000 | Only bytes 0x00-0x05 are decoded (`ultimate_sys.vhd:1111-1125`); `drive_unit`'s `audio_sample` is left open (`top.v:577` `.drive_audio()`) | No stepper/motor sound from drive A though the app loads the samples; the drive and tape volume items do nothing; socket SID levels (T37) | cos | NEW | S-M (decode bytes 0x0C-0x0F, add `drive_audio` to the SID mix at `top.v:812-820`) |
| G13 | ACIA / modem (SwiftLink) | `CAPAB_ACIA` gates `Modem` (`io/acia/modem.cc:930`), `ACIA_BASE 0x1004A000`, ITU high IRQ 0 (`itu.h:41`); "Modem Settings", SwiftLink/Turbo232 at $DE00/$DF00, telnet BBS dialling | Capability bit 27 off (`top.v:524`: `28'h5C40222`); 0x1004A000 is `r_none`; `irq_high(0)` unwired (`ultimate_sys.vhd:509`) | No Modem Settings menu, no SwiftLink emulation | deg | NEW | L (Gideon's open `fpga/io/acia`, C64 bus slot in `cart_slot`) |
| G14 | Sampler / Ultimate Audio Cartridge | `CAPAB_SAMPLER`: `C64_SAMPLER_ENABLE` (+0E, "Map Ultimate Audio $DF20-DFFF"), `SAMPLER_BASE 0x10048000`, sampler playback/record REST (`api/route_runners.cc:243,274`), .mod player from REU (`filetypes/filetype_reu.cc:41`) | Capability bit 21 off; register +0E and 0x10048000 not served. **Built on branch t25-sampler (T25.6, 2026-10-07, simulated, no board): bit 21 on, +0E, 0x10048000, $DF20-$DFFF, 8 voices, Sampler L/R mixer inputs. The two REST calls are MOD playback, there is no record function in the app.** | No sampler runners, no MOD playback, the $DF20 audio-cartridge map is missing (until T25.6 is integrated) | deg | NEW | L |
| G15 | UDP streams 2 (bus) and 3 (IEC) | `U64_ETHSTREAM_ENA` bits 2 and 3, header templates at 0x10190000 + 64*id (`io/network/data_streamer.cc:316-320,404-408`) | Only `stream_ena[0]` (VIC) and `[1]` (audio) enabled senders exist (`top.v:1030,1039`); the header write path is served (`ultimate_sys.vhd` r_udp) | "Debug stream" and IEC stream give no packets (external bus/IEC loggers) | cos | NEW (TRX64 monitor/ring covers the use) | M |
| G16 | LED strip driven from the C64 | `C64_VIC_SPLIT` 0x1018002F = 1 in strip mode "Programmatic": C64 writes to $D200-$D2FF reach the strip (`u64/led_strip.cc:423-432`) | Page latch; no snoop of the C64 write in `led_engine`/`bling_io.v` | Programmatic LED mode stays dark | cos | NEW | S |
| G17 | Debug register $D7FF | `U64_DEBUG_REGISTER` 0x10181800 = the C64's $D7FF; REST `machine:debugreg` (`api/route_machine.cc:495-540`), socket `SOCKET_CMD_DEBUG_REG` (`socket_dma.cc:300-306`); VICE test programs report their verdict there | Page 101 latch (`ultimate_sys.vhd:1106`): a C64 write to $D7FF is open I/O and never reaches it | `debugreg` returns what the app last wrote, not the C64's value: VICE testprogs and any REST test harness that polls it see nothing (our own tests read the VIC shadow instead, `cart-vice-baseline.md`) | cos | NEW | S |
| G18 | ITU high interrupts | HDMI HPD (5), UNLOCK (6), GURU (7), ACIA (0), WD177x (1/2) (`itu.h:41-47`); `hpd_monitor_irq`, `unlock_irq` (`u64_config.cc:997-1000,1020-1046`) | `irq_high` drives 2 (TRX IO), 3 (WiFi), 4 (Bling) only (`ultimate_sys.vhd:509`); `irq_in(3)` TAPE unwired (`:537`) | Monitor hot-plug is never noticed (EDID re-read is moot at fixed 1080p); no UNLOCK hook (our UCI is on by setting, T28); the rest follows G13/G07/T56/T58 | cos | NEW (F10 in ARCH-lean states the lines, not the effect) | S |
| G19 | Pins the app never names but Gideon's core owns | n/a (no register) | `AUDIO_SPDIF`, `AUDIO_SDO` (socket SID audio, T37), `ETH_LED_YELn`, `WIFI_BOOT`, `WIFI_DIS`, `IEC_RESET_I`, `IEC_SRQ_I`, `RMII_IRQn`, `RMII_RX_ER`, `BOARD_REVn`, `UI_BUTTON[2:0]` are in the issue-615 XDC and not in `top.v` | Ethernet link LED off, no S/PDIF out (if the C64U has the connector), SRQ/RESET from the bus not seen by us, rocker only through the ESP32 (works, T04) | cos | NEW (measure what Gideon's core drives on SPDIF/ETH LED before any build) | S (survey), then per pin |
| G20 | Capability word | `getFpgaCapabilities()` gates features (`itu.h:49-79`) | Ours is `0x05C40222` + type: bits set UCI, EEPROM, USB2, ETH, U64, CART, HARDWARE_IEC, DRIVE_1541_1; clear: DRIVE_1541_2, DRIVE_SOUND, HARDWARE_GCR, C2N_STREAMER/RECORDER, RAM_EXPANSION, MM_DRIVE, RTC_*, EXTENDED_REU, STEREO_SID, COPPER, SAMPLER, ACIA, BUS_MEASURE | Each clear bit hides or disables its menu/REST/UI part: tape (T58), drive B and 1571/1581 (T56), sampler (G14), modem (G13), bus measure (`route_machine.cc:640`, U2-era, n/a), Copper (T25.2 dropped). The real U64-II word is not known to us (ue2emu uses 0x34000226, which also has DRIVE_1541_2) | deg (summary row) | see G13, G14, T56, T58 | - |
| G21 | IEC processor / SoftIEC | `CAPAB_HARDWARE_IEC` is set, so `IecInterface` loads microcode into 0x10028000+0x800 and runs the server (`io/iec/iec_interface.cc:12-72`; `iec_drive.cc` "SoftIEC Drive", `iec_ulticopy.cc`, MPS printer `mps_printer.cc:1807`, UCI SoftIEC target `command_interface/softiec_target.cc`) | `r_iec` is a stub: version 0x25 and two empty FIFO flags (`ultimate_sys.vhd:787,1204-1210`); there is no processor behind it | "IEC Drive" (device 11 on SD/USB), the printer, UltiCopy and the UCI softiec target cannot work; the app starts the server task against a dead block (spec §4.9 notes the stub avoids a busy poll) | deg | NEW (T56 is the 1541/1571/1581 drives only) | L (Gideon's open `fpga/io/iec_interface`, CIA-side wiring) |

## 2. Known and tracked (listed so they are not re-found)

| Area | Detail | TODO |
|------|--------|------|
| Custom palettes | `C64_PALETTE` 0x10180800/0x10180C00, `set_palette_rgb` (`u64_config.cc:2764-2835`); `vic_scaler.v:168` Colodore only; HDMI palette at 0x145000 is served for the overlay | T59 |
| REU size 16 MB | `top.v:315` `reu_cmd_addr[20:0]`, `c64_reu.v` 22-bit address | T60 (G01/G02 add path and enable) |
| Tape port | `C2N_*` pins absent; `c64_core.v:463` ties `tape_*` off; CAPAB_C2N_* clear; `irq_in(3)` | T58, T35 |
| trex_crt, DMA to $00/$01, ROM_WR_EN | DMA write to $00/$01 does not reach the 6510 port; `SLOT_CTRL.ROM_WR_EN` never set | T57 (+T16 Q8) |
| Real expansion port | `C64_BUS_BRIDGE` 0x1018002A is a latch with "any external bit drives every cycle" (T16 §13), timing registers are U2-only on `U64`, freeze button into AR/RR (`cart_slot.v:195`, baseline D6), AR/RR deviations D1-D9 | T16, T06 |
| SID sockets | `C64_SID1/2_*`, `STEREO_ADDRSEL` (0x14), `SLOT_SID1n/2n` tied high (`top.v:125-126`), `AUDIO_SDO` | T37 |
| Paddles, mouse, WASD, swap | `C64_PADDLE_*` 0x10180032-37, `C64_MOUSE_EN`, `MATRIX_WASD_TO_JOY`, `U64II_KEYB_JOY` read is 0xFF (the overlay menu is not joystick-navigable), swap, `C64_CLOCK_DETECT` b5 NMI sense, `PADDLE_*` pins | T47 |
| UltiSID / filters | `C64_EMUSID*` served; curves, digi, split | T25 (engine with TRX_SID=1; Copper dropped, T25.2) |
| Turbo | `C64_TURBOREGS_EN`, `SPEED_PREFER/UPDATE`, $D030/$D031/$D07A/$D0BC | T29 |
| System Mode / NTSC | `C64_VIDEOFORMAT`, PLL over I2C 0x64 and MMCM DRP are swallowed and replaced | T40 (done) |
| Drives | drive A is `c1541_drive` only; no drive B, 1571/1581 (`mm_drive`); CAPAB_DRIVE_1541_2/MM_DRIVE clear; WD177x irq lines | T56 |
| Freeze | shadow I/O session, CAPS2, `C64_STOP_MODE` always immediate | T12 |
| UCI | served from Gideon's VHDL; `$DF1C`/`$DE1C`/`$DFFC` slot, interrupt `irq_in(4)` | T28 (done) |
| Flash, update, reboot | `W25Q_Flash::reboot` uses ICAP (`w25q_flash.cc:370`, ICAP 0x10060600: +0x04/+0x08 are inert on purpose since the T79 ICAP loader, which owns +0x10..; no Spartan warm boot here): `MENU_C64_HARD_BOOT` and the end of an app update do nothing; decided: power off | T49, T50, T44 |
| ESP32 module control | `ModuleCtrl` boot/run/off (`dma_uart.cc:107`) goes to `enable/route => open` (`ultimate_sys.vhd:500-501`); `WIFI_BOOT/DIS` not driven | T26 (by design) |
| Keyboard / Bling / LEDs | matrix, SHIFT LOCK, case LEDs, keyboard LEDs | T09, T08, T38 (done) |
| Ring / monitor | state reads, lenses | T20, T21 |

## 3. Served and checked, no gap

ITU (timers, IRQ regs, buttons through the ESP32 events, console UART), SPI flash (T44 gate), SD, RMII MAC, USB nano and hub/ULPI resets, hardware I2C (shared with our scanner), overlay RAM and registers, HDMI HPD/DDC register, BOARDREV 0xB8, speaker enable and mixer bytes 0-5, UltiSID bases/masks/enables, SID voice levels, ETHSTREAM header templates, WiFi DMA UART (identify, power, rocker), MATRIX_KEYB, KEYB_COL/ROW, CART_DETECT (pins), case LED selector (all 16 sources, `case_leds.v`), cartridge registers 0x10040000-06, DMA window while stopped (PLA cycle) or MEMONLY (RAM port), ROM apertures 0x10188000-0x1018C000 (read/write), UCI. Not applicable on the C64U: `C64_PLD_*` (U64 v1), `U64_POWER_REG` (U64 == 1, power goes through the ESP32, `c64_subsys.cc:610-618`), `DDR2_*`/`LATTICE_*`, `REMOTE_*`, `U2PL_*`, `AUDIO_DMA_*`, `U64_CLOCK_FREQ`, `C64_GLYPH_*`, `C64_VIC_TEST` (DEVELOPER), `C64_PHI2_EDGE_RECOVER`/`C64_TIMING_ADDR_VALID` (written constant on `U64`, `c64.cc:432-435`), "Button order" (U2 only).

## 4. Counts

New: 21 rows (G01-G21). M1 drop-in blocker 3 (G01, G02, G03); degraded 11 (G04, G05, G06, G07, G08, G10, G11, G13, G14, G20, G21); cosmetic 7 (G09, G12, G15, G16, G17, G18, G19). Proposed to pull into M1 on top of the three: G04 (safe mode), G05 (RTC). Known and tracked: 17 areas in §2 (T59, T60, T58, T57, T16, T37, T47, T25, T29, T40, T56, T12, T28, T49/T50, T26, T09/T08/T38, T20/T21).

## 5. Proposed TODO rows (for review; TODO.md not edited)

M1:
- **REU path and enable** (G01 + G02, folded into T60): REU memory at the app's 0x01000000 (16 MB, spec §3a), `REU_ENABLE` into `flags[12]`, `REU_SIZE` as an address mask; check T51 and T21's "above 64 MB" before moving it. Test: load a .reu from the menu, read it from the C64 with the REU disabled/enabled.
- **Cartridge types the app accepts and we refuse** (G03): decide the list (Final III, KCS, Final I, Super Games, Westermann, Zaxxon, SS5, Pagefox, Blackbox, GeoRAM at least), descriptor where PROP-002 can express it, dedicated model where not (freezers).
- **Safe mode from RESTORE at power-on** (G04): S; recovery for testers (T49/T50 flow).
- **RTC seconds counter** (G05): S; correct file times for every tester's SD card.

Later or on request:
- DMA through the bus while running (G06; or hotkeys through MATRIX_KEYB), serial bus mode registers (G07), user port with pins and CIA lines (G08), IEC processor / SoftIEC (G21), ACIA (G13), sampler (G14), drive sound mix (G12), scanlines (G09), LED $D200 (G16), $D7FF debug register (G17), streams 2/3 (G15), HDMI hot-plug IRQ and UNLOCK (G18), pin survey (G19).
- Decide, not build: HDMI resolutions and DVI (G10), analog video (G11): both are T40 non-goals today; whether "drop-in" includes them is Alex's call.

## Board checks on Gideon's core (2026-10-04, flashed image 3.15 / core 1.50)

- **G05 (RTC):** a file written over FTP got "Jan 01 01:00" on Gideon's core as well (SNTP had evidently not set the time on that boot). The code settles it: on the U64-II/C64U build the app links `rtc_dummy.cc` (target/u64ii/riscv/ultimate/Makefile:72); SNTP's `sntp_time_received` (network/sntp_time.cc) calls `rtc.set_time_utc(sec)`, which writes the FPGA seconds counter `RTC_TIMER_SECONDS` (0x10060400), and every time read (`Rtc::get_time`, FAT timestamps via `get_fattime`) reads that counter back. Our core does not decode 0x10060400, so even with SNTP working the time is lost: **G05 stays a gap** (size S: a free-running seconds counter with a write port).
- **G06 (DMA to I/O while running):** confirmed as a gap. Gideon: `machine:readmem` at $D020/$D021 returns real values ($FE/$F6) and a write of $02 to $D020 reads back $F2 while the C64 runs.
- **G02 (REU disabled):** inconclusive. With "RAM Expansion Unit" Disabled (after a reset), `readmem` $DF00-$DF0A returned the same bytes as with it Enabled (16 MB). Needs a C64-side probe (a PRG reading $DF00 itself) to tell whether the REU really disappears.

## 6. Decisions (Alex, 2026-10-04)

| Gap | Decision |
|-----|----------|
| G01 | M1, T60: one 16 MB expansion-port block at DDR2 0x01000000 shared by REU and cartridge ROM; Run-PRG boot cart (8 KB) in its own slot outside the block; 0x03C00000 and 0x04000000 freed for the ring (built 2026-10-06, branch t60-shared, spec T60 §7: boot cart slot 0x00F00000, trx_io ROM_SEL) |
| G02 | M1, T60/T62: REU enable and size as on Gideon's core |
| G03 | Not supported. Documented for the RC only (no app message): Final Cartridge III, Final I, Super Snapshot 5, KCS Power, Atomic/Nordic Power, Epyx Fastload, Westermann, COMAL 80, EXOS, Pagefox, Kingsoft Business Basic, Blackbox V3/V4/V8/V9, Super Games, Zaxxon, GeoRAM |
| G04 | M1, T62 |
| G05 | M1, T61 (software clock) |
| G06 | M1, T63: real cycle steal; build measures LUT/timing |
| G07 | Not supported (serial bus mode, parallel cable, burst): use Gideon's firmware |
| G08 | Not in M1 (user port), later, no milestone |
| G09 | M1, T62, only if build and placement allow |
| G10 | Not in M1 (HDMI resolutions, DVI) |
| G11 | Not in M1 (analog video) |
| G12 | Not supported (drive sound, mixer sources) |
| G13 | Not in M1 (ACIA/modem) |
| G14 | With T25: clean-room UltiSID replacement + sampler built complete on an isolated branch, then fit check |
| G15 | Not supported: the TRX ring replaces the debug/IEC UDP streams |
| G16 | Not supported (LED strip programmatic) |
| G17 | M1, T62 |
| G18 | Left out for now (HPD/UNLOCK/GURU interrupts) |
| G19 | M1, T62: Ethernet link LED and IEC RESET input; the other pins left out |
| G20 | Follows from the rows above |
| G21 | Not in M1 (no IEC processor); clear CAPAB_HARDWARE_IEC so the dead menus disappear (T62) |
