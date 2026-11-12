# Release sets and boards (T94 D4)

A release set is one host plus a list of `.cor` files plus the `boot.cfg` the boot menu reads:
one versioned package `trx64set/rel-<n>`. What a board is supposed to run is written down here,
not in somebody's shell history.

| Path | What it holds |
|---|---|
| `sets/rel-<n>.toml` | the set: a published host version and one `[[core]]` per core, hand-edited, checked by the lint stage |
| `boards/<board>.toml` | which set a board is supposed to run, where it is and how it is reached |

`sets/` is empty: the first bundle `rel-1` (TRX64 reference core plus the VC-20, T94 Q4) is
written when the pipeline publishes its first host (T94 D2/D3) - a set may only name versions
that exist as packages.

**Deploy freeze (Alex, 2026-10-13):** nothing new goes onto the board until T94 delivers one
host with all cores. There is no deploy job yet; when it exists it reads these files.
