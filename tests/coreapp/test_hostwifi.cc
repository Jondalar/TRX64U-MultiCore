// T184 D3: the core application takes the host's "WiFi settings" (Use DHCP, Static IP / Netmask / Gateway / DNS) from /Flash/trx/host.cfg at its start,
// read only. Real ConfigManager / ConfigStore, a store with the items of Gideon's WiFi store (same item texts, ids and defaults), FakeFs for the file.
#include "check.h"
#include "fakes.h"
#include "hostwifi.h"
#include "config.h"
#include "filemanager.h"
#include <string.h>

int FileManager::calls = 0;
void init_flash_disk(void) { }

#define ID_DHCP 0xE0
#define ID_IP   0xE1
#define ID_MASK 0xE2
#define ID_GW   0xE3
#define ID_DNS  0xE4
static const char *en_dis_t[] = { "Disabled", "Enabled" };
static t_cfg_definition defs[] = {
    { 0xA0, CFG_TYPE_ENUM,   "WiFi Enabled",   "%s", en_dis_t, 0, 1, 1 },
    { ID_DHCP, CFG_TYPE_ENUM,   "Use DHCP",       "%s", en_dis_t, 0, 1, 1 },
    { ID_IP,   CFG_TYPE_STRING, "Static IP",      "%s", NULL, 7, 16, (long)"192.168.2.64" },
    { ID_MASK, CFG_TYPE_STRING, "Static Netmask", "%s", NULL, 7, 16, (long)"255.255.255.0" },
    { ID_GW,   CFG_TYPE_STRING, "Static Gateway", "%s", NULL, 7, 16, (long)"192.168.2.1" },
    { ID_DNS,  CFG_TYPE_STRING, "Static DNS",     "%s", NULL, 7, 16, (long)"8.8.8.8" },
    { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 }
};

static ConfigStore *boot()
{
    ConfigManager *cm = ConfigManager::getConfigManager();
    ConfigStore *old = cm->find_store((uint32_t)0x57494649);
    if (old) cm->remove_store(old);
    return cm->register_store(0x57494649, "WiFi settings", defs, NULL);
}

int main()
{
    const char *host =
        "[Network Settings]\nFTP File Service=Enabled\n\n"
        "[WiFi settings]\nUse DHCP=Disabled\nStatic IP=10.0.0.7\nStatic Netmask=255.255.0.0\nStatic Gateway=10.0.0.1\nStatic DNS=9.9.9.9\nWiFi Enabled=Disabled\n\n";
    FakeFs fs;
    // S3a: the host's static values arrive; the radio's On/Off is not taken over
    {
        ConfigStore *s = boot();
        CHECK_EQ(s->get_value(ID_DHCP), 1);
        fs.put_text(HOSTWIFI_FILE, host);
        CHECK_EQ(hostwifi_load(&fs), 5);
        CHECK_EQ(s->get_value(ID_DHCP), 0);
        CHECK(!s->need_effectuate());                 // applied to the interface
        CHECK_STR(s->get_string(ID_IP), "10.0.0.7");
        CHECK_STR(s->get_string(ID_MASK), "255.255.0.0");
        CHECK_STR(s->get_string(ID_GW), "10.0.0.1");
        CHECK_STR(s->get_string(ID_DNS), "9.9.9.9");
        CHECK_EQ(s->get_value(0xA0), 1);
        CHECK_EQ(fs.writes, 0);                       // read only: the core never writes the host's file
        CHECK_EQ(fs.mutations, 0);
    }
    // S3b: DHCP in the host file
    {
        ConfigStore *s = boot();
        fs.put_text(HOSTWIFI_FILE, "[WiFi settings]\nUse DHCP=Enabled\nStatic IP=10.1.1.1\n");
        CHECK_EQ(hostwifi_load(&fs), 2);
        CHECK_EQ(s->get_value(ID_DHCP), 1);
    }
    // S3c: no file, no section, an unreadable file, junk: the defaults stay (DHCP)
    {
        ConfigStore *s = boot();
        FakeFs none;
        CHECK_EQ(hostwifi_load(&none), -1);
        CHECK_EQ(s->get_value(ID_DHCP), 1);
        CHECK_STR(s->get_string(ID_IP), "192.168.2.64");
        fs.put_text(HOSTWIFI_FILE, "[Network Settings]\nFTP File Service=Enabled\n");
        CHECK_EQ(hostwifi_load(&fs), 0);
        CHECK_EQ(s->get_value(ID_DHCP), 1);
        fs.put_text(HOSTWIFI_FILE, "[WiFi settings]\nUse DHCP=Maybe\nStatic IP=\nFoo=1\n");
        CHECK_EQ(hostwifi_load(&fs), 0);
        CHECK_EQ(s->get_value(ID_DHCP), 1);
        CHECK_STR(s->get_string(ID_IP), "192.168.2.64");
        fs.put_text(HOSTWIFI_FILE, host);
        fs.fail_loads = 1;
        CHECK_EQ(hostwifi_load(&fs), -1);
        CHECK_EQ(s->get_value(ID_DHCP), 1);
    }
    // a section of another store is not taken
    {
        ConfigStore *s = boot();
        fs.put_text(HOSTWIFI_FILE, "[Ethernet Settings]\nUse DHCP=Disabled\nStatic IP=1.2.3.4\n");
        CHECK_EQ(hostwifi_load(&fs), 0);
        CHECK_EQ(s->get_value(ID_DHCP), 1);
    }
    DONE("hostwifi");
}
