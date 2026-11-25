#include "picklist.h"
#include "listgrow.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void put(char *dst, int n, const char *s, int len)
{
    if (len > n - 1) {
        len = n - 1;
    }
    memcpy(dst, s, (size_t)len);
    dst[len] = 0;
}

void picklist_init(PickRows *p)
{
    memset(p, 0, sizeof(*p));
}

void picklist_free(PickRows *p)
{
    free(p->text);
    free(p->off);
    picklist_init(p);
}

int picklist_count(const char *text)
{
    int n = 0;
    while (text && *text) {
        const char *e = strchr(text, '\n');
        if ((e ? e : text + strlen(text)) > text) {
            n++;
        }
        text = e ? e + 1 : 0;
    }
    return n;
}

int picklist_parse(PickRows *p, const char *text)
{
    picklist_free(p);
    if (!text || !*text) {
        return 0;
    }
    const int want = (int)strlen(text);
    const int all = want + (int)sizeof(int) * picklist_count(text);   // the text and the offset of every non-empty line (what the two mallocs hold)
    const int take = ListGrow::fit(all, 1);                      // bytes: a quarter of the heap that is free now at most, for both
    int len = want;
    if (take < all) {                                            // cut at the end of a line: never half a row; every row costs its text and its offset
        int cost = 0;
        len = 0;
        for (const char *q = text; *q;) {
            const char *e = strchr(q, '\n');
            const int l = (int)((e ? e : q + strlen(q)) - q);
            const int step = l + (e ? 1 : 0);
            const int c = step + (l > 0 ? (int)sizeof(int) : 0);
            if (cost + c > take) {
                break;
            }
            cost += c;
            len += step;
            q += step;
        }
        p->cut = len < want;
    }
    if (len <= 0) {
        return 0;
    }
    char *t = (char *)malloc((size_t)len + 1);
    if (!t) {
        p->cut = true;
        return 0;
    }
    memcpy(t, text, (size_t)len);
    t[len] = 0;
    int lines = 0;
    for (const char *q = t; q && *q;) {
        const char *e = strchr(q, '\n');
        if ((e ? e : q + strlen(q)) > q) {
            lines++;
        }
        q = e ? e + 1 : 0;
    }
    int *off = (int *)malloc(sizeof(int) * (size_t)(lines ? lines : 1));
    if (!off) {
        free(t);
        p->cut = true;
        return 0;
    }
    int k = 0;
    for (const char *q = t; q && *q;) {
        const char *e = strchr(q, '\n');
        if ((e ? e : q + strlen(q)) > q) {
            off[k++] = (int)(q - t);
        }
        q = e ? e + 1 : 0;
    }
    p->text = t;
    p->off = off;
    p->n = lines;
    p->cap = lines;
    p->len = len;
    return lines;
}

bool picklist_same(const PickRows &p, const char *text)
{
    if (!text || !*text) {
        return p.n == 0 && !p.cut;
    }
    if (!p.text) {
        return false;
    }
    if (p.cut) {
        return strncmp(p.text, text, (size_t)p.len) == 0 && (int)strlen(text) > p.len;
    }
    return strcmp(p.text, text) == 0;
}

void picklist_get(const PickRows &p, int i, PickRow *out)
{
    memset(out, 0, sizeof(*out));
    out->tone = -1;
    if (i < 0 || i >= p.n) {
        return;
    }
    const char *text = p.text + p.off[i];
    const char *e = strchr(text, '\n');
    const int ll = e ? (int)(e - text) : (int)strlen(text);
    if (text[0] == '!') {
        out->note = true;
        put(out->label, sizeof(out->label), text + 1, ll - 1);
        return;
    }
    if (text[0] == '+') {
        out->more = true;
        put(out->label, sizeof(out->label), text + 1, ll - 1);
        if (!out->label[0]) {
            snprintf(out->label, sizeof(out->label), "More");
        }
        return;
    }
    const char *f[3 + 1 + PICKLIST_SIDE_MAX];
    int fl[3 + 1 + PICKLIST_SIDE_MAX];
    int k = 0;
    f[0] = text;
    for (const char *q = text; q < text + ll; q++) {
        if (*q == '\t' && k < 3 + PICKLIST_SIDE_MAX) {
            fl[k] = (int)(q - f[k]);
            f[++k] = q + 1;
        }
    }
    fl[k] = (int)(text + ll - f[k]);
    put(out->value, sizeof(out->value), f[0], fl[0]);
    if (k >= 1) {
        put(out->label, sizeof(out->label), f[1], fl[1]);
    }
    if (k >= 2) {
        put(out->right, sizeof(out->right), f[2], fl[2]);
    }
    if (k >= 3) {
        static const char *const tones[] = { "muted", "cyan", "violet", "amber", "coral" };
        for (int t = 0; t < 5; t++) {
            if ((int)strlen(tones[t]) == fl[3] && !strncmp(f[3], tones[t], (size_t)fl[3])) {
                out->tone = t;
            }
        }
    }
    for (int s = 4; s <= k; s++) {
        if (fl[s] > 0 && out->nside < PICKLIST_SIDE_MAX) {
            put(out->side[out->nside++], sizeof(out->side[0]), f[s], fl[s]);
        }
    }
    if (!out->label[0]) {
        snprintf(out->label, sizeof(out->label), "%s", out->value);
    }
}

int picklist_find(const PickRows &p, const char *value)
{
    for (int i = 0; i < p.n; i++) {
        PickRow r;
        picklist_get(p, i, &r);
        if (!r.note && !r.more && !strcmp(r.value, value)) {
            return i;
        }
    }
    return -1;
}
