// T252: the I2C switches. The behaviours T242 changed (the P7 keyboard scanner runs, no wait after it stops, no keyboard expander writes) and the screen
// check (picture_service, the one periodic I2C user) as settings rows a person changes live on the board, to find the one that breaks the picture.
// Generic (T192 D24): a core whose capability list names I2CSW_CAP gets the rows after its own settings (i2csw_extend, called by menu_build) and their
// operations (i2csw_register, called by ops_of and desc_check). Each row is applied when it changes and at the core's start; the defaults are T242's.
#ifndef COREAPP_I2CSW_H
#define COREAPP_I2CSW_H

#include "desc.h"
#include "ctx.h"

#define I2CSW_CAP "i2c_switches"
#define I2CSW_N 4                              // scanner, screen check, wait after the scanner stops, keyboard expander setup

#define I2CSW_KEY_SCAN "P7 keyboard scanner"
#define I2CSW_KEY_POLL "Screen check every 500 ms"
#define I2CSW_KEY_WAIT "Wait after scanner stop"
#define I2CSW_KEY_EXP  "Keyboard expander setup"

#define I2CSW_OP_SCAN "i2c.scan"
#define I2CSW_OP_POLL "i2c.screen_check"
#define I2CSW_OP_WAIT "i2c.park_wait"
#define I2CSW_OP_EXP  "i2c.expander"

// out = *d, with the four rows after d's own settings when d names the capability. `store` holds the merged array (SETTINGS_MAX items, as the other
// extend functions') and has to outlive `out`. Returns null, or the reason when there is no room.
const char *i2csw_extend(const CoreDesc *d, CoreDesc *out, Setting *store);

// The operations behind the rows. For a core with the capability only.
void i2csw_register(OpRegistry &r);

// What the rows do on the board (the I2C driver); the native builds get functions that do nothing, a test puts its own in. False: the I2C lock was not
// had (another task held it past its timeout), nothing was done.
struct I2cSwHw {
    bool (*scan)(bool on);                     // on: the scanner has the buses between the application's transactions (T242 D1); off: parked, as before T242
    bool (*park_wait)(int ticks);              // the sleep after scan_enable = 0 while the scanner runs: 0 (T242 D4), 2 (PR #112: the firmware's driver)
    bool (*expander)(int which);               // 1: the init's three writes before T242; 2: those and then PR #112's two (both ports input); 0: none (T242 D5)
};
extern I2cSwHw i2csw_hw;

#endif
