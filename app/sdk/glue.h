// What the C++ side of an application that was written against ABI 2 (app/sdk/u2p_app.h) needs: the context of app_main as that ABI saw it.
#ifndef APP_SDK_GLUE_H
#define APP_SDK_GLUE_H

#include "libapi.h"

extern "C" {
#include "u2p_app.h"
}

// The context of ABI 2 made of the one the base gave: the action, the file, a reply that lands in AppCtx::reply. Valid for the call of app_main.
struct U2pCtx {
    u2p_app_context_t ctx;
    AppCtx *base;
};

static inline int u2p_reply_into(AppCtx *c, const char *text)
{
    int n = 0;
    while (text[n] && n < c->reply_len - 1) {
        c->reply[n] = text[n];
        n++;
    }
    c->reply[n] = 0;
    return n;
}

#endif
