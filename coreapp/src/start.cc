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

StartResult start_identify(const uint8_t id[4], const Handover *h, const CoreDesc **desc, char *msg, int msglen, CoreIo *io)
{
    *desc = 0;
    const CoreDesc *d = core_find((const char *)id);
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
        uint32_t key = 0;
        for (int i = 0; i < 8 && h->host_build[i]; i++) {
            // case-insensitive: the firmware's small_printf writes %x in capitals (board 2026-11-06); any other character voids the key
            char ch = h->host_build[i];
            uint32_t v;
            if (ch >= '0' && ch <= '9') {
                v = (uint32_t)(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                v = (uint32_t)(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                v = (uint32_t)(ch - 'A' + 10);
            } else {
                key = 0;
                break;
            }
            key = (key << 4) | v;
        }
        c->host_key = key;
        strncpy(c->cor_path, h->path, sizeof(c->cor_path) - 1);
        c->cor_path[sizeof(c->cor_path) - 1] = 0;
        snprintf(c->host_version, sizeof(c->host_version), "%s", handover_host_version(h));
        strncpy(c->core_version, h->core_version, sizeof(c->core_version) - 1);
        c->core_version[sizeof(c->core_version) - 1] = 0;
        if (!corefolder_cfg(c->cor_path, c->cfg_path, sizeof(c->cfg_path))) {          // T148 D1: in the core folder
            c->cfg_path[0] = 0;
        }
    }
    if (!c->cfg_path[0] && prof && prof->cfg_dir && c->desc && c->desc->folder) {
        snprintf(c->cfg_path, sizeof(c->cfg_path), "%s/%s.cfg", prof->cfg_dir, c->desc->folder);
    }
}

bool start_migrate_cfg(CoreCtx *c, const BoardProfile *prof)
{
    char old[HANDOVER_PATH_MAX];
    if (!c->fs || !c->cfg_path[0] || !prof || !prof->cfg_dir || !c->desc || !c->desc->folder) {
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
