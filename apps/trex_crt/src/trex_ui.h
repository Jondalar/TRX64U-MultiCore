/*
 * trex_ui.h - what the tools of TREX CRT publish for their screens (T104.3 D0, D9.1), and how the screens reach them.
 *
 * The three tools were written to draw a 40 x 24 character screen and to ask questions in dialogs. They still ask in dialogs (the base's, blocking the
 * application's task); what they drew is now this model: plain strings that glue.cc offers as state values (`cart.*`) and the description (app.toml) binds
 * its screens to. Everything here is written by the application's own task and read by the base's menu task; a string that is read while it is written is
 * at worst one frame out of date.
 */
#ifndef TREX_UI_H
#define TREX_UI_H

#include "ui_shim.h"

#define UI_MAP_MAX 256

typedef struct {
    // what is in the port, and the tool that looks at it
    char type[40];                 // "EasyFlash 3"
    char chip[40];                 // the state chip of the screen: the type in capitals, or why there is none
    char banks_chip[24];           // "64 BANKS"
    char slot[80];                 // "Slot 2: Sam's Journey", "All slots, system area first", "No slot selected"
    char slot_short[40];           // "2 / SAM'S JOURNEY"
    char target[140];              // "EasyFlash 3 / Slot 2 / Sam's Journey", or the type
    char port[20];                 // ENABLED, NOT ARMED, NO CARTRIDGE
    char gate[20];                 // the write gate: SAFE, OPEN
    char is_ef3[4];                // "1" on an EasyFlash 3: the slot and the whole-cartridge erase belong to it
    char not_ef3[4];               // "1" when it is not
    // files
    char dest[200];                // where a dump goes
    char file[80];                 // the CRT's name, as the old screen showed it
    char src[200];                 // what the writer writes: the CRT file
    char src_type[48];             // "EASYFLASH CRT", "TYPE 90 IMAGE", ...
    char src_name[40];             // the CRT's own name
    // what the screen says
    char status[128];
    char title[64];                // the title of the progress and result screens
    char phase[110];               // "PROGRAMMING ROML BANK $1B"
    char count[32];                // "27 / 64"
    char pct[8];                   // 0 .. 100
    char note[180];                // the caption under the bar
    char lock[180];                // not empty: RUN/STOP does not close while it is (the reason)
    char screen[28];               // the screen the application wants shown: "progress#3", "result#4": the id, and a number that makes it new
    char map_lo[UI_MAP_MAX + 1], map_hi[UI_MAP_MAX + 1];       // one digit for each bank: the BANK_* states
    char map_lo_d[40], map_hi_d[40];
    char confirm[160];             // the write confirmation: what goes
    char scope[220];               // ... and what it erases first
    char res_prog[80], res_verify[80], res_extra[120];
    // the inspector
    char grid[1500];               // the memory view: one line to a row, a leading ~ dims it
    char i_type[40], i_slot[16], i_bank[16], i_cpu[16], i_de00[16], i_pins[32];
    // not state values: what the helpers keep
    int banks_lo, banks_hi, has_romh;
    int op_total, op_done;
    int screen_seq;
} trex_ui_t;

extern trex_ui_t g_ui;

void ui_set(char *dst, int cap, const char *fmt, ...);
#define UI_SET(field, ...) ui_set(g_ui.field, (int)sizeof(g_ui.field), __VA_ARGS__)

// A step of an operation (a scan, a dump, a write, a verify): the bar and its caption. total banks; step() moves it on by one.
void ui_op_begin(const char *title, const char *phase, int total);
void ui_op_step(void);
void ui_op_phase(const char *phase);
void ui_op_end(void);
// The screen the application wants shown now ("progress", "result", "flash", ...), and the lock on RUN/STOP.
void ui_show(const char *id);
void ui_lock(const char *reason);
// True when the base asked the application to end or the person closed the screens: a long loop leaves at the next bank.
bool ui_cancelled(void);

// The tools, for the task that serves the screens (trex_main.c)
enum { TOOL_NONE = 0, TOOL_DUMP, TOOL_FLASH, TOOL_INSPECT };
// What the operations of the screens ask of that task: the tool's screen is shown, a key of the tool, the dump's destination, the flash source
enum { CMD_NONE = 0, CMD_ENTER, CMD_KEY, CMD_DEST, CMD_SOURCE };
bool trex_post(int kind, int tool, int key);       // false: the queue is full
int trex_queued(void);
int trex_main(const u2p_api_t *api, const char *action, const char *file);

#endif
