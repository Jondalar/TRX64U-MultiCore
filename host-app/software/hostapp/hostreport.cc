#include "hostreport.h"
#include "bugreport.h"
#include "videomode.h"
#include <stdio.h>
#include <string.h>

int host_report_build(const WebSnapshot *s, uint32_t fattime, bool time_valid, const char *log, int log_len, char *out, int cap, const char *startlog)
{
    BrOut o;
    br_init(&o, out, cap);
    char d[32];
    br_date(d, sizeof(d), fattime, time_valid);
    br_put(&o, "TRX64 bug report (host app)\n");
    br_kv(&o, "Date", "%s", d);

    br_section(&o, "Versions");
    br_kv(&o, "Host app", "%s built %s", s->app, s->app_date);
    br_kv(&o, "Host bitstream", "%s", s->host_key[0] ? s->host_key : "none named");
    br_kv(&o, "Board", "%s", s->board);
    br_kv(&o, "Core", "none running (the host menu is open)");

    br_section(&o, "Network and storage");
    br_kv(&o, "Network", "%s", s->net ? "up" : "no network");
    if (s->net) {
        br_kv(&o, "Address", "%s", s->ip);
    }
    br_kv(&o, "SD card", "%s", s->sd ? "present" : "absent");
    br_kv(&o, "USB", "%s", s->usb ? "present" : "absent");
    br_kv(&o, "Media", "%s", s->media);

    br_section(&o, "Settings");
    br_kv(&o, "Direct boot", "%s", s->direct ? "on" : "off");
    br_kv(&o, "Autostart countdown", "%d s (switched %s)", s->autostart_s, s->countdown > 0 ? "on" : "off");
    br_kv(&o, "Auto entry", "%d", s->def);
    br_kv(&o, "Picture", "%s, %s", video_sys_name(s->vid_sys), s->vid_hdmi < 0 ? "from the screen" : video_hdmi_name(s->vid_hdmi));
    br_kv(&o, "Library attempt under way", "%s", s->lib_attempt ? "yes" : "no");
    br_put(&o, "Stored settings:\n");
    br_text(&o, s->cfgtext, (int)strlen(s->cfgtext));

    br_section(&o, "Boot menu (cores added to the menu; size and SHA-256 as taken when the entry was added)");
    if (!s->n) {
        br_put(&o, "(empty)\n");
    }
    for (int i = 0; i < s->n; i++) {
        br_put(&o, "%d%s %s | %s | %s\n", i, i == s->def ? " (auto)" : "", s->name[i], s->path[i], s->state[i]);
        if (s->hash[i][0]) {
            br_put(&o, "    size %u, sha256 %s\n", s->size[i], s->hash[i]);
        } else {
            br_put(&o, "    size and sha256 not recorded\n");
        }
    }

    br_section(&o, "Start log (startlog.txt on the SD card: the last starts, what the host saw)");
    if (startlog && *startlog) {
        br_text(&o, startlog, (int)strlen(startlog));
    } else {
        br_put(&o, "(no startlog.txt on the SD card)\n");
    }

    br_section(&o, "Log (last lines the host app printed)");
    br_log(&o, log, log_len);
    br_finish(&o);
    return o.n;
}
