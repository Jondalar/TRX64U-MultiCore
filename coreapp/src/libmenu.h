// The menu record of a library or application (T104.1 D9 "a sibling for libraries", Alex 2026-10-26): the nodes, file actions, settings, tabs and poll a
// file declares as TOML (tools/u2a_pack.py --menu), compiled to the record of descrec.h and carried behind the other parts of the .u2a. Header flag
// bit 0 says it is there and the `reserved` word holds its file offset. What a file's operations, states and events are stays code (lib_register):
// they are handlers. The record may not give a route, hide or rename what others contribute, or carry windows - that is the description's. It may carry key rows (section 18, base 3.6; a library's only): contrib_add_keys merges them behind the core's.
#ifndef COREAPP_LIBMENU_H
#define COREAPP_LIBMENU_H

#include "libload.h"
#include "descrec.h"

#define U2A3_FLAG_MENU 1U

// Reads and checks the record at `off`; *rec and *bytes are the caller's to delete (rec points into bytes). 0 on success, else a sentence in why.
// `app`: the file is an application, whose record may give a route of its own (its screens, the area "APP", T104.3 D0); a library's may not.
int libmenu_read(LibSource *src, uint32_t fsize, uint32_t off, DescRecord **rec, uint8_t **bytes, char *why, int whylen, bool app = false);

#endif
