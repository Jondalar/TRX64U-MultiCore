#include "menuflows.h"
#include "desc.h"
#include "ctx.h"
#include "regset.h"
#include "trx64u_ops.h"
#include "wifinet.h"
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

const char *menu_layout_enter(const char *layout_name)
{
    return layout_name && !strcmp(layout_name, "terminal") ? "monitor.open" : 0;
}

const char *menu_layout_tick(const char *layout_name)
{
    return layout_name && !strcmp(layout_name, "terminal") ? "monitor.tick" : 0;
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
