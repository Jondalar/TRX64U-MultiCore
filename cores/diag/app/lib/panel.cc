// The diag core's panels: parser, register access, presets, the list the person edits (panel.h).
#include "panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---- the register ----------------------------------------------------------------------------------------------------------------------------------
static uint32_t full_mask(const PanelReg &g) { return g.bytes >= 4 ? 0xFFFFFFFFu : ((1u << (8 * g.bytes)) - 1u) >> g.shift; }

static uint32_t reg_read(CoreIo *io, const PanelReg &g)
{
    uint32_t v = 0;
    for (int i = 0; i < g.bytes; i++) {
        v |= (uint32_t)io->rd(g.ofs + i) << (8 * i);
    }
    return v;
}

static void (*g_system)(CoreCtx *, int);
void panel_set_system_hook(void (*fn)(CoreCtx *, int)) { g_system = fn; }

int32_t panel_get(CoreCtx *c, const PanelRow &r)
{
    uint32_t v = (reg_read(c->io, r.reg) >> r.reg.shift) & r.reg.mask;
    if (r.reg.sgn) {
        uint32_t top = (r.reg.mask >> 1) + 1;            // the sign bit of the field
        if (v & top) {
            return (int32_t)v - (int32_t)(r.reg.mask + 1u);
        }
    }
    return (int32_t)v;
}

void panel_put(CoreCtx *c, const PanelRow &r, int32_t v)
{
    v = v < r.lo ? r.lo : v > r.hi ? r.hi : v;
    CoreIo *io = c->io;
    const PanelReg &g = r.reg;
    uint32_t put = ((uint32_t)v & g.mask) << g.shift;
    if (g.shift == 0 && g.mask == full_mask(g)) {         // the whole register: a plain write
        for (int i = 0; i < g.bytes; i++) {
            io->wr(g.ofs + i, (uint8_t)(put >> (8 * i)));
        }
    } else {                                             // some bits of it: the other bits stay as they are
        uint32_t keep = reg_read(io, g) & ~(g.mask << g.shift);
        uint32_t w = keep | put;
        for (int i = 0; i < g.bytes; i++) {
            io->wr(g.ofs + i, (uint8_t)(w >> (8 * i)));
        }
    }
    if (r.system && g_system) {
        g_system(c, v);
    }
    if (r.nvfmt) {
        io->video_format(r.vfmt[v >= 0 && v < r.nvfmt ? v : 0]);
    }
}

int panel_name_idx(const PanelRow &r, int32_t v)
{
    for (int i = 0; i < r.n; i++) {
        if (r.vals[i] == v) {
            return i;
        }
    }
    return -1;
}

void panel_text(CoreCtx *c, const PanelRow &r, char *out, int n)
{
    int32_t v = panel_get(c, r);
    int i = panel_name_idx(r, v);
    if (i >= 0) {
        snprintf(out, n, "%s", r.names[i]);
    } else {
        snprintf(out, n, r.n ? "#%d" : "%d", (int)v);
    }
}

void panel_step(CoreCtx *c, const PanelRow &r, int dir)
{
    int32_t v = panel_get(c, r);
    if (r.n) {
        int i = panel_name_idx(r, v);
        i = i < 0 ? (dir > 0 ? 0 : r.n - 1) : (i + dir + r.n) % r.n;
        panel_put(c, r, r.vals[i]);
        return;
    }
    v += dir * r.step;
    panel_put(c, r, v > r.hi ? r.lo : v < r.lo ? r.hi : v);
}

void panel_defaults(CoreCtx *c, const Panel &p)
{
    for (int k = 0; k < p.nrows; k++) {
        if (!p.row[k].action[0]) {
            panel_put(c, p.row[k], p.row[k].def);
        }
    }
}

// ---- presets ---------------------------------------------------------------------------------------------------------------------------------------
// A line "Key = "value"" of a preset file: the key before ` = "`, the text up to the closing quote.
static bool preset_line(const char *s, const char *e, char *key, int kn, char *val, int vn)
{
    const char *eq = 0;
    for (const char *q = s; q + 3 < e; q++) {
        if (q[0] == ' ' && q[1] == '=' && q[2] == ' ' && q[3] == '"') {
            eq = q;
            break;
        }
    }
    if (!eq || eq - s >= kn) {
        return false;
    }
    memcpy(key, s, (size_t)(eq - s));
    key[eq - s] = 0;
    const char *v = eq + 4;
    int j = 0;
    while (v < e && *v != '"' && j < vn - 1) {
        val[j++] = *v++;
    }
    val[j] = 0;
    return v < e && *v == '"';
}

int panel_preset_parse(const Panel &p, const char *text, int32_t raw[PNL_ROWS], uint8_t have[PNL_ROWS])
{
    int bad = 0;
    memset(have, 0, PNL_ROWS);
    const char *s = text;
    while (*s) {
        const char *e = strchr(s, '\n');
        if (!e) {
            e = s + strlen(s);
        }
        char key[PNL_KEY + 8], val[48];
        if (preset_line(s, e, key, sizeof(key), val, sizeof(val))) {
            for (int k = 0; k < p.nrows; k++) {
                const PanelRow &r = p.row[k];
                if (r.action[0] || strcmp(r.key, key)) {
                    continue;
                }
                if (val[0] == '#') {
                    raw[k] = atoi(val + 1);
                    have[k] = PNL_HAVE;
                } else {
                    int i = -1;
                    for (int j = 0; j < r.n && i < 0; j++) {
                        const char *a = r.names[j], *b = val;
                        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
                            a++;
                            b++;
                        }
                        i = !*a && !*b ? j : -1;
                    }
                    if (i >= 0) {
                        raw[k] = r.vals[i];
                        have[k] = PNL_HAVE;
                    } else {
                        have[k] = PNL_BAD;
                        bad++;
                    }
                }
            }
        }
        s = *e ? e + 1 : e;
    }
    return bad;
}

int panel_preset_load(CoreCtx *c, const Panel &p, const char *text, int *bad)
{
    static int32_t raw[PNL_ROWS];
    static uint8_t have[PNL_ROWS];
    *bad = panel_preset_parse(p, text, raw, have);
    int done = 0;
    for (int k = 0; k < p.nrows; k++) {                    // in the order of the panel: the Standard is the first row and the others do not depend on it
        if (have[k] == PNL_HAVE) {
            panel_put(c, p.row[k], raw[k]);
            done++;
        }
    }
    return done;
}

int panel_preset_write(CoreCtx *c, const Panel &p, const char *name, char *out, int cap)
{
    int n = snprintf(out, cap, "# TRX64U Port Diagnostics preset (T201, T259, T260): the rows of the panel \"%s\" in user terms, \"#n\" a register value that has no name. Load it with Load preset in the panel.\nkind = \"%s\"\n\n[preset]\nname = \"%s\"\n",
                     p.name, p.preset_kind, name);
    for (int k = 0; k < p.nrows && n >= 0 && n < cap; k++) {
        const PanelRow &r = p.row[k];
        if (r.action[0]) {
            continue;
        }
        int32_t v = panel_get(c, r);
        int i = panel_name_idx(r, v);
        n += i >= 0 ? snprintf(out + n, cap - n, "%s = \"%s\"\n", r.key, r.names[i]) : snprintf(out + n, cap - n, "%s = \"#%d\"\n", r.key, (int)v);
    }
    return n >= cap ? -1 : n;
}

// ---- the parser ------------------------------------------------------------------------------------------------------------------------------------
namespace {
struct Ps {
    Panel *p;
    PanelResolve res;
    char *err;
    int errn;
    int line;
    bool bad;
    // what the row being read named
    bool has_reg, has_mask, has_bytes, has_shift, has_min, has_max, has_step, has_def, def_is_name, sgn;
    char regname[40];
    uint32_t mask, bytes, shift;
    int32_t lo, hi, step, def_raw;
    char def_name[PNL_KEY * 2];
    int name_from, nvals_from, nvals;
    int vf_from;
    bool in_row;
};
}

static int ps_fail(Ps *s, const char *fmt, const char *a)
{
    if (!s->bad) {
        int n = snprintf(s->err, s->errn, "line %d: ", s->line);
        snprintf(s->err + n, s->errn - n, fmt, a);
    }
    s->bad = true;
    return -1;
}

static const char *ws(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

static const char *num(Ps *s, const char *p, int32_t *out)
{
    char *end;
    bool neg = false;
    if (*p == '-' || *p == '+') {
        neg = *p == '-';
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        ps_fail(s, "%s", "a number is expected");
        return 0;
    }
    long long v = (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) ? (long long)strtoull(p + 2, &end, 16) : (long long)strtoull(p, &end, 10);
    *out = (int32_t)(neg ? -v : v);
    return end;
}

// a string into the pool; the pointer after the closing quote
static const char *str(Ps *s, const char *p, const char **out)
{
    Panel *pn = s->p;
    p++;
    int n = 0;
    const char *start = pn->pool + pn->np;
    while (*p && *p != '"' && *p != '\n') {
        if (pn->np + n + 2 >= (int)sizeof(pn->pool)) {
            ps_fail(s, "%s", "the panel's texts do not fit");
            return 0;
        }
        char ch = *p++;
        if (ch == '\\' && *p) {
            ch = *p++;
        }
        pn->pool[pn->np + n++] = ch;
    }
    if (*p != '"') {
        ps_fail(s, "%s", "a string is not closed on its line");
        return 0;
    }
    pn->pool[pn->np + n] = 0;
    pn->np += n + 1;
    *out = start;
    return p + 1;
}

static void row_begin(Ps *s)
{
    Panel *p = s->p;
    PanelRow &r = p->row[p->nrows];
    memset(&r, 0, sizeof(r));
    s->has_reg = s->has_mask = s->has_bytes = s->has_shift = s->has_min = s->has_max = s->has_step = s->has_def = s->def_is_name = s->sgn = false;
    s->regname[0] = 0;
    s->def_name[0] = 0;
    s->nvals = 0;
    s->name_from = p->nn;
    s->nvals_from = p->nn;
    s->vf_from = p->nvf;
    s->in_row = true;
}

static int row_end(Ps *s)
{
    Panel *p = s->p;
    if (!s->in_row) {
        return 0;
    }
    s->in_row = false;
    PanelRow &r = p->row[p->nrows];
    if (!r.key[0]) {
        return ps_fail(s, "%s", "a row has no key");
    }
    if (r.action[0]) {
        if (strcmp(r.action, "load") && strcmp(r.action, "save") && strcmp(r.action, "defaults")) {
            return ps_fail(s, "an action is load, save or defaults, not \"%s\"", r.action);
        }
        p->nrows++;
        return 0;
    }
    if (!s->has_reg) {
        return ps_fail(s, "the row \"%s\" has no reg", r.key);
    }
    PanelReg g;
    memset(&g, 0, sizeof(g));
    const bool numeric = s->regname[0] >= '0' && s->regname[0] <= '9';
    if (numeric) {
        g.ofs = (uint16_t)strtoul(s->regname, 0, 0);
        g.bytes = 1;
    } else if (!s->res || !s->res(s->regname, &g)) {
        return ps_fail(s, "no register called \"%s\" in the core's TOML A", s->regname);
    }
    if (s->has_bytes && !numeric) {
        return ps_fail(s, "the row \"%s\": bytes go with a register number only (a named register has its own)", r.key);
    }
    if (s->has_bytes) {
        g.bytes = (uint8_t)s->bytes;
    }
    if (s->has_shift) {
        g.shift = (uint8_t)s->shift;
    }
    if (g.bytes < 1 || g.bytes > 4 || g.shift > 24) {
        return ps_fail(s, "the row \"%s\": bytes are 1 to 4", r.key);
    }
    if (s->has_mask) {
        g.mask = s->mask;
    } else if (numeric) {
        g.mask = full_mask(g);
    }
    if (s->sgn) {
        g.sgn = true;
    }
    r.reg = g;
    r.n = (uint16_t)(p->nn - s->name_from);
    r.names = p->namep + s->name_from;
    if (r.n && s->nvals && s->nvals != r.n) {
        return ps_fail(s, "the row \"%s\": names and values are not the same number", r.key);
    }
    if (r.n && !s->nvals) {                              // values default to 0, 1, ...
        for (int i = 0; i < r.n; i++) {
            p->valp[s->name_from + i] = i;
        }
    }
    r.vals = p->valp + s->name_from;
    r.nvfmt = (uint8_t)(p->nvf - s->vf_from);
    r.vfmt = p->vfp + s->vf_from;
    int32_t span_lo, span_hi;
    if (g.sgn) {
        span_hi = (int32_t)(g.mask >> 1);
        span_lo = -span_hi - 1;
    } else {
        span_lo = 0;
        span_hi = (int32_t)g.mask;
    }
    r.lo = s->has_min ? s->lo : span_lo;
    r.hi = s->has_max ? s->hi : span_hi;
    r.step = s->has_step ? s->step : 1;
    if (r.lo > r.hi || r.step < 1) {
        return ps_fail(s, "the row \"%s\": the range or the step is empty", r.key);
    }
    if (s->def_is_name) {
        int i = -1;
        for (int j = 0; j < r.n; j++) {
            if (!strcmp(r.names[j], s->def_name)) {
                i = j;
            }
        }
        if (i < 0) {
            return ps_fail(s, "the default \"%s\" is no name of the row", s->def_name);
        }
        r.def = r.vals[i];
    } else if (s->has_def) {
        r.def = s->def_raw;
    } else {
        r.def = r.n ? r.vals[0] : r.lo;
    }
    p->nrows++;
    return 0;
}

static int set_kv(Ps *s, const char *key, const char *p, int section)   // section 0 top, 1 panel, 2 row
{
    Panel *pn = s->p;
    PanelRow &r = pn->row[pn->nrows];
    const char *sv = 0;
    int32_t iv = 0;
    bool isstr = *p == '"', isarr = *p == '[', isbool = !strncmp(p, "true", 4) || !strncmp(p, "false", 5);
    if (isstr) {
        p = str(s, p, &sv);
        if (!p) {
            return -1;
        }
    } else if (isbool) {
        iv = !strncmp(p, "true", 4);
        p += iv ? 4 : 5;
    } else if (!isarr) {
        p = num(s, p, &iv);
        if (!p) {
            return -1;
        }
    }
    if (section == 0 && !strcmp(key, "kind")) {
        return isstr && !strcmp(sv, "panel") ? 0 : ps_fail(s, "%s", "kind is \"panel\"");
    }
    if (section == 1) {
        if (!isstr) {
            return ps_fail(s, "%s needs a string", key);
        }
        if (!strcmp(key, "name")) { snprintf(pn->name, sizeof(pn->name), "%s", sv); }
        else if (!strcmp(key, "presets")) { snprintf(pn->presets, sizeof(pn->presets), "%s", sv); }
        else if (!strcmp(key, "preset_kind")) { snprintf(pn->preset_kind, sizeof(pn->preset_kind), "%s", sv); }
        else { return ps_fail(s, "[panel] has no key %s", key); }
        return 0;
    }
    if (section != 2) {
        return ps_fail(s, "%s is no key of the top of a panel file", key);
    }
    if (!strcmp(key, "key") && isstr) { snprintf(r.key, sizeof(r.key), "%s", sv); }
    else if (!strcmp(key, "action") && isstr) { snprintf(r.action, sizeof(r.action), "%s", sv); }
    else if (!strcmp(key, "reg") && isstr) { snprintf(s->regname, sizeof(s->regname), "%s", sv); s->has_reg = true; }
    else if (!strcmp(key, "reg") && !isarr && !isbool) { snprintf(s->regname, sizeof(s->regname), "%d", (int)iv); s->has_reg = true; }
    else if (!strcmp(key, "bytes") && !isstr) { s->bytes = (uint32_t)iv; s->has_bytes = true; }
    else if (!strcmp(key, "shift") && !isstr) { s->shift = (uint32_t)iv; s->has_shift = true; }
    else if (!strcmp(key, "mask") && !isstr) { s->mask = (uint32_t)iv; s->has_mask = true; }
    else if (!strcmp(key, "signed") && isbool) { s->sgn = iv != 0; }
    else if (!strcmp(key, "system") && isbool) { r.system = iv != 0; }
    else if (!strcmp(key, "min") && !isstr && !isarr) { s->lo = iv; s->has_min = true; }
    else if (!strcmp(key, "max") && !isstr && !isarr) { s->hi = iv; s->has_max = true; }
    else if (!strcmp(key, "step") && !isstr && !isarr) { s->step = iv; s->has_step = true; }
    else if (!strcmp(key, "default") && isstr) { snprintf(s->def_name, sizeof(s->def_name), "%s", sv); s->def_is_name = true; }
    else if (!strcmp(key, "default") && !isarr && !isbool) { s->def_raw = iv; s->has_def = true; }
    else if (!strcmp(key, "names") || !strcmp(key, "values") || !strcmp(key, "video_format")) {
        if (!isarr) {
            return ps_fail(s, "%s is an array", key);
        }
        bool names = key[0] == 'n', vals = key[0] == 'v' && key[1] == 'a';
        if (names) {
            pn->nn = s->name_from;                       // (a repeated key replaces the first)
        }
        if (vals) {
            s->nvals = 0;
        }
        if (key[0] == 'v' && key[1] == 'i') {
            pn->nvf = s->vf_from;
        }
        p = ws(p + 1);
        while (*p && *p != ']') {
            if (names) {
                const char *t;
                if (*p != '"' || !(p = str(s, p, &t))) {
                    return ps_fail(s, "%s", "names are strings");
                }
                if (pn->nn >= PNL_NAMES) {
                    return ps_fail(s, "%s", "too many names in the panel");
                }
                pn->namep[pn->nn++] = t;
            } else {
                int32_t x;
                if (!(p = num(s, p, &x))) {
                    return -1;
                }
                if (vals) {
                    if (s->name_from + s->nvals >= PNL_NAMES) {
                        return ps_fail(s, "%s", "too many values in the panel");
                    }
                    pn->valp[s->name_from + s->nvals++] = x;
                } else {
                    if (pn->nvf >= PNL_VFMT) {
                        return ps_fail(s, "%s", "too many video_format entries");
                    }
                    pn->vfp[pn->nvf++] = (uint8_t)x;
                }
            }
            p = ws(p);
            if (*p == ',') {
                p = ws(p + 1);
            }
        }
        if (*p != ']') {
            return ps_fail(s, "%s", "an array is not closed on its line");
        }
    } else {
        return ps_fail(s, "a row has no key \"%s\" of this kind", key);
    }
    return 0;
}

int panel_parse(const char *text, uint32_t len, Panel *p, PanelResolve res, char *err, int errn)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->presets, sizeof(p->presets), "presets");
    snprintf(p->preset_kind, sizeof(p->preset_kind), "preset");
    Ps s;
    memset(&s, 0, sizeof(s));
    s.p = p;
    s.res = res;
    s.err = err;
    s.errn = errn;
    err[0] = 0;
    int section = 0;
    const char *e = text + len;
    for (const char *l = text; l < e && !s.bad;) {
        const char *nl = (const char *)memchr(l, '\n', (size_t)(e - l));
        if (!nl) {
            nl = e;
        }
        s.line++;
        char buf[1400];
        size_t n = (size_t)(nl - l);
        if (n >= sizeof(buf)) {
            ps_fail(&s, "%s", "a line is longer than 1399 characters");
            break;
        }
        memcpy(buf, l, n);
        buf[n] = 0;
        if (n && buf[n - 1] == '\r') {
            buf[n - 1] = 0;
        }
        l = nl < e ? nl + 1 : e;
        const char *c = ws(buf);
        if (!*c || *c == '#') {
            continue;
        }
        if (!strncmp(c, "[[row]]", 7)) {
            if (row_end(&s)) {
                break;
            }
            if (p->nrows >= PNL_ROWS) {
                ps_fail(&s, "%s", "too many rows in the panel");
                break;
            }
            row_begin(&s);
            section = 2;
            continue;
        }
        if (!strncmp(c, "[panel]", 7)) {
            section = 1;
            continue;
        }
        if (*c == '[') {
            ps_fail(&s, "no section %s", c);
            break;
        }
        char key[24];
        int k = 0;
        while ((isalnum((unsigned char)*c) || *c == '_') && k < (int)sizeof(key) - 1) {
            key[k++] = *c++;
        }
        key[k] = 0;
        c = ws(c);
        if (!k || *c != '=') {
            ps_fail(&s, "%s", "expected key = value");
            break;
        }
        c = ws(c + 1);
        // an array may stay open over more than one line in a hand-written file: not supported, said so by the "not closed" error
        if (set_kv(&s, key, c, section)) {
            break;
        }
    }
    if (!s.bad) {
        row_end(&s);
    }
    if (!s.bad && !p->nrows) {
        ps_fail(&s, "%s", "the panel has no row");
    }
    return s.bad ? -1 : 0;
}

// ---- the list --------------------------------------------------------------------------------------------------------------------------------------
namespace {
struct Run {
    CoreCtx *c;
    CoreDialogs *dlg;
    const Panel *p;
    DlgRow rows[PNL_ROWS];
    char folder[200];
};
}

static void refresh(Run *r)
{
    for (int k = 0; k < r->p->nrows; k++) {
        const PanelRow &pr = r->p->row[k];
        snprintf(r->rows[k].label, sizeof(r->rows[k].label), "%s", pr.key);
        r->rows[k].action = pr.action[0] != 0;
        if (pr.action[0]) {
            r->rows[k].value[0] = 0;
        } else {
            panel_text(r->c, pr, r->rows[k].value, sizeof(r->rows[k].value));
        }
    }
}

static void on_key(void *ud, int row, int key)
{
    Run *r = (Run *)ud;
    panel_step(r->c, r->p->row[row], key == DLG_EDIT_LEFT ? -1 : 1);
    refresh(r);
}

static bool name_ok(const char *n)
{
    if (!*n || strlen(n) > 40) {
        return false;
    }
    for (; *n; n++) {
        if (*n == '/' || *n == '\\' || *n == '"' || *n == ':' || (unsigned char)*n < 0x20) {
            return false;
        }
    }
    return true;
}

static void do_load(Run *r)
{
    char start[240], path[300];
    snprintf(start, sizeof(start), "%s/%s", r->folder, r->p->presets);
    if (!r->dlg->pick(start, path, sizeof(path))) {
        return;
    }
    static char buf[2048];
    int n = r->c->fs->load(path, buf, sizeof(buf) - 1);
    if (n < 0) {
        r->dlg->notice(r->p->name, "The file cannot be read.");
        return;
    }
    buf[n] = 0;
    char kl[48];
    snprintf(kl, sizeof(kl), "kind = \"%s\"", r->p->preset_kind);
    if (!strstr(buf, kl)) {
        r->dlg->notice(r->p->name, "That file is no preset of this panel.");
        return;
    }
    int bad = 0;
    int done = panel_preset_load(r->c, *r->p, buf, &bad);
    char m[120];
    snprintf(m, sizeof(m), "%d rows loaded, %d with a value the row does not have (rows not in the file kept).", done, bad);
    r->dlg->notice(r->p->name, m);
}

static void do_save(Run *r)
{
    char name[48] = "";
    if (!r->dlg->text("Preset name", name, sizeof(name))) {
        return;
    }
    if (!name_ok(name)) {
        r->dlg->notice(r->p->name, "The name is empty, longer than 40 or has / \\ : or a quote.");
        return;
    }
    static char buf[2200];
    int n = panel_preset_write(r->c, *r->p, name, buf, sizeof(buf));
    char dir[240], path[300], m[400];
    snprintf(dir, sizeof(dir), "%s/%s", r->folder, r->p->presets);
    r->c->fs->mkdir(r->folder);
    if (n < 0 || !r->c->fs->mkdir(dir)) {
        snprintf(m, sizeof(m), "Cannot make %s (read only card?).", dir);
    } else {
        snprintf(path, sizeof(path), "%s/%s.toml", dir, name);
        snprintf(m, sizeof(m), r->c->fs->save(path, buf, (uint32_t)n) ? "Preset written to %s." : "Cannot write %s (read only card?).", path);
    }
    r->dlg->notice(r->p->name, m);
}

void panel_run(CoreCtx *c, CoreDialogs *dlg, const Panel &p, const char *folder)
{
    static Run run;                                     // one panel at a time: the person has one screen
    run.c = c;
    run.dlg = dlg;
    run.p = &p;
    snprintf(run.folder, sizeof(run.folder), "%s", folder);
    refresh(&run);
    int sel = 0;
    for (;;) {
        int a = dlg->edit_list(p.name, "LEFT / RIGHT change a row", run.rows, p.nrows, sel, on_key, &run);
        if (a < 0) {
            return;
        }
        sel = a;
        const char *act = p.row[a].action;
        if (!strcmp(act, "load")) {
            do_load(&run);
        } else if (!strcmp(act, "save")) {
            do_save(&run);
        } else if (!strcmp(act, "defaults")) {
            panel_defaults(c, p);
        }
        refresh(&run);
    }
}
