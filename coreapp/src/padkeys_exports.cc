// The table of the pad-keys library (padkeys.cc): its table "padkeys" (padkeys_api.h), which the generic application finds through libtab_table("padkeys") (libtab.h), the `table` entry of the library
// in the TOML B. Pure: no board code, so the native tests link it with the library.
#include "padkeys_api.h"
#include "padkeys.h"

static bool row(const Setting *s) { return padkey_row(*s); }

static const PadkeysApi g_padkeys = { sizeof(PadkeysApi), row, padkey_learn_value, PADKEY_NOT_AVAILABLE };

const void *padkeys_table(void)
{
    return &g_padkeys;
}
