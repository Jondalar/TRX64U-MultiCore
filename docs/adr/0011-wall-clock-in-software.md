# 11. Wall clock in software, no LUTs (G05)

Date: 2026-10-04

Status: Accepted

## Context

The app's rtc_dummy.cc keeps the time in RTC_TIMER_SECONDS (0x10060400); our core has no such counter.

## Decision

Alex 2026-10-04: software, no LUTs. On the TRX64 bitstream rtc_dummy.cc returns the last set time plus FreeRTOS ticks (app branch rtc-sw-app); Gideon's bitstreams keep their counter.

## Consequences

App-only (an rvlite patch 0006 was tried for timing and reverted, run 391). Board 2026-10-06: clock right.

## Links

- [T61](../../docs/todo/T61.md)
- [docs/gap-audit-gideon-app.md](../../docs/gap-audit-gideon-app.md)
