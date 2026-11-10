// The host application's page and its JSON (T106): what they show, and what a form does.
// The control the routes exist for: a route that ignores the form body must fail S2.
#include "webui.h"
#include "videomode.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <strings.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static WebSnapshot snap(void)
{
    WebSnapshot s;
    memset(&s, 0, sizeof(s));
    snprintf(s.app, sizeof(s.app), "6a20094");
    snprintf(s.host_key, sizeof(s.host_key), "0e8fb06e");
    snprintf(s.ip, sizeof(s.ip), "192.168.2.44");
    s.net = true; s.usb = true; s.sd = true;
    snprintf(s.media, sizeof(s.media), "Usb1, Sd");
    s.vid_sys = VID_SYS_PAL50;
    s.vid_hdmi = VID_HDMI_1080P;
    s.vid_mask = 0x0007;                    // T164: the screen names 576p, 720p and 1080p at 50 Hz
    s.vid_def = -1;
    s.countdown = 5;
    s.def = 0;
    s.n = 2;
    snprintf(s.name[0], WEB_NAME_MAX, "TRX64 Ultimate <C64>");
    snprintf(s.path[0], WEB_PATH_MAX, "/Usb1/cores/trx64.cor");
    snprintf(s.state[0], 16, "ok");
    s.startable[0] = true;
    snprintf(s.name[1], WEB_NAME_MAX, "Atari 800 XL");
    snprintf(s.path[1], WEB_PATH_MAX, "/Usb1/cores/atari800.cor");
    snprintf(s.state[1], 16, "missing");
    s.startable[1] = false;
    return s;
}

static char buf[16384];

// T111 S3: the string literals of a source file that look like a route (start with "/"), in order.
static std::vector<std::string> routes_in(const char *file)
{
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string t = ss.str(), cur;
    std::vector<std::string> out;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '"') {
            size_t j = i + 1;
            cur.clear();
            while (j < t.size() && t[j] != '"') {
                if (t[j] == '\\') j++;
                cur += t[j++];
            }
            if (!cur.empty() && cur[0] == '/' && cur.size() > 1 && cur.find("%") == std::string::npos && cur.find(' ') == std::string::npos && cur.find('<') == std::string::npos) {
                out.push_back(cur);
            }
            i = j;
        } else if (t[i] == '/' && i + 1 < t.size() && t[i + 1] == '/') {
            while (i < t.size() && t[i] != '\n') i++;                // a comment is not a route
        }
    }
    return out;
}

static bool names_a_format(const std::string &r)
{
    static const char *w[] = { "format", "fmt", "mkfs", "media", "erase", "wipe", "disk", "partition", "exfat" };
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
        for (size_t k = 0; k + strlen(w[i]) <= r.size(); k++) {
            if (!strncasecmp(r.c_str() + k, w[i], strlen(w[i]))) return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    WebSnapshot s = snap();

    // --- S1 the page shows what the menu shows
    int n = web_page(&s, "", buf, sizeof(buf));
    CHECK(n > 0 && n < (int)sizeof(buf));
    CHECK(strstr(buf, "Atari 800 XL") != 0);
    CHECK(strstr(buf, "/Usb1/cores/trx64.cor") != 0);
    CHECK(strstr(buf, "Found on the media") == 0 && strstr(buf, "/v1/menu/add") == 0);   // T101 S10: nothing is searched for, the page offers no add
    CHECK(strstr(buf, "Usb1, Sd") != 0);                               // the media
    CHECK(strstr(buf, video_hdmi_name(VID_HDMI_1080P)) != 0);          // the picture
    CHECK(strstr(buf, video_hdmi_name(VID_HDMI_720P)) != 0);
    CHECK(strstr(buf, video_hdmi_name(VID_HDMI_1280x1024)) == 0 && strstr(buf, video_hdmi_name(VID_HDMI_800x600)) == 0);   // S10: only what the screen names
    CHECK(strstr(buf, "name=\"sys\"") == 0);                           // Q6: no Hz choice
    CHECK(strstr(buf, "192.168.2.44") != 0);
    CHECK(strstr(buf, "TRX64 Ultimate &lt;C64&gt;") != 0);             // a name is the user's text, escaped
    CHECK(strstr(buf, "<C64>") == 0);
    // only a startable entry offers a start
    const char *a = strstr(buf, "Atari 800 XL");
    CHECK(a && strstr(a, "value=\"1\"") != 0 && strstr(a, ">start<") == 0);
    CHECK(strstr(buf, "/v1/boot") != 0);

    // a page that does not fit is cut where it ran out, and nothing is written past what it was given
    char tiny[232];
    memset(tiny, 0x7F, sizeof(tiny));
    int t = web_page(&s, "", tiny, 200);
    CHECK(t >= 0 && t < 200 && tiny[t] == 0);
    for (int i = 200; i < (int)sizeof(tiny); i++) {
        CHECK((unsigned char)tiny[i] == 0x7F);
    }

    // --- S4 the JSON names the same facts
    n = web_json(&s, "hello", buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(buf[0] == '{' && buf[n - 1] == '}');
    CHECK(strstr(buf, "\"host_key\":\"0e8fb06e\"") != 0);
    CHECK(strstr(buf, "\"network\":true") != 0);
    CHECK(strstr(buf, "\"hdmi\":2") != 0);
    CHECK(strstr(buf, "\"offered\":[0,1,2]") != 0 && strstr(buf, "\"default\":-1") != 0);
    CHECK(strstr(buf, "\"startable\":true") != 0 && strstr(buf, "\"startable\":false") != 0);
    CHECK(strstr(buf, "\"state\":\"missing\"") != 0);
    CHECK(strstr(buf, "\"note\":\"hello\"") != 0);
    CHECK(strstr(buf, "\"found\"") == 0);
    {   // a quote in a name must not break the document
        WebSnapshot q = s;
        snprintf(q.name[0], WEB_NAME_MAX, "a\"b\\c");
        n = web_json(&q, "", buf, sizeof(buf));
        CHECK(strstr(buf, "a\\\"b\\\\c") != 0);
    }

    // --- S2 the picture, from a form
    WebCmd c;
    int status = 0;
    CHECK(web_parse("/v1/video", "hdmi=2", &c, &s, &status));
    CHECK(status == 202 && c.kind == WEB_VIDEO && c.b == 2);
    CHECK(web_parse("/v1/video", "hdmi=-1", &c, &s, &status) && c.b == -1);   // from the screen
    CHECK(!web_parse("/v1/video", "sys=1", &c, &s, &status) && status == 400);
    CHECK(!web_parse("/v1/video", "hdmi=99", &c, &s, &status) && status == 400);
    CHECK(web_parse("/v1/video?hdmi=1", "", &c, &s, &status));           // a query string serves too
    CHECK(c.b == 1);

    // --- S3 starting an entry, and refusing one that cannot start
    CHECK(web_parse("/v1/boot", "index=0", &c, &s, &status));
    CHECK(status == 202 && c.kind == WEB_BOOT && c.a == 0);
    CHECK(!web_parse("/v1/boot", "index=1", &c, &s, &status));
    CHECK(status == 409);                                                // in the menu, not startable
    CHECK(!web_parse("/v1/boot", "index=7", &c, &s, &status) && status == 400);
    CHECK(!web_parse("/v1/boot", "", &c, &s, &status) && status == 400);

    // --- the rest of the routes
    CHECK(web_parse("/v1/menu/remove", "index=1", &c, &s, &status) && c.kind == WEB_REMOVE && c.a == 1);
    CHECK(web_parse("/v1/default", "index=1", &c, &s, &status) && c.kind == WEB_DEFAULT);
    CHECK(web_parse("/v1/countdown", "seconds=0", &c, &s, &status) && c.kind == WEB_COUNTDOWN && c.a == 0);
    CHECK(!web_parse("/v1/countdown", "seconds=61", &c, &s, &status) && status == 400);
    CHECK(!web_parse("/v1/menu/add", "path=%2FUsb1%2Fcores%2Fnes.cor", &c, &s, &status) && status == 404);   // adding is done on the board (F3)
    CHECK(!web_parse("/v1/nothing", "x=1", &c, &s, &status) && status == 404);
    CHECK(!web_parse("/", "", &c, &s, &status) && status == 404);

    // --- the mailbox is the whole interface (S6): one command, taken once
    WebCmd got;
    CHECK(!web_take(&got));
    web_publish(&s);
    CHECK(web_parse("/v1/video", "hdmi=1", &c, &s, &status));

    // --- T111 S3 no remote path to a format. argv[1]: the source of the routes (webui.cc, or a copy with a route added: the control).
    // Every route literal of the source (the dispatch of the board half is in it too: GET /v1/status, POST to web_parse, the rest 404/405) names
    // none of format, fmt, mkfs, media, erase, wipe, disk, partition; the commands the UI task can be handed are exactly the five of the picture
    // and the menu; and nothing of the format is declared in the web header.
    if (argc > 1) {
        std::vector<std::string> rt = routes_in(argv[1]);
        if (argc > 3) {                                                         // the routes of the transport half (webnet.cc) are routes of the web UI too
            std::vector<std::string> more = routes_in(argv[3]);
            rt.insert(rt.end(), more.begin(), more.end());
        }
        bool bad = false;
        for (size_t i = 0; i < rt.size(); i++) {
            bad = bad || names_a_format(rt[i]);
            if (names_a_format(rt[i])) printf("route %s names a format\n", rt[i].c_str());
        }
        CHECK(rt.size() >= 6 && !bad);                                         // /v1/status, /v1/, video, boot, menu/remove, default, countdown
        CHECK(WEB_NONE == 0 && WEB_VIDEO == 1 && WEB_BOOT == 2 && WEB_REMOVE == 3 && WEB_COUNTDOWN == 4 && WEB_DEFAULT == 5);   // no sixth command
        // whatever is asked on the route table's behalf, the answer is one of those commands or no command at all
        const char *uris[] = { "/v1/format", "/v1/media", "/v1/media/format", "/v1/fmt", "/v1/format?medium=Usb0", "/v1/disk/erase", "/format",
                               "/v1/boot/../format", "/v1/exfat", "/v1/mkfs", "/v1/video/format", "/v1/media?name=Usb0&confirm=1" };
        const char *bodies[] = { "", "medium=Usb0", "name=Usb0&confirm=1", "index=0", "seconds=5", "hdmi=1" };
        for (unsigned a = 0; a < sizeof(uris) / sizeof(uris[0]); a++) {
            for (unsigned b = 0; b < sizeof(bodies) / sizeof(bodies[0]); b++) {
                WebCmd cc;
                int st = 0;
                bool ok = web_parse(uris[a], bodies[b], &cc, &s, &st);
                CHECK(!ok && cc.kind == WEB_NONE && st == 404);
            }
        }
        std::ifstream hh(argc > 2 ? argv[2] : "webui.h");
        std::stringstream hs;
        hs << hh.rdbuf();
        std::string h = hs.str();
        CHECK(h.size() > 100 && h.find("fmt") == std::string::npos && h.find("format") == std::string::npos && h.find("mkfs") == std::string::npos && h.find("exfat") == std::string::npos);   // (the snapshot lists the media by name since T106; that is not the format view)
    }

    printf("test_webui: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
