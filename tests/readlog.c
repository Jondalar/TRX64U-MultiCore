/* Read recorder for the test cache (T100 D14), LD_PRELOAD on Linux.
 *
 * A test program is cached by what it reads. The compiler's dependency list says what the compile reads; this says what the RUN reads
 * (goldens, fixtures, product sources a test scans, python tools and their imports). Every path a process under it opens, stats, lists or
 * creates is appended to the file in $RL_LOG as one line, "R <path>" (read, stat, access, directory listing) or "W <path>" (opened for
 * writing), the path made absolute. tests/ctlcache.py turns the log into the entry's manifest and refuses to cache a unit that read a
 * fixture nobody declared. Nothing here changes what a call returns.
 *
 *   cc -shared -fPIC -O1 -o readlog.so tests/readlog.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static int logfd = -2;
static __thread int busy;

static void note(char kind, int dirfd, const char *path) {
  if (!path || !*path || busy) return;
  busy = 1;
  if (logfd == -2) {
    const char *f = getenv("RL_LOG");
    int (*o)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    logfd = (f && *f) ? o(f, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644) : -1;
  }
  if (logfd >= 0) {
    char buf[PATH_MAX * 2 + 8], base[PATH_MAX];
    size_t n = 0;
    buf[n++] = kind; buf[n++] = ' ';
    if (path[0] != '/') {
      base[0] = 0;
      if (dirfd == AT_FDCWD) {
        if (!getcwd(base, sizeof base)) base[0] = 0;
      } else {
        char l[64]; ssize_t r;
        snprintf(l, sizeof l, "/proc/self/fd/%d", dirfd);
        r = readlink(l, base, sizeof base - 1);
        base[r > 0 ? r : 0] = 0;
      }
      n += snprintf(buf + n, sizeof buf - n, "%s/", base);
    }
    n += snprintf(buf + n, sizeof buf - n, "%s\n", path);
    if (n < sizeof buf) { ssize_t w = write(logfd, buf, n); (void)w; }
  }
  busy = 0;
}
static char kind_of_flags(int flags) { return (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) ? 'W' : 'R'; }
static char kind_of_mode(const char *m) { return (m && (strchr(m, 'w') || strchr(m, 'a') || strchr(m, '+'))) ? 'W' : 'R'; }

#define REAL(ret, name, args) static ret (*real_##name) args; if (!real_##name) real_##name = dlsym(RTLD_NEXT, #name)

#define OPEN_FN(name) \
  int name(const char *path, int flags, ...) { \
    REAL(int, name, (const char *, int, ...)); mode_t m = 0; \
    if (flags & (O_CREAT | 020200000)) { va_list ap; va_start(ap, flags); m = va_arg(ap, mode_t); va_end(ap); } \
    note(kind_of_flags(flags), AT_FDCWD, path); return real_##name(path, flags, m); }
#define OPENAT_FN(name) \
  int name(int dfd, const char *path, int flags, ...) { \
    REAL(int, name, (int, const char *, int, ...)); mode_t m = 0; \
    if (flags & (O_CREAT | 020200000)) { va_list ap; va_start(ap, flags); m = va_arg(ap, mode_t); va_end(ap); } \
    note(kind_of_flags(flags), path && path[0] == '/' ? AT_FDCWD : dfd, path); return real_##name(dfd, path, flags, m); }
OPEN_FN(open) OPEN_FN(open64) OPENAT_FN(openat) OPENAT_FN(openat64)

int creat(const char *path, mode_t m) { REAL(int, creat, (const char *, mode_t)); note('W', AT_FDCWD, path); return real_creat(path, m); }
#define FOPEN_FN(name) FILE *name(const char *p, const char *m) { REAL(FILE *, name, (const char *, const char *)); note(kind_of_mode(m), AT_FDCWD, p); return real_##name(p, m); }
FOPEN_FN(fopen) FOPEN_FN(fopen64)
#define STAT_FN(name, T) int name(const char *p, T *b) { REAL(int, name, (const char *, T *)); note('R', AT_FDCWD, p); return real_##name(p, b); }
STAT_FN(stat, struct stat) STAT_FN(lstat, struct stat) STAT_FN(stat64, struct stat64) STAT_FN(lstat64, struct stat64)
#define FSTATAT_FN(name, T) int name(int d, const char *p, T *b, int f) { REAL(int, name, (int, const char *, T *, int)); note('R', p && p[0] == '/' ? AT_FDCWD : d, p); return real_##name(d, p, b, f); }
FSTATAT_FN(fstatat, struct stat) FSTATAT_FN(fstatat64, struct stat64)
int statx(int d, const char *p, int f, unsigned int mask, struct statx *b) {
  REAL(int, statx, (int, const char *, int, unsigned int, struct statx *));
  note('R', p && p[0] == '/' ? AT_FDCWD : d, p); return real_statx(d, p, f, mask, b); }
int access(const char *p, int m) { REAL(int, access, (const char *, int)); note('R', AT_FDCWD, p); return real_access(p, m); }
int faccessat(int d, const char *p, int m, int f) { REAL(int, faccessat, (int, const char *, int, int)); note('R', p && p[0] == '/' ? AT_FDCWD : d, p); return real_faccessat(d, p, m, f); }
DIR *opendir(const char *p) { REAL(DIR *, opendir, (const char *)); note('L', AT_FDCWD, p); return real_opendir(p); }
