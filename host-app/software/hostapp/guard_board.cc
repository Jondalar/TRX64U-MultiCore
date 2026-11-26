// T265 P2/P3: the board's half of the guard (guard.h): the blocks in DDR2 read once at the start, the background job that brings the recovery program, the copy and
// the record up to date when this application was started from the flash, and the gate the flash writers wait at. The rules are guard.cc's, tested in tests/hostapp/test_guard.cc.
#include "guard.h"
#include "hostfs.h"
#include "flash.h"
#include "guardnote.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

// the recovery program this application carries: the image with its CRC-32 trailer (host-app/target/u64ii/riscv/hostrecovery, linked in by the Makefile)
extern "C" const uint8_t _recovery_slot_app_start[];
extern "C" const uint8_t _recovery_slot_app_end[];

static GuardInfo g_info;
static volatile bool g_busy, g_done;
static char g_note[GUARD_NOTE_MAX];
static volatile int g_tone;                       // 0: no note waiting; else the HcTone of the note

void guard_capture(void)
{
    volatile uint32_t *p = (volatile uint32_t *)GUARD_BLK_ADDR;
    uint32_t w[GUARD_BLK_WORDS];
    for (int i = 0; i < GUARD_BLK_WORDS; i++) {
        w[i] = p[i];
    }
    guard_decode(w, &g_info);
    p[GUARD_W_RECOVERY] = 0;                      // the blocks are read once: a later start of anything else must not read this one's
    p[GUARD_W_GUARD] = 0;
    p[GUARD_W_RESULT] = 0;
    printf("[host] guard: started %s (verdict %u, recovery status %u)\n", g_info.start == GS_FLASH ? "from the flash" : (g_info.start == GS_RECOVERY ? "by the recovery program" : "by something else"),
           (unsigned)g_info.verdict, (unsigned)g_info.rec_status);
}

const GuardInfo &guard_info(void) { return g_info; }

bool guard_busy(void) { return g_busy; }

void guard_wait_idle(void)
{
    for (int i = 0; g_busy && i < 1200; i++) {
        if (i == 0) {
            printf("[host] guard: waiting for the recovery copy to be written\n");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool guard_take_note(char *text, int max, int *tone)
{
    if (!g_tone) {
        return false;
    }
    snprintf(text, max, "%s", g_note);
    *tone = g_tone;
    g_tone = 0;
    return true;
}

static void log_line(const char *line) { printf("%s\n", line); }

static void set_note(const GuardProv &o)
{
    int tone = 4;                                 // HCT_WARN
    if (o.rec_failed) {
        snprintf(g_note, sizeof(g_note), "The recovery program could not be written. Restart the board to try again.");
    } else if (o.copy_room) {
        snprintf(g_note, sizeof(g_note), "The recovery copy does not fit on the flash drive. Delete files from it, then restart.");
    } else if (o.copy_failed) {
        snprintf(g_note, sizeof(g_note), "The recovery copy could not be written. Restart the board to try again.");
    } else if (o.record_failed) {
        snprintf(g_note, sizeof(g_note), "The guard record could not be written. Restart the board to try again.");
    } else if (!o.app_ok) {
        return;                                   // the flash holds another application than this one: nothing to say, nothing was written
    } else {
        return;
    }
    g_tone = tone;
}

static void provision_task(void *)
{
    FoFs *fs = hostfs_fo();
    guard_install_fs(fs);
    // the flash drive may not be mounted yet
    FoStat st;
    for (int i = 0; i < 120 && !fs->stat("/Flash", &st); i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    GuardEnv env = { get_flash(), _recovery_slot_app_start, (uint32_t)(_recovery_slot_app_end - _recovery_slot_app_start), log_line };
    GuardProv o;
    bool ok = guard_provision(env, g_info, &o);
    if (!ok) {
        set_note(o);
    }
    g_busy = false;
    g_done = true;
    vTaskDelete(NULL);
}

void guard_start_background(void)
{
    if (g_info.start != GS_FLASH || g_done || g_busy) {
        return;
    }
    g_busy = true;
    if (xTaskCreate(provision_task, "Guard", 8 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        g_busy = false;
        printf("[host] guard: the background job could not be started\n");
    }
}

void guard_board_install(void) { guard_install_fs(hostfs_fo()); }
