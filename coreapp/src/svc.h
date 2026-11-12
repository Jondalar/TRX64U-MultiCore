// The service layer's door for transports (T104.1 D3): REST and WS JSON-RPC are adapters that turn text into the typed call the registry takes and the
// result back into text. This file is what they share; neither of them names an operation (S2): the names come from the request, the types from what
// the contributors registered. A call goes through ops_call, so it runs on the executor like the menu's calls do.
#ifndef COREAPP_SVC_H
#define COREAPP_SVC_H

#include "ctx.h"
#include "svcjson.h"

// Calls operation `name` with the members of `args` (null: none). The members are matched to the operation's registered arguments by name and
// handed over as text or numbers for the registry to check against their types. A member the operation has no argument of, a blob argument, a text
// where a number is wanted - each is OPE_ARG with the reason in r->msg, and nothing runs. An unknown operation is OPE_NOT_FOUND.
int svc_call(CoreCtx *c, const char *name, const JsonObj *args, OpResult *r);

// The HTTP status a result code is carried as by REST (200 for OPE_OK).
int svc_http_status(int code);

// JSON writers; each returns false when the text does not fit the buffer (*pos unchanged then).
// {"code":0,"msg":"...","more":false}
bool svc_put_result(char *out, int cap, int *pos, const OpResult &r);
// One operation: {"name":"..","owner":"..","args":[{"name":"..","type":"int","min":0,"max":9}, ...]}
bool svc_put_op(char *out, int cap, int *pos, const OpDef &d);
// One state value: {"name":"..","owner":"..","value":"..","rev":3} (value null: nothing)
bool svc_put_state(CoreCtx *c, char *out, int cap, int *pos, const StateDef &d);
bool svc_put_event(char *out, int cap, int *pos, const EventDef &d);

// A result of this layer's own (not an operation's): code and sentence.
void svc_fail(OpResult *r, int code, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif
