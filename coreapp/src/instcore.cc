// core_files_install (instfiles.h): the running core's own bundle; split from instfiles.cc so that the benches that only install a record link no cor.cc.
#include "instfiles.h"
#include "corefolder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {
class FileSrc : public CorSource {
public:
    explicit FileSrc(CoreFile *f) : f_(f) { }
    uint32_t size() { return fs_size32(f_->size()); }
    bool read(uint32_t off, void *buf, uint32_t len) { return len == 0 || f_->read(off, buf, len); }
private:
    CoreFile *f_;
};

// The record in memory (it is read once, hashed against the manifest by cor_read_part; the .cor is 36 KB of it for the diag core).
class MemSrc : public CorSource {
public:
    MemSrc(const uint8_t *p, uint32_t n) : p_(p), n_(n) { }
    uint32_t size() { return n_; }
    bool read(uint32_t off, void *buf, uint32_t len) { if ((uint64_t)off + len > n_) return false; memcpy(buf, p_ + off, len); return true; }
private:
    const uint8_t *p_;
    uint32_t n_;
};
}

int core_files_install(CoreCtx *c)
{
    size_t n = strlen(c->cor_path);
    if (n <= 4 || strcasecmp(c->cor_path + n - 4, ".cor")) {
        return 0;                                                  // started without a bundle (JTAG, the monolith)
    }
    CoreFile *f = c->fs->open(c->cor_path, false);
    if (!f) {
        return 0;
    }
    FileSrc src(f);
    CorInfo ci;
    int w = 0;
    cor_host_key = c->host_key;
    cor_result_t r = cor_probe(src, ci);
    if (((r == COR_OK || r == COR_ERR_LARGE) && strcasecmp(ci.name, c->desc->cor_name) == 0) && ci.size[COR_PART_FILES]) {
        char folder[200];
        uint32_t n = ci.size[COR_PART_FILES];
        uint8_t *rec = n <= 4 * INST_FILE_MAX ? new uint8_t[n] : 0;
        const char *why = "";
        cor_result_t rr = rec ? cor_read_part(src, ci, COR_PART_FILES, rec, 0, 0, true) : COR_ERR_RANGE;
        if (rr != COR_OK) {
            why = cor_result_str(rr);
            w = -1;
        } else if (!corefolder_dir(c->cor_path, folder, sizeof(folder))) {
            why = "no core folder";
            w = -1;
        } else {
            MemSrc mem(rec, n);
            w = instfiles_install(c->fs, mem, 0, n, folder, &why);
        }
        delete[] rec;
        if (w < 0) {
            printf("[core] %s: the files of the bundle could not be installed: %s\n", c->desc->cor_name, why);
        } else {
            printf("[core] %s: %d file(s) written to %s\n", c->desc->cor_name, w, folder);
        }
    }
    c->fs->close(f);
    return w;
}
