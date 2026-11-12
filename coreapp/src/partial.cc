#include "partial.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define PARTIAL_CHUNK 16384u

static uint8_t chunk_buf[PARTIAL_CHUNK] __attribute__((aligned(4)));

namespace {

class FileSrc : public CorSource {
public:
    explicit FileSrc(CoreFile *f) : f_(f) { }
    uint32_t size() { return fs_size32(f_->size()); }
    bool read(uint32_t off, void *buf, uint32_t len) { return f_->read(off, buf, len); }
private:
    CoreFile *f_;
};

}

// A large .cor (a flat build: A1200, ZX Next) also ends in .cor and its bitstream has the host's ICAP, but it carries no partial:
// its "Reload core" is the cold start of the machine, not a partial load (board 2026-10-28: COR_ERR_LARGE on the A1200).
static bool cor_is_large(const CoreCtx *c)
{
    CoreFile *f = c->fs->open(c->cor_path, false);
    if (!f) {
        return false;
    }
    FileSrc src(f);
    CorInfo ci;
    cor_host_key = c->host_key;
    cor_result_t r = cor_probe(src, ci);
    c->fs->close(f);
    return r == COR_ERR_LARGE || ci.large;
}

bool reload_is_dfx(const CoreCtx *c)
{
    size_t n = strlen(c->cor_path);
    return c->partial && n > 4 && !strcasecmp(c->cor_path + n - 4, ".cor") && c->partial->present() && !cor_is_large(c);
}

int partial_check(CoreCtx *c, CorInfo *ci, char *msg, int msglen)
{
    CoreFile *f = c->fs->open(c->cor_path, false);
    if (!f) {
        snprintf(msg, msglen, "Cannot open %s: the core bundle is not there (the stick is out?). The core keeps running.", c->cor_path);
        return 1;
    }
    FileSrc src(f);
    cor_host_key = c->host_key;
    cor_result_t r = cor_probe(src, *ci);
    if (r == COR_ERR_HOST) {
        // both keys and the file's identity go to the console and into the message, so a report says which side is off (the stick's other
        // copy of the file, or the hand-over). Board 2026-11-06: the hand-over key had been read from capitals as 0 digits (start.cc).
        printf("[core] reload: %s routed against host %08x, hand-over says host %08x, %u bytes\n", c->cor_path, (unsigned)ci->host_key,
               (unsigned)c->host_key, (unsigned)ci->file_size);
        snprintf(msg, msglen, "%s: %s (the file says %08x, this host is %08x). The core keeps running.", c->cor_path, cor_result_str(r),
                 (unsigned)ci->host_key, (unsigned)c->host_key);
        c->fs->close(f);
        return 1;
    }
    if (r != COR_OK) {
        snprintf(msg, msglen, "%s: %s. The core keeps running.", c->cor_path, cor_result_str(r));
        c->fs->close(f);
        return 1;
    }
    if (strcasecmp(ci->name, c->desc->cor_name) != 0) {
        snprintf(msg, msglen, "%s holds \"%s\", not this core (\"%s\"). The core keeps running.", c->cor_path, ci->name, c->desc->cor_name);
        c->fs->close(f);
        return 1;
    }
    sha256_t h;
    sha256_init(&h);
    uint32_t total = ci->size[COR_PART_PARTIAL];
    for (uint32_t done = 0; done < total;) {
        uint32_t n = total - done < PARTIAL_CHUNK ? total - done : PARTIAL_CHUNK;
        if (!src.read(ci->off[COR_PART_PARTIAL] + done, chunk_buf, n)) {
            snprintf(msg, msglen, "%s: cannot read the partial bitstream (the stick is out?). The core keeps running.", c->cor_path);
            c->fs->close(f);
            return 1;
        }
        sha256_update(&h, chunk_buf, n);
        done += n;
    }
    c->fs->close(f);
    uint8_t d[32];
    char hx[65];
    sha256_final(&h, d);
    sha256_hex(d, hx);
    if (memcmp(hx, ci->sha[COR_PART_PARTIAL], 64) != 0) {
        snprintf(msg, msglen, "%s: %s. The core keeps running.", c->cor_path, cor_result_str(COR_ERR_HASH));
        return 1;
    }
    return 0;
}

int partial_stream(CoreCtx *c, const CorInfo *ci, char *msg, int msglen)
{
    PartialPort *p = c->partial;
    CoreFile *f = c->fs->open(c->cor_path, false);
    if (!f) {
        snprintf(msg, msglen, "Cannot open %s any more. The core keeps running.", c->cor_path);
        return 1;
    }
    if (!p->decouple(true)) {
        c->fs->close(f);
        snprintf(msg, msglen, "The decouple did not take effect; the core stays as it is.");
        return 1;
    }
    printf("[core] reload: partial %u bytes from %s\n", (unsigned)ci->size[COR_PART_PARTIAL], c->cor_path);
    uint32_t total = ci->size[COR_PART_PARTIAL];
    uint32_t t0 = c->io->now_ms();
    p->begin();
    bool ok = true;
    for (uint32_t done = 0; done < total && ok;) {
        uint32_t n = total - done < PARTIAL_CHUNK ? total - done : PARTIAL_CHUNK;
        ok = f->read((uint64_t)ci->off[COR_PART_PARTIAL] + done, chunk_buf, n);
        if (ok) {
            p->write(chunk_buf, n);
            done += n;
        } else {
            snprintf(msg, msglen, "Reading the partial failed at byte %u (the stick is out?). The core is cut off: Return to host menu, or power cycle.", (unsigned)done);
        }
    }
    p->end();
    c->fs->close(f);
    if (!ok) {
        return 1;
    }
    uint32_t words = p->words();
    printf("[core] reload: %u words sent, %u expected, %u ms\n", (unsigned)words, (unsigned)(total / 4), (unsigned)(c->io->now_ms() - t0));
    if (words != total / 4) {
        snprintf(msg, msglen, "The ICAP word count differs from the bundle (%u of %u). The core is cut off: Return to host menu, or power cycle.",
                 (unsigned)words, (unsigned)(total / 4));
        return 1;
    }
    p->decouple(false);
    return 0;
}
