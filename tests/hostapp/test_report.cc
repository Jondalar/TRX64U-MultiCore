// T162: the host app's bug report. S1 the file (sections, the name, the folder, no overwrite), S2 the REST route and its headers, S3 no secrets,
// D2 the log limits. Every check has a control in run.sh: the product source is broken on purpose and this test must go red.
#include "hostreport.h"
#include "hostreport_live.h"
#include "bugreport.h"
#include "webui.h"
#include "videomode.h"
#include "hostfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <map>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// ---- the board the report reads, as a model ----------------------------------------------------------------------------------------
static std::string g_logtext;
static bool g_clock_valid = true;
static uint32_t g_fat;
static std::map<std::string, std::string> g_files;     // "/SD/report/20261030-140510.txt" -> text
static int g_mkdirs;
static bool g_dir_ok = true, g_save_ok = true;
static bool g_usb_ok[4] = { true, true, true, true };     // which /Usb<n> take a folder

extern "C" uint32_t get_fattime(void) { return g_fat; }
extern "C" bool rtc_time_valid(void) { return g_clock_valid; }
const char *hostreport_board(void) { return "C64 Ultimate (U64 Elite II)"; }
int hostlog_tail(char *out, int max)
{
    int n = (int)g_logtext.size() < max ? (int)g_logtext.size() : max;
    memcpy(out, g_logtext.data() + g_logtext.size() - n, n);
    return n;
}
struct ModelFo : FoFs {
    bool stat(const char *, FoStat *) { return false; }
    int dir_entries(const char *) { return 0; }
    bool free_bytes(const char *, uint64_t *) { return false; }
    bool same_volume(const char *, const char *) { return true; }
    void *open_read(const char *) { return 0; }
    void *open_write(const char *) { return 0; }
    int read(void *, void *, uint32_t) { return -1; }
    bool write(void *, const void *, uint32_t) { return false; }
    bool close(void *) { return false; }
    bool remove(const char *) { return false; }
    bool rename(const char *, const char *) { return false; }
    bool make_dir(const char *p)
    {
        g_mkdirs++;
        if (!strncmp(p, "/Usb", 4)) {
            return p[4] >= '0' && p[4] <= '3' && g_usb_ok[p[4] - '0'];
        }
        return g_dir_ok;
    }
};
FoFs *hostfs_fo(void) { static ModelFo fo; return &fo; }
bool hostfs_exists(const char *path) { return g_files.count(path) != 0; }
bool hostfs_save(const char *dir, const char *name, const void *buf, int len)
{
    if (!g_save_ok) {
        return false;
    }
    g_files[std::string(dir) + "/" + name] = std::string((const char *)buf, len);
    return true;
}

static uint32_t fat(int y, int mo, int d, int h, int mi, int s)
{
    return ((uint32_t)(y - 1980) << 25) | ((uint32_t)mo << 21) | ((uint32_t)d << 16) | ((uint32_t)h << 11) | ((uint32_t)mi << 5) | (uint32_t)(s / 2);
}

static WebSnapshot snap(void)
{
    WebSnapshot s;
    memset(&s, 0, sizeof(s));
    snprintf(s.app, sizeof(s.app), "6a20094");
    snprintf(s.app_date, sizeof(s.app_date), "2026-10-30");
    snprintf(s.board, sizeof(s.board), "C64 Ultimate");
    snprintf(s.host_key, sizeof(s.host_key), "0e8fb06e");
    snprintf(s.ip, sizeof(s.ip), "192.168.2.44");
    s.net = true; s.usb = true; s.sd = true;
    snprintf(s.media, sizeof(s.media), "Usb1, Sd");
    s.vid_sys = VID_SYS_PAL50;
    s.vid_hdmi = VID_HDMI_1080P;
    s.countdown = 5; s.autostart_s = 5; s.direct = 1; s.def = 0;
    snprintf(s.cfgtext, sizeof(s.cfgtext), "countdown=5\nautostart_s=5\ndirect=1\ndefault=0\nvideo=0,2\n");
    s.n = 2;
    snprintf(s.name[0], WEB_NAME_MAX, "TRX64 Ultimate");
    snprintf(s.path[0], WEB_PATH_MAX, "/SD/cores/trx64.cor");
    snprintf(s.state[0], 16, "ok");
    s.size[0] = 1234567;
    snprintf(s.hash[0], sizeof(s.hash[0]), "ab12cd34ef56ab12cd34ef56ab12cd34ef56ab12cd34ef56ab12cd34ef56ab12");
    snprintf(s.name[1], WEB_NAME_MAX, "Atari 800 XL");
    snprintf(s.path[1], WEB_PATH_MAX, "/SD/cores/atari800.cor");
    snprintf(s.state[1], 16, "missing");
    return s;
}

static std::string build(const WebSnapshot &s, const std::string &log, uint32_t ft = 0, bool valid = true)
{
    static char out[HOST_REPORT_MAX];
    int n = host_report_build(&s, ft ? ft : fat(2026, 10, 30, 14, 5, 10), valid, log.data(), (int)log.size(), out, sizeof(out));
    CHECK(n == (int)strlen(out));
    return std::string(out, n);
}

static bool has(const std::string &t, const char *s) { return t.find(s) != std::string::npos; }
static int count_lines(const std::string &t) { int n = 0; for (char c : t) n += c == '\n'; return n; }

int main()
{
    // ---- S1: the sections and what is in them ----
    {
        WebSnapshot s = snap();
        std::string t = build(s, "boot line 1\nboot line 2\n");
        CHECK(has(t, "== Versions =="));
        CHECK(has(t, "Host app: 6a20094 built 2026-10-30"));
        CHECK(has(t, "Host bitstream: 0e8fb06e"));
        CHECK(has(t, "Board: C64 Ultimate"));
        CHECK(has(t, "Date: 2026-10-30 14:05:10"));
        CHECK(has(t, "== Settings =="));
        CHECK(has(t, "countdown=5") && has(t, "video=0,2"));
        CHECK(has(t, "== Boot menu"));
        CHECK(has(t, "0 (default) TRX64 Ultimate | /SD/cores/trx64.cor | ok"));
        CHECK(has(t, "size 1234567, sha256 ab12cd34ef56"));
        CHECK(has(t, "Atari 800 XL") && has(t, "size and sha256 not recorded"));
        CHECK(has(t, "== Log"));
        CHECK(has(t, "boot line 1\nboot line 2\n"));
        CHECK(has(t, "Picture: PAL 50 Hz, 1080p") || has(t, "Picture: PAL"));
        // a clock never set says so, and the name is all zeros
        std::string u = build(s, "", 0, false);
        CHECK(has(u, "Date: clock not set"));
        char st[BR_STAMP_LEN];
        br_stamp(st, 0, false);
        CHECK(!strcmp(st, "nodate"));
        br_stamp(st, fat(2026, 10, 30, 14, 5, 10), true);
        CHECK(!strcmp(st, "20261030-140510"));
        // a report that does not fit ends with the marker instead of running over
        char small[600];
        int n = host_report_build(&s, fat(2026, 10, 30, 14, 5, 10), true, "x\n", 2, small, sizeof(small));
        CHECK(n < (int)sizeof(small) && has(std::string(small, n), "[report cut"));
    }
    // ---- S1: the file on the SD card ----
    {
        WebSnapshot s = snap();
        g_files.clear();
        g_mkdirs = 0;
        g_logtext = "line a\nline b\n";
        g_clock_valid = true;
        g_fat = fat(2026, 10, 30, 14, 5, 10);
        char name[40], why[64];
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)));
        CHECK(!strcmp(name, "20261030-140510.txt"));
        CHECK(g_mkdirs == 1);
        CHECK(g_files.count("/SD/report/20261030-140510.txt") == 1);
        CHECK(has(g_files["/SD/report/20261030-140510.txt"], "== Versions ==") && has(g_files["/SD/report/20261030-140510.txt"], "line b"));
        // the same second again: the first file stays, the second gets a number
        std::string first = g_files["/SD/report/20261030-140510.txt"];
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)));
        CHECK(!strcmp(name, "20261030-140510-1.txt"));
        CHECK(g_files["/SD/report/20261030-140510.txt"] == first && g_files.size() == 2);
        // no SD card, a folder that cannot be made, a card that does not take the file: false, a reason, no file
        g_files.clear();
        s.sd = false; s.usb = false;
        CHECK(!hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && strstr(why, "SD") && g_files.empty());
        s.sd = true; s.usb = true;
        g_dir_ok = false; s.usb = false;
        CHECK(!hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && strstr(why, "takes a folder") && g_files.empty());
        // an SD card that is listed but takes no folder: the stick gets the report (the emulator lists an SD slot with no card)
        s.usb = true;
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && g_files.count("/Usb0/report/20261030-140510.txt") == 1);
        g_files.clear();
        g_dir_ok = true;
        g_save_ok = false;
        CHECK(!hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && strstr(why, "did not take") && g_files.empty());
        g_save_ok = true;
        // the busy flag is released after a failed write too: the next one works
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)));
        // a clock never set still writes a file: nodate-<n>.txt with the first free n
        g_files.clear();
        g_clock_valid = false;
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && !strcmp(name, "nodate-1.txt"));
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && !strcmp(name, "nodate-2.txt"));
        g_files.erase("/SD/report/nodate-1.txt");
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && !strcmp(name, "nodate-1.txt") && g_files.size() == 2);
        g_clock_valid = true;
        // D5: no SD card -> the first USB medium that takes a folder; neither -> the message, nothing written
        g_files.clear();
        s.sd = false;
        g_usb_ok[0] = false;
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && !strcmp(name, "20261030-140510.txt"));
        CHECK(g_files.size() == 1 && g_files.count("/Usb1/report/20261030-140510.txt") == 1 && !strcmp(hostreport_dir(), "/Usb1/report"));
        g_files.clear();
        g_usb_ok[0] = true;
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && g_files.count("/Usb0/report/20261030-140510.txt") == 1);
        g_files.clear();
        s.usb = false;
        CHECK(!hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && !strcmp(why, "no SD card or USB stick") && g_files.empty());
        s.usb = true;
        for (int u = 0; u < 4; u++) g_usb_ok[u] = false;
        CHECK(!hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && g_files.empty());
        for (int u = 0; u < 4; u++) g_usb_ok[u] = true;
        s.sd = true;
        g_files.clear();
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)) && g_files.count("/SD/report/20261030-140510.txt") == 1);   // the SD card wins over a stick
    }
    // ---- S2: REST ----
    {
        CHECK(web_get_route("/v1/report") == WEBGET_REPORT);
        CHECK(web_get_route("/v1/report?x=1") == WEBGET_REPORT);
        CHECK(web_get_route("/v1/status") == WEBGET_STATUS);
        CHECK(web_get_route("/v1/reports") == WEBGET_NONE);
        CHECK(web_get_route("/") == WEBGET_NONE);
        char h[300];
        int n = web_report_head(fat(2026, 10, 30, 14, 5, 10), true, h, sizeof(h));
        CHECK(n > 0 && has(h, "Content-Type: text/plain"));
        CHECK(has(h, "Content-Disposition: attachment; filename=\"20261030-140510.txt\""));
        // the answer is the text of the file: the live path (the same builder, the same snapshot, the same clock and log)
        WebSnapshot s = snap();
        g_logtext = "same\n";
        g_fat = fat(2026, 10, 30, 14, 5, 10);
        int len = 0;
        const char *text = hostreport_make(&s, &len);
        CHECK(text && len > 0);
        std::string rest(text ? text : "", len);
        CHECK(hostreport_make(&s, &len) == 0);              // a second maker while the first holds the buffer is refused
        hostreport_release();
        char name[40], why[64];
        g_files.clear();
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)));
        CHECK(g_files["/SD/report/20261030-140510.txt"] == rest);
    }
    // ---- D2: the log limits ----
    {
        WebSnapshot s = snap();
        std::string log;
        for (int i = 1; i <= 350; i++) {
            char l[40];
            snprintf(l, sizeof(l), "log %03d\n", i);
            log += l;
        }
        std::string t = build(s, log);
        CHECK(has(t, "log 350\n") && has(t, "log 151\n") && !has(t, "log 150\n"));          // the last 200 lines
        // 16 KB wins when the lines are long: 100 lines of 400 bytes are 40 KB; the report holds at most 16 KB of them
        std::string longlog;
        for (int i = 0; i < 100; i++) {
            char l[420];
            snprintf(l, sizeof(l), "L%03d ", i);
            std::string line = l;
            line.append(380, 'x');
            longlog += line + "\n";
        }
        std::string u = build(s, longlog);
        size_t at = u.find("== Log");
        CHECK(at != std::string::npos);
        std::string tail = u.substr(at);
        CHECK(tail.size() <= (size_t)BR_LOG_BYTES + 128);
        CHECK(has(u, "L099 ") && !has(u, "L000 "));
        // a line cut in two by the byte limit is not shown as a whole line
        for (int i = 0; i < 100; i++) {
            char marker[8];
            snprintf(marker, sizeof(marker), "L%03d ", i);
            size_t p = u.find(marker);
            if (p != std::string::npos) {
                CHECK(u.find('\n', p) - p == 385);                               // every line that is shown is shown whole
            }
        }
        CHECK(count_lines(tail) < 200);
        // an empty log says so
        CHECK(has(build(s, ""), "(the log is empty)"));
    }
    // ---- S3: no secrets (P4) ----
    {
        WebSnapshot s = snap();
        snprintf(s.cfgtext, sizeof(s.cfgtext),
                 "countdown=5\nftp_server_password=hunter2-ftp\nnetwork_key=wlan-secret-77\nwifi_psk=psk-value-9\nftp_url=ftp://bob:topsecret@10.0.0.7/games\nKeyboard Layout=German\n");
        std::string log = "net up\nconnecting with password=hunter2-log\nftp://alice:logsecret@nas/x\nPSK is abc\nplain line stays\n";
        std::string t = build(s, log);
        CHECK(!has(t, "hunter2-ftp") && !has(t, "wlan-secret-77") && !has(t, "psk-value-9"));
        CHECK(!has(t, "topsecret") && !has(t, "hunter2-log") && !has(t, "logsecret"));
        CHECK(has(t, "ftp_server_password=<withheld>") && has(t, "network_key=<withheld>"));
        CHECK(has(t, "ftp://***@10.0.0.7/games"));
        CHECK(has(t, "[line withheld"));
        CHECK(has(t, "plain line stays") && has(t, "Keyboard Layout=German") && has(t, "countdown=5"));     // what is no secret is kept
        // the same through the live path and the file
        g_logtext = log;
        g_files.clear();
        char name[40], why[64];
        CHECK(hostreport_write(&s, name, sizeof(name), why, sizeof(why)));
        for (auto &f : g_files) {
            CHECK(!has(f.second, "hunter2-ftp") && !has(f.second, "wlan-secret-77") && !has(f.second, "hunter2-log"));
        }
        CHECK(br_secret_name("Network Password") && br_secret_name("wifi_key") && br_secret_name("apikey") && br_secret_name("PSK"));
        CHECK(!br_secret_name("Keyboard Layout") && !br_secret_name("Key Repeat") && !br_secret_name("countdown"));
    }
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
