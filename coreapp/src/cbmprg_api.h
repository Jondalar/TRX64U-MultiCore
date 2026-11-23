// The export "cbmprg" (major 1): the CBM BASIC program loader of this repository (app/loaders/cbm_prg.cc), a shared library of T239.3 D12 (advisor 2026-11-23).
// The VC-20, C16/Plus 4 and PET (cbm_run), the C128, the SuperCPU and the C64 place, relink and start programs with the same code; a core's library finds it with
// SvcLib::find("cbmprg", U2A3_VERSION(1, 0), &size). It is not a member of LibApi.
//
// Why no CoreIo: the functions of cbm_prg.h take one; behind this table they act on the memory of the running core (its io), so no call here takes it.
//
// Rules: `size` first, in the table and in CbmPrgMachine; members only appended inside a major. A library tests `size` before it calls a member added later. Errors: a
// result of int is 0 for done and non-zero with the reason in msg (cut to msglen - 1). Task: lock for every call that touches the machine's memory (place,
// type_start, script_step, mem_write, mem_read); any for the others.
#ifndef COREAPP_CBMPRG_API_H
#define COREAPP_CBMPRG_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CBMPRG_API_NAME  "cbmprg"
#define CBMPRG_API_MAJOR 1
#define CBMPRG_BASIC_STARTS_MAX 4

// The numbers of one machine (CbmPrgParams of cbm_prg.h); `size` is sizeof(CbmPrgMachine) of the caller.
typedef struct CbmPrgMachine {
    uint32_t size;
    // A load address equal to one of these means "a BASIC program": it is relinked to the machine's own start of BASIC and started with RUN. Anything else is a
    // machine-language file and gets SYS.
    uint16_t basic_starts[CBMPRG_BASIC_STARTS_MAX];
    uint8_t nbasic_starts;
    uint16_t txttab;                      // the pointer pair that holds the start of BASIC ($2B on the C64)
    uint16_t txttab_min, txttab_max;      // the window its value has to fall in for the machine to be believed
    uint16_t vartab, arytab, strend;      // the three pointers that follow the program
    uint16_t kbd_buffer;                  // where typed keys go ($0277 on the C64)
    uint16_t kbd_count;                   // the byte that says how many ($C6 on the C64)
    uint16_t ready_ms;                    // how long the machine needs after a reset before it reads the buffer
    uint16_t p00_header;                  // bytes a .P00 carries before the load address (0x1A)
    uint32_t min_size, max_size;          // a file outside this is not a program
    uint32_t mem_ddr;                     // 0: the RAM window; else the DDR2 window at mem_ddr + address as well (a CPU that keeps its RAM there, T217)
} CbmPrgMachine;

// Where a placed program went.
typedef struct CbmPrgPlaced {
    uint16_t load;                        // its first byte
    uint16_t end;                         // the first byte after it
    bool basic;                           // it was relinked and is started with RUN
    uint16_t start;                       // the address SYS uses (0 for a BASIC program)
} CbmPrgPlaced;

typedef struct CbmPrgApi {
    uint32_t size;
    // Writes the program into the machine's memory, relinked when it is BASIC. `data` is the file as on the medium, with its two load-address bytes and, for a .P00
    // (is_p00), its header. 0, or the reason. Task: lock.
    int (*place)(const uint8_t *data, uint32_t n, const CbmPrgMachine *m, bool is_p00, CbmPrgPlaced *out, char *msg, int msglen);
    // Types "RUN" and RETURN for a BASIC program, "SYS<start>" and RETURN otherwise, into the keyboard buffer. Call it once the machine is at READY (ready_ms after
    // its reset). Task: lock.
    void (*type_start)(const CbmPrgMachine *m, const CbmPrgPlaced *r);
    // A typed script (Mount & Run): pieces separated by '|', each at most ten keys. 0, or the reason. Task: any.
    int (*script_check)(const char *text, char *msg, int msglen);
    // Types the next piece of the script at *pos when the machine has taken the one before (its key count is 0). 1 when the whole script is in and taken, 0 while
    // pieces are left or the machine is busy. Task: lock.
    int (*script_step)(const CbmPrgMachine *m, const char *script, int *pos);
    // The relink of a BASIC program in a buffer: every line's link is rewritten for `to`; returns the address after the program. Task: any.
    uint16_t (*relink)(uint8_t *prog, uint32_t n, uint16_t from, uint16_t to);
    // The key of a PETSCII character on the C64 keyboard: *idx = column * 8 + row, *shift = left SHIFT goes down with it. False: no key types it. Task: any.
    bool (*key_for)(uint8_t petscii, uint8_t *idx, bool *shift);
    // The machine's own memory, through the RAM window, or the DDR2 window as m->mem_ddr says. Task: lock.
    void (*mem_write)(const CbmPrgMachine *m, uint16_t addr, const void *data, uint32_t n);
    void (*mem_read)(const CbmPrgMachine *m, uint16_t addr, void *data, uint32_t n);
} CbmPrgApi;

#ifdef __cplusplus
}
#endif

#endif
