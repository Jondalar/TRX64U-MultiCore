# T220 — The host's network chip shows the link the board has, within seconds of a change

**Status:** PROPOSED (2026-11-15), backlog.
**Repo:** TRX64U-MultiCore — host application (`host-app/software/hostapp/`).
**Number:** T220.
**Origin:** a tester in GitHub discussion #84 (release main-ef67ea93, 2026-11-15): the chip in the top right corner of the main menu "showed 'no network' (but I noticed lan was working just fine) and after cycling wifi on and off in F8 setting suddenly the menu showed that I am connected via 'LAN(192,168,178,248)'". Alex: backlog, "do we poll the network again and again, or how do the code and the UI do it?" (translated).
**Depends on:** nothing.
**Milestone:** M2.

---

## §1 The problem

**How it works today (read from the code, 2026-11-15):**
- The chip text comes from `hc_chips()` (`host_core_rows.cc:973`, `net_chip_text`: "LAN x" before "WiFi x", else "no network" `wifinet.h:49`) out of the status `g_st` that `refresh_status()` fills (`hostui.cc:392-438`).
- `refresh_status()` is polled: in the main loop every 3 s, but only when no key came for 1.5 s (`hostui.cc:4154`, `t - last >= 3000 && t - last_key_ms >= 1500`); also once at start (`:3992`, `:4328`, `:4556`) and after actions (`:3211`, `:3221`, `:3250`).
- The wired address: `ip_string()` (`hostui.cc:343-375`) takes the first wired interface with `is_link_up()` and a usable address (`net_address_usable`), then WiFi from the driver status.
- When something changed, `update_chips()` redraws the chips (`:4158-4159`).

**What the tester saw:** LAN worked (address held, network used) while the chip said "no network" until a WiFi toggle forced a refresh. Cause not found yet. Parts:
- **P1** Which input stayed stale: `is_link_up()` of the wired interface, its address, `net_address_usable()`, or the poll not running (a state where the 3 s branch is not reached, e.g. a job, another view, or a key stream).
- **P2** Whether the redraw follows a detected change in every view (the chip is in the header of all pages).

## §2 Scenarios

- **S1** Given the host menu is shown and the LAN cable gets an address after the menu came up (DHCP late), when 3-5 s pass with no key, then the chip shows "LAN <address>". Proof: ue2emu with a delayed lease, or the board (Alex).
- **S2** Given LAN up and the chip shows it, when the cable is pulled, then within 5 s the chip shows WiFi or "no network"; plugged again, "LAN x" returns. Proof: board.
- **S3** The same in every host view (Home, Library, F8 pages, a running job). Proof: ue2emu.

## §3 Decision

Open (backlog).

## §4 Out

Nothing decided yet.

## §5 History

- 2026-11-15: spec written from the tester's report and a read of the code (Claude); nothing built.
