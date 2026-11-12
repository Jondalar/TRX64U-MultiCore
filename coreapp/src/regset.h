// Settings that are bits of a register byte (T104 5.2 "setting, extended" and `reset_write`). Several settings can share one byte
// (the Amiga's MEM_CFG: chip [1:0] + slow [3:2]; FLOPPY_CFG: write protect [1] + drives - 1 [3:2]); a value maps to a byte through a
// table (CHIPSET_CFG 0x00/0x08/0x10); and some bytes are written with every commit that no setting owns (CPU_CFG, IDE_CFG). The byte
// written is always computed from all the settings that name the register, never read back, because those registers are write-only
// commits (a write to one also copies the whole configuration into the core). Pure: the core is a CoreIo.
#ifndef COREAPP_REGSET_H
#define COREAPP_REGSET_H

#include "ctx.h"

// The byte setting `i` contributes with its current value (no other setting).
uint8_t regset_part(const CoreCtx *c, int i);

// The byte of register `reg`: the OR of what every setting bound to it contributes.
uint8_t regset_value(const CoreCtx *c, uint16_t reg);

// Writes register `reg` with that byte.
void regset_commit(CoreCtx *c, uint16_t reg);

// Applies setting `i`: through its operation, or by committing its register. A setting that only chooses the rom_set member does nothing here.
void regset_apply_setting(CoreCtx *c, int i);

// A settings commit: every setting of the descriptor whose `when` is WHEN_RESET (or every one when all), each register once, then the fixed
// writes. The core must be held for the reset-time items.
void regset_apply(CoreCtx *c, bool all);

#endif
