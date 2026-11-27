// The table of the gamepad library (pad.cc, pad_ops.cc, padwiz.cc): its table "pad" (pad_api.h), which the generic application finds through libtab_export("pad", 1) (libtab.h), its export in the `exports` of the
// library in the TOML B. Pure: no board code, so the native tests link it with the library; what the board adds (the critical section, the USB hooks, the XInput claim) is pad_board.cc.
#include "pad_api.h"
#include "pad.h"

static const PadApi g_pad = { sizeof(PadApi), pad_register, pad_pace, pad_reset };

const void *pad_table(void)
{
    return &g_pad;
}
