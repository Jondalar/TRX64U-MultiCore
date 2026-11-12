// The host application's answers on the shared network transport (T192.1 P6, Q5): the T106 page, GET /v1/status, GET /v1/report, the POST forms and the
// WebSocket endpoint /v1/ws, as functions of a parsed request. Pure - no socket, no RTOS; nettransport.h (coreapp/src) is the socket loop around it and
// the glue that starts it is at the end of webui.cc.
#ifndef HOST_WEBNET_H
#define HOST_WEBNET_H

#include "httpreq.h"

// The whole response (status line, headers, body) for a ready request into out[0..cap); -1 when it does not fit. *ws is set when the 101 of an
// accepted WebSocket upgrade is in it.
int webnet_serve(const HttpReq &q, char *out, int cap, bool *ws);
// The answer for a request that never became ready.
int webnet_serve_bad(const HttpReq &q, char *out, int cap);

// The seam for the token (T192.1 P1): asked for every /v1/ui/ request, the UI socket's upgrade included. The routes the host had before the service (the T106 page,
// /v1/status, /v1/report, the POST forms, /v1/ws) are open as they were; a build without the service (a bench) has no token and accepts everything.
bool webnet_authorized(const HttpReq &q);

// The UI service of the host (hostdomain.h); null: no /v1/ui/ routes. Set once, before the first request.
struct UicService;
void webnet_set_service(UicService *svc);
UicService *webnet_service();

// One /v1/ui/ws connection: a client session of the service. The transport closes it when `done` says so (after a `resync`).
struct UicClient;
UicClient *webnet_ui_open();
void webnet_ui_close(UicClient *c);
int webnet_ui_handle(UicClient *c, const char *msg, int n, char *out, int cap);
int webnet_ui_next(UicClient *c, char *out, int cap);
bool webnet_ui_done(UicClient *c);

// One WebSocket connection. `hello` is queued for the client as the first notification; a method call `hello` answers the same facts.
struct WebnetWs {
    bool hello_sent;
    char app[24], build[32], host_key[12];       // taken when the connection opens
};
void webnet_ws_open(WebnetWs *w);
int webnet_ws_handle(WebnetWs *w, const char *msg, int n, char *out, int cap);
int webnet_ws_next(WebnetWs *w, char *out, int cap);

#endif
