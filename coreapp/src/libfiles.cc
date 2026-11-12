// The Library's file management (T104.2 UI): copy, move and delete of the marked files or the one under the cursor, the clipboard's paste, rename, new folder, the
// home folder. Host-owned code over CoreFs (fsif.h); every write is refused for /flash by the interface, so "Delete (only writable paths)" is what CoreFs says.
//
// copy, move and delete are jobs in steps (the menu's progress display, T122 5): the operation is called again for as long as it answers `more`, one file chunk or
// one directory entry per call. The first call of a job (CoreCtx::job_pct 0 and no job_path) makes the work list from the selection.
#include "libfiles.h"
#include "home.h"
#include "ops_glue.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEPTH 6
#define ENTS 192
#define CHUNK 0x10000

enum { FJ_COPY = 0, FJ_MOVE = 1, FJ_DELETE = 2 };

struct Frame {
    char src[FS_PATH_MAX];
    char dst[FS_PATH_MAX];
    FsEntry *ents;
    int n, i;
    bool listed;
    bool remove_after;                    // delete / a move done as copy: the directory goes when its content is done
};

struct Job {
    bool active;
    int kind;
    char srcs[LIBSEL_MAX][FS_PATH_MAX];
    int nsrc, si;
    char dest[200];
    Frame fr[DEPTH];
    int depth;
    // the file being copied
    CoreFile *in, *out;
    uint64_t pos, size;
    char in_path[FS_PATH_MAX], out_path[FS_PATH_MAX];
    bool delete_in_after;
    int done_files, skipped, failed;
    char first_err[120];
};
static Job *g_job;

static const char *base_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void join(char *out, int n, const char *dir, const char *name)
{
    int l = (int)strlen(dir);
    snprintf(out, n, "%s%s%s", dir, l && dir[l - 1] == '/' ? "" : "/", name);
}

// The sources of an operation: the marked names, else the row under the cursor.
static int selection(CoreCtx *c, char out[][FS_PATH_MAX], int max)
{
    int n = 0;
    LibSel &l = c->lib;
    for (int i = 0; i < l.nmarked && n < max; i++) {
        join(out[n++], FS_PATH_MAX, l.dir, l.marked[i]);
    }
    if (!n && l.cur[0] && n < max) {
        snprintf(out[n++], FS_PATH_MAX, "%s", l.cur);
    }
    return n;
}

static bool is_dir(CoreCtx *c, const char *p)
{
    uint64_t sz;
    bool d = false;
    return c->fs->stat(p, &sz, &d) && d;
}

static void note_error(Job *j, const char *what, const char *path)
{
    j->failed++;
    if (!j->first_err[0]) {
        snprintf(j->first_err, sizeof(j->first_err), "%s: %s", what, base_of(path));
    }
}

static void job_free(void)
{
    if (!g_job) {
        return;
    }
    for (int i = 0; i < DEPTH; i++) {
        delete[] g_job->fr[i].ents;
    }
    delete g_job;
    g_job = 0;
}

static bool inside(const char *dir, const char *path)               // `path` is `dir` or below it
{
    size_t l = strlen(dir);
    return !strncasecmp(dir, path, l) && (path[l] == 0 || path[l] == '/');
}

static int job_begin(CoreCtx *c, int kind, bool from_clip, char *msg, int msglen)
{
    job_free();
    g_job = new Job();
    Job *j = g_job;
    j->kind = kind;
    if (from_clip) {
        for (int i = 0; i < c->lib.nclip && j->nsrc < LIBSEL_MAX; i++) {
            snprintf(j->srcs[j->nsrc++], FS_PATH_MAX, "%s", c->lib.clip[i]);
        }
        j->kind = c->lib.clip_move ? FJ_MOVE : FJ_COPY;
        snprintf(j->dest, sizeof(j->dest), "%s", c->lib.dir);
    } else {
        j->nsrc = selection(c, j->srcs, LIBSEL_MAX);
        snprintf(j->dest, sizeof(j->dest), "%s", c->media_dir);
    }
    if (!j->nsrc) {
        snprintf(msg, msglen, "Nothing is selected");
        job_free();
        return 1;
    }
    if (kind != FJ_DELETE || from_clip) {
        if (!j->dest[0] || !strcmp(j->dest, "/")) {
            snprintf(msg, msglen, "Pick a folder on a medium first");
            job_free();
            return 1;
        }
        for (int i = 0; i < j->nsrc; i++) {
            if (is_dir(c, j->srcs[i]) && inside(j->srcs[i], j->dest)) {
                snprintf(msg, msglen, "%s cannot go into itself", base_of(j->srcs[i]));
                job_free();
                return 1;
            }
        }
    }
    return 0;
}

// A copy never overwrites and never fails on a name that is taken (GitHub #42): "name.ext" becomes "name 1.ext", then "name 2.ext" ... A folder or a name
// without an extension gets the number at the end. `dst` is rewritten; false when nothing up to 999 is free.
static bool free_name(CoreCtx *c, char *dst, int n)
{
    uint64_t sz;
    bool d;
    if (!c->fs->stat(dst, &sz, &d)) {
        return true;
    }
    char stem[FS_PATH_MAX];
    snprintf(stem, sizeof(stem), "%s", dst);
    const char *b = base_of(stem);
    const char *dot = d ? 0 : strrchr(b, '.');
    char ext[48] = "";
    if (dot && dot != b) {
        snprintf(ext, sizeof(ext), "%s", dot);
        stem[dot - stem] = 0;
    }
    for (int k = 1; k < 1000; k++) {
        snprintf(dst, n, "%s %d%s", stem, k, ext);
        if (!c->fs->stat(dst, &sz, &d)) {
            return true;
        }
    }
    return false;
}

// Opens the next file to copy: false when it cannot be (counted as failed), true when a copy is running
static bool start_copy(CoreCtx *c, Job *j, const char *src, const char *dst, bool delete_after)
{
    uint64_t sz = 0, dsz;
    bool d = false;
    if (c->fs->stat(dst, &dsz, &d)) {
        j->skipped++;
        return false;
    }
    c->fs->stat(src, &sz, &d);
    j->in = c->fs->open(src, false);
    if (!j->in) {
        note_error(j, c->fs->stat(src, &sz, &d) ? "cannot read" : "gone (renamed, moved or deleted since)", src);
        return false;
    }
    if (!c->fs->save(dst, "", 0)) {
        c->fs->close(j->in);
        j->in = 0;
        note_error(j, "cannot create (read only?)", dst);
        return false;
    }
    j->out = c->fs->open(dst, true);
    if (!j->out || !j->out->writable()) {
        if (j->out) {
            c->fs->close(j->out);
        }
        j->out = 0;
        c->fs->close(j->in);
        j->in = 0;
        c->fs->remove(dst);
        note_error(j, "cannot write", dst);
        return false;
    }
    j->pos = 0;
    j->size = j->in->size();
    snprintf(j->in_path, sizeof(j->in_path), "%s", src);
    snprintf(j->out_path, sizeof(j->out_path), "%s", dst);
    j->delete_in_after = delete_after;
    snprintf(c->job_path, sizeof(c->job_path), "%s", base_of(src));
    return true;
}

static void push_frame(CoreCtx *c, Job *j, const char *src, const char *dst, bool remove_after)
{
    if (j->depth >= DEPTH) {
        note_error(j, "folders nested too deep", src);
        return;
    }
    Frame &f = j->fr[j->depth++];
    snprintf(f.src, sizeof(f.src), "%s", src);
    snprintf(f.dst, sizeof(f.dst), "%s", dst);
    if (!f.ents) {
        f.ents = new FsEntry[ENTS];
    }
    f.n = f.i = 0;
    f.listed = false;
    f.remove_after = remove_after;
}

// one item (a file or a folder, from the top list or from a folder): what its kind does with it
static void process_item(CoreCtx *c, Job *j, const char *src, const char *dst, bool dir)
{
    snprintf(c->job_path, sizeof(c->job_path), "%s", base_of(src));
    switch (j->kind) {
    case FJ_DELETE:
        if (dir) {
            push_frame(c, j, src, "", true);
        } else if (c->fs->remove(src)) {
            j->done_files++;
        } else {
            note_error(j, "cannot delete (read only?)", src);
        }
        break;
    case FJ_MOVE:
        if (c->fs->rename(src, dst)) {                      // the same medium: one rename, folder and all
            j->done_files++;
            break;
        }
        // another medium: copy, then delete
        if (dir) {
            if (!c->fs->mkdir(dst)) {
                note_error(j, "cannot create folder", dst);
                break;
            }
            push_frame(c, j, src, dst, true);
        } else if (start_copy(c, j, src, dst, true)) {
            break;
        }
        break;
    default:
        if (dir) {
            if (!c->fs->mkdir(dst)) {
                note_error(j, "cannot create folder", dst);
                break;
            }
            push_frame(c, j, src, dst, false);
        } else {
            start_copy(c, j, src, dst, false);
        }
        break;
    }
}

// Does one step. 0 more to do, 1 finished.
static int job_step(CoreCtx *c, Job *j, char *msg, int msglen)
{
    if (c->job_cancel) {
        if (j->in) {
            c->fs->close(j->in);
            c->fs->close(j->out);
            c->fs->remove(j->out_path);
            j->in = j->out = 0;
        }
        snprintf(msg, msglen, "Cancelled after %d file(s)", j->done_files);
        return 1;
    }
    if (j->in) {                                            // a chunk of the file being copied
        static uint8_t buf[CHUNK];
        uint32_t n = j->size - j->pos < CHUNK ? (uint32_t)(j->size - j->pos) : CHUNK;
        bool ok = n == 0 || (j->in->read(j->pos, buf, n) && j->out->write(j->pos, buf, n));
        j->pos += n;
        if (!ok) {
            note_error(j, "copy failed (full or gone)", j->in_path);
            c->fs->close(j->in);
            c->fs->close(j->out);
            c->fs->remove(j->out_path);
            j->in = j->out = 0;
        } else if (j->pos >= j->size) {
            j->out->sync();
            c->fs->close(j->in);
            c->fs->close(j->out);
            j->in = j->out = 0;
            j->done_files++;
            if (j->delete_in_after && !c->fs->remove(j->in_path)) {
                note_error(j, "copied but not deleted", j->in_path);
            }
        }
        int sub = j->size ? (int)(j->pos * 100 / j->size) : 100;
        c->job_pct = (j->si * 100 + (j->in ? sub : 100)) / (j->nsrc ? j->nsrc : 1);
        return 0;
    }
    if (j->depth) {                                         // an entry of the folder on top of the stack
        Frame &f = j->fr[j->depth - 1];
        if (!f.listed) {
            int k = c->fs->list(f.src, f.ents, ENTS);
            f.n = k < 0 ? 0 : k;
            f.i = 0;
            f.listed = true;
            if (k < 0) {
                note_error(j, "cannot read folder", f.src);
            }
            return 0;
        }
        if (f.i < f.n) {
            const FsEntry &e = f.ents[f.i++];
            char s[FS_PATH_MAX], d[FS_PATH_MAX];
            join(s, sizeof(s), f.src, e.name);
            join(d, sizeof(d), f.dst, e.name);
            process_item(c, j, s, d, e.dir);
            return 0;
        }
        if (f.remove_after && !c->fs->remove(f.src)) {
            note_error(j, "cannot remove folder", f.src);
        } else if (f.remove_after) {
            j->done_files++;
        }
        j->depth--;
        return 0;
    }
    if (j->si < j->nsrc) {                                  // the next item of the selection
        const char *s = j->srcs[j->si++];
        char d[FS_PATH_MAX];
        join(d, sizeof(d), j->dest, base_of(s));
        if (j->kind == FJ_COPY && !free_name(c, d, sizeof(d))) {
            note_error(j, "no free name for", d);
            c->job_pct = j->si * 100 / j->nsrc;
            return 0;
        }
        process_item(c, j, s, d, is_dir(c, s));
        c->job_pct = j->si * 100 / j->nsrc;
        return 0;
    }
    static const char *const verb[] = { "Copied", "Moved", "Deleted" };
    int w = snprintf(msg, msglen, "%s %d item(s)", verb[j->kind], j->done_files);
    if (j->skipped && w < msglen) {
        w += snprintf(msg + w, msglen - w, ", %d skipped (the name exists there)", j->skipped);
    }
    if (j->failed && w < msglen) {
        snprintf(msg + w, msglen - w, ", %d failed (%s)", j->failed, j->first_err);
    }
    return 1;
}

static int run_job(CoreCtx *c, int kind, bool from_clip, OpOut &o)
{
    if (!c->job_step) {                                      // a caller that does not step: run it all
        if (job_begin(c, kind, from_clip, o.msg, o.msglen)) {
            return OPE_FAILED;
        }
        while (!job_step(c, g_job, o.msg, o.msglen)) {
        }
        job_free();
        return OPE_OK;
    }
    if (!g_job || (c->job_pct == 0 && !c->job_path[0] && !g_job->active)) {
        if (job_begin(c, kind, from_clip, o.msg, o.msglen)) {
            return OPE_FAILED;
        }
        g_job->active = true;
        o.more = true;
        return OPE_OK;
    }
    if (job_step(c, g_job, o.msg, o.msglen)) {
        job_free();
        o.more = false;
        return OPE_OK;
    }
    o.more = true;
    return OPE_OK;
}

static int op_copy_job(CoreCtx *c, const OpArgs &, OpOut &o) { return run_job(c, FJ_COPY, false, o); }
static int op_move_job(CoreCtx *c, const OpArgs &, OpOut &o) { return run_job(c, FJ_MOVE, false, o); }
static int op_delete_job(CoreCtx *c, const OpArgs &, OpOut &o) { return run_job(c, FJ_DELETE, false, o); }
static int op_paste_job(CoreCtx *c, const OpArgs &, OpOut &o) { return run_job(c, FJ_COPY, true, o); }

// ---- the clipboard, marks, rename, new folder, home ------------------------------------------------------------------------------------------------
static int op_clip(CoreCtx *c, const OpArgs &a, OpOut &o)           // value 0 copy, 1 cut
{
    char (*sel)[FS_PATH_MAX] = (char (*)[FS_PATH_MAX])malloc((size_t)LIBSEL_MAX * FS_PATH_MAX);      // 33 KB for this call, not for good
    if (!sel) {
        snprintf(o.msg, o.msglen, "No memory for the selection");
        return OPE_FAILED;
    }
    int n = selection(c, sel, LIBSEL_MAX);
    if (!n) {
        free(sel);
        snprintf(o.msg, o.msglen, "Nothing is selected");
        return OPE_FAILED;
    }
    for (int i = 0; i < n; i++) {
        snprintf(c->lib.clip[i], sizeof(c->lib.clip[i]), "%s", sel[i]);
    }
    free(sel);
    c->lib.nclip = n;
    c->lib.clip_move = a.i(0) != 0;
    snprintf(o.msg, o.msglen, "%d item(s) on the clipboard (%s): open the target folder and paste", n, c->lib.clip_move ? "cut" : "copy");
    return OPE_OK;
}

static int op_nop(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }          // a node whose work is the menu's own (rename)

static int op_nop2(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }         // menu.t64: the menu itself opens the tape
static const ArgSpec path_arg[] = { ARG_PATH("path") };

static int op_mark_all(CoreCtx *c, const OpArgs &, OpOut &o)
{
    c->lib.mark_all = true;
    snprintf(o.msg, o.msglen, "Everything in this folder is marked");
    return OPE_OK;
}
static int op_mark_none(CoreCtx *c, const OpArgs &, OpOut &o)
{
    c->lib.mark_none = true;
    snprintf(o.msg, o.msglen, "Nothing is marked");
    return OPE_OK;
}

static bool name_ok(const char *n)
{
    if (!n[0] || !strcmp(n, ".") || !strcmp(n, "..")) {
        return false;
    }
    for (; *n; n++) {
        if (*n < 0x20 || strchr("/\\:*?\"<>|", *n)) {
            return false;
        }
    }
    return true;
}

static int op_rename(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *from = a.s(0), *name = a.s(1);
    if (!name_ok(name)) {
        snprintf(o.msg, o.msglen, "\"%s\" is not a name a file can have", name);
        return OPE_FAILED;
    }
    char to[FS_PATH_MAX];                                  // a folder path and a long name (255)
    snprintf(to, sizeof(to), "%s", from);
    char *sl = strrchr(to, '/');
    if (!sl) {
        return OPE_ARG;
    }
    snprintf(sl + 1, sizeof(to) - (sl + 1 - to), "%s", name);
    uint64_t sz;
    bool d;
    if (c->fs->stat(to, &sz, &d)) {
        snprintf(o.msg, o.msglen, "%s exists already", name);
        return OPE_FAILED;
    }
    if (!c->fs->rename(from, to)) {
        snprintf(o.msg, o.msglen, "Cannot rename %s (read only medium?)", base_of(from));
        return OPE_FAILED;
    }
    // The clipboard holds paths: the item renamed (or a folder above it) is the same item under its new name (GitHub #42: paste said "cannot read: <old name>")
    size_t fl = strlen(from);
    for (int i = 0; i < c->lib.nclip; i++) {
        char *cp = c->lib.clip[i];
        if (inside(from, cp) && (cp[fl] == 0 || cp[fl] == '/')) {
            char moved[FS_PATH_MAX];
            snprintf(moved, sizeof(moved), "%s%s", to, cp + fl);
            snprintf(cp, sizeof(c->lib.clip[i]), "%s", moved);
        }
    }
    snprintf(o.msg, o.msglen, "%s is now %s", base_of(from), name);
    return OPE_OK;
}

static int op_mkdir(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (!name_ok(a.s(0))) {
        snprintf(o.msg, o.msglen, "\"%s\" is not a name a folder can have", a.s(0));
        return OPE_FAILED;
    }
    const char *dir = c->media_dir[0] ? c->media_dir : c->lib.dir;
    if (!dir[0] || !strcmp(dir, "/")) {
        snprintf(o.msg, o.msglen, "Pick a folder on a medium first");
        return OPE_FAILED;
    }
    char p[FS_PATH_MAX];                                  // a folder path and a long name (255)
    join(p, sizeof(p), dir, a.s(0));
    if (!c->fs->mkdir(p)) {
        snprintf(o.msg, o.msglen, "Cannot create %s (exists as a file, or the medium is read only)", p);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Folder %s made", p);
    return OPE_OK;
}

// "lib.show": the Library opens on the folder of the file a state names (the disk in the drive, the cartridge, the last program); an empty state: the Library as it is
static int op_show(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *p = ops_state(c, a.s(0));
    if (p && *p) {
        snprintf(c->lib.want_dir, sizeof(c->lib.want_dir), "%s", p);
        char *sl = strrchr(c->lib.want_dir, '/');
        if (sl && sl != c->lib.want_dir) {
            *sl = 0;
        } else {
            c->lib.want_dir[0] = 0;
        }
    }
    snprintf(o.msg, o.msglen, "%s", c->lib.want_dir[0] ? c->lib.want_dir : "Library");
    return OPE_OK;
}

static int op_set_home(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (!strcmp(c->lib.dir, "/")) {
        snprintf(o.msg, o.msglen, "Open a folder first");
        return OPE_FAILED;
    }
    snprintf(c->lib.home, sizeof(c->lib.home), "%s", c->lib.dir);
    OpIn in[2] = { in_str("key", HOME_DIR_KEY), in_str("value", c->lib.dir) };
    OpResult r;
    ops_call(c, "settings.set", in, 2, &r);
    snprintf(o.msg, o.msglen, "Home folder is %s", c->lib.dir);
    return OPE_OK;
}

// ---- states ------------------------------------------------------------------------------------------------------------------------------------------
static char sbuf[96];
static const char *state_folder(CoreCtx *c) { return c->lib.dir[0] ? c->lib.dir : "/"; }
static const char *state_marked(CoreCtx *c)
{
    snprintf(sbuf, sizeof(sbuf), "%d marked", c->lib.nmarked);
    return c->lib.nmarked ? sbuf : "none";
}
static const char *state_clip(CoreCtx *c)
{
    if (!c->lib.nclip) {
        return "empty";
    }
    snprintf(sbuf, sizeof(sbuf), "%d item(s), %s", c->lib.nclip, c->lib.clip_move ? "cut" : "copy");
    return sbuf;
}
static const char *state_cursor(CoreCtx *c) { return c->lib.cur[0] ? base_of(c->lib.cur) : "nothing"; }
static const char *state_home(CoreCtx *c) { return c->lib.home[0] ? c->lib.home : "not set"; }
static const char *state_has_clip(CoreCtx *c) { return c->lib.nclip ? "yes" : 0; }
static const char *state_has_sel(CoreCtx *c) { return c->lib.nmarked || c->lib.cur[0] ? "yes" : 0; }
static const char *state_in_media(CoreCtx *c) { return c->lib.dir[0] && strcmp(c->lib.dir, "/") ? "yes" : 0; }

static const ArgSpec value_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec rename_args[] = { ARG_PATH("path"), ARG_STRING("name", 255) };
static const ArgSpec state_arg[] = { ARG_STRING("state", 60) };
static const ArgSpec name_arg[] = { ARG_STRING("name", 255) };

void libfiles_register(OpRegistry &r)
{
    r.contributor("files");
    r.add_op("files.copy_job", 0, 0, op_copy_job);
    r.add_op("files.move_job", 0, 0, op_move_job);
    r.add_op("files.delete_job", 0, 0, op_delete_job);
    r.add_op("files.paste_job", 0, 0, op_paste_job);
    r.add_op("files.clip", value_arg, 1, op_clip);
    r.add_op("files.nop", 0, 0, op_nop);
    r.add_op("menu.t64", path_arg, 1, op_nop2);
    r.add_op("files.mark_all", 0, 0, op_mark_all);
    r.add_op("files.mark_none", 0, 0, op_mark_none);
    r.add_op("files.rename", rename_args, 2, op_rename);
    r.add_op("files.mkdir", name_arg, 1, op_mkdir);
    r.add_op("files.set_home", 0, 0, op_set_home);
    r.add_op("lib.show", state_arg, 1, op_show);
    r.add_state("files.folder", state_folder);
    r.add_state("files.marked", state_marked);
    r.add_state("files.clipboard", state_clip);
    r.add_state("files.cursor", state_cursor);
    r.add_state("files.home", state_home);
    r.add_state("files.has_clip", state_has_clip);
    r.add_state("files.has_selection", state_has_sel);
    r.add_state("files.in_media", state_in_media);
}
