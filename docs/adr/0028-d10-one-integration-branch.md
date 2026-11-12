# 28. One integration branch integ-m1

Date: 2026-10-06

Status: Accepted

Formerly: decision 10. Old references to it resolve here.

## Context

Many single merges to main.

## Decision

Alex 2026-10-06: one integration branch `integ-m1` with all finished branches instead of many single merges ("viel zu fragmentiert"); one CI build, one board test, then to main.

## Consequences

integ-m1 / integ-m1-app carry the merges; CI tier 2 also runs on integ-* branches (T31 decision 5). Alex merges integ-m1 into main after the board test.

## Links

- [T31](../../docs/specs/T31-ci-pipeline.md#status-and-history)
- [docs/architecture-reset-2026-10-05.md](../../docs/architecture-reset-2026-10-05.md)
