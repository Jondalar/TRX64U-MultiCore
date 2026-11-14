# T163 — the host's and the cores' settings live in our own store, apart from Gideon's

**Status:** PACKAGE A BUILT (2026-10-30), asked by Alex. Status check 2026-11-13: PACKAGE A, S3 and S4 built and merged (0f79218f4, 34f7cd231); board FTP step and integration run open.
**Repo:** TRX64U-MultiCore — host app (`host-app/software/components/config.cc`, `network/`), generic core app (`coreapp/src/netops.cc`).
**Number:** T163.
**Origin:** Alex, 2026-10-30, after FTP stayed off on the host following a stock 1.1.0 test (translated): "that is a fat bug in my
opinion. Two things really ... it is ok that we read Gideon's settings at the first start. But I do not want it like this! Our host
and core settings MUST be decoupled from his."
**Depends on:** nothing.
**Milestone:** M2.

---

## §1 The problem

- **Today:** the host app keeps its settings in the SPI flash config pages (`config.cc`, `read_config_page`/`write_config_page`),
  keyed by store name ("Network settings" etc.) - the same pages and names Gideon's application and Commodore's 1.1.0 use. After
  the board ran stock 1.1.0 (2026-10-30), our host came up with FTP off: `ftpd.cc` reads `CFG_NETWORK_FTP_SERVICE` from that page,
  our default (1, `network_config.cc:24`) never applied. The host has no menu to change it.
- **P1 decoupling (Alex):** our host and our cores keep their own settings, in a place Gideon's and Commodore's firmware never
  write; their firmware never changes ours, ours never changes theirs.
- **P2 first start (Alex: "it is ok that we read Gideon's settings at the first start"):** with no store of ours yet, the values
  may be taken over once from Gideon's pages (read only); after that only our store counts.
- **P3 the Network page shows the truth:** `hostui.cc:1469` prints "on, port 21, no password" whenever the network is up, without
  looking at the setting or the listener. It must show the real state (and the host needs a way to switch FTP).

## §2 Scenarios

```gherkin
Scenario: S1 foreign firmware does not change ours
  Given our host with FTP on in our store
  When  Gideon's or Commodore's firmware writes "FTP File Service = Disabled" into its config pages
  Then  our host starts with FTP on, port 21 open
  # board: 1.1.0 run then our host; bench: config store test with the foreign page set to 0 -> control reads the foreign page -> FAIL

Scenario: S2 first start takes over once
  Given no store of ours, Gideon's pages present
  Then  our store is created from them once; a later change in Gideon's pages is not seen

Scenario: S3 the Network page tells the truth
  Given FTP off in our store
  Then  the Network page shows FTP off (not "on, port 21")
  # bench: render bench scenarios_t184 (off / on / password / no network); controls ftphard, ftpnonet, ftpnooff go red

Scenario: S4 FTP is switchable from the Settings page (F8)
  Given the Settings page with the row "FTP service"
  When  RETURN or RIGHT is pressed on it
  Then  the store item changes and is written/effectuated once per press, the row and the F7 row show the new state
  # bench: same scenario; controls ftpnoset (row does not write), ftpwrong (writes the wrong value) go red
  # board: switch off in F8, FTP port 21 refuses; switch on, a client connects
```

## §3 Decision

- **D1 (Alex, 2026-10-30): decoupled.** Host and core settings must not share Gideon's config store.
- **D2 (Alex, 2026-10-30): reading Gideon's settings once at the first start is allowed.**
- **D3 (Alex, 2026-10-30): order and scope.** "Let us do the core things and the HDMI etc., and after that do the host app properly once:
  take the generic app as the framework, smooth the UI once, isolate the settings, etc." (translated). T163 is part of that host app
  rework, which comes after the open core and HDMI work (T107, T159, T140, T89).
- **D4 (Alex, 2026-10-30, "Ja"):** in the rework the menu is drawn natively at the HDMI mode's resolution (720p at 1280 x 720), not as a
  960 x 540 canvas scaled by whole factors (T107 §5.9).
- **D5 (Alex, 2026-10-30: "a"):** our store is a file on `/Flash`, `/Flash/trx/host.cfg`, in the same `[section]` text format as the
  cores' `.cfg`. Not own config pages: they would share the flash area with Gideon's pages, and an update of either could overwrite
  the other's. Gideon's firmware and 1.1.0 never read the file; it works without an SD card and can be copied over FTP.
- **Split (T164 D3):** package A builds the store, S1 and S2; P3 (the Network page shows the truth, FTP switchable) is package B's UI rework.

## §4 Out

- Changing Gideon's or Commodore's firmware or their settings.

## §5 Decisions taken while building

Package A built (host app only; S3/P3 stay with package B).

- `components/config.{cc,h}` (vendored, patches `host-app/patches/components_config.*.patch`, listed in MODIFIED): `ConfigBackend` and
  `config_set_backend()`. With a backend set, every `ConfigStore` loads from and saves to the backend's file; the flash pages are never
  written. `hostapp/hostcfg.cc` is the backend: `/Flash/trx/host.cfg`, `[section]` text via `coreapp/src/cfgfile.cc`, installed first thing
  in `hostapp.cc` (`hostcfg_install_board()`, I/O in `hostcfg_io.cc`). `ftpd.cc` is unchanged and reads `CFG_NETWORK_FTP_SERVICE` from the store; default on.
- Take-over (D2): a store with no section in a **new** file (no host.cfg) is read once from Gideon's flash page (read only) and the file is
  saved at once. A store without a section in an existing file gets defaults. A file that cannot be read counts as not new: defaults, nothing written.
- Bench `tests/hostapp/run_hostcfg.sh` (called by `run.sh`; real `ConfigManager`, fake SPI flash and file). Report (builder, trx64-sim-env:4):
  `26 checks, 0 failed`, `run_hostcfg: ok`. Controls, each red as it must: s1_foreign_always (10 checks red), s1_scan_at_register (3),
  s2_no_takeover (8), s2_takeover_late (1), unreadable_is_new (2), takeover_not_saved (6), write_to_page (4), page_file_fallback (1).
- Also fixed: `host-app/patches/filesystem_blockdev_flash.cc.patch` was missing since T139 (6f69214e3), so `hostapp_vendor.py copy` stopped; regenerated.
  `copy` now reproduces the tree (only PROVENANCE.md differs, committed).
- Not done here: the riscv build of the host app (needs the u64-build-env container, not available on the builder), the board and the ue2emu FTP probe.

Package B (late integration, 2026-11-08): S3 and S4 built in the host app.
- `ftpstate.h` (`ftp_state_text`: off beats a missing network, on waits for an address, on shows port and password) and `ftpstate_board.cc`
  (reads and sets `CFG_NETWORK_FTP_SERVICE` / `CFG_NETWORK_PASSWORD` in the `networkConfig` store, which `hostcfg` keeps in host.cfg;
  set = `write()` + `effectuate()`, ftpd.cc follows its setting). The F7 row and the new F8 row "FTP service" use the same text.
- F8 now has ten rows; the pitch shrank (37 -> 34 normal, 32 -> 29 compact) so they fit in the height nine had. Changed screens: F7 Network, F8 Settings.
- Bench: render bench scenarios_t184 (CHECKs) and modes_pages (pictures network-ftp-off, network-ftp-pw); five controls in `tests/hostapp/render/run.sh`.
- Not run here: render bench (integration), board step (S1 board run, F8 switch with a real FTP client).
