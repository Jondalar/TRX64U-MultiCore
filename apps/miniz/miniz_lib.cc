// The library "miniz" (T197 P1): miniz's zip reader and inflate as one resident library of format 3. It registers nothing in the menu or the registry; what
// it contributes is the call table of zip_lib.h, which an application reaches through api->lib->find("miniz", ZIPLIB_VERSION, &size).
#include "libapi.h"
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

#ifdef __riscv
#define LIB_ENTRY_ATTR __attribute__((visibility("default"), section(".text.lib_register")))
#else
#define LIB_ENTRY_ATTR
#endif
extern "C" LIB_ENTRY_ATTR int lib_register(const LibApi *api, LibRecord *out)
{
    if (api->abi_major != 3 || api->abi_minor < 1) {
        return 1;                                       // needs the library group (base 3.1)
    }
    out->exports = &k_table;
    out->exports_size = sizeof(k_table);
    return 0;
}
