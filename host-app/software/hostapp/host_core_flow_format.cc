// The format flow of the host application (host_core_flow_format.h). The text of every page is the old hostui.cc's, word for word.
#include "host_core_flow_format.h"
#include "host_core_flow_util.h"
#include "fmtconfirm.h"
#include "mediaglue.h"
#include "mediaui.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

using namespace hcf;

namespace {

char g_why[160];       // the bench reads it
int g_runs;            // formats started

FmtcKey key_class(int k, int *ch)
{
    *ch = 0;
    if (k == KEY_RETURN) return FMTK_RETURN;
    if (k == KEY_BACK || k == KEY_DELETE) return FMTK_BACKSPACE;
    if (k == KEY_ESCAPE || k == KEY_BREAK) return FMTK_ESCAPE;
    if (k >= 0x20 && k < 0x7F) {
        *ch = k;
        return FMTK_CHAR;
    }
    return FMTK_OTHER;
}

// ESC and RUN/STOP only (not the left arrow: the format's page says "ESC or RUN/STOP cancels").
bool cancel_key(const HcFlowEnv &e)
{
    int k;
    while ((k = e.getch_nowait(e.ctx)) >= 0) {
        if (k == KEY_BREAK || k == KEY_ESCAPE) {
            return true;
        }
    }
    return false;
}

// The page that asks (TYPING and ARMED).
void ask_page(HcPage &pg, const MediaInfo &mi, int rows, const FmtConfirm &fc, uint32_t now)
{
    char a[96], b[64];
    char title[72];
    snprintf(title, sizeof(title), "Format %s as exFAT?", mi.name);
    hp_page(&pg, HCT_BAD, title);
    snprintf(a, sizeof(a), "ALL DATA ON %s WILL BE ERASED.", mi.name);
    hp_line(&pg, 0, HCT_BAD, a);
    char sz[24], fs[80];
    ui_size_text(sz, sizeof(sz), (uint64_t)mi.sectors * mi.sector_size);
    ui_fs_text(fs, sizeof(fs), &mi.sn);
    snprintf(a, sizeof(a), "%s  -  %s", sz, fs);
    hp_line(&pg, 1, HCT_INK, a);
    ui_rows_text(b, sizeof(b), rows);
    if (rows > 0) {
        snprintf(a, sizeof(a), "%s; they show 'missing' until their files are copied back.", b);
    } else {
        snprintf(a, sizeof(a), "%s.", b);
    }
    hp_line(&pg, 2, rows > 0 ? HCT_WARN : HCT_MUTED, a);
    hp_line(&pg, 3, HCT_MUTED, "Result: one exFAT partition from 4 MiB to the end. Files of 4 GiB and more fit.");
    pg.input = true;
    snprintf(pg.msg, sizeof(pg.msg), "%s", fc.msg ? fc.msg : "");
    pg.mcol = HCT_WARN;
    if (fc.state == FMTC_ARMED) {
        int left = fmtc_arm_left(&fc, now);
        snprintf(pg.typed, sizeof(pg.typed), "%s", fc.typed);
        hp_line(&pg, 4, HCT_BAD, "The name is right. This is the last question.");
        snprintf(a, sizeof(a), "Press RETURN once more within %d s to erase %s.", left, mi.name);
        hp_line(&pg, 5, HCT_INK, a);
        snprintf(pg.hint, sizeof(pg.hint), "RETURN starts the format    any other key cancels, nothing has been written");
    } else {
        snprintf(pg.typed, sizeof(pg.typed), "%s_", fc.typed);
        hp_line(&pg, 4, HCT_MUTED, "Until the first write you can leave with ESC, and nothing has changed.");
        snprintf(a, sizeof(a), "To go on, type the name of the medium (%s) and press RETURN:", mi.name);
        hp_line(&pg, 5, HCT_INK, a);
        snprintf(pg.hint, sizeof(pg.hint), "RETURN confirms the name    BACKSPACE deletes    ESC cancels");
    }
}

struct Run {
    const HcFlowEnv *env;
    int last_step;
    uint32_t last_written, total;
    bool cancel_asked;
    char name[24];
};

int progress(void *ctx, int step, uint32_t written, uint32_t total)
{
    Run *r = (Run *)ctx;
    const HcFlowEnv &e = *r->env;
    r->last_step = step;
    r->last_written = written;
    r->total = total;
    HcPage pg;
    char title[72];
    snprintf(title, sizeof(title), "Formatting %s", r->name);
    hp_page(&pg, HCT_HOST, title);
    char t[124];
    hp_line(&pg, 0, HCT_INK, ui_step_name(step));
    ui_progress_text(t, sizeof(t), step, written, total);
    hp_line(&pg, 1, HCT_INK, t);
    hp_line(&pg, 3, HCT_WARN, "Do not remove the medium.");
    pg.bar = ui_percent(step, written, total, false);
    if (step <= FST_TABLE) {
        snprintf(pg.hint, sizeof(pg.hint), "ESC or RUN/STOP cancels: the medium is blank then");
    } else {
        snprintf(pg.hint, sizeof(pg.hint), "The table is written, the volume is complete");
    }
    draw(e, pg, step != FST_WRITE || written == 0);
    idle(e);
    if (step <= FST_TABLE && cancel_key(e)) {
        r->cancel_asked = true;
        return 1;
    }
    return 0;
}

void result_page(const HcFlowEnv &e, const char *name, FmtResult res, const char *why, const Run &run, const MediaInfo *after, int rows)
{
    HcPage pg;
    char a[124], title[72];
    if (res == FMT_OK) {
        snprintf(title, sizeof(title), "%s is formatted", name);
        hp_page(&pg, HCT_OK, title);
        char sz[24], fs[80];
        ui_size_text(sz, sizeof(sz), after ? (uint64_t)after->sectors * after->sector_size : 0);
        ui_fs_text(fs, sizeof(fs), after ? &after->sn : 0);
        snprintf(a, sizeof(a), "%s  -  %s", sz, fs);
        hp_line(&pg, 0, HCT_OK, a);
        char fr[24] = "?";
        uint64_t fb = 0;
        if (media_free_bytes(name, &fb)) {
            ui_size_text(fr, sizeof(fr), fb);
        }
        snprintf(a, sizeof(a), "The host has mounted it: %s free, no files yet.", fr);
        hp_line(&pg, 1, HCT_INK, a);
        if (rows > 0) {
            snprintf(a, sizeof(a), "Boot menu: %d row%s point%s here, 0 found. Copy the cores back and they turn ready.", rows, rows == 1 ? "" : "s", rows == 1 ? "s" : "");
            hp_line(&pg, 2, HCT_WARN, a);
        } else {
            hp_line(&pg, 2, HCT_MUTED, "Boot menu: no row points here.");
        }
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    } else {
        snprintf(title, sizeof(title), res == FMT_CANCELLED ? "Format of %s cancelled" : (res == FMT_REFUSED ? "%s was not formatted" : "Format of %s failed"), name);
        hp_page(&pg, HCT_BAD, title);
        hp_line(&pg, 0, HCT_WARN, why && why[0] ? why : "no reason given");
        if (res == FMT_REFUSED || run.last_written == 0) {
            hp_line(&pg, 1, HCT_INK, "Nothing was written. The medium is as it was.");
        } else if (run.last_step >= FST_SYNC) {
            hp_line(&pg, 1, HCT_INK, "The format was written but could not be checked. Take the medium out and put it in again.");
        } else {
            hp_line(&pg, 1, HCT_INK, "The medium is blank now: no partition table, no volume.");
            hp_line(&pg, 2, HCT_MUTED, "Press F in the File Browser to format it again.");
        }
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    }
    draw(e, pg);
}

}  // namespace

int hc_format_runs(void) { return g_runs; }
const char *hc_format_why(void) { return g_why; }

// The whole act as a job: refusals first (a note, nothing shown), the two-step confirmation (asking), the format (running), the result page.
namespace {

class FormatJob : public HcJob {
public:
    FormatJob(const HcFormatEnv &f, const char *nm) : HcJob(*f.base), fe(f), outcome(HCFO_REFUSED)
    {
        snprintf(name, sizeof(name), "%s", nm);
        kind = 5;
    }
    HcFormatOutcome result() const { return outcome; }

protected:
    bool run() override;

private:
    const HcFormatEnv &fe;
    char name[24], n[96];
    MediaInfo mi;
    int rows;
    FmtConfirm fc;
    HcPage pg;
    bool go;
    uint32_t shown_left;
    Run runst;
    char why[160];
    uint32_t t0;
    FmtResult res;
    MediaInfo after;
    HcFormatOutcome outcome;
};

bool FormatJob::run()
{
    HJ_BEGIN();
    if (!media_info(name, &mi) || !mi.found) {
        env.note(env.ctx, "that is not a medium", HCT_WARN);
        outcome = HCFO_REFUSED;
        return finish(false);
    }
    if (!mi.formattable) {
        snprintf(n, sizeof(n), "%s is the internal drive and is never formatted from here", name);
        env.note(env.ctx, n, HCT_WARN);
        outcome = HCFO_REFUSED;
        return finish(false);
    }
    if (!mi.ready) {
        snprintf(n, sizeof(n), "no medium in %s", name);
        env.note(env.ctx, n, HCT_WARN);
        outcome = HCFO_REFUSED;
        return finish(false);
    }
    if (mi.refuse) {
        snprintf(n, sizeof(n), "cannot format %s: %s", name, mi.refuse);
        env.note(env.ctx, n, HCT_WARN);
        outcome = HCFO_REFUSED;
        return finish(false);
    }
    rows = fe.boot_rows(fe.ctx, name);
    fe.begin(fe.ctx);
    flog(env, "[host] format of %s asked for: %u sectors, %d boot rows on it", name, (unsigned)mi.sectors, rows);
    state = HCJ_ASKING;
    fmtc_open(&fc, name, env.now(env.ctx));
    ask_page(pg, mi, rows, fc, env.now(env.ctx));
    draw(env, pg);
    go = false;
    shown_left = 0;
    for (;;) {
        {
            int k = poll_key_nowait();
            uint32_t t = env.now(env.ctx);
            FmtcEvent ev = FMTC_NOTHING;
            if (k > 0) {
                int ch;
                FmtcKey kc = key_class(k, &ch);
                ev = fmtc_key(&fc, kc, ch, t);
            } else {
                sleep_ms(env, 20);
                ev = fmtc_tick(&fc, t, media_present(name));
                if (ev == FMTC_REDRAW && fc.state == FMTC_ARMED) {
                    uint32_t left = (uint32_t)fmtc_arm_left(&fc, t);
                    if (left == shown_left) {
                        ev = FMTC_NOTHING;               // only a new second redraws the countdown
                    }
                    shown_left = left;
                }
            }
            idle(env);
            if (ev == FMTC_GO) {
                go = true;
                break;
            }
            if (ev == FMTC_ABORT) {
                break;
            }
            if (ev == FMTC_REDRAW) {
                ask_page(pg, mi, rows, fc, t);
                draw(env, pg);
            }
        }
        HJ_YIELD();
    }
    if (!go) {
        flog(env, "[host] format of %s: not confirmed, nothing written", name);
        fe.end(fe.ctx);
        snprintf(n, sizeof(n), "format of %s cancelled, nothing was written", name);
        env.note(env.ctx, n, HCT_MUTED);
        outcome = HCFO_NOT_CONFIRMED;
        return finish(false);
    }
    state = HCJ_RUNNING;
    can_cancel = false;
    memset(&runst, 0, sizeof(runst));
    snprintf(runst.name, sizeof(runst.name), "%s", name);
    runst.env = &env;
    g_runs++;
    flog(env, "[host] formatting %s as exFAT, free heap %u bytes", name, (unsigned)media_free_heap());
    why[0] = 0;
    t0 = env.now(env.ctx);
    res = media_format(name, progress, &runst, why, sizeof(why));
    flog(env, "[host] format of %s: result %d (%s) after %u ms, %u bytes written of %u estimated", name, (int)res, why, (unsigned)(env.now(env.ctx) - t0),
         (unsigned)runst.last_written, (unsigned)runst.total);
    snprintf(g_why, sizeof(g_why), "%s", why);
    memset(&after, 0, sizeof(after));
    media_info(name, &after);
    state = HCJ_RESULT;
    result_page(env, name, res, why, runst, &after, rows);
    for (;;) {
        if (poll_key() >= 0) {
            break;
        }
        idle(env);
        sleep_ms(env, 20);
        HJ_YIELD();
    }
    fe.end(fe.ctx);
    if (res == FMT_OK) {
        snprintf(n, sizeof(n), "%s formatted as exFAT", name);
        env.note(env.ctx, n, HCT_OK);
    } else {
        snprintf(n, sizeof(n), "format of %s: %s", name, res == FMT_CANCELLED ? "cancelled" : (res == FMT_REFUSED ? "refused" : "failed"));
        env.note(env.ctx, n, res == FMT_REFUSED ? HCT_WARN : HCT_BAD);
    }
    outcome = HCFO_RAN;
    return finish(res == FMT_OK);
    HJ_END();
}

}  // namespace

HcJob *hc_format_job(const HcFormatEnv &fe, const char *name_in) { return new FormatJob(fe, name_in); }

HcFormatOutcome hc_format_outcome(const HcJob *j) { return static_cast<const FormatJob *>(j)->result(); }

HcFormatOutcome hc_format_run(const HcFormatEnv &fe, const char *name_in)
{
    FormatJob *j = new FormatJob(fe, name_in);
    HcJobIn in;
    in.key = -1;
    while (!j->step(in)) {
    }
    HcFormatOutcome o = j->result();
    delete j;
    return o;
}
