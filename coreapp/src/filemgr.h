// The file manager of the core application (T192.3 leak 8, D-B): the model behind the F3 Library - the folder and its entries, the marks, the cursor, the list of a file that is a list
// (a tape image), the media chips, the search, the quick seek, the viewer (View, Hex View) and the file operations the person starts on them (clipboard, mark all, home folder, rename,
// new folder). The menu controller holds one and asks it; it knows no screen, no key and no dialog. What the person has to answer (a name, a folder to copy to, a question before a
// delete) is the controller's: it asks, and calls the operation here when the answer is in.
//
// A plain struct on purpose: the model is read by the views (through MenuCtl) and by the file operations (through CoreCtx::lib, which sync_lib() keeps current).
#ifndef COREAPP_FILEMGR_H
#define COREAPP_FILEMGR_H

#include "ctx.h"
#include "menuvlist.h"

struct FileMgr {
    enum { ROWS_START = 512, VROWS = 128, VIEW_ROWS = 24, VIEW_COLS = 96, VIEW_MAX = 262144, SEEK_MAX = 24 };

    // what the manager needs of whoever holds it: a line for the status row, and whether the open tab is the Library (the folder is only handed to the file operations then)
    struct Port {
        void *user;
        void (*say)(void *user, const char *text, bool bad);
        bool (*on_browser)(void *user);
    };

    // a row as the views draw it
    struct BrowseRow {
        char name[64], ext[8], sub[120], primary[40], badge[16];       // badge: [A] in drive A, [C] in the port, [P] the last program
        char value[120];                 // T209: what the row shows at its right edge, one line: the size of a file ("3 KB"), the folder of a search hit; empty for a folder (`sub` is the info panel's text, never a second line)
        bool dir;
        bool marked;                     // SPACE marked it (Manage: copy, move, delete take the marked rows)
        int tone;                        // 0 muted, 1 cyan, 2 violet, 3 amber, 4 coral
        bool info;                       // the line of a cut list: information, no entry
        bool more;                       // it has more than one action (RETURN opens the list)
    };

    FileMgr(CoreCtx *c, const Port &port);
    ~FileMgr();
    FileMgr(const FileMgr &) = delete;
    FileMgr &operator=(const FileMgr &) = delete;
    bool ensure_list();                             // the list and its flags are allocated (false: no memory)
    void clear_marks();                             // every flag down
    void say(const char *text, bool bad) const;     // a line for the status row

    CoreCtx *c;
    Port port;

    // ---- the model ----
    char path[200];                      // the folder ("/" the list of media); while an image is browsed, the image's path
    char list_path[200];                 // the folder a grown buffer (cap > START) was read for: kept for a relist of that folder, freed for another
    FsEntry *list;                       // the entries, from the heap, `cap` of them (listgrow.h: 512 first, more while the folder needs it)
    int cap, marked_cap;
    bool more;                           // the folder is longer than the list the heap budget allows: the Library says so on a last line
    int nlist;
    bool dir_ok;
    int sel;                             // the cursor
    bool *marked;                        // `cap` flags
    int image_ft;                        // inside a disk image: the index of its file type, else -1
    char last_dir[200];                  // the folder of the last file mounted or run
    char roots[8][16];                   // the media of the root listing, for the source chips
    int nroots;
    // a file that is a list for the browser (a tape image), and the result of a search: list[i] then names a file whose full path is vfull[i]
    int vmode;                           // 0 none, 1 a search, 4 the entries of a file (vlist)
    const MenuVList *vlist;
    int vl_index;                        // the entry the sheet was opened on
    char vl_path[FS_PATH_MAX];
    char vlabel[96];
    char (*vfull)[FS_PATH_MAX];
    // the search and the quick seek
    char search_q[40];
    bool seek_on;
    char seek[SEEK_MAX];
    uint32_t seek_at;
    // the viewer
    bool view, view_hex;
    char view_path[FS_PATH_MAX];
    uint8_t *view_data;
    uint32_t view_len, view_top;
    uint32_t *view_lines;
    int view_nlines;

    // ---- the listing ----
    void load_dir();                                // the folder again (or the list the Library shows instead of it)
    bool relist();                                  // the folder and the media again, the cursor stays on its name; true when something changed
    void enter_dir(const char *name);
    void parent_dir();
    void refresh_roots();
    void lib_sync();                                // the folder, the cursor and the marks into CoreCtx::lib, where the file operations read them
    void apply_pending();                           // what an operation asked of the Library (mark all, none, open this folder) is done
    const char *full_path(int i, char *buf, int n) const;
    bool visible_ext(const char *name, const FileType **ft) const;
    // T212 D5: while a drive mounts from the Library, only files of these extensions (".d64,.g64") are listed beside the folders; null: every file
    const char *ext_filter = 0;
    bool ext_wanted(const char *name) const;
    bool browse_image(const char *file);            // a disk image is a folder for the browser; false (and a line) when it cannot be read
    bool open_vlist(const MenuVList *vl, const char *file);   // the entries of a file are the rows
    void open_folder(const char *full);             // "Open" on a folder: the Library goes there
    bool reopen_last() const;                       // the core's "Menu opens at" is "Last screen" (T192.3 D30)
    void open_home_if_set();                        // the menu opens: the home folder is known, and F3 starts there when the setting says so
    void mark_all(bool all);
    // the media chips of the path row (uniform browser): fixed names, grey when the medium is not there
    int nchips() const;
    int chip_base(int vi) const;
    const char *chip_name(int i) const;
    int chip_active() const;
    bool media_level() const;                       // the Library stands on "/": the list of the media
    bool medium_at(int i, MediumInfo *mi) const;
    void more_text(char *out, int n) const;         // the last line of a list the heap budget cut
    void browse_row(int i, BrowseRow *r, const char *marks_spec) const;

    // ---- search and quick seek ----
    void run_search();
    void seek_key(int k);

    // ---- the viewer ----
    void start_view(const char *path, bool hex);
    void start_view_mem(const char *title, int kind, bool hex);
    void view_setup(const char *title, uint32_t n, bool hex);
    void view_line(int row, char *out, int n) const;
    void view_status(char *out, int n) const;
    void view_scroll(int how);                      // VS_UP .. VS_END (menuintent.h)

    // ---- the file operations a person starts on a file or on the marks ----
    void clip(bool cut, const char *file, bool file_is_marked);   // copy / cut to the clipboard
    void set_home();                                               // the folder the Library is on becomes the home folder
    void do_rename(const char *path, const char *name);            // path "" = a new folder
    // the jobs the actions of every file start, and the actions every file has
    static const Task *task_copy();
    static const Task *task_move();
    static const Task *task_delete();
    static const Task *task_paste();
    static const FileAction *universal(int *n);                    // View, Hex View, Copy to, Move to, clipboard, Rename, Delete
    void up_while_gone();
    static const FileAction *folder_open();                        // the sheet of a folder: Open, then the universal ones from the third, then folder_items
    static const FileAction *folder_items(int *n);
    // The person's text for a rename, a new folder or a search: no spaces at its end.
    static void trim_name(char *text);
    // The question before a delete, from what the operation will act on (CoreCtx::lib: the marks, else the file under the cursor): the menu and a REST call of the delete
    // operation ask the same words.
    static void delete_question(const CoreCtx *c, char *q, int n);
};

#endif
