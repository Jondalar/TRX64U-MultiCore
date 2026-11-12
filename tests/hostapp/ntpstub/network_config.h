#pragma once
#include "config.h"
#define CFG_NETWORK_NTP_EN   0x30
#define CFG_NETWORK_NTP_ZONE 0x31
#define CFG_NETWORK_NTP_SRV1 0x32
#define CFG_NETWORK_NTP_SRV2 0x33
#define CFG_NETWORK_NTP_SRV3 0x34
struct NetworkConfig {
    ConfigStore *cfg;
    const char *posix = "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00";   // "Western Europe", Gideon's default zone 16
    const char *get_posix_timezone() { return posix; }
};
extern NetworkConfig networkConfig;
