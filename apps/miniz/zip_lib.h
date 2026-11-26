/* The call table of miniz's zip reader (T197 P1): what miniz_lib.cc returns and unzip.c calls through (the image of unzip.u2a carries both, T239.3 D60). It is a plain table of
 * function pointers, so unzip.c itself carries no miniz code, only this header and miniz's own headers for the types (mz_zip_archive and friends).
 * The table is append-only: `size` says how much of it there is, a caller checks the field it uses against it (ZIPLIB_HAS). */
#ifndef APPS_MINIZ_ZIP_LIB_H
#define APPS_MINIZ_ZIP_LIB_H

#include "miniz.h"

#define ZIPLIB_NAME "miniz"
#define ZIPLIB_VERSION ((1u << 16) | 0u)     /* U2A3_VERSION(1, 0) */

typedef struct ZipLib {
    uint32_t size;
    mz_bool (*reader_init)(mz_zip_archive *z, mz_uint64 size, mz_uint flags);
    mz_bool (*reader_end)(mz_zip_archive *z);
    mz_uint (*get_num_files)(mz_zip_archive *z);
    mz_bool (*file_stat)(mz_zip_archive *z, mz_uint index, mz_zip_archive_file_stat *st);
    mz_bool (*extract_to_callback)(mz_zip_archive *z, mz_uint index, mz_file_write_func cb, void *opaque, mz_uint flags);
    mz_bool (*extract_to_mem)(mz_zip_archive *z, mz_uint index, void *buf, size_t size, mz_uint flags);
    int (*locate_file)(mz_zip_archive *z, const char *name, const char *comment, mz_uint flags);
    size_t (*inflate_to_mem)(void *out, size_t out_len, const void *src, size_t src_len, int flags);   /* tinfl_decompress_mem_to_mem */
} ZipLib;

#define ZIPLIB_HAS(t, field) ((t) && (t)->size >= (uint32_t)(__builtin_offsetof(ZipLib, field) + sizeof((t)->field)))

#endif
