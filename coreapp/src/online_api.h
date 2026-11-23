// The table "online" (major 1): what the file manager asks of the Assembly64 / CommoServe search to show its results as the folder "/Online" (netfs.cc). The library that has the search gives it
// (online.cc); the file manager finds it by name (LibLoader::find_export) and calls no function of the search. Size first; within a major members are only appended.
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
    // a file of a result: the bytes (malloc, the caller frees) or null
    uint8_t *(*fetch)(const char *rel, uint32_t *len);
    const char *(*error)(void);
} OnlineApi;

#endif
