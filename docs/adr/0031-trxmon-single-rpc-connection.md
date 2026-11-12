# 31. trxmon accepts exactly one RPC connection

Date: 2026-10-06

Status: Accepted

## Context

C64RE spec 889 needs a clear contract for the monitor's RPC port.

## Decision

Alex 2026-10-06, for C64RE spec 889: trxmon accepts exactly ONE RPC connection; a second one is refused with a clear reason naming the held port (and the peer address); `ping` carries `capabilities` (served methods + notifications) and the real build ids (firmware git sha and trxmon git sha) instead of the constant "trxmon 0.1".

## Consequences

Built 2026-10-06 (app t21-rpc1-app 2fcac32b + ABI 5390a16e): WS close 1013 / HTTP 503 "port 4312 is held by <peer>". A holder that vanishes without FIN blocked the port; closed 2026-10-08 (T55, app t55-mon-app): a WebSocket ping after 20 s of silence, the holder dropped after 50 s. The trxmon manifest also got `"rest": [ "serve" ]` (app 5b605bb0).

## Links

- [T21](../../docs/specs/T21-monitor.md#status-and-history)
- [docs/specs/T21-monitor.md](../../docs/specs/T21-monitor.md)
