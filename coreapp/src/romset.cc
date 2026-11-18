#include "romset.h"
#include "engines.h"
#include "romfind.h"
#include "corefolder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define LIST_MAX 128

static FsEntryShort listing[LIST_MAX];

static bool ext_is(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}

static bool size_ok(const RomSetSpec &s, uint64_t size)
{
    for (int i = 0; i < ROMSET_SIZES && s.sizes[i]; i++) {
        if (s.sizes[i] == size) {
            return true;
        }
    }
    return false;
}

static void write_labels(const RomSetSpec &s, const RomSetState &rs)
{
    RomSetLabels *l = s.labels;
    snprintf(l->text[0], ROMSET_NAME, "%s", s.auto_label);
    for (int i = 1; i <= ROMSET_MAX; i++) {
        snprintf(l->text[i], ROMSET_NAME, "%s", i <= rs.count ? rs.m[i - 1].name : "-");
    }
    for (int i = 0; i <= ROMSET_MAX; i++) {
        l->item[i] = l->text[i];
    }
}

// T148 D2: files of the set's kind that lie in the old places (the board's folder, <root>/roms/<folder>) are copied into the core folder,
// verified, once. The roms folder beside the .cor is no source here: a set has no file name that says it belongs to this machine.
static FsEntryShort implist[LIST_MAX];

static void romset_import(CoreCtx *c, const char *own)
{
    const RomSetSpec &s = *c->desc->romset;
    uint32_t impmax;
    uint8_t *impbuf = rom_scratch(&impmax);
    static FsEntryShort have[LIST_MAX];
    int nown = c->fs->list_short(own, have, LIST_MAX);
    if (nown < 0) {                                   // no core folder yet: -1, and have[nown] below would write before the array
        nown = 0;
    }
    int total = 0;
    for (int i = 0; i < nown; i++) {
        total += !have[i].dir && ext_is(have[i].name, s.ext);
    }
    char srcs[8][96];
    int ns = 0;
    if (c->board && c->board->rom_dir && c->board->rom_dir[0]) {
        snprintf(srcs[ns++], sizeof(srcs[0]), "%s", c->board->rom_dir);
    }
    for (int r = 0; rom_roots[r] && ns < 8; r++) {
        snprintf(srcs[ns++], sizeof(srcs[0]), "%s/roms/%s", rom_roots[r], c->desc->folder);
    }
    for (int d = 0; d < ns && total < s.max; d++) {
        if (!strcasecmp(srcs[d], own)) {
            continue;
        }
        int n = c->fs->list_short(srcs[d], implist, LIST_MAX);
        for (int i = 0; i < n && total < s.max; i++) {
            const FsEntryShort &fe = implist[i];
            if (fe.dir || fe.cut || !ext_is(fe.name, s.ext) || !size_ok(s, fe.size) || fe.size > impmax) {
                continue;
            }
            bool there = false;
            for (int k = 0; k < nown && !there; k++) {
                there = !have[k].dir && !strcasecmp(have[k].name, fe.name);
            }
            if (there) {
                continue;
            }
            char sp[200], dp[200];
            snprintf(sp, sizeof(sp), "%s/%s", srcs[d], fe.name);
            snprintf(dp, sizeof(dp), "%s/%s", own, fe.name);
            c->fs->mkdir(own);
            if (corefolder_copy_verified(c->fs, sp, dp, impbuf, impmax)) {
                total++;
                if (nown < LIST_MAX) {
                    have[nown] = fe;
                    nown++;
                }
            }
        }
    }
}

RomStatus romset_scan(CoreCtx *c, char *summary, int sumlen)
{
    const RomSetSpec &s = *c->desc->romset;
    RomSetState &rs = engines_of(c)->rs;
    const int max = s.max > ROMSET_MAX ? ROMSET_MAX : s.max;
    // the directories, in the order of the ROM manager: the folder beside the .cor, the board's own, then <root>/roms/<folder>
    char dirs[8][96];
    int ndirs = 0;
    bool own_only = rom_own_dir(c->cor_path, dirs[0], sizeof(dirs[0]));
    if (own_only) {                                                      // T148 D2: the core folder alone; the old places are copied from
        ndirs++;
        romset_import(c, dirs[0]);
    } else {
        if (c->board && c->board->rom_dir && c->board->rom_dir[0]) {
            snprintf(dirs[ndirs++], sizeof(dirs[0]), "%s", c->board->rom_dir);
        }
        for (int r = 0; rom_roots[r] && ndirs < 8; r++) {
            snprintf(dirs[ndirs++], sizeof(dirs[0]), "%s/roms/%s", rom_roots[r], c->desc->folder);
        }
    }
    char other[ROMSET_NAME] = "";
    for (int d = 0; d < ndirs; d++) {
        int n = c->fs->list_short(dirs[d], listing, LIST_MAX);
        if (n < 0) {
            continue;
        }
        int any = 0, cnt = 0;
        RomSetMember list[ROMSET_MAX];
        for (int i = 0; i < n; i++) {
            const FsEntryShort &fe = listing[i];
            if (fe.dir || fe.cut || !ext_is(fe.name, s.ext)) {
                continue;
            }
            any++;
            if (!other[0]) {
                snprintf(other, sizeof(other), "%s", fe.name);
            }
            if (!size_ok(s, fe.size) || cnt >= max || strlen(fe.name) >= ROMSET_NAME) {
                continue;
            }
            KsCandidate cand = { fs_size32(fe.size), 0, 0 };
            uint8_t head[32];
            if (s.version && s.head_bytes && s.head_bytes <= sizeof(head) && fe.size >= s.head_bytes) {
                char path[200];
                snprintf(path, sizeof(path), "%s/%s", dirs[d], fe.name);
                CoreFile *f = c->fs->open(path, false);
                if (f) {
                    if (f->read(0, head, s.head_bytes)) {
                        s.version(head, s.head_bytes, &cand.ver, &cand.rev);
                    }
                    c->fs->close(f);
                }
            }
            int k = cnt++;
            while (k > 0 && strcasecmp(list[k - 1].name, fe.name) > 0) {         // insertion sort by name
                list[k] = list[k - 1];
                k--;
            }
            memset(&list[k], 0, sizeof(list[k]));
            snprintf(list[k].name, ROMSET_NAME, "%s", fe.name);
            list[k].c = cand;
        }
        if (!any) {
            continue;
        }
        if (cnt == 0) {
            snprintf(summary, sumlen, "%s: %s in %s is neither %s. Put %s (%s) there.", s.role, other, dirs[d], s.size_text, s.example, s.size_text);
            return ROM_WRONG_SIZE;
        }
        char before[(ROMSET_MAX + 1) * ROMSET_NAME];
        before[0] = 0;
        for (int i = 0; i <= ROMSET_MAX; i++) {
            strncat(before, s.labels->text[i], sizeof(before) - strlen(before) - 1);
        }
        memcpy(rs.m, list, sizeof(list));
        rs.count = cnt;
        snprintf(rs.dir, sizeof(rs.dir), "%s", dirs[d]);
        rs.scanned = true;
        write_labels(s, rs);
        char after[(ROMSET_MAX + 1) * ROMSET_NAME];
        after[0] = 0;
        for (int i = 0; i <= ROMSET_MAX; i++) {
            strncat(after, s.labels->text[i], sizeof(after) - strlen(after) - 1);
        }
        if (strcmp(before, after) != 0 && c->settings && !c->settings->safe()) {
            c->settings->load();                      // the .cfg may name a file that was not in the list before (not in safe mode: it is not read)
        }
        summary[0] = 0;
        return ROM_FOUND;
    }
    if (own_only) {                                                      // D4: one path
        snprintf(summary, sumlen, "%s (a %s %s file, e.g. %s) is missing in %s/", s.role, s.size_text, s.ext, s.example, dirs[0]);
        return ROM_MISSING;
    }
    snprintf(summary, sumlen, "%s not found. Put a %s %s file (e.g. %s) in roms/%s on a USB stick or SD card.", s.role, s.size_text, s.ext,
             s.example, c->desc->folder);
    return ROM_MISSING;
}

int romset_choice(CoreCtx *c)
{
    const RomSetSpec &s = *c->desc->romset;
    RomSetState &rs = engines_of(c)->rs;
    if (rs.count <= 0) {
        return -1;
    }
    int sel = c->settings ? c->settings->find(s.choice_key) : -1;
    sel = sel >= 0 ? c->settings->value(sel) : 0;
    if (sel >= 1 && sel <= rs.count) {
        return sel - 1;
    }
    if (!s.pick) {
        return 0;
    }
    KsCandidate cand[ROMSET_MAX];
    for (int i = 0; i < rs.count; i++) {
        cand[i] = rs.m[i].c;
    }
    return s.pick(cand, rs.count, s.want);
}

bool romset_stale(CoreCtx *c)
{
    if (!c->desc->romset) {
        return false;
    }
    RomSetState &rs = engines_of(c)->rs;
    int i = romset_choice(c);
    return i >= 0 && strcmp(rs.loaded, rs.m[i].name) != 0;
}

void romset_clear(CoreCtx *c)
{
    const RomSetSpec &s = *c->desc->romset;
    static uint8_t zero[4096];
    for (uint32_t o = 0; o < s.clear_below; o += sizeof(zero)) {
        c->io->ddr_write(o, zero, s.clear_below - o < sizeof(zero) ? s.clear_below - o : (uint32_t)sizeof(zero));
    }
}

bool romset_hit(CoreCtx *c, RomHit *hit)
{
    RomSetState &rs = engines_of(c)->rs;
    int i = romset_choice(c);
    if (i < 0) {
        return false;
    }
    memset(hit, 0, sizeof(*hit));
    snprintf(hit->dir, sizeof(hit->dir), "%s", rs.dir);
    snprintf(hit->name, sizeof(hit->name), "%s", rs.m[i].name);
    hit->size = rs.m[i].c.size;
    rs.chosen = i;
    return true;
}

int romset_put(CoreCtx *c, const uint8_t *data, uint32_t size, char *msg, int msglen)
{
    const RomSetSpec &s = *c->desc->romset;
    RomSetState &rs = engines_of(c)->rs;
    int copies = (s.mirror_size && size == s.mirror_size) ? 2 : 1;
    static uint8_t back[4096];
    for (int k = 0; k < copies; k++) {
        uint32_t base = s.window_off + (uint32_t)k * s.mirror_stride;
        for (uint32_t o = 0; o < size; o += sizeof(back)) {
            uint32_t n = size - o < sizeof(back) ? size - o : (uint32_t)sizeof(back);
            c->io->ddr_write(base + o, data + o, n);
            c->io->ddr_read(base + o, back, n);
            if (memcmp(back, data + o, n) != 0) {
                snprintf(msg, msglen, "%s: the core's memory did not take the ROM (read-back differs)", s.role);
                return 2;
            }
        }
    }
    if (rs.chosen >= 0 && rs.chosen < rs.count) {
        snprintf(rs.loaded, sizeof(rs.loaded), "%s", rs.m[rs.chosen].name);
        printf("[core] %s %s/%s (%u bytes, version %u.%u)\n", s.role, rs.dir, rs.m[rs.chosen].name, (unsigned)size,
               (unsigned)rs.m[rs.chosen].c.ver, (unsigned)rs.m[rs.chosen].c.rev);
    }
    return 0;
}
