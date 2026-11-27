// The registers of a native test: DRV_PTR_HEADER of the generated drvtab.h (tools/descrec.py drivers, T239.3 D31). A driver's register access lands in one array, indexed by the
// low 21 bits of the address, so a test reads back what the driver wrote (and sets what it reads). The test defines the array.
#ifndef TESTS_DRV_FAKE_H
#define TESTS_DRV_FAKE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t g_drv_fake[0x200000];
#ifdef __cplusplus
}
#endif

#define DRV_PTR(type, addr) ((volatile type *)(g_drv_fake + ((uintptr_t)(addr) & 0x1FFFFF)))

#endif
