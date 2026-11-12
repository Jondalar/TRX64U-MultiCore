/*
 * mon.h — the TRX64 monitor verbs trxmon answers (monitor/exec). Syntax,
 * output and error sentences are TRX64's (crates/trx64-monitor/src/verbs.rs,
 * crates/trx64-daemon/src/main.rs run control, MONITOR.md); a verb the
 * hardware cannot answer says so ("not available on this host").
 */
#ifndef TRXMON_MON_H
#define TRXMON_MON_H

#include "json.h"
#include "mach.h"

enum { MON_OK = 0, MON_ERR = 1, MON_PENDING = 2 };
enum { PK_NONE = 0, PK_UNTIL, PK_NEXT, PK_RET };

/* A run-to verb that answers when the machine stops */
typedef struct
{
    int kind;
    uint16_t addr;   /* until */
    uint64_t cyc0;   /* cycles when it started */
    uint32_t instr0; /* instructions when it started */
    uint32_t r_cyc;  /* next: the JSR's own cycles */
} mon_pending_t;

void mon_init(void);
/* Runs one command line. The text (with span marks) goes to out. */
int mon_exec(const char *cmd, sbuf_t *out, mon_pending_t *pend);
/* The text of a pending verb once the machine stopped (capped: it was paused
 * because it took too long). */
void mon_finish(const mon_pending_t *p, int capped, sbuf_t *out);
/* `machine`: the device and the banking state */
void mon_machine_json(jw_t *w);
/* TRX64's register_dump: "  ADDR AC XR YR SP NV-BDIZC\n.;PPPP AA XX YY SS flags" */
void mon_register_dump(sbuf_t *out);
/* The overlay: m prints 8 bytes a row, d drops the raw bytes when a line is too long.
 * Only the screen's own lines set it (T21 §8.2); RPC always gets TRX64's text. */
void mon_set_narrow(int on);
/* How wide the screen is (40 or 80) and how many rows one `m` or `d` fills there (0: TRX64's own
 * defaults). At 80 columns `m` prints 16 bytes and the PETSCII, `d` the ".C:" labels and a '>' at the PC
 * (T21 §8.10). Applies while mon_set_narrow(1). */
void mon_set_screen(int cols, int page_rows);
/* The text for a stop no verb was waiting for (breakpoint, observer, pause) */
void mon_stop_text(sbuf_t *out);
/* `/quit` was given */
int mon_quit_requested(void);
/* Completion (CTRL+I / TAB on the screen, T55): the word that ends at line[len] starts at *start. The candidates
 * (whole words; directories end in '/') are written to out, one per line. Verbs first, then observer names, marks,
 * lens words, and paths for the file verbs. Returns how many. */
int mon_complete(const char *line, int len, int *start, sbuf_t *out);
/* 1 while `a <addr>` assemble mode (or a block) takes the lines: an empty line ends it */
int mon_asm_mode(void);
/* 1 and the next address while `a` assembles line by line (not in a block): the screen prefills its prompt
 * with "a <addr> " (VICE style, T21 8.3) */
int mon_asm_next(uint16_t *addr);

#endif
