#include "fileops.h"
#include "sha256.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

const char *fo_text(FoResult r)
{
    switch (r) {
    case FO_OK: return "done";
    case FO_EXISTS: return "a file of that name is there";
    case FO_CANCELLED: return "cancelled";
    case FO_ERR_NOSRC: return "the file is gone";
    case FO_ERR_NOTFILE: return "that is a folder, not a file";
    case FO_ERR_NOTDIR: return "the target is not a folder";
    case FO_ERR_SAME: return "source and target are the same file";
    case FO_ERR_DIR_NOTEMPTY: return "the folder is not empty";
    case FO_ERR_NOROOM: return "not enough room";
    case FO_ERR_READ: return "cannot read the source";
    case FO_ERR_WRITE: return "cannot write the target";
    case FO_ERR_VERIFY: return "the copy does not read back equal";
    case FO_ERR_DELETE: return "copied, but the original could not be removed";
    case FO_ERR_FS: return "the file system refused";
    }
    return "?";
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static bool join(char *out, int n, const char *dir, const char *name)
{
    int l = snprintf(out, n, "%s%s%s", dir, (dir[0] && dir[strlen(dir) - 1] == '/') ? "" : "/", name);
    return l > 0 && l < n;
}

FoResult fo_plan(FoFs &fs, const char *src, const char *dstdir, bool move, FoPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    FoStat ss, ds;
    if (!fs.stat(src, &ss)) {
        return FO_ERR_NOSRC;
    }
    if (ss.dir) {
        return FO_ERR_NOTFILE;
    }
    if (!fs.stat(dstdir, &ds) || !ds.dir) {
        return FO_ERR_NOTDIR;
    }
    if (strlen(src) >= FO_PATH_MAX || !join(plan->dst, sizeof(plan->dst), dstdir, base_name(src))) {
        return FO_ERR_FS;
    }
    snprintf(plan->src, sizeof(plan->src), "%s", src);
    if (!strcasecmp(plan->src, plan->dst)) {
        return FO_ERR_SAME;
    }
    plan->size = ss.size;
    FoStat es;
    plan->exists = fs.stat(plan->dst, &es);
    if (plan->exists && es.dir) {
        return FO_ERR_NOTFILE;                      // a folder of that name is in the way: never replaced
    }
    if (move && !plan->exists && fs.same_volume(src, plan->dst)) {
        plan->rename_only = true;
        return FO_OK;
    }
    uint64_t fr = 0;
    if (!fs.free_bytes(dstdir, &fr)) {
        return FO_ERR_FS;
    }
    plan->free = fr;
    if (plan->size > fr) {
        return FO_ERR_NOROOM;                       // before one byte is written; the new data lives beside the old file until it is verified
    }
    return plan->exists ? FO_EXISTS : FO_OK;
}

static int report(FoProgress cb, void *ctx, int phase, uint64_t done, uint64_t total)
{
    return cb ? cb(ctx, phase, done, total) : 0;
}

// Source to tmp, SHA-256 of what was read; then tmp read back and compared.
static FoResult copy_verified(FoFs &fs, const char *src, const char *tmp, uint64_t size, uint8_t *buf, uint32_t buflen, FoProgress cb, void *ctx)
{
    void *in = fs.open_read(src);
    if (!in) {
        return FO_ERR_READ;
    }
    void *out = fs.open_write(tmp);
    if (!out) {
        fs.close(in);
        return FO_ERR_WRITE;
    }
    sha256_t h;
    sha256_init(&h);
    FoResult r = FO_OK;
    uint64_t done = 0;
    for (;;) {
        int n = fs.read(in, buf, buflen);
        if (n < 0) {
            r = FO_ERR_READ;
            break;
        }
        if (n == 0) {
            break;
        }
        sha256_update(&h, buf, (uint32_t)n);
        if (!fs.write(out, buf, (uint32_t)n)) {
            r = FO_ERR_WRITE;
            break;
        }
        done += (uint64_t)n;
        if (report(cb, ctx, FO_PHASE_COPY, done, size)) {
            r = FO_CANCELLED;
            break;
        }
    }
    fs.close(in);
    if (!fs.close(out) && r == FO_OK) {
        r = FO_ERR_WRITE;
    }
    if (r == FO_OK && done != size) {
        r = FO_ERR_READ;                            // the file changed under the copy
    }
    if (r != FO_OK) {
        return r;
    }
    uint8_t want[32], got[32];
    sha256_final(&h, want);
    void *back = fs.open_read(tmp);
    if (!back) {
        return FO_ERR_VERIFY;
    }
    sha256_init(&h);
    uint64_t seen = 0;
    for (;;) {
        int n = fs.read(back, buf, buflen);
        if (n < 0) {
            r = FO_ERR_VERIFY;
            break;
        }
        if (n == 0) {
            break;
        }
        sha256_update(&h, buf, (uint32_t)n);
        seen += (uint64_t)n;
        if (report(cb, ctx, FO_PHASE_VERIFY, seen, size)) {
            r = FO_CANCELLED;
            break;
        }
    }
    fs.close(back);
    if (r != FO_OK) {
        return r;
    }
    sha256_final(&h, got);
    return (seen == size && !memcmp(want, got, 32)) ? FO_OK : FO_ERR_VERIFY;
}

FoResult fo_copy(FoFs &fs, const FoPlan &plan, bool replace, uint8_t *buf, uint32_t buflen, FoProgress cb, void *ctx)
{
    if (plan.exists && !replace) {
        return FO_EXISTS;
    }
    char tmp[FO_PATH_MAX + 8];
    snprintf(tmp, sizeof(tmp), "%s.part", plan.dst);
    FoResult r = copy_verified(fs, plan.src, tmp, plan.size, buf, buflen, cb, ctx);
    if (r != FO_OK) {
        fs.remove(tmp);                             // the half-written copy never gets the file's name
        return r;
    }
    if (plan.exists && !fs.remove(plan.dst)) {
        fs.remove(tmp);
        return FO_ERR_FS;
    }
    if (!fs.rename(tmp, plan.dst)) {
        return FO_ERR_WRITE;                        // the verified copy stays as <name>.part
    }
    return FO_OK;
}

FoResult fo_move(FoFs &fs, const FoPlan &plan, bool replace, uint8_t *buf, uint32_t buflen, FoProgress cb, void *ctx)
{
    if (plan.exists && !replace) {
        return FO_EXISTS;
    }
    if (plan.rename_only && fs.rename(plan.src, plan.dst)) {
        return FO_OK;
    }
    if (plan.rename_only) {                         // the volume would not rename: the bytes are needed after all
        uint64_t fr = 0;
        if (!fs.free_bytes(plan.dst, &fr)) {
            return FO_ERR_FS;
        }
        if (plan.size > fr) {
            return FO_ERR_NOROOM;
        }
    }
    FoResult r = fo_copy(fs, plan, replace, buf, buflen, cb, ctx);
    if (r != FO_OK) {
        return r;
    }
    if (report(cb, ctx, FO_PHASE_DELETE, 0, 0) || !fs.remove(plan.src)) {
        return FO_ERR_DELETE;
    }
    return FO_OK;
}

FoResult fo_delete(FoFs &fs, const char *path)
{
    FoStat st;
    if (!fs.stat(path, &st)) {
        return FO_ERR_NOSRC;
    }
    if (st.dir) {
        int n = fs.dir_entries(path);
        if (n < 0) {
            return FO_ERR_FS;
        }
        if (n > 0) {
            return FO_ERR_DIR_NOTEMPTY;
        }
    }
    return fs.remove(path) ? FO_OK : FO_ERR_FS;
}

int fo_boot_refs(const BootCfg &cfg, const char *path, int *idx, int max)
{
    int n = 0;
    for (int i = 0; i < cfg.n; i++) {
        if (!strcasecmp(cfg.e[i].path, path)) {
            if (n < max) {
                idx[n] = i;
            }
            n++;
        }
    }
    return n;
}
