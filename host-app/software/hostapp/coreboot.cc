#include "coreboot.h"
#include "hostfs.h"
#include "startcheck.h"
#include "handover.h"
#include "configlist.h"
#include "videomode.h"
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "u2p.h"
#include "u64.h"
#include "iomap.h"
#include "usb_base.h"
#include "trx_lv_port.h"
#include <stdio.h>
#include <string.h>

extern "C" bool rtc_time_valid(void);
extern "C" void get_current_time(int &wd, int &year, int &month, int &day, int &hour, int &min, int &sec);

#define HOST_RMII_RX_ENABLE (*(volatile uint8_t *)(RMII_BASE + 0x08))   // rmii_interface.h RMII_RX_ENABLE
#define HOST_WIFI_FLOWCTRL  (*(volatile uint8_t *)(WIFI_UART_BASE + 0x03))   // dma_uart.h flowctrl; bit 7 = soft_reset (uart_dma.vhd c_uart_flowctrl)

// ICAP loader of layer 1 (docs/specs/T79.1-host-core-contract.md section 7)
#define ICAP_ID      (*(volatile uint8_t *)0x10060600)
#define ICAP_DATA    (*(volatile uint8_t *)0x10060610)
#define ICAP_EN      (*(volatile uint8_t *)0x10060614)
#define ICAP_CNT(i)  (*(volatile uint8_t *)(0x10060618 + (i)))
#define ICAP_DC      (*(volatile uint8_t *)0x10060620)
#define ICAP_PORT_REL (*(volatile uint8_t *)0x10060621)   // T135 D6: bit 0 = 1 releases the expansion port lock (host/ultimate_sys.vhd, app_prel_req)

struct StageCtx {
    core_progress_t cb;
    void *ctx;
    int base, span;
};

static bool stage_aborted;
static bool start_safe;                        // T224 D2: the next jump asks the core application for its safe video values
static char start_config[CONFIG_NAME_MAX + 1];  // T198 D7: the config the jump hands over
static char staged_path[HANDOVER_PATH_MAX];      // T93 D11: the bundle the staged application belongs to
static void part_progress(uint32_t done, uint32_t total, void *c)
{
    StageCtx *s = (StageCtx *)c;
    if (s->cb && s->cb(STEP_VERIFY, s->base + (int)((uint64_t)done * s->span / total), NULL, s->ctx)) {
        stage_aborted = true;
        cor_abort = true;
    }
}

// Milliseconds from the ITU's free-running 16-bit ms counter (itu.c getMsTimer, the one the SD driver and the
// USB code time with), unwrapped to 32 bits; it must be read at least every 65 s. (The CPU's mcycle CSR
// does not count in this core: the first board run measured 0 ms for every step.)
uint32_t core_cycles(void)
{
    static uint16_t last;
    static uint32_t hi;
    uint16_t now = getMsTimer();
    if (now < last) {
        hi += 0x10000;
    }
    last = now;
    return hi + now;
}

bool core_stage(const char *path, CorInfo *ci, const char **why, core_progress_t cb, void *ctx,
                const BootEntry *trusted, bool skip_checks)
{
    stage_aborted = false;
    cor_abort = false;
    staged_path[0] = 0;
    cor_clock = core_cycles;
    cor_t_read = cor_t_hash = 0;
    uint32_t t_start = core_cycles();
    FileSource src(path);
    if (!src.ok()) {
        *why = "cannot open the bundle";
        return false;
    }
    cor_result_t r = cor_probe(src, *ci);
    if (r != COR_OK) {
        *why = cor_result_str(r);
        return false;
    }
    if (ci->size[COR_PART_APP] > STAGE_APP_MAX || ci->size[COR_PART_PARTIAL] > STAGE_PART_MAX ||
        ci->app_load < APP_LOAD_MIN || ci->app_load + ci->size[COR_PART_APP] > APP_LOAD_LIMIT ||
        (ci->app_load & 3)) {
        *why = cor_result_str(COR_ERR_RANGE);
        return false;
    }
    FileStat fst;
    bool verify = !skip_checks && !(trusted && hostfs_stat(path, &fst.size, &fst.mtime) && startcheck_trusts(*trusted, *ci, fst));
    printf("[host] bundle %u bytes, %s\n", (unsigned)ci->file_size,
           skip_checks ? "no SHA-256 check (direct start)" : verify ? "SHA-256 check" : "hash trusted from boot.cfg (same size, mtime, header and manifest hash)");
    StageCtx pc = { cb, ctx, 0, 90 };
    r = cor_read_part(src, *ci, COR_PART_PARTIAL, (uint8_t *)STAGE_PART_ADDR, part_progress, &pc, verify);
    if (stage_aborted) {
        *why = CORE_ABORTED;
        return false;
    }
    if (r != COR_OK) {
        *why = cor_result_str(r);
        return false;
    }
    pc.base = 90;
    pc.span = 10;
    r = cor_read_part(src, *ci, COR_PART_APP, (uint8_t *)STAGE_APP_ADDR, part_progress, &pc, verify);
    if (stage_aborted) {
        *why = CORE_ABORTED;
        return false;
    }
    if (r != COR_OK) {
        *why = cor_result_str(r);
        return false;
    }
    strncpy(staged_path, path, sizeof(staged_path) - 1);
    staged_path[sizeof(staged_path) - 1] = 0;
    uint32_t tot = (core_cycles() - t_start) / CORE_CYC_PER_MS;
    printf("[host] stage: %u ms total, read %u ms, hash %u ms\n", (unsigned)tot,
           (unsigned)(cor_t_read / CORE_CYC_PER_MS), (unsigned)(cor_t_hash / CORE_CYC_PER_MS));
    return true;
}

bool core_icap_present(void)
{
    return ICAP_ID == 0x7A;
}

bool core_swap(const CorInfo *ci, const char **why, core_progress_t cb, void *ctx)
{
    if (!core_icap_present()) {
        *why = "no ICAP loader in this bitstream (ID byte is not 0x7A)";
        return false;
    }
    if (cb && cb(STEP_DECOUPLE, 0, NULL, ctx)) {
        *why = CORE_ABORTED;                         // nothing touched yet
        return false;
    }
    ICAP_DC = 1;
    vTaskDelay(pdMS_TO_TICKS(10) + 1);              // the drive's DDR2 request is served (T79.1: about 1 ms)
    if (!(ICAP_DC & 2)) {
        *why = "the decouple did not take effect; the core stays as it is";
        return false;
    }
    bool abort_req = false;
    if (cb) {
        abort_req |= cb(STEP_DECOUPLE, 100, NULL, ctx) != 0;
        abort_req |= cb(STEP_ICAP, 0, NULL, ctx) != 0;
    }
    if (abort_req) {
        ICAP_DC = 0;                                 // still the old core's configuration: give it back
        *why = CORE_ABORTED;
        return false;
    }
    const uint8_t *p = (const uint8_t *)STAGE_PART_ADDR;
    uint32_t len = ci->size[COR_PART_PARTIAL];
    uint32_t t_icap = core_cycles();
    ICAP_EN = 1;
    // The ICAP data register is one byte wide on the io bus, so the loop is bound by the byte stores: one
    // word load from DDR2, four byte stores to the same address, no call per byte; progress per 256 KiB.
    volatile uint8_t *dp = &ICAP_DATA;
    for (uint32_t done = 0; done < len;) {
        uint32_t n = (len - done > 262144) ? 262144 : len - done;
        const uint32_t *w = (const uint32_t *)(p + done);
        uint32_t words = n >> 2;
        while (words >= 2) {
            uint32_t a = w[0], b = w[1];
            *dp = (uint8_t)a; *dp = (uint8_t)(a >> 8); *dp = (uint8_t)(a >> 16); *dp = (uint8_t)(a >> 24);
            *dp = (uint8_t)b; *dp = (uint8_t)(b >> 8); *dp = (uint8_t)(b >> 16); *dp = (uint8_t)(b >> 24);
            w += 2;
            words -= 2;
        }
        if (words) {
            uint32_t a = w[0];
            *dp = (uint8_t)a; *dp = (uint8_t)(a >> 8); *dp = (uint8_t)(a >> 16); *dp = (uint8_t)(a >> 24);
        }
        done += n;
        if (cb) {
            // a half-written configuration is worse than a finished one: the request is remembered, not obeyed here
            abort_req |= cb(STEP_ICAP, (int)((uint64_t)done * 100 / len), NULL, ctx) != 0;
        }
    }
    ICAP_EN = 0;
    {
        uint32_t ms = (core_cycles() - t_icap) / CORE_CYC_PER_MS;
        printf("[host] ICAP: %u bytes in %u ms = %u KiB/s\n", (unsigned)len, (unsigned)ms, ms ? (unsigned)((uint64_t)len * 1000 / 1024 / ms) : 0);
    }
    uint32_t words = ICAP_CNT(0) | (ICAP_CNT(1) << 8) | (ICAP_CNT(2) << 16) | ((uint32_t)ICAP_CNT(3) << 24);
    printf("[host] ICAP: %u words sent, %u expected\n", (unsigned)words, (unsigned)(len / 4));
    if (words != len / 4) {
        *why = "the ICAP word count differs from the bundle; the core stays decoupled";
        return false;
    }
    if (abort_req) {
        *why = CORE_LOADED_NOT_STARTED;              // loaded completely, but the user asked not to start it
        return false;
    }
    return true;
}

// The copy loop, then a sled of NOPs, then the jump. The instruction cache (rv_icache) is direct-mapped, has no
// invalidate, and writes a tag and a line for every fetch that misses (its fill is not gated by is_cacheable, so
// this holds for the trampoline at 0x03F00000 too, which lies above the cacheable 32 MiB and is never hit): running
// the sled sequentially replaces every line, so no line of this program survives to be fetched at 0x30000... as
// part of the new application. A sled of S bytes replaces every line of a cache of up to S bytes; the host gateware
// has 8 KiB (c_icache_bits = 13 in host/cache_cfg_pkg.vhd, Gideon's was 2 KiB), the sled is 16 KiB. The data cache
// needs nothing: staging and trampoline lie above its window, and the copy's stores update a cached line they hit.
// The code touches no stack and is position independent: it is copied to TRAMPOLINE_ADDR and runs from there,
// because its target (0x30000...) is where this program lives.
extern "C" void tb_copy_start(void);
extern "C" void tb_copy_end(void);
__asm__(".pushsection .text.tb_copy, \"ax\", @progbits\n"
        ".balign 4\n"
        ".global tb_copy_start\n"
        "tb_copy_start:\n"
        "1: blez a2, 2f\n"
        "   lw   t0, 0(a1)\n"
        "   sw   t0, 0(a0)\n"
        "   addi a0, a0, 4\n"
        "   addi a1, a1, 4\n"
        "   addi a2, a2, -4\n"
        "   j    1b\n"
        "2:\n"
        ".rept 4096\n"
        "   nop\n"
        ".endr\n"
        "   jr   a3\n"
        ".global tb_copy_end\n"
        "tb_copy_end:\n"
        ".popsection\n");

void core_set_start_safe(bool on) { start_safe = on; }

void core_set_start_config(const char *name)
{
    strncpy(start_config, name ? name : "", sizeof(start_config) - 1);
    start_config[sizeof(start_config) - 1] = 0;
}

void core_jump(const CorInfo *ci)
{
    uint32_t load = ci->app_load, entry = ci->app_entry;
    uint32_t len = (ci->size[COR_PART_APP] + 3) & ~3u;
    printf("[host] handover: %u bytes to 0x%08x, entry 0x%08x\n", (unsigned)len, (unsigned)load, (unsigned)entry);

    // Release the decouple before the jump (T84 D6, 2026-10-12): the new core comes up cold, with the C64 held
    // in reset by the host's power-up page 0 (T79.1), until its application writes the ROMs and releases it.
    // An application that knows nothing of the decouple (today's Ultimate app) thus gets a connected core.
    ICAP_DC = 0;
    // T135 D6: the host holds the expansion port off (buffers, clock, RESET) from every configuration; a layer-2 core is out of decouple now and
    // about to run, so the port is its own from here. Never done for the null core (the host never jumps to it) nor for a .cfw (legacy_jump).
    ICAP_PORT_REL = 1;
    printf("[host] expansion port released\n");
    // what the Ultimate app's machine_handover does: no interrupts, nothing writing into the old memory
    trx_lv_yield_layer();                            // pixel layer off
    HOST_RMII_RX_ENABLE = 0;                        // received frames would land in the heap
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    portDISABLE_INTERRUPTS();
    HOST_WIFI_FLOWCTRL = HOST_WIFI_FLOWCTRL | 0x80;  // ESP32 frames too: soft_reset drops the armed address (uart_dma.vhd rx_addr_valid, rx_dma flush); the module runs on

    // T93 D11: the application reads this page first (path of the .cor for its .cfg, the manifest's name and version).
    // It lies outside the application's load range, the staging areas and the trampoline, so the copy below leaves it alone.
    handover_fill((Handover *)HANDOVER_ADDR, staged_path, ci->name, ci->version, cor_host_key, video_current, ci->id);
    handover_set_config((Handover *)HANDOVER_ADDR, start_config);
    handover_set_safe((Handover *)HANDOVER_ADDR, start_safe);
    start_safe = false;
    if (rtc_time_valid()) {                          // T191: the core application has no SNTP; it sets its clock from this
        int wd, y, mo, d, h, mi, sec;
        get_current_time(wd, y, mo, d, h, mi, sec);
        handover_set_time((Handover *)HANDOVER_ADDR, y, mo, d, h, mi, sec);
    }
    __asm__ volatile ("fence" ::: "memory");
    (void)*(volatile uint32_t *)(HANDOVER_ADDR + sizeof(Handover) - 4);

    uint32_t code = (uint32_t)((uintptr_t)tb_copy_end - (uintptr_t)tb_copy_start);
    memcpy((void *)TRAMPOLINE_ADDR, (const void *)tb_copy_start, code);
    __asm__ volatile ("fence" ::: "memory");
    (void)*(volatile uint32_t *)(TRAMPOLINE_ADDR + code - 4);            // posted stores are done
    (void)*(volatile uint32_t *)(STAGE_APP_ADDR + len - 4);
    ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t))TRAMPOLINE_ADDR)(load, STAGE_APP_ADDR, len, entry);
    for (;;) {
    }
}
