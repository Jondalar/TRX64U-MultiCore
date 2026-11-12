/*
 * app_api_net.h
 *
 * Firmware side of the ABI 2.1 tables `net` and `trx` (app_api_net.cc).
 */

#ifndef APP_LOADER_APP_API_NET_H_
#define APP_LOADER_APP_API_NET_H_

#include <stdint.h>
#include "app_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const u2p_net_api_t g_app_net;

/* The trx table, or NULL when the bitstream has no TRX64 debug block. */
const u2p_trx_api_t *app_trx_table(void);

/* The TCP port an app serves the TRX64 runtime contract on, 0 when none. */
uint16_t app_trx_rpc_port(void);

/* Whether an app's detached task (trx->detach) runs: the loader then starts
 * no other app and leaves the cleanup to that task. */
bool app_resident_active(void);

/* Asks the detached task to end (trx->stop_requested) and waits up to
 * timeout_ms for it; true when no detached task runs any more. */
bool app_resident_stop(uint32_t timeout_ms);

/* The manifest id of the app the loader starts now ("" without a manifest),
 * and of the app whose detached task runs. */
#define APP_ID_MAX 31
void app_set_current_id(const char *id);
const char *app_resident_id(void);

/* Called by the loader when an app has returned. */
void app_net_cleanup(void);
void app_trx_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOADER_APP_API_NET_H_ */
