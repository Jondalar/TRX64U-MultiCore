# T186 — The core application's FTP client survives servers behind NAT, dead connections and late transfer replies

**Status:** BUILT in the code and the Mac tests (2026-11-06), S1-S3; RISC-V build and app tests: see §5; S4 (board) open. Status check 2026-11-13: BUILT and in main (476da487f); S4 (board, with T184 S3) open.
**Repo:** TRX64U-MultiCore — core app only (`coreapp/src/ftp_client.cc`, the `/ftp` mount of NetFs).
**Number:** T186.
**Origin:** T184 D4 (2026-11-05): the evaluation of the fork omnicron-c64/ultimate-community-edition f7f665ab (GPL-3) listed three FTP
client fixes that are not WiFi work and were set apart ("FTP client fixes ... = separate, not this task",
T184 notes). Alex, 2026-11-06 (translated): "finish all branches" — this is the open item from that list.
**Depends on:** T184 (network over WiFi), the NetFs `/ftp` mount.
**Milestone:** M3.

---

## §1 The problem

A person browses an FTP server from a core's file browser (`/ftp`). Three faults of the client break this in common setups:

- **P1 Passive address:** the client connects the data channel to the address in the `227` reply. A server behind NAT (a home NAS
  reached through a router, many hosted servers) reports its private address; the data connection then goes nowhere. The fork uses the
  control connection's peer address instead, which is what every common client does.
- **P2 Dead control connection:** after the server closed the connection (timeout, network loss), the client still sends `QUIT` on the
  dead socket and waits; the browser hangs until the timeout instead of reconnecting.
- **P3 Late final reply:** after a transfer the final `226`/`250` can arrive after the data channel closed; a client that does not read
  it takes it as the reply of its next command, and every reply after that is off by one.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | `227` carries 10.0.0.5, the control peer is 192.168.1.20: the data connection goes to 192.168.1.20 at the given port | coreapp test with a scripted server model; control: the old address use must fail | `tests/coreapp/run.sh` |
| S2 | P2 | the server closes the control connection; the next operation reconnects, no `QUIT` is written to the dead socket, no wait | coreapp test; control | `tests/coreapp/run.sh` |
| S3 | P3 | the `226` arrives after the data channel closed; the next `LIST`/`RETR` reads its own reply, listing and file intact | coreapp test; control | `tests/coreapp/run.sh` |
| S4 | P1-P3 | browse and load a file from a real FTP server on the LAN through `/ftp` in a core | board, with T184 S3 | board session |

## §3 Decision

Take the three fixes as the fork made them, adapted to `coreapp/src/ftp_client.cc`; GPL-3 notice and a provenance line naming the fork
and f7f665ab where code is taken.

## §4 Out

- The fork's `filesystem_ftp` rewrite and its saved-AP WiFi RPCs (T184 D4).
- SFTP.

## §5 Build

**Comparison first.** The fork's `ftp_client.cc` is a complete rewrite (non-blocking connect, EPSV/MLSD, progress observer, error texts); our file is the 1541ultimate client
(Alex's pull request #662 to GideonZ/1541ultimate, merged 2026-03-17; #695 of 2026-05-23 put it into the virtual file system) with the NetFs as its only caller. The three fixes were taken as ideas and written into our file; no fork code was copied
as a block. The provenance lines in `ftp_client.cc`/`.h` name the fork and f7f665ab; the GPL-3 notice stays (both are GPL-3).

**What changed (`coreapp/src/ftp_client.cc`, `.h`).**
- P1: `open()` keeps the resolved control peer (`peer_ip`); `open_data_connection()` connects there and takes only the port from the `227` reply.
- P2: `drop_control()` closes the control socket without `QUIT`; a failed `recv`/`send` on the control connection, an overflowing reply and a
  failed transfer call it. A command on a dropped link returns -1 at once (no `send` on fd -1). `disconnect()` writes `QUIT` only on a link still
  believed alive and does not wait for the `221`. NetFs `client()` already reconnects when `is_connected()` is false, so it needed no change.
- P3: our client already read the `226`/`250` after closing the data channel; what was missing were the paths that did not leave the link in step:
  `STOR` send error (no reply read, the server's `426` stayed queued), `RETR` local failure, a reply timeout (the late `226` arrived later and
  became the next command's reply). `finish_transfer(fd, ok)` closes the data channel, drops the control link when the transfer was broken off
  (`!ok`) or the final reply is not `226`/`250` or timed out, otherwise reads the final reply. Also: a `STOR` whose local file cannot be read is
  now an error, not a short upload reported as done.

**Tests.** `tests/coreapp/test_ftp.cc` (65 checks): `ftp_client.cc` unchanged against a scripted server model, lwIP calls faked in `tests/coreapp/ftpstub/`,
virtual clock (a receive timeout costs 10 s of model time). `tests/coreapp/run.sh`: test `ftp` and five controls (each sed puts one change back to the old behaviour).

| Scenario | Checks | Control (sed in `ftp_client.cc`) | Red checks |
|---|---|---|---|
| S1 data to 192.168.1.20:1025 although 227 says 10.0.0.5 (LIST and RETR) | connect log ip/port | `ftp_pasv_ip`: `peer_ip` -> `ip` | 2 |
| S2 silent peer: one 10 s timeout, then no traffic on the dead link, no QUIT on it, reconnect without wait; FIN case; open() on an undetected dead link: no wait | log, virtual clock | `ftp_dead_kept`: read_response failure does not drop | 6 |
| S2 `QUIT` is not waited for | clock | `ftp_quit_waits`: `send_cmd("QUIT")` in `disconnect()` | 1 |
| S3 broken `STOR` (server 426 pending), unreadable local file: link not reused, no command into the pending reply, reconnect in step | log | `ftp_broken_transfer`: `finish_transfer` ignores `!ok` | 2 |
| S3 `226` 4 s late read as the transfer's own (LIST, RETR, STOR; next PWD is 257); `226` 15 s late: transfer fails, link not reused | replies, listing and file bytes | `ftp_late_226`: final reply not read | 15 |

The test run: 65 checks, 0 failed; all five controls fail as they must (run of `run.sh`'s T186 block alone, Mac, niced).

**RISC-V / host tests.** Builder, `u64-build-env:ci`, CI cache mounted, `ci/trxapps.sh` on the code commit (tree of 476da487f), 20 min 20 s, rc 0. Host app and core app build for RISC-V;
`check_app_size.py`: `coreapp.app: 1705080 bytes of data (dest 0x30000, run 0x30000); the slot takes 1966068, 260988 of it unused in this image`.
`ci/apptests.sh`: hostapp PASS (46 s, units from the cache), coreapp PASS (902 s; 100 test programs incl. `ftp`, 1498 controls, all failed as they must; the five
`ftp_*` controls red with 2/6/1/2/15 checks), loader PASS (2 s). Render benches (view, core app, host app) PASS from the cache: no rendered screen changed.

**Open / not verified.** S4: browse and load from a real FTP server through `/ftp` on the board (with T184 S3). The fake server models replies and timing,
not a real TCP stack: a real server behind NAT and a real dead link are the board's part.
