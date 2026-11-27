// The repair of the host application after the recovery program started it (host_core_flow_guard.h). The pages follow the update's (host_core_flow.cc): one page per
// state, one line per row, an error says what happened and what to do.
#include "host_core_flow_guard.h"
#include "host_core_flow_util.h"
#include "host_core_job.h"
#include "txu.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

using namespace hcf;

const char *hc_repair_medium(uint32_t medium)
{
    switch (medium) {
    case GM_FLASH_COPY: case GM_FLASH_NEW: return "the flash drive";
    case GM_SD: return "the SD card";
    case GM_USB: return "a USB stick";
    }
    return "an unknown medium";
}

namespace {

struct RepairUi {
    const HcFlowEnv *env;
    uint32_t medium, host_key;
    bool app_needed, copy_needed;
    int step;                        // 0 the application, 1 the copy and the record
    char detail[64];
    uint32_t done, total;
};

void rp_page(const RepairUi &u, bool force)
{
    HcPage pg;
    char l[124];
    hp_page(&pg, HCT_WARN, "Repairing the host application");
    hp_line(&pg, 0, HCT_WARN, "Another firmware replaced the host application in the flash.");
    snprintf(l, sizeof(l), "This copy was started from %s.", hc_repair_medium(u.medium));
    hp_line(&pg, 1, HCT_INK, l);
    snprintf(l, sizeof(l), "Write the host application   %s", !u.app_needed ? "not needed, the flash holds it" : (u.step > 0 ? "done" : "..."));
    hp_line(&pg, 3, !u.app_needed || u.step > 0 ? HCT_OK : HCT_INK, l);
    snprintf(l, sizeof(l), "Write the recovery copy   %s", u.step > 0 ? "..." : "");
    hp_line(&pg, 4, u.step > 0 ? HCT_INK : HCT_MUTED, l);
    hp_line(&pg, 5, HCT_MUTED, u.detail);
    uint32_t units = u.app_needed ? 3 : 1, base = u.step > 0 ? (u.app_needed ? 2 : 0) : 0;
    uint32_t d = u.total && u.step == 0 ? (uint32_t)((uint64_t)u.done * 2 / u.total) : 0;
    pg.bar = (int)((uint64_t)(base + d) * 100 / units);
    fw_pct(pg.pct, sizeof(pg.pct), (unsigned)pg.bar);
    snprintf(pg.hint, sizeof(pg.hint), "No key stops this    do not switch the power off");
    draw(*u.env, pg, force);
}

void rp_write_cb(void *ctx, int phase, uint32_t done, uint32_t total, const char *detail)
{
    RepairUi *u = (RepairUi *)ctx;
    (void)phase;
    snprintf(u->detail, sizeof(u->detail), "%s", detail);
    if (total && (!strcmp(detail, "erasing") || !strcmp(detail, "programming"))) {
        snprintf(u->detail, sizeof(u->detail), "%s sector %u of %u", detail, (unsigned)(done + (done < total ? 1 : 0)), (unsigned)total);
    }
    if (u->detail[0] >= 'a' && u->detail[0] <= 'z') {
        u->detail[0] = (char)(u->detail[0] - 'a' + 'A');
    }
    u->done = done;
    u->total = total;
    idle(*u->env);
    rp_page(*u, done == 0 || done >= total);
    hook(*u->env, -5, phase, total ? done * 100 / total : 100);
}

class RepairJob : public HcJob {
public:
    RepairJob(const HcFlowEnv &e, const GuardInfo &g, const uint8_t *st) : HcJob(e), gi(g), stage(st), k(0)
    {
        kind = 9;
    }

protected:
    bool run() override;

private:
    GuardInfo gi;
    const uint8_t *stage;
    FwImage img;
    const char *why;
    Flash *fl;
    RepairUi u;
    FwWriteResult wr;
    GuardOut go;
    HcPage pg;
    char l[124];
    int k;
};

bool RepairJob::run()
{
    HJ_BEGIN();
    state = HCJ_PROBING;
    // the check: nothing is written before it passes
    if (!guard_repair_check(gi, TXU_IFACE_NEEDED, stage, &img, &why)) {
        state = HCJ_RESULT;
        hp_page(&pg, HCT_WARN, "The host application was not repaired");
        fw_sentence(l, sizeof(l), why);
        hp_line(&pg, 0, HCT_WARN, l);
        if (gi.iface != TXU_IFACE_NEEDED) {
            hp_line(&pg, 1, HCT_INK, "The host in the flash does not match this application.");
        }
        hp_line(&pg, 2, HCT_INK, "Put the hostrecovery.bin of the installed release on a stick or the SD card, then restart.");
        hp_line(&pg, 3, HCT_INK, "Nothing was written. The flash is as it was.");
        snprintf(pg.hint, sizeof(pg.hint), "any key opens the boot menu");
        draw(env, pg);
        flog(env, "[host] guard: no repair: %s", why);
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    fl = env.flash(env.ctx);
    {
        const char *chip, *cwhy;
        if (!fw_flash_check(fl, &chip, &cwhy)) {
            state = HCJ_RESULT;
            hp_page(&pg, HCT_WARN, "The host application was not repaired");
            fw_sentence(l, sizeof(l), cwhy);
            hp_line(&pg, 0, HCT_WARN, l);
            hp_line(&pg, 1, HCT_INK, "Restart the board. The recovery program starts this application again.");
            hp_line(&pg, 2, HCT_INK, "Nothing was written. The flash is as it was.");
            snprintf(pg.hint, sizeof(pg.hint), "any key opens the boot menu");
            draw(env, pg);
            HJ_WAIT_KEY(k);
            return finish(false);
        }
    }
    memset(&u, 0, sizeof(u));
    u.env = &env;
    u.medium = gi.medium;
    u.host_key = cor_host_key;
    u.app_needed = !fw_flash_equals_app(fl, img);                    // a cut between the application and the record leaves the flash with it already
    u.copy_needed = gi.medium != GM_FLASH_COPY;
    snprintf(u.detail, sizeof(u.detail), "Starting");
    state = HCJ_RUNNING;
    can_cancel = false;
    rp_page(u, true);
    flog(env, "[host] guard: repair from %s: %u bytes, flash holds it already: %d", hc_repair_medium(gi.medium), (unsigned)img.app_len, (int)!u.app_needed);
    wr.ok = true;
    if (u.app_needed) {
        fw_write_parts(fl, img, FW_PART_APP, rp_write_cb, &u, &wr);
        flog(env, "[host] guard: application write: %s: %s", wr.ok ? "ok" : "FAILED", wr.why ? wr.why : "");
    }
    if (wr.ok) {
        u.step = 1;
        snprintf(u.detail, sizeof(u.detail), "Writing the recovery copy");
        rp_page(u, true);
        guard_repair_finish(fl, gi, img, &go);
        flog(env, "[host] guard: copy %s (%s), record %s (%s)", go.copy_ok ? "ok" : "FAILED", go.copy_why, go.record_ok ? "ok" : "FAILED", go.record_why);
    }
    state = HCJ_RESULT;
    can_cancel = false;
    if (!wr.ok) {
        hp_page(&pg, HCT_BAD, wr.started ? "The flash may not boot" : "Nothing was written");
        fw_sentence(l, sizeof(l), wr.why);
        hp_line(&pg, 0, HCT_WARN, l);
        hp_line(&pg, 1, HCT_INK, "Do not switch the power off. Press a key to write it again.");
        snprintf(pg.hint, sizeof(pg.hint), "any key writes again");
        draw(env, pg);
        if (env.write_failed) {
            env.write_failed(env.ctx, wr.why ? wr.why : "");
        }
        HJ_WAIT_KEY(k);
        pc = 0;                                                      // from the start: the check, then the write again from the stage
        return false;
    }
    hp_page(&pg, go.copy_ok && go.record_ok ? HCT_OK : HCT_WARN, "Host application repaired");
    hp_line(&pg, 0, HCT_OK, "The host application is back in the flash.");
    if (go.copy_ok && go.record_ok) {
        hp_line(&pg, 1, HCT_OK, "The recovery copy is current.");
    } else {
        snprintf(l, sizeof(l), "%s", !go.copy_ok ? (go.copy_room ? "The recovery copy does not fit on the flash drive." : "The recovery copy could not be written.")
                                                 : "The record could not be written.");
        hp_line(&pg, 1, HCT_WARN, l);
        hp_line(&pg, 2, HCT_INK, !go.copy_ok && go.copy_room ? "Delete files from the flash drive, then restart the board." : "Restart the board to try again.");
    }
    hp_line(&pg, 4, HCT_MUTED, "The next power-on starts normally.");
    snprintf(pg.hint, sizeof(pg.hint), "any key opens the boot menu");
    draw(env, pg);
    HJ_WAIT_KEY(k);
    return finish(true);
    HJ_END();
}

}  // namespace

HcJob *hc_repair_job(const HcFlowEnv &e, const GuardInfo &gi, const uint8_t *stage) { return new RepairJob(e, gi, stage); }
