/*
 * libc.c - the few C library calls miniz and the app use.
 *
 * Apps are linked without a C library. gcc also emits calls to memcpy and
 * memset for struct copies and clears on its own, so these must exist.
 */

#include <stddef.h>

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    while (n--) {
        *dp++ = *sp++;
    }
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp < sp) {
        while (n--) {
            *dp++ = *sp++;
        }
    } else {
        while (n--) {
            dp[n] = sp[n];
        }
    }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dp = d;
    while (n--) {
        *dp++ = (unsigned char)c;
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *ap = a;
    const unsigned char *bp = b;
    for (; n; n--, ap++, bp++) {
        if (*ap != *bp) {
            return *ap - *bp;
        }
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) {
        p++;
    }
    return p - s;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
