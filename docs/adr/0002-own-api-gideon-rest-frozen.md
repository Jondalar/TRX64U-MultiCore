# 2. Own API (TRX64 WS JSON-RPC) is the target; Gideon's REST is no longer extended

Date: 2026-10-02

Status: Accepted

## Context

Gideon's app exposes a REST API and web pages; the TRX64 functions (monitor, ring, media/machine/input control) use the TRX64 WebSocket JSON-RPC contract.

## Decision

Decision 2026-10-02 "option 1 as target": stage 2 of the decoupling from Gideon is an own UI (T24, LVGL) and an own API for everything (WS JSON-RPC per the TRX64 contract, also for media/machine/input, web UI on it). Gideon's REST is no longer extended. Gideon's app remains only the base layer.

## Consequences

New functions get JSON-RPC methods, not new REST routes; the command table, the `ult/*` methods and the open questions are in T24-ui-wiring §8, §10. The web interface later becomes a loadable app (T52).

Amended 2026-10-18 (Alex, T104.1 Q2): in the generic application, day 1 is REST only - every operation
through one generic REST adapter (`/v1/ops/<name>`) plus an alias table for the Gideon paths our tools use
today; WS JSON-RPC comes after day 1 as a second adapter over the same service layer. Gideon's REST code
itself is still not extended.

## Links

- [T36](../specs/README.md#belonged-to-trx64-ultimate)
- [T52](../../docs/todo/T52.md)
- [TRX64-Ultimate/docs/specs/T24-ui-wiring.md](../specs/README.md#belonged-to-trx64-ultimate)
