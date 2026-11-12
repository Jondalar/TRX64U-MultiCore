// The Assembly64 / CommoServe search as a medium and a form (T104.2 UI). The query form is declarative (menu.toml F3 3: the nodes call the operations here, the rows read the states);
// the results are the folder "/Online" of the Library (netfs.cc asks this file), a result is a folder of its files, a file is downloaded when something reads it. Host-owned; the
// client is a64_assembly.cc (Gideon's assembly.cc).
#ifndef COREAPP_ONLINE_H
#define COREAPP_ONLINE_H

#include "ctx.h"
#include "ops.h"
#include "fsif.h"

void online_register(OpRegistry &r);

bool online_has_results(void);
// rel: the path below "/Online" without the leading slash: "" the results, "<result>" its files
int online_list(const char *rel, FsEntry *out, int max);
bool online_stat(const char *rel, uint64_t *size, bool *dir);
// a file of a result: the bytes (malloc, the caller frees) or null
uint8_t *online_fetch(const char *rel, uint32_t *len);
const char *online_error(void);

#endif
