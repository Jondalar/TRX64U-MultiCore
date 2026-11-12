// The official firmware files the host recognises by the SHA-256 of the whole file (T137 D3): the name and version a converted .cor carries.
// Pure code over CorSource; the table itself is known_fw.h.
#ifndef HOST_FWKNOWN_H
#define HOST_FWKNOWN_H

#include <stdint.h>
#include "cor.h"

struct FwKnown {
    const char *sha256;               // of the whole file, lower-case hex
    const char *vendor;
    const char *name;
    const char *version;
};

const FwKnown *fw_known_lookup(const char *sha_hex);
int fw_known_count(void);

typedef void (*FwReadProgress)(void *ctx, uint32_t done, uint32_t total);
// The SHA-256 of the whole file, streamed. false: unreadable or `abort` (may be NULL; polled per block) said stop.
bool fw_file_sha(CorSource &src, char hex[65], FwReadProgress cb, void *ctx, bool (*abort)(void *));

#endif
