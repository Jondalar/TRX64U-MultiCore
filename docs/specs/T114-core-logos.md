# T114 — every shipped core has a recognisable logo in the TRX64 LVGL theme

**Status:** DONE (2026-10-26); S1-S4 green for all current shipped and external core identities. 2026-11-13 status check: state confirmed: merged, row and todo done.
**Repo:** TRX64U-MultiCore — visual assets and their deterministic renderer/validator; no gateware or application behaviour.
**Number:** T114 (registry: the table in `TODO.md`; T113 was the highest in use).
**Origin:** Alex, 2026-10-20: "Can you create a PNG or SVG logo for every core which corresponds to the original logo and looks cool in the LVGL theme?" (translated).
**Depends on:** T101 D5 (the `.cor` icon is 32 x 32 RGB565), T104 (the shared LVGL visual language).
**Milestone:** M3.

---

## §1 The problem

- **P1 One family, distinct machines.** The shipped DFX, large and external core identities need production artwork. A core must be recognisable before its label is read, while all marks still belong to one dark TRX64 interface.
- **P2 Two deliberate representations, one generator.** The UI needs scalable artwork and `.cor` needs exactly 2,048 bytes of little-endian 32 x 32 RGB565. A reduced vector tile is not a usable 32 px icon; both representations must be authored deliberately and generated together.
- **P3 Do not fake provenance.** The set should recall the machines' original visual identities without claiming that newly drawn artwork is an official vendor asset.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 every current core has one complete asset set (P1)
  Given the ids from cores/*/core.toml and cores/*/mono/core.toml
  And   build-only and null filler cores are excluded
  And   the external C64U and TRX64 identities are included
  When  the asset validator runs
  Then  it finds exactly one 32 x 32 PNG and one RGB565 icon for every id
  And   U64 has the same standalone asset set for Gideon's full firmware

Scenario: S2 the host can pack every icon without conversion ambiguity (P2)
  Given any generated RGB565 file
  When  it is checked as the T101 D5 icon record
  Then  it is exactly 2,048 bytes, little-endian, and matches the generated 32 x 32 PNG after RGB565 quantisation
  And   a one-byte-short negative control fails validation

Scenario: S3 the family is legible in the target visual language (P1, P3)
  Given the enlarged native-32-pixel contact sheet on a black background
  When  it is rendered at native size
  Then  every mark has a distinct machine cue and strong silhouette
  And   the 32 px icons contain no reduced tile frame or tiny vector-label residue
  And   the icon is one reduced platform symbol rather than a miniature logo tile
  And   it uses only its flat platform colours plus RGB565 edge shades required for antialiasing

Scenario: S4 the three Ultimate identities cannot be confused (P1)
  Given the generated TRX64, U64 and C64U icons
  Then  all three icon payloads differ byte for byte
  And   TRX64 uses its own TRX mark
  And   U64 uses a U mark
  And   C64U uses only the classic blue/red Commodore C= mark
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | complete set | `tools/core_logos/build.py --check` | native |
| S2 | P2 | exact PNG/RGB565 derivatives | `tools/core_logos/build.py --check` plus its negative control | native |
| S3 | P1, P3 | visual contact sheet | `tools/core_logos/contact-sheet-32.png` | human review |
| S4 | P1 | distinct Ultimate identities | `tools/core_logos/build.py --check` | native |

## §3 Decision

- **D1 One reduced platform symbol per core at 32 x 32.** (The 256 px/SVG view of the first version is gone, §5 2026-10-26.) The generator renders that symbol large, downsamples it once with antialiasing, quantises to the target RGB565 display values, and packs it to T101 RGB565. There is no detailed tile.
- **D2 Shared shell, individual centre.** Every scalable logo uses the same near-black rounded tile and hairline frame; the native 32 px view drops that frame to spend every useful pixel on the silhouette. There is no repeated `TRX64 CORE` footer.
- **D3 Recognition by the smallest useful cue.** The 32 px icon carries no machine name, model number, frame or rainbow: Commodore-family machines use only the classic two-colour `C=` (blue C and upper wedge, red lower wedge), Amiga uses `A`, Apple II uses a monochrome apple, Atari uses the Fuji, NES uses `N`, CPC uses `CPC`, Spectrum uses `ZX`, and Next uses `N`. The adjacent menu label distinguishes models that share a platform symbol.
- **D4 Deterministic output.** One checked-in script owns the native pixel definitions and generates the PNG, the RGB565 and the contact sheet. Validation compares the directory with the live core descriptors.
- **D5 Full-firmware identities stay distinct.** `TRX64`, Gideon's `U64` and Commodore's `C64U` are three independent marks. `tools/core_logos/u64/` is the standalone U64 source used for Gideon's `.cfw`; C64U remains a separate platform identity.

## §4 Out

- ~~Wiring `--icon` into the release packer~~ - lifted by Alex on 2026-10-21, see §5. Changing `core.toml` stays out.
- A licence or endorsement claim for the historical machine names and marks.
- Logos for planned cores which do not yet have a `cores/<id>/core.toml` descriptor.
- Animation, splash screens and wide wordmarks; this is the square core identity used by selectors and cards.

## §5 Decisions taken while building

- **2026-10-26, the 256 px logos and SVGs go (Alex):** "die 256px logos können wegen mir weg und die SVGs dazu auch" (translated: "as far as I'm concerned the 256 px logos can go, and the SVGs with them"). Only `cores/<id>/logo/icon.rgb565` matters for the firmware; `logo-32.png` is its preview. The 32 px icons come from the native pixel definition (`pixel_icon`/`icon_master` in `build.py`), not from the SVGs, so all `icon.rgb565` files stay byte-identical (sha256 compared before/after). Removed: `logo.svg`, `logo-256.png` in every logo folder, `tools/core_logos/contact-sheet.png`, `docs/img/core-logos.png`, the SVG source code and the `rsvg-convert` dependency. Each logo folder now holds exactly `logo-32.png` and `icon.rgb565`. Statements above about SVG/256 px masters describe the first version and are superseded.

- **2026-10-26, softened native artwork (Alex):** the first direct-pixel pass was recognisable but "a pretty drastic pixelated difference from before" (translated). The simplified 32 px geometry remains, but controlled supersampling now softens only its edges before RGB565 quantisation. Frames and miniature footer text do not return.
- **2026-10-26, reduce to the platform symbol (Alex):** the softened pixel pass was still "blocky and blurred". The icon no longer tries to encode model number, wordmark and colours. It is one smooth, mostly monochrome cue; the menu text owns the exact core name.
- **2026-10-26, native 32 px artwork (Alex):** "TRX64 should have its own. U64 and C64U too. The rest may be based on the original platforms and their logos." (translated). The small assets are independent integer-pixel designs without a tile frame or miniature footer text. `contact-sheet-32.png` enlarges those exact output pixels with nearest-neighbour scaling for review. U64 no longer borrows C64U's icon.

- The earlier 2026-10-22 cell-quantised SVG reduction is superseded by D1. It proved RGB565 determinism but also proved that frames and miniature labels become noise at 32 px.
- After visual review, Apple and Commodore no longer share a generic stripe. Apple has its separate six-colour apple; Commodore has five stripes only: `#F20029`, `#FD7801`, `#F2F540`, `#48CC31`, `#14A3FD`.
- Alex's two physical-reference photographs corrected the `C=` itself: both wedges are the same metal/white as the C. The earlier coral lower wedge was removed; the five colours belong only to the separate lines.
- `python3 tools/core_logos/build.py --check` reports 13 complete core sets plus U64 and the generic firmware mark; native-pixel, PNG and RGB565 checks pass and the short-file control fails as required (S1, S2, S4).
- `contact-sheet-32.png` was rendered and inspected: the exact 32 px sources have fourteen distinct, frame-free silhouettes (S3).
- No `.cor`, `core.toml`, application source or packaging path was changed. The ready-made `rgb565/<id>.rgb565` files are the integration boundary left by §4.
- **2026-10-21, wiring lifted (Alex):** "Since I keep the board busy with the TRX64-FW session a bit longer, feel free to build the logos in. The u64-emulator does LVGL too, you can test everything there ... and take screenshots etc." (Alex, 2026-10-21, translated) `tools/pack_cores_mc.sh` (used by `ci/package.sh`) now passes `--icon assets/core-logos/rgb565/<id>.rgb565` for every core that has one; a core without a logo is packed without an icon and the log says so. The icon is part of the .cor bytes, so a changed logo changes the bundle and the manifest SHA-256. Bench: `tests/hostapp/render` draws the boot menu with the real logos and a corrupted-icon control (S4 there).
- **2026-10-21, the logos move into their cores (Alex):** "If the logos are part of the core, they should go there, shouldn't they?" ... "Yes please" - and no rebuild for a picture (Alex, 2026-10-21, translated) Layout: `cores/<id>/logo/{logo.svg,logo-256.png,logo-32.png,icon.rgb565}` per core; the shared generator and `contact-sheet.png` are in `tools/core_logos/` (the nine sets are byte-identical to main 27ca1b25, sha256 compared). The packer takes `cores/<id>/logo/icon.rgb565` by that fixed name when it exists; `core.toml` is untouched and has no field for it. `tree_key` in `ci/lib.sh` excludes `cores/*/logo/**`, so a picture moves no host, OOC or core key (proved on all nine cores, with a .v control that does move its key; see the history file).

- **2026-10-22, icon scale (Alex's board photos of the boot menu: "something is fundamentally off there", translated):** the picture was drawn 1:1 (no LVGL zoom; the layer is 960 x 540 and the pixel layer shows it at the whole factor 2, `PX_CTRL` 0x22 in `trx_lv_port.cc`, `host/pix_layer.v`) inside a 48 px wide grey box, so a 32 px picture had 8 px bars left and right (`hostui.cc` `build_row`, image at x 8). The box is now exactly the picture (32 px, `ICON_W`; 48 px stays the text tile's), the LVGL scale is set to 256 (1:1) explicitly. Render bench: `icon_no_bars` (the pixels around the found picture are the row background) and two controls (`bars`: box wider than the picture; `zoom`: LVGL scale 300) that must go red. The uneven pixel widths of the photo do not come from the app: layer, zoom and the x2 are whole numbers end to end, the output is fixed 1080p; what remains is the source (the 32 px derivative was a Lanczos downsample then, so edges were blended grey values, not pixels (replaced the same day, see the next entry in `docs/todo/T114.md`)) and the screen's own scaling of the 1080p signal. Text inside the logos ("ultimate") is about 3 px high in 32 x 32 and cannot be legible; a legible mark needs a 32 px design without text or a larger icon record (proposal, Alex's decision, nothing built).
