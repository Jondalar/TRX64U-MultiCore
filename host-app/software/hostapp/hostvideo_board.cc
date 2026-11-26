// T164 WP17: the board side of hostvideo.h - the hot-plug line, the DDC read, the mode request register (overlay window 0x4F00..0x4F09) and the
// host's stored picture choice (/Flash/trx/host.cfg [Video], the T163 store). The policy is hostvideo.cc's and is benched with fake hooks.
#include "hostvideo.h"
#include "hostcfg.h"
#include "config.h"
#include "init_function.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u64.h"

#define CFG_HOST_VIDEO_MODE   0x01
#define CFG_HOST_VIDEO_SYSTEM 0x02

// Index 0 is "from the screen"; index 1 + VID_HDMI_* the resolutions (the labels are what the file shows).
static const char *mode_items[] = { "From the screen", "576p / 480p", "1280 x 720", "1920 x 1080", "800 x 600", "1024 x 768", "1280 x 1024" };
static const char *sys_items[] = { "PAL 50 Hz", "NTSC 60 Hz" };

static struct t_cfg_definition video_config[] = {
    { CFG_HOST_VIDEO_MODE,   CFG_TYPE_ENUM, "Mode",   "%s", mode_items, 0, 6, 0 },
    { CFG_HOST_VIDEO_SYSTEM, CFG_TYPE_ENUM, "System", "%s", sys_items,  0, 1, 0 },
    { CFG_TYPE_END,          CFG_TYPE_END,  "",       "",   NULL,       0, 0, 0 }
};

class HostVideoConfig : ConfigurableObject {
public:
    void init()
    {
        cfg = ConfigManager :: getConfigManager()->register_store(0x48535401, "Video", video_config, this);
        cfg->set_effectuated();
    }
    void load(int *hdmi, int *sys)
    {
        if (cfg) {
            *hdmi = cfg->get_value(CFG_HOST_VIDEO_MODE) - 1;
            *sys = cfg->get_value(CFG_HOST_VIDEO_SYSTEM);
        }
    }
    void store(int hdmi, int sys)
    {
        if (cfg) {
            cfg->set_value(CFG_HOST_VIDEO_MODE, hdmi + 1);
            cfg->set_value(CFG_HOST_VIDEO_SYSTEM, sys == VID_SYS_NTSC60 ? 1 : 0);
            cfg->write();
        }
    }
};

static HostVideoConfig hostVideoConfig;
InitFunction init_hostvideocfg("Host picture setting", [](void *, void *) { hostVideoConfig.init(); }, NULL, NULL, 21);

void hv_saved_load(int *hdmi, int *sys) { hostVideoConfig.load(hdmi, sys); }
void hv_saved_store(int hdmi, int sys) { hostVideoConfig.store(hdmi, sys); }

#define GW(off) (*(volatile uint8_t *)(VID_IO_BASE + (off)))

static bool b_hpd(void) { return (U64_HDMI_REG & U64_HDMI_HPD_CURRENT) != 0; }
static bool b_gw_present(void) { return GW(0x4F04) == 0x48 && GW(0x4F05) >= 1; }
static void b_gw_write(int row) { GW(0x4F00) = (uint8_t)row; }
static bool b_gw_status(HvGwStatus *s)
{
    s->flags = GW(0x4F06);
    s->mode = GW(0x4F07);
    s->written = GW(0x4F08);
    s->processed = GW(0x4F09);
    return GW(0x4F04) == 0x48;
}
static void b_gw_av_write(int mode, int std)
{
    if (GW(0x4F04) == 0x48 && GW(0x4F05) >= 2) {                  // a host older than version 2 has no A/V register
        GW(0x4F0A) = (uint8_t)((mode & 3) | ((std & 3) << 2));
    }
}
static void b_enable(void) { U64_HDMI_ENABLE = 1; }
static uint32_t b_ms(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static void b_pause(int ms) { TickType_t t = pdMS_TO_TICKS(ms); vTaskDelay(t ? t : 1); }

static const HostVideoIo board_io = { b_hpd, video_read_edid, b_gw_present, b_gw_write, b_gw_status, video_apply, b_enable, b_ms, b_pause, b_gw_av_write };

const HostVideoIo *hostvideo_board_io(void) { return &board_io; }
