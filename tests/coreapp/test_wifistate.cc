// T184 R6: one name per state. The word the core application prints for the WiFi state (the summary line of F8 and the state value) comes from
// wifi_state_word() and net_chip_text(): Off / No module for the disabled and missing module, the driver's own words for the link states.
#include "check.h"
#include "wifinet.h"
#include <stdio.h>
#include <string.h>

static WifiNetStatus mk(WifiNetState st, const char *text)
{
    WifiNetStatus w;
    memset(&w, 0, sizeof(w));
    w.state = st;
    w.text = text;
    return w;
}

int main()
{
    WifiNetStatus nm = mk(WN_NO_MODULE, "No module detected"), dis = mk(WN_DISABLED, "Disabled"), off = mk(WN_OFF, "Off"), up = mk(WN_LINK_UP, "Link up");
    CHECK_STR(wifi_state_word(nm), "No module");
    CHECK_STR(wifi_state_word(dis), "Off");
    CHECK_STR(wifi_state_word(off), "Off");
    CHECK_STR(wifi_state_word(up), "Link up");
    // the summary line as netops.cc st_wifi_line builds it
    char l[96];
    snprintf(l, sizeof(l), "%s, %s, %s", wifi_state_word(dis), "no network", "no address");
    CHECK(strstr(l, "Off") == l);
    CHECK(strstr(l, "Disabled") == NULL);
    snprintf(l, sizeof(l), "%s, %s, %s", wifi_state_word(nm), "no network", "no address");
    CHECK(strstr(l, "No module,") == l);
    CHECK(strstr(l, "detected") == NULL);
    // T184 D5: the network chip (host header and the core's Network line) names the link; both up: LAN; none: the caller's text
    char c[48];
    CHECK(net_chip_text(c, sizeof(c), "192.0.2.189", "", "no link") && !strcmp(c, "LAN 192.0.2.189"));
    CHECK(net_chip_text(c, sizeof(c), "", "192.0.2.44", "no link") && !strcmp(c, "WiFi 192.0.2.44"));
    CHECK(net_chip_text(c, sizeof(c), "192.0.2.189", "192.0.2.44", "no link") && !strcmp(c, "LAN 192.0.2.189"));
    CHECK(!net_chip_text(c, sizeof(c), "", "", "no link") && !strcmp(c, "no link"));
    CHECK(!net_chip_text(c, sizeof(c), "0.0.0.0", "", "no network") && !strcmp(c, "no network"));
    CHECK(net_chip_text(c, sizeof(c), "0.0.0.0", "10.0.0.7", "no link") && !strcmp(c, "WiFi 10.0.0.7"));
    CHECK(!net_chip_text(c, sizeof(c), 0, 0, "no link") && !strcmp(c, "no link"));
    CHECK(!strcmp(NET_NONE_TEXT, "no network"));                          // one word for host and core
    // the longest text fits the core top bar's cut (ui.cc chip_net[24]); the old cut at 14 turned .189 into .18
    CHECK(net_chip_text(c, sizeof(c), "", "255.255.255.255", "") && strlen(c) < 24);
    DONE("wifistate");
}
