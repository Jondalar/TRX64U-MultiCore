# 9. Flash write gate open in M1; T44 and T50 out of M1

Date: 2026-10-04

Status: Accepted

## Context

T44's gate protects flash below 0x400000; the 100T layout would need another limit (0x58) and T49 app updates need the app slot writable.

## Decision

Alex 2026-10-04: the gate stays in the design but open (`g_low_limit` 0: program/erase pass at every address, as on Gideon's core); status-register writes and 4-byte opcodes stay blocked. T44 and T50 (T+R+X unlock) are out of M1: with the gate open no unlock is needed.

## Consequences

In main 2026-10-06 (t44-open). Protection comes back with T50 (milestone M3). The board has run with the open gate since 2026-10-06.

## Links

- [T44](../specs/done/T44-flash-write-gate.md#status-and-history)
- [T50](../specs/README.md#belonged-to-trx64-ultimate)
- [docs/specs/done/T44-flash-write-gate.md](../specs/done/T44-flash-write-gate.md)
- [TRX64-Ultimate/docs/specs/T50-update-unlock.md](../specs/README.md#belonged-to-trx64-ultimate)
