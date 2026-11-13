// T135 D1: the one decision the host application takes before it builds anything: start the default core now, or show the menu and say why.
// Pure code (no hardware): the inputs are read by hostui.cc (boot.cfg, the return marker, the menu keys, a cheap look at the default's file)
// and tests/hostapp/test_directboot.cc runs the table.
#ifndef HOST_DIRECTBOOT_H
#define HOST_DIRECTBOOT_H

#include <stdint.h>

enum DbDefault {          // what the cheap look at the default entry found (hostui.cc probe_state)
    DD_OK,                // the file is there and is what boot.cfg stored (or boot.cfg stored no check: an installer-written entry)
    DD_MISSING,           // no such file (the medium may not be mounted yet: the caller waits before it asks the last time)
    DD_CHANGED,           // size, mtime or header differ from what boot.cfg stored
    DD_REFUSED            // not a package, another contract, another host
};

struct DirectIn {
    bool cfg_loaded;      // boot.cfg was read
    int direct;           // boot.cfg direct=
    int n, def;           // entries, index of the default (-1 = none)
    bool marker;          // a core asked for the menu (D3)
    bool key_cbm;         // C= held (D2)
    bool key_fire;        // fire on joystick port 1 held (D2)
    DbDefault dstate;     // only read when everything before it says "start"
};

enum DbReason {
    DB_START = 0,         // start the default now
    DB_RETURN,            // a core asked for the menu
    DB_KEY_CBM,
    DB_KEY_FIRE,
    DB_OFF,               // direct=0: today's menu with its countdown
    DB_NOCFG,             // no boot.cfg could be read: today's menu (which keeps trying to read it)
    DB_NODEFAULT,         // no entry is the default
    DB_MISSING,
    DB_CHANGED,
    DB_REFUSED
};

// Order of the rules: no boot.cfg (a held key still wins over it, issue #69) -> a return marker -> a held key -> direct=0 -> no default -> the default's file. The first that applies wins.
DbReason directboot_decide(const DirectIn &in);

// The menu stays without a countdown for these (a person asked for it, or the default cannot start); DB_OFF and DB_NOCFG keep today's countdown.
bool directboot_stops_countdown(DbReason r);

// One line for the menu's note and the console; "" for DB_START.
const char *directboot_text(DbReason r);

// True when the default's file has to be looked at at all: every rule before the file rule says "start".
bool directboot_needs_file(const DirectIn &in);

// Issue #14: the default's file on a removable medium (USB, SD) is looked at until it is there. A medium that is not mounted yet, or whose
// root was listed only a moment ago, is not "missing": the wait ends when the file shows up, a key is held, the medium has been listed for
// DB_GRACE_MS and the file is still absent (the answer is then DD_MISSING), or limit_ms ran out. All hardware comes in through DbEnv, so
// tests/hostapp/test_directboot.cc drives the very loop the host application runs with a fake clock and a fake medium.
#define DB_POLL_MS  50
#define DB_GRACE_MS 1500

struct DbEnv {
    uint32_t (*now)(void *ctx);                  // ms
    void (*sleep)(void *ctx, uint32_t ms);       // also samples the menu keys
    DbDefault (*look)(void *ctx);                // the cheap look at the default (hostui.cc direct_default_state)
    bool (*root_listed)(void *ctx);              // the default's medium (first path component) is in the root listing
    bool (*key_held)(void *ctx);                 // C= or fire
    void (*waiting)(void *ctx, bool on);         // on: put "waiting for <medium>" up, off: back to the start line
    void *ctx;
};

DbDefault directboot_look(const DbEnv &env, bool removable, uint32_t limit_ms, uint32_t grace_ms);

#endif
