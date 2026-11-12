#include "configlist.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

void configlist_clear(ConfigList *l)
{
    memset(l, 0, sizeof(*l));
}

int configlist_find(const ConfigList *l, const char *name)
{
    for (int i = 0; i < l->n; i++) {
        if (!strcasecmp(l->name[i], name)) {
            return i;
        }
    }
    return -1;
}

bool configlist_add(ConfigList *l, const char *file, bool is_dir, bool legacy)
{
    const char *ext = legacy ? CONFIG_LEGACY_EXT : CONFIG_EXT;
    int el = (int)strlen(ext), fl = (int)strlen(file);
    if (is_dir || fl <= el || fl - el > CONFIG_NAME_MAX || strcasecmp(file + fl - el, ext)) {
        return false;
    }
    char name[CONFIG_NAME_MAX + 1];
    memcpy(name, file, (size_t)(fl - el));
    name[fl - el] = 0;
    if (!config_name_ok(name)) {
        return false;
    }
    int at = configlist_find(l, name);
    if (at >= 0) {
        if (l->legacy[at] && !legacy) {                  // configs/ wins over media/
            l->legacy[at] = false;
            strcpy(l->name[at], name);
        }
        return false;
    }
    if (l->n >= CONFIGS_MAX) {
        l->more++;
        return false;
    }
    strcpy(l->name[l->n], name);
    l->legacy[l->n] = legacy;
    l->n++;
    return true;
}

void configlist_sort(ConfigList *l)
{
    for (int i = 1; i < l->n; i++) {                     // at most 16 names: an insertion sort
        char nm[CONFIG_NAME_MAX + 1];
        strcpy(nm, l->name[i]);
        bool lg = l->legacy[i];
        int j = i;
        while (j > 0 && strcasecmp(l->name[j - 1], nm) > 0) {
            strcpy(l->name[j], l->name[j - 1]);
            l->legacy[j] = l->legacy[j - 1];
            j--;
        }
        strcpy(l->name[j], nm);
        l->legacy[j] = lg;
    }
}

bool config_core_dir(const char *cor_path, char *out, int outlen)
{
    out[0] = 0;
    int l = cor_path ? (int)strlen(cor_path) : 0;
    if (l < 6 || cor_path[0] != '/' || strcasecmp(cor_path + l - 4, ".cor") || l - 4 >= outlen) {
        return false;
    }
    int slash = 0;                                       // a folder below a medium: "/x.cor" has none
    for (int i = 1; i < l - 4; i++) {
        slash += cor_path[i] == '/';
    }
    if (slash == 0 || cor_path[l - 5] == '/') {
        return false;
    }
    memcpy(out, cor_path, (size_t)(l - 4));
    out[l - 4] = 0;
    return true;
}

bool config_dir_path(const char *core_dir, bool legacy, char *out, int outlen)
{
    return snprintf(out, (size_t)outlen, "%s/%s", core_dir, legacy ? CONFIG_LEGACY_DIR : CONFIG_DIR) < outlen;
}

bool config_file_path(const char *core_dir, const char *name, bool legacy, char *out, int outlen)
{
    return snprintf(out, (size_t)outlen, "%s/%s/%s%s", core_dir, legacy ? CONFIG_LEGACY_DIR : CONFIG_DIR, name,
                    legacy ? CONFIG_LEGACY_EXT : CONFIG_EXT) < outlen;
}

static bool fix_stem(const char *file, const char *ext, char *stem)
{
    int el = (int)strlen(ext), fl = (int)strlen(file);
    if (fl <= el || fl - el > CONFIG_NAME_MAX || strcasecmp(file + fl - el, ext)) {
        return false;
    }
    memcpy(stem, file, (size_t)(fl - el));
    stem[fl - el] = 0;
    return config_name_ok(stem);
}

void configlist_fix_plan(ConfigFix *f, int n, const char *(*get)(void *ctx, int i, bool *is_dir), void *ctx)
{
    f->n = 0;
    f->more = false;
    for (int i = 0; i < n; i++) {
        bool dir = false;
        const char *file = get(ctx, i, &dir);
        char stem[CONFIG_NAME_MAX + 1];
        if (!file || dir) {
            continue;
        }
        bool tmp = fix_stem(file, ".tmp", stem);
        bool bak = !tmp && fix_stem(file, ".bak", stem);
        if (!tmp && !bak) {
            continue;
        }
        bool has_cfg = false;
        for (int j = 0; bak && j < n && !has_cfg; j++) {
            bool d2 = false;
            const char *o = get(ctx, j, &d2);
            char s2[CONFIG_NAME_MAX + 1];
            has_cfg = o && !d2 && fix_stem(o, CONFIG_EXT, s2) && !strcasecmp(s2, stem);
        }
        if (f->n >= CONFIG_FIX_MAX) {
            f->more = true;
            return;
        }
        ConfigFix::Act *a = &f->act[f->n++];
        a->recover = bak && !has_cfg;
        snprintf(a->file, sizeof(a->file), "%s", file);
        snprintf(a->to, sizeof(a->to), "%s%s", stem, CONFIG_EXT);
    }
}
