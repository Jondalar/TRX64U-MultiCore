#include "mbr.h"
#include <string.h>

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void mbr_build(uint8_t sector[512], uint32_t total, uint32_t disk_sig)
{
    memset(sector, 0, 512);
    le32(sector + 440, disk_sig);
    uint8_t *e = sector + 446;
    e[0] = 0x00;                          // not bootable
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;    // CHS of the first sector: out of range, LBA is authoritative
    e[4] = MBR_TYPE_EXFAT;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;    // CHS of the last sector
    le32(e + 8, MBR_PART_START);
    le32(e + 12, total - MBR_PART_START);
    sector[510] = 0x55;
    sector[511] = 0xAA;
}
