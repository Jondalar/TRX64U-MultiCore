#include "filemgr.h"
#include "listgrow.h"
#include "menuintent.h"
#include "ops_glue.h"
#include "diag.h"
#include "home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define VIEW_LINES_MAX 12000

// FNV-1a over the names, so a directory that has not changed costs no redraw
static uint32_t list_sum(const FsEntry *l, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        for (const char *p = l[i].name; *p; p++) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        h = (h ^ (l[i].dir ? 1u : 0u)) * 16777619u;
    }
    return h;
}


// The path row's chips: the media a person looks for, always the same ones in the same place; a medium that is not there is greyed (a card that is out looks out,
// not like an empty folder). HOME is the Library's own list.
// chips 0-5 are media (the FTP hosts and the Assembly64 results are folders of the file system, netfs.cc), 6 Home
static const char *const g_chips[] = { "SD", "USB0", "USB1", "Flash", "FTP", "Online", "Home" };
#define NCHIPS 7
#define CHIP_MEDIA 6


// the name the file system uses for chip i, "" when the medium is not there
static const char *medium_of(const char roots[][16], int n, const char *chip)
{
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(roots[i], chip)) {
            return roots[i];
        }
    }
    return "";
}


static void vfill(FsEntry *e, const char *full, const char *name_src, bool dir, uint64_t size)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name_src);
    e->dir = dir;
    e->size = size;
    (void)full;
}

// ---- search ---------------------------------------------------------------------------------------------------------------------------------------
static bool name_matches(const char *name, const char *pat)
{
    // the pattern may sit anywhere in the name; ? is any one character
    int pl = (int)strlen(pat), nl = (int)strlen(name);
    for (int s = 0; s + pl <= nl; s++) {
        int k = 0;
        while (k < pl && (pat[k] == '?' || tolower((unsigned char)pat[k]) == tolower((unsigned char)name[s + k]))) {
            k++;
        }
        if (k == pl) {
            return true;
        }
    }
    return false;
}





FileMgr::FileMgr(CoreCtx *ctx, const Port &p) : c(ctx), port(p), list(0), cap(0), marked_cap(0), more(false), marked(0), nlist(0), dir_ok(true), sel(0), image_ft(-1), nroots(0), vmode(0), vlist(0), vl_index(0), seek_on(false), seek_at(0),
    view(false), view_hex(false), view_data(0), view_len(0), view_top(0), view_lines(0), view_nlines(0)
{
    strcpy(path, "/");
    last_dir[0] = 0;
    memset(roots, 0, sizeof(roots));
    vl_path[0] = 0;
    list_path[0] = 0;
    vlabel[0] = 0;
    vfull = new char[VROWS][FS_PATH_MAX];
    search_q[0] = 0;
    seek[0] = 0;
    view_path[0] = 0;
}

FileMgr::~FileMgr()
{
    free(list);
    free(marked);
    delete[] vfull;
}

// The list and its flags are there, as many flags as entries (a list the vmode fills needs them without a folder having been read).
bool FileMgr::ensure_list()
{
    if (!list) {
        list = (FsEntry *)malloc(sizeof(FsEntry) * ListGrow::START);
        cap = list ? ListGrow::START : 0;
    }
    if (list && (!marked || marked_cap != cap)) {
        free(marked);
        marked = (bool *)calloc((size_t)cap, sizeof(bool));
        marked_cap = marked ? cap : 0;
    }
    return list && marked;
}

void FileMgr::clear_marks()
{
    if (marked) {
        memset(marked, 0, sizeof(bool) * (size_t)cap);
    }
}

void FileMgr::say(const char *text, bool bad) const
{
    port.say(port.user, text, bad);
}

bool FileMgr::visible_ext(const char *name, const FileType **ft) const
{
    int l = (int)strlen(name);
    for (int i = 0; i < c->desc->nfiletypes; i++) {
        const char *ext = c->desc->filetypes[i].ext;
        int e = (int)strlen(ext);
        if (l > e && !strcasecmp(name + l - e, ext)) {
            if (ft) {
                *ft = &c->desc->filetypes[i];
            }
            return true;
        }
    }
    return false;
}

bool FileMgr::ext_wanted(const char *name) const
{
    if (!ext_filter || !*ext_filter) {
        return true;
    }
    int l = (int)strlen(name);
    for (const char *p = ext_filter; *p;) {
        const char *e = strchr(p, ',');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (l > n && !strncasecmp(name + l - n, p, n)) {
            return true;
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return false;
}

// the listing of the browser's directory, folders and then the files whose extension some action of the descriptor wants
void FileMgr::load_dir()
{
    if (vmode) {                                                  // a search result or the entries of a tape: the list stands, the folder is not read
        lib_sync();
        return;
    }
    if (nroots == 0 || !strcmp(path, "/")) {
        refresh_roots();
    }
    // A FsEntry holds a whole FAT long name (T180 #38), 272 bytes: the folder is listed into list itself (after the ".." row) and the filter below moves the entries
    // that stay to the front; no second buffer of that size is kept. The list grows with the folder (listgrow.h, GitHub #93); `more` is what the heap budget cut.
    const int up = strcmp(path, "/") != 0 ? 1 : 0;
    if (list && cap > ListGrow::START && strcmp(list_path, path)) {   // another folder: a long folder's buffer goes back to the heap before the next one is sized against what is free (as the host's browser does); the same folder keeps it (T209: one read per growth)
        free(list);
        free(marked);
        list = 0;
        marked = 0;
        cap = 0;
        marked_cap = 0;
    }
    int n = ListGrow::read(c->fs, path, &list, &cap, up, &more);
    snprintf(list_path, sizeof(list_path), "%s", path);
    if (!ensure_list()) {                                         // no flags for the entries (marked is null): nothing may be listed
        dir_ok = false;
        more = false;
        nlist = 0;
        sel = 0;
        lib_sync();
        return;
    }
    dir_ok = n >= 0;
    nlist = 0;
    if (up && list) {
        memset(&list[0], 0, sizeof(FsEntry));
        strcpy(list[0].name, "..");
        list[0].dir = true;
        nlist = 1;
    }
    for (int i = 0; i < n; i++) {
        const FsEntry &fe = list[up + i];
        bool want;
        if (image_ft >= 0) {
            const char *ie = c->desc->filetypes[image_ft].inner_ext;
            int l = (int)strlen(fe.name), e = (int)strlen(ie);
            want = !fe.dir && l > e && !strcasecmp(fe.name + l - e, ie);
        } else {
            want = fe.dir || ext_wanted(fe.name);                  // every file is listed (T212: or only the types of the drive that mounts from here): the ones no action knows still have View, Copy, Rename, Delete
        }
        if (want) {
            if (nlist != up + i) {
                list[nlist] = fe;
            }
            nlist++;
        }
    }
    if (!strcmp(path, "/") && image_ft < 0 && c->lib.home[0] && nlist < cap) {      // the home folder, after the media
        memset(&list[nlist], 0, sizeof(FsEntry));
        snprintf(list[nlist].name, sizeof(list[nlist].name), "%s", "[Home]");
        list[nlist].dir = true;
        nlist++;
    }
    // the marks stay with the names (the list is read again every two seconds and after every job)
    bool same = !strcmp(c->lib.dir, path);
    clear_marks();
    for (int i = 0; same && c->lib.nmarked && i < nlist; i++) {
        for (int m = 0; m < c->lib.nmarked; m++) {
            if (!strcmp(c->lib.marked[m], list[i].name)) {
                marked[i] = true;
            }
        }
    }
    if (sel >= nlist) {
        sel = nlist ? nlist - 1 : 0;
    }
    lib_sync();
}

// The media the browser lists can turn up later than the menu does: a USB stick is still being mounted while the
// core starts, so the first listing of the root has no /Usb1 in it and only leaving the page and coming back used
// to bring it in. The host application looks again every few seconds for the same reason
// (hostui.cc:1760-1769). Here the browser's own directory is read again and the selection follows its entry by
// name, so a stick appearing above the cursor does not move it.
bool FileMgr::relist()
{
    char was[sizeof(list[0].name)];
    was[0] = 0;
    if (sel < nlist) {
        strncpy(was, list[sel].name, sizeof(was) - 1);
        was[sizeof(was) - 1] = 0;
    }
    uint32_t before = list_sum(list, nlist);
    char roots_before[sizeof(roots)];
    memcpy(roots_before, roots, sizeof(roots));
    int nroots_before = nroots;
    load_dir();
    refresh_roots();                                               // a stick that mounted late is a new source chip
    bool roots_same = nroots == nroots_before && !memcmp(roots_before, roots, sizeof(roots));
    if (list_sum(list, nlist) == before) {
        return !roots_same;
    }
    for (int i = 0; i < nlist; i++) {
        if (!strcmp(list[i].name, was)) {
            sel = i;
            break;
        }
    }
    return true;
}

void FileMgr::enter_dir(const char *name)
{
    if (!strcmp(name, "..")) {
        parent_dir();
        return;
    }
    if (name[0] == '[') {                                          // [Home]
        snprintf(path, sizeof(path), "%s", c->lib.home);
        vmode = 0;
        sel = 0;
        load_dir();
        return;
    }
    int l = (int)strlen(path);
    if (l + 1 + (int)strlen(name) >= (int)sizeof(path)) {
        say("The path is too long", true);
        return;
    }
    snprintf(path + l, sizeof(path) - l, "%s%s", l > 1 ? "/" : "", name);
    sel = 0;
    load_dir();
}

void FileMgr::parent_dir()
{
    if (vmode) {                                                  // out of a search or a list: back to the media
        vmode = 0;
        strcpy(path, "/");
        sel = 0;
        load_dir();
        return;
    }
    char *slash = strrchr(path, '/');
    if (!slash || slash == path) {                                // the top of a medium: ../ and LEFT go up to the media level, as in the host's File Browser
        strcpy(path, "/");
    } else {
        *slash = 0;
    }
    sel = 0;
    image_ft = -1;                                                // leaving a disk image (it has no folders below it)
    load_dir();
}

// ---- the library ------------------------------------------------------------------------------------------------------------------------
void FileMgr::refresh_roots()
{
    FsEntry r[8];
    int n = c->fs->list("/", r, 8);
    memset(roots, 0, sizeof(roots));
    nroots = 0;
    for (int i = 0; i < n && nroots < 8; i++) {
        snprintf(roots[nroots++], sizeof(roots[0]), "%s", r[i].name);
    }
}

// The chips shown: the media that are there, Home once it is set. chip_base(i) is the place of the i-th shown chip in g_chips.
int FileMgr::chip_base(int vi) const
{
    int k = 0;
    for (int b = 0; b < NCHIPS; b++) {
        bool show = b < CHIP_MEDIA ? medium_of(roots, nroots, g_chips[b])[0] != 0 : c->lib.home[0] != 0;
        if (show && k++ == vi) {
            return b;
        }
    }
    return -1;
}

int FileMgr::nchips() const
{
    int n = 0;
    for (int b = 0; b < NCHIPS; b++) {
        n += b < CHIP_MEDIA ? (medium_of(roots, nroots, g_chips[b])[0] != 0) : (c->lib.home[0] != 0);
    }
    return n;
}

const char *FileMgr::chip_name(int i) const
{
    int b = chip_base(i);
    return b >= 0 ? g_chips[b] : "";
}

int FileMgr::chip_active() const
{
    if (vmode || !strcmp(path, "/") || image_ft >= 0) {
        return image_ft >= 0 ? -1 : -1;
    }
    const char *p = path + 1;
    size_t l = strcspn(p, "/");
    for (int i = 0; i < CHIP_MEDIA; i++) {
        if (strlen(g_chips[i]) == l && !strncasecmp(g_chips[i], p, l)) {
            for (int v = 0; v < nchips(); v++) {
                if (chip_base(v) == i) {
                    return v;
                }
            }
        }
    }
    return -1;
}

// The media level, "/": the list of the media (and the Library's own lists), the first thing the browser shows, as the host's File Browser does (T104.2).
bool FileMgr::media_level() const
{
    return !vmode && image_ft < 0 && !strcmp(path, "/");
}

// What the medium under row `i` is: false when the row is not a medium (a list of the Library) or the board cannot tell.
bool FileMgr::medium_at(int i, MediumInfo *mi) const
{
    if (!media_level() || i < 0 || i >= nlist || list[i].name[0] == '[') {
        return false;
    }
    return c->fs->medium_info(list[i].name, mi);
}

void FileMgr::more_text(char *out, int n) const
{
    snprintf(out, n, "... more not shown");
}

void FileMgr::browse_row(int i, BrowseRow *r, const char *marks_spec) const
{
    memset(r, 0, sizeof(*r));
    if (i == nlist && more && !vmode) {
        more_text(r->name, sizeof(r->name));
        r->info = true;
        return;
    }
    if (i < 0 || i >= nlist) {
        return;
    }
    const FsEntry &f = list[i];
    fs_short_name(r->name, sizeof(r->name), f.name);                // the row shows it; every operation takes list[i].name whole
    r->dir = f.dir;
    r->marked = marked[i];
    r->badge[0] = 0;
    {                                                                 // [A] in drive A, [C] in the port, [P] the last program: the tab's ui `marks` = "A=state,C=state"
        char spec[160], full[FS_PATH_MAX];
        if (!f.dir && image_ft < 0 && marks_spec && marks_spec[0]) {
            snprintf(spec, sizeof(spec), "%s", marks_spec);
            full_path(i, full, sizeof(full));
            int bl = 0;
            for (char *p = spec; *p;) {
                char letter = *p;
                char *eq = strchr(p, '=');
                if (!eq) {
                    break;
                }
                char *end = strchr(eq, ',');
                char name[64];
                size_t nl = end ? (size_t)(end - eq - 1) : strlen(eq + 1);
                if (nl >= sizeof(name)) {
                    nl = sizeof(name) - 1;
                }
                memcpy(name, eq + 1, nl);
                name[nl] = 0;
                const char *w = ops_state(c, name);
                if (w && !strcasecmp(w, full) && bl < 12) {
                    bl += snprintf(r->badge + bl, sizeof(r->badge) - bl, "[%c]", letter);
                }
                p = end ? end + 1 : p + strlen(p);
            }
        }
    }
    if (f.dir && media_level()) {                                                 // the media level: what the medium is (the host's File Browser shows its size and file system)
        if (f.name[0] == '[') {                                                   // [Home], after the media
            snprintf(r->ext, sizeof(r->ext), "list");
            snprintf(r->sub, sizeof(r->sub), "Your home folder");
        } else {
            snprintf(r->ext, sizeof(r->ext), "medium");
            MediumInfo mi;
            if (!c->fs->medium_info(f.name, &mi)) {
                snprintf(r->sub, sizeof(r->sub), "Medium");
            } else if (!mi.ready) {
                snprintf(r->sub, sizeof(r->sub), "%s", mi.contents);
            } else {
                snprintf(r->sub, sizeof(r->sub), "%s  %s", mi.size, mi.contents);
            }
        }
        snprintf(r->primary, sizeof(r->primary), "Open");
        r->tone = 0;
        return;
    }
    if (f.dir) {
        snprintf(r->ext, sizeof(r->ext), "%s", strcmp(f.name, "..") ? "dir" : "up");
        snprintf(r->sub, sizeof(r->sub), "%s", strcmp(f.name, "..") ? "Folder" : "Parent folder");
        snprintf(r->primary, sizeof(r->primary), "Open");
        r->tone = 0;
        return;
    }
    const char *dot = strrchr(f.name, '.');
    snprintf(r->ext, sizeof(r->ext), "%s", dot ? dot + 1 : "");
    snprintf(r->sub, sizeof(r->sub), "%u KB", (unsigned)((f.size + 1023) >> 10));
    snprintf(r->value, sizeof(r->value), "%s", r->sub);
    if (vmode && i < VROWS) {                                                  // the folder it lies in
        snprintf(r->sub, sizeof(r->sub), "%s", vfull[i]);
        snprintf(r->value, sizeof(r->value), "%s", vfull[i]);
    }
    const FileType *ft = 0;
    r->tone = 1;
    if (image_ft >= 0) {
        ft = &c->desc->filetypes[image_ft];
        snprintf(r->primary, sizeof(r->primary), "%s", ft->ninner ? ft->inner[0].label : "");
        r->more = ft->ninner > 1;
        return;
    }
    if (visible_ext(f.name, &ft)) {
        snprintf(r->primary, sizeof(r->primary), "%s", ft->actions[0].label);
        r->more = true;                                                        // View, Copy, Rename, Delete are behind TAB
        r->tone = ft->ninner ? 2 : 1;                                       // a disk image (it has files inside) is violet
    } else {
        snprintf(r->primary, sizeof(r->primary), "View");
        r->more = true;
        r->tone = 0;
    }
}

const char *FileMgr::full_path(int i, char *buf, int n) const
{
    if (i < 0 || i >= nlist) {
        buf[0] = 0;
        return buf;
    }
    if (vmode) {
        snprintf(buf, n, "%s", i < VROWS ? vfull[i] : "");
    } else {
        snprintf(buf, n, "%s%s%s", path, strcmp(path, "/") ? "/" : "", list[i].name);
    }
    return buf;
}

// The folder, the cursor and the marks for the file operations (they read CoreCtx::lib, never this class).
void FileMgr::lib_sync()
{
    LibSel &l = c->lib;
    snprintf(l.dir, sizeof(l.dir), "%s", vmode ? "/" : path);
    l.cur[0] = 0;
    l.cur_dir = false;
    if (port.on_browser(port.user) && image_ft < 0 && sel < nlist && strcmp(list[sel].name, "..") && list[sel].name[0] != '[') {
        full_path(sel, l.cur, sizeof(l.cur));
        l.cur_dir = list[sel].dir;
    }
    if (port.on_browser(port.user) && !vmode) {
        int want = 0;
        for (int i = 0; i < nlist; i++) {
            want += marked[i] ? 1 : 0;
        }
        int fit = want ? ListGrow::fit(want, sizeof(l.marked[0])) : 0;
        const int room = fit > l.cap_marked ? fit : l.cap_marked;       // what fits, or what the kept buffer already holds
        int take = want < room ? want : room;
        if (take > l.cap_marked) {                                  // the names are written anew below: the old buffer is not copied
            free(l.marked);
            l.marked = (char (*)[FS_NAME_MAX + 1])malloc((size_t)take * sizeof(l.marked[0]));
            l.cap_marked = l.marked ? take : 0;
        }
        if (take > l.cap_marked) {
            take = l.cap_marked;
        }
        int n = 0;
        for (int i = 0; i < nlist && n < take; i++) {
            if (marked[i]) {
                snprintf(l.marked[n++], sizeof(l.marked[0]), "%s", list[i].name);
            }
        }
        l.nmarked = n;
        l.marked_cut = want - n;                                    // marked on screen, not kept for the operations: they say so
    }
}

void FileMgr::apply_pending()
{
    LibSel &l = c->lib;
    if (l.want_dir[0]) {                                                // the Home desk: open the Library where the medium's file lies
        snprintf(path, sizeof(path), "%s", l.want_dir);
        l.want_dir[0] = 0;
        vmode = 0;
        image_ft = -1;
        sel = 0;
        refresh_roots();
        load_dir();
    }
    if (l.mark_all) {
        l.mark_all = false;
        for (int i = 0; i < nlist; i++) {
            marked[i] = strcmp(list[i].name, "..") && list[i].name[0] != '[';
        }
        lib_sync();
    }
    if (l.mark_none) {
        l.mark_none = false;
        clear_marks();
        l.nmarked = l.marked_cut = 0;
    }
}

void FileMgr::run_search()
{
    vmode = 1;
    snprintf(vlabel, sizeof(vlabel), "SEARCH: %s", search_q);
    nlist = 0;
    sel = 0;
    clear_marks();
    image_ft = -1;
    if (!search_q[0]) {
        lib_sync();
        return;
    }
    // The search reads whole folders (a buffer that grows with the folder, listgrow.h) and queues folder paths: taken from the heap for this search, given back at its end.
    FsEntry *raw = 0;
    int rawcap = 0;
    char (*const queue)[FS_PATH_MAX] = (char (*)[FS_PATH_MAX])malloc((size_t)64 * FS_PATH_MAX);
    if (!ensure_list() || !queue || !(raw = (FsEntry *)malloc(sizeof(FsEntry) * ListGrow::START))) {
        free(raw);
        free(queue);
        say("No memory for the search", true);
        lib_sync();
        return;
    }
    int qh = 0, qt = 0;
    // from the medium the Library is on, else from every medium
    char root[200];
    root[0] = 0;
    if (strcmp(path, "/") && path[0] == '/') {
        const char *p = path + 1;
        size_t l = strcspn(p, "/");
        if (l > sizeof(root) - 2) {
            l = sizeof(root) - 2;
        }
        root[0] = '/';
        memcpy(root + 1, p, l);                                    // (the firmware's printf has no %.*s)
        root[1 + l] = 0;
    }
    if (root[0]) {
        snprintf(queue[qt++], FS_PATH_MAX, "%s", root);
    } else {
        FsEntry r[10];
        int k = c->fs->list("/", r, 10);
        for (int i = 0; i < k && qt < 8; i++) {
            if (!strcasecmp(r[i].name, "FTP") || !strcasecmp(r[i].name, "Online")) {         // a search of "every medium" does not walk the network
                continue;
            }
            snprintf(queue[qt++], FS_PATH_MAX, "/%s", r[i].name);
        }
    }
    uint32_t t0 = c->io->now_ms();
    rawcap = ListGrow::START;
    int dirs = 0;
    bool cut = false;
    while (qh < qt && nlist < VROWS) {
        if (dirs >= 600 || c->io->now_ms() - t0 > 8000) {
            cut = true;
            break;
        }
        const char *dir = queue[qh % 64];
        char dcopy[FS_PATH_MAX];
        snprintf(dcopy, sizeof(dcopy), "%s", dir);
        qh++;
        bool folder_cut = false;
        int k = ListGrow::read(c->fs, dcopy, &raw, &rawcap, 0, &folder_cut);
        cut = cut || folder_cut;                                   // a folder the budget cut: the answer says so
        dirs++;
        for (int i = 0; i < k && nlist < VROWS; i++) {
            char full[FS_PATH_MAX];
            snprintf(full, sizeof(full), "%s%s%s", dcopy, dcopy[strlen(dcopy) - 1] == '/' ? "" : "/", raw[i].name);
            if (name_matches(raw[i].name, search_q)) {
                snprintf(vfull[nlist], FS_PATH_MAX, "%s", full);
                vfill(&list[nlist], full, raw[i].name, raw[i].dir, raw[i].size);
                nlist++;
            }
            if (raw[i].dir && qt - qh < 63) {
                snprintf(queue[qt % 64], FS_PATH_MAX, "%s", full);
                qt++;
            }
        }
    }
    free(raw);
    free(queue);
    char s[120];
    snprintf(s, sizeof(s), "%d found in %d folders%s", nlist, dirs, cut || nlist >= VROWS ? " (stopped: refine the name)" : "");
    say(s, false);
    lib_sync();
}

// ---- quick seek --------------------------------------------------------------------------------------------------------------------------------------
void FileMgr::seek_key(int k)
{
    uint32_t now = c->io->now_ms();
    int l = (int)strlen(seek);
    if (now - seek_at > 1500 || l >= (int)sizeof(seek) - 1) {
        l = 0;
    }
    seek[l++] = (char)k;
    seek[l] = 0;
    seek_at = now;
    for (int i = 0; i < nlist; i++) {
        const char *nm = list[i].name;
        int j = 0;
        while (j < l && nm[j] && (seek[j] == '?' || tolower((unsigned char)seek[j]) == tolower((unsigned char)nm[j]))) {
            j++;
        }
        if (j == l) {
            sel = i;
            break;
        }
    }
    char s[48];
    snprintf(s, sizeof(s), "Seek: %s", seek);
    say(s, false);
    lib_sync();
}

// the viewer on text the application has in memory (the debug log, the EDID, the trace)
void FileMgr::start_view_mem(const char *title, int kind, bool hex)
{
    if (!view_data) {
        view_data = (uint8_t *)malloc(VIEW_MAX + 1);
        view_lines = (uint32_t *)malloc(sizeof(uint32_t) * (12000 + 2));
    }
    if (!view_data || !view_lines) {
        say("No memory for the viewer", true);
        return;
    }
    int n;
    const char *d = diag_text(kind, &n);
    if (n > (int)VIEW_MAX) {
        n = (int)VIEW_MAX;
    }
    memcpy(view_data, d, n);
    view_setup(title, (uint32_t)n, hex);
}

void FileMgr::start_view(const char *file, bool hex)
{
    CoreFile *f = c->fs->open(file, false);
    if (!f) {
        say("Cannot open the file", true);
        return;
    }
    uint64_t size = f->size();
    if (!view_data) {
        view_data = (uint8_t *)malloc(VIEW_MAX + 1);
        view_lines = (uint32_t *)malloc(sizeof(uint32_t) * (VIEW_LINES_MAX + 2));
    }
    if (!view_data || !view_lines) {
        c->fs->close(f);
        say("No memory for the viewer", true);
        return;
    }
    uint32_t n = size > VIEW_MAX ? VIEW_MAX : (uint32_t)size;
    bool ok = n == 0 || f->read(0, view_data, n);
    c->fs->close(f);
    if (!ok) {
        say("Cannot read the file", true);
        return;
    }
    view_setup(file, n, hex);
    say(size > VIEW_MAX ? "Only the first 256 KB are shown" : "", false);
}

void FileMgr::view_setup(const char *title, uint32_t n, bool hex)
{
    view_len = n;
    view_hex = hex;
    view_top = 0;
    snprintf(view_path, sizeof(view_path), "%s", title);
    view_nlines = 0;
    if (!hex) {                                                    // line starts: a newline ends a line, a long line wraps at the screen's width
        uint32_t col = 0;
        view_lines[view_nlines++] = 0;
        for (uint32_t i = 0; i < n && view_nlines < 12000; i++) {
            uint8_t ch = view_data[i];
            if (ch == '\n' || col >= VIEW_COLS) {
                view_lines[view_nlines++] = ch == '\n' ? i + 1 : i;
                col = 0;
                if (ch == '\n') {
                    continue;
                }
            }
            col++;
        }
        view_lines[view_nlines] = n;
    }
    view = true;
}

void FileMgr::view_line(int row, char *out, int n) const
{
    out[0] = 0;
    if (!view) {
        return;
    }
    if (view_hex) {
        uint32_t off = view_top + (uint32_t)row * 16;
        if (off >= view_len) {
            return;
        }
        int p = snprintf(out, n, "%08X ", (unsigned)off);
        char asc[20];
        for (int i = 0; i < 16; i++) {
            if (i == 8 && p < n) {
                out[p++] = ' ';
            }
            if (off + i < view_len) {
                p += snprintf(out + p, n - p, " %02X", view_data[off + i]);
                asc[i] = view_data[off + i] >= 0x20 && view_data[off + i] < 0x7F ? (char)view_data[off + i] : '.';
            } else {
                p += snprintf(out + p, n - p, "   ");
                asc[i] = ' ';
            }
        }
        asc[16] = 0;
        snprintf(out + p, n - p, "  |%s|", asc);
        return;
    }
    int li = (int)view_top + row;
    if (li >= view_nlines) {
        return;
    }
    uint32_t a = view_lines[li], b = view_lines[li + 1];
    int k = 0;
    for (uint32_t i = a; i < b && k < n - 1 && k < VIEW_COLS; i++) {
        uint8_t ch = view_data[i];
        if (ch == '\n' || ch == '\r') {
            continue;
        }
        out[k++] = ch == '\t' ? ' ' : (ch >= 0x20 && ch < 0x7F ? (char)ch : '.');
    }
    out[k] = 0;
}

void FileMgr::view_status(char *out, int n) const
{
    if (view_hex) {
        snprintf(out, n, "HEX  %08X of %08X", (unsigned)view_top, (unsigned)view_len);
    } else {
        snprintf(out, n, "TEXT  line %d of %d", (int)view_top + 1, view_nlines);
    }
}

// A disk image is a folder for the browser (DiskFs, d1541_fs.h): the path becomes the image's, the listing the files inside it.
bool FileMgr::browse_image(const char *file)
{
    if (strlen(file) >= sizeof(path)) {                            // path holds a folder, or the image while it is browsed: a path cut is another path
        say("The path is too long", true);
        return false;
    }
    const FileType *ft = 0;
    if (!visible_ext(file, &ft) || !ft->ninner) {
        say("This is no disk image", true);
        return false;
    }
    char keep[sizeof(path)];
    snprintf(keep, sizeof(keep), "%s", path);
    snprintf(path, sizeof(path), "%s", file);
    image_ft = (int)(ft - c->desc->filetypes);
    sel = 0;
    load_dir();
    if (!dir_ok) {
        snprintf(path, sizeof(path), "%s", keep);
        image_ft = -1;
        load_dir();
        say("The disk cannot be read", true);
    }
    return true;
}

// the entries of a file are the rows of the Library (a tape image): the provider lists them, the full path of every row is the file's
bool FileMgr::open_vlist(const MenuVList *vl, const char *file)
{
    if (!ensure_list()) {
        say("No memory for the list", true);
        return false;
    }
    int n = vl->list(c, file, list, VROWS);
    if (n < 0) {
        say(vl->bad, true);
        return false;
    }
    vmode = 4;
    vlist = vl;
    const char *slash = strrchr(file, '/');
    snprintf(vlabel, sizeof(vlabel), "%s%s", vl->label, slash ? slash + 1 : file);
    snprintf(vl_path, sizeof(vl_path), "%s", file);
    nlist = n;
    for (int i = 0; i < n; i++) {
        snprintf(vfull[i], FS_PATH_MAX, "%s", file);
    }
    sel = 0;
    clear_marks();
    lib_sync();
    return true;
}

// "Open" on a folder (the sheet of a folder) and RETURN on a folder of a list: the Library goes there
void FileMgr::open_folder(const char *full)
{
    snprintf(path, sizeof(path), "%s", full);
    vmode = 0;
    sel = 0;
    load_dir();
}

// The menu opens: the home folder is read from the core's settings, and F3 starts there when "Enter Home on Startup" says so (T180 D10).
void FileMgr::open_home_if_set()
{
    snprintf(c->lib.home, sizeof(c->lib.home), "%s", home_dir(c));
    // the Library opens where the host's File Browser does: on the media level ("/"), and later where it was left (T104.2, Alex 2026-10-29)
    if (home_enter(c) && c->lib.home[0] && !vmode) {
        snprintf(path, sizeof(path), "%s", c->lib.home);
    }
}

bool FileMgr::reopen_last() const
{
    return home_reopen_last(c);                                  // "Menu opens at" = Last screen (T192.3 D30)
}

void FileMgr::mark_all(bool all)
{
    for (int i = 0; i < nlist; i++) {
        marked[i] = all && strcmp(list[i].name, "..") && list[i].name[0] != '[';
    }
    lib_sync();
}

// The viewer's cursor: a line, a page, the ends
void FileMgr::view_scroll(int how)
{
    int total = view_hex ? (int)((view_len + 15) / 16) : view_nlines;
    int top = view_hex ? (int)(view_top / 16) : (int)view_top;
    int maxtop = total > VIEW_ROWS ? total - VIEW_ROWS : 0;
    switch (how) {
    case VS_UP: top--; break;
    case VS_DOWN: top++; break;
    case VS_PAGE_UP: top -= VIEW_ROWS - 1; break;
    case VS_PAGE_DOWN: top += VIEW_ROWS - 1; break;
    case VS_HOME: top = 0; break;
    default: top = maxtop; break;
    }
    if (top < 0) top = 0;
    if (top > maxtop) top = maxtop;
    view_top = view_hex ? (uint32_t)top * 16 : (uint32_t)top;
}

// ---- the file operations ---------------------------------------------------------------------------------------------------------------------------
// Copy or cut to the clipboard: the marks, or the one file the sheet was opened on (the clipboard then is that file alone); file null: the cursor and the marks as they are.
void FileMgr::clip(bool cut, const char *file, bool file_is_marked)
{
    OpIn in[1] = { in_int("value", cut ? 1 : 0) };
    OpResult r;
    if (file) {
        if (!file_is_marked) {
            c->lib.nmarked = c->lib.marked_cut = 0;
        }
        snprintf(c->lib.cur, sizeof(c->lib.cur), "%s", file);
    } else {
        lib_sync();
    }
    ops_call(c, "files.clip", in, 1, &r);
    say(r.msg, false);
}

void FileMgr::set_home()
{
    OpResult r;
    lib_sync();
    ops_call(c, "files.set_home", 0, 0, &r);
    say(r.msg, false);
}

// A new name for a file, or a new folder (path ""); the Library lists the folder again
void FileMgr::do_rename(const char *from, const char *name)
{
    OpResult r;
    int rc;
    if (!from[0]) {
        OpIn mk[1] = { in_str("name", name) };
        rc = ops_call(c, "files.mkdir", mk, 1, &r);
    } else {
        OpIn in[2] = { in_str("path", from), in_str("name", name) };
        rc = ops_call(c, "files.rename", in, 2, &r);
    }
    say(r.msg, rc != 0);
    load_dir();
    up_while_gone();
}

// the folder the browser is in was renamed or deleted from inside (the sheet of "..", T219): the nearest one above that lists
void FileMgr::up_while_gone()
{
    while (!vmode && image_ft < 0 && !dir_ok && strcmp(path, "/")) {
        parent_dir();
    }
}

void FileMgr::delete_question(const CoreCtx *c, char *q, int n)
{
    if (c->lib.nmarked > 1) {
        if (c->lib.marked_cut) {
            snprintf(q, n, "Delete %d of the %d marked items (%d are not taken: memory)?", c->lib.nmarked, c->lib.nmarked + c->lib.marked_cut, c->lib.marked_cut);
        } else {
            snprintf(q, n, "Delete the %d marked items?", c->lib.nmarked);
        }
        return;
    }
    const char *slash = strrchr(c->lib.cur, '/');
    snprintf(q, n, "Delete %s?", slash ? slash + 1 : c->lib.cur);
}

void FileMgr::trim_name(char *text)
{
    int l = (int)strlen(text);
    while (l > 0 && text[l - 1] == ' ') {
        text[--l] = 0;
    }
}

// the Tasks the actions of every file start
static const Task g_copy_task = { "Copy", "files.copy_job", 0, 0, false, 0, 0, true, "Copy here", 0 };
static const Task g_move_task = { "Move", "files.move_job", 0, 0, false, 0, 0, true, "Move here", 0 };
static const Task g_delete_task = { "Delete", "files.delete_job", 0, 0, false, 0, 0, false, 0, 0 };
static const Task g_paste_task = { "Paste", "files.paste_job", 0, 0, false, 0, 0, false, 0, 0 };
const Task *FileMgr::task_copy() { return &g_copy_task; }
const Task *FileMgr::task_move() { return &g_move_task; }
const Task *FileMgr::task_delete() { return &g_delete_task; }
const Task *FileMgr::task_paste() { return &g_paste_task; }

// What every file has besides what its type offers (Gideon's context menu: View, Hex View, Copy to, Move to, Rename, Delete; the favourites are ours).
static const FileAction g_universal[] = {
    { "View", "ui.view", 0, 0, false, 0, 0, 0, 0, 'T' },
    { "Hex View", "ui.hex", 0, 0, false, 0, 0, 0, 0, 'H' },
    { "Copy to...", "ui.copy", 0, 0, false, 0, 0, 0, 0, 'C' },
    { "Move to...", "ui.move", 0, 0, false, 0, 0, 0, 0, 'V' },
    { "Copy to clipboard", "ui.clip0", 0, 0, false, 0, 0, 0, 0, 'Y' },
    { "Cut to clipboard", "ui.clip1", 0, 0, false, 0, 0, 0, 0, 'X' },
    { "Rename", "ui.rename", 0, 0, false, 0, 0, 0, 0, 'N' },
    { "Delete", "ui.delete", 0, 0, false, 0, 0, 0, 0, 'D' },
};
// the sheet of a folder (RIGHT on a folder row): what is done to it, and to the folder the browser is in
static const FileAction g_folder_open = { "Open", "ui.open", 0, 0, false, 0, 0, 0, 0, 'O' };
static const FileAction g_folder_items[] = {
    { "Create folder here", "ui.mkdir", 0, 0, false, 0, 0, 0, 0, 'K' },
    { "Paste here", "ui.paste", 0, 0, false, 0, 0, 0, 0, 'P' },
    { "Mark all", "ui.markall", 0, 0, false, 0, 0, 0, 0, 'A' },
    { "Mark none", "ui.marknone", 0, 0, false, 0, 0, 0, 0, 'Z' },
    { "Set as home folder", "ui.home", 0, 0, false, 0, 0, 0, 0, 'M' },
};
const FileAction *FileMgr::universal(int *n) { *n = (int)(sizeof(g_universal) / sizeof(g_universal[0])); return g_universal; }
const FileAction *FileMgr::folder_open() { return &g_folder_open; }
const FileAction *FileMgr::folder_items(int *n) { *n = (int)(sizeof(g_folder_items) / sizeof(g_folder_items[0])); return g_folder_items; }
