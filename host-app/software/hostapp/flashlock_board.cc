// T152 D4/D6/D7: the board side of flashlock.h - the SPI flash controller's registers as a FlashSpi, and the F8 setting in the host's settings store
// (/Flash/trx/host.cfg, T163). The logic is flashlock.cc's and is benched against a chip model; this file is only the port.
#include "flashlock.h"
#include "hostcfg.h"
#include "config.h"
#include "w25q_flash.h"
#include "init_function.h"
#include "FreeRTOS.h"
#include "small_printf.h"

#define CFG_HOST_PROTECT_FLASH 0x01

static const char *off_on[] = { "Off", "On" };

static struct t_cfg_definition host_config[] = {
    { CFG_HOST_PROTECT_FLASH, CFG_TYPE_ENUM, "Protect Flash from updates", "%s", off_on, 0, 1, 0 },     // default off (D9)
    { CFG_TYPE_END,           CFG_TYPE_END,  "",                           "",   NULL,   0, 0, 0 }
};

class HostFlashConfig : ConfigurableObject {
public:
    void init()
    {
        cfg = ConfigManager :: getConfigManager()->register_store(0x48535400, "Host Settings", host_config, this);
        cfg->set_effectuated();
    }
    bool enabled() { return cfg ? cfg->get_value(CFG_HOST_PROTECT_FLASH) != 0 : true; }     // no store (cannot happen after init): protected
    void set(bool on)
    {
        if (cfg) {
            cfg->set_value(CFG_HOST_PROTECT_FLASH, on ? 1 : 0);
            cfg->write();
        }
    }
};

static HostFlashConfig hostFlashConfig;
InitFunction init_hostflashcfg("Host flash protection setting", [](void *, void *) { hostFlashConfig.init(); }, NULL, NULL, 21);

void flashprot_register(void) { }
bool flashprot_enabled(void) { return hostFlashConfig.enabled(); }
void flashprot_set(bool on) { hostFlashConfig.set(on); }

// The flash controller of the host (w25q_flash.cc uses the same registers): force CS low with SPI_FORCE_SS, a write to the data register sends a
// byte, a read clocks one in.
namespace {
struct BoardSpi : FlashSpi {
    void begin() { SPI_FLASH_CTRL = SPI_FORCE_SS; }
    void send(uint8_t out) { SPI_FLASH_DATA = out; }
    uint8_t recv() { return SPI_FLASH_DATA; }
    void end() { SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; }
    bool wait_idle(int ms)
    {
        uint16_t start = getMsTimer();                          // a 16 bit millisecond timer, as in w25q_flash.cc wait_ready()
        for (;;) {
            SPI_FLASH_CTRL = SPI_FORCE_SS;
            SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
            uint8_t s = SPI_FLASH_DATA;
            SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
            if (!(s & 1)) {
                return true;
            }
            if ((uint16_t)(getMsTimer() - start) > (uint16_t)ms) {
                return false;
            }
        }
    }
};
}

// lower-case hex with a fixed number of digits (the firmware's printf prints the zero padded forms in capitals)
static void put_hex(char *p, uint32_t v, int digits)
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < digits; i++) {
        p[i] = hx[(v >> (4 * (digits - 1 - i))) & 15];
    }
    p[digits] = 0;
}

void flashprot_status_text(char *out, int n)
{
    static BoardSpi spi;
    FlashLockInfo info;
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = 0;
    bool ok = flashlock_status(spi, &info);
    SPI_FLASH_CTRL = 0;
    portEXIT_CRITICAL();
    char id[8], a[4], b[4];
    put_hex(id, info.jedec, 6);
    put_hex(a, info.sr1, 2);
    put_hex(b, info.sr2, 2);
    if (ok) {
        snprintf(out, n, "Flash chip %s: status registers SR1 %s, SR2 %s now.", id, a, b);
    } else {
        snprintf(out, n, "Flash chip %s: not a W25Q128JV, its status registers are not touched.", id);
    }
}

bool flashprot_before_handover(const char **msg)
{
#if !HOSTAPP_FLASH_LOCK
    (void)msg;
    printf("[host] flash protection: not in this build (HOSTAPP_FLASH_LOCK 0)\n");
    return true;
#else
    static BoardSpi spi;
    FlashLockInfo info;
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = 0;
    FlashLockResult r = flashlock_apply(spi, flashprot_enabled(), &info);
    SPI_FLASH_CTRL = 0;
    portEXIT_CRITICAL();
    char id[8], a[4], c[4];
    put_hex(id, info.jedec, 6);
    put_hex(a, info.sr1, 2);
    put_hex(c, info.sr2, 2);
    printf("[host] flash protection before the handover: %s (JEDEC %s, status %s %s)\n", info.msg, id, a, c);
    if (r == FLR_FAILED) {
        *msg = info.msg;
        return false;
    }
    return true;
#endif
}
