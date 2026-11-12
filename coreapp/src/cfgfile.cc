#include "cfgfile.h"
#include <string.h>
#include <strings.h>

static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int len_without_trailing_space(const char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        n--;
    }
    return n;
}

bool cfgfile_label_equal(const char *a, const char *b)
{
    a = skip_space(a);
    b = skip_space(b);
    int la = len_without_trailing_space(a), lb = len_without_trailing_space(b);
    return la == lb && strncasecmp(a, b, la) == 0;
}

static int head_len(const char *s)
{
    int n = len_without_trailing_space(s);
    if (n > 0 && s[n - 1] == ')') {
        for (int i = n - 1; i > 0; i--) {
            if (s[i] == '(' && s[i - 1] == ' ') {
                n = i - 1;
                while (n > 0 && s[n - 1] == ' ') {
                    n--;
                }
                break;
            }
        }
    }
    return n;
}

bool cfgfile_label_head_equal(const char *a, const char *b)
{
    a = skip_space(a);
    b = skip_space(b);
    int la = head_len(a), lb = head_len(b);
    return la > 0 && la == lb && strncasecmp(a, b, la) == 0;
}

int cfgfile_parse(const char *text, int len, cfg_item_cb item, void *user)
{
    char line[256], store[64];
    int n = 0, p = 0;
    bool in_store = false;
    while (p < len) {
        int l = 0;
        bool too_long = false;
        while (p < len && text[p] != '\n') {
            if (text[p] != '\r') {
                if (l < (int)sizeof(line) - 1) {
                    line[l++] = text[p];
                } else {
                    too_long = true;
                }
            }
            p++;
        }
        if (p < len) {
            p++;                                   // the newline
        }
        line[l] = 0;
        if (too_long || line[0] == '#' || line[0] == ';' || !line[0]) {
            continue;
        }
        if (line[0] == '[') {
            char *e = strchr(line, ']');
            in_store = false;
            if (e) {
                *e = 0;
                if ((int)strlen(line + 1) < (int)sizeof(store)) {
                    strcpy(store, line + 1);
                    in_store = true;
                }
            }
            continue;
        }
        if (!in_store) {
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = 0;
        const char *key = skip_space(line);
        line[len_without_trailing_space(line)] = 0;        // "Item = value" finds the same item as "Item=value" (Gideon's reader)
        const char *v = skip_space(eq + 1);
        int vl = len_without_trailing_space(v);
        char val[200];
        if (vl >= (int)sizeof(val)) {
            continue;
        }
        memcpy(val, v, vl);
        val[vl] = 0;
        item(user, store, key, val);
        n++;
    }
    return n;
}

static bool put(char *out, int max, int *pos, const char *s)
{
    int l = (int)strlen(s);
    if (*pos + l >= max) {
        return false;
    }
    memcpy(out + *pos, s, l + 1);
    *pos += l;
    return true;
}

bool cfgfile_begin(char *out, int max, int *pos, const char *store)
{
    return put(out, max, pos, "[") && put(out, max, pos, store) && put(out, max, pos, "]\n");
}

bool cfgfile_item(char *out, int max, int *pos, const char *key, const char *value)
{
    return put(out, max, pos, key) && put(out, max, pos, "=") && put(out, max, pos, value) && put(out, max, pos, "\n");
}

bool cfgfile_end(char *out, int max, int *pos)
{
    return put(out, max, pos, "\n");
}
