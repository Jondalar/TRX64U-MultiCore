#include "greet.h"

static int put(char *out, int outlen, int at, const char *s)
{
    while (*s && at < outlen - 1) {
        out[at++] = *s++;
    }
    return at;
}

int greet_text(const char *name, char *out, int outlen)
{
    int n = 0;
    if (outlen <= 0) {
        return 0;
    }
    n = put(out, outlen, n, "Hello, ");
    n = put(out, outlen, n, name);
    n = put(out, outlen, n, "!");
    out[n] = 0;
    return n;
}
