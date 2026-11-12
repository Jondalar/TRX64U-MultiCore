// The File Browser's file operations as the core runs them (T139 D1/D2, T192.2 P2, T192 D23): delete, copy and move with their checks, the target-folder pick, the
// question "replace the file that is there?", and the add / recheck of a boot menu entry. The rules are fileops.cc's; this file holds the state of the pick and the decisions,
// and says what happened as a note (text + tone) through the environment. The screen keeps the browser's rows, the mode of the browser and the arm of the destructive
// keys (T192 D21: the service owns the question). No LVGL, no FreeRTOS, no key wait of its own.
#ifndef HOST_HOSTCORE_FLOW_FILEOPS_H
#define HOST_HOSTCORE_FLOW_FILEOPS_H

#include <stdint.h>
#include "host_core_flow.h"
#include "fileops.h"
#include "cor.h"
#include "cfw.h"
#include "coremaint.h"

// The screen's side of the file operations. Every function gets `ctx`; the notes go through base->note (tone = HcTone).
struct HcFoEnv {
    const HcFlowEnv *base;
    uint8_t *buf;                                                        // the block buffer of copy and move
    uint32_t buf_size;
    bool (*scope_begin)(void *ctx, const char *kind, const char *title, bool cancelable);   // the flow the service tells (returns whether this call opened it)
    void (*scope_end)(void *ctx, bool own);
    void (*progress)(void *ctx, int pct, const char *label, const char *text);   // text null: the line is not due; else the note line to draw at once
    void (*busy)(void *ctx, const char *text);                           // a note, drawn now, before a long check
    void (*pick_end)(void *ctx);                                         // the browser comes back from the target pick
    void (*mark)(void *ctx, int entry);                                  // the menu entry is read again
    void (*after_add)(void *ctx, int added, const char *path);           // an entry was added (HcResult of the add): the screen follows it
    void (*reload)(void *ctx);                                           // the browser reads its folder again
    void (*ask)(void *ctx, const char *text);                            // the question for the service (RETURN yes, ESC no); null text: gone
    void *ctx;
};

// One row of the browser as the checks need it: valid is false at the media level or outside the list.
struct HcFoRow {
    bool valid;
    const char *dir;                 // the folder the browser shows
    const char *name;
    bool is_dir;
};

// ---- the target pick (copy / move) ----
bool hc_fo_pick_start(const HcFoEnv &e, bool move, bool at_media, const char *sel_path, const char *bpath, int bsel);   // false: refused, the note says why
void hc_fo_pick_confirm(const HcFoEnv &e, bool at_media, const char *bpath);       // RETURN on the folder the browser shows
bool hc_fo_pick_move(void);                                                         // the pick copies or moves
const char *hc_fo_pick_back(void);                                                  // the folder the browser comes back to
int hc_fo_pick_bsel(void);                                                          // ... and the row
bool hc_fo_replace_pending(void);                                                   // the question "replace?" waits
void hc_fo_replace_drop(void);                                                      // the question is answered or gone
void hc_fo_replace_run(const HcFoEnv &e);                                           // the answer is yes

// ---- delete ----
// 0: go on (`path` is the row's); 1: nothing to do, the key says nothing; 2: refused, `*warn` is the note.
int hc_fo_delete_check(const HcFoRow &row, char *path, int cap, const char **warn);
void hc_fo_delete_arm_text(const HcFoRow &row, const char *path, char *out, int cap);     // what the first DEL says
void hc_fo_delete_run(const HcFoEnv &e, const HcFoRow &row, const char *path);            // the second DEL

// ---- add and recheck an entry of the boot menu ----
void hc_fo_refuse(cor_result_t r, const CorInfo &ci, char *out, int cap, int *tone);
void hc_fo_refuse_full(cfw_result_t r, char *out, int cap, int *tone);
bool hc_fo_add_bundle(const HcFoEnv &e, const char *path, const CorInfo &ci);
bool hc_fo_add_full(const HcFoEnv &e, const char *path, const char *name, uint32_t size);
void hc_fo_browse_add(const HcFoEnv &e, const char *path, bool full, cfw_result_t bcw_res, const CfwInfo &bcw, cor_result_t bci_res, const CorInfo &bci);
void hc_fo_recheck(const HcFoEnv &e, int sel);                                      // R in the menu: the entry again, in full

// ---- Cores and ROMs: delete what belongs to no core (T148) ----
void hc_fo_cores_delete(const HcFlowEnv &e, const CmReport &rep, CmDelete *out, char *msg, int cap, int *tone);

#endif
