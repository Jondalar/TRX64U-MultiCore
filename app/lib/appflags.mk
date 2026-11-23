# The compile and link options of the core application, for the package of the SDK (T239.3 D31, P3-2). They are the options of
# host-app/target/u64ii/riscv/coreapp/Makefile (OPTIONS, COPTIONS, CPPOPT, LFLAGS); `python3 tools/app_build.py flags <that Makefile> <this file>` (lint, tools/sdk_pack.sh)
# refuses a -D that the Makefile has and this file lacks, so the app the package links is the app the tree links.
RV_PREFIX ?= riscv32-unknown-elf-
CROSS     = $(RV_PREFIX)
CC        = $(CROSS)gcc
CPP       = $(CROSS)g++
LD        = $(CROSS)ld
AR        = $(CROSS)ar
OBJCOPY   = $(CROSS)objcopy
SIZE      = $(CROSS)size
MARCH    ?= rv32im
MABI     ?= ilp32
OPTIONS   = -march=$(MARCH) -mabi=$(MABI) -ffunction-sections -fdata-sections -nostartfiles -mno-div
OPTIONS  += -gdwarf-2 -Os -DRISCV -DU64=2 -DUSB2513 -DOS -DCLOCK_FREQ=100000000 -Wno-write-strings -DFP_SUPPORT=1
OPTIONS  += -DFLASH_EXTRA_PROTECTION=1 -DTRX_HOSTAPP=1 -DTRX_COREAPP=1
OPTIONS  += -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
OPTIONS  += -DCORE_OWN_IMAGE_C64
COPTIONS  = $(OPTIONS) -std=gnu99
CPPOPT    = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LFLAGS    = --gc-sections --wrap=pvPortMalloc --wrap=_sbrk --wrap=outbyte --wrap=malloc --wrap=_malloc_r --wrap=free --wrap=_free_r --wrap=realloc --wrap=_realloc_r --wrap=calloc --wrap=_calloc_r
