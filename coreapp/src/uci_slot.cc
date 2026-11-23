#include "uci_slot.h"
#include "desc.h"
#include "board.h"
#include "drvtab.h"

bool uci_slot_present(const CoreCtx *c)
{
    return c && c->desc && desc_has_capability(c->desc, "uci") && (!c->board || (c->board->probes & BC_UCI));
}

uint8_t uci_slot_read_enable(void) { return DRV_cmd_if_slot_enable_RD(); }
void uci_slot_write_enable(uint8_t v) { DRV_cmd_if_slot_enable_WR(v); }
void uci_slot_write_base(uint8_t v) { DRV_cmd_if_slot_base_WR(v); }
