// "Run PRG" and "Load PRG" for the CBM 8-bit machines whose BASIC lives at a start address the machine states itself (the VIC-20 and the
// 264 family, T83 and T90 on the generic application). The pieces that are not machine specific are the PRG library (app/loaders/cbm_prg.*:
// relink, pointers, typed start); what is left is the job: read and check the file, reset the machine with its settings, wait until it has
// run long enough to say READY, place the bytes, type RUN or SYS. The C64 descriptor has its own copy of this job (c64.cc, with the CPU
// address start the C64 core can do and these cores cannot); the two are not merged because the C64 is on the board and these are not.
//
// The menu holds the machine (C64_STOP) while an operation of it runs, and a held machine never reaches READY. So the operation only checks
// and resets and queues; the service (polled while the machine runs, desc.h service_op) does the rest, counting the time the machine ran.
#ifndef COREAPP_CBM_RUN_H
#define COREAPP_CBM_RUN_H

#include "ctx.h"
#include "cbm_prg.h"

struct CbmRunMachine {
    const char *tag;                                   // "vc20", for the log
    CbmPrgParams prg;                                  // BASIC starts, pointers, keyboard buffer, the size window
    uint16_t ready_ms;                                 // the least the machine runs after a reset before the first look at READY
    uint16_t settle_ms;                                // ... and after BASIC has set its start pointer, until the editor reads the buffer
    uint16_t giveup_ms;                                // after ready_ms: no READY by then, the program is dropped and the reason told
    // Optional: a reason to refuse a file before the machine is touched (the C16's 16 KB, where a program past $3FFF would wrap).
    // `load` is the file's load address, `len` the bytes behind it.
    const char *(*refuse)(CoreCtx *c, uint32_t load, uint32_t len);
    // T206: how a typed start reaches the machine. screen 0 = the keyboard buffer (prg.kbd_buffer / kbd_count, READY from the BASIC pointers).
    // screen != 0 = the key matrix (CoreIo::matrix_keys), for a machine whose CPU does not read the C64 RAM the application writes (the SuperCPU
    // reads bank 0 from its own SRAM, which mirrors only pages $02-$FF to the C64 RAM): the keys are pressed like a person's, key_hold_ms down
    // and key_gap_ms up (the KERNAL scans at 50/60 Hz and wants a key down for two scans and up between two equal characters), and READY is the
    // screen at `screen` (40x25 screen codes, the VIC reads the C64 RAM): the last row with a character on it starts with READY.
    uint16_t screen;
    uint16_t key_hold_ms, key_gap_ms;
    // T217: with prg.mem_ddr set (the program goes into the CPU's own memory through the DDR2 window), the register that invalidates the CPU's cache
    // (bit 0 starts it, reads 1 while it runs; SCPU_ROM_CTRL) - written after every placement so the CPU reads the new bytes.
    uint32_t mem_flush_reg = 0;
    // T217 D4: a machine with a second mode (the C128's C64 mode). A file whose load address is c64_load (0 = none) is started in that mode: the machine
    // is reset, comes up in its first mode (prg = its READY), `switch_text` is typed there (GO64, the answer), and when the status register mode_reg
    // shows mode_mask the C64's own READY is awaited (c64_prg: its TXTTAB, its keyboard buffer; c64_screen: its screen, c64_ready_ms: the least after
    // the switch) and the program is placed and started as on a C64.
    uint16_t c64_load = 0;
    CbmPrgParams c64_prg;
    const char *switch_text = 0;
    uint32_t mode_reg = 0;
    uint8_t mode_mask = 0;
    uint16_t c64_screen = 0, c64_ready_ms = 0;
    // ... and what the first mode shows: ask_screen = its 40-column screen (0: nothing can be read), `ask` / ask_len the screen codes of its question. The text
    // is typed when the first mode's screen shows READY (or ready_blind_ms after its TXTTAB, if the screen does not: the 80-column picture), the answer when
    // the question is on the screen (or ask_blind_ms after the first piece was taken).
    uint16_t ask_screen = 0;
    const uint8_t *ask = 0;
    uint8_t ask_len = 0;
    uint16_t ask_blind_ms = 0, ready_blind_ms = 0;
};


struct CbmRun {
    char program[56];                                  // the last program started
    int job;                                           // 0 none, 1 a run waits for READY, 2 a typed script waits for READY / its keys
    char script[64];                                   // job 2: the pieces, separated by '|' (cbm_run_queue_script)
    int script_pos;
    char name[56];
    bool p00;
    uint32_t size;
    uint32_t ran_ms, last_ms, ready_at;
    // T206, the matrix method: the key now down (kidx 0xFF none, kshift the L SHIFT with it), when it went down / came up (ran_ms), and whether the
    // line just typed ended in RETURN (the next character waits for the prompt again)
    uint8_t kidx, kphase;                               // kphase 0 no key down, 1 L SHIFT alone down, 2 the key down
    bool kshift, kwait;
    bool c64;                                          // T217 D4: job 1 starts the program in the machine's second mode (CbmRunMachine::c64_load)
    uint8_t c64_phase;                                 // 0 the first mode's READY, 1 the switch typed, 4 waiting for the question, 5 the answer typed, 2 waiting for the mode, 3 the second mode's READY
    uint32_t c64_t;                                    // ran_ms of the last step of the phases
    bool start_typed;                                  // T217: job 1 with m.screen: the program is placed, its RUN / SYS goes in through the key matrix (job 2)
    uint32_t kt;
};

// The operation's half of "Run PRG": the file is read and checked, the machine reset (boot_cold_reset: the reset-time settings go in while
// it is held), the job queued. 0 or a reason in msg; the machine is not touched when the file is no program.
int cbm_run_queue(CoreCtx *c, CbmRun *r, const CbmRunMachine &m, const char *path, char *msg, int msglen);

// The service's half, called every period while the machine runs: counts the machine's time, and with the machine at READY places the
// program and types its start. Sets a notice (told once on the next menu) when the program could not go in.
int cbm_run_service(CoreCtx *c, CbmRun *r, const CbmRunMachine &m);

// T104.1 D15, Mount & Run: reset the machine and, once it is at READY, type `text` piece by piece (at most ten keys each, the keyboard buffer;
// the next piece goes in when the machine has taken the one before). Nothing is placed in memory. 0 or a reason in msg.
int cbm_run_queue_script(CoreCtx *c, CbmRun *r, const CbmRunMachine &m, const char *text, char *msg, int msglen);

// "Load PRG": the bytes go into the machine as it is now - no reset, nothing typed. A BASIC program is relinked to the start of BASIC the
// machine states and its pointers follow, as with Run.
int cbm_load_now(CoreCtx *c, CbmRun *r, const CbmRunMachine &m, const char *path, char *msg, int msglen);

// A reset ends a program that was still waiting.
void cbm_run_clear(CbmRun *r);
const char *cbm_run_name(const CbmRun *r);             // the last program started, null when none
const char *cbm_run_pending(const CbmRun *r);          // "run" while a program waits for READY, else null

#endif
