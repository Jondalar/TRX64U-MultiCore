#include "startcheck.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static EntryCheck of_result(cor_result_t r)
{
    switch (r) {
    case COR_OK: return EC_OK;
    case COR_ERR_CONTRACT: return EC_CONTRACT;
    case COR_ERR_HOST: return EC_HOST;
    default: return EC_BAD;
    }
}

EntryCheck startcheck_entry(const BootEntry &e, StartFs &fs, CorInfo &ci, FileStat &st)
{
    memset(&ci, 0, sizeof(ci));
    if (!fs.stat(e.path, &st)) {
        return EC_MISSING;
    }
    CorSource *src = fs.open(e.path);
    if (!src) {
        return EC_MISSING;
    }
    cor_result_t r = cor_probe(*src, ci);
    delete src;
    EntryCheck ec = of_result(r);
    if (ec != EC_OK) {
        return ec;
    }
    if (!e.mtime) {
        return EC_UNCHECKED;
    }
    return startcheck_trusts(e, ci, st) ? EC_OK : EC_CHANGED;
}

bool startcheck_trusts(const BootEntry &e, const CorInfo &ci, const FileStat &st)
{
    return e.mtime && e.hash[0] && st.size == e.size && ci.file_size == e.size && st.mtime == e.mtime && ci.hdr_digest == e.hdr &&
           !strcmp(e.hash, ci.sha[COR_PART_PARTIAL]);
}

EntryCheck startcheck_firmware(const BootEntry &e, StartFs &fs, FileStat &st)
{
    if (!fs.stat(e.path, &st)) {
        return EC_MISSING;
    }
    if (!e.mtime) {
        return EC_UNCHECKED;
    }
    return st.size == e.size && st.mtime == e.mtime ? EC_OK : EC_CHANGED;
}

cor_result_t cor_full_check(CorSource &src, const CorInfo &ci)
{
    uint32_t big = ci.size[COR_PART_PARTIAL] > ci.size[COR_PART_APP] ? ci.size[COR_PART_PARTIAL] : ci.size[COR_PART_APP];
    for (int part = 0; part < COR_PARTS; part++) {         // the description and the libraries (T104.1) can be larger than both
        big = ci.size[part] > big ? ci.size[part] : big;
    }
    uint8_t *scratch = (uint8_t *)malloc(big);
    if (!scratch) {
        return COR_ERR_IO;
    }
    cor_abort = false;       // set by a core start the person cancelled; this read is a run of its own
    cor_result_t r = COR_OK;
    for (int part = 0; r == COR_OK && part < COR_PARTS; part++) {
        if (ci.size[part]) {
            r = cor_read_part(src, ci, part, scratch, 0, 0);
        }
    }
    free(scratch);
    return r;
}

bool startcheck_migrate(BootEntry &e, StartFs &fs, bool fw)
{
    FileStat st;
    if (!fs.stat(e.path, &st)) {
        return false;
    }
    if (fw) {
        e.size = st.size;
        e.mtime = st.mtime;
        e.hdr = 0;
        return true;
    }
    // a stored size that is not the file's: the file was replaced, the full check below could only refuse it - and ran again at every
    // start (2026-10-26, c264.cor swapped on the SD). The entry shows as changed; R checks it once.
    if (e.size && e.size != st.size) {
        return false;
    }
    CorSource *src = fs.open(e.path);
    if (!src) {
        return false;
    }
    CorInfo ci;
    bool ok = cor_probe(*src, ci) == COR_OK && cor_full_check(*src, ci) == COR_OK;
    delete src;
    // an older entry may already carry the hash and size of its adding: a file that differs from them was changed since, and it stays
    // marked (the person decides with a re-check), it does not become the new truth silently
    if (!ok || (e.hash[0] && strcmp(e.hash, ci.sha[COR_PART_PARTIAL])) || (e.size && e.size != ci.file_size)) {
        return false;
    }
    snprintf(e.hash, sizeof(e.hash), "%s", ci.sha[COR_PART_PARTIAL]);
    e.size = ci.file_size;
    e.mtime = st.mtime;
    e.hdr = ci.hdr_digest;
    return true;
}
