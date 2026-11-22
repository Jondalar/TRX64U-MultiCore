#include "legacy.h"
#include "handover.h"
#include "fatwalk.h"
#include "hostfs.h"
#include "sha256.h"
#include "srcverify.h"
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "u2p.h"
#include "u64.h"
#include "iomap.h"
#include "usb_base.h"
#include "trx_lv_port.h"
#include "filemanager.h"
#include "flash.h"
#include "filesystem_fat.h"
#include "i2c_drv.h"
#include <stdio.h>
#include <string.h>

extern "C" bool rtc_time_valid(void);
extern "C" void get_current_time(int &wd, int &year, int &month, int &day, int &hour, int &min, int &sec);
#include <stdlib.h>

#define HOST_RMII_RX_ENABLE (*(volatile uint8_t *)(RMII_BASE + 0x08))   // rmii_interface.h RMII_RX_ENABLE, as in coreboot.cc
#define HOST_WIFI_FLOWCTRL  (*(volatile uint8_t *)(WIFI_UART_BASE + 0x03))   // dma_uart.h flowctrl; bit 7 = soft_reset (uart_dma.vhd c_uart_flowctrl)

#define LEGACY_PATH      LEGACY_DIR "/" LEGACY_FILE
#define LEGACY_STAMP_PATH LEGACY_DIR "/" LEGACY_STAMP
#define LEGACY_CHUNK     32768u
#define TRAMP_SEQ_OFFSET 0x200u         // the ICAP bytes wait in DDR2 this far behind the trampoline code

static_assert(CFW_APP_MAX == STAGE_APP_MAX, "the parser and the staging area agree on the largest application");
static_assert(CFW_BIT_MAX <= STAGE_PART_MAX, "a .bit fits where the partial bitstreams are staged");
static_assert(CFW_APP_ADDR >= APP_LOAD_MIN, "the firmware's application lands above the boot ROM's scratch");

extern "C" void tb_legacy_start(void);
extern "C" void tb_legacy_end(void);

static uint8_t chunk[LEGACY_CHUNK] __attribute__((aligned(8)));

struct FlashVol {
    Flash *fl;
    uint32_t start;              // the flash drive's first byte in the SPI flash
};

static bool flash_rd(void *ctx, uint32_t off, void *buf, uint32_t n)
{
    FlashVol *v = (FlashVol *)ctx;
    v->fl->read_linear_addr((int)(v->start + off), (int)n, buf);
    return true;
}

// 1: the user asked to stop
static int report(core_progress_t cb, void *ctx, int step, int pct)
{
    return cb ? cb(step, pct, NULL, ctx) : 0;
}

enum Scratch { SC_OK, SC_MISSING, SC_FRAGMENTED, SC_CHAIN, SC_DIFFERS, SC_ERROR };

// Is the scratch file in the flash drive the bitstream, in one run of clusters? Sets *faddr when it is.
static Scratch check_scratch(FlashVol &fv, const FatGeo &geo, const uint8_t *bit, uint32_t len, uint32_t *faddr, const char **why,
                             core_progress_t cb, void *ctx)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = NULL;
    if (fm->fopen(LEGACY_DIR, LEGACY_FILE, FA_READ, &f) != FR_OK || !f) {
        return SC_MISSING;
    }
    uint32_t size = f->get_size32(), first = f->get_inode();
    fm->fclose(f);
    if (size != len) {
        return SC_MISSING;                                       // another bitstream (or a damaged file): to be replaced
    }
    uint32_t vol_off = 0;
    fat_result_t r = fat_contiguous(&geo, flash_rd, &fv, first, size, &vol_off);
    if (r == FAT_ERR_IO) {
        *why = fat_result_str(r);
        return SC_ERROR;
    }
    if (r != FAT_OK) {
        printf("[host] legacy: the flash file is not one run of clusters (%s)\n", fat_result_str(r));
        return r == FAT_ERR_FRAGMENTED ? SC_FRAGMENTED : SC_CHAIN;
    }
    uint32_t addr = fv.start + vol_off;
    for (uint32_t done = 0; done < len;) {
        uint32_t n = len - done > 4096 ? 4096 : len - done;
        fv.fl->read_linear_addr((int)(addr + done), (int)n, chunk);
        if (memcmp(chunk, bit + done, n)) {
            printf("[host] legacy: the flash file differs from the bitstream at +0x%x\n", (unsigned)(done));
            return SC_DIFFERS;
        }
        done += n;
        if ((done & 0x3FFFF) == 0 || done == len) {
            report(cb, ctx, STEP_ICAP, (int)((uint64_t)done * 100 / len));
        }
    }
    *faddr = addr;
    return SC_OK;
}

// Replaces the scratch file with the bitstream: deleted first (its clusters are free again), then ONE run of clusters is reserved for it
// (f_expand, opt 1: FatFS' own cluster-by-cluster allocation does not lay a 2 MB file out in one run, even on an almost empty drive),
// then it is written over the reserved clusters.
static void stamp_remove(void);
static bool write_scratch(const FatGeo &geo, const uint8_t *bit, uint32_t len, const char **why, core_progress_t cb, void *ctx)
{
    static char no_run[192];
    FileManager *fm = FileManager :: getFileManager();
    fm->create_dir(LEGACY_DIR);                                  // exists: not an error
    stamp_remove();                                              // T135 D10: a half-written file must not keep a stamp that names it complete
    fm->delete_file(LEGACY_PATH);
    File *f = NULL;
    if (fm->fopen(LEGACY_DIR, LEGACY_FILE, FA_WRITE | FA_CREATE_ALWAYS, &f) != FR_OK || !f) {
        *why = "cannot create the file in the flash drive";
        return false;
    }
    // the flash drive is a FAT volume (fat_geometry accepted it), so this File is a FileFAT; there is no RTTI to ask
    FRESULT er = f_expand(static_cast<FileFAT *>(f)->getFIL(), len, 1);
    if (er != FR_OK) {
        fm->fclose(f);
        fm->delete_file(LEGACY_PATH);
        if (er == FR_DENIED) {
            fat_no_run_text(no_run, sizeof(no_run), &geo, len);
        } else {
            snprintf(no_run, sizeof(no_run), "cannot reserve the flash file (FatFS error %d)", (int)er);
        }
        *why = no_run;
        printf("[host] legacy: %s\n", no_run);
        return false;
    }
    bool ok = true;
    for (uint32_t done = 0; ok && done < len;) {
        uint32_t n = len - done > 65536 ? 65536 : len - done, tr = 0;
        ok = f->write(bit + done, n, &tr) == FR_OK && tr == n;
        done += n;
        report(cb, ctx, STEP_DECOUPLE, (int)((uint64_t)(ok ? done : 0) * 100 / len));   // a stop request is honoured after the write
    }
    fm->fclose(f);
    if (!ok) {
        fm->delete_file(LEGACY_PATH);
        *why = "cannot write the flash file (flash drive full?)";
        return false;
    }
    return true;
}

int legacy_stamp_text(const CfwInfo &ci, uint32_t file_mtime, char *out, int max)
{
    int n;
    if (ci.from_cor) {
        n = snprintf(out, max, "cor2|%s|%u|%s\n", ci.id, (unsigned)ci.bit_len, ci.bit_sha);
    } else {
        n = snprintf(out, max, "cfw2|%u|%u|%u|%u\n", (unsigned)ci.file_size, (unsigned)file_mtime, (unsigned)ci.bit_off, (unsigned)ci.bit_len);
    }
    return (n < 0 || n >= max) ? -1 : n;
}

// T115 D10 guard (Alex, 2026-10-28): the stamp also carries the CRC-32 of the flash file as it was written, and a start that trusts the
// stamp reads the file once and checks it (board 2026-10-28: the file differed at +0x97000 from its bitstream, and a start that trusted
// the stamp would have jumped into it). Standard CRC-32 (reflected 0xEDB88320), table built at the first call.
static uint32_t crc32_update(uint32_t c, const uint8_t *p, uint32_t n)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) {
                v = (v & 1) ? (v >> 1) ^ 0xEDB88320u : v >> 1;
            }
            table[i] = v;
        }
    }
    c = ~c;
    while (n--) {
        c = table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    }
    return ~c;
}

// T135 D10: the stamp file holds exactly `want`, then "crc XXXXXXXX\n" (T115 D10 guard); *crc gets the value.
static bool stamp_matches(const char *want, uint32_t *crc)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = NULL;
    if (fm->fopen(LEGACY_DIR, LEGACY_STAMP, FA_READ, &f) != FR_OK || !f) {
        return false;
    }
    char got[256];
    uint32_t tr = 0;
    uint32_t len = (uint32_t)strlen(want), all = len + 13;
    bool ok = f->get_size32() == all && all < sizeof(got) && f->read(got, all, &tr) == FR_OK && tr == all && !memcmp(got, want, len) &&
              !memcmp(got + len, "crc ", 4) && got[all - 1] == '\n';
    fm->fclose(f);
    if (ok && crc) {
        got[all - 1] = 0;
        *crc = (uint32_t)strtoul(got + len + 4, NULL, 16);
    }
    return ok;
}

static void stamp_remove(void)
{
    FileManager :: getFileManager()->delete_file(LEGACY_STAMP_PATH);
}

static bool stamp_write(const char *text, uint32_t crc)
{
    FileManager *fm = FileManager :: getFileManager();
    stamp_remove();
    File *f = NULL;
    if (fm->fopen(LEGACY_DIR, LEGACY_STAMP, FA_WRITE | FA_CREATE_ALWAYS, &f) != FR_OK || !f) {
        return false;
    }
    char line[300];
    int n = snprintf(line, sizeof(line), "%scrc %08x\n", text, (unsigned)crc);
    uint32_t tr = 0, len = (n > 0 && n < (int)sizeof(line)) ? (uint32_t)n : 0;
    bool ok = len && f->write(line, len, &tr) == FR_OK && tr == len;
    fm->fclose(f);
    if (!ok) {
        stamp_remove();
    }
    return ok;
}

// Progress and staging for sv_read: the share of the VERIFY bar a part takes, and (the application) the staging under the map.
struct StageCtx {
    core_progress_t cb;
    void *ctx;
    int base, span;
    uint32_t total;
    const CfwInfo *ci;           // set for the application: every chunk is padded to 128 bytes and permuted into the staging area
};

static int stage_sink(void *vc, uint32_t done, uint8_t *buf, uint32_t n)
{
    StageCtx *c = (StageCtx *)vc;
    if (c->ci) {
        uint32_t padded = (n + 127) & ~127u;
        memset(buf + n, 0, padded - n);                          // the last 128-byte block is whole, its tail zero
        cfw_stage_copy(c->ci->map, (void *)STAGE_APP_ADDR, done, buf, padded);
    }
    return report(c->cb, c->ctx, STEP_VERIFY, c->base + (int)((uint64_t)(done + n) * c->span / c->total));
}

static const char *sv_why(sv_result_t r, bool cor, bool app)
{
    if (r == SV_STOPPED) {
        return CORE_ABORTED;
    }
    if (r == SV_READ) {
        return "cannot read the firmware file";
    }
    if (cor) {
        return app ? "application: SHA-256 mismatch on every read: the package is damaged or the medium is unreliable"
                   : "bitstream: SHA-256 mismatch on every read: the package is damaged or the medium is unreliable";
    }
    return "bitstream: two reads of the file differ every time: the medium is unreliable";
}

bool legacy_stage(const char *path, LegacyPlan *p, const char **why, core_progress_t cb, void *ctx, bool verify, bool direct)
{
    memset(p, 0, sizeof(*p));
    uint32_t t0 = core_cycles();
    if (!core_icap_present()) {
        *why = "no ICAP loader in this bitstream (ID byte is not 0x7A)";
        return false;
    }
    if ((uint32_t)((uintptr_t)tb_legacy_end - (uintptr_t)tb_legacy_start) > TRAMP_SEQ_OFFSET) {
        *why = "internal error: the hand-over code is larger than its slot";
        return false;
    }
    if (report(cb, ctx, STEP_VERIFY, 0)) {
        *why = CORE_ABORTED;
        return false;
    }
    FileSource src(path);
    if (!src.ok()) {
        *why = "cannot open the firmware file";
        return false;
    }
    cfw_result_t r = cfw_probe(src, CFW_IDCODE_XC7A50T, p->info);
    if (r != CFW_OK) {
        *why = cfw_result_str(r);
        return false;
    }
    const CfwInfo &ci = p->info;
    printf("[host] legacy: %s%s: bitstream +0x%x len 0x%x, application +0x%x len 0x%x, map %s\n", ci.name, ci.from_cor ? " (.cor)" : "", (unsigned)ci.bit_off,
           (unsigned)ci.bit_len, (unsigned)ci.app_off, (unsigned)ci.app_len, ci.map == CFW_MAP_IDENTITY ? "identity" : "gideon_bl4");

    // a large .cor: the bitstream is checked against the manifest while it is read (never on a direct start, T135 D10)
    bool hash = verify && ci.from_cor && !direct;

    // the flash drive and, on a direct start, whether the scratch file already holds this very bitstream (its stamp, and the length recorded)
    Flash *fl = get_flash();
    t_flash_address fa;
    if (!fl) {
        *why = "no flash";
        return false;
    }
    memset(&fa, 0, sizeof(fa));
    fl->get_image_addresses(FLASH_ID_FLASHDRIVE, &fa);
    if (fa.id != FLASH_ID_FLASHDRIVE) {
        *why = "no flash drive";
        return false;
    }
    FlashVol fv = { fl, (uint32_t)fa.start };
    FatGeo geo;
    fat_result_t fr = fat_geometry(flash_rd, &fv, &geo);
    if (fr != FAT_OK) {
        *why = fat_result_str(fr);
        return false;
    }
    uint32_t fmtime = 0;
    if (!ci.from_cor) {
        uint32_t fsz;
        if (!hostfs_stat(path, &fsz, &fmtime)) {
            fmtime = 0;
        }
    }
    char stamp[256];
    bool have_stamp = legacy_stamp_text(ci, fmtime, stamp, sizeof(stamp)) > 0;
    uint32_t faddr = 0;
    bool scratch_ready = false;                                  // the file is complete and named by its stamp: nothing is read or written
    uint32_t want_crc = 0;
    if (direct && have_stamp && stamp_matches(stamp, &want_crc)) {
        FileManager *fm = FileManager :: getFileManager();
        File *f = NULL;
        if (fm->fopen(LEGACY_DIR, LEGACY_FILE, FA_READ, &f) == FR_OK && f) {
            uint32_t size = f->get_size32(), first = f->get_inode();
            fm->fclose(f);
            uint32_t vol_off = 0;
            if (size == ci.bit_len && fat_contiguous(&geo, flash_rd, &fv, first, size, &vol_off) == FAT_OK) {
                faddr = fv.start + vol_off;
                // the guard: the file read back once against the CRC its stamp recorded
                uint32_t t1 = core_cycles(), c = 0;
                bool rd = true;
                for (uint32_t done = 0; rd && done < size;) {
                    uint32_t n = size - done > LEGACY_CHUNK ? LEGACY_CHUNK : size - done;
                    fl->read_linear_addr((int)(faddr + done), (int)n, chunk);
                    c = crc32_update(c, chunk, n);
                    done += n;
                    if (report(cb, ctx, STEP_VERIFY, (int)((uint64_t)done * 40 / size))) {
                        *why = CORE_ABORTED;
                        return false;
                    }
                }
                if (c == want_crc) {
                    scratch_ready = true;
                    printf("[host] legacy: scratch file complete (stamp present, %u bytes, CRC %08x checked in %u ms), no flash write\n", (unsigned)size,
                           (unsigned)c, (unsigned)((core_cycles() - t1) / CORE_CYC_PER_MS));
                } else {
                    printf("[host] legacy: the scratch file is damaged (CRC %08x, the stamp says %08x): copying the bitstream again\n", (unsigned)c,
                           (unsigned)want_crc);
                    stamp_remove();
                }
            }
        }
        if (!scratch_ready) {
            printf("[host] legacy: the stamp is there but the scratch file is not complete: copying the bitstream\n");
            stamp_remove();
        }
    } else if (direct) {
        printf("[host] legacy: no stamp for this bitstream: copying it to the scratch file\n");
        stamp_remove();                                          // an old-format or foreign stamp never stays behind, whatever the copy does next
    }

    // the bitstream into RAM (it is compared with the flash file and, when it differs, written from here). A menu start of a .cor checks it
    // against the manifest here; otherwise it is checked before it is written (below), so the trusted fast path stays a plain read.
    uint8_t *bit = (uint8_t *)STAGE_PART_ADDR;
    bool bit_checked = false;
    if (!scratch_ready) {
        StageCtx sc_bit = { cb, ctx, 0, 40, ci.bit_len, NULL };
        SvJob jb = { ci.bit_off, ci.bit_len, bit, chunk, sizeof(chunk), hash ? ci.bit_sha : NULL, false, stage_sink, &sc_bit, 3, 1, "bitstream", path };
        sv_result_t sr = sv_read(src, jb);
        if (sr != SV_OK) {
            *why = sv_why(sr, ci.from_cor, false);
            return false;
        }
        bit_checked = hash;
    }
    // the application, permuted into the staging area under the map (D3). A .cor has its SHA-256 in the manifest: checked on every start,
    // the trusted one too (a wrong bit in the application is as fatal as one in the bitstream). A .cfw carries no hash.
    p->stage_len = (ci.app_len + 127) & ~127u;
    {
        uint32_t t1 = core_cycles();
        StageCtx sc_app = { cb, ctx, 40, 60, ci.app_len, &ci };
        SvJob ja = { ci.app_data, ci.app_len, NULL, chunk, sizeof(chunk), ci.from_cor ? ci.app_sha : NULL, false, stage_sink, &sc_app, 3, 1, "application", path };
        sv_result_t sr = sv_read(src, ja);
        if (sr != SV_OK) {
            *why = sv_why(sr, true, true);
            return false;
        }
        if (ci.from_cor) {
            printf("[host] legacy: application read and checked against its SHA-256 in %u ms\n", (unsigned)((core_cycles() - t1) / CORE_CYC_PER_MS));
        }
    }
    printf("[host] legacy: staged in %u ms\n", (unsigned)((core_cycles() - t0) / CORE_CYC_PER_MS));
    report(cb, ctx, STEP_VERIFY, 100);

    // the scratch file in the flash drive (D2)
    if (report(cb, ctx, STEP_DECOUPLE, 0)) {
        *why = CORE_ABORTED;
        return false;
    }
    if (!scratch_ready) {
        Scratch sc = check_scratch(fv, geo, bit, ci.bit_len, &faddr, why, cb, ctx);
        if (sc == SC_ERROR) {
            return false;
        }
        if (sc != SC_OK) {
            // missing, another bitstream, damaged, or not one run of clusters: a fresh file replaces it. What is written is the copy in RAM, so
            // it is proven first (a read error of the medium must never reach the flash): a .cor against the manifest's SHA-256, a .cfw (no
            // hash) by reading it twice.
            if (!bit_checked) {
                bool reread = false;
                if (ci.from_cor) {
                    sha256_t hb;
                    uint8_t d[32];
                    char hx[65];
                    sha256_init(&hb);
                    sha256_update(&hb, bit, ci.bit_len);
                    sha256_final(&hb, d);
                    sha256_hex(d, hx);
                    if (memcmp(hx, ci.bit_sha, 64)) {
                        printf("[host] legacy: the bitstream read from %s differed from its SHA-256 (try 1), read again\n", path);
                        reread = true;
                    }
                } else {
                    reread = true;
                }
                if (reread) {
                    StageCtx sc_bit = { cb, ctx, 0, 40, ci.bit_len, NULL };
                    SvJob jb = { ci.bit_off, ci.bit_len, bit, chunk, sizeof(chunk), ci.from_cor ? ci.bit_sha : NULL, !ci.from_cor, stage_sink, &sc_bit, 3, ci.from_cor ? 2 : 1, "bitstream", path };
                    sv_result_t sr = sv_read(src, jb);
                    if (sr != SV_OK) {
                        *why = sv_why(sr, ci.from_cor, false);
                        return false;                            // nothing was written
                    }
                }
                bit_checked = true;
                if (reread) {
                    sc = check_scratch(fv, geo, bit, ci.bit_len, &faddr, why, cb, ctx);   // the corrected copy may be what the flash holds
                    if (sc == SC_ERROR) {
                        return false;
                    }
                }
            }
        }
        if (sc != SC_OK) {
            uint32_t t1 = core_cycles();
            if (!write_scratch(geo, bit, ci.bit_len, why, cb, ctx)) {
                return false;
            }
            printf("[host] legacy: flash file written in %u ms\n", (unsigned)((core_cycles() - t1) / CORE_CYC_PER_MS));
            sc = check_scratch(fv, geo, bit, ci.bit_len, &faddr, why, cb, ctx);
            if (sc != SC_OK) {
                FileManager :: getFileManager()->delete_file(LEGACY_PATH);       // a file that cannot be started from is not left behind
                if (sc == SC_FRAGMENTED || sc == SC_CHAIN) {
                    *why = "flash drive fragmented: the bitstream does not lie in one run of clusters. Free space on the flash drive";
                } else if (sc == SC_DIFFERS) {
                    *why = "the flash file differs from the bitstream after writing";
                } else if (sc == SC_MISSING) {
                    *why = "the flash file is missing after writing";
                }
                return false;
            }
        }
        // the stamp is the last write: the file is there, complete and (menu start: verified, direct start: copied just now) the bitstream
        uint32_t crc = crc32_update(0, bit, ci.bit_len);           // the bitstream in RAM is what the flash file now holds (compared or written)
        uint32_t got_crc = 0;
        if (have_stamp && !(stamp_matches(stamp, &got_crc) && got_crc == crc)) {
            if (stamp_write(stamp, crc)) {
                printf("[host] legacy: stamp written\n");
            } else {
                printf("[host] legacy: the stamp could not be written (the next direct start copies again)\n");
            }
        }
    }
    report(cb, ctx, STEP_DECOUPLE, 100);
    report(cb, ctx, STEP_ICAP, 100);
    p->flash_addr = faddr;
    // T112 Q1: the application of a large .cor learns the path it was started from (ROMs and .cfg beside it); a .cfw leaves a wiped page.
    // Whether DDR2 keeps it across the IPROG is board step S2 (not measured yet); a lost page falls back to the flash layout.
    // The page is written under the application's map.
    static uint32_t page[CFW_HANDOVER_BYTES / 4];
    int now[6], wd;
    bool timed = rtc_time_valid();                 // T191 S9: the large core's application has no SNTP either
    if (timed) {
        get_current_time(wd, now[0], now[1], now[2], now[3], now[4], now[5]);
    }
    cfw_handover_page(ci, path, (uint8_t *)page, timed ? now : 0);
    memcpy((void *)HANDOVER_ADDR, page, CFW_HANDOVER_BYTES);
    printf("[host] legacy: flash file at 0x%x, %u ms in all\n", (unsigned)faddr, (unsigned)((core_cycles() - t0) / CORE_CYC_PER_MS));
    return true;
}

// The two clock synthesizers (CDCE913, 0xC8 on the 1V8 and the HDMI bus) to the PLL bytes the firmware's boot ROM writes itself
// (bootloader_u64ii.c init_ext_pll: init_ntsc to the video PLL, init_hdmi_60 to the other one; the video PLL sits on the HDMI bus on
// board rev 0x15, on the 1V8 bus otherwise) - T115 section 3, D8. Our gateware (cdce_i2c.v) runs the video PLL with both output
// dividers at 1; when the boot ROM moves them from 1 to 3, Gideon's 09/10 bitstream comes up with its border one phi cycle right of
// the character grid in about 6 of 7 starts, with the boot ROM's bytes already in place in every start (board, 2026-10-25). The
// application reaches the synthesizers only with CDCE open (app_i2c.v, +8 b1; any scan_enable write clears it): set here, after
// the lock, as the last I2C use before the hand-over.
static void legacy_clocks(void)
{
    static const uint8_t init_ntsc[8] = { 0x57, 0x00, 0x03, 0x03, 0x02, 0xD0, 0x22, 0x08 };
    static const uint8_t init_hdmi_60[8] = { 0x57, 0x00, 0x01, 0x01, 0x55, 0xF7, 0x82, 0x89 };
    volatile uint8_t *scan_reg = (volatile uint8_t *)(U64II_HW_I2C_BASE + 8);   // b0 scan_enable, b1 CDCE open
    bool rev15 = (U2PIO_BOARDREV >> 3) == 0x15;
    i2c->i2c_lock("legacy_clocks");   // the scanner is off (T242 D6, legacy_jump): the lock and the unlock write nothing to +8
    *scan_reg = 0x02;                 // CDCE open, scan_enable 0; it stays so up to the IPROG
    i2c->set_channel(rev15 ? I2C_CHANNEL_HDMI : I2C_CHANNEL_1V8);
    int e1 = i2c->i2c_write_block(0xC8, 0x14, init_ntsc, 8);
    i2c->set_channel(rev15 ? I2C_CHANNEL_1V8 : I2C_CHANNEL_HDMI);
    int e2 = i2c->i2c_write_block(0xC8, 0x14, init_hdmi_60, 8);
    i2c->i2c_unlock();
    printf("[host] legacy: clock synthesizers on the boot ROM's PLL bytes (%d, %d)\n", e1, e2);
    vTaskDelay(pdMS_TO_TICKS(20));
}

void legacy_jump(const LegacyPlan *p)
{
    uint32_t code = (uint32_t)((uintptr_t)tb_legacy_end - (uintptr_t)tb_legacy_start);
    printf("[host] legacy handover: %u bytes to 0x%08x, WBSTAR 0x%08x\n", (unsigned)p->stage_len, (unsigned)CFW_APP_ADDR, (unsigned)p->flash_addr);
    uint8_t seq[CFW_IPROG_LEN];
    cfw_iprog_seq(p->flash_addr, seq);

    // T242 D6 (from PR #112): the P7 scanner off. The first command of legacy_clocks() parks it between two transactions
    // (host/app_i2c.v) and it stays parked, so no scanner transaction runs on the I2C bus when the IPROG starts the firmware or
    // the large core (both come here); and the unlock no longer writes +8, so CDCE open stays set as legacy_clocks() left it.
    i2c->enable_scan(false, false);

    legacy_clocks();

    // what core_jump does: pixel layer off, nothing writing into the old memory, no interrupts
    trx_lv_yield_layer();
    HOST_RMII_RX_ENABLE = 0;                         // received frames would land in the heap
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    portDISABLE_INTERRUPTS();
    HOST_WIFI_FLOWCTRL = HOST_WIFI_FLOWCTRL | 0x80;  // ESP32 frames too: soft_reset drops the armed address (uart_dma.vhd rx_addr_valid, rx_dma flush); the module runs on

    // the boot magic: the firmware's boot ROM jumps to the application instead of loading its own from flash. Both words map
    // to themselves under either address map (bits 6:3 of 0xFFF8-0xFFFF are all one).
    volatile uint32_t *jump = (volatile uint32_t *)CFW_BOOT_JUMP_ADDR;
    volatile uint32_t *magic = (volatile uint32_t *)CFW_BOOT_MAGIC_ADDR;
    *jump = CFW_APP_ADDR;
    *magic = CFW_BOOT_MAGIC;
    __asm__ volatile ("fence" ::: "memory");
    (void)*magic;

    memcpy((void *)TRAMPOLINE_ADDR, (const void *)tb_legacy_start, code);
    memcpy((void *)(TRAMPOLINE_ADDR + TRAMP_SEQ_OFFSET), seq, sizeof(seq));
    __asm__ volatile ("fence" ::: "memory");
    (void)*(volatile uint32_t *)(TRAMPOLINE_ADDR + code - 4);                 // posted stores are done
    (void)*(volatile uint32_t *)(TRAMPOLINE_ADDR + TRAMP_SEQ_OFFSET + sizeof(seq) - 4);
    (void)*(volatile uint32_t *)(STAGE_APP_ADDR + p->stage_len - 4);
    ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))TRAMPOLINE_ADDR)(CFW_APP_ADDR, STAGE_APP_ADDR, p->stage_len,
                                                                                  TRAMPOLINE_ADDR + TRAMP_SEQ_OFFSET, sizeof(seq));
    for (;;) {
    }
}
