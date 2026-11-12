// T135 B1/B2: "ours" = the bitstream writes AXSS and the application names TRX64 (ourfw.h). The streaming scan is what the updater runs over the
// flash in chunks; the same file lives in both trees and must stay byte-equal (run.sh compares them).
#include "ourfw.h"
#include "handover.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static bool chunked(const char *s, unsigned n, unsigned chunk)      // the updater's way: the flash in pieces, one state
{
    int st = 0;
    unsigned len = (unsigned)strlen(s);
    for (unsigned i = 0; i < len; i += n) {
        if (ourfw_scan_feed(&st, (const uint8_t *)s + i, len - i < n ? len - i : n)) {
            return true;
        }
    }
    (void)chunk;
    return false;
}

int main()
{
    // the word is found wherever it lies, also across two chunks
    CHECK(ourfw_app_marked((const uint8_t *)"TRX64", 5));
    CHECK(ourfw_app_marked((const uint8_t *)"xxTRX64 host app", 16));
    for (unsigned n = 1; n <= 7; n++) {
        CHECK(chunked("Software version: TRX64 Ultimate", n, n));
    }
    // near misses and the vendor's own strings are not it
    CHECK(!ourfw_app_marked((const uint8_t *)"TRX6", 4));
    CHECK(!ourfw_app_marked((const uint8_t *)"TRTRX6", 6));
    CHECK(ourfw_app_marked((const uint8_t *)"TTRX64", 6));                      // a repeated first letter restarts at once
    CHECK(!ourfw_app_marked((const uint8_t *)"Commodore 64 Ultimate 1.1.0 PRKL s2p8", 38));
    CHECK(!ourfw_app_marked((const uint8_t *)"", 0));
    for (unsigned n = 1; n <= 7; n++) {
        CHECK(!chunked("Commodore 64 Ultimate 1.1.0 TRX6 4 trx64", n, n));
    }
    // the decision: both, never one
    CHECK(ourfw_is_ours(true, true));
    CHECK(!ourfw_is_ours(true, false));           // B1: a self-built firmware writes the stamp as well (issue #6)
    CHECK(!ourfw_is_ours(false, true));
    CHECK(!ourfw_is_ours(false, false));
    // B2: a page a host wrote (handover_fill) is a guest start; noise, a half-written page and a cleared one are not
    {
        static uint32_t page[1024];
        memset(page, 0, sizeof(page));
        CHECK(!ourfw_page_valid((const uint8_t *)page));
        handover_fill((Handover *)page, "/USB1/cores/commodore-110.cor", "Commodore 64 Ultimate", "1.1.0", 0x1234ABCDu, 0, "bk");
        CHECK(handover_valid((Handover *)page) && ourfw_page_valid((const uint8_t *)page));
        uint8_t *b = (uint8_t *)page;
        b[100] ^= 1;                                        // one byte of the path
        CHECK(!handover_valid((Handover *)page) && !ourfw_page_valid(b));
        b[100] ^= 1;
        b[3] ^= 0x80;                                       // the magic
        CHECK(!ourfw_page_valid(b));
        b[3] ^= 0x80;
        b[4] = 2;                                           // another version
        CHECK(!ourfw_page_valid(b));
        b[4] = 1;
        CHECK(ourfw_page_valid(b));
        for (unsigned i = 0; i < sizeof(page); i++) {       // DDR2 after a power-up: noise
            b[i] = (uint8_t)(i * 2654435761u >> 13);
        }
        CHECK(!ourfw_page_valid(b));
    }
    if (fails) {
        printf("%d failed\n", fails);
        return 1;
    }
    printf("ourfw: ok\n");
    return 0;
}
