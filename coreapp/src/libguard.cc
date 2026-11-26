#include "libguard.h"
#include "bootcfg.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {
// The host's boot.cfg is at most BOOT_MAX core lines of about 250 bytes; too large for a task stack, so static.
BootCfg g_cfg;
char g_text[8192];
}

// True when `path` is certainly not there: its folder lists and holds no entry of that name (case-insensitive, as FAT), or the folder
// itself is certainly not there. Never at a volume root: "/Flash" missing from "/" means the flash disk is not mounted yet, not that
// boot.cfg is absent. A folder too large to list in full is not certain.
static bool absent_for_sure(CoreFs *fs, const char *path)
{
    static FsEntryShort ents[LIBGUARD_LIST_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return false;                    // "/x": a volume root, never certain
    }
    char dir[96];
    int dl = (int)(slash - path);
    if (dl >= (int)sizeof(dir)) {
        return false;
    }
    memcpy(dir, path, dl);
    dir[dl] = 0;
    int n = fs->list_short(dir, ents, LIBGUARD_LIST_MAX);
    if (n < 0) {
        return absent_for_sure(fs, dir); // the folder does not list: certain only if the folder itself is certainly absent
    }
    if (n >= LIBGUARD_LIST_MAX) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (strcasecmp(ents[i].name, slash + 1) == 0) {
            return false;                // listed but not statable: not absent
        }
    }
    return true;
}

// The file as the host writes it. A file that is not there may be created, but only when its absence is certain: a stat() that
// fails while the flash disk is still mounting (hostui.cc:1823) must not lead to a default BootCfg over the host's boot menu. So a
// failing stat() is "absent" only when the folder lists and the name is not in it; otherwise the file is unknown (false).
bool LibGuard::read(int *v, bool *exists)
{
    if (!path_[0]) {                     // a standalone core with no valid id has no file: the counter lives in memory
        *exists = false;
        *v = stored_;
        return true;
    }
    uint64_t size;
    bool dir;
    *exists = fs_->stat(path_, &size, &dir) && !dir;
    if (!*exists && !absent_for_sure(fs_, path_)) {
        return false;
    }
    int n = *exists ? fs_->load(path_, g_text, sizeof(g_text) - 1) : 0;
    if (*exists && n < 0) {
        return false;
    }
    g_text[n < 0 ? 0 : n] = 0;
    bootcfg_parse(&g_cfg, g_text);
    *v = g_cfg.lib_attempt;
    return true;
}

// Replaces only lib_attempt: the rest of the file goes through BootCfg as the host itself would write it (read() parsed it into g_cfg).
bool LibGuard::write(int v)
{
    if (!path_[0]) {
        stored_ = v;
        return true;
    }
    g_cfg.lib_attempt = v;
    int n = bootcfg_format(&g_cfg, g_text, sizeof(g_text));
    if (n < 0 || !fs_->save(path_, g_text, (uint32_t)n)) {
        return false;
    }
    stored_ = v;
    return true;
}

void LibGuard::begin(bool held, LibGuardPlan *out)
{
    memset(out, 0, sizeof(*out));
    out->armed = true;
    int v = 0;
    bool exists = false;
    known_ = read(&v, &exists);
    if (known_) {
        stored_ = v;
    }
    if (held) {
        out->skip = LIBSKIP_KEY;
        snprintf(out->why, sizeof(out->why), "The libraries were not loaded: the Commodore key was held while the core started.");
        return;
    }
    if (known_ && v > 0) {
        out->skip = LIBSKIP_COUNTER;
        snprintf(out->why, sizeof(out->why),
                 "The libraries were not loaded: the previous start did not finish, a library may have crashed it. They load again at the next start.");
        return;
    }
    if (!known_) {                       // unreadable, or not there without certainty (still mounting): never overwritten, the host's boot menu lives in it
        out->armed = false;
        snprintf(out->why, sizeof(out->why), "The crash guard could not read %s: libraries load without it.", path_);
        return;
    }
    out->armed = false;                  // armed by arm(), just before the first library loads
}

// Sets the counter to 1, only once a library is about to load: a start without libraries writes no flash.
void LibGuard::arm(LibGuardPlan *out)
{
    if (!known_ || out->skip || out->armed) {
        return;
    }
    if (!write(1)) {
        snprintf(out->why, sizeof(out->why), "The crash guard could not write %s: libraries load without it.", path_);
        return;
    }
    out->armed = true;
}

bool LibGuard::finished()
{
    if (!known_ || stored_ == 0) {
        return true;
    }
    int v = 0;
    bool exists = false;
    if (!read(&v, &exists)) {            // read again: the file is parsed into g_cfg for write(), and may have changed during a long start
        return false;
    }
    stored_ = v;
    return v == 0 || write(0);
}
