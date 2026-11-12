// The board's side of the host's UI service (hostdomain.h): FreeRTOS critical sections, the UI task's identity, the network tasks' sleep, material for the token,
// and the token file /Flash/trx/ui.token (T192.1 Q2).
#include "hostdomain.h"
#include "hostfs.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"

namespace {

#define TOKEN_DIR "/Flash/trx"
#define TOKEN_FILE "ui.token"

TaskHandle_t g_ui_task;

void b_lock(void) { portENTER_CRITICAL(); }
void b_unlock(void) { portEXIT_CRITICAL(); }
bool b_on_ui(void) { return xTaskGetCurrentTaskHandle() == g_ui_task; }
void b_sleep(int ms) { TickType_t t = pdMS_TO_TICKS(ms); vTaskDelay(t ? t : 1); }
uint32_t b_now(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }

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

int b_load(char *buf, int cap) { return hostfs_load(TOKEN_DIR "/" TOKEN_FILE, buf, cap); }
bool b_save(const char *buf, int n) { return hostfs_save(TOKEN_DIR, TOKEN_FILE, buf, n); }
bool b_remove(void) { return hostfs_fo()->remove(TOKEN_DIR "/" TOKEN_FILE); }

}

const HostApiPlatform *hostapi_board_platform()
{
    g_ui_task = xTaskGetCurrentTaskHandle();          // called by the UI task
    static const HostApiPlatform p = { b_lock, b_unlock, b_on_ui, b_sleep, b_entropy, b_now, b_load, b_save, b_remove };
    return &p;
}
