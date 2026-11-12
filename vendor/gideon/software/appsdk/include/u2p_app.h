/*
 * u2p_app.h — convenience header for .u2a apps.
 *
 * Brings in the ABI (u2p_app_api.h), the firmware's popup button and key code
 * values, and small helpers.
 */

#ifndef U2P_APP_H_
#define U2P_APP_H_

#include "u2p_app_api.h"
#include <string.h>
#include <stddef.h>

/* Popup buttons (ui->popup_buttons), identical to the firmware values. */
#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
#define BUTTON_ALL    0x08
#define BUTTON_CANCEL 0x10

/* Key codes (ui->getkey, ui->getch), identical to the firmware values.
 * Both calls return keymapped keys, and which function key carries
 * which meaning depends on the interface the machine presents: the
 * Ultimate one sends F1/F3/F5/F7 as KEY_PAGEUP, KEY_HELP, KEY_TASKS
 * and KEY_PAGEDOWN, the Commodore one as KEY_TASKS, KEY_PAGEUP,
 * KEY_PAGEDOWN and KEY_HELP. Ask ui->fkey for the meaning, not the key. */
#define KEY_BACK      0x08
#define KEY_TAB       0x09
#define KEY_RETURN    0x0D
#define KEY_BREAK     0x11
#define KEY_DOWN      0x12
#define KEY_HOME      0x13
#define KEY_ESCAPE    0x1B
#define KEY_RIGHT     0x1D
#define KEY_SPACE     0x20
#define KEY_SHIFT_SP  0xA0
#define KEY_CTRL_HOME 0x14
#define KEY_CTRL_TAP  0x1F8 /* CTRL alone, after ui->key_ctrl_tap(1) (C64 keyboard only) */
#define KEY_CTRL_A    0x01
#define KEY_CTRL_B    0x02
#define KEY_CTRL_C    0x03
#define KEY_CTRL_D    0x04
#define KEY_CTRL_E    0x05
#define KEY_CTRL_I    0x09
#define KEY_CTRL_J    0x0A
#define KEY_CTRL_L    0x0C
#define KEY_CTRL_N    0x0E
#define KEY_CTRL_O    0x0F
#define KEY_CTRL_V    0x16
#define KEY_CTRL_0    0xB0
#define KEY_CTRL_1    0xB1
#define KEY_CTRL_2    0xB2
#define KEY_CTRL_3    0xB3
#define KEY_CTRL_4    0xB4
#define KEY_CTRL_5    0xB5
#define KEY_CTRL_6    0xB6
#define KEY_CTRL_7    0xB7
#define KEY_CTRL_8    0xB8
#define KEY_CTRL_9    0xB9
#define KEY_CTRL_R    0xBA
#define KEY_A         0x61
#define KEY_S         0x73
#define KEY_D         0x64
#define KEY_W         0x77
#define KEY_F1        0x85
#define KEY_F3        0x86
#define KEY_F5        0x87
#define KEY_F7        0x88
#define KEY_F2        0x89
#define KEY_F4        0x8A
#define KEY_F6        0x8B
#define KEY_F8        0x8C
#define KEY_F9        0x8D
#define KEY_F10       0x8E
#define KEY_F11       0x8F
#define KEY_F12       0x90
#define KEY_UP        0x91
#define KEY_CLEAR     0x93
#define KEY_INSERT    0x94
#define KEY_LEFT      0x9D
#define KEY_PAGEUP    0x92
#define KEY_PAGEDOWN  0x95
#define KEY_END       0x96
#define KEY_DELETE    0x7F
#define KEY_ERR       0xFF
#define KEY_CAPS      0xA1
#define KEY_PRSCR     0xA2
#define KEY_SCRLOCK   0xA3
#define KEY_NUMLOCK   0xA4
#define KEY_MENU      0x1FE
#define KEY_SEARCH    0x1FD
#define KEY_TASKS     0x1FC
#define KEY_HELP      0x1FB
#define KEY_CONFIG    0x1FA
#define KEY_SYSINFO   0x1F9

/* Build the path to <app>.cfg next to the .u2a file.
 * Returns 0 on success, -1 if the buffer is too small or the app_path is
 * malformed. */
static inline int app_config_path(const u2p_app_context_t *ctx,
                                  char *out, size_t out_sz)
{
    if (!ctx || !ctx->app_path || !out || out_sz == 0) return -1;
    size_t n = strlen(ctx->app_path);
    /* Strip trailing ".u2a" if present; replace with ".cfg". */
    const char *suffix = ".u2a";
    const size_t sl = 4;
    if (n > sl && strcmp(ctx->app_path + n - sl, suffix) == 0) n -= sl;
    if (n + 4 + 1 > out_sz) return -1;  /* ".cfg\0" */
    memcpy(out, ctx->app_path, n);
    memcpy(out + n, ".cfg", 5);
    return 0;
}

#endif /* U2P_APP_H_ */
