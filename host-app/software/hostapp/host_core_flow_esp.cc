// The ESP32 flows (host_core_flow_esp.h). The text of every page is the old hostui.cc's, word for word.
#include "host_core_flow_esp.h"
#include "host_core_flow_convert.h"
#include "host_core_flow_util.h"
#include "espinspect.h"
#include "espflash.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

using namespace hcf;

static char last_esp_line[160];
static int n_esp_writes;                                          // the bench counts the writes into the ESP32

int hc_esp_writes(void) { return n_esp_writes; }
const char *hc_inspect_line(void) { return last_esp_line; }

namespace {

// ---- T152 S8: write the ESP32 image set of the inspected file into the ESP32 (U on the inspection page). The flash of the board is not touched. -----------
const char *const esp_stage_text[EFS_STAGES] = { "Starting the ESP32's ROM loader", "Connecting to the ROM loader", "Setting up the ESP32's flash",
                                                  "Writing the bootloader", "Writing the partition table", "Writing the application",
                                                  "Starting the new firmware and asking its version" };
const char *const esp_stopped_text[EFS_STAGES] = { "starting the ROM loader", "connecting to the ROM loader", "setting up the flash",
                                                    "writing the bootloader", "writing the partition table", "writing the application",
                                                    "asking the new firmware for its version" };
struct EspUpUi {
    const HcFlowEnv *env;
    char title[72];
    int last_stage;
};

void esp_up_cb(void *ctx, int stage, uint32_t done, uint32_t total)
{
    EspUpUi *u = (EspUpUi *)ctx;
    const HcFlowEnv &e = *u->env;
    while (e.getch_nowait(e.ctx) >= 0) {
    }                                                          // no key stops it, and none is kept for the page after
    HcPage pg;
    hp_page(&pg, HCT_HOST, u->title);
    hp_line(&pg, 0, HCT_INK, esp_stage_text[stage]);
    char t[124];
    snprintf(t, sizeof(t), "%u of %u blocks of 1 KiB", (unsigned)done, (unsigned)total);
    hp_line(&pg, 1, HCT_MUTED, t);
    pg.bar = total ? (int)((uint64_t)done * 100 / total) : 0;
    fw_pct(pg.pct, sizeof(pg.pct), (unsigned)pg.bar);
    snprintf(pg.hint, sizeof(pg.hint), "no key stops it    do not switch the power off");
    draw(e, pg, stage != u->last_stage || done == 0 || done >= total);
    u->last_stage = stage;
    idle(e);
    hook(e, -5, (uint64_t)(100 + stage), total ? done * 100 / total : 0);      // the bench takes a picture of the write
}

// the question: one, plain (nothing of the board's flash can be damaged; the ROM loader of the chip cannot be overwritten)
void esp_up_ask_page(HcPage &pg, const char *file, const EspInfo &ei)
{
    char a[124];
    hp_page(&pg, HCT_WARN, "Update the ESP32?");
    snprintf(a, sizeof(a), "Writes ESP32 firmware %s from %s.", ei.version[0] ? ei.version : "(no version text)", file);
    hp_line(&pg, 0, HCT_INK, a);
    hp_line(&pg, 1, HCT_INK, "Bootloader, partition table and application. The board's flash is not touched.");
    hp_line(&pg, 2, HCT_INK, "WiFi and the control module are off while it runs, up to a minute.");
    hp_line(&pg, 3, HCT_WARN, "Do not switch the power off.");
    snprintf(pg.hint, sizeof(pg.hint), "Y writes it    any other key cancels");
}

// What the write did, as a page (built; `again`: only after a failure that changed the ESP32's flash the page offers the write again). The drawing and the waiting are the job's.
void esp_up_result_page(const HcFlowEnv &e, HcPage &pg, bool &again, const EspInfo &ei, const EspFlashResult &r)
{
    char a[124], b[124];
    again = false;
    if (r.ok) {
        const bool warn = r.ident != EFI_MATCH && r.ident != EFI_NOT_ASKED;
        hp_page(&pg, warn ? HCT_WARN : HCT_OK, warn ? "ESP32 written" : "ESP32 updated");
        snprintf(a, sizeof(a), "Written: ESP32 firmware %s.", ei.version[0] ? ei.version : "(no version text)");
        hp_line(&pg, 0, warn ? HCT_INK : HCT_OK, a);
        if (r.ident == EFI_MATCH) {
            snprintf(a, sizeof(a), "The module reports V%u.%u, the version in the file.", (unsigned)r.major, (unsigned)r.minor);
            hp_line(&pg, 1, HCT_OK, a);
        } else if (r.ident == EFI_NO_ANSWER) {
            fw_sentence(a, sizeof(a), r.why);
            hp_line(&pg, 1, HCT_WARN, a);
            hp_line(&pg, 2, HCT_INK, "Look at F8 -> WiFi in a minute, or switch the power off and on.");
        } else {
            hp_line(&pg, 1, HCT_INK, "The file's application has no version text to compare with.");
        }
        hp_line(&pg, 3, HCT_MUTED, "The board's flash was not touched.");
    } else if (!r.started) {
        hp_page(&pg, HCT_BAD, "Nothing was written");
        fw_sentence(a, sizeof(a), r.why);
        hp_line(&pg, 0, HCT_WARN, a);
        if (r.stage >= 0 && r.stage < EFS_STAGES && r.why[0]) {
            snprintf(b, sizeof(b), "Stopped while %s.", esp_stopped_text[r.stage]);
            hp_line(&pg, 1, HCT_INK, b);
        }
        hp_line(&pg, 2, HCT_INK, "The ESP32's flash is as it was.");
    } else {
        hp_page(&pg, HCT_BAD, r.ident == EFI_DIFFERS ? "The ESP32 reports another version" : "The ESP32 may not start");
        fw_sentence(a, sizeof(a), r.why);
        hp_line(&pg, 0, HCT_WARN, a);
        if (r.ident == EFI_DIFFERS) {
            snprintf(b, sizeof(b), "The file has %s, the module reports V%u.%u.", ei.version, (unsigned)r.major, (unsigned)r.minor);
        } else {
            snprintf(b, sizeof(b), "Stopped while %s.", esp_stopped_text[r.stage < EFS_STAGES ? r.stage : 0]);
        }
        hp_line(&pg, 1, HCT_INK, b);
        hp_line(&pg, 2, HCT_INK, "The ESP32's flash holds a part of the firmware. Writing it again repairs that:");
        hp_line(&pg, 3, HCT_INK, "the chip's ROM loader cannot be damaged by a failed write.");
        again = true;
    }
    if (e.write_failed) {
        e.write_failed(e.ctx, r.why ? r.why : "");
    }
    snprintf(pg.hint, sizeof(pg.hint), "%s", again ? "RETURN writes again    ESC or LEFT leaves" : "any key returns to the File Browser");
}

}  // namespace

// ---- T152 D8/S9: look into a .cfw/.ue2 for its ESP32 image set (F8 -> Inspect, then RETURN on the file). Read only. -----------------------------
// As a job: probing (the read) -> result (the page: what the file holds; U asks) -> asking (the ESP32 question) -> running (the write) -> result.
namespace {

class InspectJob : public HcJob {
public:
    InspectJob(const HcEspEnv &c, const char *p) : HcJob(*c.base), cenv(c), srcp(0)
    {
        snprintf(path, sizeof(path), "%s", p);
        kind = 4;
    }
    ~InspectJob() { delete srcp; }

protected:
    bool run() override;

private:
    const HcEspEnv &cenv;
    char path[200], file[56], title[72], a[124];
    CorSource *srcp;
    HcPage pg;
    ReadUi ru;
    EspInfo ei;
    EspResult r;
    int border, n, k;
    EspFlashLink *link;
    EspUpUi u;
    EspFlashResult fr;
    uint32_t t0;
    bool again;
};

bool InspectJob::run()
{
    HJ_BEGIN();
    snprintf(file, sizeof(file), "%s", base_of(path));
    srcp = env.open(env.ctx, path);
    if (!srcp) {
        snprintf(title, sizeof(title), "Cannot inspect %s", file);
        state = HCJ_RESULT;
        cv_refuse_draw(env, title, "The file cannot be read.", "");
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    memset(&ru, 0, sizeof(ru));
    ru.env = &env;
    snprintf(ru.title, sizeof(ru.title), "Looking into %s", file);
    snprintf(ru.what, sizeof(ru.what), "Searching the file for an ESP32 image set");
    read_cb(&ru, 0, 1);
    memset(&ei, 0, sizeof(ei));
    r = esp_inspect(*srcp, &ei, read_cb, &ru, abort_cb);
    border = r == ESP_FOUND ? HCT_OK : HCT_WARN;
    snprintf(title, sizeof(title), "ESP32 in %s", file);
    hp_page(&pg, border, title);
    n = 0;
    switch (r) {
    case ESP_FOUND:
        snprintf(a, sizeof(a), "ESP32 firmware %s", ei.version[0] ? ei.version : "(no version string in it)");
        hp_line(&pg, n++, HCT_OK, a);
        snprintf(a, sizeof(a), "Bootloader, partition table and the application %s; its SHA-256 matches.", ei.project[0] ? ei.project : "(unnamed)");
        hp_line(&pg, n++, HCT_INK, a);
        break;
    case ESP_NONE:
        hp_line(&pg, n++, HCT_WARN, "This file has no ESP32 part.");
        break;
    case ESP_INCOMPLETE:
        hp_line(&pg, n++, HCT_WARN, "An ESP32 application is in this file, but not a whole image set.");
        snprintf(a, sizeof(a), "ESP32 firmware %s; the bootloader or the partition table is missing.", ei.version[0] ? ei.version : "(no version string)");
        hp_line(&pg, n++, HCT_INK, a);
        break;
    case ESP_DAMAGED:
        hp_line(&pg, n++, HCT_BAD, "The ESP32 application in this file is damaged (SHA-256 differs).");
        break;
    case ESP_ABORTED:
        hp_line(&pg, n++, HCT_WARN, "Cancelled while reading the file.");
        break;
    default:
        hp_line(&pg, n++, HCT_WARN, "The file cannot be read.");
        break;
    }
    hp_line(&pg, n++, HCT_MUTED, "Nothing was written.");
    snprintf(last_esp_line, sizeof(last_esp_line), "%s", pg.line[0]);
    snprintf(pg.hint, sizeof(pg.hint), "%s", r == ESP_FOUND ? "U writes it into the ESP32    any other key returns" : "any key returns to the File Browser");
    cenv.yes_key(cenv.ctx, r == ESP_FOUND ? 'u' : KEY_RETURN);
    state = HCJ_RESULT;
    draw(env, pg);
    if (r != ESP_FOUND) {
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    for (;;) {                                                                          // S8: the one way on from the inspection
        hook(env, -1, 0, 0);
        k = env.getch(env.ctx);
        if (k >= 0) {
            break;
        }
        idle(env);
        sleep_ms(env, 20);
        HJ_YIELD();
    }
    if (k != 'u' && k != 'U') {
        return finish(false);
    }
    // the question: one, plain
    state = HCJ_ASKING;
    esp_up_ask_page(pg, file, ei);
    while ((k = env.getch_nowait(env.ctx)) >= 0) {
    }                                                          // a key from before the page is not an answer to it
    cenv.yes_key(cenv.ctx, 'y');
    draw(env, pg);
    hook(env, -3, 0, 0);                                       // the bench looks at the question and answers it
    for (;;) {
        k = poll_key();
        if (k >= 0) {
            break;
        }
        idle(env);
        sleep_ms(env, 20);
        HJ_YIELD();
    }
    if (!(k == 'y' || k == 'Y')) {
        env.note(env.ctx, "ESP32 not updated. Nothing was written.", HCT_MUTED);
        return finish(false);
    }
    link = esp_flash_board_link();
    for (;;) {
        state = HCJ_RUNNING;
        can_cancel = false;
        memset(&u, 0, sizeof(u));
        u.env = &env;
        u.last_stage = -1;
        snprintf(u.title, sizeof(u.title), "Updating the ESP32");
        t0 = env.now(env.ctx);
        esp_flash(*link, *srcp, ei, esp_up_cb, &u, &fr);
        flog(env, "[host] ESP32 update: %s after %u ms, stage %d, started %d, ident %d V%u.%u: %s", fr.ok ? "ok" : "FAILED", (unsigned)(env.now(env.ctx) - t0), fr.stage,
             (int)fr.started, fr.ident, (unsigned)fr.major, (unsigned)fr.minor, fr.why ? fr.why : "");
        n_esp_writes++;
        state = HCJ_RESULT;
        esp_up_result_page(env, pg, again, ei, fr);
        draw(env, pg);
        if (!again) {
            HJ_WAIT_KEY(k);
            if (fr.ok) {
                env.note(env.ctx, fr.ident == EFI_NO_ANSWER ? "ESP32 written. It did not report its version yet." : "ESP32 updated.",
                         fr.ident == EFI_NO_ANSWER ? HCT_WARN : HCT_OK);
            }
            return finish(fr.ok);
        }
        for (;;) {
            hook(env, -1, 0, 0);                               // a page is up and waits for a key
            k = env.getch(env.ctx);
            if (k == KEY_RETURN) {
                break;                                         // write again
            }
            if (k == KEY_ESCAPE || k == KEY_BREAK || k == KEY_LEFT) {
                if (fr.ok) {
                    env.note(env.ctx, fr.ident == EFI_NO_ANSWER ? "ESP32 written. It did not report its version yet." : "ESP32 updated.",
                             fr.ident == EFI_NO_ANSWER ? HCT_WARN : HCT_OK);
                }
                return finish(fr.ok);
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

HcJob *hc_inspect_job(const HcEspEnv &env, const char *path) { return new InspectJob(env, path); }

void hc_inspect_run(const HcEspEnv &env, const char *path) { hc_job_run(new InspectJob(env, path)); }
