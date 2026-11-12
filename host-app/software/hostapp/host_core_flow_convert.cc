// The conversion of a .cfw/.ue2 into a large .cor (host_core_flow_convert.h). The text of every page is the old hostui.cc's, word for word. The rules are
// fwconvert.cc's (benched without hardware: tests/hostapp/test_fwconvert.cc); this part is the order of the pages.
#include "host_core_flow_convert.h"
#include "host_core_flow_util.h"
#include "fileops.h"
#include "fwconvert.h"
#include "fwknown.h"
#include "cfw.h"
#include "mediaui.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

using namespace hcf;

static int n_cv_runs;                                         // conversions started (the bench's counter)
static char last_cv_why[160];

int hc_convert_runs(void) { return n_cv_runs; }
const char *hc_convert_why(void) { return last_cv_why; }

namespace hcf {
void cv_refuse_draw(const HcFlowEnv &e, const char *title, const char *l0, const char *l1)
{
    HcPage pg;
    hp_page(&pg, HCT_WARN, title);
    int n = 0;
    hp_line(&pg, n++, HCT_WARN, l0);
    if (l1 && l1[0]) {
        hp_line(&pg, n++, HCT_INK, l1);
    }
    hp_line(&pg, n, HCT_INK, "Nothing was packaged.");
    snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    snprintf(last_cv_why, sizeof(last_cv_why), "%s", l0);
    draw(e, pg);
}

}  // namespace hcf

namespace {

void cv_read_cb(void *ctx, uint32_t done, uint32_t total)
{
    ReadUi *u = (ReadUi *)ctx;
    const HcFlowEnv &e = *u->env;
    HcPage pg;
    hp_page(&pg, HCT_HOST, u->title);
    hp_line(&pg, 0, HCT_INK, "Comparing the file with the list of official firmware");
    char a[32], b[32];
    ui_size_text(a, sizeof(a), done);
    ui_size_text(b, sizeof(b), total);
    snprintf(pg.msg, sizeof(pg.msg), "%s of %s", a, b);
    pg.mcol = HCT_MUTED;
    pg.bar = total ? (int)((uint64_t)done * 100 / total) : 100;
    snprintf(pg.hint, sizeof(pg.hint), "ESC or LEFT cancels    nothing is written");
    draw(e, pg, done == 0 || done >= total);
    idle(e);
}

struct CvConvUi {
    const HcFlowEnv *env;
    char title[72];
    int phase;
};

int cv_conv_cb(void *ctx, int phase, uint64_t done, uint64_t total)
{
    CvConvUi *u = (CvConvUi *)ctx;
    const HcFlowEnv &e = *u->env;
    static const char *what[3] = { "Reading the firmware and hashing both parts", "Writing the .cor", "Reading it back and comparing" };
    HcPage pg;
    hp_page(&pg, HCT_HOST, u->title);
    for (int i = 0; i < 3; i++) {
        hp_line(&pg, i, i < phase ? HCT_OK : (i == phase ? HCT_INK : HCT_MUTED), what[i]);
    }
    char a[32], b[32];
    ui_size_text(a, sizeof(a), done);
    ui_size_text(b, sizeof(b), total);
    snprintf(pg.msg, sizeof(pg.msg), "%s of %s", a, b);
    pg.mcol = HCT_MUTED;
    pg.bar = total ? (int)(done * 100 / total) : 100;
    snprintf(pg.hint, sizeof(pg.hint), "ESC or LEFT cancels    nothing is left behind");
    bool newphase = phase != u->phase;
    u->phase = phase;
    draw(e, pg, newphase || done == 0 || done >= total);
    idle(e);
    hook(e, phase, done, total);
    return abort_key(e) ? 1 : 0;
}

}  // namespace

// ---- the conversion as a job: probing (the file, the plan) -> [asking: replace the .cor that is there?] -> running (the write) -> result ----------------------------------
namespace {

class ConvertJob : public HcJob {
public:
    ConvertJob(const HcConvertEnv &c, const char *p) : HcJob(*c.base), cenv(c), srcp(0)
    {
        snprintf(path, sizeof(path), "%s", p);
        dst[0] = 0;
        kind = 3;
    }
    ~ConvertJob() { delete srcp; }
    const char *dst_path() const { return dst; }

protected:
    bool run() override;

private:
    const HcConvertEnv &cenv;
    char path[200], file[56], title[72], l0[124], l1[124];
    char dst[FO_PATH_MAX];
    CorSource *srcp;
    CfwInfo ci;
    FwConvPlan plan;
    FwConvResult cr;
    CvConvUi cu;
    const FwKnown *known;
    const uint8_t *icon;
    ReadUi ru;
    char hex[65];
    HcPage pg;
    char a[124], b[32], c32[32];
    uint32_t buflen;
    uint8_t *buf;
    int k;
    int tgt;
    bool replace_ask;
};

#define CV_REFUSE(t, a_, b_) do { state = HCJ_RESULT; cv_refuse_draw(env, t, a_, b_); HJ_WAIT_KEY(k); return finish(false); } while (0)

bool ConvertJob::run()
{
    HJ_BEGIN();
    snprintf(file, sizeof(file), "%s", base_of(path));
    snprintf(title, sizeof(title), "Cannot package %s", file);
    srcp = env.open(env.ctx, path);
    cr = FWCV_ERR_FS;
    known = 0;
    if (!srcp || cfw_probe(*srcp, CFW_IDCODE_XC7A50T, ci) != CFW_OK) {
        CV_REFUSE(title, srcp ? "The file is not a usable firmware image." : "The file cannot be read.", "");
    }
    tgt = fwcv_target(path, dst, sizeof(dst));              // not inside a switch: the waits' labels belong to this function's switch
    if (tgt == FWCT_FLASH) {
        CV_REFUSE(title, "This firmware lies on the internal drive; the .cor is too large for it.", "Copy it to a card or stick with C, then make the .cor there.");
    } else if (tgt == FWCT_LONG) {
        CV_REFUSE(title, "The name of the .cor does not fit.", "");
    } else if (tgt != FWCT_OK) {
        CV_REFUSE(title, "The file name is not that of a .cfw or .ue2.", "");
    }
    memset(&ru, 0, sizeof(ru));
    ru.env = &env;
    snprintf(ru.title, sizeof(ru.title), "Checking %s", file);
    cv_read_cb(&ru, 0, 1);
    if (!fw_file_sha(*srcp, hex, cv_read_cb, &ru, abort_cb)) {
        CV_REFUSE(title, "Cancelled while reading the file.", "");
    }
    known = fw_known_lookup(hex);
    icon = cenv.icon(cenv.ctx, path, ci.name);
    cr = fwcv_plan(*hostfs_fo(), ci, known, true, dst, &plan);
    if (cr == FWCV_ERR_NOROOM || cr == FWCV_ERR_FS) {
        ui_size_text(a, sizeof(a), plan.size);
        ui_size_text(b, sizeof(b), plan.free);
        snprintf(l1, sizeof(l1), "The .cor needs %s, the medium has %s free.", a, b);
        CV_REFUSE(title, cr == FWCV_ERR_NOROOM ? "There is no room for it on that medium." : "The medium cannot take the .cor.", cr == FWCV_ERR_NOROOM ? l1 : "");
    }
    if (cr == FWCV_EXISTS) {                                    // T139: a file of that name is there: ask
        state = HCJ_ASKING;
        hp_page(&pg, HCT_WARN, "Replace the .cor that is there?");
        snprintf(l0, sizeof(l0), "%s is in the cores folder already.", base_of(dst));
        hp_line(&pg, 0, HCT_WARN, l0);
        hp_line(&pg, 1, HCT_INK, "The new file is written first and checked; the old one is replaced only then.");
        snprintf(pg.hint, sizeof(pg.hint), "RETURN replaces it    any other key cancels");
        draw(env, pg);
        for (;;) {
            hook(env, -2, 0, 0);                                // the question is up
            if ((k = env.getch(env.ctx)) >= 0) {
                break;
            }
            idle(env);
            sleep_ms(env, 20);
            HJ_YIELD();
        }
        if (k != KEY_RETURN) {
            if (env.note) {
                env.note(env.ctx, "Not replaced. Nothing was changed.", HCT_MUTED);
            }
            return finish(false);
        }
    }
    memset(&cu, 0, sizeof(cu));
    cu.env = &env;
    cu.phase = -1;
    snprintf(cu.title, sizeof(cu.title), "Packaging %s", base_of(dst));
    n_cv_runs++;
    state = HCJ_RUNNING;
    buflen = 0;
    buf = cenv.buf(cenv.ctx, &buflen);
    cr = fw_convert(*hostfs_fo(), *srcp, ci, plan, icon, cr == FWCV_EXISTS, buf, buflen, cv_conv_cb, &cu);
    flog(env, "[host] convert of %s: %s", file, fwcv_text(cr));
    state = HCJ_RESULT;
    if (cr != FWCV_OK) {
        snprintf(l0, sizeof(l0), "%s", fwcv_text(cr));
        hp_page(&pg, HCT_WARN, cr == FWCV_CANCELLED ? "Package cancelled" : "Packaging failed");
        hp_line(&pg, 0, HCT_WARN, l0);
        hp_line(&pg, 1, HCT_INK, "No half-written file was left behind. The firmware file is untouched.");
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
        snprintf(last_cv_why, sizeof(last_cv_why), "%s", l0);
        draw(env, pg);
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    hp_page(&pg, HCT_OK, "Packaged as .cor");
    ui_size_text(b, sizeof(b), plan.size);
    snprintf(a, sizeof(a), "%s  (%s), written and checked.", base_of(dst), b);
    hp_line(&pg, 0, HCT_OK, a);
    snprintf(a, sizeof(a), "Name: %s    Version: %s", plan.name, plan.version);
    hp_line(&pg, 1, HCT_INK, a);
    hp_line(&pg, 2, HCT_INK, "Made on this board from a firmware file.");
    hp_line(&pg, 3, HCT_MUTED, "Marked \"foreign\" in the boot menu. A adds it, S starts it.");
    snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    draw(env, pg);
    HJ_WAIT_KEY(k);
    return finish(true);
    HJ_END();
}

}  // namespace

HcJob *hc_convert_job(const HcConvertEnv &env, const char *path) { return new ConvertJob(env, path); }

const char *hc_convert_dst(const HcJob *finished_job) { return static_cast<const ConvertJob *>(finished_job)->dst_path(); }

bool hc_convert_run(const HcConvertEnv &env, const char *path_in, char *dst, int dst_n)
{
    ConvertJob *j = new ConvertJob(env, path_in);
    HcJob *job = j;
    HcJobIn in;
    in.key = -1;
    while (!job->step(in)) {
    }
    bool ok = job->ok;
    if (dst && dst_n > 0) {
        snprintf(dst, dst_n, "%s", j->dst_path());
    }
    delete job;
    return ok;
}
