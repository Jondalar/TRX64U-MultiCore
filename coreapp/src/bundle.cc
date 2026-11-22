#include "bundle.h"
#include "ops_glue.h"
#include "hotkeys.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {

class FileSrc : public CorSource {
public:
    explicit FileSrc(CoreFile *f) : f_(f) { }
    uint32_t size() { return fs_size32(f_->size()); }
    bool read(uint32_t off, void *buf, uint32_t len) { return f_->read(off, buf, len); }
private:
    CoreFile *f_;
};

bool is_cor(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && !strcasecmp(path + n - 4, ".cor");
}

}

// `name`: the core the bundle has to be for (its manifest's name); null: any (the descriptor is still to be made of the bundle, T239.3 D17)
static void read_cor_at(CoreFs *fs, const char *cor_path, uint32_t host_key, const char *name, BundleExtras *out)
{
    memset(out, 0, sizeof(*out));
    if (!is_cor(cor_path)) {
        return;                                                  // started without a bundle (JTAG, the monolith): nothing to read
    }
    CoreFile *f = fs->open(cor_path, false);
    if (!f) {
        snprintf(out->why, sizeof(out->why), "%s cannot be opened: the bundle's description and libraries are not read.", cor_path);
        return;
    }
    FileSrc src(f);
    cor_host_key = host_key;
    cor_result_t r = cor_probe(src, out->ci);
    if ((r != COR_OK && r != COR_ERR_LARGE) || (name && strcasecmp(out->ci.name, name) != 0)) {
        const char *reason = (r != COR_OK && r != COR_ERR_LARGE) ? cor_result_str(r) : "this is another core's bundle";
        snprintf(out->why, sizeof(out->why), "%s: %s; its description and libraries are not read.", cor_path, reason);
        memset(&out->ci, 0, sizeof(out->ci));
        fs->close(f);
        return;
    }
    out->probed = true;
    uint32_t n = out->ci.size[COR_PART_DESC];
    if (n) {
        if (n > DESCREC_MAX_SIZE) {
            snprintf(out->why, sizeof(out->why), "The description record of %s is %u bytes; at most %u are read.", cor_path, (unsigned)n, DESCREC_MAX_SIZE);
        } else {
            uint8_t *buf = new uint8_t[n];
            r = cor_read_part(src, out->ci, COR_PART_DESC, buf, 0, 0, true);
            DescRecord *d = r == COR_OK ? new DescRecord : 0;
            char why[160];
            if (r != COR_OK) {
                snprintf(out->why, sizeof(out->why), "The description record of %s: %s.", cor_path, cor_result_str(r));
            } else if (descrec_parse(buf, n, d, why, sizeof(why)) != 0) {
                snprintf(out->why, sizeof(out->why), "%s: %s.", cor_path, why);
                delete d;
                d = 0;
            }
            if (d) {
                out->desc = d;
                out->desc_bytes = buf;                           // the record's strings live in it for as long as the application runs
            } else {
                delete[] buf;
            }
        }
    }
    fs->close(f);
}

static void read_cor(CoreCtx *c, BundleExtras *out)
{
    read_cor_at(c->fs, c->cor_path, c->host_key, c->desc->cor_name, out);
}

// T104.2 D10: a start that has no .cor, or whose .cor carries no (readable) menu record, uses the record the application carries for this core, if it does:
// the same bytes tools/descrec.py makes from the core's menu.toml and the packer puts into the .cor.
void bundle_read(CoreCtx *c, BundleExtras *out)
{
    read_cor(c, out);
    if (!out->desc && c->desc->menu_rec) {
        DescRecord *d = new DescRecord;
        char why[160];
        if (descrec_parse(c->desc->menu_rec, c->desc->menu_rec_n, d, why, sizeof(why)) == 0) {
            out->desc = d;
        } else {
            snprintf(out->why + strlen(out->why), sizeof(out->why) - strlen(out->why), " The record built into the application: %s.", why);
            delete d;
        }
    }
}

static char printable(uint8_t c)
{
    return (c >= 32 && c < 127) ? (char)c : '?';
}

const CoreDesc *BundleDescSource::describe(const uint8_t id[4], const Handover *h, char *msg, int msglen)
{
    char who[160];
    snprintf(who, sizeof(who), "This application has no description for the core with the ID %c%c%c%c", printable(id[0]), printable(id[1]), printable(id[2]), printable(id[3]));
    if (!h || !handover_valid(h) || !h->path[0] || !is_cor(h->path)) {
        snprintf(msg, msglen, "%s, and there is no bundle to read one from. The core stays held.", who);
        return 0;
    }
    BundleExtras x;
    read_cor_at(fs_, h->path, start_host_key(h), 0, &x);
    // the record and its bytes are freed with every refusal; only the descriptor that is returned keeps them (its strings live in them)
    struct Keep {
        BundleExtras &x;
        bool kept;
        explicit Keep(BundleExtras &e) : x(e), kept(false) { }
        ~Keep() { if (!kept) { delete x.desc; delete[] x.desc_bytes; } }
    } keep(x);
    if (!x.desc) {
        snprintf(msg, msglen, "%s, and the bundle %s has no description record to make one of%s%s. The core stays held.", who, h->path, x.why[0] ? ": " : "", x.why);
        return 0;
    }
    if (!x.desc->has_core) {
        snprintf(msg, msglen, "%s, and the description record of %s has no data sections (no core section). The core stays held.", who, h->path);
        return 0;
    }
    CoreDesc *d = new CoreDesc;
    char why[200];
    if (descrec_core(x.desc, hooks_, d, why, sizeof(why)) != 0) {
        snprintf(msg, msglen, "%s: %s. The core stays held.", h->path, why);
        delete d;
        return 0;
    }
    if (memcmp(d->id, id, 4) != 0) {
        snprintf(msg, msglen, "%s, and the description record of %s is for the core with the ID %c%c%c%c. The core stays held.", who, h->path, printable((uint8_t)d->id[0]),
                 printable((uint8_t)d->id[1]), printable((uint8_t)d->id[2]), printable((uint8_t)d->id[3]));
        delete d;
        return 0;
    }
    if (strcasecmp(x.ci.name, d->cor_name) != 0) {
        snprintf(msg, msglen, "%s: the bundle is \"%s\", its description record's core section says \"%s\". The core stays held.", h->path, x.ci.name, d->cor_name);
        delete d;
        return 0;
    }
    if (!d->init || !d->release) {
        snprintf(msg, msglen, "%s: the description record is complete, but the code of the core (the control hooks init and release) is not in this application. The core stays held.", h->path);
        delete d;
        return 0;
    }
    keep.kept = true;
    return d;                                                   // the record and its bytes stay: the descriptor's strings live in them
}

int bundle_merge(CoreCtx *c, const CoreDesc *base, const BundleExtras *x, LibLoader *loader, LibLog *log, CoreDesc *out, MenuMerge *mm, const AppIndex *apps)
{
    Contributor cs[LIB_MAX * 2 + 1 + APPS_MAX + 1];
    int n = 0;
    const Skeleton *skel = 0;
    const Overrides *ov = 0;
    if (x && x->desc) {
        cs[n].name = "description";
        cs[n].rank = CR_DESC;
        cs[n].rec = &x->desc->rec;
        n++;
        if (x->desc->has_route) {
            skel = &x->desc->skel;
        }
        ov = &x->desc->ov;
    }
    for (int h = 0; loader && h < LIB_MAX && n < LIB_MAX * 2 + 1; h++) {
        const LibInfo *li = loader->info(h);
        if (!li) {
            continue;
        }
        cs[n].name = li->name;
        cs[n].rank = li->kind == U2A3_KIND_APP ? CR_APP : CR_LIB;
        cs[n].rec = &li->record;
        n++;
        if (li->menu && n < LIB_MAX * 2 + 1 && !(x && x->desc && descrec_merged(*x->desc, li->name))) {
            // the file's own TOML record (libmenu.h): a second contributor of the same name and rank - unless the packer merged it into the description (T104.2 D10)
            cs[n].name = li->name;
            cs[n].rank = cs[n - 1].rank;
            cs[n].rec = &li->menu->rec;
            n++;
        }
    }
    if (apps) {
        n += apps->contributors(cs + n, APPS_MAX + 1);
    }
    CoreDesc own;
    if (x && x->desc && descrec_applies(*x->desc, base) && (x->desc->has_own || x->desc->has_pad || x->desc->restart_clear)) {
        descrec_apply(*x->desc, base, &own);                      // the core's own tasks, media, file types and settings are the record's (T104.2 D10), its pad
                                                                  // (T228 R10) and what its restart clears (R17) too
        // T228 R1: a task of the record that declares a hotkey gets its item on the Hotkeys page, behind the application's own (the record's table holds them)
        if (const char *why = hk_extend_tasks(&own, const_cast<DescRecord *>(x->desc)->csettings, DR_CSETS)) {
            printf("[menu] description: %s\n", why);
        }
        base = &own;
    }
    int refused = contrib_merge(base, skel, ov, cs, n, ops_of(c), out, mm);
    if (x && x->desc && x->desc->nkeys) {                         // base 3.3: the description's key table is the menu's (MenuCtl stage KS_CORE); the rows live in the record
        mm->keys = x->desc->keys;
        mm->nkeys = x->desc->nkeys;
    }
    for (int i = 0; log && i < mm->nerr; i++) {
        char line[CONTRIB_ERR_LEN + 24];
        snprintf(line, sizeof(line), "[menu] refused: %s", mm->err[i]);
        log->line(line);
    }
    if (log && refused > mm->nerr) {
        char line[64];
        snprintf(line, sizeof(line), "[menu] %d more contributions refused", refused - mm->nerr);
        log->line(line);
    }
    return refused;
}
