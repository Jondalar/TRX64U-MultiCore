// The network side of the service layer (T104.1 D3, Q8: "in the base, always available even when a loadable application or library fails"): one
// listening task on port 80 that serves REST (rest.h) and, for a client that asks to upgrade /v1/ws, WS JSON-RPC (rpc.h). Board code - lwIP sockets and
// FreeRTOS tasks; everything it decides is in the pure files, which tests/coreapp/ runs.
#ifndef COREAPP_RESTSRV_H
#define COREAPP_RESTSRV_H

#include "ctx.h"

#define RESTSRV_PORT 80
#define RESTSRV_CLIENTS_MAX 3             // connections served at once; a fourth waits in the listen queue

// Starts the task; it waits for the network and does nothing twice. `c` is the core application's context (its registry and executor); `net_up` says
// whether a link is up and has an address (run.cc's ip_string): the task listens only then.
void restsrv_start(CoreCtx *c, bool (*net_up)(char *buf, int len));

#endif
