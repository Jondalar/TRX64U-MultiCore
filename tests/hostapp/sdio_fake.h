// Fake SD register objects for the native test of host-app/software/io/sd_card (force-included into sdio.cc, sd_card.cc and the test).
// Every access to SDIO_DATA / SDIO_DATA_32 is one SPI byte clocked through a fake card (test_sdcrc.cc); a 32-bit read is four byte reads, little endian,
// the way bus_converter.vhd splits it.
#ifndef SDIO_FAKE_H
#define SDIO_FAKE_H
#include <stdint.h>
#define SDIO_EXTERNAL_REGS 1
uint8_t  fake_spi_xfer(uint8_t tx);
struct FakeData8  { void operator=(uint8_t v) { fake_spi_xfer(v); } operator uint8_t() { return fake_spi_xfer(0xFF); } };
struct FakeData32 { void operator=(uint32_t) { } operator uint32_t() { uint32_t r = 0; for (int i = 0; i < 4; i++) r |= (uint32_t)fake_spi_xfer(0xFF) << (8 * i); return r; } };
struct FakeReg8   { uint8_t v, rd; void operator=(uint8_t x) { v = x; } operator uint8_t() { return rd; } }; // v: last write, rd: what a read returns (test_sdspeed.cc sets f_ctrl.rd)
extern FakeData8 f_data; extern FakeData32 f_data32; extern FakeReg8 f_speed, f_ctrl, f_crc, f_switch;
#define SDIO_DATA    f_data
#define SDIO_DATA_32 f_data32
#define SDIO_SPEED   f_speed
#define SDIO_CTRL    f_ctrl
#define SDIO_CRC     f_crc
#define SDIO_SWITCH  f_switch
#endif
