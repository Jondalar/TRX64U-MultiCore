/* T239.3 D15: the heap of the application, set by the core it runs (core.toml `heap`).
 *
 * One image serves every core. The heap is FreeRTOS heap_5 with one region that starts at the end of the image (the linker's _end, after .bss) and is
 * the size of the heap word; the stack of crt0 stays at the top of the application region (__heap_limit - 4) and keeps HEAPCFG_STACK_RESERVE bytes.
 * The word is the record below: tools/cor_pack.py finds `TRXHEAP:` in the image, writes the core's heap into its copy and refuses an image whose end
 * + heap + reserve does not fit the region (the check the linker made for the static heap array before). A word nobody patches keeps
 * HEAPCFG_DEFAULT, the heap of every core before T239.3.
 *
 * Nothing allocates before the region exists: every allocation (new, malloc and its relatives through memory_wrap.cc, FreeRTOS' own) ends in
 * pvPortMalloc, which the link wraps (--wrap=pvPortMalloc); the first call defines the region. The image has no other source of memory, so _sbrk,
 * which newlib would use for a malloc that is not wrapped, fails instead of handing out the region.
 */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include "FreeRTOS.h"

#ifndef HEAPCFG_DEFAULT
#define HEAPCFG_DEFAULT 0x7A0000u      /* `make own OWN_HEAP=...` sets the default of an image of its own (trx64u) */
#endif
#define HEAPCFG_STACK_RESERVE 0x20000u /* the crt0 stack (before the scheduler starts, the ISR stack is a FreeRTOS array of its own) */

extern char _end[];                    /* linker.x: end of .bss */
extern char __heap_limit[];            /* linker.x: ORIGIN(memory) + LENGTH(memory), the stack starts below it */

/* heap, image_end, region_end, stack_reserve: the layout is tools/cor_pack.py HEAP_WORDS; only the first word is written by the packer */
const volatile struct {
    char magic[8];
    uint32_t heap;
    uint32_t image_end;
    uint32_t region_end;
    uint32_t stack_reserve;
} trx_heapcfg = { { 'T', 'R', 'X', 'H', 'E', 'A', 'P', ':' }, HEAPCFG_DEFAULT, (uint32_t)_end, (uint32_t)__heap_limit, HEAPCFG_STACK_RESERVE };

void *__real_pvPortMalloc(size_t xWantedSize);

/* The region: [start, start + size) with start the 16-aligned end of the image; size is the word, cut so that the reserve under the stack stays. */
static void heapcfg_define(void)
{
    uint32_t start = ((uint32_t)_end + 15u) & ~15u;
    uint32_t limit = (uint32_t)__heap_limit - trx_heapcfg.stack_reserve;
    uint32_t size = trx_heapcfg.heap;
    if (start >= limit)
        for (;;) ;                                    /* no room at all: the packer refuses such an image, this is the board's last word */
    if (size > limit - start)
        size = limit - start;
    HeapRegion_t regions[2] = { { (uint8_t *)start, size }, { NULL, 0 } };
    vPortDefineHeapRegions(regions);
}

void *__wrap_pvPortMalloc(size_t xWantedSize)
{
    static int defined;
    if (!defined) {                                   /* single-threaded: the first allocation is made by the start-up code, before the scheduler */
        defined = 1;
        heapcfg_define();
    }
    return __real_pvPortMalloc(xWantedSize);
}

void *__wrap__sbrk(ptrdiff_t incr)
{
    (void)incr;
    errno = ENOMEM;
    return (void *)-1;
}
