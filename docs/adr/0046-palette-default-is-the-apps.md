# 0046. The app's palette decides, including the default

Date: 2026-10-07

Status: Accepted

## Context

The scaler had Colodore hard-wired (blue 2E2C9B); the app's `default_colors` (u64_config.cc) has 2C29B1 and writes it to C64_PALETTE at boot. The write was dropped by the decode (T59 spec §2). Alex (2026-10-04): custom palettes as on Gideon's core.

## Decision

The app is the source of truth for the palette values, including the default. The RTL keeps Colodore only as reset contents until the app's first write. "Gideon measured wins" does not apply to the values (they are in the app); only behaviour the app does not show (immediate vs next-frame update) is a board check.

## Consequences

- After the app's first write the C64 picture changes slightly from the old Colodore (blue 2C29B1 and others).
- No board measurement is needed to build or to choose the palette; board test T59-PAL compares with Gideon's picture.

## Links

TODO T59, docs/specs/T59-palette.md
