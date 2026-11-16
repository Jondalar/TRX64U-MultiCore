#include "directboot.h"

DbReason directboot_decide(const DirectIn &in)
{
    if (!in.cfg_loaded) {
        // issue #69: a held key is a request for the menu even when boot.cfg could not be read in time; else the countdown starts once it is read
        return in.key_cbm ? DB_KEY_CBM : in.key_fire ? DB_KEY_FIRE : DB_NOCFG;
    }
    if (in.marker) {
        return DB_RETURN;
    }
    if (in.key_cbm) {
        return DB_KEY_CBM;
    }
    if (in.key_fire) {
        return DB_KEY_FIRE;
    }
    if (!in.direct) {
        return DB_OFF;
    }
    if (in.n <= 0 || in.def < 0 || in.def >= in.n) {
        return DB_NODEFAULT;
    }
    switch (in.dstate) {
    case DD_OK: return DB_START;
    case DD_MISSING: return DB_MISSING;
    case DD_CHANGED: return DB_CHANGED;
    default: return DB_REFUSED;
    }
}

bool directboot_needs_file(const DirectIn &in)
{
    DirectIn t = in;
    t.dstate = DD_OK;
    return directboot_decide(t) == DB_START;
}

bool directboot_stops_countdown(DbReason r)
{
    return r != DB_START && r != DB_OFF && r != DB_NOCFG;
}

const char *directboot_text(DbReason r)
{
    switch (r) {
    case DB_RETURN: return "Menu requested by the core";
    case DB_KEY_CBM: return "Menu: C= was held";
    case DB_KEY_FIRE: return "Menu: fire on port 1 was held";
    case DB_OFF: return "Direct boot is off";
    case DB_NOCFG: return "boot.cfg could not be read";
    case DB_NODEFAULT: return "No auto core is set";
    case DB_MISSING: return "Auto core: file missing";
    case DB_CHANGED: return "Auto core: file changed (press R to check it)";
    case DB_REFUSED: return "Auto core: refused (see its badge)";
    default: return "";
    }
}

DbDefault directboot_look(const DbEnv &env, bool removable, uint32_t limit_ms, uint32_t grace_ms)
{
    DbDefault d = env.look(env.ctx);
    if (d != DD_MISSING || !removable) {
        return d;
    }
    uint32_t t0 = env.now(env.ctx);
    bool shown = false;
    bool listed = false;
    uint32_t t_listed = 0;
    while (d == DD_MISSING && !env.key_held(env.ctx)) {
        uint32_t t = env.now(env.ctx);
        if (t - t0 >= limit_ms) {
            break;
        }
        bool l = env.root_listed(env.ctx);
        if (l && !listed) {
            t_listed = t;
        }
        listed = l;
        if (listed && t - t_listed >= grace_ms) {
            break;                               // the medium is there and has been for a while: the file is really not
        }
        if (!shown && !listed) {
            env.waiting(env.ctx, true);
            shown = true;
        }
        env.sleep(env.ctx, DB_POLL_MS);
        d = env.look(env.ctx);
    }
    if (shown) {
        env.waiting(env.ctx, false);
    }
    return d;
}
