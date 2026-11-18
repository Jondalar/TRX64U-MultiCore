#include "picklist.h"
#include <string.h>
#include <stdio.h>

static void put(char *dst, int n, const char *s, int len)
{
    if (len > n - 1) {
        len = n - 1;
    }
    memcpy(dst, s, (size_t)len);
    dst[len] = 0;
}

int picklist_parse(PickRows *out, const char *text)
{
    out->n = 0;
    while (text && *text && out->n < PICKLIST_MAX) {
        const char *e = strchr(text, '\n');
        int ll = e ? (int)(e - text) : (int)strlen(text);
        if (ll > 0) {
            PickRow &r = out->row[out->n];
            memset(&r, 0, sizeof(r));
            if (text[0] == '!') {
                r.note = true;
                put(r.label, sizeof(r.label), text + 1, ll - 1);
            } else {
                const char *f[3] = { text, 0, 0 };
                int fl[3] = { 0, 0, 0 };
                int k = 0;
                for (const char *q = text; q < text + ll; q++) {
                    if (*q == '\t' && k < 2) {
                        fl[k] = (int)(q - f[k]);
                        f[++k] = q + 1;
                    }
                }
                fl[k] = (int)(text + ll - f[k]);
                put(r.value, sizeof(r.value), f[0], fl[0]);
                if (k >= 1) {
                    put(r.label, sizeof(r.label), f[1], fl[1]);
                }
                if (k >= 2) {
                    put(r.right, sizeof(r.right), f[2], fl[2]);
                }
                if (!r.label[0]) {
                    snprintf(r.label, sizeof(r.label), "%s", r.value);
                }
            }
            out->n++;
        }
        text = e ? e + 1 : 0;
    }
    return out->n;
}

int picklist_find(const PickRows &p, const char *value)
{
    for (int i = 0; i < p.n; i++) {
        if (!p.row[i].note && !strcmp(p.row[i].value, value)) {
            return i;
        }
    }
    return -1;
}
