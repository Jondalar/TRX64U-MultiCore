// The Telnet remote menu service (telnetgui.h). The text screen and the key decoder are telnetscr.cc.
#include "telnetgui.h"
#include "telnetscr.h"
#include "uiapp.h"
#include "picture.h"
#include "network_config.h"
#include "socket_keepalive.h"
#include "lwip/sockets.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define TELNET_PORT 23                    // socket_gui.cc:252
#define BIND_RETRY_MS 2000                // :235, :186 (the firmware waits 2 s between tries)
#define ATTEMPTS 5                        // :95
#define DELAY_FIRST_MS 250                // :96, doubled after each failure (0.25 -> 4 s)
#define PW_MAX 31                         // :84 (buf[32 + 1], 32 - 1 characters)
#define FRAME_MS 150                      // the screen is composed this often (and at once after a key)
#define OPEN_WAIT_MS 2000                 // after the menu was asked to open, the notice that it is closed waits this long
#define OWN_CLOSE_MS 1500                 // a menu that closes this soon after a key of the terminal was closed by it: the session ends (MENU_EXIT, userinterface.cc:532)

namespace {

enum State { ST_NONE = 0, ST_PASSWORD, ST_THROTTLE, ST_UI };

int s_ls = -1;                            // the listener
int s_cl = -1;                            // the client
State s_state;
char s_pw[PW_MAX + 1];
int s_pos, s_attempts, s_skip;
uint32_t s_delay, s_until, s_try;
bool s_enabled_seen;
TelnetKeys s_keys;
TelnetScreen s_scr;
char s_out[3072];
bool s_prev_open;
uint32_t s_last_key, s_last_frame, s_open_asked;

uint32_t now_ms(void) { return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; }

void nonblocking(int s)
{
    int flags = lwip_fcntl(s, F_GETFL, 0);
    lwip_fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

// The bytes go out within a short time or the client is dropped (the firmware bounds a send to 5 s with a task of its own to wait in; this one is the menu's).
bool send_all(const char *p, int len)
{
    int done = 0, idle = 0;
    while (done < len) {
        int n = lwip_send(s_cl, p + done, len - done, MSG_DONTWAIT);
        if (n > 0) {
            done += n;
            idle = 0;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && idle < 200) {
            vTaskDelay(1);
            idle++;
            continue;
        }
        return false;
    }
    return true;
}

void drop(const char *why)
{
    if (s_cl >= 0) {
        lwip_shutdown(s_cl, 2);
        lwip_close(s_cl);
        s_cl = -1;
    }
    s_state = ST_NONE;
    printf("[telnet] %s\n", why);
}

bool enabled(void)
{
    return networkConfig.cfg && networkConfig.cfg->get_value(CFG_NETWORK_TELNET_SERVICE) != 0;
}

void listen_start(uint32_t now)
{
    if (s_ls >= 0 || (s_try && now - s_try < BIND_RETRY_MS)) {
        return;
    }
    s_try = now | 1;
    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return;
    }
    int one = 1;
    lwip_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(TELNET_PORT);
    if (lwip_bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || lwip_listen(s, 2) < 0) {
        lwip_close(s);
        return;                                                   // tried again after BIND_RETRY_MS
    }
    nonblocking(s);
    s_ls = s;
    printf("[telnet] serving the menu on TCP %d\n", TELNET_PORT);
}

void listen_stop(void)
{
    if (s_ls >= 0) {                                              // a session that is up stays, as the firmware's does (:244-251)
        lwip_close(s_ls);
        s_ls = -1;
        s_try = 0;
    }
}

void ask_password(void)
{
    static const char p[] = "Password: \xff\xfb\x01";           // the prompt, IAC WILL ECHO (:107-108)
    s_pos = 0;
    s_skip = 0;
    if (!send_all(p, (int)sizeof(p) - 1)) {
        drop("the client does not read");
        return;
    }
    s_state = ST_PASSWORD;
}

void start_ui(const TelnetEnv &e, uint32_t now)
{
    s_keys.reset();
    s_scr.reset();
    s_state = ST_UI;
    s_prev_open = e.menu_open;
    s_last_key = 0;
    s_last_frame = 0;
    s_open_asked = 0;
    char g[64];
    int n = TelnetScreen::greeting(g, sizeof(g));
    if (!send_all(g, n)) {
        drop("the client does not read");
        return;
    }
    if (!e.menu_open) {
        uiapp_local_open();                                       // the remote menu shows the menu: it comes up on the machine as well (one MenuCtl)
        s_open_asked = now | 1;
    }
    printf("[telnet] logged in\n");
}

void accept_one(const TelnetEnv &e, uint32_t now)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    int c = lwip_accept(s_ls, (struct sockaddr *)&a, &al);
    if (c < 0) {
        return;
    }
    nonblocking(c);
    net_enable_client_keepalive(c);                               // reaps a half-open session (socket_gui.cc:62)
    if (s_cl >= 0) {                                              // one client: the menu is one (:262-269)
        static const char busy[] = "Too many connections, try again later.\r\n";
        lwip_send(c, busy, (int)sizeof(busy) - 1, MSG_DONTWAIT);
        lwip_shutdown(c, 2);
        lwip_close(c);
        return;
    }
    s_cl = c;
    s_attempts = ATTEMPTS;
    s_delay = DELAY_FIRST_MS;
    const char *pw = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);
    if (!*pw) {                                                   // empty: all good (:90-91)
        start_ui(e, now);
    } else {
        ask_password();
    }
}

// A line of the password was typed: the check of socket_gui.cc:119-134.
void password_line(const TelnetEnv &e, uint32_t now)
{
    s_pw[s_pos] = 0;
    if (!send_all("\r\n", 2)) {
        drop("the client does not read");
        return;
    }
    const char *pw = networkConfig.cfg->get_string(CFG_NETWORK_PASSWORD);   // read again at the check, as the firmware does
    if (!*pw || !strcmp(s_pw, pw)) {
        printf("[telnet] connection logged in\n");
        if (!send_all("\xff\xfc\x01\r\n", 5)) {                 // IAC WONT ECHO, then the line (:140-141)
            drop("the client does not read");
            return;
        }
        start_ui(e, now);
        return;
    }
    printf("[telnet] invalid password\n");
    s_until = now + s_delay;                                      // throttled: the answer comes after the delay, what was typed meanwhile is dropped
    s_delay <<= 1;
    s_attempts--;
    s_state = ST_THROTTLE;
}

void read_password(const TelnetEnv &e, uint32_t now)
{
    uint8_t b[64];
    int n = lwip_recv(s_cl, b, sizeof(b), MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        drop("closed before successful authentication");
        return;
    }
    for (int i = 0; i < n && s_state == ST_PASSWORD; i++) {
        int ch = b[i];
        if (s_skip > 0) {                                         // a telnet command: its two bytes are ignored (:67-81)
            s_skip--;
        } else if (ch == 0xFF) {
            s_skip = 2;
        } else if (ch == '\r') {
            continue;
        } else if (ch == 0 || ch == '\n') {
            password_line(e, now);
        } else if (ch == '\b' || ch == 0x7F || ch == 0x7E) {
            if (s_pos > 0) {
                s_pos--;
                if (!send_all("\b \b", 3)) {
                    drop("the client does not read");
                    return;
                }
            }
        } else if (s_pos < PW_MAX) {
            s_pw[s_pos++] = (char)ch;
            if (!send_all("*", 1)) {
                drop("the client does not read");
                return;
            }
        }
    }
}

void throttle(const TelnetEnv &, uint32_t now)
{
    uint8_t b[64];
    int n = lwip_recv(s_cl, b, sizeof(b), MSG_DONTWAIT);        // input is flushed meanwhile (:130)
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        drop("closed before successful authentication");
        return;
    }
    if ((int32_t)(now - s_until) < 0) {
        return;
    }
    static const char bad[] = "\r\nIncorrect password!\r\n\xff\xfc\x01\r\n";   // :128, then IAC WONT ECHO and the line (:140-141)
    if (!send_all(bad, (int)sizeof(bad) - 1)) {
        drop("the client does not read");
        return;
    }
    if (s_attempts <= 0) {
        drop("closed before successful authentication");
        return;
    }
    ask_password();
}

void end_session(const char *why)
{
    char b[32];
    int n = TelnetScreen::farewell(b, sizeof(b));
    send_all(b, n);
    drop(why);
}

bool serve_ui(const TelnetEnv &e, uint32_t now)
{
    bool took = false;
    uint8_t b[64];
    int n = lwip_recv(s_cl, b, sizeof(b), MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        drop("the client left");
        return false;
    }
    for (int i = 0; i < n; i++) {
        s_keys.put(b[i]);
    }
    if (s_prev_open && !e.menu_open && s_last_key && now - s_last_key < OWN_CLOSE_MS) {
        end_session("the menu was left by the client");
        return took;
    }
    s_prev_open = e.menu_open;
    int k, nk = 0;
    while (nk < 24 && (k = s_keys.get(now)) >= 0) {
        nk++;
        if (!e.menu_open) {                                       // the menu is closed on the machine: a key of the terminal opens it (the key is spent on that)
            uiapp_local_open();
            s_open_asked = now | 1;
        } else {
            uiapp_local_key(k);
            s_last_key = now | 1;
        }
        took = true;
    }
    if (s_open_asked && (e.menu_open || now - s_open_asked >= OPEN_WAIT_MS)) {
        s_open_asked = 0;
    }
    if (took || now - s_last_frame >= FRAME_MS) {
        s_last_frame = now;
        if (!s_open_asked || e.menu_open) {
            TsOver ov;
            ov.dlg = e.dlg;
            char pic[200];
            ov.picture = 0;
            if (picture_confirm_pending()) {
                char a[48], p[48];
                picture_confirm_names(a, sizeof(a), p, sizeof(p));
                snprintf(pic, sizeof(pic), "Keep %s? Back to %s in %d s.", a, p, picture_confirm_left_s());
                ov.picture = pic;
            }
            int len = s_scr.frame(e.menu, e.menu_open, ov, s_out, sizeof(s_out));
            if (len > 0 && !send_all(s_out, len)) {
                drop("the client does not read");
            }
        }
    }
    return took;
}

}  // namespace

bool telnetgui_pass(const TelnetEnv &e)
{
    uint32_t now = now_ms();
    if (!e.offered || !enabled()) {
        listen_stop();
    } else {
        listen_start(now);
    }
    if (s_ls >= 0) {
        accept_one(e, now);
    }
    switch (s_cl < 0 ? ST_NONE : s_state) {
    case ST_PASSWORD: read_password(e, now); break;
    case ST_THROTTLE: throttle(e, now); break;
    case ST_UI: return serve_ui(e, now);
    default: break;
    }
    return false;
}
