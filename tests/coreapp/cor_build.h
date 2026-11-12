// A .cor bundle made in memory (T104 5.14): the layout of tools/cor_pack.py as host-app/software/hostapp/cor.cc reads it - "TRXCOR01", the
// length behind the first 12 bytes, version 1, the manifest, then one record per part (type 2 partial, 3 application) of type, length, bytes.
#ifndef TESTS_COREAPP_COR_BUILD_H
#define TESTS_COREAPP_COR_BUILD_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "sha256.h"

static inline std::string cor_sha_hex(const std::vector<uint8_t> &d)
{
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, d.data(), (uint32_t)d.size());
    uint8_t dg[32];
    char hx[65];
    sha256_final(&c, dg);
    sha256_hex(dg, hx);
    return hx;
}

static inline void cor_le32(std::vector<uint8_t> &v, uint32_t x)
{
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i)));
}

// host_key: the 8 hex digits the manifest names. sha_override: a wrong hash for the partial (the manifest then disagrees with the bytes).
static inline std::vector<uint8_t> make_cor(const char *name, const char *host_key, const std::vector<uint8_t> &partial, const char *sha_override = 0)
{
    std::vector<uint8_t> app(64, 0x60);
    char m[900];
    snprintf(m, sizeof(m),
             "{\"name\":\"%s\",\"version\":\"0.1\",\"fpga\":\"xc7a50t\",\"contract\":2,\"host_key\":\"%s\",\"app_load\":196608,\"app_entry\":196608,"
             "\"partial_size\":%u,\"partial_sha256\":\"%s\",\"app_size\":%u,\"app_sha256\":\"%s\"}",
             name, host_key, (unsigned)partial.size(), sha_override ? sha_override : cor_sha_hex(partial).c_str(), (unsigned)app.size(),
             cor_sha_hex(app).c_str());
    std::vector<uint8_t> f;
    const char magic[] = "TRXCOR01";
    f.insert(f.end(), magic, magic + 8);
    cor_le32(f, 0);
    cor_le32(f, 1);
    cor_le32(f, (uint32_t)strlen(m));
    f.insert(f.end(), m, m + strlen(m));
    cor_le32(f, 2); cor_le32(f, (uint32_t)partial.size());
    f.insert(f.end(), partial.begin(), partial.end());
    cor_le32(f, 3); cor_le32(f, (uint32_t)app.size());
    f.insert(f.end(), app.begin(), app.end());
    uint32_t len = (uint32_t)f.size() - 12;
    for (int i = 0; i < 4; i++) f[8 + i] = (uint8_t)(len >> (8 * i));
    return f;
}

#endif
