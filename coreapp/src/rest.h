// The REST adapter of the service layer (T104.1 D3, Q2, Q8): HTTP/1.1 requests in, the registry's operations and state values out. Pure - the bytes
// come and go through the caller, so tests/coreapp/ drives it without a socket, and restsrv.cc (board only) is the socket loop around it.
//
//   GET  /v1/info                       what this is: the core, the counts
//   GET  /v1/ops[?from=N]               the registered operations with their argument types, in pages
//   GET  /v1/ops/<name>                 one operation
//   POST /v1/ops/<name>                 calls it; arguments as a JSON object, a form body or the query: {"code":0,"msg":"..","more":false}
//   GET  /v1/state[?from=N]             the state values, in pages
//   GET  /v1/state/<name>               one state value with its revision
//   GET  /v1/events                     the events a WS client can subscribe to
//   and the alias table below: the paths our board tests, deploy scripts and JTAG tools use today (Q2), each one naming an operation.
//
// The adapter has no operation names of its own except in the alias table. A native route takes the name from the path, so an operation a library
// registers tomorrow is reachable without a line changed here (S2).
#ifndef COREAPP_REST_H
#define COREAPP_REST_H

#include "ctx.h"
#include "svcjson.h"
#include "httpreq.h"

// The whole response (status line, headers, body) for a ready request in out[0..cap); returns its length, or -1 when it does not fit. *ws is set
// when the request was a WebSocket handshake that was accepted: out then holds the 101 and the caller speaks frames from here.
int rest_serve(CoreCtx *c, const HttpReq &q, char *out, int cap, bool *ws);

// T228 R21 row 58, as TRX64-Ultimate's REST (api/routes.h:325-335): when the source returns a non-empty password, a request without the header
// `X-Password: <it>` is answered 401 before any route (the token gate included). The WebSocket of the UI (/v1/ui/ws) is checked by its token alone: a
// browser cannot set the header on a socket. No source (the benches), or an empty password: nothing is asked.
void rest_set_password_source(const char *(*get)(void));

// The start gate (T192.3 section 5, board 2026-11-09): the application opens its port before its start sequence, which can wait for as long as it
// takes (a missing ROM, the medium), so a client reaches it then and learns why. While the gate is on, every request is answered 503
// {"code":-1,"msg":<what the start is doing>,"starting":true}, the service untouched. rest_starting turns it on and names the step (copied; ignored
// once rest_started ran); rest_started turns it off for good, after the UI service and its token gate are up. The benches never turn it on.
void rest_starting(const char *what);
void rest_started(void);
bool rest_start_gate(void);                       // true while the gate answers 503: the transport serves one client from static buffers

// T162: GET /v1/report. The application sets this to diag_report_rest (the text into body, its length, -1 when busy or too small; the file name for the
// download in stamp); null (the benches, a build without it): the route answers 501.
extern int (*rest_report)(CoreCtx *c, char *body, int cap, char *stamp, int stamplen);

// T192.1 MVP: the UI service (uicore) of the application, set by uiapp_init; null (the benches, a build without it): no token, no /v1/ui/ routes. With it, every request passes uic_gate first, and the destructive operations ask (D19a).
struct UicService;
extern UicService *rest_uic;
extern bool (*rest_ui_op_destructive)(const char *op);
extern bool (*rest_ui_op_ask)(const char *op, const char *query, const char *body, int body_len, char *reply, int cap, int *status);
// A POST /v1/ops/<name> as REST would run it, from the stored query and body (the menu task runs a destructive call after its question was answered).
// Every operation a network path calls (REST, the JSON-RPC sockets) goes through this and not through svc_call: a destructive one does not run, it fails with the way to ask
// (D19a, one gate for every route). Without the service nothing is destructive and it is svc_call.
int svc_call_net(CoreCtx *c, const char *name, const JsonObj *args, OpResult *r);
int rest_op_exec(CoreCtx *c, const char *name, const char *query, const char *body, int body_len, OpResult *r);

// The answer for a request that never became ready (HS_BAD): a status and a short JSON body.
int rest_serve_bad(const HttpReq &q, char *out, int cap);

// The alias table is data; tests read it too.
struct RestAlias {
    const char *method;                   // "PUT"
    const char *pattern;                  // "/v1/machine:reset", or segments with {name} (becomes the argument `name`) and {_} (ignored)
    const char *ops;                      // "prg.run|c64.run_prg": the first one registered is called
    const char *from;                     // an argument the client names `from` is `to` for the operation; null: none renamed
    const char *to;
};
extern const RestAlias rest_aliases[];
extern const int rest_naliases;

#endif
