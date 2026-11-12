#include "hostwifi.h"
#include "cfgfile.h"
#include "config.h"
#include "fsif.h"
#include <stdio.h>
#include <string.h>

#define HOSTWIFI_MAX 8192            // the host's HOSTCFG_MAX: the whole file

namespace {
struct Ctx {
    ConfigStore *s;
    int n;
};
}

static void item(void *user, const char *store, const char *key, const char *value)
{
    Ctx *c = (Ctx *)user;
    if (!cfgfile_label_equal(store, "WiFi settings") || !value[0]) {
        return;
    }
    static const char *const keys[] = { "Use DHCP", "Static IP", "Static Netmask", "Static Gateway", "Static DNS" };
    bool known = false;
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        known = known || cfgfile_label_equal(key, keys[i]);
    }
    if (!known) {
        return;                                       // only the address settings: the radio's on/off is not stored (T184 D3)
    }
    ConfigItem *it = c->s->find_item(key);            // the store's own item text names the item, so its id is the store's
    if (!it) {
        return;
    }
    uint8_t id = it->definition->id;
    if (it->definition->type == CFG_TYPE_ENUM) {
        for (int k = it->definition->min; k <= it->definition->max; k++) {
            if (it->definition->items[k] && cfgfile_label_equal(it->definition->items[k], value)) {
                c->s->set_value(id, k);
                c->n++;
                break;
            }
        }
    } else if (it->definition->type == CFG_TYPE_STRING) {
        c->s->set_string(id, value);
        c->n++;
    }
}

int hostwifi_apply(ConfigStore *wifi, const char *text, int len)
{
    Ctx c = { wifi, 0 };
    cfgfile_parse(text, len, item, &c);
    return c.n;
}

int hostwifi_load(CoreFs *fs)
{
    static char text[HOSTWIFI_MAX + 1];
    ConfigStore *s = ConfigManager::getConfigManager()->find_store((uint32_t)0x57494649);
    if (!s || !fs) {
        printf("[wifi] host settings: no WiFi store, defaults stay\n");
        return -1;
    }
    int n = fs->load(HOSTWIFI_FILE, text, HOSTWIFI_MAX);
    if (n <= 0) {
        printf("[wifi] host settings: %s cannot be read, defaults stay (DHCP)\n", HOSTWIFI_FILE);
        return -1;
    }
    int k = hostwifi_apply(s, text, n);
    if (k > 0) {
        s->effectuate();
    }
    printf("[wifi] host settings: %d value(s) taken over from %s (%s)\n", k, HOSTWIFI_FILE, k > 0 ? "host" : "defaults");
    return k;
}
