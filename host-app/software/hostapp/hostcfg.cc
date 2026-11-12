// T163 D5: the host application's settings store, see hostcfg.h.
#include "hostcfg.h"
#include "cfgfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SECTION_MAX 2048

static const HostCfgIo *io = NULL;
static char text[HOSTCFG_MAX];          // the file as it is on the flash disk (plus what this boot added)
static int  text_len = 0;
static bool loaded = false;             // the file has been read (once per boot)
static bool is_new = false;             // the file did not exist when it was read: stores registering in this boot are taken over
static bool unavailable = false;        // the file could not be read although it may exist: nothing is taken over, nothing is written
static char sec[SECTION_MAX];

static const ConfigBackend backend = { hostcfg_load, hostcfg_save };

void hostcfg_install(const HostCfgIo *i)
{
    io = i;
    loaded = is_new = unavailable = false;
    text_len = 0;
    text[0] = 0;
    config_set_backend(&backend);
}

static void ensure_loaded(void)
{
    if (loaded) {
        return;
    }
    loaded = true;
    int n = io ? io->read(text, HOSTCFG_MAX - 1) : -2;
    if (n == -1) {
        is_new = true;
        text_len = 0;
    } else if (n < 0) {
        unavailable = true;
        text_len = 0;
        printf("host.cfg: cannot be read, settings stay at their defaults and nothing is written\n");
    } else {
        text_len = n;
    }
    text[text_len] = 0;
}

// The section "[name]" in text[0..len): its first byte (the '[') and the byte after its last line.
static bool find_section(const char *t, int len, const char *name, int *start, int *end)
{
    int p = 0;
    bool in = false;
    while (p < len) {
        int e = p;
        while (e < len && t[e] != '\n') {
            e++;
        }
        int next = (e < len) ? e + 1 : e;
        if (t[p] == '[') {
            if (in) {
                *end = p;
                return true;
            }
            char head[96];
            int l = e - p;
            while (l > 0 && (t[p + l - 1] == '\r' || t[p + l - 1] == ' ')) {
                l--;
            }
            if (l >= 2 && l < (int)sizeof(head) && t[p + l - 1] == ']') {
                memcpy(head, t + p + 1, l - 2);
                head[l - 2] = 0;
                if (!strcasecmp(head, name)) {
                    *start = p;
                    in = true;
                }
            }
        }
        p = next;
    }
    if (in) {
        *end = len;
    }
    return in;
}

static void apply_item(void *user, const char *, const char *key, const char *value)
{
    ConfigStore *s = (ConfigStore *)user;
    ConfigItem *it = s->find_item(key);
    if (!it) {
        return;
    }
    const t_cfg_definition *d = it->definition;
    switch (d->type) {
        case CFG_TYPE_ENUM:
            for (int k = d->min; k <= d->max; k++) {
                if (d->items[k] && cfgfile_label_equal(d->items[k], value)) {
                    it->setValueQuietly(k);
                    break;
                }
            }
            break;
        case CFG_TYPE_VALUE: {
            char *e = NULL;
            long v = strtol(value, &e, 10);
            if (e != value && v >= d->min && v <= d->max) {
                it->setValueQuietly((int)v);
            }
            break;
        }
        case CFG_TYPE_STRING:
        case CFG_TYPE_STRFUNC:
        case CFG_TYPE_STRPASS:
            it->setString(value);
            break;
        default:
            break;
    }
}

int hostcfg_load(ConfigStore *s)
{
    ensure_loaded();
    if (unavailable) {
        return CFG_LOAD_DEFAULT;
    }
    int a, b;
    if (find_section(text, text_len, s->get_store_name(), &a, &b)) {
        cfgfile_parse(text + a, b - a, apply_item, s);
        printf("host.cfg: '%s' read from the file\n", s->get_store_name());
        return CFG_LOAD_FILE;
    }
    printf("host.cfg: '%s' %s\n", s->get_store_name(), is_new ? "is not in the file, the file is new: taken over once from the flash page (read only)" : "is not in the file: defaults");
    return is_new ? CFG_LOAD_TAKEOVER : CFG_LOAD_DEFAULT;
}

static void clean(char *v)
{
    for (; *v; v++) {
        if (*v == '\n' || *v == '\r') {
            *v = ' ';
        }
    }
}

void hostcfg_save(ConfigStore *s)
{
    ensure_loaded();
    if (unavailable || !io) {
        return;
    }
    int n = 0;
    if (!cfgfile_begin(sec, SECTION_MAX, &n, s->get_store_name())) {
        return;
    }
    for (int i = 0; i < s->items.get_elements(); i++) {
        ConfigItem *it = s->items[i];
        const t_cfg_definition *d = it->definition;
        char v[64];
        bool ok = true;
        switch (d->type) {
            case CFG_TYPE_ENUM: {
                int k = it->getSavedValue();
                if (k < d->min || k > d->max || !d->items[k]) {
                    continue;
                }
                snprintf(v, sizeof(v), "%s", d->items[k]);
                ok = cfgfile_item(sec, SECTION_MAX, &n, d->item_text, v);
                break;
            }
            case CFG_TYPE_VALUE:
                snprintf(v, sizeof(v), "%d", it->getSavedValue());
                ok = cfgfile_item(sec, SECTION_MAX, &n, d->item_text, v);
                break;
            case CFG_TYPE_STRING:
            case CFG_TYPE_STRFUNC:
            case CFG_TYPE_STRPASS: {
                char str[200];
                snprintf(str, sizeof(str), "%s", it->getString() ? it->getString() : "");
                clean(str);
                ok = cfgfile_item(sec, SECTION_MAX, &n, d->item_text, str);
                break;
            }
            default:
                break;                          // separators, functions, info lines: nothing to keep
        }
        if (!ok) {
            printf("host.cfg: section '%s' does not fit, not saved\n", s->get_store_name());
            return;
        }
    }
    cfgfile_end(sec, SECTION_MAX, &n);

    int a, b;
    if (find_section(text, text_len, s->get_store_name(), &a, &b)) {
        if (text_len - (b - a) + n >= HOSTCFG_MAX) {
            printf("host.cfg: full, section '%s' not saved\n", s->get_store_name());
            return;
        }
        memmove(text + a + n, text + b, text_len - b);
        memcpy(text + a, sec, n);
        text_len += n - (b - a);
    } else {
        if (text_len + 1 + n >= HOSTCFG_MAX) {
            printf("host.cfg: full, section '%s' not saved\n", s->get_store_name());
            return;
        }
        if (text_len > 0 && text[text_len - 1] != '\n') {
            text[text_len++] = '\n';
        }
        memcpy(text + text_len, sec, n);
        text_len += n;
    }
    text[text_len] = 0;
    if (!io->write(text, text_len)) {
        printf("host.cfg: write failed\n");
    } else {
        printf("host.cfg: '%s' saved, %d bytes in the file\n", s->get_store_name(), text_len);
    }
}
