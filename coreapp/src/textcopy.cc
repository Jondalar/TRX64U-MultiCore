#include "textcopy.h"
#include <string.h>

const char *nf_base(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// The first `n` characters of `s`. The board's printf (vendor/gideon/software/system/small_printf.cc, built with FP_SUPPORT) has no "%.*s": it takes
// the '.' and the '*' for a precision of its floating point support, pops the length as the string's address and prints whatever lies there -
// the "weird garbage characters" in the name prompt of the Atari (board 2026-10-24). No precision in a format of this tree.
void nf_copy(char *out, int outlen, const char *s, size_t n)
{
    if (outlen <= 0) {
        return;
    }
    size_t l = 0;
    while (l < n && s[l]) {
        l++;
    }
    if (l > (size_t)outlen - 1) {
        l = (size_t)outlen - 1;
    }
    memcpy(out, s, l);
    out[l] = 0;
}
