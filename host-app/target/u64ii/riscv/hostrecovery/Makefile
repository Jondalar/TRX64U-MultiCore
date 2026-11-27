# TRX64 host recovery program (T265 P2). Linked at 0x01800000 and written to the flash slot below the host application (0x1E0000-0x21EFFF); the boot ROM starts it when the
# application in the flash is not the host's own. The drivers are the host application's (Gideon's GPL tree, host-app/PROVENANCE.md): USB with the hub over I2C, the SD card,
# the flash drive (attached read-only), FatFs with long names. No LVGL, no network, no video, no audio. recovery_slot.app is what the boot ROM takes (the record, the data and a
# CRC-32 trailer) and what the host application embeds; the build fails when it does not fit the slot.
# Built in the u64-build-env container: ci/trxapps.sh, tools/build_hostapp.sh (before the host application, which links recovery_slot.app).

CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
MARCH ?= rv32im
MABI  ?= ilp32
# The core has no divider: link libraries built without the M extension
LIBMARCH ?= rv32i

include ../../../common/environment.mk
PRJ      =  hostrecovery
FINAL    =  $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).elf $(RESULT)/$(PRJ).app $(RESULT)/recovery_slot.app

SRCS_C   =	itu.c \
			dump_hex.c \
			assert.c \
			list.c \
			queue.c \
			tasks.c \
			timers.c \
			port.c \
			heap_4.c \
			ff.c \
			ffsystem.c \
			ffunicode.c \
			time.c \
			riscv_main.c \
			do_ctors.c \
			malloc_lock.c

SRCS_CC	 =  usb_hwinit.cc \
			i2c_drv.cc \
			hw_i2c_drv.cc \
			small_printf.cc \
			stream.cc \
			stream_uart.cc \
			memory_wrap.cc \
			init_function.cc \
			flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			s25fl_l_flash.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			rtc_dummy.cc \
			mystring.cc \
			path.cc \
			pattern.cc \
			blockdev.cc \
			blockdev_flash.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
			filesystem_fat.cc \
			filesystem_root.cc \
			sd_card.cc \
			sdio.cc \
			sdcard_manager.cc \
			size_str.cc \
			usb_base.cc \
			usb_device.cc \
			usb_scsi.cc \
			usb_hub.cc \
			ult_syscalls.cc \
			recovery.cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/u64ii
VPATH +=	$(PATH_SW)/u64
VPATH +=	$(PATH_SW)/hostapp
VPATH +=	$(PATH_SW)/hostrecovery
VPATH +=	$(PATH_SW)/u64ctrl/main
VPATH +=	$(PATH_SW)/io/c64
VPATH +=	$(PATH_SW)/api
VPATH +=	$(PATH_SW)/io/usb
VPATH +=	$(PATH_SW)/io/uart

SRCS_ASM =
SRCS_ASMS = crt0.S port_asm.S
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO = nano_minimal.nan

# the tables of the base blocks and the link fragment with IOBASE and U2P_IO_BASE: from the TOML A of the host, as for the host application (T239.3 V-3, D30, D31)
BASETAB_TOMLA ?= $(PATH_SW)/../../coreapp/gateware/host.toml
BASETAB_DIR    = $(OUTPUT)/io
BASETAB_GEN   := $(shell python3 $(PATH_SW)/../../tools/descrec.py basetab $(BASETAB_TOMLA) $(BASETAB_DIR) 2>&1 || echo "basetab: the table of the base blocks was not generated")
ifneq ($(strip $(BASETAB_GEN)),)
$(error $(BASETAB_GEN))
endif
VPATH += $(BASETAB_DIR)

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-div
OPTIONS += -gdwarf-2 -Os -DRISCV -DU64=2 -DUSB2513 -DOS -DCLOCK_FREQ=100000000 -Wno-write-strings -DFP_SUPPORT=1
OPTIONS += -DFLASH_EXTRA_PROTECTION=1 -DTRX_HOSTAPP=1 -I$(PATH_SW)/hostapp
OPTIONS += -DconfigTOTAL_HEAP_SIZE=0x100000
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS     =
ifndef IOFRAG
HOST_TOMLA = $(PATH_SW)/../../coreapp/gateware/host.toml
IOFRAG     = $(OUTPUT)/io/iofrag.ld
IOFRAG_GEN := $(shell mkdir -p $(OUTPUT)/io && python3 $(PATH_SW)/../../tools/descrec.py ldfrag $(HOST_TOMLA) $(IOFRAG).new --maps $(PATH_SW)/../../app/sdkpkg/maps 2>&1 && { cmp -s $(IOFRAG).new $(IOFRAG) || cp $(IOFRAG).new $(IOFRAG); } || echo "iofrag: the link fragment was not generated")
ifneq ($(strip $(IOFRAG_GEN)),)
$(error $(IOFRAG_GEN))
endif
endif
LIBS    += $(IOFRAG)
LFLAGS   = --gc-sections --wrap=malloc --wrap=_malloc_r --wrap=free --wrap=_free_r --wrap=realloc --wrap=_realloc_r --wrap=calloc --wrap=_calloc_r
LINKMETHOD = ld

VERSION_COMPONENT = hostapp
include ../../../common/rules.mk

$(OUTPUT)/$(PRJ).out: $(IOFRAG)

# the slot image: the record, the data, a CRC-32 trailer; refuses a program that does not fit the 0x3F000 slot (T265 P2: tools/rec_trailer.py, the limit is the slot less the record and the trailer)
$(RESULT)/recovery_slot.app: $(RESULT)/$(PRJ).app
	@echo Creating the slot image $@
	@python3 -I $(PATH_SW)/../../tools/rec_trailer.py $< $@ 0x3EFF0
