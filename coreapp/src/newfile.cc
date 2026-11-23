#include "newfile.h"
#include "textcopy.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void name_with_ext(const char *path, const char *ext, char *out, int outlen)
{
    size_t l = strlen(path);
    size_t e = strlen(ext);
    if (l >= e && !strcasecmp(path + l - e, ext)) {
        snprintf(out, outlen, "%s", path);
    } else {
        snprintf(out, outlen, "%s%s", path, ext);
    }
}

// the first free `stem`, `stem2` ... in `dir`: 0 and the full path in `out`, 1 when 99 names are taken
static int pick_dir_name(CoreFs *fs, const char *dir, const char *stem, const char *ext, char *out, int outlen)
{
    for (int i = 1; i < 100; i++) {
        char name[96];
        if (i == 1) {
            snprintf(name, sizeof(name), "%s%s", stem, ext);
        } else {
            snprintf(name, sizeof(name), "%s%d%s", stem, i, ext);
        }
        snprintf(out, outlen, "%s%s%s", dir, dir[0] && dir[strlen(dir) - 1] == '/' ? "" : "/", name);
        uint64_t sz;
        bool isdir;
        if (!fs->stat(out, &sz, &isdir)) {
            return 0;
        }
    }
    return 1;
}

int nf_folders(const char *pref, const char *hint, const char **out)
{
    int n = 0;
    if (hint && hint[0]) {
        out[n++] = hint;
    } else if (pref && pref[0]) {
        out[n++] = pref;
    }
    static const char *const media[5] = { "/usb0", "/usb1", "/usb2", "/usb3", "/sd" };
    for (int i = 0; i < 5; i++) {
        if (n == 0 || strcmp(out[0], media[i]) != 0) {
            out[n++] = media[i];
        }
    }
    return n;
}

bool nf_suggest(CoreFs *fs, const char *pref, const char *stem, const char *ext, char *out, int outlen)
{
    if (outlen <= 0) {
        return false;
    }
    out[0] = 0;
    const char *f[6];
    int n = nf_folders(pref, 0, f);
    FsEntry probe[1];
    for (int i = 0; i < n; i++) {
        // The folder asked for was listed by whoever asked (the menu's folder picker); reading all of it again to see that it can be read is
        // what made the prompt slow on a big folder. A medium tried as a fallback is probed with a listing.
        bool asked = i == 0 && pref && pref[0];
        if (!asked && fs->list(f[i], probe, 1) < 0) {
            continue;
        }
        char full[260];
        if (pick_dir_name(fs, f[i], stem, ext, full, sizeof(full)) == 0) {
            const char *b = nf_base(full);
            nf_copy(out, outlen, b, strlen(b) - strlen(ext));
            return true;
        }
    }
    snprintf(out, (size_t)outlen, "%s", stem);
    return false;
}

// Places the file by the one rule (a hinted folder, a bare name, a full path; never over an existing file) and saves its first `first` bytes.
// nf_write_new passes the whole length; the writer below passes one chunk and appends the rest.
static int place_new(CoreFs *fs, const char *pref, const char *path, const char *auto_stem, const char *ext, const uint8_t *buf, uint32_t first,
                     char *full, int fl, char *msg, int ml)
{
    bool slash = path && strchr(path, '/');
    bool dir_hint = !path || !path[0];
    if (!dir_hint && slash) {
        if (path[strlen(path) - 1] == '/') {
            dir_hint = true;
        } else {
            uint64_t sz;
            bool isd = false;
            dir_hint = fs->stat(path, &sz, &isd) && isd;
        }
    }
    uint64_t sz;
    bool isdir;
    if (!dir_hint && slash) {
        name_with_ext(path, ext, full, fl);
        if (fs->stat(full, &sz, &isdir)) {
            snprintf(msg, ml, "%s exists already; it is not overwritten.", full);
            return 1;
        }
        if (!fs->save(full, buf, first)) {
            snprintf(msg, ml, "Cannot write %s (read-only medium, full, or not allowed).", full);
            return 1;
        }
        return 0;
    }
    bool bare = path && path[0] && !slash;
    const char *f[6];
    int nf = nf_folders(pref, dir_hint ? path : 0, f);
    if (dir_hint && path && path[0]) {
        nf = 1;                                      // a folder that was named is the folder: no other medium takes the file if it fails
    }
    for (int i = 0; i < nf; i++) {
        if (bare) {
            char name[100];
            name_with_ext(path, ext, name, sizeof(name));
            snprintf(full, (size_t)fl, "%s%s%s", f[i], f[i][0] && f[i][strlen(f[i]) - 1] == '/' ? "" : "/", name);
            if (fs->stat(full, &sz, &isdir)) {
                snprintf(msg, ml, "%s exists already; it is not overwritten.", full);
                return 1;
            }
        } else if (pick_dir_name(fs, f[i], auto_stem, ext, full, fl)) {
            continue;
        }
        if (fs->save(full, buf, first)) {
            return 0;
        }
    }
    if (dir_hint && path && path[0]) {
        snprintf(msg, ml, "Cannot write into %s (read-only medium, full, or not allowed).", path);
    } else {
        snprintf(msg, ml, "No medium to write the image to.");
    }
    return 1;
}

int nf_write_new(CoreFs *fs, const char *pref, const char *path, const char *auto_stem, const char *ext, const uint8_t *buf, uint32_t n,
                 char *full, int fl, char *msg, int ml)
{
    return place_new(fs, pref, path, auto_stem, ext, buf, n, full, fl, msg, ml);
}

// ---- the same, in chunks -------------------------------------------------------------------------------------------------------------
NfWriter::NfWriter() : fs_(0), f_(0), buf_(0), n_(0), done_(0)
{
    full_[0] = 0;
}

NfWriter::~NfWriter()
{
    abort();
}

void NfWriter::abort()
{
    if (f_) {
        fs_->close(f_);
        f_ = 0;
    }
    if (fs_) {
        fs_->remove(full_);                          // a half-written image is not left behind
    }
    fs_ = 0;
}

int NfWriter::fail(const char *what, char *msg, int ml)
{
    snprintf(msg, ml, "Cannot write %s (%s).", full_, what);
    abort();
    return NF_FAIL;
}

int NfWriter::begin(CoreFs *fs, const char *pref, const char *path, const char *auto_stem, const char *ext, const uint8_t *buf, uint32_t n,
                    char *msg, int ml)
{
    abort();
    full_[0] = 0;
    uint32_t first = n > NF_CHUNK ? NF_CHUNK : n;
    if (place_new(fs, pref, path, auto_stem, ext, buf, first, full_, sizeof(full_), msg, ml)) {
        return NF_FAIL;
    }
    buf_ = buf;
    n_ = n;
    done_ = first;
    if (done_ >= n_) {
        return NF_DONE;
    }
    fs_ = fs;
    f_ = fs->open(full_, true);
    if (!f_ || !f_->writable()) {
        return fail("it cannot be opened for writing", msg, ml);
    }
    return NF_MORE;
}

int NfWriter::step(char *msg, int ml)
{
    if (!fs_ || !f_) {
        snprintf(msg, ml, "No image is being written.");
        return NF_FAIL;
    }
    uint32_t len = n_ - done_ > NF_CHUNK ? NF_CHUNK : n_ - done_;
    if (!f_->write(done_, buf_ + done_, len)) {
        return fail("read-only medium, full, or gone", msg, ml);
    }
    done_ += len;
    if (done_ < n_) {
        return NF_MORE;
    }
    bool ok = f_->sync();
    fs_->close(f_);
    f_ = 0;
    if (!ok) {
        return fail("the medium did not take the last sector", msg, ml);
    }
    fs_ = 0;
    return NF_DONE;
}

int NfWriter::percent() const
{
    return n_ ? (int)((uint64_t)done_ * 100 / n_) : 0;
}
