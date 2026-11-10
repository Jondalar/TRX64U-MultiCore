#include "corefolder.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Splits "<dir>/<name>.cor": *dirlen = length of <dir>, name = the file name without the extension.
static bool split(const char *cor_path, int *dirlen, const char **name, int *namelen)
{
    if (!cor_path || cor_path[0] != '/') {
        return false;
    }
    const char *slash = strrchr(cor_path, '/');
    int l = (int)strlen(cor_path);
    // the .cor has to lie below a medium: "/x.cor" has no folder to put anything beside
    if (slash == cor_path || l < 5 || strcasecmp(cor_path + l - 4, ".cor") != 0 || l - 4 <= (int)(slash - cor_path) + 1) {
        return false;
    }
    *dirlen = (int)(slash - cor_path);
    *name = slash + 1;
    *namelen = (int)(cor_path + l - 4 - (slash + 1));
    return true;
}

static bool build(const char *cor_path, const char *sub, const char *file, bool own_name, char *out, int outlen)
{
    int dl, nl;
    const char *name;
    out[0] = 0;
    if (!split(cor_path, &dl, &name, &nl)) {
        return false;
    }
    char tmp[300];
    int n = 0;
    if (dl + 1 + nl >= (int)sizeof(tmp) - 1) {
        return false;
    }
    memcpy(tmp, cor_path, (size_t)dl);
    n = dl;
    tmp[n++] = '/';
    if (own_name) {
        memcpy(tmp + n, name, (size_t)nl);
        n += nl;
    }
    tmp[n] = 0;
    if (sub) {
        if (own_name) {
            strncat(tmp, "/", sizeof(tmp) - strlen(tmp) - 1);
        }
        strncat(tmp, sub, sizeof(tmp) - strlen(tmp) - 1);
    }
    if (file) {
        if (sub || own_name) {
            strncat(tmp, "/", sizeof(tmp) - strlen(tmp) - 1);
        }
        strncat(tmp, file, sizeof(tmp) - strlen(tmp) - 1);
    }
    if ((int)strlen(tmp) >= outlen) {
        return false;
    }
    strcpy(out, tmp);
    return true;
}

bool corefolder_dir(const char *cor_path, char *out, int outlen)
{
    return build(cor_path, 0, 0, true, out, outlen);
}

bool corefolder_sub(const char *cor_path, const char *sub, char *out, int outlen)
{
    return build(cor_path, sub, 0, true, out, outlen);
}

bool corefolder_cfg(const char *cor_path, char *out, int outlen)
{
    int dl, nl;
    const char *name;
    char file[100];
    out[0] = 0;
    if (!split(cor_path, &dl, &name, &nl) || nl + 5 > (int)sizeof(file)) {
        return false;
    }
    memcpy(file, name, (size_t)nl);
    strcpy(file + nl, ".cfg");
    return build(cor_path, 0, file, true, out, outlen);
}

bool corefolder_old_cfg(const char *cor_path, char *out, int outlen)
{
    int dl, nl;
    const char *name;
    char file[100];
    out[0] = 0;
    if (!split(cor_path, &dl, &name, &nl) || nl + 5 > (int)sizeof(file)) {
        return false;
    }
    memcpy(file, name, (size_t)nl);
    strcpy(file + nl, ".cfg");
    return build(cor_path, 0, file, false, out, outlen);
}

bool corefolder_old_roms(const char *cor_path, char *out, int outlen)
{
    return build(cor_path, "roms", 0, false, out, outlen);
}

bool corefolder_save_path(const char *cor_path, const char *file, char *out, int outlen)
{
    out[0] = 0;
    if (!file || !file[0] || strchr(file, '/')) {
        return false;
    }
    return build(cor_path, "saves", file, true, out, outlen);
}

bool corefolder_ensure(CoreFs *fs, const char *cor_path)
{
    char d[260], r[260], s[260];
    if (!fs || !corefolder_dir(cor_path, d, sizeof(d)) || !corefolder_sub(cor_path, "roms", r, sizeof(r)) ||
        !corefolder_sub(cor_path, "saves", s, sizeof(s))) {
        return false;
    }
    bool ok = fs->mkdir(d);
    ok = ok && fs->mkdir(r);
    ok = ok && fs->mkdir(s);
    if (!ok) {
        printf("[core] could not make the core folder %s (read only medium?)\n", d);
    }
    return ok;
}

bool corefolder_move_cfg(CoreFs *fs, const char *cor_path)
{
    char oldp[260], newp[260];
    if (!fs || !corefolder_old_cfg(cor_path, oldp, sizeof(oldp)) || !corefolder_cfg(cor_path, newp, sizeof(newp))) {
        return false;
    }
    uint64_t size;
    bool dir;
    if (fs->stat(newp, &size, &dir) || !fs->stat(oldp, &size, &dir) || dir || size > 4096) {
        return false;
    }
    static uint8_t a[4096], b[4096];
    int got = fs->load(oldp, a, sizeof(a));
    if (got < 0 || !fs->save(newp, a, (uint32_t)got) || fs->load(newp, b, sizeof(b)) != got || memcmp(a, b, (size_t)got) != 0) {
        fs->remove(newp);
        printf("[core] settings: could not move %s to %s\n", oldp, newp);
        return false;
    }
    fs->remove(oldp);
    printf("[core] settings: moved %s to %s\n", oldp, newp);
    return true;
}

static void digest(const uint8_t *p, uint32_t n, uint8_t out[32])
{
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, p, n);
    sha256_final(&c, out);
}

bool corefolder_copy_verified(CoreFs *fs, const char *src, const char *dst, uint8_t *buf, uint32_t max)
{
    int got = fs->load(src, buf, max);
    if (got < 0) {
        return false;
    }
    uint8_t want[32], have[32];
    digest(buf, (uint32_t)got, want);
    if (!fs->save(dst, buf, (uint32_t)got)) {
        return false;
    }
    int back = fs->load(dst, buf, max);
    if (back == got) {
        digest(buf, (uint32_t)back, have);
    }
    if (back != got || memcmp(want, have, 32) != 0) {
        fs->remove(dst);
        printf("[core] ROM copy of %s to %s did not verify, removed\n", src, dst);
        return false;
    }
    const char *name = strrchr(dst, '/');
    printf("[core] ROM %s: copied from %s to %s\n", name ? name + 1 : dst, src, dst);
    return true;
}
