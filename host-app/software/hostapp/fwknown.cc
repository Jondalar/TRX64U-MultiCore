#include "fwknown.h"
#include "sha256.h"
#include <string.h>

bool fw_file_sha(CorSource &src, char hex[65], FwReadProgress cb, void *ctx, bool (*abort)(void *))
{
    static uint8_t blk[16384];
    sha256_t s;
    sha256_init(&s);
    uint32_t size = src.size(), done = 0;
    while (done < size) {
        if (abort && abort(ctx)) {
            return false;
        }
        uint32_t n = size - done > sizeof(blk) ? (uint32_t)sizeof(blk) : size - done;
        if (!src.read(done, blk, n)) {
            return false;
        }
        sha256_update(&s, blk, n);
        done += n;
        if (cb) {
            cb(ctx, done, size);
        }
    }
    uint8_t d[32];
    sha256_final(&s, d);
    sha256_hex(d, hex);
    return true;
}

#include "known_fw.h"

const FwKnown *fw_known_lookup(const char *sha_hex)
{
    for (const FwKnown *k = known_fw; k->sha256; k++) {
        if (!strcmp(k->sha256, sha_hex)) {
            return k;
        }
    }
    return 0;
}

int fw_known_count(void)
{
    int n = 0;
    for (const FwKnown *k = known_fw; k->sha256; k++) {
        n++;
    }
    return n;
}
