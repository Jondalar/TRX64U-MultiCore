// The online search as a sub-app (T239.3 D12, P3-1, L-3): a resident image of format 3 that registers the operations and state values of the search form (app.toml puts the tab "online"
// into F3), and exports the table "online" (online_api.h) that the file manager reads the folder "/Online" through. The search itself is online_search.cc; its client is online_http.cc and
// online_json.cc over api->net (the outgoing half of base 3.3: connect, resolve). The core's description does not name any of this: a core that wants the search carries online.u2a in its .cor.
#include "libapi.h"
#include "libfmt.h"
#include "online_impl.h"

static const LibExportDecl k_exports[] = { { ONLINE_API_NAME, ONLINE_API_MAJOR, &online_table, sizeof(OnlineApi) } };

// lib.ld puts this section first, so the entry is at offset 0 of the image. The host test compiles the file with ONLINE_ENTRY_NAME set to another name, because the test program holds
// the test library's lib_register as well.
#ifdef __riscv
#define LIB_ENTRY_ATTR __attribute__((visibility("default"), section(".text.lib_register")))
#else
#define LIB_ENTRY_ATTR
#endif
#ifndef ONLINE_ENTRY_NAME
#define ONLINE_ENTRY_NAME lib_register
#endif
extern "C" LIB_ENTRY_ATTR int ONLINE_ENTRY_NAME(const LibApi *api, LibRecord *out)
{
    if (api->abi_major != U2A_ABI_MAJOR || api->abi_minor < 3 || !api->mem || !api->net || !api->job || !LIB_RECORD_COVERS(out->struct_size, export_list)) {
        return 1;                                                    // needs the outgoing sockets and the jobs of base 3.3 (every request runs on a job) and the named exports of 3.2
    }
    if (api->net->size < __builtin_offsetof(SvcNet, resolve) + sizeof(api->net->resolve)) {
        return 1;
    }
    online_attach(api);
    out->ops = online_ops;
    out->nops = online_nops;
    out->states = online_states;
    out->nstates = online_nstates;
    out->export_list = k_exports;
    out->nexport_list = 1;
    return 0;
}
