// Tiny check macros for the native tests of coreapp/ (T93). A test counts its failures and exits non-zero when there is one.
#ifndef TESTS_COREAPP_CHECK_H
#define TESTS_COREAPP_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static int g_fails, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fails++; } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fails++; } } while (0)
// Both sides are copied while the full expression that made them is still alive: CHECK_STR(f().c_str(), ..) on a
// returned std::string otherwise keeps a pointer into a string destroyed at the end of the declaration, which
// macOS happened to leave readable and glibc did not (run 897).
static inline std::string check_str_of(const char *s) { return s ? s : "(null)"; }
#define CHECK_STR(a, b) do { g_checks++; const std::string _a = check_str_of(a), _b = check_str_of(b); \
    if (_a != _b) { printf("FAIL %s:%d  %s == %s  (\"%s\" vs \"%s\")\n", __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str()); g_fails++; } } while (0)
#define DONE(name) do { printf("%s: %d checks, %d failed\n", name, g_checks, g_fails); return g_fails ? 1 : 0; } while (0)

#endif
