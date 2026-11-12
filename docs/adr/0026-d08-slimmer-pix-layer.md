# 26. Slimmer pix_layer

Date: 2026-10-06

Status: Accepted

Formerly: decision 8. Old references to it resolve here.

## Context

pix_layer's still capture and configuration registers cost LUTs.

## Decision

Alex 2026-10-06: yes - still capture as a shift register with one buffer, configuration registers no longer readable (status only).

## Consequences

OPT-pix built; benches pixlayer/still60 updated; in main 2026-10-06; freeze picture and Timewarp scrub pass on the board.

## Links

- [OPT-pix](../../docs/specs/OPT-pix.md#status-and-history)
- [docs/specs/OPT-pix.md](../../docs/specs/OPT-pix.md)
