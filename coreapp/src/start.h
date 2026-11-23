// Identity and the start decisions of a core application (T93 D4 steps 1-2, S1). Pure.
#ifndef COREAPP_START_H
#define COREAPP_START_H

#include "ctx.h"

enum StartResult {
    START_OK = 0,
    START_NO_DESCRIPTOR,       // the ID block matches no descriptor: the core stays held
    START_COR_MISMATCH,        // the descriptor is not the one the .cor manifest names: the core stays held
    START_AMBIGUOUS            // several machines share the ID block and there is no bundle name to choose by: the core stays held
};

// T239.3 D17: where a descriptor comes from when none is compiled into the application: the data description (record 6) of the bundle the hand-over names
// (bundle.h BundleDescSource). It returns the descriptor, kept for the whole run, or null with the sentence that says why not in msg.
class DescSource {
public:
    virtual ~DescSource() { }
    virtual const CoreDesc *describe(const uint8_t id[4], const Handover *h, char *msg, int msglen) = 0;
};

// The host build the hand-over names, 8 hex digits (any other character voids it: 0); what a .cor's partial is checked against (start_paths keeps it in the context).
static inline uint32_t start_host_key(const Handover *h)
{
    uint32_t key = 0;
    for (int i = 0; h && i < 8 && h->host_build[i]; i++) {
        char ch = h->host_build[i];        // case-insensitive: the firmware's small_printf writes %x in capitals (board 2026-11-06)
        uint32_t v;
        if (ch >= '0' && ch <= '9') {
            v = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            v = (uint32_t)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            v = (uint32_t)(ch - 'A' + 10);
        } else {
            return 0;
        }
        key = (key << 4) | v;
    }
    return key;
}

// id = the four bytes at the core window's ID register. h may be null or invalid (a developer start): then there is no
// manifest to compare and no .cor path. On START_OK *desc is the descriptor. msg always gets a line: the log line of S1
// ("[core] A8XL Atari 800 XL, cor /USB1/cores/atari800.cor") or the reason.
// io (may be 0): the register block, read for id_feat_reg when a shared ID has no bundle name
// src (may be 0, T239.3 D17): asked for the descriptor when the ID matches none that is compiled in; its refusal holds the core with its sentence (START_NO_DESCRIPTOR)
StartResult start_identify(const uint8_t id[4], const Handover *h, const CoreDesc **desc, char *msg, int msglen, CoreIo *io = 0, DescSource *src = 0);

// The way back from the held page (T115 7 H1). The page that start_identify's refusal puts up offers "Return to host menu" exactly where the SYSTEM
// menu does: the board has an ICAP loader that can take the FPGA back to the host (board.can_return_host), i.e. a layer-2 core under the
// MultiCore host and a large .cor whose flat build keeps the host's top. On the TRX64 Ultimate and Gideon's bitstream there is none.
bool held_offers_return(const BoardProfile *b);
// The page's text: the reason, then the hint when the way back is offered, then `err` (why the last try came back) when there is one.
void held_text(const char *why, const BoardProfile *b, const char *err, char *out, int outlen);
typedef bool (*HeldReturnFn)(char *msg, int n);            // hw_return_to_host: does not come back on success
enum HeldKeyResult { HELD_IGNORED = 0, HELD_RETURN_FAILED };
// A key on the held page. RETURN calls `ret` when the way back is offered; if it comes back, msg says why and the page stays.
HeldKeyResult held_key(const BoardProfile *b, int key, HeldReturnFn ret, char *msg, int msglen);

// Fills the context's paths and version from the hand-over (empty strings when h is null or invalid). Without a hand-over path the
// board profile may name a directory for the settings file (T104.2 D7): then cfg_path is "<cfg_dir>/<folder>.cfg" of the context's
// descriptor, which has to be set. The file is named by the folder, not by the store: the store is the [section] of the file and
// reads "Atari 800 XL Settings".
void start_paths(CoreCtx *c, const Handover *h, const BoardProfile *prof = 0);

// T223 D4: the start file of a large core (handover.h, startfile_*): read once, then deleted whatever it said. Call after start_paths, with c->fs set. The hand-over page's path
// decides where the file is; a block with no path (the page did not survive) looks on "/SD". A file whose cor= is not the page's path (left behind by another start) is dropped.
// Without a page path the file's cor= is the .cor of this start: the folder and its settings follow it (c->cor_path, c->cfg_path). *config gets the name ("" = plain), *safe the flag.
// True when a file of this start was found and used.
bool start_file_take(CoreCtx *c, const Handover *h, char *config, int cfglen, bool *safe);

// T112 Q1, "never lose a user's settings": a large core that used to keep its settings in the profile's directory (the monolith's
// /flash/trx64u/<folder>.cfg) now has its .cfg in its core folder (T148 D1). When that file does not exist and the profile's file does, the profile's file is copied
// there once; the original stays. True when it was copied. Call after start_paths, with c->fs set.
bool start_migrate_cfg(CoreCtx *c, const BoardProfile *prof);

#endif
