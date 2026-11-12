# 23. Floppy in software on the RISC-V / one 6502 core time-shared: rejected

Date: 2026-10-05

Status: Rejected

## Context

Idea in the reset document: run the 1541 in software, or share one 6502 core between the C64 and the 1541.

## Decision

Alex 2026-10-05: rejected - rvlite cannot do a cycle-exact 1541 in real time; for a shared CPU: in C64RE it only became good once the floppy CPU was really separate (fast loaders and floppy-code loaders are picky).

## Consequences

The hardware 1541 (c1541_drive) stays (decision 1).

## Links

- [docs/architecture-reset-2026-10-05.md](../../docs/architecture-reset-2026-10-05.md)
