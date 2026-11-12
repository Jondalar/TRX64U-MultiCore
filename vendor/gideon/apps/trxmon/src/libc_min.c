/*
 * libc_min.c — the four memory functions the compiler emits calls to. The
 * app has no libc (app.mk links with -nostdlib); the host tests use the real one.
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = (uint8_t *)d;
    const uint8_t *sp = (const uint8_t *)s;
    while (n--) {
        *dp++ = *sp++;
    }
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = (uint8_t *)d;
    const uint8_t *sp = (const uint8_t *)s;
    if (dp < sp) {
        while (n--) {
            *dp++ = *sp++;
        }
    } else {
        dp += n;
        sp += n;
        while (n--) {
            *--dp = *--sp;
        }
    }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    uint8_t *dp = (uint8_t *)d;
    while (n--) {
        *dp++ = (uint8_t)c;
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return x[i] - y[i];
        }
    }
    return 0;
}
