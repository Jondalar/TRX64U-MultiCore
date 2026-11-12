// TRX64 Ultimate host boot app (T84): the program that always starts on the C64U, shows the boot menu and
// starts a core from a .cor bundle on SD or USB. FreeRTOS, USB, SD, FatFS, the file system layer, lwIP and
// FTP are Gideon's GPL firmware (PROVENANCE.md); there is no C64 part in here.
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "init_function.h"
#include "itu.h"
#include "u64.h"
#include "product.h"
#include "usb_base.h"
#include "hdmi_scan.h"
#include "trx_io.h"
#include "hostui.h"
#include "cor.h"
#include "gitinfo.h"
#include "startlog.h"
#include "menukeys.h"
#include "hostcfg.h"

#include "videomode.h"
#include "hostvideo.h"
extern "C" void ResetHdmiPll(void);

// T84 D5b: which host is running. The host build writes the first 8 hex of ci/lib.sh's host_key
// into the bitstream's AXSS register (host/pins.xdc); USR_ACCESSE2 hands it to the design and the
// TRX IO block shows it as BUILD, four bytes from offset 0x008, present when CAPS2 bit 1 is set
// (trx64/trx_io.v). Everything the boot menu reads is checked against it, so a partial routed
// against another host is named instead of hanging in the ICAP. These two are not in the vendored
// trx_io.h, which is Gideon's tree with our block bolted on; they belong to this application.
#define TRX_BUILD_REG     (TRX_IO_BASE + 0x008)
#define TRX_CAPS2_BUILD   0x02

static uint32_t running_host_key(void)
{
    if (!trx_io_caps() || !(ioRead8(TRX_CAPS2) & TRX_CAPS2_BUILD)) {
        return 0;                       // no BUILD register: this host names no key
    }
    return (uint32_t)ioRead8(TRX_BUILD_REG) | ((uint32_t)ioRead8(TRX_BUILD_REG + 1) << 8) |
           ((uint32_t)ioRead8(TRX_BUILD_REG + 2) << 16) | ((uint32_t)ioRead8(TRX_BUILD_REG + 3) << 24);
}

// trx_io.cc asks which TCP port the monitor app serves; the host app has none.
extern "C" uint16_t app_trx_rpc_port(void) { return 0; }

// Video timing: the host's scaler, overlay, pixel layer and HDMI encoder need a scan mode before the
// pixel layer shows anything (the null core delivers no picture, T79.1 decision 2a). Which mode is
// videomode.cc's answer - a choice out of boot.cfg, else what the screen says it can do. It used to be
// PAL 50 Hz on 1920 x 1080 with no way out, and a screen that does not take that leaves a board that
// looks dead (T105).
//
// The flash disk is not mounted this early, so the stored choice arrives later: hostui_run() applies it
// once it has read boot.cfg. This first call is the one that gets a picture up at all.
static void video_init(void)
{
    hv_start(false, 0, 0);
}

// The boot task's stack is 1600 words; LVGL and the bundle check need more, so the menu gets its own task.
static void ui_task(void *)
{
    hostui_run();
}

extern "C" void ultimate_main(void *a)
{
    printf("\n*** TRX64 Ultimate host boot app %s (%s) ***\n", APP_VERSION_STRING, APP_BUILD_DATE);
    printf("*** FPGA Capabilities: %8x ***\n\n", getFpgaCapabilities());
    STARTLOG("phase main: the scheduler runs, the app starts");
    menukeys_sample();                  // T135 D2: C= / port-1 fire are latched from the first moment; hostui.cc decides later

    hostcfg_install_board();            // T163: our own settings file, before any store registers
    puts("Executing init functions.");
    InitFunction :: executeAll();
    STARTLOG("phase init functions done (network config, lwIP, RMII, FTP daemon object)");
    menukeys_sample();

    usb2.initHardware();
    STARTLOG("phase USB hardware initialised (the USB task enumerates in the background)");
    video_init();
    STARTLOG("phase video mode up (EDID read when boot.cfg names none; the first call)");
    menukeys_sample();
    printf("TRX IO block caps %02x\n", trx_io_caps());
    cor_host_key = running_host_key();
    if (cor_host_key) {
        printf("host key %08x (bundles for another host are refused)\n", (unsigned)cor_host_key);
    } else {
        puts("this bitstream names no host key: no bundle is refused for it");
    }
    printf("All linked modules have been initialized and are now running.\n");
    STARTLOG("phase handing over to the UI task");

    xTaskCreate(ui_task, "Host UI", 8 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskSuspend(NULL);
}
