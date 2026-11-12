# T151 — Every core can keep its inserted media across a reset and a restart; "Reset and eject all" in Actions

**Status:** PROPOSED (2026-10-28), asked by Alex.
**Repo:** TRX64U-MultiCore — the shared base of the core application (`coreapp/src`, the media slots every descriptor
already declares), the core folder of T148 (where the list is kept), the menu (Actions). No gateware.
**Number:** T151.
**Origin:** Alex, 2026-10-28, at the board (translated): "General feature: not 'save state', but in every core you should be
able to say that you want to keep the inserted media. So that after a reset or a restart it continues. Then a 'Reset + eject
all media' in the actions would be good."
**Depends on:** T148 (core folder), T104 D2/D3 (shared behaviour in the base, not per core), T104.2 D10 (menus in TOML).
**Milestone:** M3.

---

## §1 The problem

- Today a reset, a "Reload core" or a power cycle leaves every media slot empty (disks, cartridge, hard-disk images, tape):
  the person mounts the same files again each time.
- Parts: **P1** a per-core setting "Keep media" (on/off); **P2** the list of what is in each slot, kept in the core folder,
  written when a slot changes; **P3** re-insert at reset, reload and core start, before the machine is released; a file that is
  gone is skipped and named; **P4** "Reset and eject all media" in Actions; **P5** the same for every core, in the base: a core
  gives only its slots (it already declares them).

## §2 Scenarios

```gherkin
Scenario: S1 kept across a power cycle
  Given the A1200 with "Keep media" on, game.adf in DF0 and work.hdf in HD0
  When  the board is power-cycled and the A1200 is started again
  Then  DF0 and HD0 hold the same files before the machine runs (log names each re-insert)

Scenario: S2 off
  Given "Keep media" off
  When  the core restarts
  Then  every slot is empty, as today

Scenario: S3 a file is gone
  Given a kept DF1 whose file was deleted
  When  the core starts
  Then  DF1 stays empty, the others are inserted, one line says which file is missing

Scenario: S4 Reset and eject all
  When  Actions -> "Reset and eject all media" is chosen
  Then  every slot is empty, the kept list is empty, the machine resets
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1-S4 | P1-P5 | board, one core with disks (A1200), one with a cartridge (C64/TRX64U) | board |

## §3 Decision (proposed)

- **D1** The setting and the list live in the base, for every core: `<medium>/cores/<core>/media.cfg` beside `<core>.cfg`
  (slot name -> path, mode such as read-only/unlinked).
- **D2** Default off (today's behaviour) until Alex says otherwise.
- **D3** Write-back images (hard disks, writable floppies) are re-inserted as they are; nothing is copied.

## §3a Open questions

- **Q1** Default on or off.

## §4 Out

- Machine state (RAM, CPU): that is save state, not this.

## §5 Build notes

(filled in while building)
