/*
 * libc.c - the few C library calls the applications use. They are linked without a C library; gcc also emits calls to memcpy and memset for struct copies and
 * clears on its own, so these must exist.
 *
 * PROVENANCE: the first six are vendor/gideon/apps/unzip/src/libc.c (GPL-3.0); the rest are the same kind.
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

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a) {
            return (unsigned char)*a - (unsigned char)*b;
        }
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) {
    }
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) {
        d[i] = s[i];
    }
    for (; i < n; i++) {
        d[i] = 0;
    }
    return d;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) {
        d++;
    }
    while ((*d++ = *s++)) {
    }
    return r;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) {
            return (char *)s;
        }
        if (!*s) {
            return 0;
        }
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    for (;; s++) {
        if (*s == (char)c) {
            last = s;
        }
        if (!*s) {
            return (char *)last;
        }
    }
}

/* The CPU has no divider (-mno-div) and libgcc's own division is built position dependent, so the four 32-bit routines the compiler calls are here. */
static unsigned int udivmod(unsigned int a, unsigned int b, unsigned int *rem)
{
    unsigned int q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) {
            r -= b;
            q |= 1u << i;
        }
    }
    *rem = r;
    return q;
}

unsigned int __udivsi3(unsigned int a, unsigned int b)
{
    unsigned int r;
    return b ? udivmod(a, b, &r) : 0xFFFFFFFFu;
}

unsigned int __umodsi3(unsigned int a, unsigned int b)
{
    unsigned int r;
    if (!b) {
        return a;
    }
    udivmod(a, b, &r);
    return r;
}

int __divsi3(int a, int b)
{
    unsigned int ua = a < 0 ? 0u - (unsigned int)a : (unsigned int)a, ub = b < 0 ? 0u - (unsigned int)b : (unsigned int)b, r;
    unsigned int q = ub ? udivmod(ua, ub, &r) : 0x7FFFFFFFu;
    return (a < 0) != (b < 0) ? (int)(0u - q) : (int)q;
}

int __modsi3(int a, int b)
{
    unsigned int ua = a < 0 ? 0u - (unsigned int)a : (unsigned int)a, ub = b < 0 ? 0u - (unsigned int)b : (unsigned int)b, r = ua;
    if (ub) {
        udivmod(ua, ub, &r);
    }
    return a < 0 ? (int)(0u - r) : (int)r;
}

/* 64-bit shifts (a 32-bit CPU): the compiler calls them for a uint64_t bit map */
unsigned long long __ashldi3(unsigned long long a, int b)
{
    unsigned int lo = (unsigned int)a, hi = (unsigned int)(a >> 32);
    if (b <= 0) {
        return a;
    }
    if (b >= 64) {
        return 0;
    }
    if (b >= 32) {
        hi = lo << (b - 32);
        lo = 0;
    } else {
        hi = (hi << b) | (lo >> (32 - b));
        lo <<= b;
    }
    return ((unsigned long long)hi << 32) | lo;
}

unsigned long long __lshrdi3(unsigned long long a, int b)
{
    unsigned int lo = (unsigned int)a, hi = (unsigned int)(a >> 32);
    if (b <= 0) {
        return a;
    }
    if (b >= 64) {
        return 0;
    }
    if (b >= 32) {
        lo = hi >> (b - 32);
        hi = 0;
    } else {
        lo = (lo >> b) | (hi << (32 - b));
        hi >>= b;
    }
    return ((unsigned long long)hi << 32) | lo;
}

long long __ashrdi3(long long a, int b)
{
    unsigned int lo = (unsigned int)a;
    int hi = (int)(a >> 32);
    if (b <= 0) {
        return a;
    }
    if (b >= 64) {
        return hi < 0 ? -1 : 0;
    }
    if (b >= 32) {
        lo = (unsigned int)(hi >> (b - 32));
        hi = hi < 0 ? -1 : 0;
    } else {
        lo = (lo >> b) | ((unsigned int)hi << (32 - b));
        hi >>= b;
    }
    return (long long)(((unsigned long long)(unsigned int)hi << 32) | lo);
}

/* 64-bit division and remainder (a 32-bit CPU with no divider): shift and subtract, the 32-bit routine when both fit */
static unsigned long long udivmod64(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
    if (d == 0) {
        *rem = n;
        return ~0ull;
    }
    if ((n >> 32) == 0 && (d >> 32) == 0) {
        unsigned int r;
        unsigned int q = udivmod((unsigned int)n, (unsigned int)d, &r);
        *rem = r;
        return q;
    }
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    unsigned long long r;
    return udivmod64(a, b, &r);
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    unsigned long long r;
    udivmod64(a, b, &r);
    return r;
}

long long __divdi3(long long a, long long b)
{
    unsigned long long ua = a < 0 ? 0ull - (unsigned long long)a : (unsigned long long)a, ub = b < 0 ? 0ull - (unsigned long long)b : (unsigned long long)b, r;
    unsigned long long q = udivmod64(ua, ub, &r);
    return (a < 0) != (b < 0) ? (long long)(0ull - q) : (long long)q;
}

long long __moddi3(long long a, long long b)
{
    unsigned long long ua = a < 0 ? 0ull - (unsigned long long)a : (unsigned long long)a, ub = b < 0 ? 0ull - (unsigned long long)b : (unsigned long long)b, r;
    udivmod64(ua, ub, &r);
    return a < 0 ? (long long)(0ull - r) : (long long)r;
}
