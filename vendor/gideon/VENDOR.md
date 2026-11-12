# 1541ultimate (Gideon Zweijtzer), vendored parts

Our own copy (T120, Alex 2026-11-01), kept in git, no submodule behind it. Made by `tools/gideon_sources.sh <1541ultimate checkout>` (tools/vendor_gideon.sh, tools/vendor_gideon_app.sh). Do not edit here: renew the copy from upstream or add a patch in tools/patches/gideon.

| repository | commit |
|---|---|
| 1541ultimate | 32e5fd5f759e559c386ff711250e732964c6062d |

Licence: GPL-3.0 (LICENSE.txt); no file here carries the restricted clause.
`bootloader_u64ii.c` is kept as the reference for `host/bootrom/`.
The char_generator files and font_pkg are the reference for host/overlay.v (fonts via tools/font2mem.py).
`nano_minimal.nan` (the USB nano CPU program the app loads) and `parse_nano.py` are kept for the USB simulation (host/sim/usb).
dpram_8x16/dpram_8x32 come as entities only; host/usb_dpram.vhd has the architectures.
fpga/6502n: the entities carry the prefix drv_ (the C64 core has the same ones; see tools/vendor_gideon.sh).
fpga/ip/memory pseudo_dpram_8x32: only the entity; host/pseudo_dpram_8x32_x7.vhd is its architecture.
fpga/io/command_interface: the UCI (T28, host/uci_unit.vhd), with dpram, slot_bus_pkg, io_bus_bridge2 and synchronizer_gzw.
Patches applied from tools/patches/gideon:
- 0001-c1541-drive-debug-status.patch
- 0002-floppy-head-on-track-18.patch
- 0003-eth-transmit-store-and-forward.patch
- 0004-drive-cpu-sync.patch
- 0005-rvlite-dbg-pc.patch
- 0006-rvlite-mul-pipeline.patch
- 0007-rvlite-mul-hold-only-for-a-multiply.patch

Software (tools/vendor_gideon_app.sh; the list is tools/gideon_app.list, the third-party trees come whole):

| repository | commit |
|---|---|
| software/lwip (own repository in the checkout) | 26a22151f4b7ebb2925523192edc83fa7f31cba9 |
| software/httpd (own repository in the checkout) | ad73d3217bb0e1b3f37e0dbba595309b942aea17 |

software/, tools/*.c and Makefile, target/, roms/: what host-app/ and coreapp/ compile (host-app/target Makefiles, ci/trxapps.sh), the update_trx updater
(tools/make_update.sh) and the host tests (tests/hostapp, tests/coreapp) read. host-app/software/ holds the patched copies of the first-party files
(tools/hostapp_vendor.py, host-app/patches) made from here. software/lvgl (MIT), software/lwip (BSD), software/FreeRTOS (MIT), software/chan_fat
(FatFs licence) and software/httpd keep their own licences next to their files; the rest is GPL-3.0 (LICENSE.txt). No software file carries the restricted clause.

Changed after copying (tools/patches/gideon-app, applied by the script): software/io/wifi/{wifi,wifi_cmd}.{cc,h}, software/io/network/network_esp32.{cc,h},
software/u64ctrl/main/rpc_calls.h, and the new software/io/wifi/{wifi_reply,wifi_scan_result,wifi_connection_feedback}.h - T184 D4, the driver fixes of
https://github.com/omnicron-c64/ultimate-community-edition at f7f665ab (GPL-3.0, like this tree); each changed file says so in its first line.
