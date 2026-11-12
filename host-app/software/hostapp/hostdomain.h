// The host application's side of the UI service (T192.1 MVP, T192 D21): a UicDomain that walks the declaration (hostdecl.h) over the app's model
// (hostmodel.h). The snapshot, the ids, the intents and their routing come from the tables; the routes are the service's generic ones.
// Pure C++11 (uicore + the declaration): the board's pieces (FreeRTOS critical sections, the token file) are handed in through HostApiPlatform.
//
// Threads: ONE UI task calls everything on this page except what is marked "network side" in uic.h (the service's own functions). hostui.cc is the UI task.
#ifndef HOST_HOSTDOMAIN_H
#define HOST_HOSTDOMAIN_H

#include "hostmodel.h"
#include "uic.h"

struct HostApiPlatform {
    void (*lock)(void);
    void (*unlock)(void);
    bool (*on_ui)(void);
    void (*sleep_ms)(int ms);
    int (*entropy)(uint8_t *out, int n);
    uint32_t (*now_ms)(void);
    int (*file_load)(char *buf, int cap);           // /Flash/trx/ui.token; -1 when there is none; null (all three): no token, no authentication
    bool (*file_save)(const char *buf, int n);
    bool (*file_remove)(void);
};

// hostapi_board.cc: FreeRTOS and the token file; call from the UI task.
const HostApiPlatform *hostapi_board_platform();

// Registers the domain over `model` and starts the service. The snapshot buffer lives inside.
void hostapi_init(HostModel *model, const HostApiPlatform *plat);
UicService *hostapi_service();                       // null before hostapi_init

// ---- the UI task -----------------------------------------------------------------------------------------------------------------------
// The main loop, once per pass. `changed`: a key was taken or the screen was redrawn. Returns true when a remote intent was applied (redraw).
bool hostapi_pass(bool changed);
// A modal wait or a long step calls this (host_getch, a progress callback): the service runs, remote keys and answers arrive. Not re-entrant.
void hostapi_pump();
// The board's own key enters through the same dispatcher as a remote `key` (T192.1-S7); the result is in the queue below.
void hostapi_local_key(int k);
// The screen tells the service that the arm with this token ended by itself (HArmEnd in hostmodel.h): the question about it closes, once, as the screen's answer.
void hostapi_arm_end(uint32_t token, int how);
// True while the key the screen handles now came from the board, false when it came from the API's queue.
bool hostapi_key_local();
// The queue both paths fill and on_key / the modal waits drain. -1: none.
int hostapi_pop_key();
bool hostapi_push_key(int k);
// The Network page's REST row: what the server is doing (it listens on WEB_PORT once the network has an address) and whether a token is asked.
void hostapi_rest_text(bool net, char *out, int n);
// An intent applied on the UI task now, as the service applies one that came in over the network (the old web page's commands enter here, hostui.cc's web_pump).
void hostapi_apply(const UicIntent &in, UicReply *r);
void hostapi_bind_model(HostModel *model);          // the model the dispatcher acts on, when the service is not started (a bench); hostapi_init sets it itself
// The v2 snapshot as the UI task renders it now (a bench).
int hostapi_render(char *out, int cap);

// ---- the app tells the service what it is doing --------------------------------------------------------------------------------------
// A page of a flow (hostui.cc's FmPage): the lines, the typed text, the bar. A page with a bar is progress; one that waits for a key is a question.
struct HostPageInfo {
    char title[72];
    char line[8][124];
    int nlines;
    char hint[124];
    char msg[124];
    char typed[48];
    bool input;                // a text field: the typed name is part of the page
    int bar;                   // -1: none, else 0..100
    char pct[16];
};
// yes_key / no_key: the keys the page takes as yes and no (RETURN / ESC unless it says otherwise); destructive: the question writes or erases.
void hostapi_page(const HostPageInfo &p, int yes_key, int no_key, bool destructive);
void hostapi_page_end();
// The text entry modal (a name, an address): `typed` is what the field holds now.
void hostapi_entry(const char *title, const char *sub, const char *typed, const char *hint);
void hostapi_entry_end();
// A pick list modal (the access points, the authentication): the rows and the cursor. Choosing a row moves the cursor there and presses RETURN.
void hostapi_pick(const char *title, const char *const *labels, int n, int sel, const char *hint);
void hostapi_pick_end();
// The modal wait itself: host_getch calls it so the service knows the screen waits for an answer now.
void hostapi_waiting();
// The page of the settings that asks (Cores and ROMs, delete): its text and the keys, or an empty text when it does not ask.
// remote: the question was raised by a remote client (web.start); like every API question it answers no after 60 s. The board's own RETURN has no time-out.
void hostapi_ask(const char *text, int yes_key, int no_key, bool remote = false);
// A flow of the app as a job (progress, cancel). begin returns false when a flow runs already (the app starts nothing second).
bool hostapi_flow_begin(const char *kind, const char *title, bool cancelable);
void hostapi_flow_progress(int done, int total, const char *step);
void hostapi_flow_end(bool ok, const char *text);
bool hostapi_flow_running();
// The line the screen flashes at the bottom (set_note): it goes out as a toast event; `bad` marks a failure (the running flow ends not ok).
void hostapi_note(const char *text, bool bad);
// A line typed and sent: kept for the history (D19d), RAM only.
void hostapi_history(const char *text);

#endif
