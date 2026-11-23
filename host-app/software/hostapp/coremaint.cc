#include "coremaint.h"
#include "sha256.h"
#include "descrec.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const CmReq cm_reqs[] = {
#include "romreq_table.inc"
};
#define CM_NREQ ((int)(sizeof(cm_reqs) / sizeof(cm_reqs[0])))

// ---- what a core needs ---------------------------------------------------------------------------------------------------------------

static bool ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext);
    return n > e && !strcasecmp(s + n - e, ext);
}

static bool size_in(const CmReq &r, uint64_t size)
{
    for (int i = 0; i < 3 && r.sizes[i]; i++) {
        if (r.sizes[i] == size) {
            return true;
        }
    }
    return false;
}

// The file of the row that is in the folder: a name of the row (case-blind) with an allowed size; for a ROM set any file of the extension.
static bool row_present(const CmReq &r, const CmEntry *e, int n)
{
    for (int i = 0; i < n; i++) {
        if (e[i].dir || !size_in(r, e[i].size)) {
            continue;
        }
        if (r.flags & 4) {
            if (ends_with(e[i].name, r.names)) {
                return true;
            }
            continue;
        }
        const char *p = r.names;
        while (*p) {
            const char *bar = strchr(p, '|');
            size_t l = bar ? (size_t)(bar - p) : strlen(p);
            if (strlen(e[i].name) == l && !strncasecmp(e[i].name, p, l)) {
                return true;
            }
            p = bar ? bar + 1 : p + l;
        }
    }
    return false;
}

static void add_text(char *out, int len, const char *s)
{
    size_t l = strlen(out);
    if (l && l + 2 < (size_t)len) {
        strcat(out, ", ");
        l += 2;
    }
    if (l < (size_t)len - 1) {
        strncat(out, s, (size_t)len - 1 - l);
    }
}

static void first_name(const CmReq &r, char *out, int len)
{
    const char *bar = strchr(r.names, '|');
    size_t l = bar ? (size_t)(bar - r.names) : strlen(r.names);
    if (l >= (size_t)len) {
        l = (size_t)len - 1;
    }
    memcpy(out, r.names, l);
    out[l] = 0;
}

// `core` null: every row of `reqs` is the core's (the rows of a record); else the rows of the table whose manifest name is `core`.
static void rom_status(const CmReq *reqs, int nreq, const char *core, const CmEntry *entries, int n, int *need, int *have, char *missing, int missing_len)
{
    *need = *have = 0;
    missing[0] = 0;
    bool any_set = false, set_done = false;
    int best_missing = 1000;
    char best_name[56] = "";
    for (int bit = 0; bit < 8; bit++) {
        int rows = 0, miss = 0;
        char first[56] = "";
        for (int i = 0; i < nreq; i++) {
            const CmReq &r = reqs[i];
            if ((core && strcasecmp(r.core, core)) || !(r.set & (1 << bit))) {
                continue;
            }
            rows++;
            if (!row_present(r, entries, n)) {
                miss++;
                if (!first[0]) {
                    first_name(r, first, sizeof(first));
                }
            }
        }
        if (rows) {
            any_set = true;
            if (!miss) {
                set_done = true;
            } else if (miss < best_missing) {
                best_missing = miss;
                snprintf(best_name, sizeof(best_name), "%s", first);
            }
        }
    }
    for (int i = 0; i < nreq; i++) {
        const CmReq &r = reqs[i];
        if ((core && strcasecmp(r.core, core)) || r.set || !(r.flags & 1)) {
            continue;
        }
        (*need)++;
        if (row_present(r, entries, n)) {
            (*have)++;
        } else {
            char nm[56], t[80];
            first_name(r, nm, sizeof(nm));
            snprintf(t, sizeof(t), "%s", (r.flags & 4) ? r.role : nm);
            add_text(missing, missing_len, t);
        }
    }
    if (any_set) {
        (*need)++;
        if (set_done) {
            (*have)++;
        } else {
            char t[100];
            snprintf(t, sizeof(t), "%s (one complete set)", best_name);
            add_text(missing, missing_len, t);
        }
    }
}

void cm_rom_status(const char *core_name, const CmEntry *entries, int n, int *need, int *have, char *missing, int missing_len)
{
    rom_status(cm_reqs, CM_NREQ, core_name, entries, n, need, have, missing, missing_len);
}

void cm_rom_status_rows(const CmRows &rows, const CmEntry *entries, int n, int *need, int *have, char *missing, int missing_len)
{
    rom_status(rows.req, rows.n, 0, entries, n, need, have, missing, missing_len);
}

static bool row_add(CmRows *o, const char *role, const char *const *names, int nnames, const uint32_t *sizes, int flags, int set)
{
    if (o->n >= CM_ROWS) {
        return false;
    }
    int i = o->n;
    snprintf(o->role[i], sizeof(o->role[i]), "%s", role);
    char *p = o->names[i];
    p[0] = 0;
    size_t l = 0;
    for (int k = 0; k < nnames && names[k]; k++) {
        size_t nl = strlen(names[k]);
        if (l + (l ? 1 : 0) + nl + 1 > CM_ROW_NAMES) {
            return false;                         // a name that does not fit whole would match a wrong file: refuse the row
        }
        if (l) {
            p[l++] = '|';
        }
        memcpy(p + l, names[k], nl + 1);
        l += nl;
    }
    CmReq &r = o->req[i];
    r.core = "";
    r.folder = "";
    r.role = o->role[i];
    r.names = o->names[i];
    for (int k = 0; k < 3; k++) {
        r.sizes[k] = sizes ? sizes[k] : 0;
    }
    r.flags = flags;
    r.set = set;
    o->n++;
    return true;
}

bool cm_rows_from_record(const uint8_t *rec, uint32_t n, CmRows *out)
{
    out->n = 0;
    DescRecord *d = new DescRecord;                  // large (the tables of a menu): on the heap, and only while a core is read
    char why[120];
    bool ok = descrec_parse(rec, n, d, why, sizeof(why)) == 0 && d->has_core && d->sec;
    for (int i = 0; ok && i < d->sec->nroms; i++) {
        const RomSpec &r = d->sec->roms[i];
        ok = row_add(out, r.role, r.names, ROM_NAMES_MAX, r.sizes, (r.required ? 1 : 0) | (r.shared ? 2 : 0), r.set);
    }
    if (ok && d->sec->has_romset) {
        const RomSetSpec &q = d->sec->romset;
        ok = row_add(out, q.role, &q.ext, 1, q.sizes, 1 | 4, 0);
    }
    delete d;
    if (!ok) {
        out->n = 0;
    }
    return ok;
}


// ---- the scan ------------------------------------------------------------------------------------------------------------------------

struct Cand {                    // a ROM file looked at for duplicates
    char path[CM_PATH];
    char name[56];
    uint64_t size;
    bool old;                    // in an old place (<medium>/roms, <cores>/roms), not in a core folder
    uint8_t sha[32];
    bool hashed;
};
static Cand cand[CM_FILES];
static int ncand;
static CmEntry le[3][64];        // listings: [0] the cores folder, [1] a folder in it, [2] one below
static CmEntry lroms[64];

static bool sha_of(FoFs &fs, const char *path, uint8_t out[32])
{
    static uint8_t buf[2048];
    void *h = fs.open_read(path);
    if (!h) {
        return false;
    }
    sha256_t c;
    sha256_init(&c);
    int n;
    while ((n = fs.read(h, buf, sizeof(buf))) > 0) {
        sha256_update(&c, buf, (uint32_t)n);
    }
    fs.close(h);
    if (n < 0) {
        return false;
    }
    sha256_final(&c, out);
    return true;
}

static void add_cand(CmReport *rep, const char *dir, const CmEntry &e, bool old)
{
    if (ncand >= CM_FILES) {
        rep->truncated = true;
        return;
    }
    Cand &c = cand[ncand++];
    snprintf(c.path, sizeof(c.path), "%s/%s", dir, e.name);
    snprintf(c.name, sizeof(c.name), "%s", e.name);
    c.size = e.size;
    c.old = old;
    c.hashed = false;
}

static CmOrphan *add_orphan(CmReport *rep, const char *path, int kind, bool dir, uint64_t size, bool deletable, const char *why)
{
    if (rep->norph >= CM_ORPHANS) {
        rep->truncated = true;
        return 0;
    }
    CmOrphan &o = rep->orph[rep->norph++];
    snprintf(o.path, sizeof(o.path), "%s", path);
    o.kind = (uint8_t)kind;
    o.dir = dir;
    o.size = size;
    o.deletable = deletable;
    snprintf(o.why, sizeof(o.why), "%s", why);
    return &o;
}

// A folder with no .cor: everything in it belongs to no core, except what a core would keep for the person - the files of saves/. A folder
// that holds such a file stays too. Returns true when something in it is kept.
static bool orphan_folder(CmFs &list, CmReport *rep, const char *path, int depth, bool in_saves)
{
    static CmEntry sub[3][64];
    CmEntry *mine = sub[depth];
    int n = list.list(path, le[2], 64);
    if (n < 0) {
        n = 0;
    }
    memcpy(mine, le[2], sizeof(CmEntry) * (size_t)n);
    CmOrphan *me = add_orphan(rep, path, CMK_FOLDER, true, 0, !in_saves, in_saves ? "saves are kept" : "folder of no core");
    bool kept = false;
    for (int i = 0; i < n; i++) {
        char p[CM_PATH];
        snprintf(p, sizeof(p), "%s/%s", path, mine[i].name);
        if (mine[i].dir) {
            if (depth < 2) {
                kept = orphan_folder(list, rep, p, depth + 1, in_saves || !strcasecmp(mine[i].name, "saves")) || kept;
            }
        } else {
            add_orphan(rep, p, CMK_FOLDER, false, mine[i].size, !in_saves, in_saves ? "a save is kept" : "belongs to no core");
            kept = kept || in_saves;
        }
    }
    if (me && kept) {
        me->deletable = false;
        snprintf(me->why, sizeof(me->why), "holds saves, kept");
    }
    return kept || in_saves;
}

// A folder of cores/ that holds a .cor of its own (the one-folder-per-core layout of T112) is a core's folder, not an orphan.
static bool folder_has_cor(CmFs &list, const char *path)
{
    int n = list.list(path, le[2], 64);
    for (int i = 0; i < n; i++) {
        if (!le[2][i].dir && (ends_with(le[2][i].name, ".cor") || ends_with(le[2][i].name, ".cfw"))) {
            return true;
        }
    }
    return false;
}

static void old_roms_dir(CmFs &list, CmReport *rep, const char *dir)
{
    CmEntry *e = lroms;
    int n = list.list(dir, e, 64);
    for (int i = 0; i < n; i++) {
        if (!e[i].dir) {
            add_cand(rep, dir, e[i], true);
        }
    }
    static CmEntry sub[64];
    for (int i = 0; i < n; i++) {
        if (!e[i].dir) {
            continue;
        }
        char p[CM_PATH];
        snprintf(p, sizeof(p), "%s/%s", dir, e[i].name);
        int m = list.list(p, sub, 64);
        for (int k = 0; k < m; k++) {
            if (!sub[k].dir) {
                add_cand(rep, p, sub[k], true);
            }
        }
    }
}

static void scan_medium(CmFs &list, FoFs &fs, CmReport *rep, const char *medium)
{
    (void)fs;
    char cores[CM_PATH];
    snprintf(cores, sizeof(cores), "/%s/cores", medium);
    CmEntry *e = le[0];
    int n = list.list(cores, e, 64);
    if (n < 0) {
        return;
    }
    if (n == 64) {
        rep->truncated = true;
    }
    for (int i = 0; i < n; i++) {                       // the cores first: the folders and the other files are judged against them
        if (e[i].dir || !ends_with(e[i].name, ".cor")) {
            continue;
        }
        if (rep->ncores >= CM_CORES) {
            rep->truncated = true;
            break;
        }
        CmCore &c = rep->core[rep->ncores++];
        memset(&c, 0, sizeof(c));
        snprintf(c.medium, sizeof(c.medium), "%s", medium);
        snprintf(c.file, sizeof(c.file), "%s", e[i].name);
        char path[CM_PATH], base[56];
        snprintf(path, sizeof(path), "%s/%s", cores, e[i].name);
        snprintf(base, sizeof(base), "%s", e[i].name);
        base[strlen(base) - 4] = 0;
        if (!list.core_name(path, c.name, sizeof(c.name))) {
            c.name[0] = 0;
        }
        for (int k = 0; k < n; k++) {
            c.folder = c.folder || (e[k].dir && !strcasecmp(e[k].name, base));
        }
        if (!c.folder) {
            continue;
        }
        char dir[CM_PATH];
        snprintf(dir, sizeof(dir), "%s/%s/roms", cores, base);
        int m = list.list(dir, lroms, 64);
        if (m < 0) {
            m = 0;
        }
        static CmRows rows;                              // a record core states its own needs (record 6); the table is for the cores that have none
        if (list.core_roms(path, &rows)) {
            cm_rom_status_rows(rows, lroms, m, &c.need, &c.have, c.missing, sizeof(c.missing));
        } else {
            cm_rom_status(c.name, lroms, m, &c.need, &c.have, c.missing, sizeof(c.missing));
        }
        for (int k = 0; k < m; k++) {
            if (!lroms[k].dir) {
                add_cand(rep, dir, lroms[k], false);
            }
        }
        snprintf(dir, sizeof(dir), "%s/%s/saves", cores, base);
        m = list.list(dir, lroms, 64);
        for (int k = 0; k < m; k++) {
            c.saves += !lroms[k].dir;
        }
    }
    for (int i = 0; i < n; i++) {
        char path[CM_PATH];
        snprintf(path, sizeof(path), "%s/%s", cores, e[i].name);
        char stem[56];
        snprintf(stem, sizeof(stem), "%s", e[i].name);
        char *dot = strrchr(stem, '.');
        if (e[i].dir) {
            if (!strcasecmp(e[i].name, "roms")) {
                old_roms_dir(list, rep, path);           // beside the cores, shared by all of them: the old place
                continue;
            }
            if (!strcasecmp(e[i].name, "apps")) {
                continue;                                // #43: the applications every core reads (APP_SHARED_FOLDER, coreapp/src/appindex.h): not a core's folder, not an orphan
            }
            bool has_cor = false;
            for (int k = 0; k < n && !has_cor; k++) {
                has_cor = !e[k].dir && ends_with(e[k].name, ".cor") && strlen(e[k].name) == strlen(e[i].name) + 4 &&
                          !strncasecmp(e[k].name, e[i].name, strlen(e[i].name));
            }
            if (!has_cor && !folder_has_cor(list, path)) {
                orphan_folder(list, rep, path, 0, false);
            }
            continue;
        }
        if (ends_with(e[i].name, ".cor") || ends_with(e[i].name, ".cfw")) {
            continue;
        }
        bool belongs = false;
        if (dot && (!strcasecmp(dot, ".cfg") || !strcasecmp(dot, ".icon"))) {
            for (int k = 0; k < n && !belongs; k++) {
                belongs = !e[k].dir && (ends_with(e[k].name, ".cor") || ends_with(e[k].name, ".cfw")) && strlen(e[k].name) == strlen(e[i].name) - strlen(dot) + 4 &&
                          !strncasecmp(e[k].name, e[i].name, strlen(e[i].name) - strlen(dot));
            }
        }
        if (!belongs) {
            add_orphan(rep, path, CMK_FILE, false, e[i].size, true, "belongs to no core");
        }
    }
}

void cm_scan(CmFs &list, FoFs &fs, CmReport *rep)
{
    memset(rep, 0, sizeof(*rep));
    ncand = 0;
    static CmEntry media[8];
    int nm = list.list("/", media, 8);
    for (int i = 0; i < nm; i++) {
        if (!media[i].dir || !strcasecmp(media[i].name, "flash")) {
            continue;
        }
        scan_medium(list, fs, rep, media[i].name);
        char roms[CM_PATH];
        snprintf(roms, sizeof(roms), "/%s/roms", media[i].name);
        old_roms_dir(list, rep, roms);
    }
    // duplicates: only files of one size can be equal, so only those are read
    for (int i = 0; i < ncand; i++) {
        bool same_size = false;
        for (int k = 0; k < ncand && !same_size; k++) {
            same_size = k != i && cand[k].size == cand[i].size && cand[i].size > 0;
        }
        if (same_size && !cand[i].hashed) {
            cand[i].hashed = sha_of(fs, cand[i].path, cand[i].sha);
        }
    }
    static bool grouped[CM_FILES];
    memset(grouped, 0, sizeof(grouped));
    for (int i = 0; i < ncand; i++) {
        bool copy_in_core = false;                      // an old file whose content is in a core folder
        if (!cand[i].hashed) {
            if (cand[i].old) {
                add_orphan(rep, cand[i].path, CMK_OLD_ROMS, false, cand[i].size, false, "old place, no copy in a core folder");
            }
            continue;
        }
        if (!grouped[i]) {
            int members[CM_FILES], nmem = 0;
            for (int k = i; k < ncand; k++) {
                if (cand[k].hashed && cand[k].size == cand[i].size && !memcmp(cand[k].sha, cand[i].sha, 32)) {
                    members[nmem++] = k;
                    grouped[k] = true;
                }
            }
            if (nmem > 1 && rep->ndups < CM_DUPS) {
                CmDup &d = rep->dup[rep->ndups++];
                snprintf(d.name, sizeof(d.name), "%s", cand[i].name);
                d.size = cand[i].size;
                d.n = nmem;
                for (int k = 0; k < nmem && k < 3; k++) {
                    snprintf(d.where[k], sizeof(d.where[k]), "%s", cand[members[k]].path);
                }
            } else if (nmem > 1) {
                rep->truncated = true;
            }
        }
        if (cand[i].old) {
            for (int k = 0; k < ncand && !copy_in_core; k++) {
                copy_in_core = !cand[k].old && cand[k].hashed && cand[k].size == cand[i].size && !memcmp(cand[k].sha, cand[i].sha, 32);
            }
            add_orphan(rep, cand[i].path, CMK_OLD_ROMS, false, cand[i].size, copy_in_core,
                       copy_in_core ? "a copy is in a core folder" : "old place, no copy in a core folder");
        }
    }
    for (int i = 0; i < rep->norph; i++) {
        if (rep->orph[i].deletable) {
            rep->ndelete++;
            rep->delete_bytes += rep->orph[i].size;
        }
    }
}

// ---- delete --------------------------------------------------------------------------------------------------------------------------

// strcasestr is not in the board's C library (run 1030)
static const char *find_ci(const char *s, const char *sub)
{
    size_t n = strlen(sub);
    for (; *s; s++) {
        if (!strncasecmp(s, sub, n)) {
            return s;
        }
    }
    return NULL;
}

bool cm_may_delete(FoFs &fs, const char *path)
{
    if (ends_with(path, ".cor") || ends_with(path, ".cfw") || !strncasecmp(path, "/flash/", 7)) {   // the flash is Gideon's firmware's (D3)
        return false;
    }
    const char *c = find_ci(path, "/cores/");
    if (c) {
        const char *rest = c + 7;
        const char *slash = strchr(rest, '/');
        if (!strncasecmp(rest, "apps", 4) && (rest[4] == 0 || rest[4] == '/')) {         // #43: the shared applications folder is never removed from a report
            return false;
        }
        if (slash) {                                     // inside a folder of cores/: not when a .cor of that name is there
            char cor[CM_PATH];
            size_t pre = (size_t)(slash - path);
            if (pre + 5 >= sizeof(cor)) {
                return false;
            }
            memcpy(cor, path, pre);
            strcpy(cor + pre, ".cor");
            FoStat st;
            if (fs.stat(cor, &st)) {
                return false;
            }
            char own[CM_PATH];                           // the one-folder layout: <folder>/<folder>.cor
            size_t comp = (size_t)(slash - rest);
            if (pre + 1 + comp + 5 < sizeof(own)) {
                memcpy(own, path, pre);
                own[pre] = '/';
                memcpy(own + pre + 1, rest, comp);
                strcpy(own + pre + 1 + comp, ".cor");
                if (fs.stat(own, &st)) {
                    return false;
                }
            }
        }
        char cor[CM_PATH];                               // a folder directly in cores/ whose .cor is there is that core's own folder
        FoStat st;
        if (strlen(path) + 5 < sizeof(cor)) {
            snprintf(cor, sizeof(cor), "%s.cor", path);
            if (fs.stat(cor, &st)) {
                return false;
            }
        }
        return true;
    }
    // the old medium roms folder: /<medium>/roms/...
    const char *s = path[0] == '/' ? strchr(path + 1, '/') : 0;
    return s && !strncasecmp(s, "/roms/", 6);
}

void cm_delete(FoFs &fs, const CmReport &rep, CmDelete *out)
{
    memset(out, 0, sizeof(*out));
    for (int pass = 0; pass < 2; pass++) {               // files, then the folders (children come after their parent in the list: backwards)
        for (int j = 0; j < rep.norph; j++) {
            int i = pass ? rep.norph - 1 - j : j;
            const CmOrphan &o = rep.orph[i];
            if (!o.deletable || o.dir != (pass == 1)) {
                continue;
            }
            if (!cm_may_delete(fs, o.path)) {
                out->refused++;
                continue;
            }
            if (fo_delete(fs, o.path) == FO_OK) {
                out->deleted++;
                out->bytes += o.size;
            } else {
                out->failed++;
            }
        }
    }
}
