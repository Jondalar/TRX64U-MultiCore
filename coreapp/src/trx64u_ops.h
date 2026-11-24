// Operations and states of the TRX64U menu beyond the C64U's registers (trx64u_ops.cc). Host-owned.
#ifndef COREAPP_TRX64U_OPS_H
#define COREAPP_TRX64U_OPS_H

#include "ctx.h"
#include "ops.h"

void trx64u_register(OpRegistry &r);

void trx64u_load_assoc_cfg(CoreCtx *c, const char *path, bool usr_too);   // the .cfg (a PRG: or .usr) beside a file, loaded before it runs

// the TRX64U core tells the module every KERNAL image it puts into the aperture (the fast reset patch is made against that copy).
void trx64u_kernal_put(CoreCtx *c, const uint8_t *data, uint32_t n);

// The SID player (T228 R21 item 7): the TRX64U core hands the BASIC ROM on for the player cartridge's image, the poll of c64.service drives the handshake, a reset of
// the application ends it.
void trx64u_basic_put(CoreCtx *c, const uint8_t *data, uint32_t n);
void trx64u_sid_service(CoreCtx *c);
void trx64u_sid_cancel(void);

#endif
