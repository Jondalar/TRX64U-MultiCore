// The socket side both applications' web servers share (T192.1 P6, Q5): one listening task, one task per client, the HTTP request read by httpreq.h, and
// after an accepted upgrade the WebSocket framing of wsproto.h. Board code - lwIP sockets and FreeRTOS tasks. What is served is the application's:
// it implements NetApp (the core application over its service registry: restsrv.cc; the host application over its snapshot: hostapp/webnet.cc).
#ifndef COREAPP_NETTRANSPORT_H
#define COREAPP_NETTRANSPORT_H

#include "httpreq.h"

// One WebSocket connection of the application: text messages in, answers and notifications out.
class NetWs {
public:
    virtual ~NetWs() { }
    // One text message; the answer goes to out[0..cap). Returns its length, 0 for none, -1 when it does not fit.
    virtual int handle(const char *msg, int n, char *out, int cap) = 0;
    // The next waiting notification, or 0.
    virtual int next(char *out, int cap) = 0;
    // True when the session ends the connection after what is queued is sent (the UI socket after `resync`): the transport closes it with 1001.
    virtual bool done() { return false; }
};

class NetApp {
public:
    virtual ~NetApp() { }
    // The whole response (status line, headers, body) for a ready request; -1 when it does not fit. *ws is set when the 101 of an accepted upgrade is in it.
    virtual int serve(const HttpReq &q, char *out, int cap, bool *ws) = 0;
    // The answer for a request that never became ready.
    virtual int serve_bad(const HttpReq &q, char *out, int cap) = 0;
    // After an accepted upgrade: the connection's session, and its end. A null session closes the connection.
    virtual NetWs *ws_open() = 0;
    // The same, told the path that was upgraded (an application with more than one socket); the default ignores it.
    virtual NetWs *ws_open_path(const char *path) { (void)path; return ws_open(); }
    virtual void ws_close(NetWs *w) = 0;
    // True while the application answers every request with a short fixed refusal (the core app's start gate): the transport then serves one client at a
    // time from static buffers and allocates nothing, so a client polling a long start cannot take heap the start itself needs.
    virtual bool starting() { return false; }
};

struct NetConfig {
    int port;
    int clients_max;                      // connections served at once; a further one is answered 503 and closed
    int ws_max;                           // of those, at most this many WebSocket connections (the rest stay for plain requests)
    int out_max;                          // the largest response, in one piece (allocated per request, not per connection)
    bool share_out;                       // true: one such buffer for all clients, taken under a mutex (a request waits up to 5 s, then 503)
    int stack_words;                      // the client task's stack, in words
    int ws_msg_max;                       // the largest WebSocket message either way
    const char *name;                     // for the log line
};

// Starts the task; it waits for the network (`net_up` says whether a link is up and has an address, and gives it as text) and does nothing twice.
void nettransport_start(NetApp *app, const NetConfig &cfg, bool (*net_up)(char *buf, int len));

#endif
