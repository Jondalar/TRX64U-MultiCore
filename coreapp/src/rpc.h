// The WS JSON-RPC adapter of the service layer (T104.1 D3, Q2: "follows after day 1 as a second thin adapter"): JSON-RPC 2.0 messages in text frames.
// The method name is the operation name, the params are the same object REST takes, events are notifications with the event name. Pure: a message
// in, a message out; wsproto.h does the framing and restsrv.cc the socket.
//
//   {"jsonrpc":"2.0","id":1,"method":"settings.set","params":{"key":"Sound","value":"On"}}
//   -> {"jsonrpc":"2.0","id":1,"result":{"code":0,"msg":"Sound: On","more":false}}
//   an operation that ran and refused is an error object whose code is the OpErr (-1 .. -7), the protocol's own codes are -32700 .. -32601:
//   -> {"jsonrpc":"2.0","id":1,"error":{"code":-7,"message":"..."}}
//
// The methods of the protocol itself (no operation is called) are:
//   service.ops {from}            the operations with their argument types
//   service.state {name}          one state value; without name: the list
//   service.subscribe {event}     notifications {"jsonrpc":"2.0","method":"<event>","params":{..}} follow; service.unsubscribe {event} ends them
// Contract method names of T24-ui-wiring section 8 are in the alias table: they are data, the adapter has no code per operation.
#ifndef COREAPP_RPC_H
#define COREAPP_RPC_H

#include "ctx.h"
#include "svcjson.h"

#define RPC_SUBS_MAX 4                    // events one connection listens to
#define RPC_QUEUE 4                       // notifications waiting for the connection's task
#define RPC_NOTE_MAX 320
#define RPC_RESULT_MAX 1400

// Whoever emits an event is some task; whoever sends is the connection's. The queue is guarded by what the board provides (restsrv.cc sets a
// critical section); a bench has no second task and sets none.
class RpcLock {
public:
    virtual ~RpcLock() { }
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

struct RpcSession {
    CoreCtx *c;
    RpcLock *lock;
    int nsub;
    char sub[RPC_SUBS_MAX][40];
    char q[RPC_QUEUE][RPC_NOTE_MAX];
    int qhead, qcount;
    int dropped;                          // notifications that found the queue full
};

void rpc_open(RpcSession *s, CoreCtx *c, RpcLock *lock = 0);
// The end of the connection: its subscriptions are taken back.
void rpc_close(RpcSession *s);

// Handles one text message. Writes the response into out[0..cap) and returns its length; 0 when there is none (a notification that carries no id);
// -1 when the response does not fit.
int rpc_handle(RpcSession *s, const char *msg, int n, char *out, int cap);

// The next waiting notification (an event of a subscribed name), or 0.
int rpc_next(RpcSession *s, char *out, int cap);

struct RpcAlias {
    const char *method;
    const char *op;
};
extern const RpcAlias rpc_aliases[];
extern const int rpc_naliases;

#endif
