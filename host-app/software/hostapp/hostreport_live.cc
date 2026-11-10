#include "hostreport_live.h"
#include "hostreport.h"
#include "bugreport.h"
#include "hostfs.h"
#include <stdio.h>
#include <string.h>

extern "C" uint32_t get_fattime(void);
extern "C" bool rtc_time_valid(void);

// ---- the one work area (the text and the log's tail); the menu and REST run in two tasks, so a second user is told "busy" ------------------
static char g_text[HOST_REPORT_MAX];
static char g_tail[BR_LOG_BYTES];
static volatile bool g_busy;

uint32_t hostreport_fattime(bool *valid)
{
    *valid = rtc_time_valid();
    return *valid ? get_fattime() : 0;
}

const char *hostreport_make(const WebSnapshot *s, int *len)
{
    if (g_busy) {
        return 0;
    }
    g_busy = true;
    bool valid;
    uint32_t ft = hostreport_fattime(&valid);
    int tn = hostlog_tail(g_tail, sizeof(g_tail));
    *len = host_report_build(s, ft, valid, g_tail, tn, g_text, sizeof(g_text));
    return g_text;
}

void hostreport_release(void)
{
    g_busy = false;
}

static char g_dir[24] = HOST_REPORT_DIR;

const char *hostreport_dir(void)
{
    return g_dir;
}

// D5: /SD/report on the SD card, else report/ on the first USB medium that takes a folder. A medium that is listed but takes no folder (an empty slot
// the board still lists, emulator screenshot 2026-10-30) counts as absent.
static bool pick_dir(const WebSnapshot *s, char *why, int whylen)
{
    if (s->sd) {
        snprintf(g_dir, sizeof(g_dir), "%s", HOST_REPORT_DIR);
        if (hostfs_fo()->make_dir(g_dir)) {
            return true;
        }
    }
    if (s->usb) {
        for (int u = 0; u < 4; u++) {
            snprintf(g_dir, sizeof(g_dir), "/Usb%d/report", u);
            if (hostfs_fo()->make_dir(g_dir)) {
                return true;
            }
        }
    }
    snprintf(why, whylen, "no SD card or USB stick%s", s->sd || s->usb ? " takes a folder" : "");
    return false;
}

bool hostreport_write(const WebSnapshot *s, char *name, int namelen, char *why, int whylen)
{
    why[0] = 0;
    int len = 0;
    const char *text = hostreport_make(s, &len);
    if (!text) {
        snprintf(why, whylen, "another report is being made");
        return false;
    }
    bool valid;
    uint32_t ft = hostreport_fattime(&valid);
    char stamp[BR_STAMP_LEN];
    br_stamp(stamp, ft, valid);
    bool ok = false;
    if (pick_dir(s, why, whylen)) {
        // two reports in one second (or with the clock not set) must not replace each other
        for (int k = valid ? 0 : 1; k < 100 && !ok; k++) {
            if (!valid) {
                snprintf(name, namelen, "nodate-%d.txt", k);
            } else if (k) {
                snprintf(name, namelen, "%s-%d.txt", stamp, k);
            } else {
                snprintf(name, namelen, "%s.txt", stamp);
            }
            char path[96];
            snprintf(path, sizeof(path), "%s/%s", g_dir, name);
            if (hostfs_exists(path)) {
                continue;
            }
            ok = hostfs_save(g_dir, name, text, len);
            if (!ok) {
                snprintf(why, whylen, "the medium did not take the file");
                break;
            }
        }
        if (!ok && !why[0]) {
            snprintf(why, whylen, "no free name");
        }
    }
    hostreport_release();
    return ok;
}
