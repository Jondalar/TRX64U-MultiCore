# 42. EF3 chip erase is a real 8 MB erase

Date: 2026-10-07

Status: Accepted

## Context

The T51 spec (D5) had a chip erase refused on the emulated EF3.

## Decision

Alex 2026-10-07: chip erase = real 8 MB erase (D5 overturned); recovery by writing skoe's base image to slot 0. The CRT writer (T57) must also erase a whole real EF3, not only single slots.

## Consequences

Chip erase of the emulated EF3 = whole 8 MB; trex_crt: whole-cart chip erase (popup + typed word), type 90 image write/verify as the way back after an erase.

## Links

- [T51](../../docs/specs/T51-ef3.md#status-and-history)
- [T57](../../docs/specs/T57-trex-crt-both-cores.md#status-and-history)
- [docs/specs/T51-ef3.md](../../docs/specs/T51-ef3.md)
