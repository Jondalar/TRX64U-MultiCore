#include "start.h"
#include "corefolder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "keyboard.h"

const CoreDesc *core_find(const char id[4])
{
    for (const CoreDesc *const *p = core_table; *p; p++) {
        if (memcmp((*p)->id, id, 4) == 0) {
            return *p;
        }
    }
    return 0;
}

const CoreDesc *core_find_by_name(const char *cor_name)
{
    for (const CoreDesc *const *p = core_table; *p; p++) {
        if (!strcasecmp((*p)->cor_name, cor_name)) {
            return *p;
        }
    }
    return 0;
}

// T115 7 H2: does the hand-over name this descriptor? Both sides have an id: the ids decide, the name is not looked at. Otherwise the name
// decides, as it did before the id existed (a layer-2 .cor, an older host, a descriptor without core_id).
static bool bundle_names(const CoreDesc *d, const Handover *h)
{
    const char *hid = handover_core_id(h);
    if (hid[0] && d->core_id && d->core_id[0]) {
        return strcasecmp(hid, d->core_id) == 0;
    }
    return h->core_name[0] && strcasecmp(h->core_name, d->cor_name) == 0;
}

static bool bundle_says_something(const Handover *h)
{
    return h->core_name[0] || handover_core_id(h)[0];
}

static const char *bundle_label(const Handover *h)
{
    return h->core_name[0] ? h->core_name : handover_core_id(h);
}

static char printable(uint8_t c)
{
    return (c >= 32 && c < 127) ? (char)c : '?';
}

StartResult start_identify(const uint8_t id[4], const Handover *h, const CoreDesc **desc, char *msg, int msglen, CoreIo *io, DescSource *src)
{
    *desc = 0;
    const CoreDesc *d = core_find((const char *)id);
    if (!d && src) {                                           // T239.3 D17: no descriptor compiled in: the bundle's data description makes one
        d = src->describe(id, h, msg, msglen);
        if (!d) {
            return START_NO_DESCRIPTOR;
        }
    }
    if (!d) {
        snprintf(msg, msglen, "This application has no description for the core with the ID %c%c%c%c. The core stays held.",
                 printable(id[0]), printable(id[1]), printable(id[2]), printable(id[3]));
        return START_NO_DESCRIPTOR;
    }
    bool have = h && handover_valid(h);
    if (d->id_shared) {
        // One gateware, several machines (T104 5.5: the Amiga and the A1200 answer with the same ID word): the ID block cannot say which
        // descriptor is meant, the bundle's name can. Without a name there is nothing to choose by, and nothing is guessed.
        const CoreDesc *named = 0;
        char all[160] = "";
        for (const CoreDesc *const *p = core_table; *p; p++) {
            if (memcmp((*p)->id, id, 4) == 0 && (*p)->id_shared) {
                strncat(all, all[0] ? "; " : "", sizeof(all) - strlen(all) - 1);
                strncat(all, (*p)->cor_name, sizeof(all) - strlen(all) - 1);
                if (have && bundle_names(*p, h)) {
                    named = *p;
                }
            }
        }
        if (!named && io && !(have && h->core_name[0])) {
            // no bundle name: the gateware's own feature bits, when exactly one descriptor matches them
            int n = 0;
            const CoreDesc *byfeat = 0;
            for (const CoreDesc *const *p = core_table; *p; p++) {
                if (memcmp((*p)->id, id, 4) == 0 && (*p)->id_shared && (*p)->id_feat_mask &&
                    (io->rd((*p)->id_feat_reg) & (*p)->id_feat_mask) == (*p)->id_feat_val) {
                    byfeat = *p;
                    n++;
                }
            }
            if (n == 1) {
                named = byfeat;
            }
        }
        if (named) {
            d = named;
        } else if (!have || !bundle_says_something(h)) {
            snprintf(msg, msglen, "The ID %c%c%c%c belongs to several machines (%s) and there is no bundle to say which. The core stays held.",
                     printable(id[0]), printable(id[1]), printable(id[2]), printable(id[3]), all);
            return START_AMBIGUOUS;
        } else {
            snprintf(msg, msglen, "The bundle is \"%s\" but the core in the FPGA is one of \"%s\". The core stays held.", bundle_label(h), all);
            return START_COR_MISMATCH;
        }
    }
    if (have && bundle_says_something(h) && !bundle_names(d, h)) {
        snprintf(msg, msglen, "The bundle is \"%s\" but the core in the FPGA is \"%s\". The core stays held.", bundle_label(h), d->cor_name);
        return START_COR_MISMATCH;
    }
    snprintf(msg, msglen, "[core] %c%c%c%c %s, cor %s", d->id[0], d->id[1], d->id[2], d->id[3], d->cor_name,
             have ? h->path : "(no hand-over)");
    *desc = d;
    return START_OK;
}

void start_paths(CoreCtx *c, const Handover *h, const BoardProfile *prof)
{
    c->cor_path[0] = c->cfg_path[0] = c->core_version[0] = c->host_version[0] = 0;
    c->host_key = 0;
    if (h && handover_valid(h)) {
        c->host_key = start_host_key(h);
        strncpy(c->cor_path, h->path, sizeof(c->cor_path) - 1);
        c->cor_path[sizeof(c->cor_path) - 1] = 0;
        snprintf(c->host_version, sizeof(c->host_version), "%s", handover_host_version(h));
        strncpy(c->core_version, h->core_version, sizeof(c->core_version) - 1);
        c->core_version[sizeof(c->core_version) - 1] = 0;
        if (!corefolder_cfg(c->cor_path, c->cfg_path, sizeof(c->cfg_path))) {          // T148 D1: in the core folder
            c->cfg_path[0] = 0;
        }
    }
    if (!c->cfg_path[0] && prof && prof->flash_cfg && prof->cfg_dir && c->desc && c->desc->folder) {       // T228 DC13: the parameter decides
        snprintf(c->cfg_path, sizeof(c->cfg_path), "%s/%s.cfg", prof->cfg_dir, c->desc->folder);
    }
}

// T230 P-2: does this .cor path name a .cor of the running core? The base name has to be the descriptor's core id, its manifest name or its
// folder (the monolith's trx64u.cor is "TRX64U" in the descriptor; its folder is "c64", the roms' name, not the .cor's).
static bool cor_is_ours(const CoreDesc *d, const char *cor)
{
    char cfg[HANDOVER_PATH_MAX + 8];
    if (!d || !corefolder_cfg(cor, cfg, sizeof(cfg))) {
        return false;
    }
    const char *slash = strrchr(cfg, '/');
    const char *file = slash ? slash + 1 : cfg;
    char base[64];
    size_t bl = strlen(file) - 4;                                               // "<name>.cfg" -> "<name>"; the board's printf has no string precision
    if (bl >= sizeof(base)) {
        return false;
    }
    memcpy(base, file, bl);
    base[bl] = 0;
    const char *own[3] = { d->core_id, d->cor_name, d->folder };
    for (int i = 0; i < 3; i++) {
        if (own[i] && own[i][0] && !strcasecmp(base, own[i])) {
            return true;
        }
    }
    return false;
}

bool start_file_take(CoreCtx *c, const Handover *h, char *config, int cfglen, bool *safe)
{
    config[0] = 0;
    *safe = false;
    if (!c->fs) {
        return false;
    }
    const bool page = h && handover_valid(h) && h->path[0];
    // with a page the file lies on the medium of the .cor; without one (the monolith) the host wrote it on the medium of the .cor it started,
    // which is any of these (T230 P-2)
    static const char *const media[] = { "/SD/x", "/USB0/x", "/USB1/x" };
    const int nmedia = page ? 1 : 3;
    for (int m = 0; m < nmedia; m++) {
        char path[HANDOVER_PATH_MAX + 32];
        if (!startfile_path(page ? h->path : media[m], path, sizeof(path))) {
            continue;
        }
        uint64_t size;
        bool dir;
        if (!c->fs->stat(path, &size, &dir) || dir) {
            continue;
        }
        static char buf[STARTFILE_MAX + 8];
        int got = size < sizeof(buf) ? c->fs->load(path, buf, sizeof(buf)) : -1;
        // one-shot, deleted whoever it is for: the host writes it right before the jump (host_core.cc), so a file this running core finds
        // and does not take is a leftover of a start whose application never deleted it (Gideon's trx64g does not)
        if (!c->fs->remove(path)) {
            printf("[core] start file %s: could not be deleted\n", path);
        }
        char cor[HANDOVER_PATH_MAX], cfg[HANDOVER_CONFIG_MAX + 2];
        bool sf = false;
        if (got < 0 || !startfile_parse(buf, got, cor, sizeof(cor), cfg, sizeof(cfg), &sf)) {
            printf("[core] start file %s: not a start file, dropped\n", path);
            continue;
        }
        if (page) {
            if (strcasecmp(cor, h->path)) {
                printf("[core] start file %s: for %s, not for this start (%s), dropped\n", path, cor, h->path);
                return false;
            }
        } else {
            if (!cor_is_ours(c->desc, cor)) {
                printf("[core] start file %s: for %s, not for this core, dropped\n", path, cor);
                continue;
            }
            strncpy(c->cor_path, cor, sizeof(c->cor_path) - 1);
            c->cor_path[sizeof(c->cor_path) - 1] = 0;
            if (corefolder_cfg(c->cor_path, c->cfg_path, sizeof(c->cfg_path))) {
                printf("[core] start file: no hand-over path, the core folder of %s is used\n", cor);
            }
        }
        snprintf(config, cfglen, "%s", cfg);
        *safe = sf;
        printf("[core] start file %s read and deleted: config \"%s\", safe %d\n", path, cfg, sf ? 1 : 0);
        return true;
    }
    return false;
}

bool start_migrate_cfg(CoreCtx *c, const BoardProfile *prof)
{
    char old[HANDOVER_PATH_MAX];
    if (!c->fs || !c->cfg_path[0] || !prof || !prof->flash_cfg || !prof->cfg_dir || !c->desc || !c->desc->folder) {
        return false;
    }
    snprintf(old, sizeof(old), "%s/%s.cfg", prof->cfg_dir, c->desc->folder);
    if (!strcasecmp(old, c->cfg_path)) {
        return false;
    }
    uint64_t size;
    bool dir;
    if (c->fs->stat(c->cfg_path, &size, &dir) || !c->fs->stat(old, &size, &dir) || dir || size > 4096) {
        return false;                                   // the new file exists (or the old one does not, or is no settings file)
    }
    static uint8_t buf[4096];
    int got = c->fs->load(old, buf, sizeof(buf));
    if (got < 0 || !c->fs->save(c->cfg_path, buf, (uint32_t)got)) {
        printf("[core] settings: could not copy %s to %s\n", old, c->cfg_path);
        return false;
    }
    printf("[core] settings: copied %s to %s\n", old, c->cfg_path);
    return true;
}

bool held_offers_return(const BoardProfile *b)
{
    return b && b->can_return_host;
}

void held_text(const char *why, const BoardProfile *b, const char *err, char *out, int outlen)
{
    snprintf(out, outlen, "%s", why ? why : "");
    if (held_offers_return(b)) {
        size_t l = strlen(out);
        snprintf(out + l, outlen - l, "\n\nPress RETURN to return to the host menu.");
    }
    if (err && err[0]) {
        size_t l = strlen(out);
        snprintf(out + l, outlen - l, "\n\nThe way back did not work: %s", err);
    }
}

HeldKeyResult held_key(const BoardProfile *b, int key, HeldReturnFn ret, char *msg, int msglen)
{
    if (key != KEY_RETURN || !held_offers_return(b) || !ret) {
        return HELD_IGNORED;
    }
    if (ret(msg, msglen)) {
        return HELD_IGNORED;                 // does not happen on the board: the FPGA reconfigures
    }
    return HELD_RETURN_FAILED;
}
