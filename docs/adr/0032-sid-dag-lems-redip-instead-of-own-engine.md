# 32. SID: Dag Lem's reDIP brought up to UltiSID level; no own clean-room engine

Date: 2026-10-06

Status: Accepted (supersedes [ADR-0001](0001-reDIP-stays-default-no-copper.md))

## Context

The clean-room engine (ADR reDIP-stays-default) did not place; Dag Lem's reDIP SID is the production engine. Licence settled by decision 11.

## Decision

Alex 2026-10-06: no own clean-room SID engine any more; bring Dag Lem's reDIP SID up to UltiSID level instead (up to 8 SIDs time-multiplexed, the app's UltiSID register interface/mapping/panning/model, filter curve choice audible), then the decimator/mixer (T25.3) and the sampler (G14). Do not ask Dag first: "Erst machen, rund bekommen und dann ihm zurück geben (per git)" - the finished changes go back to him as git commits/patches.

## Consequences

T25.4 (reDIP generalised to NS SIDs, 8 = two UltiSIDs split in four) built on branch t25-redip, merged into integ-m1; board 2026-10-07 hearing test of 8 SIDs passed ("The Tuneful Eight"). Patch for Dag: commits 2dc7187 + 547ebd1 + 10f43f4. Further 2026-10-07 calls: ADR-0037, ADR-0038, ADR-0039, ADR-0040.

## Links

- [T25](../../docs/todo/T25.md)
- [docs/specs/T25.4-redip-ultisid.md](../../docs/specs/T25.4-redip-ultisid.md)
- [LICENSING.md](../../LICENSING.md)
