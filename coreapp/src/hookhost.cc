#include "hookhost.h"
#include "libload.h"
#include "libtab.h"
#include "core_hooks.h"
#include "libfmt.h"
#include "baseset.h"
#include "settings.h"
#include "mediamounts.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

namespace {
const CoreHooks *g_hooks;
uint32_t g_size;                   // how many bytes of the table the library has: the smaller of the call's answer and the table's own `size`
bool g_armed;                      // base_setting_changed is told from here on (after start)
HookEnter g_enter;                 // the scope of the named library (hooks_attach_table); null: none
HookLeave g_leave;
void *g_user;

// A call into the library's code is made on the library's account (LibLoader::Scope), when the table came from a named library.
struct Guard {
    alignas(8) unsigned char token[HOOK_TOKEN_BYTES];
    Guard()
    {
        if (g_enter) {
            g_enter(g_user, token);
        }
    }
    ~Guard()
    {
        if (g_leave) {
            g_leave(g_user, token);
        }
    }
};

// A member is called only when the library's table reaches it and it is set: a library built against an earlier 3.x has a shorter table.
template <typename T> bool reaches(const T *member)
{
    return g_hooks && (uint32_t)((const char *)member - (const char *)g_hooks) + sizeof(T) <= g_size && *member != 0;
}
#define HOOK(m) (reaches(&g_hooks->m) ? g_hooks->m : 0)
}

void hooks_attach(void)
{
    hooks_detach();
    uint32_t size = 0;
    const CoreHooks *t = (const CoreHooks *)libtab_export(COREHOOKS_NAME, COREHOOKS_MAJOR, &size);   // base 4.0: the linked slot table (libslot.h), exact major
    if (!t || size < sizeof(uint32_t) || t->size < sizeof(uint32_t)) {
        return;
    }
    g_hooks = t;
    g_size = size < t->size ? size : t->size;
}

void hooks_detach(void)
{
    g_hooks = 0;
    g_size = 0;
    g_armed = false;
    g_enter = 0;
    g_leave = 0;
    g_user = 0;
}

int hooks_attach_table(const void *table, uint32_t size, HookEnter enter, HookLeave leave, void *user)
{
    hooks_detach();
    const CoreHooks *t = (const CoreHooks *)table;
    if (!t || size < sizeof(uint32_t) || t->size < sizeof(uint32_t)) {
        return -1;
    }
    g_hooks = t;
    g_size = size < t->size ? size : t->size;
    g_enter = enter;
    g_leave = leave;
    g_user = user;
    return 0;
}

bool hooks_present(void)
{
    return g_hooks != 0;
}

int hooks_start(CoreCtx *c, char *msg, int msglen)
{
    int rc = 0;
    if (msg && msglen > 0) {
        msg[0] = 0;
    }
    int (*start)(char *, int) = HOOK(start);
    if (start && msg && msglen > 0) {
        Guard scope;
        rc = start(msg, msglen) ? 1 : 0;
        msg[msglen - 1] = 0;
    }
    if (!g_hooks) {
        return rc;
    }
    g_armed = true;
    for (int i = 0; i < baseset_count(); i++) {
        const char *key = baseset_key(i);
        if (c && c->settings && c->settings->find(key) >= 0) {
            hooks_base_changed(key);
        }
    }
    return rc;
}

bool hooks_has_menu_closed(void)
{
    return HOOK(menu_closed) != 0;
}

bool hooks_menu_closed(void)
{
    void (*f)(void) = HOOK(menu_closed);
    if (!f) {
        return false;
    }
    Guard scope;
    f();
    return true;
}

void hooks_menu_video(bool behind)
{
    void (*f)(int) = HOOK(menu_video);
    if (f) {
        Guard scope;
        f(behind ? 1 : 0);
    }
}

void hooks_base_changed(const char *key)
{
    void (*f)(const char *) = HOOK(base_setting_changed);
    if (f && g_armed) {
        Guard scope;
        f(key);
    }
}

// ---- the descriptor's hooks (core_hooks.h: init, release, warm_reset, cold_start, media_flush, media_pending) and the mount slots, for a core whose record names the library

namespace {
// The call of a hook that has the form of CoreDesc's: 0 done, or 1 with the reason in msg. A member the table lacks answers 0 and does nothing.
int call_hook(int (*f)(char *, int), char *msg, int msglen)
{
    if (msg && msglen > 0) {
        msg[0] = 0;
    }
    if (!f) {
        return 0;
    }
    int rc;
    {
        Guard scope;
        rc = f(msg, msglen);
    }
    if (msg && msglen > 0) {
        msg[msglen - 1] = 0;
    }
    return rc ? 1 : 0;
}

int d_init(CoreCtx *, char *msg, int msglen) { return call_hook(HOOK(init), msg, msglen); }
int d_release(CoreCtx *, char *msg, int msglen) { return call_hook(HOOK(release), msg, msglen); }
int d_warm_reset(CoreCtx *, char *msg, int msglen) { return call_hook(HOOK(warm_reset), msg, msglen); }
int d_cold_start(CoreCtx *, char *msg, int msglen) { return call_hook(HOOK(cold_start), msg, msglen); }
int d_media_flush(CoreCtx *, char *msg, int msglen) { return call_hook(HOOK(media_flush), msg, msglen); }

bool d_media_pending(CoreCtx *)
{
    bool (*f)(void) = HOOK(media_pending);
    if (!f) {
        return false;
    }
    Guard scope;
    return f();
}

// The mount slots: the base's buffer for the spec, the adapters to the three members. `slot` is the position in the record's [mount] list, which is the position in MountSlots::names.
char g_spec[Settings::MOUNT_VAL_LEN];
const char *mount_names[MOUNT_SLOTS_MAX];
MountSlots g_slots;
Setting g_settings[SETTINGS_MAX];                       // the descriptor's settings without the Persist row, when the slots are taken away

const char *s_spec(CoreCtx *, int i)
{
    int (*f)(int, char *, int) = HOOK(slot_spec);
    if (!f) {
        return 0;
    }
    int n;
    {
        Guard scope;
        g_spec[0] = 0;
        n = f(i, g_spec, (int)sizeof(g_spec));
    }
    g_spec[sizeof(g_spec) - 1] = 0;
    if (n < 0 || n >= (int)sizeof(g_spec)) {
        printf("[core] the library's slot_spec refused slot %d: the spec does not fit %d bytes\n", i, (int)sizeof(g_spec) - 1);
        return 0;
    }
    return n == 0 ? 0 : g_spec;
}

int s_mount(CoreCtx *, int i, const char *spec, char *msg, int msglen)
{
    int (*f)(int, const char *, char *, int) = HOOK(slot_mount);
    if (msg && msglen > 0) {
        msg[0] = 0;
    }
    if (!f) {
        return 1;
    }
    int rc;
    {
        Guard scope;
        rc = f(i, spec, msg, msglen);
    }
    if (msg && msglen > 0) {
        msg[msglen - 1] = 0;
    }
    return rc ? 1 : 0;
}

int s_eject(CoreCtx *, int i, char *msg, int msglen)
{
    int (*f)(int, char *, int) = HOOK(slot_eject);
    if (msg && msglen > 0) {
        msg[0] = 0;
    }
    if (!f) {
        return 1;
    }
    int rc;
    {
        Guard scope;
        rc = f(i, msg, msglen);
    }
    if (msg && msglen > 0) {
        msg[msglen - 1] = 0;
    }
    return rc ? 1 : 0;
}
}

const char *hooks_bind(CoreDesc *d)
{
    if (!d || !d->hooks_lib) {
        return 0;
    }
    d->init = d_init;                                   // boot.cc calls both without a test: the library's, or nothing
    d->release = d_release;
    d->warm_reset = HOOK(warm_reset) ? d_warm_reset : 0;
    d->cold_start = HOOK(cold_start) ? d_cold_start : 0;
    d->media_flush = HOOK(media_flush) ? d_media_flush : 0;
    d->media_pending = HOOK(media_pending) ? d_media_pending : 0;
    const char *err = g_hooks ? 0 : "the library that is the code of this core has no table of hooks: nothing starts or releases the machine";
    if (d->mount_slots && d->mount_slots->n > 0) {
        const MountSlots *rec = d->mount_slots;
        if (HOOK(slot_spec) && HOOK(slot_mount) && HOOK(slot_eject)) {
            int n = rec->n < MOUNT_SLOTS_MAX ? rec->n : MOUNT_SLOTS_MAX;
            for (int i = 0; i < n; i++) {
                mount_names[i] = rec->names[i];
            }
            g_slots.n = n;
            g_slots.names = mount_names;
            g_slots.spec = s_spec;
            g_slots.mount = s_mount;
            g_slots.eject = s_eject;
            d->mount_slots = &g_slots;
        } else {
            d->mount_slots = 0;                         // a slot the code cannot serve is no row (R20)
            if (!mounts_have_slots(d)) {                // and "Persist medium mounts" (added from the record's names) does nothing without a slot: out too
                int n = 0;
                for (int i = 0; i < d->nsettings && n < SETTINGS_MAX; i++) {
                    if (strcasecmp(d->settings[i].key, PERSIST_KEY)) {
                        g_settings[n++] = d->settings[i];
                    }
                }
                d->settings = g_settings;
                d->nsettings = n;
            }
            if (!err) {
                err = "the library's table lacks slot_spec, slot_mount or slot_eject: the media slots of the record are left out";
            }
        }
    }
    return err;
}
