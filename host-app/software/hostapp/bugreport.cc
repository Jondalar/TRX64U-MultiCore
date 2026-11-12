#include "bugreport.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define CUT_MARK "\n[report cut: it did not fit]\n"

void br_init(BrOut *o, char *buf, int cap)
{
    o->buf = buf;
    o->cap = cap;
    o->n = 0;
    o->cut = false;
    if (cap > 0) {
        buf[0] = 0;
    }
}

// room is kept for the cut marker, so a report that does not fit still ends with a line that says so
static int room(const BrOut *o)
{
    return o->cap - o->n - (int)sizeof(CUT_MARK);
}

static void put_raw(BrOut *o, const char *s, int len)
{
    if (o->cut) {
        return;
    }
    if (len > room(o)) {
        o->cut = true;
        return;
    }
    memcpy(o->buf + o->n, s, len);
    o->n += len;
    o->buf[o->n] = 0;
}

void br_put(BrOut *o, const char *fmt, ...)
{
    char t[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t, sizeof(t), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    put_raw(o, t, n >= (int)sizeof(t) ? (int)sizeof(t) - 1 : n);
}

void br_section(BrOut *o, const char *title)
{
    br_put(o, "\n== %s ==\n", title);
}

static bool has_ci(const char *s, const char *sub)
{
    int n = (int)strlen(sub);
    for (; *s; s++) {
        int i = 0;
        while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)sub[i])) {
            i++;
        }
        if (i == n) {
            return true;
        }
    }
    return false;
}

bool br_secret_name(const char *name)
{
    static const char *const words[] = { "pass", "pwd", "psk", "secret", "token", "wpa", "credential" };
    if (!name) {
        return false;
    }
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (has_ci(name, words[i])) {
            return true;
        }
    }
    // "network key", "wifi_key", "apikey", "key": a name that ENDS in key (so "Keyboard Layout" and "Key Repeat" are not secrets)
    int n = (int)strlen(name);
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '=')) {
        n--;
    }
    return n >= 3 && !strncasecmp(name + n - 3, "key", 3);
}

bool br_secret_word(const char *line)
{
    static const char *const words[] = { "password", "passwd", "passphrase", "pass=", "pwd", "psk", "secret", "token", "network key", "wifi key", "api key", "apikey" };
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (has_ci(line, words[i])) {
            return true;
        }
    }
    return false;
}

int br_url_scrub(const char *in, char *out, int max)
{
    int o = 0;
    for (const char *p = in; *p && o < max - 1;) {
        const char *sch = strstr(p, "://");
        if (!sch) {
            while (*p && o < max - 1) {
                out[o++] = *p++;
            }
            break;
        }
        while (p < sch + 3 && o < max - 1) {
            out[o++] = *p++;
        }
        // the authority ends at '/', white space or the end; user information is whatever stands before an '@' inside it
        const char *q = p;
        const char *at = 0;
        while (*q && *q != '/' && *q != ' ' && *q != '\t' && *q != '"' && *q != '\'') {
            if (*q == '@') {
                at = q;
            }
            q++;
        }
        if (at) {
            const char *stars = "***";
            while (*stars && o < max - 1) {
                out[o++] = *stars++;
            }
            p = at;
        }
    }
    out[o] = 0;
    return o;
}

void br_kv(BrOut *o, const char *key, const char *fmt, ...)
{
    char v[400], s[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v, sizeof(v), fmt, ap);
    va_end(ap);
    if (br_secret_name(key)) {
        br_put(o, "%s: %s\n", key, BR_WITHHELD);
        return;
    }
    br_url_scrub(v, s, sizeof(s));
    br_put(o, "%s: %s\n", key, s);
}

static void text_line(BrOut *o, const char *line, int len)
{
    char l[400], s[400];
    if (len > (int)sizeof(l) - 1) {
        len = (int)sizeof(l) - 1;
    }
    memcpy(l, line, len);
    l[len] = 0;
    char *eq = strchr(l, '=');
    if (eq) {
        char key[96];
        int kl = (int)(eq - l);
        if (kl > (int)sizeof(key) - 1) {
            kl = (int)sizeof(key) - 1;
        }
        memcpy(key, l, kl);
        key[kl] = 0;
        if (br_secret_name(key)) {
            br_put(o, "%s=%s\n", key, BR_WITHHELD);
            return;
        }
    }
    br_url_scrub(l, s, sizeof(s));
    br_put(o, "%s\n", s);
}

void br_text(BrOut *o, const char *text, int len)
{
    int i = 0;
    while (i < len && text[i]) {
        int e = i;
        while (e < len && text[e] && text[e] != '\n') {
            e++;
        }
        if (e > i) {
            text_line(o, text + i, e - i);
        }
        i = e + 1;
    }
}

void br_log(BrOut *o, const char *log, int len)
{
    // the byte limit first, then the line limit, both counted from the end
    int start = len > BR_LOG_BYTES ? len - BR_LOG_BYTES : 0;
    int end = len;
    while (end > start && (log[end - 1] == '\n' || log[end - 1] == '\r')) {
        end--;
    }
    int nl = 0, pos = end;
    while (pos > start) {
        if (log[pos - 1] == '\n') {
            if (nl + 1 == BR_LOG_LINES) {
                break;
            }
            nl++;
        }
        pos--;
    }
    if (pos == start && pos > 0 && log[pos - 1] != '\n') {
        // the byte limit cut a line in two: the partial line is dropped
        while (pos < end && log[pos] != '\n') {
            pos++;
        }
        if (pos < end) {
            pos++;
        }
    }
    int shown = 0;
    int i = pos;
    while (i < end) {
        int e = i;
        while (e < end && log[e] != '\n') {
            e++;
        }
        char l[480], s[480];
        int ll = e - i > (int)sizeof(l) - 1 ? (int)sizeof(l) - 1 : e - i;
        memcpy(l, log + i, ll);
        l[ll] = 0;
        if (br_secret_word(l)) {
            br_put(o, "[line withheld: it holds a credential word]\n");
        } else {
            br_url_scrub(l, s, sizeof(s));
            br_put(o, "%s\n", s);
        }
        shown++;
        i = e + 1;
    }
    if (!shown) {
        br_put(o, "(the log is empty)\n");
    }
}

void br_finish(BrOut *o)
{
    if (o->cut && o->cap > (int)sizeof(CUT_MARK)) {
        memcpy(o->buf + o->n, CUT_MARK, sizeof(CUT_MARK));
        o->n += (int)sizeof(CUT_MARK) - 1;
    }
}

static void fat_fields(uint32_t f, int *y, int *mo, int *d, int *h, int *mi, int *s)
{
    *y = 1980 + (int)(f >> 25);
    *mo = (int)((f >> 21) & 15);
    *d = (int)((f >> 16) & 31);
    *h = (int)((f >> 11) & 31);
    *mi = (int)((f >> 5) & 63);
    *s = (int)((f & 31) * 2);
}

void br_stamp(char *out, uint32_t fattime, bool valid)
{
    int y, mo, d, h, mi, s;
    if (!valid) {
        snprintf(out, BR_STAMP_LEN, "nodate");
        return;
    }
    fat_fields(fattime, &y, &mo, &d, &h, &mi, &s);
    snprintf(out, BR_STAMP_LEN, "%04d%02d%02d-%02d%02d%02d", y, mo, d, h, mi, s);
}

void br_date(char *out, int max, uint32_t fattime, bool valid)
{
    int y, mo, d, h, mi, s;
    if (!valid) {
        snprintf(out, max, "clock not set");
        return;
    }
    fat_fields(fattime, &y, &mo, &d, &h, &mi, &s);
    snprintf(out, max, "%04d-%02d-%02d %02d:%02d:%02d", y, mo, d, h, mi, s);
}
