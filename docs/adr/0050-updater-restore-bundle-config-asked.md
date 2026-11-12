# 0050. The updater writes a restore bundle with the config; restoring the config is asked

Date: 2026-10-09

Status: Accepted

## Context

Before it writes, the T49 updater saves bitstream, app and config pages as a raw `TRXBKnnn.BIN` (spec §6.4), which only
JTAG can put back. The updater runs from RAM at 0x03000000 as a complete package (code, blobs, manifest), so it can also
write that backup as an installable package: its own code plus the saved bitstream and app plus a new manifest, started
with "Run Update" like any package. Q5 says the updater never touches the config pages. The config holds the network
password (`CFG_NETWORK_PASSWORD`, type `CFG_TYPE_STRPASS`).

## Decision

Alex (2026-10-09): "Lass uns die Config immer mit ins Backup und Bundle schreiben. Nur beim Restore fragen ob
wiederherstellen."

- The backup (`TRXBKnnn.BIN`) and the restore bundle (`.cfw`) always contain the config pages 0xFE8000-0xFFFFFF; no
  question at backup time.
- When a restore bundle runs, the updater asks "Restore your settings from the backup?" with NO as the default. NO keeps
  Q5 (the config is not touched); YES writes the saved config pages after bitstream and app.
- The bundle is written as `.cfw` only (accepted by every FPGA type and by the stock firmware), not as `.ue2`.

## Consequences

- The stick holds the network password in clear; the tester instructions say so.
- A tester can go back to the previous firmware (also Gideon's/Commodore's after a first install) without JTAG, as long
  as the machine still boots to the file browser. A dead bitstream still needs JTAG until T26.
- The updater finds its blobs through the manifest at run time instead of link symbols, and the manifest gets a
  restore flag (a stock bitstream has no AXSS stamp).
- A power loss while the config pages are written leaves default settings; the machine still boots.
- Running a bundle takes a fresh backup first, about 4.3 MB more on the stick.
- Built 2026-10-09 (spec T49 §4.3). The config pages are written after bitstream and app, as decided; a flash with no bitstream/app pair to restore gets the raw `.BIN` only. A raw bitstream without `.bit` header (openFPGALoader) is bundled too: length = last non-0xFF byte of the slot rounded up to 4, a synthesized 48 byte `.bit` header in the bundle, flag `TRX_FLAG_RAWBIT`; the restore writes the raw image back byte-exact (2026-10-10, spec T49 §4.3).

## Links

TODO T49; docs/specs/T49-update-packages.md §6.4, Q5
