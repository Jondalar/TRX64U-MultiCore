# 6. trex_crt (dump / write / inspect) 100 % on both cores

Date: 2026-10-03

Status: Accepted

## Context

The same app must run on Gideon's firmware/bitstream and on ours (the app evaluates which it is running on). Open policy questions Q1-Q7 of the T57 spec were decided on 2026-10-04.

## Decision

Alex 2026-10-03/04: trex_crt dumper, writer and inspector are 100 % on both cores. Q1 = Auto, Q2 = route A (hardware), Q3 = reset on our core only, Q4 = U3 popup, no re-latch, Q5 = NO unattended mode (removed; the board session drives the menu, Alex presses the keys), Q6 = MegaCart host-tested only / CRT type 88, TwoMegabyter unsupported, Q7 = EF3 not board-tested in M1 and not in the cart session.

## Consequences

Detection `api->trx != NULL` + ID "TRX2" + SLOT_STATUS b31; the Gideon path keeps today's register traffic unchanged. The Q1-Q7 detail stays in the T57 spec. Q7 (EF3 not board-tested in M1) is touched by the later EF3 decisions (whole-EF3 dump and chip erase in trex_crt, see ADR-0042 and ADR-0043).

## Links

- [T57](../../docs/specs/T57-trex-crt-both-cores.md#status-and-history)
- [docs/specs/T57-trex-crt-both-cores.md](../../docs/specs/T57-trex-crt-both-cores.md)
