#include "hookhost.h"
#include "core_hooks.h"
#include "baseset.h"
#include "settings.h"
#include <stddef.h>
#include <string.h>

namespace {
const CoreHooks *g_hooks;
uint32_t g_size;                   // how many bytes of the table the library has: the smaller of the call's answer and the table's own `size`
bool g_armed;                      // base_setting_changed is told from here on (after start)

// A member is called only when the library's table reaches it and it is set: a library built against an earlier 3.x has a shorter table.
template <typename T> bool reaches(const T *member)
{
    return g_hooks && (uint32_t)((const char *)member - (const char *)g_hooks) + sizeof(T) <= g_size && *member != 0;
}
#define HOOK(m) (reaches(&g_hooks->m) ? g_hooks->m : 0)
}

void hooks_attach(const SvcLib *lib)
{
    g_hooks = 0;
    g_size = 0;
    g_armed = false;
    if (!lib || !lib->find) {
        return;
    }
    uint32_t size = 0;
    const CoreHooks *t = (const CoreHooks *)lib->find(COREHOOKS_NAME, (uint32_t)COREHOOKS_MAJOR << 16, &size);
    uint32_t later = 0;
    if (!t || size < sizeof(uint32_t) || t->size < sizeof(uint32_t)) {
        return;
    }
    if (lib->find(COREHOOKS_NAME, (uint32_t)(COREHOOKS_MAJOR + 1) << 16, &later) == (const void *)t) {
        return;                    // the lowest major that is at least 1 is a later one: its layout is not ours
    }
    g_hooks = t;
    g_size = size < t->size ? size : t->size;
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

bool hooks_menu_closed(void)
{
    void (*f)(void) = HOOK(menu_closed);
    if (!f) {
        return false;
    }
    f();
    return true;
}

void hooks_base_changed(const char *key)
{
    void (*f)(const char *) = HOOK(base_setting_changed);
    if (f && g_armed) {
        f(key);
    }
}
