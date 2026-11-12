/* LD_PRELOAD logger of ci/sims.sh (T174 D5): which files of the repository does a bench's process tree read?
 *
 *   SIM_READS_ROOT=<repo root>  SIM_DEPS_LOG=<file>  LD_PRELOAD=<this, built>  command...
 *
 * Every open for reading (open, openat, fopen and their 64-bit forms), every opendir and every open that failed with ENOENT, below
 * SIM_READS_ROOT, is appended to SIM_DEPS_LOG, one line each:
 *     <path>      a file that was opened for reading (root-relative)
 *     ?<path>     a path that was looked for and does not exist (a file added there later changes the run)
 *     @<path>     a directory that was listed (a file added or removed there later changes the run)
 * ci/sims.sh turns that into the bench's key. The simulators (ghdl, iverilog, vvp, verilator, make, cc), the interpreters (sh, python3)
 * and the compiled benches all go through libc, so one record covers analysis, elaboration and the files a bench opens at run time
 * ($readmemh, textio, generics). Only the path is logged, never a file's content. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static const char *root; static size_t rootlen; static const char *logpath; static int inited;

static void init(void) {
  if (inited) return;
  inited = 1;
  root = getenv("SIM_READS_ROOT"); logpath = getenv("SIM_DEPS_LOG");
  if (root) { rootlen = strlen(root); while (rootlen > 1 && root[rootlen - 1] == '/') rootlen--; }
}

/* absolute, with "." and ".." resolved textually (no symlink lookup: the path as the program named it) */
static int norm(int dirfd, const char *p, char *out) {
  char tmp[PATH_MAX * 2];
  if (p[0] == '/') { if (strlen(p) >= sizeof tmp) return 0; strcpy(tmp, p); }
  else {
    char base[PATH_MAX];
    if (dirfd == AT_FDCWD) { if (!getcwd(base, sizeof base)) return 0; }
    else { char l[64]; ssize_t n; snprintf(l, sizeof l, "/proc/self/fd/%d", dirfd); n = readlink(l, base, sizeof base - 1); if (n < 0) return 0; base[n] = 0; }
    if (strlen(base) + strlen(p) + 2 >= sizeof tmp) return 0;
    snprintf(tmp, sizeof tmp, "%s/%s", base, p);
  }
  char *o = out; *o = 0; char *s = tmp;
  while (*s) {
    while (*s == '/') s++;
    char *e = s; while (*e && *e != '/') e++;
    size_t n = e - s;
    if (n == 0) break;
    if (n == 1 && s[0] == '.') { }
    else if (n == 2 && s[0] == '.' && s[1] == '.') { char *sl = strrchr(out, '/'); if (sl) *sl = 0; o = out + strlen(out); }
    else { *o++ = '/'; memcpy(o, s, n); o += n; *o = 0; }
    s = e;
  }
  if (!*out) strcpy(out, "/");
  return 1;
}

static void note(char tag, int dirfd, const char *p) {
  char abs[PATH_MAX * 2], line[PATH_MAX * 2 + 4]; int fd, n, saved = errno;
  init();
  if (!root || !logpath || !p || !*p) { errno = saved; return; }
  if (!norm(dirfd, p, abs)) { errno = saved; return; }
  if (strncmp(abs, root, rootlen) != 0 || (abs[rootlen] != '/' && abs[rootlen] != 0)) { errno = saved; return; }
  if (!abs[rootlen]) { errno = saved; return; }
  n = snprintf(line, sizeof line, "%s%s\n", tag == 'R' ? "" : tag == 'A' ? "?" : "@", abs + rootlen + 1);
  { int (*op)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    fd = op(logpath, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644); }
  if (fd >= 0) { if (write(fd, line, n) < 0) { } close(fd); }
  errno = saved;
}

static int rdonly(int flags) { return (flags & O_ACCMODE) == O_RDONLY && !(flags & (O_CREAT | O_TRUNC)); }

int open(const char *p, int flags, ...) {
  mode_t m = 0; if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, flags); m = (mode_t)va_arg(a, unsigned int); va_end(a); }
  int (*f)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
  int r = f(p, flags, m);
  if (rdonly(flags)) note(r >= 0 ? 'R' : (errno == ENOENT ? 'A' : 'X'), AT_FDCWD, p);
  return r;
}
int open64(const char *p, int flags, ...) {
  mode_t m = 0; if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, flags); m = (mode_t)va_arg(a, unsigned int); va_end(a); }
  int (*f)(const char *, int, ...) = dlsym(RTLD_NEXT, "open64");
  int r = f(p, flags, m);
  if (rdonly(flags)) note(r >= 0 ? 'R' : (errno == ENOENT ? 'A' : 'X'), AT_FDCWD, p);
  return r;
}
int openat(int d, const char *p, int flags, ...) {
  mode_t m = 0; if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, flags); m = (mode_t)va_arg(a, unsigned int); va_end(a); }
  int (*f)(int, const char *, int, ...) = dlsym(RTLD_NEXT, "openat");
  int r = f(d, p, flags, m);
  if (rdonly(flags)) note(r >= 0 ? 'R' : (errno == ENOENT ? 'A' : 'X'), d, p);
  return r;
}
int openat64(int d, const char *p, int flags, ...) {
  mode_t m = 0; if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, flags); m = (mode_t)va_arg(a, unsigned int); va_end(a); }
  int (*f)(int, const char *, int, ...) = dlsym(RTLD_NEXT, "openat64");
  int r = f(d, p, flags, m);
  if (rdonly(flags)) note(r >= 0 ? 'R' : (errno == ENOENT ? 'A' : 'X'), d, p);
  return r;
}
static FILE *fo(const char *name, const char *p, const char *mode) {
  FILE *(*f)(const char *, const char *) = dlsym(RTLD_NEXT, name);
  FILE *r = f(p, mode);
  if (mode && mode[0] == 'r' && !strchr(mode, '+')) note(r ? 'R' : (errno == ENOENT ? 'A' : 'X'), AT_FDCWD, p);
  return r;
}
FILE *fopen(const char *p, const char *mode) { return fo("fopen", p, mode); }
FILE *fopen64(const char *p, const char *mode) { return fo("fopen64", p, mode); }
DIR *opendir(const char *p) {
  DIR *(*f)(const char *) = dlsym(RTLD_NEXT, "opendir");
  DIR *r = f(p);
  if (r) note('D', AT_FDCWD, p);
  return r;
}
