// Host test of srcverify.cc (what legacy.cc reads a bitstream / application with): a medium that returns one flipped bit on a read must not
// reach the "flash" (the copy that is written from RAM). Board 2026-10-28: 0x88 instead of 0x08 at +0x10D38E of a 2 MB scratch file.
#include "srcverify.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <vector>

typedef std::vector<uint8_t> Buf;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// flip_reads: the reads (counted over the whole run, 1-based) that deliver the byte at flip_at with bit 7 flipped; 0 = none, -1 = every read
struct FlakySrc : CorSource {
    Buf d;
    uint32_t flip_at = 0;
    int flip_from = 0, flip_to = 0, reads = 0;
    bool odd = false;                    // only the odd reads flip (pass A wrong, pass B right, every try)
    uint32_t size() { return d.size(); }
    bool read(uint32_t off, void *b, uint32_t n) {
        memcpy(b, d.data() + off, n);
        if (flip_at >= off && flip_at < off + n && off == (flip_at & ~32767u)) {
            reads++;                         // one count per pass over the flipped chunk
            if (reads >= flip_from && reads <= flip_to && (!odd || (reads & 1))) ((uint8_t *)b)[flip_at - off] ^= 0x80;
        }
        return true;
    }
};

static const uint32_t LEN = 300000, FLIP = 0x10D38E % LEN;

static void hexsha(const Buf &b, char hx[65]) {
    sha256_t h; uint8_t d[32];
    sha256_init(&h); sha256_update(&h, b.data(), b.size()); sha256_final(&h, d); sha256_hex(d, hx);
}

static int count_sink(void *c, uint32_t, uint8_t *, uint32_t) { (*(int *)c)++; return 0; }

int main()
{
    FlakySrc s;
    s.d.resize(LEN);
    for (uint32_t i = 0; i < LEN; i++) s.d[i] = (uint8_t)(i * 2654435761u >> 13);
    s.d[FLIP] = 0x08;
    char sha[65]; hexsha(s.d, sha);
    static uint8_t scratch[32768];
    Buf ram(LEN), flash;
    SvJob j = { 0, LEN, ram.data(), scratch, sizeof(scratch), sha, false, NULL, NULL, 3, 1, "bitstream", "/USB1/x.cor" };

    // 1. a clean medium: one read, same bytes
    s.flip_at = FLIP; s.flip_from = s.flip_to = 0; s.reads = 0;
    CHECK(sv_read(s, j) == SV_OK && ram == s.d);

    // 2. the first read flips the bit (the board's 0x88): the hash catches it, the second read is right, the "flash" gets the right byte
    s.flip_from = s.flip_to = 1; s.reads = 0;
    ram.assign(LEN, 0);
    CHECK(sv_read(s, j) == SV_OK);
    flash = ram;                                    // write_scratch writes this buffer
    CHECK(flash == s.d && flash[FLIP] == 0x08 && s.reads == 2);

    // 3. every read wrong: refused after 3 tries, and a caller that stops on SV_MISMATCH writes nothing
    s.flip_from = 1; s.flip_to = 1000; s.reads = 0;
    CHECK(sv_read(s, j) == SV_MISMATCH && s.reads == 3);

    // 4. a .cfw has no hash: two reads must agree. Flip on the first read, then on the second read of the same try, then on both
    SvJob t = j; t.expect_sha = NULL; t.twice = true;
    s.flip_from = s.flip_to = 1; s.reads = 0; ram.assign(LEN, 0);
    CHECK(sv_read(s, t) == SV_OK && ram == s.d);          // pass A bad, pass B good -> differ -> try 2 clean
    s.flip_from = s.flip_to = 2; s.reads = 0; ram.assign(LEN, 0);
    CHECK(sv_read(s, t) == SV_OK && ram == s.d);          // pass B bad -> differ -> try 2 clean
    s.flip_from = 1; s.flip_to = 1000; s.odd = true; s.reads = 0;
    CHECK(sv_read(s, t) == SV_MISMATCH);                  // A and B differ in every try: refused
    s.odd = false;
    // (a read that is wrong the same way twice passes the double read: it sees a transient error, not a systematic one)

    // 5. the sink sees every chunk, again from the start on a repeated read
    int calls = 0;
    SvJob k = j; k.sink = count_sink; k.ctx = &calls; k.dst = NULL;
    s.flip_from = s.flip_to = 1; s.reads = 0;
    CHECK(sv_read(s, k) == SV_OK);
    CHECK(calls == 2 * ((LEN + 32767) / 32768));

    // 6. CONTROL: without the check (no hash, no second read) the flipped bit lands in the flash copy
    SvJob n = j; n.expect_sha = NULL; n.twice = false;
    s.flip_from = s.flip_to = 1; s.reads = 0; ram.assign(LEN, 0);
    CHECK(sv_read(s, n) == SV_OK);
    flash = ram;
    CHECK(flash[FLIP] == 0x88 && flash != s.d);

    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("srcverify: ok\n");
    return 0;
}
