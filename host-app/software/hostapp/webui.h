// The host application on the network: one page a browser renders and the same facts as JSON (T106).
//
// A board that shows nothing cannot be told anything - the file browser lists only `.cor`, the boot
// menu is empty after a fresh install, and the keyboard only helps somebody who can see. The FTP
// server beside this one is already a way in for files; this is the way in for a person with a phone,
// and for a test with no keyboard.
//
// One writer (T106 D2): nothing here touches the menu, the settings or the picture. A request puts a
// command in the mailbox below and is answered at once; the UI task picks it up on its next pass,
// applies it and refreshes the snapshot that the page and the JSON are both rendered from.
#ifndef HOST_WEBUI_H
#define HOST_WEBUI_H

#include <stdint.h>
#include "bootcfg.h"

#define WEB_PORT      80      // the port the web task binds (webui.cc); the Network page names it
#define WEB_NAME_MAX  48
#define WEB_PATH_MAX  96

// What the UI task knows, written by it and read by the web task. One struct, copied whole.
struct WebSnapshot {
    char app[24], host_key[12];
    char ip[24];
    bool usb, sd, net;
    char media[96];
    int vid_sys, vid_hdmi;
    unsigned vid_mask;                      // T164: the table rows the screen names (edid.h); the page lists only those
    int vid_def;                            // the stored choice, -1 = from the screen
    int countdown, def;
    int n;                                  // menu entries
    char name[BOOT_MAX][WEB_NAME_MAX];
    char path[BOOT_MAX][WEB_PATH_MAX];
    char state[BOOT_MAX][16];               // "ok", "changed", "missing", ...
    int configs[BOOT_MAX];                  // T198 S13: the entry's configs (0 = none, or not a core with a folder)
    bool startable[BOOT_MAX];
    // T162: what the bug report reads besides the above (written by the UI task with the rest, so the report from the menu and the one from
    // REST are made of the same facts)
    char app_date[32], board[32];
    int direct, autostart_s, lib_attempt;
    unsigned size[BOOT_MAX];                // the bundle's size when it was added (0 = unknown)
    char hash[BOOT_MAX][65];                // ... and its SHA-256 as boot.cfg keeps it
    char cfgtext[512];                      // the host settings as the store writes them, without the core= lines
    // T192 D19a: a destructive form (remove) does not run: it opens a question (webnet.cc asks the service); the page shows it with Yes / No while it is open
    char q_id[12], q_text[120];
};

enum WebCmdKind { WEB_NONE = 0, WEB_VIDEO, WEB_BOOT, WEB_REMOVE, WEB_COUNTDOWN, WEB_DEFAULT };

// GET routes (T162 S2). 0: not a GET route of ours.
enum WebGet { WEBGET_NONE = 0, WEBGET_STATUS, WEBGET_REPORT };

struct WebCmd {
    WebCmdKind kind;
    int a, b;                               // video: unused, hdmi (-1 = from the screen); boot/remove/default: index; countdown: seconds
    char config[33];                           // T198 S13: boot: the config to start the entry with ("" = plain)
};

// The UI task's side.
void web_publish(const WebSnapshot *s);     // the snapshot the page is rendered from
bool web_take(WebCmd *out);                 // true when a command was waiting; it is cleared
void web_result(const char *text);          // what the page shows about the last command
void web_start(void);                       // brings the task up (does nothing twice)
const char *web_parse_why(void);            // the reason of the last 400 web_parse answered ("" = none); the error body carries it
bool web_post(const WebCmd *c);             // what a request does: the command goes in the mailbox; false when one is still waiting (the request answers 409)
void web_copy(WebSnapshot *s, char *note, int notelen);   // the snapshot and the note, taken together (a web task renders from its own copy)
bool web_has_net(char *ip, int len);        // the web tasks listen only with an address (T106 D6)
void web_queue_for_test(const WebCmd *c);   // what a request does, for a bench with no socket
const char *web_note(void);                 // what the last command was answered with

// One field of a form body ("a=1&b=2"), percent-decoded. False when it is not there.
bool web_form_field(const char *body, const char *key, char *out, int max);

// Pure, so the test drives them without a socket: the route and the two renderings.
// `out` is written up to `max` bytes; the length is returned (never more than max - 1).
int web_page(const WebSnapshot *s, const char *note, char *out, int max);
int web_json(const WebSnapshot *s, const char *note, char *out, int max);
// Which GET route a request is, query string ignored. The web task's dispatch asks this and nothing else.
WebGet web_get_route(const char *uri);
// T162 S2: the extra headers of the report answer (Content-Type text/plain, Content-Disposition: attachment with the file name the menu would
// write). The body is hostreport.cc's, made where the web task answers (the clock and the log are read there, the rest is the snapshot).
int web_report_head(uint32_t fattime, bool time_valid, char *out, int max);
// Reads one form body ("a=1&b=2") or query string into a command. False when the route is unknown or
// the body does not hold what the route needs.
bool web_parse(const char *uri, const char *body, WebCmd *cmd, const WebSnapshot *s, int *status);

#endif
