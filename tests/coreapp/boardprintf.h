// The printf of the board, as far as it matters to a test (T122 §5, board 2026-10-24).
//
// The firmware's printf (vendor/gideon/software/system/small_printf.cc, built with FP_SUPPORT for the core application) understands
//   %[-][width | * | #] d u i s c p x X b B
// and nothing else: no "%%" (it prints two percent signs), no precision ("%.*s" takes the '.' and the '*' for the precision of its float
// support, pops the length as the string's address and prints whatever lies there - the garbage in the Atari's name prompt), no length
// modifiers ("%lu"). The libc of the machine that runs a test accepts all of it, so a format the board cannot do passes natively.
//
// Include this in a test binary and every snprintf of the product code that runs in it is checked against that grammar; a format the
// board's printf cannot print is a failed check (g_board_printf_bad) with the format named. snprintf here replaces libc's for the whole
// program: the product objects are linked into the test executable, so their calls land on this one.
#ifndef TESTS_COREAPP_BOARDPRINTF_H
#define TESTS_COREAPP_BOARDPRINTF_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_board_printf_bad;

static bool board_format_ok(const char *f)
{
    for (; *f; f++) {
        if (*f != '%') {
            continue;
        }
        f++;
        if (*f == '-') {
            f++;
        }
        if (*f == '#' || *f == '*') {
            f++;
        } else {
            while (*f >= '0' && *f <= '9') {
                f++;
            }
        }
        if (!*f || !strchr("duisxXcpbB", *f)) {
            return false;
        }
    }
    return true;
}

extern "C" int snprintf(char *s, size_t n, const char *fmt, ...)
{
    if (!board_format_ok(fmt)) {
        g_board_printf_bad++;
        printf("FAIL the board's printf cannot print the format \"%s\"\n", fmt);
        g_fails++;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

#endif
