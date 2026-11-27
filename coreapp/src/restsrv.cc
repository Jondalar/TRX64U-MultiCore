#include "restsrv.h"
#include "nettransport.h"
#include "rest.h"
#include "corereport.h"
#include "rpc.h"
#include "uic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "netalloc.h"
#include <string.h>

namespace {

CoreCtx *g_ctx;

class CriticalLock : public RpcLock {
public:
    void lock() { portENTER_CRITICAL(); }
    void unlock() { portEXIT_CRITICAL(); }
};
CriticalLock g_crit;

class AppWs : public NetWs {                 // no RTTI in the image: the kind is a field
public:
    bool ui;
    AppWs(bool u) : ui(u) { }
};

class CoreWs : public AppWs {
public:
    CoreWs() : AppWs(false) { }
    RpcSession s;
    int handle(const char *msg, int n, char *out, int cap) { return rpc_handle(&s, msg, n, out, cap); }
    int next(char *out, int cap) { return rpc_next(&s, out, cap); }
};

// The UI contract's socket (T192.1 D-H): hello, snapshot, events, intents over the service's client session; closes after `resync` (S11).
class UiWs : public AppWs {
public:
    UiWs() : AppWs(true) { }
    UicClient c;
    int handle(const char *msg, int n, char *out, int cap) { return uic_client_in(rest_uic, &c, msg, n, out, cap); }
    int next(char *out, int cap) { return uic_client_next(rest_uic, &c, out, cap); }
    bool done() { return c.closing; }
};

// The core application's side of the shared transport: REST and WS JSON-RPC over its registry and executor.
class CoreNet : public NetApp {
public:
    int serve(const HttpReq &q, char *out, int cap, bool *ws) { return rest_serve(g_ctx, q, out, cap, ws); }
    int serve_bad(const HttpReq &q, char *out, int cap) { return rest_serve_bad(q, out, cap); }
    bool starting() { return rest_start_gate(); }
    NetWs *ws_open_path(const char *path)
    {
        if (rest_uic && !strcmp(path, "/v1/ui/ws")) {
            UiWs *w = net_new<UiWs>();
            if (w) {
                uic_client_open(rest_uic, &w->c);
            }
            return w;
        }
        return ws_open();
    }
    NetWs *ws_open()
    {
        CoreWs *w = net_new<CoreWs>();
        if (w) {
            rpc_open(&w->s, g_ctx, &g_crit);
        }
        return w;
    }
    void ws_close(NetWs *w)
    {
        if (static_cast<AppWs *>(w)->ui) {
            UiWs *u = static_cast<UiWs *>(w);
            uic_client_close(rest_uic, &u->c);
            net_delete(u);
            return;
        }
        CoreWs *c = static_cast<CoreWs *>(w);
        rpc_close(&c->s);
        net_delete(c);
    }
};
CoreNet g_net;

}

void restsrv_start(CoreCtx *c, bool (*net_up)(char *, int))
{
    g_ctx = c;
    NetConfig cfg;
    cfg.port = RESTSRV_PORT;
    cfg.clients_max = RESTSRV_CLIENTS_MAX;
    cfg.ws_max = RESTSRV_CLIENTS_MAX;        // as before: a WS client may take any slot
    cfg.out_max = CORE_REPORT_MAX + 4096;    // T162: the bug report and its HTTP header go out in one piece
    cfg.share_out = false;                   // a request must not wait for another's operation
    cfg.stack_words = 8 * configMINIMAL_STACK_SIZE;
    cfg.ws_msg_max = 12288;                  // an answer of the contract's read_memory is base64 of up to 6 KB
    cfg.name = "rest";
    nettransport_start(&g_net, cfg, net_up);
}
