#include "handover.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stddef.h>

uint32_t handover_sum(const Handover *h)
{
    const uint8_t *p = (const uint8_t *)h + 12;       // after magic, version, sum
    uint32_t n = offsetof(Handover, id_sum) - 12, s = 0x1F3D5B79u;      // the version-1 bytes only (handover.h)
    while (n--) {
        s = ((s << 5) | (s >> 27)) ^ *p++;
    }
    return s;
}

static uint32_t handover_id_sum(const Handover *h)
{
    uint32_t s = 0x9E3779B1u ^ h->sum;
    for (unsigned i = 0; i < sizeof(h->core_id); i++) {
        s = ((s << 5) | (s >> 27)) ^ (uint8_t)h->core_id[i];
    }
    return s ? s : 1u;
}

static uint32_t handover_time_sum(const Handover *h)
{
    uint32_t s = 0x7F4A7C15u ^ h->sum;
    s = ((s << 5) | (s >> 27)) ^ h->time_date;
    s = ((s << 5) | (s >> 27)) ^ h->time_time;
    return s ? s : 1u;
}

void handover_set_time(Handover *h, int year, int mon, int day, int hour, int min, int sec)
{
    h->time_date = ((uint32_t)year << 16) | ((uint32_t)mon << 8) | (uint32_t)day;
    h->time_time = ((uint32_t)hour << 16) | ((uint32_t)min << 8) | (uint32_t)sec;
    h->time_sum = handover_time_sum(h);
}

bool handover_time(const Handover *h, int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
    if (h->time_sum != handover_time_sum(h)) {
        return false;
    }
    *year = (int)(h->time_date >> 16);
    *mon = (int)((h->time_date >> 8) & 0xFF);
    *day = (int)(h->time_date & 0xFF);
    *hour = (int)(h->time_time >> 16);
    *min = (int)((h->time_time >> 8) & 0xFF);
    *sec = (int)(h->time_time & 0xFF);
    return *year >= 2020 && *year <= 2099 && *mon >= 1 && *mon <= 12 && *day >= 1 && *day <= 31 && *hour < 24 && *min < 60 && *sec < 60;
}

static uint32_t handover_config_sum(const Handover *h)
{
    uint32_t s = 0x2C1B3C6Du ^ h->sum ^ h->id_sum;
    for (unsigned i = 0; i < sizeof(h->config); i++) {
        s = ((s << 5) | (s >> 27)) ^ (uint8_t)h->config[i];
    }
    return s ? s : 1u;
}

bool handover_set_config(Handover *h, const char *name)
{
    memset(h->config, 0, sizeof(h->config));
    h->config_sum = 0;
    if (!name || !name[0] || strlen(name) > HANDOVER_CONFIG_MAX) {
        return false;
    }
    strcpy(h->config, name);
    h->config_sum = handover_config_sum(h);
    return true;
}

static uint32_t handover_hostver_sum(const Handover *h)
{
    uint32_t s = 0x7A55E1B3u ^ h->sum ^ h->id_sum;
    for (unsigned i = 0; i < sizeof(h->host_version); i++) {
        s = ((s << 5) | (s >> 27)) ^ (uint8_t)h->host_version[i];
    }
    return s ? s : 1u;
}

bool handover_set_host_version(Handover *h, const char *version)
{
    memset(h->host_version, 0, sizeof(h->host_version));
    h->hostver_sum = 0;
    if (!version || !version[0] || strlen(version) >= sizeof(h->host_version)) {
        return false;
    }
    strcpy(h->host_version, version);
    h->hostver_sum = handover_hostver_sum(h);
    return true;
}

const char *handover_host_version(const Handover *h)
{
    if (!h->host_version[0] || h->host_version[sizeof(h->host_version) - 1] != 0 || h->hostver_sum != handover_hostver_sum(h)) {
        return "";
    }
    return h->host_version;
}

const char *handover_config(const Handover *h)
{
    if (!h->config[0] || h->config[sizeof(h->config) - 1] != 0 || strlen(h->config) > HANDOVER_CONFIG_MAX ||
        h->config_sum != handover_config_sum(h)) {
        return "";
    }
    return h->config;
}

static uint32_t handover_safe_sum(const Handover *h)
{
    uint32_t s = 0x5AFE6A7Bu ^ h->sum ^ h->id_sum;
    return s ? s : 1u;
}

void handover_set_safe(Handover *h, bool on)
{
    h->safe_sum = on ? handover_safe_sum(h) : 0;
}

bool handover_safe(const Handover *h)
{
    return h->safe_sum != 0 && h->safe_sum == handover_safe_sum(h);
}

static void put(char *dst, int cap, const char *src)
{
    memset(dst, 0, cap);
    if (src) {
        strncpy(dst, src, cap - 1);
    }
}

void handover_fill(Handover *h, const char *path, const char *core_name, const char *core_version, uint32_t host_key,
                   uint32_t video, const char *core_id)
{
    memset(h, 0, sizeof(*h));
    h->magic = HANDOVER_MAGIC;
    h->version = HANDOVER_VERSION;
    h->video = video;
    put(h->path, sizeof(h->path), path);
    put(h->core_name, sizeof(h->core_name), core_name);
    put(h->core_version, sizeof(h->core_version), core_version);
    if (host_key) {
        // not snprintf: the firmware's small_printf prints %x in capitals; the page is lowercase whatever the libc does
        for (int i = 0; i < 8; i++) {
            h->host_build[i] = "0123456789abcdef"[(host_key >> (28 - 4 * i)) & 15];
        }
    }
    h->sum = handover_sum(h);
    if (core_id && core_id[0]) {
        put(h->core_id, sizeof(h->core_id), core_id);
        h->id_sum = handover_id_sum(h);
    }
}

const char *handover_core_id(const Handover *h)
{
    if (!h->core_id[0] || h->core_id[sizeof(h->core_id) - 1] != 0 || h->id_sum != handover_id_sum(h)) {
        return "";
    }
    return h->core_id;
}

bool handover_valid(const Handover *h)
{
    return h->magic == HANDOVER_MAGIC && h->version == HANDOVER_VERSION && h->sum == handover_sum(h) &&
           h->path[sizeof(h->path) - 1] == 0 && h->core_name[sizeof(h->core_name) - 1] == 0 &&
           h->core_version[sizeof(h->core_version) - 1] == 0 && h->host_build[sizeof(h->host_build) - 1] == 0;
}

bool handover_cfg_path(const char *cor_path, char *out, int outlen)
{
    int l = (int)strlen(cor_path);
    if (l < 5 || strcasecmp(cor_path + l - 4, ".cor") != 0 || l >= outlen) {
        return false;
    }
    memcpy(out, cor_path, l - 3);
    strcpy(out + l - 3, "cfg");
    return true;
}

void handover_menu_request(void *page)
{
    volatile uint32_t *m = (volatile uint32_t *)((uint8_t *)page + HANDOVER_MENU_OFFSET);
    m[0] = HANDOVER_MENU_MAGIC;
    m[1] = ~HANDOVER_MENU_MAGIC;
    (void)m[1];                                       // the stores are done before the caller goes on (DDR2 posts writes)
}

bool handover_menu_take(void *page)
{
    volatile uint32_t *m = (volatile uint32_t *)((uint8_t *)page + HANDOVER_MENU_OFFSET);
    bool hit = m[0] == HANDOVER_MENU_MAGIC && m[1] == ~HANDOVER_MENU_MAGIC;
    m[0] = 0;
    m[1] = 0;
    (void)m[1];
    return hit;
}

bool startfile_path(const char *cor_path, char *out, int outlen)
{
    if (!cor_path || cor_path[0] != '/') {
        return false;
    }
    const char *end = strchr(cor_path + 1, '/');
    if (!end) {
        return false;
    }
    int n = (int)(end - cor_path);
    if (n + 1 + (int)sizeof(STARTFILE_NAME) > outlen) {
        return false;
    }
    memcpy(out, cor_path, n);
    snprintf(out + n, outlen - n, "/%s", STARTFILE_NAME);
    return true;
}

int startfile_format(char *out, int outlen, const char *cor_path, const char *config, bool safe)
{
    if (!cor_path || !cor_path[0] || strlen(cor_path) >= HANDOVER_PATH_MAX || (config && strlen(config) > HANDOVER_CONFIG_MAX)) {
        return 0;
    }
    int n = snprintf(out, outlen, "TRXSTART1\ncor=%s\nconfig=%s\nsafe=%d\n", cor_path, config ? config : "", safe ? 1 : 0);
    return n > 0 && n < outlen ? n : 0;
}

bool startfile_parse(const char *text, int len, char *cor, int corlen, char *config, int cfglen, bool *safe)
{
    static const char head[] = "TRXSTART1\n";
    cor[0] = config[0] = 0;
    *safe = false;
    if (len < (int)sizeof(head) - 1 || memcmp(text, head, sizeof(head) - 1)) {
        return false;
    }
    bool have_cor = false;
    int i = (int)sizeof(head) - 1;
    while (i < len) {
        int j = i;
        while (j < len && text[j] != '\n') {
            j++;
        }
        int l = j - i;
        const char *ln = text + i;
        if (l > 4 && !memcmp(ln, "cor=", 4)) {
            if (l - 4 >= corlen) {
                return false;
            }
            memcpy(cor, ln + 4, l - 4);
            cor[l - 4] = 0;
            have_cor = true;
        } else if (l >= 7 && !memcmp(ln, "config=", 7)) {
            if (l - 7 > HANDOVER_CONFIG_MAX || l - 7 >= cfglen) {
                return false;
            }
            memcpy(config, ln + 7, l - 7);
            config[l - 7] = 0;
        } else if (l == 6 && !memcmp(ln, "safe=1", 6)) {
            *safe = true;
        }
        i = j + 1;
    }
    return have_cor;
}
