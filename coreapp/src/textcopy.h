// Two string helpers of the generic application (T239.3 P3-1): the file name part of a path and a bounded copy. They were in newfile.h; code that does not use the
// library "newfile" (the menus, the file manager, the ring file, the network operations, the cores) includes this header and not the library's.
#ifndef COREAPP_TEXTCOPY_H
#define COREAPP_TEXTCOPY_H
#include <stddef.h>

// the file name part of a path
const char *nf_base(const char *path);
// `out` (outlen bytes) takes the first `n` characters of `s`, terminated. Use this where a format would say "%.*s": the board's printf cannot.
void nf_copy(char *out, int outlen, const char *s, size_t n);

#endif
