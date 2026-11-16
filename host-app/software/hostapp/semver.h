// SemVer 2.0 precedence, header only (T221 D19: an update or a core of an older version warns, and the user may go on). Build metadata ("+<sha8>") is
// ignored; a pre-release ("-dev") is below its release. Anything that is not SemVer (a commit id from a pin without a SemVer, "full firmware") compares
// as SEMVER_NONE and warns nothing.
#ifndef HOST_SEMVER_H
#define HOST_SEMVER_H

#include <stdlib.h>
#include <string.h>

#define SEMVER_NONE 2

struct SemVer {
    unsigned long n[3];
    char pre[32];       // "" = a release
};

static inline bool semver_parse(const char *s, SemVer *v)
{
    if (!s || !*s) {
        return false;
    }
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        char *e;
        v->n[i] = strtoul(s, &e, 10);
        s = e;
        if (i < 2) {
            if (*s != '.') {
                return false;
            }
            s++;
        }
    }
    if (*s == '-') {
        s++;
        size_t k = 0;
        while (*s && *s != '+' && k + 1 < sizeof(v->pre)) {
            v->pre[k++] = *s++;
        }
        if (!k) {
            return false;
        }
    }
    return *s == 0 || *s == '+';
}

// -1: a is older than b, 0 equal, 1 newer, SEMVER_NONE when one of them is no SemVer
static inline int semver_cmp(const char *a, const char *b)
{
    SemVer x, y;
    if (!semver_parse(a, &x) || !semver_parse(b, &y)) {
        return SEMVER_NONE;
    }
    for (int i = 0; i < 3; i++) {
        if (x.n[i] != y.n[i]) {
            return x.n[i] < y.n[i] ? -1 : 1;
        }
    }
    if (!x.pre[0] || !y.pre[0]) {
        return x.pre[0] == y.pre[0] ? 0 : (x.pre[0] ? -1 : 1);
    }
    int c = strcmp(x.pre, y.pre);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

#endif
