// The TRX64 runtime contract for the WS JSON-RPC door (T104.1 section 5.8, Alex 2026-10-26: "C64RE RPC compatible"). The source of the contract is the
// TRX64 daemon (/path/to/Development/C64/Tools/TRX64, crates/trx64-daemon/src/main.rs, the dispatch of `handle_request`, RUNTIME_VERSION
// "trx64-runtime/2"), which C64RE's daemon-client speaks; T21 7.4 and T24 8 describe it for this machine. Every one of its 158 methods is a row of this
// table: either answered here (a CK_ kind that is not CK_NO_*), or answered with JSON-RPC error -32601 and a sentence that names the method and the
// way out. Nothing is left to fall through to "unknown method" by accident. The table is data; the mapped methods call operations of the registry.
// The registry is asked first (rpc.cc): a method that an operation of a linked library answers ("session/state" is the operation "session.state") is that
// library's, and only a method no operation answers reaches this table.
#ifndef COREAPP_RPCCONTRACT_H
#define COREAPP_RPCCONTRACT_H

#include "rpc.h"

enum ContractKind {
    // answered by this application
    CK_PING, CK_KEEP, CK_CREATE, CK_LIST, CK_CLOSE, CK_STATE, CK_READ, CK_RESET, CK_POWER, CK_LOAD, CK_RUN, CK_MOUNT, CK_UNMOUNT, CK_OPEN, CK_DRIVERESET,
    // -32601, by reason
    CK_NO_SANDBOX,   // only the emulator can do it (history, scenarios, recorders, VIC inspection, audio export, batches)
    CK_NO_RING,      // needs the Timewarp ring (T20): no operation of this core answers
    CK_NO_TRACE,     // needs the trace engine
    CK_NO_DEBUG,     // the debugger and the monitor are a core library's: no operation of this core answers
    CK_NO_PACING,    // the machine runs in real time
    CK_NO_INPUT,     // no operation of the core presses a key or moves a joystick
    CK_NO_MEDIA,     // the browser of the menu does it
    CK_NO_DEVICE     // set by settings and by the drive library's operations
};

struct ContractRow {
    const char *method;
    uint8_t kind;
};
extern const ContractRow rpc_contract[];
extern const int rpc_ncontract;

// The reason a -32601 row gives, as a sentence for the method.
const char *contract_reason(uint8_t kind);

// Handles `r` if its method is in the table: returns the length of the response written to out (0: none, a notification), -1 if it does not fit,
// or -2 when the method is not in the contract at all (the caller goes on).
int rpc_contract_handle(RpcSession *s, const JsonRpcReq &r, char *out, int cap);

#endif
