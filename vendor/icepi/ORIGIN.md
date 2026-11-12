# vendor/icepi — our C64 core (origin: icepi-zero-c64)

Since 2026-10-01 this is TRX64 Ultimate's own code: edit it here directly.
It started as a copy of the C64 gateware of icepi-zero-c64 (m1nl, with a fork of it,
branch easyflash-writable), taken with the former tools/vendor_icepi.sh:

| part | commit |
|---|---|
| icepi-zero-c64 | a50750d745a426e5b5f138843695ac713b50297f |
| gateware/c64_redip_cia/reDIP-CIA | 7e077151804106269eb522b63ccca29b7279d9fe |
| gateware/c64_redip_sid/reDIP-SID | f89f53e3108bcb03e8f23505bc3a70d0c206e0e6 |
| gateware/usb_hid_host | e49217661b275554807760d4e12097b0f189316a |
| gateware/vicii-kawari | ca8c43dc69358173e5f0cb632a8c3b91f309cb5a |

The former local changes are baked in (they were patches in tools/patches/icepi,
removed with the commit that wrote this file; see its parent in git history):
`default_nettype none` commented out in every Verilog file (Vivado 2024.1
Synth 8-6735), and
- 0001-kawari-hdmi-duplicate-parameters
- 0002-cartridge-io2-we-valid-reg
- 0003-keyboard-declare-before-use
- 0004-vic-native-pixel-stream
- 0005-board-keyboard-joysticks
- 0006-sid-audio-out
- 0007-cartridge-io2-write-tap
- 0008-cpu-port-8-bit-floating-bits
- 0009-cia2-port-b-own-outputs
- 0020-app-dma-bus-cycles-ultimax
- 0030-external-cartridge
- 0040-optional-internal-1541
- 0050-dma-busy-for-machine-pause

Own edits since (made in place, T60, 2026-10-06; docs/specs/T60-reu-16mb.md): `c64_reu.v` 24-bit expansion
address with Gideon's size mask (reu.vhd), $DF06 8-bit write and his read-back, status bit 4 from the size;
`c64_top.v` `reu_size` input, 16 MB command address `{1'b0, reu_mem_addr[23:1]}`, `reu_present` live from
`flags[12]` (held while the REU moves data) instead of sampled at the reset.

Own edits since (T25.4, 2026-10-07; docs/specs/T25.4-redip-ultisid.md): `c64_redip_sid/reDIP-SID/gateware/*.sv`
(the pipelines for NS SIDs, new `sid_core.sv`; commits 2dc7187, 547ebd1 and 10f43f4 are the patch for Dag Lem),
`c64_redip_sid/sid_api.sv` (NS = 2 on `sid_core`), new `c64_redip_sid/sid_multi.sv` (8 SIDs, TRX_SID=redip8).

Licences: GPL-3 (icepi, kawari), CERN-OHL-S v2 (reDIP SID/CIA). Per-part authors and
licences (Apache-2.0 usb_hid_host, MIT or Apache-2.0 hdmi/): ../../PROVENANCE.md. Changes worth giving
back go to the fork as separate commits.

The host does not read `hdmi/` directly any more (T164 WP4): it has its own fork in `host/hdmi_enc/` (`ce`, runtime frame, AVI aspect, ACR on pixel
clocks, windows decided one pixel ahead, `MAX_PACKETS` 3), one `*.diff` against this directory per changed file; `hdmi/` stays untouched.
