// SHA-256 (FIPS 180-4), small and streaming, for the .cor part checks (T84).
#ifndef HOST_SHA256_H
#define HOST_SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint8_t buf[64];
    uint32_t fill;
    uint64_t total;
} sha256_t;

#ifdef __cplusplus
extern "C" {
#endif
void sha256_init(sha256_t *c);
void sha256_update(sha256_t *c, const void *data, uint32_t len);
void sha256_final(sha256_t *c, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);
#ifdef __cplusplus
}
#endif

#endif
