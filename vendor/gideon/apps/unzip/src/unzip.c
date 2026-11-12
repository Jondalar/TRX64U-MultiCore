/*
 * unzip.c - extracts ZIP files from the file browser.
 *
 * The manifest registers three actions for .zip:
 *   folder  extract into a new folder named after the file
 *   here    extract next to the file
 *   one     list the entries and extract the one chosen, next to the file
 *
 * miniz reads the archive through the firmware's fseek/fread, and every entry
 * is written out while it is inflated, so neither the archive nor an entry
 * has to fit in memory.
 *
 * An entry whose name would leave the destination - an absolute path, a ".."
 * component, a backslash or a drive letter - is skipped and counted.
 */

#include "u2p_app.h"
#include "miniz.h"

#define PATH_MAX_LEN 256
#define FOLDER_TRIES 99

static const u2p_api_t *api;

/* --- strings ---------------------------------------------------------- */

static void copy(char *d, const char *s, int size)
{
    int i = 0;
    for (; s[i] && (i < size - 1); i++) {
        d[i] = s[i];
    }
    d[i] = 0;
}

/* Appends s to d; returns 0 when it did not fit. */
static int append(char *d, const char *s, int size)
{
    int n = (int)strlen(d);
    int m = (int)strlen(s);
    if (n + m >= size) {
        return 0;
    }
    memcpy(d + n, s, m + 1);
    return 1;
}

static void append_number(char *d, int value, int size)
{
    char digits[12];
    int i = sizeof(digits) - 1;
    digits[i] = 0;
    do {
        digits[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value && i);
    append(d, digits + i, size);
}

static int ends_with_zip(const char *name)
{
    int n = (int)strlen(name);
    if (n < 4) {
        return 0;
    }
    const char *e = name + n - 4;
    return (e[0] == '.') && ((e[1] | 0x20) == 'z') && ((e[2] | 0x20) == 'i') && ((e[3] | 0x20) == 'p');
}

/* An entry name that stays inside the destination. */
static int safe_name(const char *name)
{
    if (!name[0] || (name[0] == '/')) {
        return 0;
    }
    const char *p = name;
    while (*p) {
        const char *seg = p;
        while (*p && (*p != '/')) {
            if ((*p == '\\') || (*p == ':')) {
                return 0;
            }
            p++;
        }
        if ((p - seg == 2) && (seg[0] == '.') && (seg[1] == '.')) {
            return 0;
        }
        if (*p == '/') {
            p++;
        }
    }
    return 1;
}

/* --- miniz callbacks -------------------------------------------------- */

static void *z_alloc(void *opaque, size_t items, size_t size) { return api->mem->malloc(items * size); }

static void z_free(void *opaque, void *p) { api->mem->free(p); }

static void *z_realloc(void *opaque, void *p, size_t items, size_t size) { return api->mem->realloc(p, items * size); }

static size_t z_read(void *opaque, mz_uint64 offset, void *buf, size_t n)
{
    if (api->fs->fseek(opaque, (long)offset, 0) != 0) {
        return 0;
    }
    int got = api->fs->fread(buf, 1, (int)n, opaque);
    return (got > 0) ? (size_t)got : 0;
}

static size_t z_write(void *opaque, mz_uint64 offset, const void *buf, size_t n)
{
    int put = api->fs->fwrite(buf, 1, (int)n, opaque);
    return (put > 0) ? (size_t)put : 0;
}

/* --- the archive ------------------------------------------------------ */

typedef struct
{
    mz_zip_archive zip;
    void *file;
    char dir[PATH_MAX_LEN];  /* the folder holding the archive, no trailing '/' */
    char name[PATH_MAX_LEN]; /* the archive's file name */
} archive_t;

static int open_archive(archive_t *a, const char *path)
{
    const char *slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            slash = p;
        }
    }
    int dir_len = (int)(slash - path);
    if ((dir_len <= 0) || (dir_len >= PATH_MAX_LEN)) {
        return 0;
    }
    memcpy(a->dir, path, dir_len);
    a->dir[dir_len] = 0;
    copy(a->name, slash + 1, PATH_MAX_LEN);

    a->file = api->fs->fopen(path, "rb");
    if (!a->file) {
        return 0;
    }
    api->fs->fseek(a->file, 0, 2);
    long size = api->fs->ftell(a->file);

    memset(&a->zip, 0, sizeof(a->zip));
    a->zip.m_pAlloc = z_alloc;
    a->zip.m_pFree = z_free;
    a->zip.m_pRealloc = z_realloc;
    a->zip.m_pRead = z_read;
    a->zip.m_pIO_opaque = a->file;
    if ((size <= 0) || !mz_zip_reader_init(&a->zip, (mz_uint64)size, 0)) {
        api->fs->fclose(a->file);
        return 0;
    }
    return 1;
}

static void close_archive(archive_t *a)
{
    mz_zip_reader_end(&a->zip);
    api->fs->fclose(a->file);
}

/* Creates every folder on the way to `entry` below `base`. Only the part up
 * to the last '/' is a folder; with `whole` set the entry itself is one too. */
static int make_folders(const char *base, const char *entry, int whole)
{
    char path[PATH_MAX_LEN];
    copy(path, base, PATH_MAX_LEN);
    int n = (int)strlen(path);
    for (const char *p = entry; *p; p++) {
        if ((*p == '/') || (whole && !p[1])) {
            int len = (int)(p - entry) + ((*p == '/') ? 0 : 1);
            if (n + 1 + len >= PATH_MAX_LEN) {
                return 0;
            }
            path[n] = '/';
            memcpy(path + n + 1, entry, len);
            path[n + 1 + len] = 0;
            if (api->fs->mkdir(path) != U2P_OK) {
                return 0;
            }
        }
    }
    return 1;
}

/* Writes entry `index` to `dest`/<its name>. */
static int extract(archive_t *a, mz_uint index, const char *dest, const char *entry)
{
    char path[PATH_MAX_LEN];
    copy(path, dest, PATH_MAX_LEN);
    if (!append(path, "/", PATH_MAX_LEN) || !append(path, entry, PATH_MAX_LEN)) {
        return 0;
    }
    void *out = api->fs->fopen(path, "wb");
    if (!out) {
        return 0;
    }
    int ok = mz_zip_reader_extract_to_callback(&a->zip, index, z_write, out, 0);
    api->fs->fclose(out);
    return ok;
}

typedef struct
{
    int files;
    int skipped;
    int failed;
} counts_t;

static void extract_all(archive_t *a, const char *dest, counts_t *c)
{
    mz_uint n = mz_zip_reader_get_num_files(&a->zip);
    void *progress = api->ui->progress_open("Unzipping", (int)n);
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&a->zip, i, &st)) {
            c->failed++;
        } else if (!safe_name(st.m_filename)) {
            c->skipped++;
        } else if (st.m_is_directory) {
            if (!make_folders(dest, st.m_filename, 1)) {
                c->failed++;
            }
        } else if (make_folders(dest, st.m_filename, 0) && extract(a, i, dest, st.m_filename)) {
            c->files++;
        } else {
            c->failed++;
        }
        api->ui->progress_set(progress, 1, NULL);
    }
    api->ui->progress_close(progress);
}

/* A new folder next to the archive, named after it: games.zip gives games,
 * or games_1 and on when that is taken. */
static int new_folder(archive_t *a, char *dest)
{
    char base[PATH_MAX_LEN];
    copy(base, a->name, PATH_MAX_LEN);
    if (ends_with_zip(base)) {
        base[strlen(base) - 4] = 0;
    }
    char name[PATH_MAX_LEN];
    for (int attempt = 0; attempt <= FOLDER_TRIES; attempt++) {
        copy(name, base, PATH_MAX_LEN);
        if (attempt) {
            append(name, "_", PATH_MAX_LEN);
            append_number(name, attempt, PATH_MAX_LEN);
        }
        if (!api->fs->exists(a->dir, name)) {
            copy(dest, a->dir, PATH_MAX_LEN);
            if (append(dest, "/", PATH_MAX_LEN) && append(dest, name, PATH_MAX_LEN) && (api->fs->mkdir(dest) == 0)) {
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

static void report(const counts_t *c, const char *where)
{
    char msg[160] = "Extracted ";
    append_number(msg, c->files, sizeof(msg));
    append(msg, " file(s) to ", sizeof(msg));
    append(msg, where, sizeof(msg));
    if (c->skipped) {
        append(msg, "\nSkipped ", sizeof(msg));
        append_number(msg, c->skipped, sizeof(msg));
        append(msg, " with an unsafe name", sizeof(msg));
    }
    if (c->failed) {
        append(msg, "\nFailed: ", sizeof(msg));
        append_number(msg, c->failed, sizeof(msg));
    }
    api->ui->popup("Unzip", msg);
}

static void pick_one(archive_t *a)
{
    mz_uint n = mz_zip_reader_get_num_files(&a->zip);
    const char **items = api->mem->malloc(n * sizeof(char *));
    mz_uint *index = api->mem->malloc(n * sizeof(mz_uint));
    char *names = api->mem->malloc(n * MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE);
    if (!items || !index || !names) {
        api->ui->popup("Unzip", "Not enough memory for the list.");
        return;
    }
    int count = 0;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&a->zip, i, &st) && !st.m_is_directory && safe_name(st.m_filename)) {
            char *slot = names + count * MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE;
            copy(slot, st.m_filename, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE);
            items[count] = slot;
            index[count] = i;
            count++;
        }
    }
    if (!count) {
        api->ui->popup("Unzip", "No file in this archive to extract.");
        return;
    }
    int choice = api->ui->menu(a->name, items, count);
    if (choice < 0) {
        return;
    }
    counts_t c = { 0, 0, 0 };
    if (make_folders(a->dir, items[choice], 0) && extract(a, index[choice], a->dir, items[choice])) {
        c.files = 1;
    } else {
        c.failed = 1;
    }
    report(&c, "this folder");
}

int app_main(const u2p_api_t *api_in, const u2p_app_context_t *ctx)
{
    api = api_in;
    if (!ctx->selected_file || !ctx->action) {
        api->ui->popup("Unzip", "Choose a .zip in the file browser\nand pick Unzip from its menu.");
        return 1;
    }

    static archive_t a;
    if (!open_archive(&a, ctx->selected_file)) {
        api->ui->popup("Unzip", "Cannot read this file as a ZIP.");
        return 1;
    }

    const char *action = ctx->action;
    counts_t c = { 0, 0, 0 };
    if (!strcmp(action, "one")) {
        pick_one(&a);
    } else if (!strcmp(action, "here")) {
        extract_all(&a, a.dir, &c);
        report(&c, "this folder");
    } else {
        char dest[PATH_MAX_LEN];
        if (new_folder(&a, dest)) {
            extract_all(&a, dest, &c);
            const char *last = dest;
            for (const char *p = dest; *p; p++) {
                if (*p == '/') {
                    last = p + 1;
                }
            }
            char where[PATH_MAX_LEN];
            copy(where, last, PATH_MAX_LEN);
            append(where, "/", PATH_MAX_LEN);
            report(&c, where);
        } else {
            api->ui->popup("Unzip", "Cannot create the folder.");
        }
    }
    close_archive(&a);
    return 0;
}
