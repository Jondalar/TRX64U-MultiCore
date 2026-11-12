// The page "Cores and ROMs" of the settings (T148 D7, T192.2 P1/P4, T192 D23) as the core holds it: the scan (coremaint.cc does the reading and the removing), the lines of the
// list with their tones, the two questions, the result of the last cleanup. The screen draws the lines (hc_cm_line) and the head, foot and note it is given. The page's mode and
// its first visible line are the navigation state (host_core_nav.h: cm_mode, cm_top). No LVGL, no FreeRTOS.
#ifndef HOST_HOSTCORE_CM_H
#define HOST_HOSTCORE_CM_H

#include "host_core_nav.h"
#include "host_core_flow.h"
#include "coremaint.h"

#define HC_CM_LINE 112

// The board's side: the file system views the scan reads and the environment of the delete, and the question for the service (text empty: gone).
struct HcCmEnv {
    CmFs *cm;
    FoFs *fo;
    const HcFlowEnv *flow;
    void (*ask)(void *ctx, const char *text, int yes_key, int no_key);
    void *ctx;
};

void hc_cm_open(const HcCmEnv &e);                  // the scan, the list, the cursor on the top
void hc_cm_close(void);
void hc_cm_key(const HcCmEnv &e, int act);          // one intent of the page (UP / DOWN scroll, ESC closes, DEL asks, Y answers yes, anything else is No); the question for the service follows
int hc_cm_lines(void);                              // the lines of the page now
const char *hc_cm_line(int i, int *tone);           // line i and its HcTone
void hc_cm_clamp_top(void);                         // the first visible line stays inside the list
int hc_cm_ndelete(void);                            // entries the cleanup would delete
void hc_cm_head(char *out, int cap, int *tone);     // the head line: what it says now (the question while it asks), and its tone
const char *hc_cm_foot(void);                       // the footer of the page now
const char *hc_cm_msg(int *tone);                   // the result of the last cleanup
void hc_cm_ask_sync(const HcCmEnv &e);              // the question the page asks, for the service

#endif
