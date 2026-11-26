// The reSID8 helpers of the shared SID library (T239.3 D34: the SID's driver, not the identity's): whether the SID of this bitstream is the 8-SID reDIP, and its filter
// control registers (the plan itself is resid8_filter.h). They were trx_io_resid8* in the IO block's file (host-app trx_io.cc). Board only: they write the registers.
#ifndef COREAPP_RESID8_H
#define COREAPP_RESID8_H

#include <stdint.h>
#include "resid8_filter.h"

// The SID of this bitstream is the 8-SID reDIP (reSID8): the app names it so and uses its filter control.
// False on Gideon's bitstreams (UltiSID) and on ours without that engine.
bool resid8_present(void);

// Programs the filter control of engine u (0 or 1): source (TRX_RS8_SRC_*), Auto bits, 6581 curve base and offset.
void resid8_filter(int u, uint8_t src, uint8_t auto_bits, int base, int offset);

// Filter Off for engine u: all voices straight to the mixer, FC / RES / routing / filter mode bits without effect, volume applies.
void resid8_bypass(int u, bool bypass);

#endif
