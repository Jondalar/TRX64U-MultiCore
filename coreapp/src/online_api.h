// The table "online" (major 1): what the file manager asks of the Assembly64 / CommoServe search to show its results as the folder "/Online" (netfs.cc). The sub-app that has the search
// (apps/online, a .u2a the core carries) exports it; the file manager finds it by name (LibLoader::find_export) and calls no function of the search, so a core without the sub-app has no
// folder "/Online". The functions are called on the menu task and never wait for the server: what the sub-app does not keep it fetches in the background and the call fails with `error`
// saying so, until a later call finds it (api.md 9.5.11). Size first; within a major members are only appended (`fetch` took its `alloc` argument after the table was written: base 3.3 is not released, so the major stays 1).
#ifndef COREAPP_ONLINE_API_H
#define COREAPP_ONLINE_API_H

#include <stdint.h>
#include "fsif.h"

#define ONLINE_API_NAME  "online"
#define ONLINE_API_MAJOR 1

typedef struct OnlineApi {
    uint32_t size;
    bool (*has_results)(void);
    // rel: the path below "/Online" without the leading slash: "" the results, "<result>" its files. -1: failed, online_error says why.
    int (*list)(const char *rel, FsEntry *out, int max);
    bool (*stat)(const char *rel, uint64_t *size, bool *dir);
    // a file of a result: its bytes in a block that `alloc` gave (the caller's allocator: the base frees the block with its own free, which a block of the sub-app's memory account is not
    // for), the length in *len; null when it failed (error says why). The sub-app keeps the last download for the next call.
    uint8_t *(*fetch)(const char *rel, uint8_t *(*alloc)(uint32_t n), uint32_t *len);
    const char *(*error)(void);
} OnlineApi;

#endif
