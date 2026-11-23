// The few text helpers the online sub-app uses. An application image has no C library: sdk/libc.c gives memcpy, memmove, memset, memcmp, strlen, strcmp, strncmp, strcpy, strncpy, strcat, strchr, strrchr
// and nothing else, so no snprintf, no strcasecmp, no strstr. These are the same calls over plain loops; the host test compiles the very same file.
#ifndef APPS_ONLINE_OTEXT_H
#define APPS_ONLINE_OTEXT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline int ot_len(const char *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

// dst = src, cut to n - 1 characters
static inline void ot_copy(char *dst, int n, const char *src)
{
    int i = 0;
    for (; i < n - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    if (n > 0) {
        dst[i] = 0;
    }
}

// dst += src, cut so that dst keeps n - 1 characters
static inline void ot_cat(char *dst, int n, const char *src)
{
    int l = ot_len(dst);
    if (l < n) {
        ot_copy(dst + l, n - l, src);
    }
}

static inline void ot_cat_int(char *dst, int n, long v)
{
    char t[16];
    int k = 0;
    bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do {
        t[k++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u && k < 14);
    char o[18];
    int j = 0;
    if (neg) {
        o[j++] = '-';
    }
    while (k) {
        o[j++] = t[--k];
    }
    o[j] = 0;
    ot_cat(dst, n, o);
}

static inline char ot_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

// equal, ignoring the case of ASCII letters
static inline bool ot_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (ot_lower(*a) != ot_lower(*b)) {
            return false;
        }
    }
    return *a == *b;
}

// the first place `what` stands in the n bytes at `s`, ignoring case; null when it does not
static inline const char *ot_ifind(const char *s, int n, const char *what)
{
    int w = ot_len(what);
    for (int i = 0; i + w <= n; i++) {
        int k = 0;
        while (k < w && ot_lower(s[i + k]) == ot_lower(what[k])) {
            k++;
        }
        if (k == w) {
            return s + i;
        }
    }
    return 0;
}

#endif
