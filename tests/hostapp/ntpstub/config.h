// Host-test stub: the settings store as sntp_time.cc and network_config.h use it.
#pragma once
#include <stdint.h>
class ConfigStore {
public:
    int en = 1, zone = 0;
    const char *srv[3] = { "s1", "s2", "s3" };
    int get_value(uint8_t id) { return id == 0x30 ? en : zone; }
    const char *get_string(uint8_t id) { return srv[id - 0x32]; }
};
