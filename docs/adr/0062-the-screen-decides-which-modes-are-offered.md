# 0062. The screen decides which modes are offered; the host holds a default, each core its own

Date: 2026-10-31

Status: Accepted (Alex, 2026-10-31, T107 D11a-c)

## Context

Gideon's applications and ours list every mode the board can drive. A mode the screen refuses leaves the board dark (T105: a tester lost an evening on
2026-10-16; the Sony TV refused five of six modes on 2026-10-31). Alex, 2026-10-31 (translated): "My point is to offer nothing in the menu that
the monitor/TV cannot do. And when the EDID changes, switch to a fitting mode automatically. The host may store one default mode and the cores read
the value from the host config, but each core should have its own settings."

## Decision

- The gateware executes a request and knows no policy. The applications decide: the list of modes is the table of 12 rows intersected with what the
  EDID names (CEA short video descriptors, detailed, established and standard timings, range limits, maximum TMDS clock). With no EDID a fixed
  television set is listed (T164 §3a Q9). The V key steps through the same list.
- A new EDID (hot-plug, another screen) is read again, and a running mode that is not in the new list is replaced once by the best fitting one, in the
  host and in a running core's menu.
- Precedence: a core's own Picture in its `.cfg`, else the host default (the T163 store), else the best fitting mode.

## Consequences

- One parser, `app/video/edid`, host-testable with captured EDIDs, used by both applications and the web UI.
- A screen that lies about its EDID can still be given a bad mode; the blind V key and (T164 §3a Q7) a revert timer are the way out.

## Links

[T164](../specs/done/T164-host-package-2.md) S10-S12, §3b G11-G13, [T163](../specs/done/T163-own-settings-store.md), [T105](../specs/done/T105-video-mode.md), [ADR-0057](0057-every-application-owns-the-video-mode.md)
