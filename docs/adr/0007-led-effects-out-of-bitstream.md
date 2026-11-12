# 7. LED strip and lighting effects out of the bitstream (A04)

Date: 2026-10-04

Status: Accepted

## Context

Done-audit 2026-10-04, item A04 (Alex, one by one). The LED effect engine (led_engine) costs LUTs on a core that is tight.

## Decision

LED strip and lighting effects (led_engine) leave the bitstream (parked at tag park/led-effects); keyboard data path, SHIFT LOCK LED and case LED on/off stay. Alex: "who wants the original C64U feeling boots Gideon's firmware".

## Consequences

Branch opt-bling (bling_io 620 -> 69 LUT). The app's LedStrip / Keyboard Lighting menus remain and do nothing. The other audit decisions A01-A12 stay in docs/done-audit-2026-10-04.md.

## Links

- [T08](../../docs/specs/OPT-bling.md#t08)
- [T09](../../docs/specs/OPT-bling.md#t09)
- [docs/specs/OPT-bling.md](../../docs/specs/OPT-bling.md)
- [docs/done-audit-2026-10-04.md](../../docs/done-audit-2026-10-04.md)
