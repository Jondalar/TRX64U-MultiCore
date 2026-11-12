#include "mediumwait.h"
#include "corefolder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool usb_root(const char *path, char *root, int rootlen)
{
    if (!path || path[0] != '/' || strncasecmp(path + 1, "usb", 3) != 0 || path[4] < '0' || path[4] > '9') {
        return false;
    }
    int n = 4;
    while (path[n + 1] >= '0' && path[n + 1] <= '9') {
        n++;
    }
    n++;                                                   // the digits end here: "/Usb0" is n characters
    if (n + 1 > rootlen || (path[n] && path[n] != '/')) {
        return false;
    }
    memcpy(root, path, (size_t)n);
    root[n] = 0;
    return true;
}

void medium_wait(CoreFs *fs, const char *path, MediumEnv *env, uint32_t limit_ms, MediumWait *out)
{
    out->present = true;
    out->waited = false;
    out->ms = 0;
    char root[16];
    FsEntry probe[1];
    if (!fs || !usb_root(path, root, sizeof(root))) {
        return;                                            // /SD and /flash are there when the application is
    }
    if (fs->list(root, probe, 1) >= 0) {
        return;
    }
    out->present = false;
    out->waited = true;
    uint32_t t0 = env->now_ms();
    for (;;) {
        out->ms = env->now_ms() - t0;
        if (fs->list(root, probe, 1) >= 0) {
            out->present = true;
            break;
        }
        if (out->ms >= limit_ms) {
            break;
        }
        env->delay_ms(100);
    }
}

void start_core_folder(CoreCtx *c, MediumEnv *env)
{
    if (!c->cor_path[0]) {
        return;
    }
    MediumWait w;
    medium_wait(c->fs, c->cor_path, env, START_MEDIUM_WAIT_MS, &w);
    if (w.waited) {
        printf("[core] medium of %s: %s after %u ms\n", c->cor_path, w.present ? "up" : "not seen", (unsigned)w.ms);
    }
    corefolder_ensure(c->fs, c->cor_path);
    corefolder_move_cfg(c->fs, c->cor_path);
}
