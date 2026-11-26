// The library "miniz": miniz's zip reader and inflate, linked into the app through TOML B (T239.3 D58, base 4.0: the base loads no library file). It registers nothing in the
// menu or the registry; what it contributes is the call table of zip_lib.h, which the TOML B export `{ name = "miniz", major = 1, table = "miniz_table" }` hands out. Code
// linked into the app takes it with lib_slot(LIBSLOT_miniz, &size) (libslot.h); an application file lists `miniz>=1.0` in its needs and calls api->lib->need(n, &size).
#include "zip_lib.h"

static const ZipLib k_table = {
    sizeof(ZipLib),
    mz_zip_reader_init,
    mz_zip_reader_end,
    mz_zip_reader_get_num_files,
    mz_zip_reader_file_stat,
    mz_zip_reader_extract_to_callback,
    mz_zip_reader_extract_to_mem,
    mz_zip_reader_locate_file,
    tinfl_decompress_mem_to_mem,
};

// The table of the export "miniz" (size word first). Every app that links the library has the zip code, so the table is never null. C++ linkage, as the declaration
// tools/libtab.py writes into libtab_table.cc.
const void *miniz_table(void)
{
    return &k_table;
}
