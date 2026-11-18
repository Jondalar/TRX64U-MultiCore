// reSID8 (the 8-SID reDIP of our bitstream): what the menu's filter, resonance and digi choices mean for the
// hardware. Pure data and one function, so that the menu has one path to the registers and so that it runs
// in a host test (software/test/resid8) without menu code.
// TRX64-Ultimate docs/specs/T25.5-resid8-menus.md section 3.
#ifndef RESID8_FILTER_H
#define RESID8_FILTER_H

#include <stdint.h>

#define RS8_N_FILTERS     8   // Dag Lem's curves only (Alex 2026-10-07); no loadable curve table in the hardware
#define RS8_FILTER_CUSTOM 6   // "6581 Custom": base and offset come from their items

// The source in the hardware (trx_io RS8_FCSEL)
#define RS8_SRC_MODEL     0   // the chip model's own filter (6581: DAC curve, 8580: 5 * FC): the original reDIP
#define RS8_SRC_6581      1   // reDIP's 6581 curve with base and offset
#define RS8_SRC_8580      2   // 5 * FC whatever the chip model
                              // 3 is reserved in the hardware (acts as 2), never written

struct Resid8Preset {
    uint8_t src;
    int16_t base, offset;     // own 6581 curve; the reDIP defaults where they do not matter
};

// In the order of the "Filter Curve" values of the reSID8 menu.
static const Resid8Preset resid8_presets[RS8_N_FILTERS] = {
    /*  0 Chip Model   */ { RS8_SRC_MODEL, 245, -780 },
    /*  1 6581 Follin  */ { RS8_SRC_6581, 245, -780 },
    /*  2 6581 Galway  */ { RS8_SRC_6581, 280, -405 },
    /*  3 6581 Average */ { RS8_SRC_6581, 250,    0 },
    /*  4 6581 Strong  */ { RS8_SRC_6581, 260,  400 },
    /*  5 6581 Extreme */ { RS8_SRC_6581, 200,  760 },
    /*  6 6581 Custom  */ { RS8_SRC_6581, 245, -780 },
    /*  7 8580         */ { RS8_SRC_8580, 245, -780 },
};

struct Resid8Plan {
    uint8_t src;
    int base, offset;
    uint8_t auto_bits;        // b0 resonance Auto, b1 digis Auto
    uint8_t res_reg;          // the UltiSID RES register (0 Low, 1 High)
    uint8_t digi_reg;         // the UltiSID DIGI register (0 Off .. 3 High)
    uint8_t bypass;           // 1: filter Off = reSID's enable_filter(false), the hardware bypasses the filter
};

// filter: index in the list above; res: 0 Auto, 1 Low, 2 High; digi: 0 Auto, 1 Off .. 4 High;
// filter_on: the "Filter" item, 0 Off (bypass), 1 On.
static inline Resid8Plan resid8_plan(int filter, int res, int digi, int custom_base, int custom_offset, int filter_on = 1)
{
    if (filter < 0 || filter >= RS8_N_FILTERS) {
        filter = 0;
    }
    const Resid8Preset &p = resid8_presets[filter];
    Resid8Plan r;
    r.src = p.src;
    r.base = (filter == RS8_FILTER_CUSTOM) ? custom_base : p.base;
    r.offset = (filter == RS8_FILTER_CUSTOM) ? custom_offset : p.offset;
    r.auto_bits = uint8_t((res == 0 ? 1 : 0) | (digi == 0 ? 2 : 0));
    r.res_reg = (res == 2) ? 1 : 0;
    r.digi_reg = (digi > 0) ? uint8_t(digi - 1) : 2;
    r.bypass = filter_on ? 0 : 1;
    return r;
}

#endif
