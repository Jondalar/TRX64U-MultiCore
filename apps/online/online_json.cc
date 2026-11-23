// The JSON reader of the online sub-app (online_json.h). Two passes over the same text: the first checks the syntax and counts the nodes (it writes nothing), the second builds
// the tree and decodes the strings in place.
#include "online_json.h"
#include "otext.h"

namespace {

#define J_DEPTH 12

struct Rd {
    char *p;
    char *end;
    bool build;                  // second pass: write the nodes and the decoded strings
    JNode *pool;
    int count;
    bool bad;
};

void ws(Rd &r)
{
    while (r.p < r.end && (*r.p == ' ' || *r.p == '\t' || *r.p == '\r' || *r.p == '\n')) {
        r.p++;
    }
}

JNode *node(Rd &r, uint8_t type)
{
    JNode *n = r.build ? &r.pool[r.count] : 0;
    r.count++;
    if (n) {
        n->type = type;
        n->num = 0;
        n->str = 0;
        n->key = 0;
        n->child = 0;
        n->next = 0;
    }
    return n;
}

int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// A string at the opening quote: the decoded text is written over the source (it is never longer), NUL terminated; *out is where it starts. \uXXXX below 256 is one byte, above it a '?'.
bool string(Rd &r, const char **out)
{
    if (r.p >= r.end || *r.p != '"') {
        return false;
    }
    r.p++;
    char *w = r.p;
    char *start = r.p;
    while (r.p < r.end && *r.p != '"') {
        char c = *r.p++;
        if (c == '\\') {
            if (r.p >= r.end) {
                return false;
            }
            c = *r.p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                if (r.end - r.p < 4) {
                    return false;
                }
                int v = 0;
                for (int i = 0; i < 4; i++) {
                    int h = hexv(r.p[i]);
                    if (h < 0) {
                        return false;
                    }
                    v = v * 16 + h;
                }
                r.p += 4;
                c = v < 256 && v ? (char)v : '?';
                break;
            }
            default: break;                                   // \" \\ \/ : the character itself
            }
        }
        if (r.build) {
            *w++ = c;
        }
    }
    if (r.p >= r.end) {
        return false;
    }
    char *close = r.p++;
    if (r.build) {
        *w = 0;
        (void)close;
        *out = start;
    }
    return true;
}

bool value(Rd &r, int depth, JNode **out);

bool number(Rd &r, JNode **out)
{
    bool neg = false;
    if (*r.p == '-') {
        neg = true;
        r.p++;
    }
    if (r.p >= r.end || *r.p < '0' || *r.p > '9') {
        return false;
    }
    long v = 0;
    while (r.p < r.end && *r.p >= '0' && *r.p <= '9') {
        if (v < 100000000L) {
            v = v * 10 + (*r.p - '0');
        }
        r.p++;
    }
    while (r.p < r.end && (*r.p == '.' || *r.p == 'e' || *r.p == 'E' || *r.p == '+' || *r.p == '-' || (*r.p >= '0' && *r.p <= '9'))) {
        r.p++;                                                 // a fraction or an exponent: the integer part is what the client reads
    }
    JNode *n = node(r, J_INT);
    if (n) {
        n->num = (int32_t)(neg ? -v : v);
    }
    *out = n;
    return true;
}

bool literal(Rd &r, const char *word, uint8_t type, int32_t num, JNode **out)
{
    int l = ot_len(word);
    if (r.end - r.p < l || memcmp(r.p, word, (size_t)l) != 0) {
        return false;
    }
    r.p += l;
    JNode *n = node(r, type);
    if (n) {
        n->num = num;
    }
    *out = n;
    return true;
}

bool container(Rd &r, int depth, bool obj, JNode **out)
{
    char close = obj ? '}' : ']';
    r.p++;
    JNode *self = node(r, obj ? J_OBJ : J_LIST);
    JNode *last = 0;
    ws(r);
    if (r.p < r.end && *r.p == close) {
        r.p++;
        *out = self;
        return true;
    }
    for (;;) {
        ws(r);
        const char *key = 0;
        if (obj) {
            if (!string(r, &key)) {
                return false;
            }
            ws(r);
            if (r.p >= r.end || *r.p != ':') {
                return false;
            }
            r.p++;
            ws(r);
        }
        JNode *v = 0;
        if (!value(r, depth + 1, &v)) {
            return false;
        }
        if (r.build) {
            v->key = key;
            if (last) {
                last->next = v;
            } else {
                self->child = v;
            }
            last = v;
        }
        ws(r);
        if (r.p >= r.end) {
            return false;
        }
        if (*r.p == ',') {
            r.p++;
            continue;
        }
        if (*r.p == close) {
            r.p++;
            break;
        }
        return false;
    }
    *out = self;
    return true;
}

bool value(Rd &r, int depth, JNode **out)
{
    ws(r);
    if (r.p >= r.end || depth > J_DEPTH) {
        return false;
    }
    switch (*r.p) {
    case '{': return container(r, depth, true, out);
    case '[': return container(r, depth, false, out);
    case '"': {
        const char *s = 0;
        if (!string(r, &s)) {
            return false;
        }
        JNode *n = node(r, J_STR);
        if (n) {
            n->str = s;
        }
        *out = n;
        return true;
    }
    case 't': return literal(r, "true", J_BOOL, 1, out);
    case 'f': return literal(r, "false", J_BOOL, 0, out);
    case 'n': return literal(r, "null", J_NULL, 0, out);
    default: return number(r, out);
    }
}

}   // namespace

JTree json_parse(char *text, int len, void *(*alloc)(uint32_t n, void *ctx), void *ctx)
{
    JTree t = { 0, 0, 0 };
    Rd r = { text, text + len, false, 0, 0, false };
    JNode *dummy = 0;
    if (len <= 0 || !value(r, 0, &dummy)) {
        return t;
    }
    ws(r);
    if (r.p != r.end) {
        return t;                                              // something after the value
    }
    int n = r.count;
    JNode *pool = (JNode *)alloc((uint32_t)n * (uint32_t)sizeof(JNode), ctx);
    if (!pool) {
        return t;
    }
    Rd b = { text, text + len, true, pool, 0, false };
    JNode *root = 0;
    value(b, 0, &root);
    t.root = root;
    t.pool = pool;
    t.nodes = n;
    return t;
}

const JNode *json_member(const JNode *obj, const char *key)
{
    if (!obj || obj->type != J_OBJ) {
        return 0;
    }
    for (const JNode *c = obj->child; c; c = c->next) {
        if (c->key && !strcmp(c->key, key)) {
            return c;
        }
    }
    return 0;
}

const char *json_text(const JNode *obj, const char *key)
{
    const JNode *m = json_member(obj, key);
    return m && m->type == J_STR && m->str ? m->str : "";
}

int json_int(const JNode *obj, const char *key, int def)
{
    const JNode *m = json_member(obj, key);
    return m && m->type == J_INT ? (int)m->num : def;
}
