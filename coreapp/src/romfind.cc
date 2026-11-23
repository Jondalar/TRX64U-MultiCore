#include "romfind.h"
#include "corefolder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

const char *const rom_roots[] = { "/flash", "/usb0", "/usb1", "/usb2", "/usb3", "/sd", 0 };

#define ROOTS_MAX 8
#define DIR_ENTRIES 40
#define SCRATCH_MAX (1u << 19)
#define SET_MEMBERS_MAX ROMS_MAX           // the ROMs of a descriptor that a set can name

static FsEntryShort listing[ROOTS_MAX][DIR_ENTRIES];
static int nlist[ROOTS_MAX];
static char dirs[ROOTS_MAX][96];
static uint8_t scratch[SCRATCH_MAX];

// The shared folder <root>/roms of every root (RomSpec::shared): listed only when a spec asks for it. A stick keeps the C64's whole ROM
// collection there, so the listing is longer than a core folder's.
#define SHARED_ENTRIES 128
static FsEntryShort slisting[ROOTS_MAX][SHARED_ENTRIES];
static int nslist[ROOTS_MAX];
static char sdirs[ROOTS_MAX][96];
static int nsdirs;

uint8_t *rom_scratch(uint32_t *size)
{
    *size = SCRATCH_MAX;
    return scratch;
}

static bool size_ok(const RomSpec &s, uint32_t size)
{
    for (int i = 0; i < ROM_SIZES_MAX && s.sizes[i]; i++) {
        if (s.sizes[i] == size) {
            return true;
        }
    }
    return false;
}

static int ndirs;
static char own_roms[96];                          // the roms folder beside the .cor of this scan ("" when there is none)

bool rom_own_dir(const char *cor_path, char *out, int outlen)
{
    return corefolder_sub(cor_path, "roms", out, outlen);
}

// The directories to look in, in order: `first` when the board names one (the monolith's /flash/roms, T104.2 D8), then
// <root>/roms/<folder> of every root.
static void list_roots(CoreFs *fs, const char *folder, const char *first, const char *own)
{
    for (int r = 0; r < ROOTS_MAX; r++) {
        nlist[r] = -1;
        dirs[r][0] = 0;
    }
    ndirs = 0;
    own_roms[0] = 0;
    if (own && own[0]) {                               // T148 D2: the core folder is the only place a core reads ROMs from
        snprintf(own_roms, sizeof(own_roms), "%s", own);
        snprintf(dirs[ndirs], sizeof(dirs[ndirs]), "%s", own);
        nlist[ndirs] = fs->list_short(dirs[ndirs], listing[ndirs], DIR_ENTRIES);
        ndirs++;
        return;
    }
    if (first && first[0]) {
        snprintf(dirs[ndirs], sizeof(dirs[ndirs]), "%s", first);
        nlist[ndirs] = fs->list_short(dirs[ndirs], listing[ndirs], DIR_ENTRIES);
        ndirs++;
    }
    for (int r = 0; rom_roots[r] && ndirs < ROOTS_MAX; r++) {
        snprintf(dirs[ndirs], sizeof(dirs[ndirs]), "%s/roms/%s", rom_roots[r], folder);
        nlist[ndirs] = fs->list_short(dirs[ndirs], listing[ndirs], DIR_ENTRIES);
        ndirs++;
    }
}

static void list_shared(CoreFs *fs)
{
    nsdirs = 0;
    for (int r = 0; rom_roots[r] && nsdirs < ROOTS_MAX; r++) {
        snprintf(sdirs[nsdirs], sizeof(sdirs[nsdirs]), "%s/roms", rom_roots[r]);
        nslist[nsdirs] = fs->list_short(sdirs[nsdirs], slisting[nsdirs], SHARED_ENTRIES);
        nsdirs++;
    }
}

// Directory number r of a search: the core's own directories first, then (for a spec with `shared`) the shared ones, same root order.
static const char *dir_at(int r) { return r < ndirs ? dirs[r] : sdirs[r - ndirs]; }
static const FsEntryShort *list_at(int r) { return r < ndirs ? listing[r] : slisting[r - ndirs]; }
static int count_at(int r) { return r < ndirs ? nlist[r] : nslist[r - ndirs]; }

static void names_of(const RomSpec &s, char *out, int n)
{
    out[0] = 0;
    for (int i = 0; i < ROM_NAMES_MAX && s.names[i]; i++) {
        strncat(out, i ? " or " : "", n - strlen(out) - 1);
        strncat(out, s.names[i], n - strlen(out) - 1);
    }
}

static void make_text_base(const RomSpec &s, RomStatus bad, const char *bad_text, const char *folder, char *out, int outlen);

// The sentence for a ROM that is not found: what was looked for and where, or what was wrong with the file that was there.
// `extra` (T258: the names of a ROM's parts) is added to the sentence of a ROM that is missing; a file that was there and refused has its own sentence.
static void make_text(const RomSpec &s, RomStatus bad, const char *bad_text, const char *folder, char *out, int outlen, const char *extra = "")
{
    make_text_base(s, bad, bad_text, folder, out, outlen);
    if (extra[0] && bad == ROM_MISSING) {
        strncat(out, extra, outlen - strlen(out) - 1);
    }
}

static void make_text_base(const RomSpec &s, RomStatus bad, const char *bad_text, const char *folder, char *out, int outlen)
{
    char nm[120];
    names_of(s, nm, sizeof(nm));
    if (own_roms[0] && bad == ROM_MISSING) {            // T148 D4: one path, the core folder
        snprintf(out, outlen, "%s: %s is missing in %s/", s.role, nm, own_roms);
        return;
    }
    const char *where = own_roms[0] ? own_roms : 0;
    char put[130];
    if (where) {
        snprintf(put, sizeof(put), "%s/", where);
    } else {
        snprintf(put, sizeof(put), "roms/%s", folder);
    }
    if (bad == ROM_WRONG_SIZE) {
        char sizes[60] = "";
        for (int k = 0; k < ROM_SIZES_MAX && s.sizes[k]; k++) {
            char one[16];
            snprintf(one, sizeof(one), "%s%u", k ? " or " : "", (unsigned)s.sizes[k]);
            strncat(sizes, one, sizeof(sizes) - strlen(sizes) - 1);
        }
        snprintf(out, outlen, "%s: %s, but %s bytes are expected. Put the right file in %s.", s.role, bad_text, sizes, put);
    } else if (bad == ROM_BAD_CONTENT) {
        snprintf(out, outlen, "%s: %s. Put the right file in %s.", s.role, bad_text, put);
    } else if (s.shared) {
        snprintf(out, outlen, "%s (%s) not found. Looked in roms/%s and in the shared roms folder of the flash, the USB sticks and the SD card.",
                 s.role, nm, folder);
    } else {
        char roots[200] = "";
        for (int r = 0; r < ndirs; r++) {
            strncat(roots, r ? ", " : "", sizeof(roots) - strlen(roots) - 1);
            strncat(roots, dirs[r], sizeof(roots) - strlen(roots) - 1);
        }
        if (own_roms[0]) {
            snprintf(out, outlen, "%s (%s) not found. Looked in %s. Put it in %s, beside the core, or in roms/%s on a USB stick or SD card.",
                     s.role, nm, roots, own_roms, folder);
        } else {
            snprintf(out, outlen, "%s (%s) not found. Looked in %s. Put it in roms/%s on a USB stick or SD card.", s.role, nm, roots, folder);
        }
    }
}

RomStatus rom_scan(CoreFs *fs, const char *folder, const RomSpec *specs, int n, RomResult *out, char *summary, int sumlen, const char *first_dir, const char *own_dir)
{
    list_roots(fs, folder, first_dir, own_dir);
    nsdirs = 0;
    for (int i = 0; i < n; i++) {
        if (specs[i].shared && !own_roms[0]) {
            list_shared(fs);
            break;
        }
    }
    RomStatus worst = ROM_FOUND;
    static char member_text[SET_MEMBERS_MAX][200];     // what is told about each ROM that belongs to a set and is not found
    summary[0] = 0;
    for (int k = 0; k < 2 * n; k++) {                  // T258: the parts of an image first (pass 0), so that the spec they go into finds them judged
        const int i = k % n;
        if ((specs[i].into != 0) != (k < n)) {
            continue;
        }
        const RomSpec &s = specs[i];
        RomResult &o = out[i];
        memset(&o, 0, sizeof(o));
        o.status = s.required ? ROM_MISSING : ROM_ABSENT;
        RomStatus bad = ROM_MISSING;
        char bad_text[160] = "";
        bool found = false;
        int nsearch = ndirs + (s.shared ? nsdirs : 0);
        for (int r = 0; r < nsearch && !found; r++) {
            const char *dir = dir_at(r);
            const FsEntryShort *ls = list_at(r);
            int cnt = count_at(r);
            for (int ni = 0; ni < ROM_NAMES_MAX && s.names[ni] && !found; ni++) {
                for (int e = 0; e < cnt && !found; e++) {
                    const FsEntryShort &fe = ls[e];
                    if (fe.dir || fe.cut || strcasecmp(fe.name, s.names[ni]) != 0) {
                        continue;
                    }
                    if (!size_ok(s, fs_size32(fe.size))) {
                        if (bad == ROM_MISSING) {
                            bad = ROM_WRONG_SIZE;
                            snprintf(bad_text, sizeof(bad_text), "%s in %s is %u bytes", fe.name, dir, (unsigned)fs_size32(fe.size));
                        }
                        continue;
                    }
                    if (s.check) {
                        char path[160];
                        snprintf(path, sizeof(path), "%s/%s", dir, fe.name);
                        int got = fs->load(path, scratch, SCRATCH_MAX);
                        if (got != (int)fe.size || !s.check(scratch, fs_size32(fe.size))) {
                            if (bad == ROM_MISSING || bad == ROM_WRONG_SIZE) {
                                bad = ROM_BAD_CONTENT;
                                snprintf(bad_text, sizeof(bad_text), "%s in %s is not a %s image", fe.name, dir, s.role);
                            }
                            continue;
                        }
                    }
                    found = true;
                    o.status = ROM_FOUND;
                    strncpy(o.hit.dir, dir, sizeof(o.hit.dir) - 1);
                    strncpy(o.hit.name, fe.name, sizeof(o.hit.name) - 1);
                    o.hit.size = fs_size32(fe.size);
                }
            }
        }
        char text[360] = "";                           // T258: what is told instead of the standard sentence when the ROM's parts are (partly) there
        char hint[200] = "";
        int np = 0, nf = 0, miss = -1, refused = -1;
        for (int j = 0; j < n && !found; j++) {
            if (specs[j].into != i + 1) {
                continue;
            }
            np++;
            nf += out[j].status == ROM_FOUND;
            if (out[j].status != ROM_FOUND && miss < 0) {
                miss = j;
            }
            if (out[j].status >= ROM_WRONG_SIZE && refused < 0) {
                refused = j;
            }
        }
        if (np && nf == np) {
            found = true;
            o.status = ROM_FOUND;
            o.composed = true;
            for (int j = 0; j < n; j++) {
                if (specs[j].into == i + 1) {
                    strncpy(o.hit.dir, out[j].hit.dir, sizeof(o.hit.dir) - 1);
                    break;
                }
            }
            o.hit.size = s.sizes[0];
        } else if (np) {
            snprintf(hint, sizeof(hint), " Or its parts:");
            for (int j = 0; j < n; j++) {
                if (specs[j].into == i + 1) {
                    strncat(hint, " ", sizeof(hint) - strlen(hint) - 1);
                    strncat(hint, specs[j].names[0], sizeof(hint) - strlen(hint) - 1);
                    strncat(hint, ",", sizeof(hint) - strlen(hint) - 1);
                }
            }
            hint[strlen(hint) - 1] = '.';
            if (bad == ROM_MISSING && (refused >= 0 || nf > 0)) {
                if (refused >= 0) {
                    bad = out[refused].status;
                    snprintf(text, sizeof(text), "%s", out[refused].why);
                } else {
                    make_text(specs[miss], ROM_MISSING, "", folder, text, sizeof(text));
                    bad = ROM_MISSING;
                }
                strncat(text, " The ", sizeof(text) - strlen(text) - 1);
                strncat(text, s.role, sizeof(text) - strlen(text) - 1);
                strncat(text, " needs all its parts.", sizeof(text) - strlen(text) - 1);
            }
        }
        if (found) {
            continue;
        }
        if (!s.required && !s.set) {
            if (bad != ROM_MISSING) {                  // T185 S3: an optional file that was there and refused is not silently absent; the worst status and the summary stay the required ROMs'
                o.status = bad;
                if (text[0]) {
                    snprintf(o.why, sizeof(o.why), "%s", text);
                } else {
                    make_text(s, bad, bad_text, folder, o.why, sizeof(o.why));
                }
            }
            continue;
        }
        o.status = bad;
        if (s.set) {                                   // judged with its set below
            if (i < SET_MEMBERS_MAX) {
                make_text(s, bad, bad_text, folder, member_text[i], sizeof(member_text[i]));
            }
            continue;
        }
        if (bad > worst) {
            worst = bad;
        }
        if (!summary[0]) {
            if (text[0]) {
                snprintf(summary, sumlen, "%s", text);
            } else {
                make_text(s, bad, bad_text, folder, summary, sumlen, hint);
            }
        }
    }
    // the sets (desc.h): one complete set is enough; else what is told is the first missing ROM of the set that lacks the fewest (the first of
    // them when two lack as many)
    bool any_set = false, complete = false;
    int best_missing = 1000, best = -1;
    for (int bit = 0; bit < 8; bit++) {
        bool has = false;
        int missing = 0, first = -1;
        for (int i = 0; i < n; i++) {
            if (specs[i].set & (1 << bit)) {
                has = true;
                if (out[i].status != ROM_FOUND) {
                    missing++;
                    first = first < 0 ? i : first;
                }
            }
        }
        any_set = any_set || has;
        complete = complete || (has && missing == 0);
        if (has && missing && missing < best_missing) {
            best_missing = missing;
            best = first;
        }
    }
    if (any_set && !complete && best >= 0) {
        if (out[best].status > worst) {
            worst = out[best].status;
        }
        if (!summary[0] && best < SET_MEMBERS_MAX) {
            snprintf(summary, sumlen, "%s One complete set of the machine's ROMs is enough.", member_text[best]);
        }
    }
    return worst;
}

#define IMPORT_ENTRIES SHARED_ENTRIES
static FsEntryShort imp_list[IMPORT_ENTRIES];

static bool name_in_spec(const RomSpec &s, const char *name)
{
    for (int i = 0; i < ROM_NAMES_MAX && s.names[i]; i++) {
        if (!strcasecmp(s.names[i], name)) {
            return true;
        }
    }
    return false;
}

int rom_import(CoreFs *fs, const char *folder, const RomSpec *specs, int n, const char *own_dir, const char *first_dir, const char *old_roms)
{
    if (!own_dir || !own_dir[0]) {
        return 0;
    }
    bool lack[SET_MEMBERS_MAX * 4];
    int nl = 0;
    bool any_shared = false;
    int m = n < (int)(sizeof(lack) / sizeof(lack[0])) ? n : (int)(sizeof(lack) / sizeof(lack[0]));
    int nown = fs->list_short(own_dir, imp_list, IMPORT_ENTRIES);
    for (int i = 0; i < m; i++) {                     // a spec whose name is in the core folder (whatever its size) is not copied over
        bool have = false;
        for (int e = 0; e < nown && !have; e++) {
            have = !imp_list[e].dir && name_in_spec(specs[i], imp_list[e].name);
        }
        lack[i] = !have;
        nl += !have;
        any_shared = any_shared || specs[i].shared;
    }
    for (int i = 0; i < m; i++) {                     // T258: an image whose parts all lie in the core folder is there; the parts of an image that is there are not copied
        int np = 0, nlp = 0;
        for (int j = 0; j < m; j++) {
            if (specs[j].into == i + 1) {
                np++;
                nlp += lack[j];
            }
        }
        if (np && !nlp && lack[i]) {
            lack[i] = false;
            nl--;
        }
        if (np && !lack[i]) {
            for (int j = 0; j < m; j++) {
                if (specs[j].into == i + 1 && lack[j]) {
                    lack[j] = false;
                    nl--;
                }
            }
        }
    }
    if (!nl) {
        return 0;
    }
    char srcs[ROOTS_MAX * 2 + 2][96];
    int ns = 0;
    if (first_dir && first_dir[0]) {
        snprintf(srcs[ns++], sizeof(srcs[0]), "%s", first_dir);
    }
    for (int r = 0; rom_roots[r]; r++) {
        snprintf(srcs[ns++], sizeof(srcs[0]), "%s/roms/%s", rom_roots[r], folder);
    }
    int shared_first = ns, shared_end = ns;           // srcs[shared_first .. shared_end) are the shared <root>/roms folders
    if (any_shared) {
        for (int r = 0; rom_roots[r]; r++) {
            snprintf(srcs[ns++], sizeof(srcs[0]), "%s/roms", rom_roots[r]);
        }
    }
    shared_end = ns;
    if (old_roms && old_roms[0]) {
        snprintf(srcs[ns++], sizeof(srcs[0]), "%s", old_roms);
    }
    int copied = 0;
    for (int d = 0; d < ns && nl > 0; d++) {
        if (!strcasecmp(srcs[d], own_dir)) {
            continue;
        }
        int cnt = fs->list_short(srcs[d], imp_list, IMPORT_ENTRIES);
        for (int i = 0; i < m && cnt > 0; i++) {
            if (!lack[i]) {
                continue;
            }
            const RomSpec &s = specs[i];
            if (s.into && s.into <= m && !lack[s.into - 1]) {   // the image it is a part of has come (the image's spec comes first in the list)
                lack[i] = false;
                nl--;
                continue;
            }
            bool shared_dir = d >= shared_first && d < shared_end;
            if (shared_dir && !s.shared) {             // the shared <root>/roms is a source for a device ROM only: a machine ROM there is another machine's
                continue;
            }
            for (int ni = 0; ni < ROM_NAMES_MAX && s.names[ni] && lack[i]; ni++) {
                for (int e = 0; e < cnt && lack[i]; e++) {
                    const FsEntryShort &fe = imp_list[e];
                    if (fe.dir || fe.cut || strcasecmp(fe.name, s.names[ni]) != 0 || !size_ok(s, fs_size32(fe.size))) {
                        continue;
                    }
                    char sp[200], dp[200];
                    snprintf(sp, sizeof(sp), "%s/%s", srcs[d], fe.name);
                    snprintf(dp, sizeof(dp), "%s/%s", own_dir, fe.name);
                    int got = fs->load(sp, scratch, SCRATCH_MAX);
                    if (got != (int)fe.size || (s.check && !s.check(scratch, fs_size32(fe.size)))) {
                        continue;
                    }
                    fs->mkdir(own_dir);
                    if (corefolder_copy_verified(fs, sp, dp, scratch, SCRATCH_MAX)) {
                        lack[i] = false;
                        nl--;
                        copied++;
                    }
                }
            }
        }
    }
    return copied;
}

bool rom_compose(CoreFs *fs, const RomSpec *specs, int n, int i, const RomResult *res, uint8_t *buf)
{
    memset(buf, 0, specs[i].sizes[0]);
    for (int j = 0; j < n; j++) {
        if (specs[j].into != i + 1) {
            continue;
        }
        char path[160];
        snprintf(path, sizeof(path), "%s/%s", res[j].hit.dir, res[j].hit.name);
        if (res[j].status != ROM_FOUND || fs->load(path, buf + specs[j].at, res[j].hit.size) != (int)res[j].hit.size) {
            return false;
        }
    }
    return true;
}

bool rom_read(CoreFs *fs, const RomHit *hit, uint8_t *buf)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", hit->dir, hit->name);
    return fs->load(path, buf, hit->size) == (int)hit->size;
}

static uint32_t hash_text(const char *s)
{
    uint32_t h = 2166136261u;                          // FNV-1a
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h;
}

RomWait::RomWait() : tries_(0), told_(false), last_(0)
{
}

bool RomWait::step(RomStatus worst, const char *msg)
{
    if (worst == ROM_FOUND) {
        tries_ = 0;
        told_ = false;
        last_ = 0;
        return false;
    }
    tries_++;
    bool due = worst == ROM_MISSING ? tries_ >= 20 : true;
    uint32_t h = hash_text(msg);
    if (!due || (told_ && h == last_)) {
        return false;
    }
    last_ = h;
    told_ = true;
    return true;
}
