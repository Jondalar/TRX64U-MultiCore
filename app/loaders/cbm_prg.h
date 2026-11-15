// The CBM BASIC program loader, as a library (T104 5.6, item 1).
//
// The VC-20 and the C16 each carry their own copy of it today (vc20.cc:264-371, c16.cc:462-538) and the two
// differ in ten numbers: the BASIC start addresses, the pointer pair that holds the start of BASIC and the
// window its value has to fall in, the three end pointers, the keyboard buffer and its count byte, how long
// READY. takes, the header a .P00 carries and the largest file that can be meant. The code between them -
// the relink of every line, the pointer writes, the keys typed into the buffer - is byte-identical, which is
// what makes it library code rather than a per-machine hook.
//
// Nothing here talks to a core: it writes through CoreIo's RAM window, so it runs natively in a test against
// a memory model.
#ifndef TRX_CBM_PRG_H
#define TRX_CBM_PRG_H

#include <stdint.h>
#include "io.h"

#define CBM_BASIC_STARTS_MAX 4

struct CbmPrgParams {
    // A load address equal to one of these means "a BASIC program": it is relinked to the machine's own
    // start of BASIC and started with RUN. Anything else is a machine-language file and gets SYS.
    uint16_t basic_starts[CBM_BASIC_STARTS_MAX];
    uint8_t nbasic_starts;
    uint16_t txttab;             // the pointer pair that holds the start of BASIC ($2B on the VC-20 and the C16)
    uint16_t txttab_min;         // the window its value has to fall in for the machine to be believed
    uint16_t txttab_max;
    uint16_t vartab, arytab, strend;   // the three pointers that follow the program
    uint16_t kbd_buffer;         // where typed keys go ($0277 / $0527)
    uint16_t kbd_count;          // ... and the byte that says how many ($C6 / $EF)
    uint16_t ready_ms;           // how long the machine needs after a reset before it reads the buffer
    uint16_t p00_header;         // bytes a .P00 carries before the load address (0x1A)
    uint32_t min_size, max_size; // a file outside this is not a program
    // T217: a machine whose CPU keeps its RAM where the application's RAM window does not reach (the SuperCPU: the 65816's bank 0 is its own SRAM in the DDR2,
    // the window reaches the C64 RAM that only the pages $02-$FF mirror, and only from the CPU to the window, never back). mem_ddr != 0: every byte the
    // loader places or reads goes to the DDR2 window at mem_ddr + address (and to the RAM window too, so the C64 side shows it); the caller then
    // invalidates the CPU's cache (cbm_run.cc mem_sync).
    uint32_t mem_ddr;
};

// The machine's own memory (T217): through the RAM window, or through the DDR2 window when p.mem_ddr says that is where its CPU looks.
void cbm_prg_mem_write(CoreIo *io, const CbmPrgParams &p, uint16_t addr, const void *data, uint32_t n);
void cbm_prg_mem_read(CoreIo *io, const CbmPrgParams &p, uint16_t addr, void *data, uint32_t n);

enum CbmPrgMode { CBM_PRG_LOAD = 0, CBM_PRG_RUN = 1 };

struct CbmPrgResult {
    uint16_t load;               // where it went
    uint16_t end;                // first byte after it
    bool basic;                  // it was relinked and will be started with RUN
    uint16_t start;              // the address SYS would use (0 for a BASIC program)
};

// Writes the program into the machine's memory through `io`, relinking it when it is BASIC. `data` is the
// file as it is on the medium, including its two load-address bytes and, for a .P00, its header.
// Returns 0, or a reason in `msg`.
int cbm_prg_place(CoreIo *io, const uint8_t *data, uint32_t size, const CbmPrgParams &p, bool is_p00,
                  CbmPrgResult *out, char *msg, int msglen);

// Types a line into the machine's keyboard buffer: "RUN\r" for a BASIC program, "SYS<addr>\r" otherwise.
// Call it once the machine is at READY. (the caller waits `ready_ms` after its reset).
void cbm_prg_type_start(CoreIo *io, const CbmPrgParams &p, const CbmPrgResult &r);

// T104.1 D15, a typed script (Mount & Run): pieces separated by '|', each at most ten keys - the keyboard buffer. 0, or a reason in msg.
int cbm_prg_script_check(const char *text, char *msg, int msglen);
// Types the next piece of the script at *pos when the machine has taken the one before (the key count is 0). Returns 1 when the whole script
// is in (and taken), 0 while pieces are left or the machine is busy.
int cbm_prg_script_step(CoreIo *io, const CbmPrgParams &p, const char *script, int *pos);

// The relink itself, on a buffer: every line's link is rewritten for `to`, and the address after the
// program is returned. Public because the test drives it directly.
uint16_t cbm_prg_relink(uint8_t *prog, uint32_t n, uint16_t from, uint16_t to);

#endif
