// The core-application blueprint (T93): the program that runs after the host app has loaded a core from a .cor bundle.
// One image for every core with a descriptor (D14); the descriptor is chosen at run time from the core's ID block (D4).
// FreeRTOS, USB, SD, FatFS, the file system layer, lwIP and FTP are Gideon's GPL firmware (host-app/PROVENANCE.md).
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "init_function.h"
#include "itu.h"
#include "u64.h"
#include "product.h"
#include "usb_base.h"
#include "hdmi_scan.h"
#include "trx_io.h"
#include "gitinfo.h"
#include "startlog.h"
#include "start.h"
#include "ui.h"
#include "hw.h"
#include "board.h"
#include "fsif.h"
#include "run.h"

#include "videomode.h"
#include "handover.h"
#include "walltime.h"

// trx_io.cc asks which TCP port the monitor app serves; a core application has none.
extern "C" uint16_t app_trx_rpc_port(void) { return 0; }

// The scan mode the host application put up, which it hands over (T105): the host's scaler, overlay, pixel layer and
// HDMI encoder need one to show the pixel layer, and a core that came up in a different one from the menu the user
// just used would go black on the same screen. Without a hand-over - a core started over JTAG - the screen is asked.
static BoardProfile board;

static void video_init(const Handover *h)
{
    int sys = -1, hdmi = -1;
    if (h && handover_valid(h)) {
        video_unpack(h->video, &sys, &hdmi);
    }
    video_start(sys, hdmi, 0, 0);
}

static char raw_id[4];

// LVGL and the file system need more stack than the boot task has.
static void app_task(void *)
{
    hw_read_id((uint8_t *)raw_id);
    trx_io_alt_id = raw_id;                       // the pixel layer is the host's, whichever core answers
    // The hand-over page is read only where the profile says there is one: on the monolith it lies inside the cartridge ROM area.
    Handover h;
    const Handover *hp = 0;
    if (board.has_handover) {
        memcpy(&h, (const void *)HANDOVER_ADDR, sizeof(h));
        hp = &h;
        walltime_adopt(hp);                       // T191: the host's time, before any core asks for it (the Amiga RTC loads it at reset)
    }
    const CoreDesc *desc = 0;
    char msg[200];
    StartResult r = start_identify((const uint8_t *)raw_id, hp, &desc, msg, sizeof(msg), hw_io());
    STARTLOG("phase core identified: %s", msg);
    if (r != START_OK) {
        ui_open(0);
        coreapp_held(msg, &board);                // the core stays held (the host's power-up hold, nobody writes C64_MODE)
    }
    coreapp_run(desc, hp, &board);                // never returns
}

extern "C" void ultimate_main(void *a)
{
    hw_read_id((uint8_t *)raw_id);                // before the init functions: the RTC asks trx_io_caps() there, which would cache "no block" for an ID other than TRX2
    trx_io_alt_id = raw_id;
    printf("\n*** TRX64 core application %s (%s) ***\n", APP_VERSION_STRING, APP_BUILD_DATE);
    printf("*** FPGA Capabilities: %8x ***\n\n", getFpgaCapabilities());
    STARTLOG("phase main: the scheduler runs, the app starts");
    puts("Executing init functions.");
    InitFunction :: executeAll();
    STARTLOG("phase init functions done");
    usb2.initHardware();
    STARTLOG("phase USB hardware initialised (the USB task enumerates in the background)");
    board = board_detect(hw_board_probe());
    printf("[core] board: %s (%s)\n", board_name(board), board.why);
    fs_flash_writable() = board.kind == BOARD_MONOLITH;       // T228 R21: /flash writable as in TRX64-Ultimate's firmware, the MultiCore host keeps its protection (T93 S14)
    video_init(board.has_handover ? (const Handover *)HANDOVER_ADDR : 0);
    STARTLOG("phase video mode up (the host's, from the hand-over)");
    hw_release_expansion_port();                  // T135 D6: before the machine runs
    hw_rocker_init();                             // before anything polls the menu button (T93 5.13)
    printf("All linked modules have been initialized and are now running.\n");
    STARTLOG("phase handing over to the core app task");
    xTaskCreate(app_task, "Core app", 12 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskSuspend(NULL);
}
