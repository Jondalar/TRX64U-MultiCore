/*
 * eapi_drivers.h
 *
 *  EAPI flash drivers from skoe's EasySDK (see eapi_drivers.c for source,
 *  version and license).
 */

#ifndef EAPI_DRIVERS_H
#define EAPI_DRIVERS_H

#include "u2p_app.h"

#define EAPI_DRIVER_SIZE 0x300  // 768 bytes
#define EAPI_ROMH_OFFSET 0x1800 // in bank 0 ROMH, starts with the signature "eapi" ($65 $61 $70 $69)

// "Am/M29F040 V1.4" (eapi-am29f040.s): EasyFlash
extern const uint8_t eapi_am29f040[EAPI_DRIVER_SIZE];
// "MX29LV640EB 1.2" (eapi-mx29640b.s): EasyFlash 3
extern const uint8_t eapi_mx29640b[EAPI_DRIVER_SIZE];

#endif
