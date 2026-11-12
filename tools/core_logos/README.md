# TRX64 core logos

These are TRX64 UI adaptations, not vendor-supplied originals. They keep the historic machine cues as one reduced platform symbol each. Commodore machines use the blue/red `C=` mark; there are no colour stripes, frames or 256 px/SVG masters (T114 §5, 2026-10-26).

A logo belongs to its core: `cores/<id>/logo/` holds two files, and this folder holds the shared generator, standalone full-firmware identities and contact sheets.

- `logo-32.png` — one separately authored, reduced platform symbol at the host selector's real size, already quantised to the display's RGB565 values. Most are monochrome; Commodore keeps its canonical blue/red `C=`. The adjacent menu label carries model and variant; the icon has no frame, model number, rainbow or footer.
- `icon.rgb565` — 32 x 32, little-endian RGB565, exactly 2,048 bytes: the `.cor` icon record 4. `tools/pack_cores_mc.sh` takes it by this fixed name; `core.toml` has no field for it.

`u64/` holds Gideon's distinct U64 full-firmware identity. `firmware/` holds the generic `.cfw` mark used for an unknown design. `tools/gen_cfw_icons.py` embeds those with the TRX64 logo into the host app; C64U remains a separate platform identity.

`contact-sheet-32.png` enlarges the exact 32 px sources with nearest-neighbour scaling so every source pixel can be reviewed. Rebuild and validate with:

```sh
python3 tools/core_logos/build.py
python3 tools/core_logos/build.py --check
```

By hand:

```sh
python3 tools/cor_pack.py pack ... --icon tools/core_logos/trx64/icon.rgb565
```

The script deliberately fails if the set of logo IDs and the live core descriptors diverge. Planned cores receive artwork only after their `core.toml` exists. `ci/lib.sh` leaves `cores/*/logo/` out of the build keys: a picture is not gateware.
