# Licensing

Our own code is under the **GNU General Public License v3.0 or later** (`LICENSE`). The CERN-OHL-S v2 parts by
Dag Lem keep their licence; our GPL code is marked as **"Available Components"** in the sense of CERN-OHL-S v2 §1.2
with respect to them. Dag Lem wrote to us on 2026-10-06: "I have no objections to you using my work, as long as the
(modified) work itself stays open and under the same license"; he does not dual-license.

Origin, licence as the files state it and our changes, per block: [PROVENANCE.md](PROVENANCE.md).

| Part | Where | Licence | Note |
|---|---|---|---|
| TRX64 Ultimate gateware, tools, CI, docs (ours) | everything not listed below | GPL-3.0-or-later | Available Components for the CERN-OHL-S parts |
| reDIP-SID, reDIP-CIA (incl. VIA) by Dag Lem | `vendor/icepi/c64_redip_sid/reDIP-SID`, `vendor/icepi/c64_redip_cia/reDIP-CIA` | CERN-OHL-S-2.0 | modifications stay open under CERN-OHL-S-2.0 |
| icepi-zero C64 core (Mateusz Nalewajski) | `vendor/icepi` (except the subfolders below) | GPL-3.0-or-later | |
| VIC-II Kawari | `vendor/icepi/vicii-kawari` | GPL-3.0 | |
| hdl-util HDMI | `vendor/icepi/hdmi` | MIT or Apache-2.0 | |
| usb_hid_host | `vendor/icepi/usb_hid_host` | Apache-2.0 | |
| Gideon's 1541 Ultimate FPGA sources and the software of his application that our applications compile | `vendor/gideon` (our own copy, in git; commit in `vendor/gideon/VENDOR.md`), `cores/c64u/gideon` | GPL-3.0 | FreeRTOS, lwIP, LVGL, FatFS, httpd inside it keep their own licences next to their files |
| Host boot app: copies of Gideon's GPL files, our own code | `host-app` | GPL-3.0 (copies keep their headers; origin of every copy in `host-app/PROVENANCE.md`) | FreeRTOS, lwIP, LVGL, FatFS are linked from the copies in `vendor/gideon/software` under their own licences |
| Vendored cores (upstream trees, copied unchanged where the NOTICE says so) | `cores/<name>/rtl` | the licence the upstream files state, per core below; `NOTICE` and the licence texts (`COPYING`, `LICENSE`) are in each `rtl/` | our glue around each core (`layer2_<name>.v`, `trx/`, `gen/` generators) is GPL-3.0-or-later |
| NES (NES_MiSTer) | `cores/nes/rtl` | GPL-3.0 | T65 BSD-style |
| ZX Spectrum (ZX-Spectrum_MiSTer) | `cores/zx/rtl` | GPL-3.0 or later; YM2149 and T80 BSD-style | |
| ZX Spectrum Next | `cores/zxnext/rtl` | GPL-3.0 or later | |
| Super Nintendo (SNES_MiSTer) | `cores/snes/rtl` | GPL-3.0 by the repository's `LICENSE` (the files carry no header) | the S-SMP boot ROM is cut out, see NOTICE |
| Neo Geo (NeoGeo_MiSTer) | `cores/neogeo/rtl` | GPL-2.0 or later (furrtek files), GPL-3.0 (FX68K, jt10, jt49); T80 BSD-style | see NOTICE |
| Amstrad CPC (Amstrad_MiSTer) | `cores/cpc/rtl` | GPL-2.0 or later | two files without licence text, see NOTICE |
| C16 / Plus 4 (C16Nano) | `cores/c264/rtl` | GPL-3.0 or later; T65 BSD-style | |
| Amiga 500 / 1200 (NanoMig) | `cores/amiga/rtl`, `cores/amiga1200/rtl` | mostly GPL-3.0 or later; FX68K GPL-3.0; TG68K LGPL-3.0 or later | files without licence text, see NOTICE |
| Apple //e (NanoApple2) | `cores/apple2/rtl` | GPL-3.0 as declared by the repository; credit lines only in several files | see NOTICE |
| VIC-20 (VIC20Nano) | `cores/vc20/rtl` | GPL-3.0 as a repository; three files carry "Replay hardware only" headers; T65 BSD-style | see NOTICE |
| TI-99/4A (zeldin/Mega99) | `cores/ti99/rtl` | LGPL-3.0 (files carry their own headers); the console ROM and GROMs are never in the repository | see NOTICE |
| Atari 800 XL (Atari800_MiSTer) | `cores/atari800/rtl` | upstream headers: non-commercial use, contact the author for other use; three LGPL files | see NOTICE |

Distribution: the sources of every part go with a bitstream or app we publish, under the licence of that part. The host
update file (`.cfw`) and the cores (`.cor`) are published as built files with the source, without ROM images.
Rights holders who see a problem: please contact us, and we remove the part concerned.

Open: SPDX headers in our own source files (`SPDX-License-Identifier: GPL-3.0-or-later`) are not added everywhere;
the table above is the reference.
