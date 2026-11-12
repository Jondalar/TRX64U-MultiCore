
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32
PRODFAMILY = U64MK2
#PRODNAME   = Ultimate\ 64-II

include ../../../common/environment.mk

# T49 TRX64 Ultimate update package. The input is named on the command line (tools/make_update.sh):
#   BLOBS_FILE=<trx_blobs.bin>   manifest, bitstream and app as tools/trx_pkg.py blobs writes them
# There is no default that could package the wrong build.
BLOBS_FILE ?=

PRJ      =  update
FINAL    =  $(RESULT)/$(PRJ).cfw $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim

SRCS_C   =	itu.c \
			dump_hex.c \
			codec.c \
			assert.c \
			profiler.c \
			croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
            heap_3.c \
            ff.c \
            ffsystem.c \
            ffunicode.c \
            time.c \
            mdio.c \
			cmd_buffer.c \
            riscv_main.c \
            sha256.c \
             
SRCS_CC	 =  u64ii_init.cc \
			color_timings.cc \
			hdmi_scan.cc \
			usb_hwinit.cc \
			memory.cc \
			i2c_drv.cc \
			hw_i2c_drv.cc \
			nau8822.cc \
			small_printf.cc \
			flash.cc \
			w25q_flash.cc \
            s25fl_flash.cc \
            s25fl_l_flash.cc \
			pattern.cc \
			config.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			filesystem_root.cc \
			init_function.cc \
			c64.cc \
			u64_machine.cc \
			screen.cc \
			screen_vt100.cc \
			keyboard_c64.cc \
			keyboard_usb.cc \
			keyboard_vt100.cc \
			mystring.cc \
			size_str.cc \
			rtc_dummy.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			stream.cc \
			host_stream.cc \
			prog_flash.cc \
			product.cc \
			stream_uart.cc \
			path.cc \
			blockdev.cc \
			blockdev_flash.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
			filesystem_fat.cc \
			sd_card.cc \
			sdio.cc \
			sdcard_manager.cc \
			dma_uart.cc \
			esp32.cc \
			wifi_cmd.cc \
			\
			usb_base.cc \
			usb_device.cc \
			usb_scsi.cc \
			usb_hub.cc \
			usb_hid.cc \
			\
			update_trx.cc

VPATH +=	.
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/6502/sidcrt/target
VPATH +=	$(PATH_SW)/u64
VPATH +=    $(PATH_SW)/application/update_u2p
VPATH +=    $(PATH_SW)/../html
VPATH +=    $(PATH_SW)/../doc/api
VPATH +=	$(PATH_SW)/u64ctrl/main
VPATH +=	$(PATH_SW)/u64ctrl/build/bootloader
VPATH +=	$(PATH_SW)/u64ctrl/build/partition_table
VPATH +=	$(PATH_SW)/u64ctrl/build

SRCS_ASM = update_binaries_trx.s

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = 

# the C64 character ROM: c64.cc (the C64 screen the updater draws on) links it
SRCS_BIN = chars.bin

SRCS_HTML = 
SRCS_YAML = 

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) -gdwarf-2 -ffunction-sections -DRISCV -Os -DOS -DUSB2513 -DRISC=1 -DSAFEMODE=1 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000
OPTIONS += -DCLOCK_FREQ=100000000 -DU64=2 -Wno-write-strings -fdata-sections -nostartfiles -mno-fdiv -DRECOVERYAPP=1 -DNO_ESP=1 -DTRX_UPDATER=1
# test builds only (tools/make_update.sh --emu-test): -DTRX_EMU_TEST accepts ue2emu's S25FL128L
OPTIONS += $(TRX_TEST_OPTS)
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS     = #../../../libs/riscv/lwip/result/liblwip.a
#LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o
LFLAGS   = --gc-sections
LINKMETHOD = ld

include ../../../common/rules.mk


# The blob region goes into the output directory under the name update_binaries_trx.s includes.
$(OUTPUT)/trx_blobs.bin: $(BLOBS_FILE)
	@test -n "$(BLOBS_FILE)" || { echo "BLOBS_FILE not set"; exit 1; }
	cp $< $@

update_binaries_trx.o: $(OUTPUT)/trx_blobs.bin
