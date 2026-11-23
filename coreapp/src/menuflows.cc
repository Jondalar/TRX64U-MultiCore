#include "menuflows.h"
#include "desc.h"
#include "ctx.h"
#include "regset.h"
#include "trx64u_ops.h"
#include "wifinet.h"
#include "libfmt.h"
#include <stddef.h>
#include <string.h>

static const MenuWizard g_wizards[] = {
    { "pad.map.active", "pad.map.title", "pad.map.left", "pad.map.right", "pad.map.sub", "pad.map.next", "pad.map.back", "pad.map.cancel", "pad.map.tick" },
};

static const MenuFlow g_flows[] = {
    { "writeback.open", "writeback.ask", "writeback.answer", "writeback.leave", "RETURN saves, <- keeps the file as it is" },
};

const MenuWizard *menu_wizards(int *n)
{
    *n = (int)(sizeof(g_wizards) / sizeof(g_wizards[0]));
    return g_wizards;
}

const MenuFlow *menu_flows(int *n)
{
    *n = (int)(sizeof(g_flows) / sizeof(g_flows[0]));
    return g_flows;
}

static const MenuFacts g_facts = { "video.line" };
const MenuFacts *menu_facts() { return &g_facts; }

static const void *(*g_find)(const char *, uint32_t, uint32_t *);

void menu_set_export_finder(const void *(*find)(const char *, uint32_t, uint32_t *)) { g_find = find; }

// does the table cover the member: both the size find answered and the table's own size member reach past it? (a member added later is there only when both say so)
#define TG_HAS(g, size, member) ((size) >= offsetof(TextGrid, member) + sizeof((g)->member) && (g)->size >= offsetof(TextGrid, member) + sizeof((g)->member))

const TextGrid *menu_grid(const char *name)
{
    uint32_t size = 0;
    const TextGrid *g = name && name[0] && g_find ? (const TextGrid *)g_find(name, U2A3_VERSION(1, 0), &size) : 0;
    return g && TG_HAS(g, size, sig) && g->render && g->cols && g->rows && g->line && g->attr && g->blank_rows && g->cursor && g->sig ? g : 0;
}

bool menu_grid_enter(const char *name)
{
    const TextGrid *g = menu_grid(name);
    uint32_t size = g ? g->size : 0;
    if (!g || !TG_HAS(g, size, enter) || !g->enter) {
        return false;
    }
    g->enter();
    return true;
}

bool menu_grid_tick(const char *name)
{
    const TextGrid *g = menu_grid(name);
    uint32_t size = g ? g->size : 0;
    return g && TG_HAS(g, size, tick) && g->tick && g->tick();
}

bool menu_action_closes(CoreCtx *c, const FileAction &a)
{
    if (a.closes_menu && !strcmp(a.op, "drive.mount") && !cfg_value(c, "Leave Menu on Mount", 0)) {      // a plain mount; Mount & Run (drive.mount_run*) runs a program and always leaves
        return false;                                                // the setting says the menu stays: the status line says what was mounted
    }
    return a.closes_menu;
}

bool menu_link_online(const char *network_line)
{
    return network_line[0] && strcmp(network_line, NET_NONE_TEXT);
}
