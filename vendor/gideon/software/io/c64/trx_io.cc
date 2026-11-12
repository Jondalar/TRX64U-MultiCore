// The TRX64 Ultimate IO block: detection and the shared interrupt. See trx_io.h.

#include "trx_io.h"
#include "itu.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "json.h"
#include "product.h"
#include "app_api_net.h"

// The block's one interrupt line is ITU high interrupt 2 (drive B's in
// Gideon's bitstreams; the TRX64 bitstream has no drive B). irq_in(4) is the
// UCI's there as in his bitstreams.
#define TRX_IRQHIGH 2

#define TRX_MAX_HANDLERS 4

static int caps = -1; // not looked yet
static uint8_t irq_en;
static struct {
    uint8_t causes;
    trx_irq_handler_t handler;
} handlers[TRX_MAX_HANDLERS];
static int n_handlers;

uint8_t trx_io_caps(void)
{
    if (caps < 0) {
        bool id = (ioRead8(TRX_ID + 0) == 'T') && (ioRead8(TRX_ID + 1) == 'R') &&
                  (ioRead8(TRX_ID + 2) == 'X') && (ioRead8(TRX_ID + 3) == '2');
        caps = id ? ioRead8(TRX_CAPS) : 0;
        if (id) {
            printf("[TRX] IO block present, CAPS %02x\n", caps);
        }
    }
    return uint8_t(caps);
}

uint8_t trx_io_caps3(void)
{
    static int caps3 = -1; // not looked yet
    if (caps3 < 0) {
        caps3 = trx_io_caps() ? ioRead8(TRX_CAPS3) : 0;
        if (caps3) {
            printf("[TRX] CAPS3 %02x\n", caps3);
        }
    }
    return uint8_t(caps3);
}

bool trx_io_resid8(void)
{
    return (trx_io_caps3() & TRX_CAPS3_RESID8) != 0;
}

void trx_io_resid8_filter(int u, uint8_t src, uint8_t auto_bits, int base, int offset)
{
    if (!trx_io_resid8()) {
        return;
    }
    u &= 1;
    base &= 0x1FF;
    offset &= 0x7FF;
    ioWrite8(TRX_RS8_FCSEL(u), src & 3);
    ioWrite8(TRX_RS8_AUTO(u), auto_bits & 3);
    ioWrite8(TRX_RS8_BASE(u), uint8_t(base));
    ioWrite8(TRX_RS8_BASE(u) + 1, uint8_t(base >> 8));
    ioWrite8(TRX_RS8_OFFSET(u), uint8_t(offset));
    ioWrite8(TRX_RS8_OFFSET(u) + 1, uint8_t(offset >> 8));
}

void trx_io_resid8_bypass(int u, bool bypass)
{
    if (trx_io_resid8()) {
        ioWrite8(TRX_RS8_BYPASS(u & 1), bypass ? 1 : 0);
    }
}

bool trx_io_slim(void)
{
    static int slim = -1; // not looked yet
    if (slim < 0) {
        slim = 0;
        if (trx_io_caps() & TRX_CAPS_VIDEO) {
            slim = (trx_io_read32(TRX_IO_BASE + 0x60C0) >> 24) >= 2; // TX_CTRL: the text layer's version
        }
    }
    return slim != 0;
}

void trx_io_lock(void) { portENTER_CRITICAL(); }
void trx_io_unlock(void) { portEXIT_CRITICAL(); }

uint32_t trx_io_read32(uint32_t addr)
{
    portENTER_CRITICAL();
    uint32_t v = ioRead8(addr);
    v |= uint32_t(ioRead8(addr + 1)) << 8;
    v |= uint32_t(ioRead8(addr + 2)) << 16;
    v |= uint32_t(ioRead8(addr + 3)) << 24;
    portEXIT_CRITICAL();
    return v;
}

void trx_io_write32(uint32_t addr, uint32_t v)
{
    portENTER_CRITICAL();
    ioWrite8(addr + 0, uint8_t(v));
    ioWrite8(addr + 1, uint8_t(v >> 8));
    ioWrite8(addr + 2, uint8_t(v >> 16));
    ioWrite8(addr + 3, uint8_t(v >> 24));
    portEXIT_CRITICAL();
}

JSON_Object *trx_io_ident(void)
{
    uint8_t c = trx_io_caps();
    if (!c) {
        return NULL;
    }
    char id[5];
    for (int i = 0; i < 4; i++) {
        id[i] = (char)ioRead8(TRX_ID + i);
    }
    id[4] = 0;
    static const char hex[] = "0123456789ABCDEF";
    char caps_str[11] = "0x000000";
    caps_str[8] = hex[c >> 4];
    caps_str[9] = hex[c & 15];
    caps_str[10] = 0;
    JSON_Object *o = JSON::Obj()
        ->add("core", id)
        ->add("caps", caps_str)
        ->add("board", isCommodoreBoard() ? "C64U" : "U64-II");
    uint16_t rpc = app_trx_rpc_port();
    if (rpc) {
        o->add("rpc", (int)rpc);
    }
    return o;
}

void trx_dbg_yield(void)
{
    if (!(trx_io_caps() & 0x08)) { // page 3: the debug block
        return;
    }
    if (trx_io_read32(TRX_IO_BASE + 0x3008) & (1u << 20)) { // STATUS: OWNER
        trx_io_write32(TRX_IO_BASE + 0x118, 0);  // OWNER off: C64_STOP rules the gate
        trx_io_write32(TRX_IO_BASE + 0x3000, 3); // RUN: drop the debugger's hold
        printf("[TRX] the firmware stops the C64: debugger run control given back\n");
    }
}

static uint8_t trx_irq_dispatch(void)
{
    uint8_t act = ioRead8(TRX_IRQ_CAUSE) & ioRead8(TRX_IRQ_EN);
    uint8_t do_switch = 0;
    for (int i = 0; i < n_handlers; i++) {
        uint8_t mine = act & handlers[i].causes;
        if (mine) {
            do_switch |= handlers[i].handler(mine);
        }
    }
    return do_switch;
}

static uint8_t trx_irq_high(void *context)
{
    return trx_irq_dispatch();
}

void trx_irq_attach(uint8_t causes, trx_irq_handler_t h)
{
    portENTER_CRITICAL();
    bool found = false;
    for (int i = 0; i < n_handlers; i++) {
        if (handlers[i].handler == h) {
            handlers[i].causes = causes;
            found = true;
        }
    }
    if (!found && n_handlers < TRX_MAX_HANDLERS) {
        handlers[n_handlers].causes = causes;
        handlers[n_handlers].handler = h;
        n_handlers++;
    }
    portEXIT_CRITICAL();
    install_high_irq(TRX_IRQHIGH, trx_irq_high, NULL);
}

void trx_irq_enable(uint8_t causes, bool on)
{
    portENTER_CRITICAL();
    if (on) {
        irq_en |= causes;
    } else {
        irq_en &= ~causes;
    }
    ioWrite8(TRX_IRQ_EN, irq_en);
    portEXIT_CRITICAL();
}
