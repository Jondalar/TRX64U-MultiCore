// The File Browser's file operations as the core runs them (host_core_flow_fileops.h). The text of every note is the old hostui.cc's, word for word.
#include "host_core_flow_fileops.h"
#include "keyboard.h"
#include "host_core_flow_util.h"
#include "mediaui.h"
#include "hostfs.h"
#include "bootcfg.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {

struct Scope {                                  // the flow the service tells, for the span of one operation
    const HcFoEnv &e;
    bool own;
    Scope(const HcFoEnv &env, const char *kind, const char *title) : e(env), own(false)
    {
        if (e.scope_begin) {
            own = e.scope_begin(e.ctx, kind, title, true);
        }
    }
    ~Scope()
    {
        if (e.scope_end) {
            e.scope_end(e.ctx, own);
        }
    }
};

struct Pick {
    bool move;                                  // the file is moved, not copied
    char src[200];                              // the file
    char back[160];                             // the folder the browser comes back to
    int bsel;                                   // ... and the row
    FoPlan rp;                                  // a copy or move that waits for the answer "replace the file that is there?"
    bool rp_pending;
};
Pick pk;

char fo_label[56];                              // the file the progress line speaks of
const HcFoEnv *fo_env;                          // the operation that runs (the progress callback has no other way to its environment)

bool is_boot_cfg(const char *path) { return !strcasecmp(path, BOOTCFG_PATH); }
void note(const HcFoEnv &e, const char *text, int tone) { e.base->note(e.base->ctx, text, tone); }
const char *base_of(const char *p) { return hcf::base_of(p); }
void join_path(char *out, int n, const char *dir, const char *name) { snprintf(out, n, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", name); }

int fo_progress(void *, int phase, uint64_t done, uint64_t total)
{
    static uint32_t last_draw, last_poll;
    if (phase == FO_PHASE_DELETE) {
        return 0;
    }
    const HcFoEnv &e = *fo_env;
    uint32_t t = e.base->now(e.base->ctx);
    char line[160];
    line[0] = 0;
    if (t - last_draw >= 150 || done == total) {
        last_draw = t;
        snprintf(line, sizeof(line), "%s %s: %d%%    ESC cancels", phase == FO_PHASE_COPY ? "Copying" : "Reading back", fo_label,
                 total ? (int)(done * 100 / total) : 100);
    }
    e.progress(e.ctx, total ? (int)(done * 100 / total) : 100, phase == FO_PHASE_COPY ? "copying" : "reading back", line[0] ? line : 0);
    if (t - last_poll >= 50) {
        last_poll = t;
        int k;
        while ((k = e.base->getch_nowait(e.base->ctx)) >= 0) {       // ESC and RUN/STOP only (the left arrow is not a cancel here)
            if (k == KEY_BREAK || k == KEY_ESCAPE) {
                return 1;
            }
        }
    }
    return 0;
}

// What a refusal says; the numbers of "no room" are the plan's.
void fo_say(const HcFoEnv &e, FoResult r, const FoPlan *p)
{
    char t[160];
    if (r == FO_ERR_NOROOM && p) {
        char fr[24], nd[24];
        ui_size_text(fr, sizeof(fr), p->free);
        ui_size_text(nd, sizeof(nd), p->size);
        snprintf(t, sizeof(t), "No room for %s: %s free, %s needed. Nothing was written.", base_of(p->src), fr, nd);
    } else if (r == FO_CANCELLED) {
        snprintf(t, sizeof(t), "Cancelled. The source is untouched; no half copy is left.");
    } else {
        snprintf(t, sizeof(t), "%s", fo_text(r));
    }
    note(e, t, r == FO_CANCELLED ? HCT_MUTED : (r == FO_ERR_NOROOM || r == FO_ERR_SAME || r == FO_ERR_NOTDIR || r == FO_ERR_DIR_NOTEMPTY ? HCT_WARN : HCT_BAD));
}

void fo_run(const HcFoEnv &e, const FoPlan &plan, bool move, bool replace)
{
    Scope flow_(e, move ? "move" : "copy", base_of(plan.src));
    snprintf(fo_label, sizeof(fo_label), "%s", base_of(plan.src));
    int idx[BOOT_MAX];
    int nref = move ? fo_boot_refs(hc_cfg(), plan.src, idx, BOOT_MAX) : 0;
    FoFs &fs = *hostfs_fo();
    fo_env = &e;
    FoResult r = move ? fo_move(fs, plan, replace, e.buf, e.buf_size, fo_progress, 0) : fo_copy(fs, plan, replace, e.buf, e.buf_size, fo_progress, 0);
    fo_env = 0;
    char dir[160];
    snprintf(dir, sizeof(dir), "%s", plan.dst);
    char *sl = strrchr(dir, '/');
    if (sl && sl != dir) {
        *sl = 0;
    }
    hcf::flog(*e.base, "[host] %s %s -> %s: %s", move ? "move" : "copy", plan.src, plan.dst, fo_text(r));
    e.pick_end(e.ctx);
    if (r != FO_OK) {
        fo_say(e, r, &plan);
        return;
    }
    char t[160];
    if (move && nref > 0) {
        for (int k = 0; k < nref && k < BOOT_MAX; k++) {
            e.mark(e.ctx, idx[k]);
        }
        snprintf(t, sizeof(t), "Moved %s to %s. %d menu entr%s now point%s at a file that is not there.", fo_label, dir, nref, nref == 1 ? "y" : "ies",
                 nref == 1 ? "s" : "");
        note(e, t, HCT_WARN);
    } else {
        snprintf(t, sizeof(t), move ? "Moved %s to %s" : "Copied %s to %s (read back, SHA-256 equal)", fo_label, dir);
        note(e, t, HCT_OK);
    }
}

// The entry goes into the menu (the part the bundle and the firmware share after their checks).
bool add_entry(const HcFoEnv &e, const char *name, const char *path, const char *hash, uint32_t size, uint32_t hdr, const char *what,
               const char *old_name)
{
    FileStat fst;
    if (!hostfs_stat(path, &fst.size, &fst.mtime)) {
        char t[96];
        snprintf(t, sizeof(t), "cannot read the directory entry of the %s", what);
        note(e, t, HCT_BAD);
        return false;
    }
    if (hc_cfg().n >= BOOT_MAX) {                              // T223 P4: the full menu says so, and how many entries it holds
        char t[120];
        snprintf(t, sizeof(t), "Menu full (%d/%d): remove one in F5 first.", hc_cfg().n, BOOT_MAX);
        note(e, t, HCT_BAD);
        return false;
    }
    int ar = hc_entry_add(name, path, hash, size, fst.mtime, hdr, old_name);     // T254 D7: an entry named old_name takes name
    if (ar == HCR_REFUSED) {
        note(e, "cannot add: the name has a '|' or a line break", HCT_BAD);
        return false;
    }
    e.after_add(e.ctx, ar, path);
    char b[120];
    int m = hc_in_menu(path);                                   // T254: a refresh keeps the entry's name (T198 D17): the note names what the menu shows
    snprintf(b, sizeof(b), "%s is in the boot menu", m >= 0 ? hc_cfg().e[m].name : name);
    note(e, b, HCT_OK);
    return true;
}

}  // namespace

// ---- the target pick ----------------------------------------------------------------------------------------------------------

bool hc_fo_pick_start(const HcFoEnv &e, bool move, bool at_media, const char *sel_path, const char *bpath, int bsel)
{
    if (at_media || !sel_path) {
        note(e, move ? "Select a file to move" : "Select a file to copy", HCT_WARN);
        return false;
    }
    if (is_boot_cfg(sel_path)) {
        note(e, "Flash:/boot.cfg is the boot menu itself and is not copied or moved here", HCT_WARN);
        return false;
    }
    snprintf(pk.src, sizeof(pk.src), "%s", sel_path);
    snprintf(pk.back, sizeof(pk.back), "%s", bpath);
    pk.bsel = bsel;
    pk.move = move;
    return true;
}

// RETURN in the target pick: the folder the browser shows is the target.
void hc_fo_pick_confirm(const HcFoEnv &e, bool at_media, const char *bpath)
{
    if (at_media) {
        note(e, "Open a medium with RIGHT first, then choose the folder", HCT_WARN);
        return;
    }
    FoPlan plan;
    FoResult r = fo_plan(*hostfs_fo(), pk.src, bpath, pk.move, &plan);
    if (r == FO_EXISTS) {
        pk.rp = plan;
        pk.rp_pending = true;
        char t[160];
        snprintf(t, sizeof(t), "%s is already in %s. RETURN replaces it, any other key cancels.", base_of(plan.src), bpath);
        note(e, t, HCT_WARN);
        if (e.ask) {
            e.ask(e.ctx, t);
        }
        return;
    }
    if (r != FO_OK) {
        fo_say(e, r, &plan);
        return;
    }
    fo_run(e, plan, pk.move, false);
}

bool hc_fo_pick_move(void) { return pk.move; }
const char *hc_fo_pick_back(void) { return pk.back; }
int hc_fo_pick_bsel(void) { return pk.bsel; }
bool hc_fo_replace_pending(void) { return pk.rp_pending; }
void hc_fo_replace_drop(void) { pk.rp_pending = false; }
void hc_fo_replace_run(const HcFoEnv &e) { fo_run(e, pk.rp, pk.move, true); }

// ---- delete -------------------------------------------------------------------------------------------------------------------

int hc_fo_delete_check(const HcFoRow &row, char *path, int cap, const char **warn)
{
    *warn = 0;
    if (!row.valid || !strcmp(row.name, "..")) {
        return 1;
    }
    join_path(path, cap, row.dir, row.name);
    if (is_boot_cfg(path)) {
        *warn = "Flash:/boot.cfg is the boot menu itself and is not deleted here";
        return 2;
    }
    if (row.is_dir && hostfs_fo()->dir_entries(path) > 0) {
        *warn = "The folder is not empty: only empty folders are deleted";
        return 2;
    }
    return 0;
}

void hc_fo_delete_arm_text(const HcFoRow &row, const char *path, char *t, int cap)
{
    int idx[BOOT_MAX];
    int nref = row.is_dir ? 0 : fo_boot_refs(hc_cfg(), path, idx, BOOT_MAX);
    if (nref == 1) {
        snprintf(t, cap, "DEL again deletes %s. Menu entry %s points at this file. Any other key cancels.", row.name, hc_cfg().e[idx[0]].name);
    } else if (nref > 1) {
        snprintf(t, cap, "DEL again deletes %s. %d menu entries point at this file (%s, ...). Any other key cancels.", row.name, nref,
                 hc_cfg().e[idx[0]].name);
    } else {
        snprintf(t, cap, "DEL again deletes %s. Any other key cancels.", row.name);
    }
}

void hc_fo_delete_run(const HcFoEnv &e, const HcFoRow &row, const char *path)
{
    int idx[BOOT_MAX];
    int nref = row.is_dir ? 0 : fo_boot_refs(hc_cfg(), path, idx, BOOT_MAX);
    char t[160];
    FoResult r = fo_delete(*hostfs_fo(), path);
    hcf::flog(*e.base, "[host] delete %s: %s", path, fo_text(r));
    if (r != FO_OK) {
        fo_say(e, r, 0);
        return;
    }
    for (int k = 0; k < nref && k < BOOT_MAX; k++) {
        e.mark(e.ctx, idx[k]);                           // the menu shows them as missing, as for any file that is gone
    }
    if (nref) {
        snprintf(t, sizeof(t), "Deleted %s. %d menu entr%s now show%s as missing.", row.name, nref, nref == 1 ? "y" : "ies", nref == 1 ? "s" : "");
    } else {
        snprintf(t, sizeof(t), "Deleted %s", row.name);
    }
    e.reload(e.ctx);                                     // the selection stays on the row, which now holds the neighbour
    note(e, t, nref ? HCT_WARN : HCT_OK);
}

// ---- add and recheck ----------------------------------------------------------------------------------------------------------

void hc_fo_refuse(cor_result_t r, const CorInfo &ci, char *out, int cap, int *tone)
{
    if (r == COR_ERR_CONTRACT) {
        snprintf(out, cap, "refused: built for another host contract");
        *tone = HCT_WARN;
    } else if (r == COR_ERR_HOST) {
        snprintf(out, cap, "refused: routed against host %08x, this host is %08x", (unsigned)ci.host_key, (unsigned)cor_host_key);
        *tone = HCT_WARN;
    } else {
        snprintf(out, cap, "refused: not a usable bundle");
        *tone = HCT_BAD;
    }
}

void hc_fo_refuse_full(cfw_result_t r, char *out, int cap, int *tone)
{
    snprintf(out, cap, "refused: %s", cfw_result_str(r));
    *tone = HCT_WARN;
}

// The full SHA-256 check of every part happens here once, then the bundle goes into the menu with its size, mtime and header digest;
// a later start trusts the stored hash while those still match (T84 decision, T101 S10). The caller has refused contract / host mismatches already.
bool hc_fo_add_bundle(const HcFoEnv &e, const char *path, const CorInfo &ci)
{
    Scope flow_(e, "check", ci.name);
    e.busy(e.ctx, "checking the bundle (SHA-256)...");
    FileSource fs(path);
    // cor_abort is set by a core start the person cancelled and cleared only by the next core start; cor_full_check clears it: this read is a run of its own.
    cor_result_t r = fs.ok() ? cor_full_check(fs, ci) : COR_ERR_IO;
    if (r != COR_OK) {
        note(e, cor_result_str(r), HCT_BAD);
        return false;
    }
    return add_entry(e, ci.name, path, ci.sha[COR_PART_PARTIAL], ci.file_size, ci.hdr_digest, "bundle", nullptr);
}

// A firmware image goes into the menu after the same read that starts it; it has no part hashes to check (D2: the
// bitstream is compared with the flash file at every start).
bool hc_fo_add_full(const HcFoEnv &e, const char *path, const char *name, uint32_t size, const char *old_name)
{
    if (!cfw_is_name(path)) {
        // a large .cor: the full SHA-256 check of its parts runs here, once (T115 section 7); a start trusts the entry
        static CorInfo lci;
        FileSource ls(path);
        const char *why = "cannot read the package";
        e.busy(e.ctx, "checking the package (SHA-256)...");
        if (!ls.ok() || cor_probe(ls, lci) != COR_ERR_LARGE || !cor_large_verify(ls, lci, &why)) {
            char t[120];
            snprintf(t, sizeof(t), "refused: %s", why);
            note(e, t, HCT_BAD);
            return false;
        }
    }
    return add_entry(e, name, path, "", size, 0, "firmware", old_name);
}

void hc_fo_browse_add(const HcFoEnv &e, const char *path, bool full, cfw_result_t bcw_res, const CfwInfo &bcw, cor_result_t bci_res, const CorInfo &bci)
{
    char t[160];
    int tone;
    if (full) {
        if (bcw_res != CFW_OK) {
            hc_fo_refuse_full(bcw_res, t, sizeof(t), &tone);
            note(e, t, tone);
        } else {
            // T254: the name the firmware gives itself, else the .bit header's. D7: an entry of this file that still has the .bit header's
            // name (the one A gave before T254) takes the new name; a name the person gave stays (D6)
            hc_fo_add_full(e, path, cfw_title(bcw), bcw.file_size, bcw.name);
        }
        return;
    }
    if (bci_res != COR_OK) {
        hc_fo_refuse(bci_res, bci, t, sizeof(t), &tone);
        note(e, t, tone);
        return;
    }
    hc_fo_add_bundle(e, path, bci);
}

// R in the menu: the full check of the selected entry again (what F3 does for a file), storing size, mtime and header digest anew.
// This is how an entry marked "changed" is accepted again; the file is not searched for, it is the one in the entry.
void hc_fo_recheck(const HcFoEnv &e, int sel)
{
    const BootCfg &cfg = hc_cfg();
    if (sel < 0 || sel >= cfg.n) {
        return;
    }
    char path[sizeof(cfg.e[0].path)], name[sizeof(cfg.e[0].name)], t[160];
    int tone;
    strcpy(path, cfg.e[sel].path);
    strcpy(name, cfg.e[sel].name);
    if (hc_is_full(path)) {                              // a .cfw or a large .cor (cfw_probe reads both)
        static CfwInfo cw;
        FileSource s(path);
        cfw_result_t r = s.ok() ? cfw_probe(s, CFW_IDCODE_XC7A50T, cw) : CFW_ERR_IO;
        if (r != CFW_OK) {
            hc_fo_refuse_full(r, t, sizeof(t), &tone);
            note(e, t, tone);
        } else {
            hc_fo_add_full(e, path, name, cw.file_size);
        }
        return;
    }
    FileSource s(path);
    CorInfo ci;
    cor_result_t r = s.ok() ? cor_probe(s, ci) : COR_ERR_IO;
    if (r != COR_OK) {
        hc_fo_refuse(r, ci, t, sizeof(t), &tone);
        note(e, t, tone);
        return;
    }
    hc_fo_add_bundle(e, path, ci);
}

// ---- Cores and ROMs -----------------------------------------------------------------------------------------------------------

void hc_fo_cores_delete(const HcFlowEnv &e, const CmReport &rep, CmDelete *out, char *msg, int cap, int *tone)
{
    cm_delete(*hostfs_fo(), rep, out);
    char sz[24];
    ui_size_text(sz, sizeof(sz), out->bytes);
    hcf::flog(e, "[host] cores and ROMs: deleted %d, failed %d, refused %d", out->deleted, out->failed, out->refused);
    *tone = out->failed || out->refused ? HCT_WARN : HCT_OK;
    snprintf(msg, cap, "Deleted %d entries (%s)%s.", out->deleted, sz, out->failed || out->refused ? ", some could not be deleted" : "");
}
