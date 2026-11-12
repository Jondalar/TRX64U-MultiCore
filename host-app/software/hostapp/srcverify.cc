#include "srcverify.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

// One pass over the part. deliver: the pass that fills dst / calls the sink; otherwise (the second read of `twice`) it only hashes.
static sv_result_t pass(CorSource &src, const SvJob &j, bool deliver, bool hash, char hex[65])
{
    sha256_t h;
    sha256_init(&h);
    for (uint32_t done = 0; done < j.len;) {
        uint32_t n = j.len - done > j.scratch_len ? j.scratch_len : j.len - done;
        uint8_t *buf = (deliver && j.dst) ? j.dst + done : j.scratch;
        if (!src.read(j.off + done, buf, n)) {
            return SV_READ;
        }
        if (hash) {
            sha256_update(&h, buf, n);
        }
        if (deliver && j.sink && j.sink(j.ctx, done, buf, n)) {
            return SV_STOPPED;
        }
        done += n;
    }
    if (hash) {
        uint8_t d[32];
        sha256_final(&h, d);
        sha256_hex(d, hex);
    }
    return SV_OK;
}

sv_result_t sv_read(CorSource &src, const SvJob &j)
{
    bool check = j.expect_sha || j.twice;
    for (int t = 1; t <= (check && j.tries > 0 ? j.tries : 1); t++) {
        char a[65], b[65];
        sv_result_t r = pass(src, j, true, check, a);
        if (r != SV_OK || !check) {
            return r;
        }
        bool same;
        if (j.expect_sha) {
            same = !memcmp(a, j.expect_sha, 64);
        } else {
            r = pass(src, j, false, true, b);
            if (r != SV_OK) {
                return r;
            }
            same = !memcmp(a, b, 64);
        }
        if (same) {
            return SV_OK;
        }
        {
            printf("[host] legacy: the %s read from %s differed from %s (try %d), read again\n", j.label, j.path,
                   j.expect_sha ? "its SHA-256" : "a second read", t + j.first_try - 1);
        }
    }
    return SV_MISMATCH;
}
