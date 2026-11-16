# T200 — Nothing on the LAN changes the board, starts code or reads the machine without the token the person granted on the board

**Status:** PROPOSED (2026-11-10, Alex: "make a security spec out of it; we can tackle it step by step", translated). Nothing decided yet. 2026-11-15 sync: no P1-P7 build exists; F4 and F6 were checked on a tree from before T192.3 D31 (`bfcd0211c`, 2026-11-10 21:06, 22 min after the 3887b1dbf check): the token gate now runs in every core of the generic app, see the F4/F6 rows.
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp/`: `webnet.cc`, `webui.{h,cc}`, `host_core_flow.cc`, `txu.cc`), core app
(`coreapp/src/`: `rest.cc`, `uiapp.cc`, `monsrv.{h,cc}`, `update.cc`), the shared UI service (`uicore/`), release tooling (`tools/github_snapshot.py`,
`tools/release_mc.sh`).
**Number:** T200 (registry: `TODO.md`).
**Origin:** a tester's review of snapshot 07f88e9 (2026-11-06, in his own fork, `docs/upstream-issue-candidates.md` and
`docs/adversarial-review-2026-11-06.md`), checked against `main` 3887b1dbf on 2026-11-10 by the coordinator (read only, file:line below).
**Depends on:** T192 (UI service, token, `uic_gate`), T152 (`.txu` update), T106 (host web UI).
**Milestone:** M2.

---

## §1 The problem

- **Who:** anyone on the same LAN as the board, and any web page open in a browser on that LAN (a page can send requests to the board's address).
- **What should hold:** changing the board (menu, default core, settings, flash, media) and reading or writing the running machine need the token
  that was granted on the board; a fresh board does not hand its token to the first client that asks.
- **What happens today (code, 2026-11-10):**

| # | Finding | State | Evidence |
|---|---|---|---|
| F1 | Host web routes change the board without a token: `/v1/boot`, `/v1/menu/remove`, `/v1/default`, `/v1/countdown`, `/v1/video` | open | `host-app/software/hostapp/webui.cc:340-372`; `webnet.cc:93-96` gates only paths under `/v1/ui/` |
| F2 | The header of the host web UI says the opposite ("nothing here touches the menu, the settings or the picture") | open, wrong comment | `host-app/software/hostapp/webui.h:8` |
| F3 | `POST /v1/ui/grant` is public: on a board with no token yet, the first LAN client that calls it gets the token | open (new with T192) | `uicore/uic_svc.cc:413-430` (refused only when a token or token file exists) |
| F4 | The token gate of the core app existed only for the Amiga 1200 and TRX64U | built since by T192.3 D31 (2026-11-10): `service_core` is true for every core with a descriptor; still no token (auth off) for a core with no core folder (`uiapp.cc` `file_save = 0`); S3 has no recorded test | `coreapp/src/uiapp.cc:1129-1132`, `:1146-1147`, `coreapp/src/rest.cc:527` |
| F5 | The monitor RPC on TCP 4312 has no token; while the UI service runs only some methods are refused (`monitor/exec`, `checkpoint/restore`, `transport/goto`, `mark/goto`, `runtime/reverse_step`, `media/*`, `sandbox/*`); memory read/write is not in that list (the full method list of `trxmon/tm_rpc.c` not yet read) | partly | `coreapp/src/monsrv.cc:171-200`, `monsrv.h:27-35` |
| F6 | `update.run_record` runs a `.cfw` with no signature (fingerprint, product family, product and record bounds only); behind the token and an on-screen question in every core with a core folder (D31); the host app's own `.txu` route was not re-checked | partly | `coreapp/src/update.cc:60-100`, `coreapp/src/uiapp.cc:153` |
| F7 | The "Network Password" setting of the vendor code protects none of our REST/WebSocket routes (no `X-Password` check in `coreapp/` or `host-app/`) | open, by design since T192 (the token replaces it) | grep; `vendor/gideon/.../network_config.cc:18` |
| F8 | The `.txu` host update writes the flash with no backup first | open | `host-app/software/hostapp/host_core_flow.cc` (~740-773, `fw_write_parts`, no backup step) |
| F9 | A release names a commit (`trx64_commit` in the manifest) under its Gitea sha; on GitHub it is the commit `release/github_map.txt` maps it to (T120 D-history; the public history is the filtered replay of Gitea's); `SHA256SUMS` comes from the same release, nothing is signed | open, by design (T120) | `tools/release_mc.sh`, `tools/github_snapshot.py --map-lookup` |
| F10 | FTP: the review says any user with an empty password is accepted | not checked yet | — |

Not a finding: an app-only `.txu` skips the IDCODE check (`txu.cc:216-221`) because it carries no bitstream to check.

- **Why it matters:** F1 and F3 let a web page in a browser on the LAN start or remove menu entries, change the default core and the video mode, or take
  the token; F4/F5 let a LAN client read and write the running machine; F6/F8 can end in a board that needs JTAG.

Parts:
- **P1 Host routes:** every host route that changes something needs the token (F1, F2).
- **P2 Grant:** the first token is handed out only with a confirmation on the board (F3).
- **P3 Cores:** the token gate in every core app, not only A1200/TRX64U (F4).
- **P4 Monitor:** the monitor socket needs the token, or its write methods are refused like the others (F5).
- **P5 Code and flash:** what may run or be flashed from the network (F6), and a way back after a `.txu` (F8).
- **P6 Provenance:** a release names something the public can check (F9).
- **P7 FTP:** what the FTP server accepts (F10).

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | each of `/v1/boot`, `/v1/menu/remove`, `/v1/default`, `/v1/countdown`, `/v1/video` without a token: refused (401), nothing changed; with the token: works as today | host app test over `webnet_authorized` and the route table | apptests |
| S2 | P2 | a board without a token: `POST /v1/ui/grant` shows a question on the screen; no answer or "no" = refused, "yes" = token given; with a token present = refused as today | uicore test + host emu | apptests, emu |
| S3 | P3 | VIC-20 (a core outside H3): `/v1/ui/state` and a destructive REST route without a token are refused | core app test with the gate on for every core | apptests |
| S4 | P4 | monitor socket: a memory write without the token is refused; with it, works | core app test | apptests |
| S5 | P5 | to be written after the decision | — | — |
| S6 | P6 | to be written after the decision | — | — |
| S7 | P7 | FTP login with an empty password: result recorded, then as decided | board | board |

## §3 Decision

(none yet; one question at a time with Alex, in the order P1, P2, P3, P4, P5, P6, P7)

## §4 Out

- Encryption (TLS) on the LAN.
- The vendor firmware (`trx64g`, Gideon's application): untouched by rule.

## §5 Build notes

(empty)
