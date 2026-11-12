// uicore - the UI service layer both apps compile (T192.1, MVP of T192 D20): a versioned state snapshot with stable item ids, intents, an event
// stream, jobs and questions, and the token. Pure C++11: no LVGL, no FreeRTOS, no lwIP, no include from an app or from Gideon's tree. Everything the
// target supplies comes through UicPort; everything role specific (host or core) comes through UicDomain, which an app implements and registers.
//
// Threads (T192.1-S17): the app has ONE UI task (the host's hostui loop, the core app's menu loop). It alone calls uic_service(), uic_note(),
// uic_local() and every UicDomain method. Network tasks call only the functions in the "network side" block: they copy, enqueue and wait, they never
// read or write UI state. UicPort::lock/unlock must make a short critical section that is safe against the UI task.
//
// The public surface is this header. The contract version (UIC_CONTRACT) is the `ui` field of every snapshot and of `hello`.
#ifndef UICORE_UIC_H
#define UICORE_UIC_H

#include <stdint.h>
#include <stddef.h>

#define UIC_CONTRACT 2                    // the major version; a client refuses an unknown major (T192.1 D-C)
#define UIC_CONTRACT_MIN 2
#define UIC_ID_MAX 48                     // item id: 1..48 characters of [a-z0-9._:-]
#define UIC_SLOTS 8                       // intents waiting between the network tasks and the UI task (D-J)
#define UIC_EVENTS 16                     // the event ring; a client that falls further behind gets `resync`
#define UIC_EV_MAX 300
#define UIC_JOBS 4
#define UIC_HISTORY 32                    // entries kept for `history` (epic D19d: RAM only, 20-50)
#define UIC_HISTORY_LEN 64
#define UIC_TOKEN_LEN 32                  // 16 bytes as lower case hex

// ---- what the target supplies ---------------------------------------------------------------------------------------------------------
struct UicPort {
    void *user;
    uint32_t (*now_ms)(void *u);
    void (*lock)(void *u);                // short critical section (UI task against network tasks)
    void (*unlock)(void *u);
    void (*sleep_ms)(void *u, int ms);    // a network task waits for the UI task
    bool (*on_ui_task)(void *u);          // true on the UI task; null: not checked (T192.1-S17 asserts with it)
    int (*entropy)(void *u, uint8_t *out, int n);   // raw material for the token (T192.1 Q3): up to n bytes, returns how many; null: none
    int (*file_load)(void *u, char *buf, int cap);  // the token file's text, its length; -1 when there is no file
    bool (*file_save)(void *u, const char *buf, int n);   // writes the token file (replacing it); false on failure
    bool (*file_remove)(void *u);
    void (*log)(void *u, const char *line);   // optional: one line per applied intent ("[ui] activate fs:x.adf -> asked"); null: silent
};

// ---- JSON writer (both snapshots use it) ----------------------------------------------------------------------------------------------
struct UicW {
    char *out;
    int cap, p;
    bool ok;
    UicW(char *o, int c) : out(o), cap(c), p(0), ok(c > 0) { if (c > 0) o[0] = 0; }
    void raw(const char *s);                          // verbatim text
    void fmt(const char *f, ...);                     // snprintf-style, %d %u %s only
    void str(const char *s);                          // a JSON string with escapes; invalid UTF-8 bytes become '?'
    void sep();                                       // a comma unless the last character opened a container
    void open(char c);                                // { or [
    void close(char c);
    void key(const char *k);                          // "k":
    void ks(const char *k, const char *v);            // , "k":"v"
    void ki(const char *k, int v);
    void ku(const char *k, uint32_t v);
    void kb(const char *k, bool v);
    void kopen(const char *k, char c);                // "k":{ or "k":[
    int length() const { return ok ? p : -1; }
};

// A flat scan of a JSON object: the value of top-level member `key` as text (strings unescaped, numbers and literals as written). Returns 0 when
// absent, 1 string, 2 number, 3 true/false/null. Nested values are skipped, never returned.
int uic_json_get(const char *js, int n, const char *key, char *out, int cap);
// The same for a member that is an object or an array: its text as written, braces included. False when absent or not a container.
bool uic_json_raw(const char *js, int n, const char *key, char *out, int cap);

// An item or entry name to an id: lower case, characters outside [a-z0-9._:-] become '_', at most UIC_ID_MAX; when anything was changed or cut, '-' and
// four hex digits of the original follow, so two names that map alike stay different. The same input gives the same id in every run.
void uic_id_make(const char *prefix, const char *raw, char *out, int cap);
bool uic_id_valid(const char *id);

// ---- intents (client -> app) ----------------------------------------------------------------------------------------------------------
enum UicDo { UD_NONE = 0, UD_SELECT, UD_ACTIVATE, UD_SET, UD_KEY, UD_HISTORY, UD_BACK, UD_OPEN, UD_ANSWER, UD_CANCEL, UD_OP, UD_GRANT, UD_TEXT };
enum UicR { UR_TAKEN = 0, UR_REFUSED, UR_ASKED, UR_HELD, UR_BLOCKED, UR_GONE, UR_BUSY, UR_BAD };

struct UicIntent {
    int kind;                             // UicDo
    char cid[16];                         // the client's own id, echoed in the ack
    char item[UIC_ID_MAX + 1];            // select, activate, set (or the setting key); UD_OP: the action name
    char key[64];                         // UD_KEY: a name (up down left right ret back esc f1..f8 runstop space del home pgup pgdn) or one character; UD_SET: setting key
    char value[256];                      // UD_SET, UD_TEXT
    char target[48];                      // UD_OPEN: a goto_spec or screen id
    char args[320];                       // UD_OP: the arguments as a JSON object, exactly what POST /v1/ops/<name> takes
    char q[12], choice[12], job[12], dir[8];
    bool menu;                            // UD_OPEN: the menu itself
    bool submit;                          // UD_TEXT: press RETURN on the field after it holds the text
    bool local;                           // from the board's own keyboard
    void *aux;                            // UD_OP: the app's own pending record
};

struct UicReply {
    int r;                                // UicR
    uint32_t rev;
    char job[12], q[12];
    char why[120];                        // the producing module's text, never composed by the framework (R23)
    char text[UIC_HISTORY_LEN + 1];       // history: the recalled entry
    char token[UIC_TOKEN_LEN + 1];        // UD_GRANT
};
const char *uic_r_name(int r);

// ---- the app's side: a domain ---------------------------------------------------------------------------------------------------------
struct UicQuestion {
    char text[120];
    bool destructive;
    char def[8];                          // "yes" / "no" taken at the timeout, "" none
    char yes_key[8], no_key[8];           // the keys the screen answers with, as `key` intents name them; "" : "ret" and "esc"
    uint32_t timeout_ms;                  // 0: waits for ever
};

struct UicJobInfo {
    char key[16];                         // stable while the job runs
    char kind[16];
    char title[48];
    char step[48];
    int done, total;                      // total 0 = unknown
    bool cancelable;
};

class UicDomain {
public:
    virtual ~UicDomain() { }
    virtual const char *app() = 0;                            // "core" | "host"
    // Who takes keys now: "menu", "dialog", "monitor", "picture", "search", "job", "closed" (the menu is not open). select/activate/set/back-to-an-item
    // are `blocked` unless it is "menu" (T192.1-S8).
    virtual const char *keys_owner() = 0;
    // The members after `rev` and `keys_owner`: screen, title, crumb, focus, unsaved, areas, tabs, items, rows, footer, status. Items carry stable ids.
    virtual void snapshot(UicW &w) = 0;
    // select / activate / set / key / back / open / UD_OP. The reply is the app's own word about it; the framework adds nothing to why.
    virtual void intent(const UicIntent &in, UicReply *r) = 0;
    // Polled once per pass: a question the screen shows now (a confirmation); false when none.
    virtual bool question(UicQuestion *q) { (void)q; return false; }
    // A remote answer to the question the screen shows: the same effect as the key (yes = RETURN, no = left).
    virtual void answer(const char *choice) { (void)choice; }
    // Polled once per pass: the jobs that run now.
    virtual int jobs(UicJobInfo *out, int max) { (void)out; (void)max; return 0; }
    virtual bool cancel_job(const char *key) { (void)key; return false; }
    virtual void job_result(const char *key, bool *ok, char *text, int n) { (void)key; *ok = true; if (n > 0) text[0] = 0; }
    virtual void set_text(const char *text) { (void)text; }   // history recall into the field being edited
    virtual void tick(uint32_t ms) { (void)ms; }              // once per pass, before the question and job polls
};

// ---- the service ----------------------------------------------------------------------------------------------------------------------
struct UicSlot {
    int state;                            // 0 free, 1 queued, 2 done, 3 abandoned by the waiting task while applied, 4 being applied
    uint32_t gen;                         // UicService::local_gen when it was queued: a local key since then means the screen it was made for is gone (T192.3 S10)
    UicIntent in;
    UicReply out;
};

struct UicEvent {
    uint32_t rev;
    char text[UIC_EV_MAX];
};

struct UicJob {
    bool used;
    char id[8];
    char key[16];                         // the domain's key, "" for a producer's own job
    char kind[16], title[48], step[48];
    int phase;                            // 0 queued 1 running 2 asking 3 done 4 failed 5 cancelled
    int done, total;
    bool cancelable, cancel_req, mirrored_seen;
    bool ok;
    char text[80];
};

struct UicClient {                        // one WebSocket client
    bool hello_sent, first_snap_sent, snap_on, closing;
    uint32_t ev_rev, snap_rev, hello_rev;   // hello_rev: the first snapshot is at least this fresh
};

struct UicService {
    UicPort port;
    UicDomain *dom;
    char *snap;                           // the cached snapshot (app-provided buffer)
    int snap_cap, snap_len;
    bool snap_valid;
    uint32_t snap_rev;
    uint32_t snap_hash, snap_hash_rev, snap_at;   // a second render at the same rev that differs means the state changed unnoticed: the rev grows
    uint32_t rev;
    uint32_t req_gen, done_gen;
    uint32_t local_gen;                   // counts the board's own intents: a remote intent queued before one is refused ("local input", T192.3 S10)
    int nclients;
    UicSlot slot[UIC_SLOTS];
    UicEvent ev[UIC_EVENTS];
    uint32_t ev_head, ev_count, ev_floor; // ev_floor: the highest rev that fell out of the ring
    // question
    bool q_open, q_settle, q_polled;      // q_polled: the open question was opened by the domain's own report, not by a producer's uic_ask
    uint32_t q_n, q_since;
    char q_id[12];
    UicQuestion q;
    // jobs
    UicJob job[UIC_JOBS];
    uint32_t job_n;
    // token
    char tok[UIC_TOKEN_LEN + 1];
    bool tok_have;
    uint32_t tok_polled, grant_n;
    bool quiet;                           // the app asks for no file access of the service's own (the token poll) while it writes flash
    int tok_miss;                         // consecutive looks that found no file: one miss may be a busy medium, two are a deleted file
    // history
    char hist[UIC_HISTORY][UIC_HISTORY_LEN + 1];
    int hist_n, hist_cur;
    // counters a bench reads
    uint32_t violations;                  // UI-task-only functions called from another task
    uint32_t overflows;
};

void uic_init(UicService *s, const UicPort *port, UicDomain *dom, char *snap_buf, int snap_cap);

// ---- the UI task ----------------------------------------------------------------------------------------------------------------------
// The state changed (a key taken, open/close, a row changed, a job step): the revision grows, a snapshot client is told.
void uic_note(UicService *s);
// Once per pass: domain tick, questions and jobs polled, queued intents applied (the remote ones, after the board's own), the snapshot rendered when
// someone wants it, the token file looked at every two seconds.
int uic_service(UicService *s);   // returns how many queued intents it applied (the app redraws its screen when > 0)
// The board's own input enters the same dispatcher (T192.1-S7): applied at once, ahead of everything remote.
void uic_local(UicService *s, const UicIntent &in, UicReply *r);
void uic_toast(UicService *s, const char *text);                 // the line the screen flashes, as an event
// A change the domain saw, as an event {"t":type,"rev":n,"v":value}: the screen's page, the focus, the dirty mark, the media (T192.3 S12). A client that missed it reads the snapshot.
void uic_event(UicService *s, const char *type, const char *value);
// A producer's own question (the screen shows it too, through its domain). Returns false when one is open already.
bool uic_ask(UicService *s, const UicQuestion &q, char *id_out);
// UI task: the producer or the domain closes the open question itself (the screen's arm ended, a back from the client): one `closed` event with `by` and `choice`
// ("" for none), the question is not polled again as an open one. False when none is open, so a question closes exactly once.
bool uic_close(UicService *s, const char *by, const char *choice);
void uic_history_add(UicService *s, const char *text);
void uic_history_clear(UicService *s);
// A producer's own job (a host flow); the domain's polled jobs need none of this.
bool uic_job_begin(UicService *s, const char *kind, const char *title, bool cancelable, char *id_out);
void uic_job_step(UicService *s, const char *id, int done, int total, const char *step);
void uic_job_end(UicService *s, const char *id, bool ok, const char *text);
bool uic_job_cancel_requested(UicService *s, const char *id);
// The app's own abort key (the local ESC) reached a cancelable job: it counts as a cancel, so the job ends as cancelled like after the cancel intent. False if the job cannot be cancelled.
bool uic_job_cancel_local(UicService *s, const char *id);
// The snapshot now, rendered from the domain (UI task only; the network side takes the cached copy). Returns its length or -1.
int uic_render(UicService *s, char *out, int cap);

// ---- the network side (any task) ------------------------------------------------------------------------------------------------------
// The service's short critical section for an adapter that keeps state of its own beside it (the host's T106 page: the question it raised): hold, read or write, release.
// The service's own fields (q_open, q_id, q) are read inside it. Network side.
void uic_hold(UicService *s);
// UI task: while true the service reads no token file (a flow that writes the flash must not have a file read in the middle of it); the poll goes on afterwards.
void uic_quiet(UicService *s, bool on);
void uic_release(UicService *s);
// Token gate for every request of a transport: returns 0 to go on, 401 to refuse. `query_tok`, `bearer`, `xtrx` are what the request carried (null
// when absent); the transport strips `t=` from its query. Public: GET /, GET /v1/ui/hello, POST /v1/ui/grant. Everything else needs the token.
int uic_gate(UicService *s, const char *method, const char *path, const char *query_tok, const char *bearer, const char *xtrx);
bool uic_route_public(const char *method, const char *path);
// The /v1/ui/hello, /v1/ui/grant, /v1/ui/state and /v1/ui/intent routes. Returns the body length (status in *status), or -1 when `out` is too small,
// or -2 when the path is none of these (the transport goes on). `v1` is the transport's own handler for /v1/ui/state?v=1; here only v2.
int uic_http(UicService *s, const char *method, const char *path, const char *body, int body_n, char *out, int cap, int *status);
// The cached snapshot, fresh for the current rev (asks the UI task and waits at most 3 s). The length, -1 on a timeout or when cap is too small.
int uic_snapshot(UicService *s, char *out, int cap, uint32_t *rev);
// One intent from the network: queued, applied on the UI task, the reply handed back (waits at most 3 s, then `busy`).
void uic_submit(UicService *s, const UicIntent &in, UicReply *r);
bool uic_intent_parse(const char *js, int n, UicIntent *in);
int uic_ack_json(const UicIntent &in, const UicReply &r, bool ws, char *out, int cap);

// WebSocket client session: hello, the snapshot, then events with rev greater; a client behind the ring gets `resync` and is closed (T192.1-S10, S11).
void uic_client_open(UicService *s, UicClient *c);
void uic_client_close(UicService *s, UicClient *c);
// The next message for the client into out; its length, 0 when there is none. After a `resync`, c->closing is set: the transport closes.
int uic_client_next(UicService *s, UicClient *c, char *out, int cap);
// A text message from the client (intent, {"t":"sub","push":"snap"|"note"}, {"t":"get"}); the reply into out, its length, 0 for none.
int uic_client_in(UicService *s, UicClient *c, const char *msg, int n, char *out, int cap);

#endif
