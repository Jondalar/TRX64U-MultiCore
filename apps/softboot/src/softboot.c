/*
 * softboot.c - starts the firmware inside an update image from RAM.
 *
 * "Run as Softboot" on a .cfw, .ue2 or .u64 does what the image's own
 * updater would do up to the point where it flashes, and then does not
 * flash: the application in the image is copied to its load address and
 * started. The flash is not touched, so the next reset returns to the
 * firmware in flash.
 *
 * A softboot does not reload the FPGA: the application runs on the bitstream
 * in flash. So the image must carry that same bitstream, byte for byte. The
 * updater puts it in flash from offset 0 exactly as it is stored in the
 * image, which gives the check:
 *   1. find the first 4 KiB of the flash in the image;
 *   2. compare the whole bitstream from there;
 *   3. the application record follows the bitstreams, in the order of
 *      update_binaries_u64.s / update_binaries_u64ii.s.
 * The bitstream's length: on the U64 the updater build truncates it to the
 * flash region before the application, 0x290000 bytes; on the U64-II and C64U
 * it is a Xilinx .bit file whose header states its length.
 *
 * The copy overwrites the running firmware and, with it, the task stack this
 * app runs on. The last step is therefore a routine that uses no stack.
 */

#include "u2p_app.h"

#define STAGE       ((uint8_t *)0x03000000) /* where Run Update loads an image; below the app area */
#define STAGE_LIMIT 0x00A00000              /* up to the app area at 0x03A00000 */
#define CHUNK       4096
#define PROBE       4096

#if defined(__riscv)
#define APP_LOAD 0x00030000u /* target/u64ii/riscv/ultimate/linker.x */
#else
#define APP_LOAD         0x00000000u /* software/nios_appl_bsp/linker.x */
#define U64_BITSTREAM_LEN 0x290000u  /* $(SWAP) ... 290000 in target/u64/nios2/updater/Makefile */
#endif

static const u2p_api_t *api;
static const u2p_app_context_t *s_ctx;
static uint8_t probe[PROBE];
static uint8_t chunk_a[CHUNK];
static uint8_t chunk_b[CHUNK];
static void *progress; /* the open progress bar, or NULL */

static void step(int n)
{
    if (progress && (n > 0)) {
        api->ui->progress_set(progress, n, NULL);
    }
}

/* Copies len bytes from src to dst, then jumps to entry. Uses no stack and
 * never returns: dst is where the running firmware and its stacks are. */
void copy_and_jump(void *dst, const void *src, uint32_t len, uint32_t entry);

#if defined(__riscv)
/* The NEORV32 instruction cache is direct-mapped, 2 KiB. Running 2 KiB of
 * NOPs from here refills every block before the new firmware is fetched, the
 * way app_loader.cc sweeps it before an app runs. */
__asm__(".pushsection .text.copy_and_jump, \"ax\", @progbits\n"
        ".balign 4\n"
        ".global copy_and_jump\n"
        "copy_and_jump:\n"
        "1: blez a2, 2f\n"
        "   lw   t0, 0(a1)\n"
        "   sw   t0, 0(a0)\n"
        "   addi a0, a0, 4\n"
        "   addi a1, a1, 4\n"
        "   addi a2, a2, -4\n"
        "   j    1b\n"
        "2:\n"
        ".rept 512\n"
        "   nop\n"
        ".endr\n"
        "   jr   a3\n"
        ".popsection\n");
#else
/* flushd and flushi do nothing on a core built without caches, which the
 * U64's BSP says it is; they keep the copy right if it has them. */
__asm__(".pushsection .text.copy_and_jump, \"ax\", @progbits\n"
        ".balign 4\n"
        ".global copy_and_jump\n"
        "copy_and_jump:\n"
        "   mov  r8, r4\n"
        "   add  r9, r4, r6\n"
        "1: bge  zero, r6, 2f\n"
        "   ldw  r2, 0(r5)\n"
        "   stw  r2, 0(r4)\n"
        "   addi r4, r4, 4\n"
        "   addi r5, r5, 4\n"
        "   addi r6, r6, -4\n"
        "   br   1b\n"
        "2: bgeu r8, r9, 3f\n"
        "   flushd 0(r8)\n"
        "   flushi r8\n"
        "   addi r8, r8, 32\n"
        "   br   2b\n"
        "3: flushp\n"
        "   jmp  r7\n"
        ".popsection\n");
#endif

static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int read_at(void *f, uint32_t offset, void *buf, int len)
{
    if (api->fs->fseek(f, (long)offset, 0) != 0) {
        return 0;
    }
    return api->fs->fread(buf, 1, len, f) == len;
}

/* The offset of the first 4-aligned place in the file that holds `probe`, or
 * -1. Reads in chunks that overlap by the probe's length. */
static long find_probe(void *f, uint32_t size)
{
    static uint8_t window[CHUNK + PROBE];
    for (uint32_t base = 0; base + PROBE <= size; base += CHUNK) {
        int want = (size - base < sizeof(window)) ? (int)(size - base) : (int)sizeof(window);
        if (!read_at(f, base, window, want)) {
            return -1;
        }
        for (int i = 0; (i < CHUNK) && (i + PROBE <= want); i += 4) {
            if ((get32(window + i) == get32(probe)) && !memcmp(window + i, probe, PROBE)) {
                step((int)((size - base) / CHUNK)); /* what is left of the search */
                return (long)(base + i);
            }
        }
        step(1);
    }
    return -1;
}

/* Whether the file from `offset` holds the flash's first `len` bytes. */
static int same_as_flash(void *f, uint32_t offset, uint32_t len)
{
    for (uint32_t done = 0; done < len; done += CHUNK) {
        int n = (len - done < CHUNK) ? (int)(len - done) : CHUNK;
        if ((api->sys->flash_read(done, chunk_a, n) != n) || !read_at(f, offset + done, chunk_b, n) ||
            memcmp(chunk_a, chunk_b, n)) {
            return 0;
        }
        step(1);
    }
    return 1;
}

#if defined(__riscv)
static const uint8_t bit_magic[13] = { 0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01 };

/* The length of a Xilinx .bit file from its header: fields a to d are a key
 * byte and a 16-bit big-endian length, e is a key byte and the 32-bit length
 * of the configuration data that follows. 0 if it is not a .bit header. */
static uint32_t bit_length(const uint8_t *h, int avail)
{
    if ((avail < 13) || memcmp(h, bit_magic, 13)) {
        return 0;
    }
    int p = 13;
    for (char key = 'a'; key <= 'd'; key++) {
        if ((p + 3 > avail) || (h[p] != key)) {
            return 0;
        }
        p += 3 + ((h[p + 1] << 8) | h[p + 2]);
    }
    if ((p + 5 > avail) || (h[p] != 'e')) {
        return 0;
    }
    uint32_t data = ((uint32_t)h[p + 1] << 24) | (h[p + 2] << 16) | (h[p + 3] << 8) | h[p + 4];
    return (uint32_t)(p + 5) + data;
}
#endif

/* A record header that describes a firmware application. */
static int app_header(const uint8_t *h, uint32_t at, uint32_t size)
{
    uint32_t load = get32(h), len = get32(h + 4), start = get32(h + 8);
    return (load == APP_LOAD) && (len >= 0x10000) && (len <= STAGE_LIMIT) && (start >= load) &&
           (start < load + len) && (at + 12 + len <= size) && (load + len <= (uint32_t)STAGE);
}

/* The application record after the bitstream that ends at `end`, skipping a
 * second bitstream (the U64-II image carries one for each FPGA size). Gaps
 * are alignment, at most 16 bytes. Returns the header's offset or -1. */
static long find_app(void *f, uint32_t end, uint32_t size)
{
    uint8_t h[256]; /* a .bit header runs to about 110 bytes before its length field */
    for (int skips = 0; skips < 2; skips++) {
        for (uint32_t at = (end + 3) & ~3u; at <= end + 16; at += 4) {
            if (!read_at(f, at, h, sizeof(h))) {
                return -1;
            }
            if (app_header(h, at, size)) {
                return (long)at;
            }
#if defined(__riscv)
            uint32_t other = bit_length(h, sizeof(h));
            if (other) {
                end = at + other;
                break;
            }
#endif
        }
    }
    return -1;
}

/* The reason goes to the screen, or to the REST caller when there is none. */
static void refuse(const char *why)
{
    api->ui->popup("Softboot", why);
    s_ctx->reply(why);
}

int softboot_main(const u2p_api_t *api_in, const u2p_app_context_t *ctx)
{
    api = api_in;
    s_ctx = ctx;
    if (!ctx->selected_file || !ctx->action) {
        refuse("Choose an update image in the file\nbrowser and pick Run as Softboot.");
        return 1;
    }

    void *f = api->fs->fopen(ctx->selected_file, "rb");
    if (!f) {
        refuse("Cannot open the image.");
        return 1;
    }
    api->fs->fseek(f, 0, 2);
    long size = api->fs->ftell(f);

    /* 1. The bitstream in flash, and where the image carries it. */
    if ((size <= 0) || (api->sys->flash_read(0, probe, PROBE) != PROBE)) {
        api->fs->fclose(f);
        refuse("Cannot read the image or the flash.");
        return 1;
    }
#if defined(__riscv)
    uint32_t bit_len = bit_length(probe, PROBE);
#else
    uint32_t bit_len = U64_BITSTREAM_LEN;
#endif
    /* One bar over the search through the file and the compare with the flash. */
    progress = api->ui->progress_open("Verifying update file", (int)((uint32_t)size / CHUNK + bit_len / CHUNK + 2));
    long bit_at = bit_len ? find_probe(f, (uint32_t)size) : -1;

    /* 2. The whole bitstream must be the one in flash. */
    int same = (bit_at >= 0) && same_as_flash(f, (uint32_t)bit_at, bit_len);
    api->ui->progress_close(progress);
    progress = NULL;
    if (!same) {
        api->fs->fclose(f);
        refuse("This image brings another FPGA core\nthan the one in flash. It can be\nflashed, not softbooted.");
        return 1;
    }

    /* 3. The application behind it. */
    long app_at = find_app(f, (uint32_t)bit_at + bit_len, (uint32_t)size);
    uint8_t h[12];
    if ((app_at < 0) || !read_at(f, (uint32_t)app_at, h, sizeof(h))) {
        api->fs->fclose(f);
        refuse("No firmware found in this image.");
        return 1;
    }
    uint32_t load = get32(h), len = get32(h + 4), start = get32(h + 8);

    /* The one question before the jump: say plainly what this is. A REST
     * caller asked for exactly this and has no screen to answer on. */
    if (ctx->has_ui && !api->ui->confirm("Developer mode\n\n"
                         "This starts firmware from RAM that\n"
                         "is not the installed one. Support\n"
                         "does not cover this run. The flash\n"
                         "stays as it is; a power cycle\n"
                         "returns to the installed firmware.\n\n"
                         "Continue?")) {
        api->fs->fclose(f);
        return 0;
    }

    /* Into the staging area, which the running firmware does not use. */
    progress = api->ui->progress_open("Loading firmware", (int)(len / CHUNK + 1));
    int ok = 1;
    for (uint32_t done = 0; ok && (done < len); done += CHUNK) {
        int n = (len - done < CHUNK) ? (int)(len - done) : CHUNK;
        ok = read_at(f, (uint32_t)app_at + 12 + done, STAGE + done, n);
        step(1);
    }
    api->ui->progress_close(progress);
    progress = NULL;
    api->fs->fclose(f);
    if (!ok) {
        refuse("Cannot read the firmware from the image.");
        return 1;
    }

    api->sys->machine_handover();
    copy_and_jump((void *)load, STAGE, (len + 3) & ~3u, start);
    return 0; /* not reached */
}
