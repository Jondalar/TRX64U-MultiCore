// The host application's flows as the core runs them (T192.2 P2, T192 D23): a flow is a sequence of pages (data) with questions, progress and a result, run by the core
// against an environment (HcFlowEnv) that draws a page, reads a key and reaches the board. The pages are data (HcPage: the lines with tones, the hint, the bar), so
// the screen draws them and the service tells them, and a bench runs the flow with no screen at all. No LVGL, no FreeRTOS, no key wait of its own.
#ifndef HOST_HOSTCORE_FLOW_H
#define HOST_HOSTCORE_FLOW_H

#include <stdint.h>
#include "host_core.h"
#include "hostfs.h"
#include "flashfw.h"
#include "txu.h"

enum HcTone { HCT_NONE = 0, HCT_INK, HCT_MUTED, HCT_OK, HCT_WARN, HCT_BAD, HCT_HOST };      // HCT_NONE: not set (an empty line, no message)

#define HC_PAGE_LINES 8
struct HcPage {
    char title[72];
    uint8_t border;                  // HcTone
    char line[HC_PAGE_LINES][124];
    uint8_t lcol[HC_PAGE_LINES];     // HcTone
    bool input;
    char typed[52];
    char msg[124];
    uint8_t mcol;
    int bar;                         // -1 = hidden
    char hint[124];                  // the page's keys: the footer says them while the page is up
    char pct[16];                    // "37 %", right-aligned on the last line (the one progress number, beside what is being done)
};

void hp_page(HcPage *pg, int border, const char *title);                 // a page with a frame colour and a title, no bar
void hp_line(HcPage *pg, int i, int tone, const char *text);

// The environment of a flow: the screen's side and the board's side. Every function gets `ctx`.
struct HcFlowEnv {
    void (*draw)(void *ctx, const HcPage &pg, bool force);               // draws the page and tells the service; force: even when it was drawn a moment ago
    int (*getch_nowait)(void *ctx);                                      // the next key, -1 none
    int (*getch)(void *ctx);                                             // one key, or -1 after the service ran once (callers loop)
    void (*idle)(void *ctx);                                             // between keys: the service runs
    void (*sleep_ms)(void *ctx, int ms);
    void (*hook)(void *ctx, int phase, uint64_t a, uint64_t b);          // the bench presses a key or takes a picture at a point of the flow (null: none)
    void (*note)(void *ctx, const char *text, int tone);                 // the line at the bottom of the screen
    void (*refused)(void *ctx, const char *first_line);                  // a refusal page was drawn (the bench reads the reason)
    void (*write_failed)(void *ctx, const char *why);                    // a write failed (the bench reads the reason)
    void (*log)(void *ctx, const char *line);                            // the console line ("[host] ...")
    uint32_t (*now)(void *ctx);
    Flash *(*flash)(void *ctx);
    uint8_t *(*bit_buf)(void *ctx);                                      // where the host bitstream waits (DDR2 on the board)
    uint8_t *(*app_buf)(void *ctx);
    CorSource *(*open)(void *ctx, const char *path);                     // null: cannot open; the caller deletes it
    int (*list)(void *ctx, const char *dir, HostDirEntry *out, int max, bool only_bundles);
    bool (*power_cycle)(void *ctx, const char **why);                    // the ESP32 control module restarts the board
    void *ctx;
};

bool hc_is_zip_name(const char *name);                    // T197: ".zip" in any case (host_core_flow_zip.cc)

// ---- the MultiCore update from a .txu (T152 D3/D5): refuse pages, the check, the questions, the write, the result ---------------------------------------------
// Returns true when something was written and verified. Every refusal is a page of its own and returns false.
bool hc_update_run(const HcFlowEnv &env, const char *path);
class HcJob;
HcJob *hc_update_job(const HcFlowEnv &env, const char *path);     // the same as a job: the caller steps it (host_core_job.h) and deletes it
int hc_update_writes(void);                  // writes started (the bench's counter)

#endif
