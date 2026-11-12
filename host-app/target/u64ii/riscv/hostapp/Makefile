# TRX64 Ultimate host boot app (T84). Derived from target/u64ii/riscv/ultimate of the
# Ultimate app (GPL, see host-app/PROVENANCE.md); everything of the C64 side is gone.
# Built in the u64-build-env container: tools/build_hostapp.sh

CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
UI_RES ?= 0   # T101: layout resolution of the pixel layer, see lvgl_port/trx_lv_port.h
MARCH ?= rv32im
MABI  ?= ilp32
# The core has no divider: link libraries built without the M extension
LIBMARCH ?= rv32i

include ../../../common/environment.mk
PRJ      =  hostapp
FINAL    =  $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).elf $(RESULT)/$(PRJ).app

SRCS_C   =	itu.c \
			dump_hex.c \
			cmd_buffer.c \
			assert.c \
			croutine.c \
			event_groups.c \
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
			mdio.c \
			riscv_main.c \
			do_ctors.c \
			malloc_lock.c \
			sha256.c \
			gitinfo.c

SRCS_CC	 =  u64ii_init.cc \
			color_timings.cc \
			hdmi_scan.cc \
			usb_hwinit.cc \
			i2c_drv.cc \
			hw_i2c_drv.cc \
			nau8822.cc \
			small_printf.cc \
			stream.cc \
			stream_uart.cc \
			memory_wrap.cc \
			init_function.cc \
			flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			s25fl_l_flash.cc \
			product.cc \
			config.cc \
			subsys.cc \
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
			usb_ax88772.cc \
			usb_hid.cc \
			usb_ms_cbi.cc \
			keyboard_usb.cc \
			network_config.cc \
			sntp_time.cc \
			network_interface.cc \
			vfs.cc \
			ftpd.cc \
			rmii_interface.cc \
			network_esp32.cc \
			wifi.cc \
			wifi_cmd.cc \
			esp32.cc \
			wifinet.cc \
			ult_syscalls.cc \
			joystick_output.cc \
			keyboard_c64.cc \
			trx_io.cc \
			trx_lv_port.cc \
			json.cc \
			cor.cc \
			cfw.cc \
			cfwicon.cc \
			fatwalk.cc \
			legacy.cc \
			srcverify.cc \
			legacy_tramp.cc \
			handover.cc \
			configlist.cc \
			hostfs.cc \
			mbr.cc \
			mediasniff.cc \
			mediaui.cc \
			mediaglue.cc \
			fmt.cc \
			fmtconfirm.cc \
			fwknown.cc \
			fwconvert.cc \
			flashfw.cc \
			fwrestore.cc \
			fwconfirm.cc \
			txu.cc \
			espinspect.cc \
			espflash.cc \
			espflash_board.cc \
			flashlock.cc \
			flashlock_board.cc \
			espreboot.cc \
			dma_uart.cc \
			bootcfg.cc \
			hostcfg.cc \
			hostcfg_io.cc \
			ftpstate_board.cc \
			cfgfile.cc \
			menusnap.cc \
			fileops.cc \
			coremaint.cc \
			lineedit.cc \
			directboot.cc \
			menukeys.cc \
			startfile.cc \
			ovtext.cc \
			discover.cc \
			startcheck.cc \
			coreboot.cc \
			hostui.cc \
			trxview.cc \
			videomode.cc \
			edid.cc \
			hostvideo.cc \
			hostvideo_board.cc \
			webui.cc \
			webnet.cc \
			httpreq.cc \
			picmodes.cc \
			wsproto.cc \
			svcjson.cc \
			nettransport.cc \
			host_core.cc \
			host_core_flow.cc \
			host_core_flow_restore.cc \
			host_core_flow_format.cc \
			host_core_flow_convert.cc \
			host_core_flow_esp.cc \
			host_core_flow_fileops.cc \
			host_core_flow_wifi.cc \
			host_core_nav.cc \
			host_core_cm.cc \
			host_core_cfgov.cc \
			host_core_rows.cc \
			host_core_ui.cc \
			host_core_model.cc \
			hostdecl.cc \
			hostdomain.cc \
			hostapi_board.cc \
			uic_json.cc \
			uic_sha.cc \
			uic_svc.cc \
			uic_net.cc \
			bugreport.cc \
			hostreport.cc \
			hostreport_live.cc \
			hostlog.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/u64ii
VPATH +=	$(PATH_SW)/u64
VPATH +=	$(PATH_SW)/hostapp
VPATH +=	$(PATH_SW)/u64ctrl/main
VPATH +=	$(PATH_SW)/../../coreapp/src
VPATH +=	$(PATH_SW)/../../uicore
VPATH +=	$(PATH_SW)/../../app/view
VPATH +=	$(PATH_SW)/../../app/edit
VPATH +=	$(PATH_SW)/../../app/video
VPATH +=	$(PATH_SW)/lvgl_port
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

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-div
OPTIONS += -gdwarf-2 -Os -DRISCV -DU64=2 -DUSB2513 -DOS -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=100000000 -Wno-write-strings -DFP_SUPPORT=1
OPTIONS += -DFLASH_EXTRA_PROTECTION=1 -DTRX_HOSTAPP=1 -DUI_RES=$(UI_RES) -I$(PATH_SW)/hostapp
OPTIONS += -I$(PATH_SW)/lvgl -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
OPTIONS += -DconfigTOTAL_HEAP_SIZE=0x7A0000
OPTIONS += $(HOSTAPP_EXTRA)      # T137: -DHOSTAPP_EMU_FLASH for a build that runs in ue2emu (its flash is an Infineon S25FL128L); never for a board
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS    = ../../../libs/riscv/lwip/result/liblwip.a
LIBS   += ../../../libs/riscv/lvgl/result/liblvgl.a
LFLAGS   = --gc-sections --wrap=outbyte --wrap=malloc --wrap=_malloc_r --wrap=free --wrap=_free_r --wrap=realloc --wrap=_realloc_r --wrap=calloc --wrap=_calloc_r
LINKMETHOD = ld

include ../../../common/rules.mk

# the hash loop is the hot spot of the bundle check
sha256.o: sha256.c
	@echo Compiling $(<F) optimized for speed
	@$(CC) $(COPTIONS) -O3 $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CC) $(COPTIONS) -O3 -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)
