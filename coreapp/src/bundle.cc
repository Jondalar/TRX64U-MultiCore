#include "bundle.h"
#include "ops_glue.h"
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

static void read_cor(CoreCtx *c, BundleExtras *out)
{
    memset(out, 0, sizeof(*out));
    if (!is_cor(c->cor_path)) {
        return;                                                  // started without a bundle (JTAG, the monolith): nothing to read
    }
    CoreFile *f = c->fs->open(c->cor_path, false);
    if (!f) {
        snprintf(out->why, sizeof(out->why), "%s cannot be opened: the bundle's description and libraries are not read.", c->cor_path);
        return;
    }
    FileSrc src(f);
    cor_host_key = c->host_key;
    cor_result_t r = cor_probe(src, out->ci);
    if ((r != COR_OK && r != COR_ERR_LARGE) || strcasecmp(out->ci.name, c->desc->cor_name) != 0) {
        const char *reason = (r != COR_OK && r != COR_ERR_LARGE) ? cor_result_str(r) : "this is another core's bundle";
        snprintf(out->why, sizeof(out->why), "%s: %s; its description and libraries are not read.", c->cor_path, reason);
        memset(&out->ci, 0, sizeof(out->ci));
        c->fs->close(f);
        return;
    }
    out->probed = true;
    uint32_t n = out->ci.size[COR_PART_DESC];
    if (n) {
        if (n > DESCREC_MAX_SIZE) {
            snprintf(out->why, sizeof(out->why), "The description record of %s is %u bytes; at most %u are read.", c->cor_path, (unsigned)n, DESCREC_MAX_SIZE);
        } else {
            uint8_t *buf = new uint8_t[n];
            r = cor_read_part(src, out->ci, COR_PART_DESC, buf, 0, 0, true);
            DescRecord *d = r == COR_OK ? new DescRecord : 0;
            char why[160];
            if (r != COR_OK) {
                snprintf(out->why, sizeof(out->why), "The description record of %s: %s.", c->cor_path, cor_result_str(r));
            } else if (descrec_parse(buf, n, d, why, sizeof(why)) != 0) {
                snprintf(out->why, sizeof(out->why), "%s: %s.", c->cor_path, why);
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
    c->fs->close(f);
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

int bundle_merge(CoreCtx *c, const CoreDesc *base, const BundleExtras *x, LibLoader *loader, LibLog *log, CoreDesc *out, MenuMerge *mm, const AppIndex *apps)
{
    Contributor cs[LIB_MAX * 2 + 1 + APPS_MAX];
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
        n += apps->contributors(cs + n, APPS_MAX);
    }
    CoreDesc own;
    if (x && x->desc && x->desc->has_own) {
        descrec_apply(*x->desc, base, &own);                      // the core's own tasks, media, file types and settings are the record's (T104.2 D10)
        base = &own;
    }
    int refused = contrib_merge(base, skel, ov, cs, n, ops_of(c), out, mm);
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
