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

void uci_cart_slot(const CoreCtx *c)
{
    if (uci_slot_present(c)) {
        uci_slot_write_enable(DRV_cmd_if_slot_enable_on);
        uci_slot_write_base(DRV_cmd_if_slot_base_df1c);
    }
}
