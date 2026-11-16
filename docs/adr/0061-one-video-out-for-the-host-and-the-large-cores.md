# 0061. One video-out source for the host and every large core

Date: 2026-10-31

Status: Accepted (Alex, 2026-10-31, T107 D11d: "via the host, or as an include in large cores")

## Context

Five cores are large (flat full-bitstream builds, `cores/<id>/mono/`): amiga1200, c128, snes, neogeo, zxnext. snes, neogeo and zxnext read the host's own
files (`host/top.v` unchanged, `pix_layer.v`, `overlay.v`, `vic_scaler.v`). The A1200 carries forks of `top.v` (104 KB), `overlay.v`, `pix_layer.v`,
`bling_io.v` and three encoder files, kept in step by `.diff` files; the C128 a `top.v.diff` for its A/V port. A fix to the picture path would otherwise
be made in the host and in up to two forks, and a fork drifts (the A1200 `pix_layer` was pipelined for timing in the fork only, run 1051).

## Decision

The picture path is one module, `host/hdmi_out.v`, with one generated mode table `host/hdmi_modes.vh`. `host/top.v` instantiates it, and the large cores
build `host/top.v`. The A1200's copies are deleted and the core reads the host's files; the C128 keeps only the diff for its own pins. The "include"
is the host's source, not a second copy.

## Consequences

- A host change reaches the large cores by building them; every large core is rebuilt with each host key (already so: they flat-build the host).
- Large cores pay the same area as the host (about +1,076 LUT, +3.3 % of the device); the C128 (85 %, 75 of 75 tiles) and Neo Geo (82 %) are the
  ones to watch.
- `tools/host_pin.sh --check` and the wrapper-fork check keep a core from editing a host file.

## Links

[T164](../specs/done/T164-host-package-2.md) S13, §3b G9, [ADR-0058](0058-full-bitstream-cores-by-iprog-from-a-flash-file.md), [T107](../specs/done/T107-application-owns-the-picture.md) §5.8
