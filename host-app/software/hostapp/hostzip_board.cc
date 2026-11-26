#include "hostzip_board.h"
#include "fm_fs.h"
#include "fastseek_api.h"
#include "libapi.h"
#include "FreeRTOS.h"
#include "task.h"

// The NEORV32 instruction cache has no fence.i on this core: executing 2 KB of NOPs from firmware text refills every block with something that is not the heap
// (libboard.cc lib_icache_sweep, app_loader.cc app_icache_sweep). The heap block an image lands in may have held other code before.
extern "C" void host_icache_sweep(void);
__asm__(".pushsection .text.host_icache_sweep, \"ax\", @progbits\n"
        ".balign 4\n"
        ".global host_icache_sweep\n"
        "host_icache_sweep:\n"
        ".rept 512\n"
        "nop\n"
        ".endr\n"
        "ret\n"
        ".popsection\n");

namespace {

class HostLibHost : public LibHost {
public:
    void *alloc(uint32_t n) { return pvPortMalloc(n); }
    void release(void *p) { vPortFree(p); }
    uint32_t addr_of(const void *p) { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) { host_icache_sweep(); }
    bool has_capability(const char *) { return false; }                      // the host is no core: an image that needs one is refused
    const char *core_name() { return "host"; }
    int call_register(const Entry &e, const LibApi *api, LibRecord *rec)
    {
        lib_register_fn fn = (lib_register_fn)(uintptr_t)e.entry_addr;
        return fn(api, rec);
    }
    int call_main(const Entry &e, const LibApi *api, AppCtx *ctx)
    {
        app_main_fn fn = (app_main_fn)(uintptr_t)e.entry_addr;
        return fn(api, ctx);
    }
    uint32_t room() { return (uint32_t)xPortGetFreeHeapSize(); }
    uint32_t now_ms() { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }
    void delay_ms(uint32_t ms) { vTaskDelay(ms >= 5 ? pdMS_TO_TICKS(ms) + 1 : 1); }
    void enter() { portENTER_CRITICAL(); }
    void leave() { portEXIT_CRITICAL(); }
};

}  // namespace

HcZipEnv *hostzip_env(const HcFlowEnv *base)
{
    static HostLibHost lh;
    static HcZipEnv env;
    if (!env.base) {
        env.base = base;
#if FF_USE_FASTSEEK
        fm_fs_set_fastseek(fastseek_table());          // the host application links the fast seek library itself: it has no TOML B (T239.3 P3-1)
#endif
        env.fs = fm_fs();
        env.libhost = &lh;
        env.rig = 0;
    }
    return &env;
}
