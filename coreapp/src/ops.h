// The service layer's registry (T104.1 D1): operations, state values and events, each registered by name by a contributor, with typed
// arguments. A caller - the menu, a bench, later REST - gives names and values; the registry checks them against the registered
// types and only then runs the handler, so a call with a wrong value never reaches the code behind the name. Pure: no board code,
// the same file runs in tests/coreapp/.
#ifndef COREAPP_OPS_H
#define COREAPP_OPS_H

#include <stdint.h>
#include "fsif.h"

struct CoreCtx;

// Results are the u2p_err_e set of the application ABI (vendor/gideon/software/app_loader/app_api.h), so a REST client and an
// application see the same code for the same failure.
enum OpErr {
    OPE_OK = 0,
    OPE_ARG = -1,            // an argument is missing, unknown or out of range
    OPE_NOT_FOUND = -2,      // no such operation, state or event
    OPE_IO = -3,
    OPE_FORMAT = -4,
    OPE_BUSY = -5,           // the executor queue is full
    OPE_UNSUPPORTED = -6,
    OPE_FAILED = -7          // the handler ran and refused; the reason is in the message
};

// The types are few and fixed (D1). A blob is a handle to bytes the caller owns for the duration of the call.
enum ArgType { AT_INT, AT_BOOL, AT_ENUM, AT_STRING, AT_PATH, AT_SLOT, AT_BLOB };

#define OP_ARGS_MAX 4
struct ArgSpec {
    const char *name;
    uint8_t type;
    int32_t lo, hi;                       // int and enum: the allowed values, inclusive (an enum is 0 .. labels - 1)
    const char *const *labels;            // enum
    uint16_t maxlen;                      // string, path, slot: the longest text
};
#define ARG_INT(n, lo, hi) { n, AT_INT, lo, hi, 0, 0 }
#define ARG_BOOL(n) { n, AT_BOOL, 0, 1, 0, 0 }
#define ARG_ENUM(n, labels, cnt) { n, AT_ENUM, 0, (cnt) - 1, labels, 0 }
#define ARG_STRING(n, max) { n, AT_STRING, 0, 0, 0, max }
#define ARG_PATH(n) { n, AT_PATH, 0, 0, 0, FS_PATH_MAX - 1 }     // a folder path and a FAT long name (T180 #38)
#define ARG_SLOT(n) { n, AT_SLOT, 0, 0, 0, 31 }
#define ARG_BLOB(n) { n, AT_BLOB, 0, 0, 0, 0 }

// What a caller hands over, untyped: a transport has text, the menu has integers and a file, a ROM manager has bytes. An integer
// argument takes `i` or the text in `s`; an enum takes its label in `s` or its index in `i`.
struct OpIn {
    const char *name;
    const char *s;
    int32_t i;
    bool is_int;
    const uint8_t *p;
    uint32_t n;
};
static inline OpIn in_int(const char *name, int32_t v) { OpIn x = { name, 0, v, true, 0, 0 }; return x; }
static inline OpIn in_str(const char *name, const char *s) { OpIn x = { name, s, 0, false, 0, 0 }; return x; }
static inline OpIn in_blob(const char *name, const uint8_t *p, uint32_t n) { OpIn x = { name, 0, 0, false, p, n }; return x; }

// The checked, typed arguments in the order of the registered ArgSpec list. An enum is its index.
struct OpArgs {
    int n;
    int32_t iv[OP_ARGS_MAX];
    const char *sv[OP_ARGS_MAX];
    const uint8_t *pv[OP_ARGS_MAX];
    uint32_t nv[OP_ARGS_MAX];
    int32_t i(int k) const { return iv[k]; }
    const char *s(int k) const { return sv[k]; }
    const uint8_t *p(int k) const { return pv[k]; }
    uint32_t len(int k) const { return nv[k]; }
};

#define OP_MSG_MAX 400
// The handler writes its reason into msg (at most msglen bytes) and sets more when it had work and wants to be called again at once
// (a poll). It returns an OpErr.
// A handler may also return bytes (a memory read): up to datacap bytes into data, ndata of them. They travel with the result, never as an argument.
#define OP_DATA_MAX 256
struct OpOut {
    char *msg;
    int msglen;
    bool more;
    uint8_t *data;
    int datacap;
    int ndata;
};
typedef int (*op_fn)(CoreCtx *c, const OpArgs &a, OpOut &o);

struct OpResult {
    int code;
    bool more;
    char msg[OP_MSG_MAX];
    int ndata;                            // bytes in data (a read), else 0
    uint8_t data[OP_DATA_MAX];
};

// The reuse of the descriptor's old hook functions: wraps a `int hook(CoreCtx *, char *msg, int msglen)` as an operation without
// arguments. Any nonzero return becomes OPE_FAILED, the reason stays in the message.
template <int (*H)(CoreCtx *, char *, int)>
int op_hook(CoreCtx *c, const OpArgs &, OpOut &o)
{
    return H(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// How a descriptor entry fills an operation's arguments (D4 `bind`): `$file` for the file the menu was on, constants otherwise.
enum BindKind { BIND_FILE = 0, BIND_INT = 1, BIND_STR = 2, BIND_ASK = 3, BIND_TEXT = 4 };   // BIND_ASK: the number the person typed (FileAction::ask); BIND_TEXT: the text (Task::ask)
struct OpBind {
    const char *arg;
    uint8_t kind;
    int32_t i;
    const char *s;
};
#define B_FILE(a) { a, BIND_FILE, 0, 0 }
#define B_INT(a, v) { a, BIND_INT, v, 0 }
#define B_STR(a, v) { a, BIND_STR, 0, v }
#define B_ASK(a) { a, BIND_ASK, 0, 0 }
#define B_TEXT(a) { a, BIND_TEXT, 0, 0 }

struct OpDef {
    const char *name;
    const char *owner;                    // the contributor that registered it
    const ArgSpec *args;
    int nargs;
    op_fn fn;
};

// T162 D4: the full path of the file a slot's state text names (the newest successful path argument whose file name equals `name`); null if none.
void ops_note_path(CoreCtx *c, const char *path);
const char *ops_find_path(CoreCtx *c, const char *name);

typedef const char *(*state_fn)(CoreCtx *c);          // the value as text; null: nothing (an empty slot)
struct StateDef {
    const char *name;
    const char *owner;
    state_fn get;
    uint32_t rev;                         // counts the changes seen by state()
    uint32_t hash;
    bool seen;
};

typedef void (*event_fn)(void *user, const char *name, const OpArgs &payload);
#define EVENT_SUBS_MAX 6
struct EventDef {
    const char *name;
    const char *owner;
    bool has_payload;
    ArgSpec payload;
    event_fn fn[EVENT_SUBS_MAX];
    void *user[EVENT_SUBS_MAX];
    int nsubs;
};

#define OPS_MAX 400                       // T228 review: trx64u's record and an application's own (TREX CRT: about 30 ops, 41 states) did not fit
#define STATES_MAX 320                    // in 300 / 200; 20 bytes an entry on RV32, so the two cost 4.4 KB more of static RAM
#define EVENTS_MAX 8

// What the registry held at one moment: a loader takes one before a library registers and gives it back when the library fails, so a
// refused library leaves no name behind (T104.1 D10.3). Only valid while nothing was registered after it by someone else.
struct RegMark {
    int nops, nstates, nevents;
};

class OpRegistry {
public:
    OpRegistry();

    // The contributor whose registrations follow. A name has one owner: the first registration wins, a second is refused and
    // error() names both.
    void contributor(const char *name) { owner_ = name; }
    int add_op(const char *name, const ArgSpec *args, int nargs, op_fn fn);
    int add_state(const char *name, state_fn get);
    int add_event(const char *name, const ArgSpec *payload);       // payload may be null
    const char *error() const { return err_; }                      // the last refusal, as a sentence
    void clear_error() { err_[0] = 0; }                             // a refusal that was taken back (a library rolled back) leaves none

    const OpDef *find_op(const char *name) const;
    const StateDef *find_state(const char *name) const;
    // The registered things in registration order, for a transport that lists them (T104.1 D3); null past the end.
    const OpDef *op_at(int i) const { return i >= 0 && i < nops_ ? &ops_[i] : 0; }
    const StateDef *state_at(int i) const { return i >= 0 && i < nstates_ ? &states_[i] : 0; }
    const EventDef *event_at(int i) const { return i >= 0 && i < nevents_ ? &events_[i] : 0; }
    int nops() const { return nops_; }
    int nstates() const { return nstates_; }
    int nevents() const { return nevents_; }
    RegMark mark() const;
    // Removes what a contributor registered, wherever it stands in the lists (an application that is freed while others stay).
    void drop_owner(const char *name);
    void rollback(const RegMark &m);                                // drops everything registered since the mark

    // Checks the arguments against the registered types and fills the typed list. Returns OPE_OK, or OPE_ARG with the reason in
    // why. Nothing runs here.
    int bind(const OpDef *d, const OpIn *in, int nin, OpArgs *out, char *why, int whylen) const;
    // Runs a handler with typed arguments on the calling thread, no lock: the executor and the poll tasks take the core lock.
    int run(CoreCtx *c, const OpDef *d, const OpArgs &a, OpResult *r) const;
    // bind + run, on the calling thread.
    int call(CoreCtx *c, const char *name, const OpIn *in, int nin, OpResult *r) const;

    // The value of a state and its revision (it counts the changes seen by calls of state()). Null: no value or no such state.
    const char *state(CoreCtx *c, const char *name, uint32_t *rev);

    // Events: a subscriber is called with the checked payload of every emit.
    int subscribe(const char *name, event_fn fn, void *user);
    int unsubscribe(const char *name, event_fn fn, void *user);     // OPE_NOT_FOUND: no such event or this subscriber was not on it
    int emit(const char *name, const OpIn *payload);                // returns the number of subscribers called, or an OpErr

private:
    const char *owner_;
    OpDef ops_[OPS_MAX];
    StateDef states_[STATES_MAX];
    EventDef events_[EVENTS_MAX];
    int nops_, nstates_, nevents_;
    char err_[200];

    bool name_ok(const char *name);
    EventDef *find_event(const char *name);
};

#endif
