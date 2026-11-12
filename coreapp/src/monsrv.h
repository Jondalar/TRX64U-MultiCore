// The TRX64 runtime contract of the linked monitor on TCP 4312 (WebSocket JSON-RPC 2.0, T21-monitor.md 7.4): the server half of trxmon's main.c, moved here when
// trxmon.u2a was dropped (T104.3, Alex 2026-10-29: the linked monitor stays). One RPC connection at a time; a second is refused with the reason.
// The server runs from the start of the application, polled by mon_serve() every pass of the main loop (monitor.cc), whatever the menu does, like trxmon.u2a on its own task.
// The monitor session (the engine) is claimed when a client sends its first command (mon_claim).
#ifndef COREAPP_MONSRV_H
#define COREAPP_MONSRV_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Listens on TCP 4312 (once; a port that is taken leaves the monitor without RPC and says so in monsrv_status(); mon_serve() asks again every 5 s).
void monsrv_start(void);
// One pass without waiting: accept, read what came, answer, drop what is dead, keep-alive.
void monsrv_poll(void);
// Closes the holder, the refused connections and the listener.
void monsrv_stop(void);
// "RPC on TCP 4312", "TCP port 4312 is taken", or "" when the server is not up.
const char *monsrv_status(void);

bool monsrv_listening(void);
bool monsrv_has_client(void);          // a holder is connected (handshake done or not)
void monsrv_drop_client(void);

// The methods of the monitor socket that change the machine's state (a restore of ring state, a goto in it, the monitor's own commands, media): while the core
// service is up the socket has no confirmation token, so these are refused there (gate_refused). Every restore/goto of the method list in trxmon/tm_rpc.c is in it.
inline bool monsrv_gated(const char *m)
{
    static const char *const gated[] = { "monitor/exec", "checkpoint/restore", "transport/goto", "mark/goto", "runtime/reverse_step", "media/", "sandbox/" };
    for (unsigned i = 0; i < sizeof(gated) / sizeof(gated[0]); i++) {
        if (!strncmp(m, gated[i], strlen(gated[i]))) {
            return true;
        }
    }
    return false;
}

// s as the inside of a JSON string (quotes, backslash, control characters escaped); out is always terminated, a text that does not fit is cut
inline void monsrv_json_esc(const char *s, char *out, int cap)
{
    int n = 0;
    for (; *s && n < cap - 7; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') {
            out[n++] = '\\';
            out[n++] = (char)ch;
        } else if (ch < 0x20) {
            n += snprintf(out + n, 7, "\\u%04x", ch);
        } else {
            out[n++] = (char)ch;
        }
    }
    out[n] = 0;
}

#endif
