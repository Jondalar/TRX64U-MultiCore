// The host application's declaration (hostdecl.h): pages, items and actions, as constant tables (the key map is host_core.cc's). Nothing here draws; the screen is hostui.cc's.
#include "hostdecl.h"
#include <string.h>
#include <stdio.h>

namespace {

// HA_KEYS: an intent of host_core with the action's name (host_core.h; the keys of the board take the same intents through the one key map in host_core.cc).
// HA_FIELD: a step of a settings or WiFi field.
#define ACT(name, title, kind, view, src, field, dir, flags, ask, mode, when) { name, title, kind, view, src, field, dir, flags, ask, mode, when }
#define KEYS(name, title, view, src, flags, ask, mode, when) ACT(name, title, HA_KEYS, view, src, 0, 0, flags, ask, mode, when)
#define KEYSF(name, title, view, field, flags, ask, when) ACT(name, title, HA_KEYS, view, 0, field, 0, flags, ask, -1, when)
#define FIELD(name, title, view, field, dir, flags, when) ACT(name, title, HA_FIELD, view, 0, field, dir, flags, 0, -1, when)

const HAction ACTIONS[] = {
    // ---- the shell: the five views and the picture --------------------------------------------------------------------------------------
    KEYS("nav.menu", "Boot menu", -1, 0, HAF_GLOBAL, 0, -1, "any screen"),
    KEYS("nav.browse", "File Browser", -1, 0, HAF_GLOBAL, 0, -1, "any screen"),
    KEYS("nav.edit", "Edit menu", -1, 0, HAF_GLOBAL, 0, -1, "any screen"),
    KEYS("nav.net", "Network", -1, 0, HAF_GLOBAL, 0, -1, "any screen"),
    KEYS("nav.settings", "Settings", -1, 0, HAF_GLOBAL, 0, -1, "any screen"),
    KEYS("picture.step", "Next picture mode", -1, 0, HAF_GLOBAL, 0, -1, "any screen: V opens the Picture page, the modes the screen names; nothing is applied while browsing"),
    KEYS("picture.keep", "Keep the picture", -1, 0, HAF_GLOBAL, 0, -1, "while the picture question is up: RETURN or any key but LEFT / ESC"),
    KEYS("picture.revert", "Go back to the previous picture", -1, 0, HAF_GLOBAL, 0, -1, "while the picture question is up: LEFT or ESC"),
    KEYS("picture.apply", "Use the picture mode under the cursor", -1, 0, 0, 0, -1, "the Picture page: asks for a key within 8 s"),
    KEYS("cursor.up", "Up", -1, 0, 0, 0, -1, "the row above"),
    KEYS("cursor.down", "Down", -1, 0, 0, 0, -1, "the row below"),
    KEYS("back", "Back", -1, 0, 0, 0, -1, "ESC of the page"),
    // ---- the boot menu (F1) and the editor (F5): the same entries ------------------------------------------------------------------------
    KEYS("entry.start", "Start", HVW_MENU, "entry", HAF_ROW | HAF_FLOW, 0, -1, "a startable entry; foreign firmware asks first"),
    // ---- the old web page's commands (T106) as intents of the service: from any view, the screen stays where it is; the question for foreign firmware is not asked (nobody there can answer it)
    KEYS("web.start", "Start (web page)", -1, "entry", HAF_ROW | HAF_FLOW, 0, -1, "the T106 page's start: as Start, with no question"),
    KEYS("web.default", "Make auto (web page)", -1, "entry", HAF_ROW, 0, -1, "the T106 page's default"),
    KEYS("web.video", "Picture (web page)", -1, 0, 0, 0, -1, "the T106 page's picture: the argument is the mode"),
    KEYS("web.countdown", "Countdown (web page)", -1, 0, 0, 0, -1, "the T106 page's countdown: the argument is the seconds, 0 is off"),
    KEYS("config.list", "Configs", HVW_MENU, "entry", HAF_ROW, 0, -1, "a core with configs: opens the list, the cursor on its auto config; none: nothing happens"),
    KEYS("config.start", "Start with the config", -1, 0, 0, 0, -1, "while the config list is open: the config under the cursor (the first row is the plain start)"),
    KEYS("config.rename", "Rename the config", -1, 0, HAF_FLOW, 0, -1, "while the config list is open: opens the name field"),
    KEYS("config.delete", "Delete the config", -1, 0, HAF_DESTRUCTIVE | HAF_FLOW, "Delete this config?", -1, "while the config list is open: the screen arms with DEL and deletes with the second DEL"),
    KEYS("config.default", "Make the auto config", -1, 0, 0, 0, -1, "while the config list is open: marks the config under the cursor as the entry's auto config, again clears it"),
    KEYS("entry.recheck", "Check again", HVW_MENU, "entry", HAF_ROW, 0, -1, "the full check of the entry, stored anew"),
    KEYS("entry.move_up", "Move up", HVW_EDIT, "entry", HAF_ROW, 0, -1, "the editor"),
    KEYS("entry.move_down", "Move down", HVW_EDIT, "entry", HAF_ROW, 0, -1, "the editor"),
    KEYS("entry.default", "Make auto", HVW_EDIT, "entry", HAF_ROW, 0, -1, "the editor"),
    KEYS("entry.remove", "Remove from the menu", HVW_EDIT, "entry", HAF_ROW | HAF_DESTRUCTIVE, "Remove this entry from the boot menu?", -1, "the editor: the screen arms with DEL and removes with the second DEL"),
    KEYS("entry.config_prev", "Auto config, previous", HVW_EDIT, "entry", HAF_ROW, 0, -1, "the editor: none / the core's configs, backwards"),
    KEYS("entry.config_next", "Auto config, next", HVW_EDIT, "entry", HAF_ROW, 0, -1, "the editor: none / the core's configs, forwards"),
    KEYS("entry.rename", "Rename", HVW_EDIT, "entry", HAF_ROW | HAF_FLOW | HAF_TEXT, 0, -1, "the editor: opens the name field; `value` is the new name"),
    // ---- the File Browser (F3) ---------------------------------------------------------------------------------------------------------
    KEYS("browse.open", "Open", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, -1, "a folder opens; in the pick modes RETURN is the pick"),
    KEYS("browse.up", "Parent folder", HVW_BROWSE, 0, 0, 0, -1, "not at the media level"),
    KEYS("menu.page_down", "Page down", HVW_MENU, 0, 0, 0, -1, "one page of rows"),
    KEYS("menu.page_up", "Page up", HVW_MENU, 0, 0, 0, -1, "one page of rows"),
    KEYS("browse.page_down", "Page down", HVW_BROWSE, 0, 0, 0, -1, "one page of rows"),
    KEYS("browse.page_up", "Page up", HVW_BROWSE, 0, 0, 0, -1, "one page of rows"),
    KEYS("browse.add", "Add to the boot menu", HVW_BROWSE, "file", HAF_ROW, 0, HMODE_NORMAL, "a .cor bundle or a .cfw firmware"),
    KEYS("browse.start", "Start once", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "a .cor bundle or a .cfw firmware"),
    KEYS("browse.copy", "Copy to ...", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "then pick the folder with the browser itself"),
    KEYS("browse.move", "Move to ...", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "then pick the folder with the browser itself"),
    KEYS("browse.delete", "Delete", HVW_BROWSE, "file", HAF_ROW | HAF_DESTRUCTIVE, "Delete this file?", HMODE_NORMAL, "the screen arms with DEL and deletes with the second DEL"),
    KEYS("browse.convert", "Convert a firmware", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "a .cfw / .ue2 file"),
    KEYS("browse.restore", "Restore a firmware", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "a .cfw file or a backup .cor; asks before any write"),
    KEYS("browse.update", "Install an update", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "a .txu file; asks before any write"),
    KEYS("browse.unzip", "Unzip", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "a .zip file; asks into a folder, here or one file"),
    KEYS("browse.format", "Format the medium", HVW_BROWSE, "file", HAF_ROW | HAF_FLOW, 0, HMODE_NORMAL, "at the media level; its pages ask for the medium's name"),
    KEYS("browse.pick_cancel", "Cancel the pick", HVW_BROWSE, 0, 0, 0, -1, "copy / move target pick, firmware inspection pick"),
    ACT("browse.pick_here", "Use this folder", HA_FIELD, HVW_BROWSE, 0, "pick.here", 1, HAF_FLOW, 0, HMODE_PICK, "the folder the browser shows is the target of the copy or move"),
    // ---- the settings (F8) -----------------------------------------------------------------------------------------------------------
    FIELD("set.direct", "Direct boot", HVW_SET, "settings.direct", 1, 0, "toggles"),
    FIELD("set.auto", "Autostart from the menu", HVW_SET, "settings.auto", 1, 0, "toggles; only with direct boot off"),
    FIELD("set.secs_up", "Autostart delay +1 s", HVW_SET, "settings.secs", 1, 0, "autostart on"),
    FIELD("set.secs_down", "Autostart delay -1 s", HVW_SET, "settings.secs", -1, 0, "autostart on"),
    FIELD("set.hdmi_next", "Picture: next mode", HVW_SET, "settings.hdmi", 1, 0, "only what the screen names; asks for a key within 8 s"),
    FIELD("set.hdmi_prev", "Picture: previous mode", HVW_SET, "settings.hdmi", -1, 0, "only what the screen names; asks for a key within 8 s"),
    FIELD("set.protect", "Protect the flash from updates", HVW_SET, "settings.protect", 1, 0, "toggles; only in a build with the flash lock"),
    FIELD("set.ftp", "FTP service", HVW_SET, "settings.ftp", 1, 0, "toggles"),
    FIELD("set.wifi_open", "WiFi page", HVW_SET, "settings.wifi", 1, 0, "opens the WiFi page"),
    FIELD("set.esp_inspect", "Inspect a .cfw / .ue2 for the ESP32", HVW_SET, "settings.esp", 1, HAF_FLOW, "opens the File Browser to pick the file"),
    FIELD("set.cores_open", "Cores and ROMs page", HVW_SET, "settings.cores", 1, 0, "opens the page"),
    FIELD("set.report", "Write the bug report", HVW_SET, "settings.report", 1, HAF_FLOW, "report/<date-time>.txt on the SD card, else on a USB stick"),
    // ---- the WiFi page (over the settings) -----------------------------------------------------------------------------------------------
    FIELD("wifi.radio", "WiFi radio on / off", HVW_SET, "wifi.radio", 1, 0, "the WiFi page"),
    FIELD("wifi.dhcp", "Use DHCP", HVW_SET, "wifi.dhcp", 1, 0, "the WiFi page"),
    KEYSF("wifi.scan", "Select an access point from the list", HVW_SET, "wifi.radio", HAF_FLOW, 0, "the WiFi page: scans, then the list is the pick"),
    KEYSF("wifi.manual", "Enter an access point", HVW_SET, "wifi.radio", HAF_FLOW, 0, "the WiFi page: the name, then the authentication, then the key"),
    KEYSF("wifi.last", "Connect to the last access point", HVW_SET, "wifi.radio", HAF_FLOW, 0, "the WiFi page"),
    KEYSF("wifi.disconnect", "Disconnect", HVW_SET, "wifi.radio", HAF_FLOW, 0, "the WiFi page"),
    KEYSF("wifi.forget", "Forget the stored access points", HVW_SET, "wifi.radio", HAF_FLOW | HAF_DESTRUCTIVE, "Forget all stored access points?", "the WiFi page: F, F"),
    // ---- Cores and ROMs (over the settings): the question is the page's own, its answer is Y --------------------------------------------------
    KEYSF("cores.delete", "Delete what belongs to no core", HVW_SET, 0, HAF_FLOW | HAF_DESTRUCTIVE, "Delete the files that belong to no core?", "the Cores and ROMs page: the page asks twice"),
    // ---- the network page (F7) ---------------------------------------------------------------------------------------------------------
    KEYS("net.colors", "Colour test", HVW_NET, 0, 0, 0, -1, "the layer proof screen; ESC returns"),
};

// ---- the rows of the pages that are always the same ---------------------------------------------------------------------------------
const HItem ITEMS[] = {
    { "settings", "direct", HI_TOGGLE, "Direct boot", "settings.direct", "set.direct", 0 },
    { "settings", "auto", HI_TOGGLE, "Autostart from menu", "settings.auto", "set.auto", 0 },
    { "settings", "secs", HI_VALUE, "Autostart delay", "settings.secs", "set.secs_up", 0 },
    { "settings", "hdmi", HI_CHOICE, "Picture", "settings.hdmi", "set.hdmi_next", 0 },
    { "settings", "protect", HI_TOGGLE, "Protect Flash from updates", "settings.protect", "set.protect", 0 },
    { "settings", "wifi", HI_BUTTON, "WiFi", "settings.wifi", "set.wifi_open", 0 },
    { "settings", "ftp", HI_TOGGLE, "FTP service", "settings.ftp", "set.ftp", 0 },
    { "settings", "esp", HI_BUTTON, "Inspect .cfw/.ue2 for ESP32", "settings.esp", "set.esp_inspect", 0 },
    { "settings", "cores", HI_BUTTON, "Cores and ROMs", "settings.cores", "set.cores_open", 0 },
    { "settings", "report", HI_BUTTON, "Bug report", "settings.report", "set.report", 0 },
    { "settings/wifi", "state", HI_INFO, "State", "wifi.state", 0, 0 },
    { "settings/wifi", "net", HI_INFO, "Connected to", "wifi.net", 0, 0 },
    { "settings/wifi", "addr", HI_INFO, "IP address", "wifi.addr", 0, 0 },
    { "settings/wifi", "radio", HI_TOGGLE, "WiFi", "wifi.radio", "wifi.radio", 0 },
    { "settings/wifi", "dhcp", HI_TOGGLE, "Use DHCP", "wifi.dhcp", "wifi.dhcp", 0 },
    { "settings/wifi", "ip", HI_TEXT, "Static IP", "wifi.ip", 0, 0 },
    { "settings/wifi", "mask", HI_TEXT, "Static Netmask", "wifi.mask", 0, 0 },
    { "settings/wifi", "gw", HI_TEXT, "Static Gateway", "wifi.gw", 0, 0 },
    { "settings/wifi", "dns", HI_TEXT, "Static DNS", "wifi.dns", 0, 0 },
    { "settings/cores", "head", HI_INFO, "Cores and ROMs", "cores.head", "cores.delete", 0 },
    { "net", "ethernet", HI_INFO, "Ethernet", "net.eth", 0, 0 },
    { "net", "ftp", HI_INFO, "FTP", "net.ftp", 0, 0 },
    { "net", "media", HI_INFO, "Media", "net.media", 0, 0 },
    { "net", "wifi_state", HI_INFO, "State", "net.wifi_state", 0, 0 },
    { "net", "wifi_net", HI_INFO, "Connected to", "net.wifi_net", 0, 0 },
    { "net", "wifi_addr", HI_INFO, "IP address", "net.wifi_addr", 0, 0 },
    { "net", "wifi_mac", HI_INFO, "MAC", "net.wifi_mac", 0, 0 },
    { "net", "note", HI_INFO, "Note", "net.note", 0, 0 },
};

const HPage PAGES[] = {
    { "menu", "Boot menu", HVW_MENU, "f1", "entry", "entry:", "entry.start" },
    { "browse", "File Browser", HVW_BROWSE, "f3", "file", "fs:", "browse.open" },
    { "edit", "Edit menu", HVW_EDIT, "f5", "entry", "entry:", 0 },
    { "net", "Network", HVW_NET, "f7", 0, 0, 0 },
    { "settings", "Settings", HVW_SET, "f8", 0, 0, 0 },
    { "settings/wifi", "WiFi", HVW_SET, 0, 0, 0, 0 },
    { "settings/cores", "Cores and ROMs", HVW_SET, 0, 0, 0, 0 },
    { "colors", "Colour test", HVW_COLORS, 0, 0, 0, 0 },
};

const char *const ROW_ENTRY[] = { "entry.start", "config.list", "entry.recheck", 0 };
const char *const ROW_EDIT[] = { "entry.move_up", "entry.move_down", "entry.default", "entry.config_prev", "entry.config_next", "entry.rename", "entry.remove", 0 };
const char *const ROW_FILE[] = { "browse.open", "browse.add", "browse.start", "browse.copy", "browse.move", "browse.delete", "browse.convert", "browse.restore",
                                 "browse.update", "browse.unzip", "browse.format", 0 };

const HostDecl DECL = {
    PAGES, (int)(sizeof(PAGES) / sizeof(PAGES[0])), ITEMS, (int)(sizeof(ITEMS) / sizeof(ITEMS[0])), ACTIONS, (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0])),
    ROW_ENTRY, ROW_EDIT, ROW_FILE
};

}  // namespace

const HostDecl &host_decl() { return DECL; }

const HAction *hd_action(const HostDecl &d, const char *name)
{
    for (int i = 0; i < d.nactions; i++) {
        if (!strcmp(d.actions[i].name, name)) {
            return &d.actions[i];
        }
    }
    return 0;
}

const HPage *hd_page(const HostDecl &d, const char *id)
{
    for (int i = 0; i < d.npages; i++) {
        if (!strcmp(d.pages[i].id, id)) {
            return &d.pages[i];
        }
    }
    return 0;
}

const HPage *hd_page_of_view(const HostDecl &d, int view)
{
    for (int i = 0; i < d.npages; i++) {
        if (d.pages[i].view == view && d.pages[i].nav) {
            return &d.pages[i];
        }
    }
    return 0;
}

void hd_item_id(const HItem &it, char *out, int cap)
{
    int o = 0;
    for (const char *p = it.page; *p && o < cap - 1; p++) {
        out[o++] = *p == '/' ? '.' : *p;
    }
    if (o < cap - 1) {
        out[o++] = '.';
    }
    for (const char *p = it.id; *p && o < cap - 1; p++) {
        out[o++] = *p;
    }
    out[o] = 0;
}

const HPage &hd_current_page(const HostDecl &d, HostModel &m)
{
    int v = m.view();
    if (v == HVW_SET) {
        if (m.cores_page()) {
            return *hd_page(d, "settings/cores");
        }
        if (m.wifi_page()) {
            return *hd_page(d, "settings/wifi");
        }
    }
    const HPage *p = v == HVW_COLORS ? hd_page(d, "colors") : hd_page_of_view(d, v);
    return p ? *p : d.pages[0];
}
