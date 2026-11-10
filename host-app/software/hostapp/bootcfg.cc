#include "bootcfg.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void bootcfg_init(BootCfg *c)
{
    memset(c, 0, sizeof(*c));
    c->countdown = BOOT_COUNTDOWN_DEFAULT;
    c->autostart_s = BOOT_COUNTDOWN_DEFAULT;
    c->direct = 1;
    c->vid_sys = -1;
    c->vid_hdmi = -1;
    c->lib_attempt = 0;
    c->def = -1;
}

static void copy(char *dst, int dl, const char *src, int n)
{
    if (n > dl - 1) {
        n = dl - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

void bootcfg_parse(BootCfg *c, const char *text)
{
    bootcfg_init(c);
    bool have_as = false;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        int l = len;
        while (l > 0 && (p[l - 1] == '\r' || p[l - 1] == ' ')) {
            l--;
        }
        if (l > 0 && p[0] != '#') {
            if (!strncmp(p, "countdown=", 10)) {
                int v = atoi(p + 10);
                c->countdown = (v < 0) ? 0 : (v > 60 ? 60 : v);
            } else if (!strncmp(p, "autostart_s=", 12)) {
                int v = atoi(p + 12);
                c->autostart_s = (v < 1) ? 1 : (v > BOOT_AUTOSTART_MAX ? BOOT_AUTOSTART_MAX : v);
                have_as = true;
            } else if (!strncmp(p, "direct=", 7)) {
                c->direct = atoi(p + 7) != 0;
            } else if (!strncmp(p, "video=", 6)) {
                const char *v = p + 6;
                int sys = atoi(v);
                const char *comma = strchr(v, ',');
                int hdmi = comma ? atoi(comma + 1) : -1;
                c->vid_sys = (sys < 0 || sys > 5) ? -1 : sys;
                c->vid_hdmi = (hdmi < 0 || hdmi >= 6) ? -1 : hdmi;
            } else if (!strncmp(p, "lib_attempt=", 12)) {
                int v = atoi(p + 12);
                c->lib_attempt = (v < 0) ? 0 : (v > 99 ? 99 : v);
            } else if (!strncmp(p, "default=", 8)) {
                c->def = atoi(p + 8);
            } else if (!strncmp(p, "core=", 5) && c->n < BOOT_MAX) {
                const char *a = p + 5, *end = p + l;
                const char *b1 = (const char *)memchr(a, '|', end - a);
                const char *b2 = b1 ? (const char *)memchr(b1 + 1, '|', end - b1 - 1) : 0;
                const char *b3 = b2 ? (const char *)memchr(b2 + 1, '|', end - b2 - 1) : 0;
                const char *b4 = b3 ? (const char *)memchr(b3 + 1, '|', end - b3 - 1) : 0;
                const char *b5 = b4 ? (const char *)memchr(b4 + 1, '|', end - b4 - 1) : 0;
                if (b1 && b2) {
                    BootEntry *be = &c->e[c->n++];
                    copy(be->path, sizeof(be->path), a, b1 - a);
                    copy(be->name, sizeof(be->name), b1 + 1, b2 - b1 - 1);
                    copy(be->hash, sizeof(be->hash), b2 + 1, (b3 ? b3 : end) - b2 - 1);
                    be->size = b3 ? (unsigned)strtoul(b3 + 1, 0, 10) : 0;
                    be->mtime = b4 ? (unsigned)strtoul(b4 + 1, 0, 10) : 0;      // absent = an entry from before the start check (T101 S10)
                    be->hdr = b5 ? (unsigned)strtoul(b5 + 1, 0, 16) : 0;
                }
            }
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    if (c->def >= c->n) {
        c->def = -1;
    }
    if (c->countdown > 0) {
        c->autostart_s = c->countdown;               // the running value wins over a stale kept one
    } else if (!have_as) {
        c->autostart_s = BOOT_COUNTDOWN_DEFAULT;     // an older file: autostart was off, the seconds were not kept
    }
}

int bootcfg_format(const BootCfg *c, char *out, int max)
{
    int n = snprintf(out, max, "# TRX64 boot menu\ncountdown=%d\nautostart_s=%d\ndirect=%d\ndefault=%d\nvideo=%d,%d\n",
                     c->countdown, c->autostart_s, c->direct ? 1 : 0, c->def, c->vid_sys, c->vid_hdmi);
    if (c->lib_attempt > 0 && n >= 0 && n < max) {
        n += snprintf(out + n, max - n, "lib_attempt=%d\n", c->lib_attempt);
    }
    for (int i = 0; i < c->n && n >= 0 && n < max; i++) {
        n += snprintf(out + n, max - n, "core=%s|%s|%s|%u|%u|%08x\n", c->e[i].path, c->e[i].name, c->e[i].hash, c->e[i].size,
                       c->e[i].mtime, c->e[i].hdr);
    }
    return (n < 0 || n >= max) ? -1 : n;
}

bool bootcfg_add(BootCfg *c, const char *name, const char *path, const char *hash, unsigned size, unsigned mtime, unsigned hdr)
{
    for (int i = 0; i < c->n; i++) {
        if (!strcmp(c->e[i].path, path)) {           // the same bundle again: refresh its name and hash
            copy(c->e[i].name, sizeof(c->e[i].name), name, strlen(name));
            copy(c->e[i].hash, sizeof(c->e[i].hash), hash, strlen(hash));
            c->e[i].size = size;
            c->e[i].mtime = mtime;
            c->e[i].hdr = hdr;
            return true;
        }
    }
    if (c->n >= BOOT_MAX || strchr(path, '|') || strchr(name, '|') || strchr(name, '\n')) {
        return false;
    }
    BootEntry *b = &c->e[c->n++];
    copy(b->name, sizeof(b->name), name, strlen(name));
    copy(b->path, sizeof(b->path), path, strlen(path));
    copy(b->hash, sizeof(b->hash), hash, strlen(hash));
    b->size = size;
    b->mtime = mtime;
    b->hdr = hdr;
    if (c->def < 0) {
        c->def = c->n - 1;
    }
    return true;
}

void bootcfg_remove(BootCfg *c, int i)
{
    if (i < 0 || i >= c->n) {
        return;
    }
    for (int k = i; k + 1 < c->n; k++) {
        c->e[k] = c->e[k + 1];
    }
    c->n--;
    if (c->def == i) {
        c->def = c->n ? 0 : -1;
    } else if (c->def > i) {
        c->def--;
    }
}

void bootcfg_move(BootCfg *c, int i, int dir)
{
    int j = i + dir;
    if (i < 0 || i >= c->n || j < 0 || j >= c->n) {
        return;
    }
    BootEntry t = c->e[i];
    c->e[i] = c->e[j];
    c->e[j] = t;
    if (c->def == i) {
        c->def = j;
    } else if (c->def == j) {
        c->def = i;
    }
}

bool bootcfg_rename(BootCfg *c, int i, const char *name)
{
    if (i < 0 || i >= c->n || !name) {
        return false;
    }
    size_t l = strlen(name);
    bool blank = true;
    for (size_t k = 0; k < l; k++) {
        if (name[k] == '|' || name[k] == '\n' || name[k] == '\r') {
            return false;
        }
        if (name[k] != ' ') {
            blank = false;
        }
    }
    if (blank || l > BOOT_NAME_MAX) {
        return false;
    }
    copy(c->e[i].name, sizeof(c->e[i].name), name, l);
    return true;
}

void bootcfg_autostart(BootCfg *c, bool on, int secs)
{
    if (secs > 0) {
        c->autostart_s = secs > BOOT_AUTOSTART_MAX ? BOOT_AUTOSTART_MAX : secs;
    }
    c->countdown = on ? c->autostart_s : 0;
}
