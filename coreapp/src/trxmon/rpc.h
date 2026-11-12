/*
 * rpc.h — JSON-RPC 2.0 over the TRX64 runtime contract (T21-monitor.md
 * §7.4): method names, parameters and result shapes are TRX64's
 * (crates/trx64-daemon/src/main.rs dispatch_request).
 */
#ifndef TRXMON_RPC_H
#define TRXMON_RPC_H

#include <stdint.h>

#include "mlib.h"

/* trxmon's own build id: `git rev-parse --short HEAD` plus "-dirty" when the tree had uncommitted changes,
 * injected as -DTRXMON_BUILD=\"...\" by the Makefile (host tests: "test") */
#ifndef TRXMON_BUILD
#define TRXMON_BUILD "unknown"
#endif
#define TRXMON_VERSION "trxmon " TRXMON_BUILD
/* a second RPC connection is refused with this JSON-RPC error code */
#define RPC_BUSY_CODE     -32001
#define RPC_BUSY_CODE_STR "-32001"
#define RUNTIME_VERSION "trx64-runtime/2"

/* The server side (main.c; the host tests capture it) */
void srv_send(int cid, const char *text, int len);
void srv_broadcast(const char *text, int len);
/* Frames sent while corked leave in one packet when it is released (the log notifications) */
void srv_cork(int on);
/* Milliseconds since the last byte came from the RPC holder; UINT32_MAX when there is none */
uint32_t srv_holder_idle_ms(void);
/* The firmware's git hash (what GET /v1/version calls git_commit_hash) and its version number; "" when the
 * firmware's app API does not tell */
const char *plat_firmware_git(void);
const char *plat_firmware_version(void);
/* The file behind a medium: role 0 cartridge, 1 drive A. 0 when empty. */
int plat_media(int role, char *buf, int len);

void rpc_init(void);
/* One text message from client cid */
void rpc_request(int cid, const char *text, int len);
/* Stops that came, run-tos that took too long: call often */
void rpc_poll(void);
void rpc_client_gone(int cid);
int rpc_quit_requested(void);

/* The person at the screen (ui.c, T21 §8). Runs a line as the screen's own: narrow formatting,
 * source "human". MON_PENDING: a run-to verb waits for its stop; rpc_local_take gives its text. */
int rpc_local_exec(const char *cmd, sbuf_t *out);
int rpc_local_take(sbuf_t *out); /* the text of the finished run-to, once; 0 when none */
int rpc_local_waiting(void);
/* A stop came that no run was waiting for (a `g` ran into a breakpoint, an observer): once */
int rpc_take_unclaimed(void);
/* A breakpoint or an observer stopped the machine (whoever was waiting): once per stop */
int rpc_take_user_stop(void);
/* Any breakpoint, observer, drive comparator or cycle compare armed: a stop can still come */
int rpc_stops_armed(void);

#endif
