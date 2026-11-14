# T84 host boot app: build, format, board test

**Status:** built, merged to `main` with the T94 layout (`host-app/`), builds in the container and **runs on the board**: flashed at 0x220000, and `vc20.cor` started from its boot menu through the ICAP without JTAG (2026-10-12, board run 3 in `docs/specs/done/T83-vc20-core.md`). Open: the colours (blue tint, pixelated font).
Spec of the boot chain: `docs/specs/done/T84-boot-chain.md` (integ branch). Mockup: `.claude/trx64-site/bootmenu-mock.html`.

## What it is

`software/hostapp` is a standalone RISC-V application (rvlite, FreeRTOS) that loads at 0x30000 like the
Ultimate app (`tools/trxdbg.sh riscv hostapp.bin` and the boot ROM start it the same way). It holds FreeRTOS,
the USB host with mass storage, SD + FatFS, the file system layer, lwIP with the RMII MAC, the FTP daemon and
LVGL; no C64 part (no C64 subsystem, cartridges, SID, drive, ring, monitor, REST).

- `tools/build_hostapp.sh` builds in `u64-build-env` (nice -n 15, `JOBS=2`): `software/hostapp/target/u64ii/riscv/hostapp/result/hostapp.{bin,elf}`.
- `tools/hostapp_vendor.py` copies the GPL files from the submodule (`vendor.list`) and writes `software/hostapp/PROVENANCE.md`.
  Third-party libraries (lwip, lvgl, FreeRTOS, chan_fat, httpd) are symlinks to the submodule's pinned copies.
- Own code: `software/hostapp/software/hostapp/` (`hostapp.cc`, `hostui.cc`, `coreboot.cc`, `cor.cc`, `bootcfg.cc`, `hostfs.cc`, `sha256.c`, `lv_conf.h`).
- Tests: `tests/hostapp/run.sh` (the .cor reader against `cor_pack.py` bundles, boot.cfg; native), `tools/test_cor_pack.py`; both in `ci/lint.sh`.

## .cor

Spec: docstring of `tools/cor_pack.py`. `TRXCOR01`, u32 length, records (u32 type, u32 length, value): manifest (flat JSON, sorted
keys: name, version, contract, fpga, partial/app/icon size and SHA-256, app_load, app_entry), partial (configuration data,
the .bit header stripped), application image, optional 32x32 RGB565 icon. Contract version of this host: 1 (`cor.h`).

## UI (LVGL on the pixel layer, 960x540 at scale 2)

F1 boot menu (list, badges, 5 s countdown on the default, any key stops it), F3 Add core (media, only folders and .cor, manifest
preview, RETURN adds, S starts once), F5 edit menu (+/- move, D default, DEL remove, LEFT/RIGHT countdown), F7 network
(IP, FTP, media). Menu file: `/Flash/boot.cfg` (`bootcfg.h`). Colours from trex64-dev.github.io. Not done: the Sixtyfour display font
(Montserrat 24 instead; the font is not in the tree and the converter is not run on the host), REST
(`GET /v1/cores`), the hash of all three parts in the browser preview (checked on add/start instead).

Since T101 ([T101-host-app-ui](T101-host-app-ui.md)): the menu also lists the `.cor` files the scan found on the media
(`discover.cc`: every root and its `cores` folder), A adds a found one, R rescans; menu and editor scroll (5 rows);
the bundle's icon is drawn; DEL in the editor asks twice; badges read "hash ok", "found", "contract N != M".

## Start of a core (`coreboot.cc`)

1. stage: header, contract, range check, partial -> DDR2 0x03400000, application -> 0x03000000, SHA-256 of each part while reading (no hardware touched);
2. `ICAP_DC=1` (0x10060620), wait, check bit 1; 3. `ICAP_EN=1`, bytes to 0x10060610, word count (+0x18) must equal size/4, the decouple stays set;
4. interrupts off, pixel layer off, RMII RX off; the copy routine is relocated to 0x03F00000 (its target is where this program lives), copies the
application to `app_load`, 2 KiB of NOPs refill the instruction cache, jump to `app_entry`. The application releases the decouple and resets its core.

## Board test (Alex)

1. Load the dfx-trial bitstream with the ICAP loader into SRAM: `tools/load.sh <file.bit>` (as for icap_test).
2. `tools/trxdbg.sh riscv software/hostapp/target/u64ii/riscv/hostapp/result/hostapp.bin follow`.
3. Expected console: `*** TRX64 Ultimate host boot app <hash> ***`, init functions, `TRX IO block caps ..`, `All linked modules have been initialized`,
   `[host] no /Flash/boot.cfg yet` (or the entry count), and, with the pixel layer in the bitstream, `[TRX UI] LVGL ...`. A screen with the boot menu appears on HDMI
   (video timing is set by the app like the Ultimate app does for PAL 1080p; **unverified**: the host's scaler may need the core's frame to show the layer).
4. USB keyboard: F3, pick the stick's folder, RETURN on a .cor adds it; F1, RETURN starts it.
5. Make a bundle: `tools/cor_pack.py pack -o vc20.cor --name VC-20 --version 0.1 --partial core.bit --app ultimate.bin --load 0x30000`; `tools/cor_pack.py check vc20.cor --contract 1`.

## Changes after the first board run (2026-10-12)

- Keyboard: the C64U's own keyboard is read through Gideon's `Keyboard_C64` on `U64II_KEYB_ROW/COL/JOY` (host object answering
  "accessible", nothing of the C64 side); USB keyboard as before; PageUp/Help/Tasks/PageDown are taken as F1/F3/F5/F7.
- Chips are sized from the measured text width.
- Colours: LVGL's RGB565 output was checked natively with the app's lv_conf (#070a12 -> 0x0042, #0d1220 -> 0x0884, as the layer expects);
  the console now prints those pixels from LVGL's buffer and the layer registers at start (`[host] pixels: ...`, `[host] layer: ...`).
- Speed: bundle hash is checked in full once, when the core is added (all three parts); boot.cfg stores the partial hash and the file size
  (`core=path|name|sha|size`) and a start whose file still has both only reads the parts. The ICAP loop is word-load + four byte stores,
  unrolled, progress per 256 KiB. The log prints `stage: N ms total, read N ms, hash N ms` and `ICAP: N bytes in N ms = N KiB/s`.
- Start screen is built once on LVGL's top layer; progress only changes labels/bar, rendered at most every 100 ms.

## Decision (Alex, 2026-10-12): check a core only when it is added

"Check the core == only when adding it" (original: "Core prüfen == nur beim adden"). The full SHA-256 check of all three parts
happens once, when a bundle is added to the boot menu; `boot.cfg` keeps the partial's hash and the file size
(`core=path|name|sha|size`), and a start whose file still has that size and manifest hash only reads the parts. A damaged
part with the same size and manifest is therefore not caught at start; that is accepted.

## Second board run (85acf2b) and what changed after it

- Timings read 0 ms because the CPU's `mcycle` does not count in this core; they now use the ITU millisecond counter (`getMsTimer`, unwrapped).
- The layer registers are write-only, so the readback was meaningless; it is gone. The pixel readback reads LVGL's first buffer (`buf_1`), not the active one.
- boot.cfg is retried every second until it loads (the Flash disk may not be mounted when the UI starts) unless the user already edited the menu.
- Colour test screen: F7, then C. Swatches of known RGB values to read the real transfer function off a photo (the header and nav came out
  strongly blue while LVGL's buffer holds the right RGB565 values; the cause is not found, see the report).
- `tests/hostapp/render/run.sh [outdir]` renders every screen natively with the real `hostui.cc` to PNG (2x nearest, like the layer).

## Decision (Alex, 2026-10-12): RUN/STOP aborts the start

"RUN/STOP should abort the start" (original: "RunStop sollte das starten abbrechen!"). RUN/STOP on the C64U matrix (`KEY_BREAK`) and
Esc on USB stop the countdown (like any key) and abort a running start as long as the jump has not happened:
- while the bundle is read and checked, and before the old core is cut off: nothing is touched;
- after the cut-off but before the first ICAP byte: the decouple is released again, the old core keeps running;
- during the ICAP stream the request is only remembered (a half-written configuration is worse than a finished one); when the stream is
  complete the app does not jump and says "the core is loaded but not started" (the core stays cut off; power cycle or pick again);
- the last check is right before the jump (after the 200 ms the screen needs to show the last step).
Keys are polled at most every 50 ms and once per progress call (the ICAP loop reports per 256 KiB).

## Keyboard scan and layer proof (board run of 28bfd65)

- The C64U matrix was scanned only inside `getch()` once per UI loop, so short taps were lost. `keyboard_c64.h` (a modified copy, patch in
  `software/hostapp/patches/`, listed as MODIFIED in `PROVENANCE.md`) now enables Gideon's timer scan (every 2 ticks = 10 ms, FreeRTOS timer task)
  for `-DTRX_HOSTAPP`; `getch()` only drains the buffer.
- Colour screen (F7, then C) has proof patterns: `r g b k` fill the whole layer with one solid colour, `v` / `h` draw 1-pixel stripes. A solid colour
  that shows a mesh, a ghost or another colour is the layer, the blend or what lies below it, not LVGL or the fonts.

## Key latency (board run of 230c8ca: "mega delayed")

- A key no longer rebuilds the view: UP/DOWN in the menu and editor restyle two rows, in the browser the highlight bar moves and only the manifest
  panel is refilled; any other key rebuilds. All queued keys are drained first (`Keyboard_C64::getch_nowait()`, no 4-tick sleep per key; a held key's
  repeats collapse into one move), then one update and `lv_refr_now()`; LVGL's refresh period is 10 ms; LVGL is built -O2.
- Console, per key batch: `[host] key XX (N queued): update U ms, render+flush R ms, key to screen T ms, area P px`; per slow render
  (>= 5 ms): `[host] render N ms, P px invalid`. Target: key to screen under 50 ms for a selection move. Native (x86, `tests/hostapp/render/run.sh`):
  a selection move is about 8x cheaper than a full rebuild; the RISC-V numbers come from the board log.
- The colour test screen ends only on Esc, RUN/STOP or RETURN (the auto-repeat of the C that opened it used to end it), and the once-per-second block no longer redraws it.

## Layer proof results and the double-buffer check (board run of 230c8ca)

- Solid red, black and 1-pixel stripes are clean on the board: layer, blend, bit widening and HDMI are fine for what those write. The grid
  inside glyph strokes is most likely the monitor's pixel grid at 2x nearest, the halo is LVGL's anti-aliasing at 2x: "pixelated" is the 960x540
  resolution, not a bug.
- Open: the wrong (blue) colours of header / nav / footer. Hypothesis: stale content in one of the two buffers the layer flips between.
  Checked natively (`tests/hostapp/render`, LVGL DIRECT with two buffers and the port's flush rule): after 40 incremental updates the shown buffer
  equals a full redraw in every step, so LVGL's own sync is consistent when the flush completes at once.
- Colour screen, new keys: `p` prints both buffers at known points, how many of the 518,400 pixels differ between them (idle: 0 expected) and
  which buffer LVGL draws into next; `x` makes LVGL render into ONE buffer (no stale second buffer possible), `X` back to two.

## Key lag 1.5 s (board run of 2de45bd)

Console: `update 1500 ms` per key, constant. Causes found in the code: (1) with one core in the menu UP/DOWN change nothing, and the
old rule "anything that is not a selection move rebuilds the view" rebuilt it for every such key; (2) every rebuild did directory listings of the
mount points and the interface list for the header chips (I/O inside `frame()`). Now: a key that changes nothing does nothing (fingerprint of the
state before and after), the chips and the network page read a cached status that the main loop refreshes every 3 s and not while keys arrive,
and `draw()` logs `build X ms, handler Y ms`; `status: roots A ms, network B ms, media list C ms` is logged when a refresh takes 20 ms or more.
The stray `@` on the console is Gideon's USB input task printing one `@` per 5 s without events (`usb_base.cc`), not an error.
The firmware's printf has no `%%`: the percent signs are no longer produced through a format.

## Resident UI (board run of ded6257: build 628 ms, full render 910 ms)

The host CPU has no data cache (T24-ui-project-review.md 2.3: ~0.4 us per DDR2 load, stores posted), so object creation, style lookup and layout cost
hundreds of ms. Rules applied: the shell (header, nav, footer) and the five views (menu, browser, editor, network, colour test) and the start panel are
built once and kept; a view switch is the hidden flag plus an update of the labels that changed (`set_txt`/`set_col`/`set_hidden` compare first);
lists are fixed row objects (6 rows for menu and editor, a window of 13 for the browser) refilled, not recreated; the header chips are rebuilt only when
their text changed; a selection move restyles two rows (menu, editor) or moves the highlight bar and refills the manifest panel (browser); the start
panel is a solid 600x270 panel on the top layer (no full-screen dimmer: a blend reads the destination); no animations, shadows, gradients or opacity.
LVGL renders into ONE buffer by default now (no sync copy of the previous frame, which reads DDR2); `X` on the colour screen goes back to two.
No block RAM or SRAM region is documented or mapped for the app (the app image and all LVGL data live in DDR2), so nothing was placed there.
The 3 s status refresh no longer redraws: it logs `[host] status change: <field> 'old' -> 'new'` for each changed field (usb, sd, link, ip, media) and only
rebuilds the chips and, on the network page, its labels.

## Decision (Alex, 2026-10-14): running an update belongs in the host app, later

Running the `.cfw`/`.ue2` of a multi-core bundle becomes a function of the host app; the updater is left
as it is for now. Alex, translated: "I think we should build running the update for MultiCore into the
HostApp later. For now please leave it as it is."

Why it came up, measured on the board the same day (run-406 host, Amiga core in layer 2, the `.cfw` of
`trx64mc-main-9eeedabb` started from the Amiga core's application): the application did its part - the
console shows `Creating Update type from info: trx64host_update_9eeedabb_20261014.cfw`, `Action set was:
Run Update`, `Update Load..` and the updater's own title - and then the updater reached for a C64.
`update_u2p/update_common.h` takes `C64 *c64 = C64::getMachine()`, accepts it when `c64->exists()` and
uses `host->getScreen()`; with a core that is not a C64 the register bus answers but nothing is behind
it, so the updater took over a machine that is not there (`[C64] goUltimax() -> C64_MODE = ULTIMAX`,
twice), held it in reset and drew into a screen that does not exist: black picture, no UI, the RISC-V
spinning between 0x03002FA0 and 0x03002FA4. Nothing was written: the writing phases print
`Writing <what> (N sectors)` and `sector n/N`, and no such line is in the console ring.

Recovered without a power cycle: `tools/trxdbg.sh boot flash` (CPU in reset, boot ROM request 1) started
the app in flash again - boot ROM 1.2 s, stamp 6D34FDD1 equal to the flash bitstream's, the menu's
countdown ran out and loaded `/USB1/cores/trx64.cor` through the ICAP (1,425,560 bytes in 239 ms), the
network came back. The flash still holds the run-406 host and the app of T84.

What the host-app function has to do differently than Gideon's updater: no C64, no machine takeover, no
C64 screen. It has the LVGL UI of the host, it knows the ICAP and the flash gate, and it can read the
same package (`trx_pkg.py` layout, `trx_manifest.h`) out of a `.cfw`. The probe for a core is in the
code already: `update_trx.cc` reads `TRX_ID` and compares it with `'TRX2'`.
