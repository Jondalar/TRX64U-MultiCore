// The tables of the libraries the app is linked with (T239.3 D12, D31, base 3.3). Each library gives its tables in its own file (c64cart_exports.cc, cbm1541_exports.cc,
// sid_exports.cc, tempdisk_exports.cc, cbmprg_exports.cc); the library table that the build makes from the app's TOML B (tools/libtab.py) lists the libraries, so this
// file names none of them.
#include "libexports.h"
#include "libtab.h"

void libexports_register(LibLoader &ld, CoreCtx *c)
{
    libtab_bind(c, ld);
}
