#include "instfiles.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static bool path_ok(const char *p, uint32_t n)
{
    if (n == 0 || n > INST_PATH_MAX || p[0] == '/') {
        return false;
    }
    uint32_t seg = 0;
    for (uint32_t i = 0; i <= n; i++) {
        char ch = i < n ? p[i] : '/';
        if (ch == '\\' || ch == 0 || (unsigned char)ch < 0x20) {
            return false;
        }
        if (ch == '/') {
            if (seg == 0 || (seg == 2 && p[i - 1] == '.' && p[i - 2] == '.')) {
                return false;                                     // an empty part, or ".."
            }
            seg = 0;
        } else {
            seg++;
        }
    }
    return true;
}

// The folders of `rel` below `folder` (everything before the last '/').
static bool make_dirs(CoreFs *fs, const char *folder, const char *rel)
{
    char dir[200];
    if (!fs->mkdir(folder)) {
        return false;
    }
    size_t fl = strlen(folder);
    if (fl + 1 >= sizeof(dir)) {
        return false;
    }
    memcpy(dir, folder, fl);
    dir[fl] = '/';
    for (const char *s = strchr(rel, '/'); s; s = strchr(s + 1, '/')) {
        size_t n = (size_t)(s - rel);                              // no "%.*s": the board's small printf does not take a precision from the arguments
        if (fl + 1 + n >= sizeof(dir)) {
            return false;
        }
        memcpy(dir + fl + 1, rel, n);
        dir[fl + 1 + n] = 0;
        if (!fs->mkdir(dir)) {
            return false;
        }
    }
    return true;
}

// FNV-1a 64 of the text on the card, when it has the size of one of the earlier versions; true when it equals one.
static bool is_earlier_version(CoreFs *fs, const char *p, uint64_t sz, const uint8_t *olds, uint32_t nold)
{
    uint8_t *buf = 0;
    uint64_t h = 0;
    bool hit = false;
    for (uint32_t i = 0; i < nold && !hit; i++) {
        const uint8_t *o = olds + 12 * i;
        if (le32(o) != sz) {
            continue;
        }
        if (!buf) {
            buf = new uint8_t[(size_t)sz];
            if (fs->load(p, buf, (uint32_t)sz) != (int)sz) {
                break;
            }
            h = 0xcbf29ce484222325ull;
            for (uint64_t k = 0; k < sz; k++) {
                h = (h ^ buf[k]) * 0x100000001b3ull;
            }
        }
        hit = (uint32_t)(h >> 32) == le32(o + 4) && (uint32_t)h == le32(o + 8);
    }
    delete[] buf;
    return hit;
}

int instfiles_install(CoreFs *fs, CorSource &src, uint32_t rec_off, uint32_t rec_size, const char *folder, const char **why)
{
    const char *w_ = "";
    if (!why) {
        why = &w_;
    }
    if (rec_size == 0) {
        return 0;
    }
    uint8_t h[12];
    if (rec_size < 12 || !src.read(rec_off, h, 12) || memcmp(h, "TRXF", 4) || le32(h + 4) != 1) {
        *why = "the record has no TRXF header of version 1";
        return -1;
    }
    uint32_t count = le32(h + 8), p = 12;
    int written = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t eh[12];
        if (p + 12 > rec_size || !src.read(rec_off + p, eh, 12)) {
            *why = "the record is truncated";
            return -1;
        }
        uint32_t elen = le32(eh), plen = le16(eh + 4), nold = le16(eh + 6), size = le32(eh + 8);
        uint32_t fixed = 8 + 12 * nold;
        if (elen % 4 || elen < fixed || elen > rec_size - p - 4 || fixed + plen + size > elen || size > INST_FILE_MAX || nold > 8 || plen > INST_PATH_MAX) {
            *why = "an entry is malformed";
            return -1;
        }
        uint8_t olds[8 * 12];
        char rel[INST_PATH_MAX + 1];
        if ((nold && !src.read(rec_off + p + 12, olds, 12 * nold)) || !src.read(rec_off + p + 4 + fixed, rel, plen)) {
            *why = "an entry is truncated";
            return -1;
        }
        rel[plen] = 0;
        if (!path_ok(rel, plen)) {
            *why = "a path leaves the core's folder";
            return -1;
        }
        uint32_t data_off = rec_off + p + 4 + fixed + plen;
        p += 4 + elen;
        char path[300];
        if (snprintf(path, sizeof(path), "%s/%s", folder, rel) >= (int)sizeof(path) || !make_dirs(fs, folder, rel)) {
            static char msg[160];
            snprintf(msg, sizeof(msg), "%s/%s: a folder cannot be made (read only card?)", folder, rel);
            *why = msg;
            return -1;
        }
        uint64_t sz;
        bool dir;
        if (fs->stat(path, &sz, &dir) && (dir || !is_earlier_version(fs, path, sz, olds, nold))) {
            continue;                                              // never over a file: the person may have edited it
        }
        uint8_t *buf = new uint8_t[size ? size : 1];
        bool ok = src.read(data_off, buf, size) && fs->save(path, buf, size);
        delete[] buf;
        if (!ok) {
            static char msg[160];
            snprintf(msg, sizeof(msg), "%s cannot be written (read only or full card?)", path);
            *why = msg;
            return -1;
        }
        written++;
    }
    if (p != rec_size) {
        *why = "the record has bytes behind its last file";
        return -1;
    }
    return written;
}
