/*
 * main.c
 *
 *  TREX CRT Tool entry point. Opens the Cart Inspector, the cartridge dumper
 *  or the cartridge writer, chosen by the manifest action the firmware passes
 *  in the launch context. A plain "Run App" shows a chooser first.
 */

#include "cart_dumper.h"
#include "cart_inspector.h"
#include "cart_writer.h"

const u2p_api_t *g_api;

static int poll_dumper(void *obj, int key) { return cart_dumper_poll((cart_dumper_t *)obj, key); }
static int poll_writer(void *obj, int key) { return cart_writer_poll((cart_writer_t *)obj, key); }
static int poll_inspector(void *obj, int key) { return cart_inspector_poll((cart_inspector_t *)obj, key); }

static void run_dumper(void)
{
    cart_dumper_t dumper;
    cart_dumper_init(&dumper);
    ui_run(&dumper, poll_dumper);
    cart_dumper_deinit(&dumper);
}

static void run_writer(const char *path, const char *filename)
{
    // A CRT is looked at before the screen opens: a file the writer could not use never gets a writer screen
    if (filename[0]) {
        const char *bad = cart_writer_check_crt(path, filename);
        if (bad) {
            cart_notify(bad);
            return;
        }
    }
    cart_writer_t writer;
    cart_writer_init(&writer, path, filename);
    ui_run(&writer, poll_writer);
    cart_writer_deinit(&writer);
}

static void run_inspector(void)
{
    cart_inspector_t inspector;
    cart_inspector_init(&inspector);
    ui_run(&inspector, poll_inspector);
    cart_inspector_deinit(&inspector);
}

// Same entries as the task menu of the manifest
static void run_chooser(void)
{
    static const char *items[] = { "Cart Inspector", "Dump Cartridge", "Flash Cartridge" };
    switch (u2p_ui_menu("TREX CRT Tool", items, 3)) {
    case 0:
        run_inspector();
        break;
    case 1:
        run_dumper();
        break;
    case 2:
        run_writer("", "");
        break;
    default:
        break;
    }
}

// Splits "/Usb0/games/foo.crt" into "/Usb0/games" and "foo.crt"
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

int app_main(const u2p_api_t *api, const u2p_app_context_t *ctx)
{
    g_api = api;
    u2p_ui_init(api);
    core_detect();

    const char *action = NULL;
    const char *selected = NULL;
    if (ctx && ctx->size >= sizeof(u2p_app_context_t)) {
        action = ctx->action;
        selected = ctx->selected_file;
    }
    LOG("[TrexCrt] action=%s file=%s\n", action ? action : "-", selected ? selected : "-");

    if (!action) {
        run_chooser();
    } else if (strcmp(action, "dump") == 0) {
        run_dumper();
    } else if (strcmp(action, "inspect") == 0) {
        run_inspector();
    } else if (strcmp(action, "flash") == 0) {
        run_writer("", "");
    } else if (strcmp(action, "write") == 0 && selected) {
        static char dir[256];
        static char name[256];
        split_path(selected, dir, sizeof(dir), name, sizeof(name));
        run_writer(dir, name);
    } else if (strcmp(action, "write") == 0) {
        run_writer("", "");
    } else if (strcmp(action, "verify") == 0 && selected) {
        static char dir[256];
        static char name[256];
        split_path(selected, dir, sizeof(dir), name, sizeof(name));
        run_writer(dir, name); // the writer's screen: F5 verifies
    } else {
        run_chooser();
    }
    slot_wr_close(); // the tools close it themselves; once more for any way out they missed
    return 0;
}
