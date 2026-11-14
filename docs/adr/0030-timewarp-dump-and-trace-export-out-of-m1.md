# 30. No .c64re / ring dump or load and no trace export in M1 (A13)

Date: 2026-10-06

Status: Accepted

## Context

Done-audit item A13 ("Load a saved Timewarp ring": out) and the follow-up on 2026-10-06.

## Decision

Alex 2026-10-06: "nimm es raus und dann in M2 ein Todo das sauber zu implementieren"; "Den Code darfst Du wegwerfen". M1 has no .c64re and no .c64rering dump or load at all (menu, REST, monitor verbs). Trace export: "Ist ok, schieben wir nach M2" - `tracering` / `trace/build_from_ring` / `.c64retrace` leave M1; the trace former (P4) and `sd` stay.

## Consequences

Removed 2026-10-06 (app a13-no-dump-app 87050ca, a13-trace-app; the code is in git history before app commit 24572a6). Rebuilt properly in T66 (M2), planning starts with a triage with the TRX64 and C64RE sessions. Idea to check (Alex, TBD): a host importer plus a streamed trace instead of files.

## Links

- [T66](../specs/README.md#belonged-to-trx64-ultimate)
- [T20](../specs/README.md#belonged-to-trx64-ultimate)
- [T21](../specs/README.md#belonged-to-trx64-ultimate)
- [T20-delta-ring.md](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T20-delta-ring.md)
- [docs/done-audit-2026-10-04.md](../../docs/done-audit-2026-10-04.md)
