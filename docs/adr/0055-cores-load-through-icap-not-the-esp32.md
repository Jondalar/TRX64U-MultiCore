# 0055. Cores load through ICAP in the host, not through the ESP32

Date: 2026-10-12

Status: Accepted (supersedes the guest loading of [docs/boot-chain-concept.md](../boot-chain-concept.md) and T26; amends [0005](0005-golden-stub-boot.md))

## Context

The boot-chain concept (2026-10-01) let the ESP32 shift full guest bitstreams into the FPGA over JTAG, because a full reconfiguration through ICAP ends the logic that does it. Since then the host/core split works with partial reconfiguration (T79.1 PoC 4: host and app survive a core swap) and layer 1 has an ICAP loader (dfx-trial 7d548a5).

## Decision

Alex, 2026-10-12: "the ESP32 can go as an access path, now with ICAP and DFX we don't need it any more." Cores come as `.cor` bundles from USB or SD; the host loads their partial bitstream through ICAP and softboots their application ([T84](../specs/done/T84-boot-chain.md)). The golden stub of ADR-0005 is the full bitstream at 0x0 with the host and the null core.

## Consequences

No new ESP32 firmware, no transfer over the WiFi UART, no JTAG sharing between ESP32 and cable for loading. Foreign full bitstreams (Gideon's original) are not offered (T84 Q3: they need their own flash layout). **Amended 2026-10-21 by [0058](0058-full-bitstream-cores-by-iprog-from-a-flash-file.md):** they start by IPROG from a flash-drive file (T115), no own layout needed. The ESP32 tools of T26.1 stay on their branch.

## Links

- [T84](../specs/done/T84-boot-chain.md), [T79](../todo/T79.md), [T81](../todo/T81.md), [T26](../todo/T26.md)
