// What online_lib.cc (the entry) and the tests take from the search itself (online_search.cc). The state of the search lives in that one file, in the sub-app.
#ifndef APPS_ONLINE_IMPL_H
#define APPS_ONLINE_IMPL_H

#include "libapi.h"
#include "online_api.h"

#define ONLINE_PRESETS 12
#define ONLINE_HOST "hackerswithstyle.se"
#define ONLINE_JSON_MAX (96u * 1024u)             // the longest JSON answer taken (presets, results, entries of one result)
#define ONLINE_DOWNLOAD_MAX (6u << 20)            // the longest file taken from a result

// The tables lib_register hands the loader.
extern const LibOpDecl online_ops[];
extern const int online_nops;
extern const LibStateDecl online_states[];
extern const int online_nstates;
// The table "online" (online_api.h) the file manager reads the folder "/Online" through.
extern const OnlineApi online_table;

// The root the base gave; kept (api.md 3.1: one object for the whole run). Sets no state: a second call is the same as the first.
void online_attach(const LibApi *api);

#endif
