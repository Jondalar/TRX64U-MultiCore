// The slot of the Ultimate Command Interface ($DF1C): the part of the UCI driver that touches the block "cmd_if" of the driver table (drvtab.h, T239.3 D31).
// Nobody else writes these two registers. uci.cc (the server) is the rest of the driver.
#ifndef COREAPP_UCI_SLOT_H
#define COREAPP_UCI_SLOT_H

#include "ctx.h"

// The core lists the capability "uci" and the board did not strike it (board_effective_caps, BC_UCI): the rule of every call below that takes a context.
bool uci_slot_present(const CoreCtx *c);

// The registers themselves, no rule: SLOT_ENABLE is read and written, SLOT_BASE written (0x47 is $DF1C, the map's state "df1c").
uint8_t uci_slot_read_enable(void);
void uci_slot_write_enable(uint8_t v);
void uci_slot_write_base(uint8_t v);

// A cartridge or the module player asks for the command interface at $DF1C: enable and base, when uci_slot_present(c).
void uci_cart_slot(const CoreCtx *c);

#endif
