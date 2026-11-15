// The PET models of the core app (T134 section 3a Q2, Q12): which of VICE's ROM files a model takes, and the machine's numbers the PRG loader needs.
// Header only, no register access: tests/coreapp/test_pet_models.cc includes it. The model index is the core's MODEL register (pet_regs.h).
#ifndef COREAPP_PET_MODELS_H
#define COREAPP_PET_MODELS_H

#include "cbm_prg.h"
#include <string.h>

// PET_M_2001B2 (T216 P8) is a 2001 with the BASIC 2 ROM set (BASIC 2 + KERNAL 2 + editor 2, the 3032's); the core's MODEL register gets 0 (pet_hw_model).
enum { PET_M_2001 = 0, PET_M_3032, PET_M_4032, PET_M_8032, PET_M_8096, PET_M_SUPERPET, PET_M_2001B2 };
enum { PET_R_BASIC = 0, PET_R_EDITOR, PET_R_KERNAL, PET_R_CHAR };

static inline int pet_basic_gen(int model)          // the BASIC generation: 1 (2001), 2 (3032), 4 (4032 and the 80 column models)
{
    return model == PET_M_2001 ? 1 : (model == PET_M_3032 || model == PET_M_2001B2) ? 2 : 4;
}
static inline bool pet_crtc(int model) { return model >= PET_M_8032 && model <= PET_M_SUPERPET; }
static inline int pet_hw_model(int model) { return model == PET_M_2001B2 ? PET_M_2001 : model; }   // the board the core builds: the 2001's

// The file names of a role for a model, best first, 0-terminated (at most 5): VICE's names of THAT model only (T216 P6, Alex 2026-11-15: a missing ROM of the
// model is refused and named, no generic or other model's file stands in). `hz50`: the 50 Hz editor (the picture rate setting). The 8032 and 8096 take
// the 4 KiB characters.901640-01 (its first 2 KiB are characters-2) or characters-2; the SuperPET only the 4 KiB file, its upper half is the Waterloo set.
static inline int pet_rom_names(int model, bool hz50, int role, const char *out[6])
{
    int n = 0;
    int gen = pet_basic_gen(model);
    switch (role) {
    case PET_R_BASIC:
        out[n++] = gen == 1 ? "basic-1.901439-09-05-02-06.bin" : gen == 2 ? "basic-2.901465-01-02.bin" : "basic-4.901465-23-20-21.bin";
        break;
    case PET_R_EDITOR:
        if (model == PET_M_2001)                                   out[n++] = "edit-1-n.901439-03.bin";
        else if (model == PET_M_3032 || model == PET_M_2001B2)    out[n++] = "edit-2-n.901447-24.bin";
        else if (model == PET_M_4032)                              out[n++] = hz50 ? "edit-4-40-n-50Hz.901498-01.bin" : "edit-4-40-n-60Hz.901499-01.bin";
        else                                                       out[n++] = hz50 ? "edit-4-80-b-50Hz.901474-04.bin" : "edit-4-80-b-60Hz.901474-03.bin";
        if (pet_crtc(model) && hz50) out[n++] = "edit-4-80-b-50Hz.901474-04_.bin";
        break;
    case PET_R_KERNAL:
        out[n++] = gen == 1 ? "kernal-1.901439-04-07.bin" : gen == 2 ? "kernal-2.901465-03.bin" : "kernal-4.901465-22.bin";
        break;
    default:
        if (model == PET_M_SUPERPET) {
            out[n++] = "characters.901640-01.bin";
        } else {
            if (model == PET_M_8032 || model == PET_M_8096) out[n++] = "characters.901640-01.bin";
            out[n++] = (model == PET_M_2001 || model == PET_M_2001B2) ? "characters-1.901447-08.bin" : "characters-2.901447-10.bin";
        }
        break;
    }
    out[n] = 0;
    return n;
}

// The loader's numbers. BASIC 2 and 4: text at $0401, the pointer pairs at $28 (start), $2A, $2C, $2E, keyboard buffer $026F with its count at $9E.
// BASIC 1 (the 2001): text at $0400, the pairs at $7A (start), $7C, $7E, $80; the KERNAL 1 keyboard buffer is $0270 with its count at $020D.
// (Read from kernal-1 and basic-1 of VICE's set: BASIC 1 stores to $7A..$80, the KERNAL stores into $0270,X and counts at $020D.)
static inline CbmPrgParams pet_prg_params(int model)
{
    CbmPrgParams p;
    memset(&p, 0, sizeof(p));
    bool b1 = pet_basic_gen(model) == 1;
    p.basic_starts[0] = b1 ? 0x0400 : 0x0401;
    p.nbasic_starts = 1;
    p.txttab = b1 ? 0x7A : 0x28;
    p.txttab_min = 0x0400;
    p.txttab_max = 0x2000;
    p.vartab = b1 ? 0x7C : 0x2A; p.arytab = b1 ? 0x7E : 0x2C; p.strend = b1 ? 0x80 : 0x2E;
    p.kbd_buffer = b1 ? 0x0270 : 0x026F;
    p.kbd_count = b1 ? 0x020D : 0x9E;
    p.p00_header = 0x1A;
    p.min_size = 3;
    p.max_size = 0x1A + 2 + 0x8000;
    return p;
}

#endif
