/*
 * trex_main.c - the task of TREX CRT: the three tools (dump, flash, inspect) behind the screens of app.toml.
 *
 * The old application ran one tool at a time in a loop that fed it keys. The screens are the base's now (T104.3 D0, D9.1); what is left of the loop is this: the
 * operations the screens call (glue.cc) put a command in a small queue, this task takes them one at a time and runs what the key of the old tool did - the tool's
 * own poll function - with the dialogs it always asked in. One session on the cartridge port serves all three tools; it is opened here and ended here.
 */

#include "trex_ui.h"
#include "cart_dumper.h"
#include "cart_writer.h"
#include "cart_inspector.h"

const u2p_api_t *g_api;

typedef struct {
    int kind, tool, key;
} cmd_t;

#define QUEUE_MAX 8
static cmd_t s_q[QUEUE_MAX];
static volatile int s_head, s_tail;      // one producer (the executor), one consumer (this task)

static cart_dumper_t s_dumper;
static cart_writer_t s_writer;
static cart_inspector_t s_inspector;
static bool s_inited[4];
static char s_pend_path[256], s_pend_name[256];     // the CRT the Flash screen starts with (the action `write`, or the source the person chose first)

bool trex_post(int kind, int tool, int key)
{
    int next = (s_head + 1) % QUEUE_MAX;
    if (next == s_tail) {
        return false;                    // full: the operation says the tool is busy
    }
    s_q[s_head].kind = kind;
    s_q[s_head].tool = tool;
    s_q[s_head].key = key;
    s_head = next;
    return true;
}

int trex_queued(void)
{
    return (s_head - s_tail + QUEUE_MAX) % QUEUE_MAX;
}

static void split_path(const char *full, char *dir, int dir_size, char *name, int name_size)
{
    int slash = -1;
    for (int i = 0; full[i]; i++) {
        if (full[i] == '/') {
            slash = i;
        }
    }
    int n = 0;
    if (slash == 0) {
        dir[n++] = '/';
    }
    for (int i = 0; i < slash && n < dir_size - 1; i++) {
        dir[n++] = full[i];
    }
    dir[n] = 0;
    strncpy(name, full + slash + 1, name_size - 1);
    name[name_size - 1] = 0;
}

// A tool is started when its screen is first shown (or asked for): the probe, the scan, the slot question - each in the dialogs it always had
static void ensure(int tool)
{
    if (s_inited[tool]) {
        return;
    }
    s_inited[tool] = true;
    switch (tool) {
    case TOOL_DUMP:
        UI_SET(status, "Looking at the cartridge ...");
        cart_dumper_init(&s_dumper);
        break;
    case TOOL_FLASH:
        UI_SET(status, "Looking at the cartridge ...");
        cart_writer_init(&s_writer, s_pend_path, s_pend_name);
        break;
    case TOOL_INSPECT:
        cart_inspector_init(&s_inspector);
        break;
    }
}

static void run_key(int tool, int key)
{
    ensure(tool);
    switch (tool) {
    case TOOL_DUMP:
        if (key == KEY_F1 && s_dumper.det_banks == 0) {      // no cartridge was found: a scan looks again
            s_inited[TOOL_DUMP] = false;
            ensure(TOOL_DUMP);
            break;
        }
        cart_dumper_poll(&s_dumper, key);
        break;
    case TOOL_FLASH:
        if (key == KEY_F1 && !s_writer.chip) {
            char p[256], n[256];
            strcpy(p, s_writer.crt_path);
            strcpy(n, s_writer.crt_filename);
            strcpy(s_pend_path, p);
            strcpy(s_pend_name, n);
            s_inited[TOOL_FLASH] = false;
            ensure(TOOL_FLASH);
            break;
        }
        cart_writer_poll(&s_writer, key);
        break;
    case TOOL_INSPECT:
        if (s_inspector.failed && key == KEY_F1) {
            s_inited[TOOL_INSPECT] = false;
            ensure(TOOL_INSPECT);
            break;
        }
        cart_inspector_poll(&s_inspector, key);
        break;
    }
}

// The source of the Flash screen: a .crt of the medium, chosen in the base's file picker, looked at before it is taken
static void choose_source(void)
{
    char path[256] = "";
    if (!g_api->ui->pick_file || g_api->ui->pick_file("/", ".crt", path, sizeof(path)) < 0 || !path[0]) {
        return;
    }
    char dir[256], name[256];
    split_path(path, dir, sizeof(dir), name, sizeof(name));
    if (s_inited[TOOL_FLASH]) {
        const char *bad = cart_writer_set_crt(&s_writer, dir, name);
        if (bad) {
            cart_notify(bad);
        } else {
            UI_SET(status, "CRT chosen: %s", name);
        }
        return;
    }
    const char *bad = cart_writer_check_crt(dir, name);
    if (bad) {
        cart_notify(bad);
        return;
    }
    strcpy(s_pend_path, dir);
    strcpy(s_pend_name, name);
    UI_SET(src, "%s/%s", dir, name);
    UI_SET(status, "CRT chosen: %s", name);
}

static void idle(void)
{
    g_api->sys->delay_ms(10);
}

static void end_all(void)
{
    if (s_inited[TOOL_DUMP]) {
        cart_ef3_restore_slot(&s_dumper.base);
    }
    if (s_inited[TOOL_FLASH]) {
        if (s_writer.crt_data) {
            g_api->mem->free(s_writer.crt_data);
            s_writer.crt_data = NULL;
        }
        cart_ef3_restore_slot(&s_writer.base);
    }
    slot_wr_close();                                    // the tools close it themselves; once more for any way out they missed
    cart_session_end();                                 // the bus back, and the C64 reset when the cartridge was touched
}

int trex_main(const u2p_api_t *api, const char *action, const char *file)
{
    g_api = api;
    u2p_ui_init(api);
    if (!g_api->c64) {                                  // the C64 service is only in a core of the TRX64 line (appsvc_c64); the index offers the tools nowhere else
        LOG("[TrexCrt] this core gives no C64 service: no cartridge port\n");
        return -1;
    }
    core_detect();
    memset(&g_ui, 0, sizeof(g_ui));
    UI_SET(type, "-");
    UI_SET(chip, "TREX CRT");
    UI_SET(slot_short, "-");
    UI_SET(port, "-");
    UI_SET(gate, "-");
    UI_SET(dest, "asked when you dump");
    UI_SET(src, "no CRT chosen: Change source");
    UI_SET(status, "Choose a tool");
    LOG("[TrexCrt] action=%s file=%s\n", action && action[0] ? action : "-", file && file[0] ? file : "-");

    int first = TOOL_NONE;
    const char *screen = "home";
    if (action && !strcmp(action, "dump")) {
        first = TOOL_DUMP;
        screen = "dump";
    } else if (action && !strcmp(action, "inspect")) {
        first = TOOL_INSPECT;
        screen = "inspect";
    } else if (action && (!strcmp(action, "flash") || !strcmp(action, "write") || !strcmp(action, "verify"))) {
        first = TOOL_FLASH;
        screen = "flash";
        if (file && file[0]) {
            // A CRT is looked at before the screen works on it: a file the writer could not use never gets a write screen
            split_path(file, s_pend_path, sizeof(s_pend_path), s_pend_name, sizeof(s_pend_name));
            const char *bad = cart_writer_check_crt(s_pend_path, s_pend_name);
            if (bad) {
                cart_notify(bad);
                s_pend_path[0] = s_pend_name[0] = 0;
            }
        }
    }
    if (first == TOOL_NONE) {
        cart_session_open(false);                       // the port is looked at once, so that Home can say what is in it
        UI_SET(port, "%s", is_trx() ? "READY" : "-");
    } else {
        ui_show(screen);
        trex_post(CMD_ENTER, first, 0);
    }

    while (!ui_cancelled()) {
        if (s_tail == s_head) {
            idle();
            continue;
        }
        cmd_t c = s_q[s_tail];
        s_tail = (s_tail + 1) % QUEUE_MAX;
        switch (c.kind) {
        case CMD_ENTER:
            ensure(c.tool);
            break;
        case CMD_KEY:
            run_key(c.tool, c.key);
            break;
        case CMD_DEST:
            ensure(TOOL_DUMP);
            cart_dumper_choose_dest(&s_dumper);
            break;
        case CMD_SOURCE:
            choose_source();
            break;
        }
    }
    end_all();
    return 0;
}
