// The call table of miniz's zip reader and inflate (zip_lib.h), linked into the image of the sub-app unzip (apps/unzip/Makefile, T239.3 D60). No core app links it and the base loads no
// library file; the table is the sub-app's own, which glue.cc takes with miniz_table().
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

// The table (size word first); C++ linkage.
const void *miniz_table(void)
{
    return &k_table;
}
