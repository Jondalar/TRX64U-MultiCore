// Restoring the original firmware as a flow of the core (host_core_flow_restore.h). The text of every page is the old hostui.cc's, word for word.
#include "host_core_flow_restore.h"
#include "host_core_flow_util.h"
#include "fwrestore.h"
#include "fwknown.h"
#include "fwconfirm.h"
#include "mediaui.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

using namespace hcf;

namespace {

int g_fw_writes;
FwPkg fw_pkg;
FwCheck fw_check_res;

// A date from a FAT directory entry's stamp (hostfs_stat: date << 16 | time): "2026/10/25 12:00"
void fw_file_date(const HcRestoreEnv &r, const char *path, char *out, int n)
{
    uint32_t sz, mt;
    if (!r.file_stat(r.ctx, path, &sz, &mt) || !(mt >> 16)) {
        snprintf(out, n, "unknown date");
        return;
    }
    unsigned d = mt >> 16, t = mt & 0xFFFF;
    snprintf(out, n, "%04u/%02u/%02u %02u:%02u", 1980 + (d >> 9), (d >> 5) & 15, d & 31, t >> 11, (t >> 5) & 63);
}

struct FwAsk {
    char title[72];
    char detail[124];         // "Commodore 64 Ultimate 1.1.0   12 KiB + 69 KiB"
    char checks[124];
    char chip[32];
};

void fw_ask_page(HcPage &pg, const FwAsk &a, const FwConfirm &c, uint32_t now)
{
    hp_page(&pg, HCT_BAD, a.title);
    int n = 0;
    if (c.state == FWC_ASK2) {
        hp_line(&pg, n++, HCT_BAD, "Last question.");
        hp_line(&pg, n++, HCT_INK, "The running firmware is erased and written again.");
        hp_line(&pg, n++, HCT_WARN, "From the first erase on, no key stops the write.");
        hp_line(&pg, n++, HCT_MUTED, "The power must stay on until the result.");
        snprintf(pg.hint, sizeof(pg.hint), "RETURN starts the write (%d s)    any other key cancels", fwc_left(&c, now));
        return;
    }
    hp_line(&pg, n++, HCT_BAD, "This replaces the running firmware.");
    char d[124];
    snprintf(d, sizeof(d), "%s", a.detail);
    if (d[0] >= 'a' && d[0] <= 'z') {
        d[0] = (char)(d[0] - 'a' + 'A');                        // "the backup of ..." starts a line
    }
    hp_line(&pg, n++, HCT_INK, d);
    hp_line(&pg, n++, HCT_OK, a.checks);
    hp_line(&pg, n++, HCT_WARN, "To come back to MultiCore, run the MultiCore update file");
    hp_line(&pg, n++, HCT_WARN, "from that firmware's update menu.");
    hp_line(&pg, n++, HCT_MUTED, "Your cores and settings stay on the flash drive.");
    hp_line(&pg, n++, HCT_MUTED, "The restored firmware does not use them.");
    char t[124];
    snprintf(t, sizeof(t), "Flash chip: %s. Nothing has been written yet.", a.chip);
    hp_line(&pg, n++, HCT_MUTED, t);
    snprintf(pg.hint, sizeof(pg.hint), "Y continues    any other key cancels");
}

void fw_ask_page_cb(HcPage &pg, const void *ask, const FwConfirm &c, uint32_t now) { fw_ask_page(pg, *(const FwAsk *)ask, c, now); }

struct FwRunUi {
    const HcFlowEnv *env;
    char title[72];
    uint32_t a_sec, b_sec, rb_pages;       // the units of the three phases
    int phase;
    char detail[48];
    uint32_t done;
    uint32_t msg_until;                    // the "cannot stop now" line is up until this time
    bool msg_on;
};

const char *fw_phase_name[FWP_PHASES] = { "Write the application", "Write the bitstream", "Read both back and compare" };

void fw_run_page(const FwRunUi &u, bool force)
{
    const HcFlowEnv &e = *u.env;
    HcPage pg;
    hp_page(&pg, HCT_HOST, u.title);
    uint32_t t = e.now(e.ctx);
    hp_line(&pg, 0, HCT_OK, "Check the file   done");
    for (int i = 0; i < FWP_PHASES; i++) {
        char l[124];
        int col = HCT_MUTED;
        if (i < u.phase) {
            snprintf(l, sizeof(l), "%s   done", fw_phase_name[i]);
            col = HCT_OK;
        } else if (i == u.phase) {
            snprintf(l, sizeof(l), "%s   ...", fw_phase_name[i]);
            col = HCT_INK;
        } else {
            snprintf(l, sizeof(l), "%s", fw_phase_name[i]);
        }
        hp_line(&pg, 1 + i, col, l);
    }
    hp_line(&pg, 5, HCT_MUTED, u.detail);                      // row 4 stays empty: what is being done does not read as a fifth step
    uint32_t units = u.a_sec + u.b_sec + u.rb_pages / 16 + 1, base = u.phase == FWP_APP ? 0 : (u.phase == FWP_BIT ? u.a_sec : u.a_sec + u.b_sec);
    uint32_t d = u.phase == FWP_READBACK ? u.done / 16 : u.done;
    pg.bar = (int)((uint64_t)(base + d) * 100 / units);
    if (pg.bar > 100) {
        pg.bar = 100;
    }
    fw_pct(pg.pct, sizeof(pg.pct), (unsigned)pg.bar);
    pg.mcol = HCT_WARN;
    if (t < u.msg_until) {
        snprintf(pg.msg, sizeof(pg.msg), "Cannot stop now. Wait for the result.");
    }
    snprintf(pg.hint, sizeof(pg.hint), "No key stops this    do not switch the power off");
    draw(e, pg, force);
}

void fw_write_cb(void *ctx, int phase, uint32_t done, uint32_t total, const char *detail)
{
    FwRunUi *u = (FwRunUi *)ctx;
    const HcFlowEnv &e = *u->env;
    bool newphase = phase != u->phase;
    u->phase = phase;
    u->done = done;
    snprintf(u->detail, sizeof(u->detail), "%s", detail);
    if (total && phase != FWP_READBACK && (!strcmp(detail, "erasing") || !strcmp(detail, "programming"))) {
        snprintf(u->detail, sizeof(u->detail), "%s sector %u of %u", detail, (unsigned)(done + (done < total ? 1 : 0)), (unsigned)total);
    }
    if (u->detail[0] >= 'a' && u->detail[0] <= 'z') {
        u->detail[0] = (char)(u->detail[0] - 'a' + 'A');
    }
    // D7: every key is read and dropped; one that would stop an ordinary operation says why it does not
    int k;
    uint32_t now = e.now(e.ctx);
    while ((k = e.getch_nowait(e.ctx)) >= 0) {
        if (k == KEY_ESCAPE || k == KEY_BREAK || k == KEY_LEFT) {
            u->msg_until = now + 3000;
        }
    }
    idle(e);
    bool msg_now = e.now(e.ctx) < u->msg_until;
    bool msg_changed = msg_now != u->msg_on;
    u->msg_on = msg_now;
    fw_run_page(*u, newphase || done == 0 || done >= total || msg_changed);
    hook(e, -5, phase, total ? done * 100 / total : 100);      // the bench looks at the page and may press a key
}

// What the write did, as a page (built and drawn; the waiting is the job's).
void fw_result_page_ok(HcPage &pg, const FwImage &img, uint32_t ms)
{
    char a[124], b[64], c[64];
    hp_page(&pg, HCT_OK, "Firmware restored");
    ui_size_text(b, sizeof(b), img.bit_len);
    ui_size_text(c, sizeof(c), img.app_len);
    snprintf(a, sizeof(a), "Firmware written and verified (%s + %s).", b, c);
    hp_line(&pg, 0, HCT_OK, a);
    if (img.axss) {
        hp_line(&pg, 1, HCT_OK, "The boot loader finds the new firmware.");
    }
    snprintf(a, sizeof(a), "Took %u s. Cores, boot menu and settings were not touched.", (unsigned)(ms / 1000));
    hp_line(&pg, 4, HCT_MUTED, a);
}

void fw_result_page_fail(const HcRestoreEnv &re, HcPage &pg, const FwWriteResult &r)
{
    char a[124];
    hp_page(&pg, HCT_BAD, r.started ? "The flash may not boot" : "Nothing was written");
    fw_sentence(a, sizeof(a), r.why);
    hp_line(&pg, 0, HCT_WARN, a);
    snprintf(a, sizeof(a), "Stopped %s %s (address 0x%06X).", r.phase == FWP_READBACK ? "during" : "while writing",
             r.phase == FWP_APP ? "the application" : (r.phase == FWP_BIT ? "the bitstream" : "the read-back check"), (unsigned)r.sector);
    hp_line(&pg, 1, HCT_INK, a);
    if (r.started) {
        hp_line(&pg, 2, HCT_BAD, "Do not switch the power off.");
        hp_line(&pg, 3, HCT_INK, "RETURN writes it again from the copy in memory.");
        hp_line(&pg, 4, HCT_MUTED, "If you leave, the board needs to be repaired with a JTAG cable.");
        snprintf(pg.hint, sizeof(pg.hint), "RETURN writes again    ESC or LEFT leaves");
    } else {
        hp_line(&pg, 2, HCT_INK, "The flash is as it was.");
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    }
    if (re.write_failed) {
        re.write_failed(re.ctx, r.why ? r.why : "");
    }
}

}  // namespace

int hc_restore_writes(void) { return g_fw_writes; }

// ---- the restore as a job: probing -> asking -> running -> result (the straight-line code of the old hc_restore_run) ------------------------------------------
namespace {

class RestoreJob : public HcJob {
public:
    RestoreJob(const HcRestoreEnv &r, const char *p) : HcJob(*r.base), re(r), srcp(0), done_ok(false)
    {
        snprintf(path, sizeof(path), "%s", p);
        kind = 2;
    }
    ~RestoreJob() { delete srcp; }

protected:
    bool run() override;

private:
    const HcRestoreEnv &re;
    char path[200], file[56], title[72], l0[124], l1[124], name[96];
    bool done_ok;
    CorSource *srcp;
    const FwKnown *known;
    Flash *fl;
    const char *chip, *cwhy;
    bool chip_ok;
    ReadUi ru;
    FwImage img;
    FwAsk ask;
    char sz[96], b1[32], b2[32], hex[65], dt[32];
    const char *why, *more;
    fw_result_t pr;
    AskSub asub;
    FwRunUi ru2;
    FwWriteResult wr;
    uint32_t t0, ms;
    HcPage pg;
    RestartSub rsub;
    int k;
};

#define RS_REFUSE(t, a, b, c) do { state = HCJ_RESULT; fw_refuse_draw(env, t, a, b, c); HJ_WAIT_KEY(k); return finish(false); } while (0)

bool RestoreJob::run()
{
    HJ_BEGIN();
    snprintf(file, sizeof(file), "%s", base_of(path));
    snprintf(title, sizeof(title), "Cannot restore %s", file);
    known = 0;
    srcp = env.open(env.ctx, path);
    fl = env.flash(env.ctx);
    chip = "none";
    cwhy = "";
    if (!srcp) {
        RS_REFUSE(title, "The file cannot be read.", "", "");
    }
    pr = fw_probe(*srcp, CFW_IDCODE_XC7A50T, &fw_pkg);
    if (pr == FW_ERR_IO) {
        RS_REFUSE(title, "The file cannot be read.", "", "");
    }
    if (pr == FW_ERR_PROBE) {
        RS_REFUSE(title, cfw_result_str(fw_pkg.probe), "", "");
    }
    if (pr != FW_OK) {
        RS_REFUSE(title, "This is not a firmware file.", "", "");
    }
    chip_ok = fw_flash_check(fl, &chip, &cwhy);
    if (fw_pkg.kind == FWK_FIRMWARE) {                          // official only by the table: the whole file is hashed first
        memset(&ru, 0, sizeof(ru));
        ru.env = &env;
        snprintf(ru.title, sizeof(ru.title), "Checking %s", file);
        snprintf(ru.what, sizeof(ru.what), "Comparing the file with the list of official firmware");
        read_cb(&ru, 0, 1);
        if (!fw_file_sha(*srcp, hex, read_cb, &ru, abort_cb)) {
            RS_REFUSE(title, "Cancelled while reading the file.", "", "");
        }
        known = fw_known_lookup(hex);
        if (!known) {
            RS_REFUSE(title, "This file is not on the list of official firmware.", "Only a backup of this board and official releases are restored.",
                      "P turns it into a .cor that S starts.");
        }
    }
    if (!fw_can_restore(fw_pkg, known, chip)) {
        why = "This file cannot be restored.";
        more = "";
        if (fw_pkg.kind == FWK_BACKUP_COR) {
            why = "This backup was read from another flash chip.";
            snprintf(l1, sizeof(l1), "Backup: %s.  This board: %s.", fw_pkg.flash_id, chip);
            more = l1;
        } else if (fw_pkg.kind == FWK_FOREIGN_COR) {
            why = "A converted .cor cannot be restored.";
            more = "Use the original .cfw/.ue2 or a backup.";
        } else if (fw_pkg.kind == FWK_OUR_COR) {
            why = "This is a TRX64 firmware, not a backup of the original.";
        }
        RS_REFUSE(title, why, more, "");
    }
    if (known) {
        snprintf(name, sizeof(name), "%s %s %s", known->vendor, known->name, known->version);
    } else if (fw_pkg.name[0]) {
        snprintf(name, sizeof(name), "%s", fw_pkg.name);
    } else {
        fw_file_date(re, path, dt, sizeof(dt));
        snprintf(name, sizeof(name), "the backup of %s", dt);
    }
    if (!chip_ok) {
        if (!fw_chip_ok(chip)) {
            snprintf(l0, sizeof(l0), "This firmware is for a Winbond W25Q128 flash chip. This board has %s.", strcmp(chip, "none") ? chip : "none that is known");
        } else {
            snprintf(l0, sizeof(l0), "%s", cwhy);
        }
        RS_REFUSE(title, l0, "", "");
    }
    // the check: read into DDR2 and compare, nothing is written
    memset(&ru, 0, sizeof(ru));
    ru.env = &env;
    snprintf(ru.title, sizeof(ru.title), "Checking %s", file);
    snprintf(ru.what, sizeof(ru.what), "Reading the file and checking every byte of it");
    read_cb(&ru, 0, 1);
    if (!fw_restore_stage(*srcp, fw_pkg, known, chip, env.bit_buf(env.ctx), env.app_buf(env.ctx), read_cb, &ru, abort_cb, &fw_check_res, &img)) {
        if (!strcmp(fw_check_res.why, "cancelled")) {
            RS_REFUSE(title, "Cancelled while reading the file.", "", "");
        } else {
            RS_REFUSE("Firmware check failed", fw_check_res.why, fw_why_line(fw_check_res.why), "");
        }
    }
    if (fw_flash_equals(fl, img)) {
        state = HCJ_RESULT;
        hp_page(&pg, HCT_OK, "Nothing to write");
        hp_line(&pg, 0, HCT_OK, "The flash already holds exactly this firmware.");
        hp_line(&pg, 1, HCT_MUTED, "Nothing was written.");
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
        draw(env, pg);
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    // the questions
    memset(&ask, 0, sizeof(ask));
    snprintf(ask.title, sizeof(ask.title), "Restore %s?", name);              // the name of what comes back, never an id
    snprintf(ask.chip, sizeof(ask.chip), "%s", chip);
    ui_size_text(b1, sizeof(b1), fw_pkg.bit_flash_len);
    ui_size_text(b2, sizeof(b2), fw_pkg.app_flash_len);
    snprintf(sz, sizeof(sz), "%s + %s", b1, b2);
    snprintf(ask.detail, sizeof(ask.detail), "%s   %s", name, sz);
    snprintf(ask.checks, sizeof(ask.checks), "%s", known ? "Official release. The file is complete and intact." : "The backup is complete and intact.");
    flog(env, "[host] restore of %s asked for, %u + %u bytes staged", file, (unsigned)img.bit_len, (unsigned)img.app_len);
    state = HCJ_ASKING;
    asub.begin(env, &ask, fw_ask_page_cb);
    HJ_SUB(asub);
    if (!asub.go) {
        flog(env, "[host] firmware write of %s: not confirmed, nothing written", file);
        env.note(env.ctx, "restore cancelled, nothing was written", HCT_MUTED);
        return finish(false);
    }
    // the write, from here on no key stops it
    for (;;) {
        state = HCJ_RUNNING;
        can_cancel = false;
        memset(&ru2, 0, sizeof(ru2));
        ru2.env = &env;
        snprintf(ru2.title, sizeof(ru2.title), "Restoring %s", name);
        ru2.a_sec = (img.app_len + FW_SECTOR - 1) / FW_SECTOR;
        ru2.b_sec = (img.bit_len + FW_SECTOR - 1) / FW_SECTOR;
        ru2.rb_pages = (img.bit_len + img.app_len + FW_PAGE - 1) / FW_PAGE;
        ru2.phase = FWP_APP;
        snprintf(ru2.detail, sizeof(ru2.detail), "Starting");
        fw_run_page(ru2, true);
        g_fw_writes++;
        flog(env, "[host] writing the firmware flash: %u bytes at 0x%06X, %u at 0x%06X", (unsigned)img.bit_len, (unsigned)FW_BIT_ADDR, (unsigned)img.app_len,
             (unsigned)FW_APP_ADDR);
        t0 = env.now(env.ctx);
        fw_write(fl, img, fw_write_cb, &ru2, &wr);
        ms = env.now(env.ctx) - t0;
        flog(env, "[host] firmware write: %s after %u ms (%u erases, %u programs): %s", wr.ok ? "ok" : "FAILED", (unsigned)ms, (unsigned)wr.erases,
             (unsigned)wr.programs, wr.why ? wr.why : "");
        state = HCJ_RESULT;
        if (wr.ok) {
            fw_result_page_ok(pg, img, ms);
            rsub.begin(env, pg, 3, "the restored firmware");
            HJ_SUB(rsub);
            done_ok = wr.ok;
            return finish(done_ok);
        }
        fw_result_page_fail(re, pg, wr);
        draw(env, pg);
        hook(env, -8, 0, 0);                                      // the result page of a write that failed is up
        if (!wr.started) {
            HJ_WAIT_KEY(k);
            return finish(false);
        }
        for (;;) {
            k = poll_key();
            if (k == KEY_RETURN) {
                break;                                           // write again from the copy in memory
            }
            if (k == KEY_ESCAPE || k == KEY_BREAK || k == KEY_LEFT) {
                return finish(false);
            }
            if (k < 0) {
                idle(env);
                sleep_ms(env, 20);
            }
            HJ_YIELD();
        }
    }
    HJ_END();
}

}  // namespace

HcJob *hc_restore_job(const HcRestoreEnv &re, const char *path) { return new RestoreJob(re, path); }

bool hc_restore_run(const HcRestoreEnv &re, const char *path) { return hc_job_run(new RestoreJob(re, path)); }
