// Operations and states of the TRX64U menu beyond the C64U's registers (trx64u_ops.cc). Host-owned.
#ifndef COREAPP_TRX64U_OPS_H
#define COREAPP_TRX64U_OPS_H

#include "ctx.h"
#include "ops.h"

void trx64u_register(OpRegistry &r);

// A setting's value index by key (cheap after the first call), `def` when the menu has no such setting; and a text setting's text ("" when empty).
// where a file named in the menu goes: the folder the menu was on, else the first medium; `ext` is added when the name has none
bool trx_target_path(CoreCtx *c, const char *name, const char *ext, char *out, int n);
int cfg_value(CoreCtx *c, const char *key, int def);
const char *cfg_text(CoreCtx *c, const char *key);

// c64.cc tells the module every KERNAL image it puts into the aperture (the fast reset patch is made against that copy).
void trx64u_kernal_put(CoreCtx *c, const uint8_t *data, uint32_t n);

// The SID player (T228 R21 item 7): c64.cc hands the BASIC ROM on for the player cartridge's image, the poll of c64.service drives the handshake, a reset of
// the application ends it.
void trx64u_basic_put(CoreCtx *c, const uint8_t *data, uint32_t n);
void trx64u_sid_service(CoreCtx *c);
void trx64u_sid_cancel(void);

#endif
