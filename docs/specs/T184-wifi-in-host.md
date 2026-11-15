# T184 — The host menu and the core applications are on the network over WiFi as well as over Ethernet

**Status:** DECIDED (2026-11-05); BUILT and merged (416278312): D1-D5; board 2026-11-07 S1 works (Alex); S2-S4 (power cycle, static address, boot without WiFi) not stepped, so not DONE. 2026-11-15 sync: 'build open' removed.
**Repo:** TRX64U-MultiCore — host app (`host-app/`, network page), core app (`coreapp/`, same network stack), the copy of Gideon's
WiFi driver under `vendor/gideon`; no gateware change expected (the ESP32 UART is in `host/ultimate_sys.vhd`, the two control pins in
`host/esp_ctl.v` since T152).
**Number:** T184.
**Origin:** GitHub issue #45 (a tester, 2026-11-05: "Allow FTP/SFTP to the hostmenu via wifi"; "FTP is fine, wifi should be possible?").
Alex, 2026-11-05 (translated): "Seriously? Build it! That was required from the start" — T84 D10 named the network stack and a network page
for the host app; only the Ethernet part (lwIP on the RMII MAC, `ftpd`) was taken over, the WiFi driver was not.
**Depends on:** T84 (host app), T152 (`esp_ctl.v`), T26.0 (ESP32 facts).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a person at the C64U without a LAN cable (most testers), and Alex.
- **What they want:** reach the board's file systems by FTP (and the REST/telnet services the app already serves over Ethernet), pick cores
  from a PC later; over WiFi, as Gideon's firmware and the C64U as sold do.
- **What happens today:** the host app links `network_interface`, `network_config`, `rmii_interface`, `ftpd`; no `wifi*.o` is in
  `host-app/target/u64ii/riscv/hostapp/output/`. The ESP32 on the board runs its WiFi bridge firmware (T26.0: "ESP32 WiFi Bridge"
  V1.8-V1.14), but nothing on the FPGA side talks to it, so the board has no WiFi under our host.
- **Parts:**
  - P1 Driver: Gideon's `software/io/wifi` (`wifi.cc`, `wifi_cmd.cc`, `esp32.cc`) and its lwIP network interface, ported into the host
    app and the core app.
  - P2 Settings: SSID, password, DHCP / static address, on/off; stored with the other network settings, read by the core app as defaults
    (T84 D-settings rule).
  - P3 UI: the host menu's network page shows and edits WiFi (scan list, connect, state, IP), parity with Gideon's WiFi menu items.
  - P4 Services: FTP (and whatever the Ethernet interface serves) on the WiFi interface too.
  - P5 Safety: the ESP32 firmware is never written (T26.0: no recovery path), `esp_ctl` pins stay as T152 left them.

## §2 Scenarios

```gherkin
Scenario: S1 FTP over WiFi from the host menu
  Given the board with no LAN cable and a configured WiFi network
  When  the host menu is up
  Then  the network page shows WiFi connected with an IP address
  And   `curl ftp://<that ip>/SD/` lists the SD card
Scenario: S2 settings survive a power cycle (Alex powers)
Scenario: S3 a core running: FTP over WiFi still reaches the board (core app)
Scenario: S4 no WiFi configured: nothing hangs, Ethernet works as before (negative control: the host boots to the menu in the same time)
Scenario: S5 the ESP32 is never flashed: no Download/Flash call is linked (map file check)
```
Proof (bench = tests/hostapp/render, builder, no hardware; board = Alex's step on the C64U):

| Scenario | Bench | Board |
|---|---|---|
| S1 | network page rows, status text, scan list, password entry, the connect call (stub driver) | `curl ftp://<ip>/SD/` over WiFi, no cable |
| S2 | the page's DHCP/static rows write the store (`scenarios_t184`) | power cycle, the rows and the connection are back |
| S3 | `hostwifi` fills the core's store from the host's `host.cfg`, read only (`run_hostwifi.sh`, 4 controls); the core's WiFi row (goldens, `test_desc`) | a core running, FTP over WiFi reaches the board, with a static address set in the host |
| S4 | no-module page: row says so, D/L/F start nothing and show a note | no WiFi configured: host boots to the menu in the same time, Ethernet works |
| S5 | map file check of both apps (0 and 0 `Esp32::Download/Flash`) | none |

The driver itself (the RPC to the ESP32, DHCP, the FTP data path) runs only on the board. The benches stub `wifinet`.

## §3 Decision

- **D1 (Alex, 2026-11-05):** build WiFi into the host app; it was required from the start.
- **D2 (Alex, #45):** FTP yes; SFTP/SSH out (not in the foundation).
- **D3 (Alex, 2026-11-06: "WiFi settings set and edited in the host under F8. In the cores only shown, and on/off", translated):** the host's F8
  Settings page holds the WiFi rows (AP list, connect, password, DHCP / static address, forget); the host's F7 Network page shows the state. A core's
  menu shows the WiFi state and has one row WiFi on/off; it edits nothing else and keeps no WiFi keys of its own.
- **D3 amended (board 2026-11-06, W.1 fail; Alex: "fix that right away before we give it to the testers", translated):** the module reported
  `Disabled` (its radio off) and the host page had no way to switch it on; every action said "There is no WiFi module". The host's
  WiFi page gets the same live "WiFi" On/Off row as the cores (`wifi_net_radio`), and the Disabled state says that the radio is off, not that
  no module exists.
- **D4 (coordinator, 2026-11-06, from the fork omnicron-c64/ultimate-community-edition f7f665ab, GPL-3):** take its host-side driver fixes to Gideon's
  WiFi code (reply slots with sequence check and timeouts, frame size check, no-reply packet send, bounded scan decode, rescan keeps the link,
  connect state only from the module's event, SSID terminator and control bytes, 64-character password, usable-address check, DNS from lwIP);
  not its saved-AP commands (they need its changed ESP32 bridge firmware; P5).

- **D5 (Alex, 2026-11-07: "A, and what do we do when both are connected? Then LAN before WiFi", translated):** the top-bar network chip names
  the link with the address, "LAN 192.168.x.x" or "WiFi 192.168.x.x"; with both links up it shows LAN. Also the check for the two-address
  finding of T187 (top bar .18 vs Status .189).
## §4 Out

- SFTP/SSH (D2). Writing the ESP32's firmware (T26.0, P5).

## §5 Build notes

**Ported files (state 2026-11-05, build proven).** Gideon's driver, 1:1 where it says so:
- `io/wifi/{wifi_cmd,esp32}.{cc,h}`, `io/wifi/wifi.{cc,h}`, `io/network/network_esp32.{cc,h}` and `u64ctrl/main/rpc_calls.h` (the RPC numbers) are listed in `host-app/vendor.list`
  and `tools/gideon_app.list`; host-app/software/ is the copy of vendor/gideon/software/ plus `host-app/patches/*.patch` (`tools/hostapp_vendor.py`, `MODIFIED`).
- The vendor files `network_esp32.*` and `wifi.*` were copied **by hand**: `~/Development/1541ultimate` is on the branch `tools/esp-probe`, whose `esp32.cc` differs
  from the one in vendor/gideon (the probe tool's changes), so `tools/gideon_sources.sh` would have brought those in. `esp32.cc` and `wifi_cmd.cc` were already in
  vendor/gideon and stay as they were. Re-vendoring from a clean Gideon checkout must give the same four files plus the patches below.
- Patches (the `MODIFIED` table has the reason of each): `wifi.cc` (-DTRX_HOSTAPP: no U64Config power-mode store, no Browsable AP list), `network_esp32.{cc,h}` (no tree
  browser, popups or string boxes), `esp32.h` (no `menu.h`: nothing there uses it; found by the first build).
- Our own: `host-app/software/hostapp/wifinet.{h,cc}` (the page's view of the driver: status, scan list, connect, disconnect, forget; jobs run on a task of their own),
  `espreboot.cc` (rewritten: the power cycle goes through `wifi_cmd.cc`, it opens no UART of its own), `coreapp/src/hw.cc` (the rocker part, below),
  `coreapp/src/hostwifi.{h,cc}` (the host's WiFi settings into the core's store, read only, below). Both Makefiles list the new files; both build with `-DFLASH_EXTRA_PROTECTION=1 -DTRX_HOSTAPP=1`.

**Settings (P2).** The store is Gideon's own "WiFi settings" (id `0x57494649`; `Use DHCP`, `Static IP/Netmask/Gateway/DNS`; with `U64 == 2` there is no "WiFi Enabled" item - the
module is enabled unconditionally at start, `WiFi::Init`). The host app keeps it in its settings file like the other stores (T163 backend). The core app does not write stores
(netops.cc) and has no WiFi settings of its own (D3, the first design with `wifiset` and core keys `WiFi Use DHCP` ... is gone). The access point and its password are not in
the store: the ESP32 module keeps them ("Connect to last AP"). The core application reads the section `[WiFi settings]` of the host's `/Flash/trx/host.cfg` once at its start
(`hostwifi_load`, `run.cc`, right after the file system is bound) into its own store and effectuates it; it never writes the file. A missing or unreadable file leaves the store's
defaults (DHCP) and logs one line. "Use DHCP" and the four static addresses are taken, nothing else (the radio's on/off is not stored).
Test: `tests/coreapp/run_hostwifi.sh` (`test_hostwifi.cc`, 25 checks, 4 controls: loader disabled, radio key taken, store not effectuated, other store's section), part of the coreapp suite.

**D3 built (2026-11-06).** Host part: the F8 Settings page has a WiFi page (AP list, connect, password, DHCP / static address, forget; `wm_*` in `hostui.cc`), F7 Network keeps the read-only
status; render bench `tests/hostapp/render` 1250 checks, 0 failed. Core part: no WiFi key in any core (`test_desc.cc` fails on a leftover `WiFi ...` / `Wi-Fi ...` setting; control `wifi_key_left`);
every classic core has one fixed row `WiFi` (EK_WIFI) in F8 System (On / Off, state shown), the C64's record one node `WiFi` (F8, `wifi.toggle`) and read-only Wi-Fi status / network / address
in F6 Network (the old Wi-Fi buttons, SSID and password settings are gone, 2 settings less in `descrec`). Goldens `tests/coreapp/golden/menu_*.txt`: 18 classic cores gain the F8 row, the C64
loses the Wi-Fi buttons, nothing else changed; `gen_menu_rec.py --check` and `check_menu_concept.py` ok; controls `menu_wifi_row`, `menu_wifi_twice`, `wifi_key_left`. The host's stored
values reach the core through `hostwifi_load` (above); `run_hostwifi.sh`: the host's static values land in the core's store, no file is written, a missing / unreadable file or a foreign
section leaves DHCP; negative control with the loader disabled fails (8 checks red). Builder: `ci/apptests.sh` hostapp, coreapp, loader PASS (cache mounted), `pages_lint` ok, core app render
bench PASS (the F8 System page of a classic core shows the WiFi row); RISC-V: `hostapp.app` 900996 bytes, `coreapp.app` 1689268 bytes (276800 of the 1966068 slot not used by this image),
`check_app_size` ok, map check `Esp32::Download|Flash` 0 and 0.
**Coordinator proposal (not decided by Alex):** the core's On/Off switches the radio live (`wifi_net_radio`) and is not stored; the next start follows the host (the module is enabled at start,
`WiFi::Init`). The address settings are read at the core's start only; a change in the host reaches a running core after its restart.

**S5 (map file check, builder, `u64-build-env:ci`, 2026-11-05).** `grep -c 'Esp32::Download\|Esp32::Flash'` in `hostapp/output/hostapp.map` and `coreapp/output/coreapp.map`
= 0 and 0, and `nm -C` of both ELFs lists only `Esp32::{Esp32,AttachApplication,StartApp,StopApp}`: `Download` and `Flash` are not linked (`-DFLASH_EXTRA_PROTECTION=1` takes both out of
esp32.h/esp32.cc). The ESP32 is never written.

**The rocker (power button) in the core app.** Before, `hw_rocker_init()` opened its own DmaUART, ran a loopback self-test and an identify probe (at most 500 ms) in line. Now the
UART is Gideon's `esp32.uart`, opened by `wifi_command_init()` from the InitFunction `WiFi::Init` (priority 52, in `executeAll`, before `hw_rocker_init`) with the same settings
(5 Mbit/s, CTS/RTS, SLIP, receive interrupt). The menu/freeze event bytes are lifted out of the stream in `rx_dma.vhd`, so the button path does not depend on the callback;
the callback `wifi_rx_isr` hands every other packet to the waiting task or the WiFi task, so the buffer pool keeps running. Removed: the boot-time loopback self-test (it only
printed a line, nothing depended on it; the `ROCKER_INJECT_MENU_MS` board hook stays, on the driver's UART). The identify (2.5 s timeout, longer than the old probe) is a task of
its own now (`rocker_report_task`): the boot of the core app never waits for the module. Not proven on the board: the Menu button after a core start, with and without WiFi
configured (board test, Alex powers).

**Build and tests (builder, 2026-11-05).** Host app and core app link for RISC-V (`hostapp.app` 882256 bytes, `coreapp.app` 1688724 bytes; the slot leaves 277344 bytes of the
1966068 to this image). `ci/apptests.sh` hostapp, coreapp, loader PASS; render benches view, core app (the WiFi driver files are in `NOT_HERE` of `tests/coreapp/render/run.sh`), host app PASS.
The driver itself runs only on the board (S1-S4 open).

**D3 amended built (2026-11-06, board W.1 fail).** `host-app/software/hostapp/hostui.cc`: the WiFi page has a first row "WiFi" (`NS_RADIO`) that reads the driver state (On; Off for Disabled and for
the driver not running; No module) and, on RETURN / LEFT / RIGHT, calls `wifi_net_radio(on)` (on when the state is Disabled or Off), note "WiFi is on" / "WiFi is off"; nothing is stored. Messages
(`wf_unavailable`): Disabled = "WiFi is switched off - switch it on above (WiFi row, RETURN)", NO_MODULE = "There is no WiFi module" (unchanged), Off = "The WiFi driver is not
running" (`eWifi_Off` is the driver's state while its task does not run: before `WiFi::Init` and after `Terminate`, wifi.cc). Connect, D, L, F, M and S in those states give the message and start
nothing; S used to do nothing visible (the rescan is sent only in NotConnected / Connected). The header chip (`ip_string`) took the first interface with a link, so a cable with link and no address hid a
WiFi that was up; it now counts any interface with a usable address (the WiFi's from `wifi_net_status`, which applies `hasUsableAddress`), the cable's address first. Bench (`scenarios_t184`,
`tests/hostapp/render/render.cc`, stub `network_interface.h` with two scriptable interfaces, `wifi_net_radio` stub): radio row text per state, RETURN/LEFT/RIGHT call the radio with true, the
Disabled / Off / NO_MODULE texts, the five refused keys start nothing, chip with WiFi only / cached lease / cable first / cable only. Controls (`run.sh`, each a sed of `hostui.cc`, all red):
radiotrue, radiotext, disnote, offnote, nomodnote, apsilent, chipwifi. Screens: `t184_wifi_disabled.png` new; the WiFi page screens have one row more (the rows after it are one slot lower; the F8 walker
images follow). Row shifts in the checks: DHCP value is text 6, Static IP text 7. Size (builder, `u64-build-env:ci`): `hostapp.app` 902416 bytes (slot 1966068, 1063652 unused in this image), `coreapp.app` 1705040 bytes (261028 unused); `ci/apptests.sh hostapp coreapp loader` PASS, host render bench PASS with 7 new controls red (3, 1, 5, 1, 1, 1, 1 checks); the F or M or S keys of a switched-off page gate before their modal loops.

**Network page (P4, state 2026-11-05).** Two panels (Ethernet: link, FTP, media, REST; WiFi: state, "Connected to", IP, MAC and module version) and Gideon's five WiFi settings
as rows (Use DHCP, Static IP/Netmask/Gateway/DNS). Keys: UP/DOWN select, RETURN/LEFT/RIGHT change (DHCP toggles, a static row opens a line edit that checks four numbers 0-255),
S AP list (scan, RETURN connect, R rescan, M enter manually, D disconnect, F forget with a second F), M enter AP (SSID, authentication list, password; the password shows as stars),
L connect to the last AP, D disconnect, F forget. The jobs run on the `wifinet` task, the page only polls its status; with no module the keys explain it in a note and start nothing.
Compact modes (480p, 576p) use a 126 px label column and a two-line footer. A note about the module is cleared when its state changes; the entry dialog hides the AP list
while it is open and the list comes back after it. Bench: `scenarios_t184` in `tests/hostapp/render/render.cc` (screens `t184_*.png`, `modes-*-network.png`), PASS, and the RISC-V build of host app and core app.

**D4 built (2026-11-06).** Taken from the fork f7f665ab (D4 list above): reply slots with sequence check and timeouts (`wifi_reply.h`, `hw_esp32_rpc` in the core app on `wifi_reply_register`/`wifi_transmit`), frame size check, no-reply packet send, bounded scan decode (`wifi_scan_result.h`), rescan keeps the link, connect state only from the module's event (`wifi_connection_feedback.h`), SSID terminator and control bytes, 64-character password, usable-address check (`dhcp_supplied_address`), `wifinet` job state (busy, last result, reason shown in the network page note and the AP list: "The scan failed (no valid reply)", "failed (-1). Authentication failed."). Not taken: DNS from lwIP (item 14: not applicable, the page shows only the configured static DNS row, no learned DNS), the saved-AP RPCs (need the fork's changed bridge firmware; P5), the `cfg_forget` popup change. Patch `tools/patches/gideon-app/0001-t184-d4-wifi-fork-fixes.patch` on `vendor/gideon`, host-app copy via `tools/hostapp_vendor.py copy`.
Tests: `tests/hostapp/run_wifireply.sh` ok (fork's tests plus a wrong-command case, mutant controls red as they must; unsanitized fallback when ASan cannot start in the builder image); RISC-V build of host app (898996 bytes) and core app (1689264 of 1966068 bytes, check_app_size ok) without errors; map check `Esp32::Download|Flash` 0 in `hostapp.map` and `coreapp.map`; `ci/apptests.sh hostapp coreapp loader` PASS (211 s / 844 s / 10 s) with `pages_lint` exit 0; render bench 1247 checks, 0 failed (new `t184_aplist_failed.png` and the connect-failure note checked).

**UX review R6 (2026-11-06).** One name per state: the state line of F7 and of the WiFi page ("State", was "WiFi") says Off / No module like the radio row (was Disabled / No module detected); link states keep the driver's words (Link up, Link down, Scanning). The missing module reads "There is no WiFi module" in host and core. The word comes from one function, `wifi_state_word()` (wifinet.h), used by host F7, the WiFi page and the core summary line; `test_wifistate.cc` plus control `wifistate_raw` hold it.

**D5 built (2026-11-07).** `net_chip_text()` / `net_address_usable()` in `host-app/software/hostapp/wifinet.h` (one rule for both apps): usable cable address -> "LAN x", else usable WiFi address -> "WiFi x", else `NET_NONE_TEXT` ("no network", host and core alike; UX review 2026-11-07: the core's old "no link" named the same state differently, and `menuctl.cc` `link.` compares against the same constant). The core chip is muted when offline, as the host's. Host: `ip_string` (`hostui.cc`) walks the wired interfaces first, then the wireless ones (the WiFi's address from `wifi_net_status`), `c_lan` says which; `update_chips` draws the text. Core: `run.cc` `net_addresses()` reads the cable and the driver's WiFi address; `info(2)` (the Network line, the top-bar chip) prints through `net_chip_text`; `ip_string` (REST server) uses the same order.
**Cause of T187's .18 / .189 (board photo, A1200 F1 Home):** not a second interface. The top-bar chip is the Network info line cut by `chip_net[14] = 0` (`coreapp/src/ui.cc:116`, before this change); "192.0.2.189" is 15 characters, the cut left ".18". Status > Network (`netops.cc` `st_eth_ip`, the wired interface's `getIpAddrString`) printed the whole address. Both read the cable; the chip is now cut at 24 (the longest text, "WiFi 255.255.255.255", is 20) and the unit test checks that. Side effect found: the core's old `ip_string` took the first interface with link, WiFi included, with its raw interface address (a cached lease would show); now the cable first and the WiFi only with the driver's usable address.
Tests: `tests/coreapp/test_wifistate.cc` 16 checks (8 new: LAN only, WiFi only, both -> LAN, none, 0.0.0.0, null, length), controls `chip_wifi_first` (1 red), `chip_zero_ok` (2), `chip_no_name` (2). Host render bench `scenarios_t184`: chip text checked for WiFi only, none, both, LAN only, and both with the interface slots swapped; 1271 checks, 0 failed; controls `chipwififirst` (WiFi preferred when both are up, 7 red), `chipplain` (bare address, 5 red), `chipwifi` (1 red). Pictures `t184_chip_{lan,wifi,both,nonet}.png`. Builder (`u64-build-env:ci`, `ci/trxapps.sh`): apptests hostapp, coreapp (525 s), loader PASS, RISC-V `hostapp.app` 902652 bytes, `coreapp.app` 1705984 bytes. After the UX fix (NET_NONE_TEXT, f541e9841, 2026-11-07): "tests/coreapp: 103 test programs passed, 1513 parallel controls", host render 1271 checks, 0 failed, `ci/trxapps.sh` rc=0; `hostapp.app` 902652, `coreapp.app` 1706000 bytes. The core's `info(2)` itself is board code (BoardMenuHost): board check S5 open (core top bar shows "LAN 192.0.2.189" in full, same address as Status > Network).
