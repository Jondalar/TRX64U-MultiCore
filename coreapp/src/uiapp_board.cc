// The board's side of the UI adapter (uiapp.cc): FreeRTOS critical sections, the menu task's identity, the network tasks' sleep, token material.
#include "uiapp.h"
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "ui.h"
#include "trxview.h"

namespace {

TaskHandle_t g_ui_task;

void b_lock(void) { portENTER_CRITICAL(); }
void b_unlock(void) { portEXIT_CRITICAL(); }
bool b_on_ui(void) { return xTaskGetCurrentTaskHandle() == g_ui_task; }
void b_sleep(int ms) { TickType_t t = pdMS_TO_TICKS(ms); vTaskDelay(t ? t : 1); }

// T192.1 Q3: the timer against the tick: the ticks do not line up with the 16 bit ms timer, so the low bits carry jitter. No true randomness (T192.1 §5).
int b_entropy(uint8_t *out, int n)
{
    int o = 0;
    for (int i = 0; i < 16 && o + 4 <= n; i++) {
        uint32_t v = ((uint32_t)getMsTimer() << 16) ^ (uint32_t)xTaskGetTickCount() ^ ((uint32_t)i * 40503u) ^ (uint32_t)(uintptr_t)out;
        memcpy(out + o, &v, 4);
        o += 4;
        vTaskDelay(1);
    }
    return o;
}

void b_shell(MenuCtl *m, UiShell *out)
{
    TrxShellInfo si;
    ui_shell_info_now(&si, m);
    snprintf(out->activity, sizeof(out->activity), "%s", si.activity ? si.activity : "");
    snprintf(out->footer_left, sizeof(out->footer_left), "%s", si.footer_left ? si.footer_left : "");
    out->nchips = si.nchips < 8 ? si.nchips : 8;
    for (int i = 0; i < out->nchips; i++) {
        snprintf(out->chips[i], sizeof(out->chips[i]), "%s", si.chips[i].label ? si.chips[i].label : "");
    }
}

}

const UiAppPlatform *uiapp_board_platform()
{
    g_ui_task = xTaskGetCurrentTaskHandle();          // called by the menu task, from uiapp_init's caller
    static const UiAppPlatform p = { b_lock, b_unlock, b_on_ui, b_sleep, b_entropy, b_shell };
    return &p;
}
