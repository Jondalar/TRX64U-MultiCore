# TRX64 Ultimate core-application blueprint (T93), on the same base as the host boot app: this Makefile is
# hostapp's with the application's own sources (coreapp/src, coreapp/cores) in place of the boot menu's.
# Built in the u64-build-env container: tools/build_coreapp.sh

CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
MARCH ?= rv32im
MABI  ?= ilp32
# The core has no divider: link libraries built without the M extension
LIBMARCH ?= rv32i

include ../../../common/environment.mk
PRJ      =  coreapp
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
			sid_coeff.c \
			tm_dis6502.c \
			tm_mlib.c \
			tm_spans.c \
			tm_asm6502.c \
			tm_cond.c \
			tm_hw_trx.c \
			tm_json.c \
			tm_mach.c \
			tm_mon.c \
			tm_ring.c \
			tm_rpc.c \
			tm_trace.c \
			tm_trx_lens.c \
			tm_ws.c \
			http_protocol.c \
			multipart.c \
			url.c \
			tm_ui.c \
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
			usb_xinput.cc \
			xinput.cc \
			usb_ms_cbi.cc \
			keyboard_usb.cc \
			network_config.cc \
			sntp_time.cc \
			syslog.cc \
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
			handover.cc \
			configlist.cc \
			cor.cc \
			partial.cc \
			start.cc \
			mediumwait.cc \
			board.cc \
			tempdisk.cc \
			blockdev_ram.cc \
			table.cc \
			cfgfile.cc \
			settings.cc \
			picture.cc \
			picmodes.cc \
			volume.cc \
			tone.cc \
			hotkeys.cc \
			ops.cc \
			ops_glue.cc \
			update.cc \
			exec.cc \
			libload.cc \
			libscan.cc \
			libboard.cc \
			libguard.cc \
			bootcfg.cc \
			service.cc \
			contrib.cc \
			bindcheck.cc \
			descrec.cc \
			libmenu.cc \
			bundle.cc \
			instfiles.cc \
			instcore.cc \
			settings_ops.cc \
			writeback.cc \
			writeback_crt.cc \
			memops.cc \
			svcjson.cc \
			svc.cc \
			httpreq.cc \
			rest.cc \
			rpc.cc \
			rpccontract.cc \
			wsproto.cc \
			nettransport.cc \
			restsrv.cc \
			regset.cc \
			engines.cc \
			ledload.cc \
			winslot.cc \
			sectors.cc \
			mediamounts.cc \
			home.cc \
			menubuild.cc \
			amiga_ffs.cc \
			folderhd.cc \
			folderhd_fold.cc \
			newfile.cc \
			newdisk.cc \
			romset.cc \
			mousesink.cc \
			rawkeys.cc \
			pad.cc \
			hidpad.cc \
			pad_ops.cc \
			padwiz.cc \
			fbwin.cc \
			romfind.cc \
			corefolder.cc \
			video.cc \
			freeze.cc \
			menutree.cc \
			menuctl.cc \
			filemgr.cc \
			menukeytab.cc \
			menuflows.cc \
			menuvlist.cc \
			lineedit.cc \
			boot.cc \
			atari800.cc \
			nes.cc \
			apple2.cc \
			apple2_disk.cc \
			apple2_nib.cc \
			apple2_woz.cc \
			apple2_hdd.cc \
			zx.cc \
			zx_snap.cc \
			tape_pulses.cc \
			tape_deck.cc \
			cpc.cc \
			cpc_disk.cc \
			vc20.cc \
			c264.cc \
			pet.cc \
			c128.cc \
			c64scpu.cc \
			ddt_mandel.cc \
			ddt_novic.cc \
			portdiag.cc \
			cbm_run.cc \
			cbm1541.cc \
			d1541_drive.cc \
			d1541_gcr.cc \
			d1541_wd.cc \
			d1541_fs.cc \
			zxnext.cc \
			snes.cc \
			snes_rom.cc \
			neogeo.cc \
			neoset.cc \
			ines.cc \
			c64.cc \
			u64cfg.cc \
			ring.cc \
			ringops.cc \
			monitor.cc \
			monsrv.cc \
			diag.cc \
			diagtest.cc \
			bugreport.cc \
			corereport.cc \
			netfs.cc \
			ftp_client.cc \
			a64_http.cc \
			a64_assembly.cc \
			online.cc \
			netops.cc \
			netsvc.cc \
			hostwifi.cc \
			ringfile.cc \
			uci.cc \
			command_intf.cc \
			network_target.cc \
			libfiles.cc \
			t64.cc \
			trx64u_ops.cc \
			c64_go.cc \
			c64_cart.cc \
			cbm_prg.cc \
			kickstart.cc \
			amiga.cc \
			atari_sio.cc \
			atari_atr.cc \
			atari_xex.cc \
			atari_cart.cc \
			atari_media.cc \
			fm_fs.cc \
			fm_medium.cc \
			mediasniff.cc \
			mediaui.cc \
			fastseek.cc \
			dma_uart.cc \
			hw.cc \
			ui.cc \
			screens.cc \
			uiapp.cc \
			uiapp_board.cc \
			uic_json.cc \
			uic_sha.cc \
			uic_svc.cc \
			uic_net.cc \
			appdlg.cc \
			appsvc.cc \
			appindex.cc \
			apphost.cc \
			trxview.cc \
			videomode.cc \
			edid.cc \
			run.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/u64ii
VPATH +=	$(PATH_SW)/u64
VPATH +=	$(PATH_SW)/hostapp
VPATH +=	$(PATH_SW)/u64ctrl/main
VPATH +=	$(PATH_SW)/io/uart
VPATH +=	$(PATH_SW)/../../coreapp/src
VPATH +=	$(PATH_SW)/../../uicore
VPATH +=	$(PATH_SW)/../../app/view
VPATH +=	$(PATH_SW)/../../app/edit
VPATH +=	$(PATH_SW)/../../app/video
VPATH +=	$(PATH_SW)/../../app/loaders
VPATH +=	$(PATH_SW)/../../app/lib/cbm1541
VPATH +=	$(PATH_SW)/../../coreapp/cores
VPATH +=	$(PATH_SW)/lvgl_port
VPATH +=	$(PATH_SW)/io/c64
VPATH +=	$(PATH_SW)/api
VPATH +=	$(PATH_SW)/io/usb
VPATH +=	$(PATH_SW)/../../vendor/gideon/software/io/audio
VPATH +=	$(PATH_SW)/../../coreapp/src/trxmon
VPATH +=	$(PATH_SW)/../../vendor/gideon/software/appsdk/include
VPATH +=	$(PATH_SW)/httpd/c-version/lib
VPATH +=	$(PATH_SW)/../../vendor/gideon/software/network

SRCS_ASM =
SRCS_ASMS = crt0.S port_asm.S
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-div
OPTIONS += -gdwarf-2 -Os -DRISCV -DU64=2 -DUSB2513 -DOS -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=100000000 -Wno-write-strings -DFP_SUPPORT=1
# coreapp/ffconf first: FatFs finds its ffconf.h there (the original with FF_USE_FASTSEEK on, T104 5.16), before chan_fat/full and hostapp
OPTIONS += -I$(PATH_SW)/../../coreapp/ffconf
OPTIONS += -DFLASH_EXTRA_PROTECTION=1 -DTRX_HOSTAPP=1 -DTRX_COREAPP=1 -I$(PATH_SW)/hostapp
OPTIONS += -I$(PATH_SW)/lvgl -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
OPTIONS += -DconfigTOTAL_HEAP_SIZE=0x7A0000
OPTIONS += $(COREAPP_EXTRA)      # #93: -DLISTGROW_START=40 -DLISTGROW_HARD=60 for the mock-up image that shows the "more entries not shown" line; never for a board
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS    = ../../../libs/riscv/lwip/result/liblwip.a
LIBS   += ../../../libs/riscv/lvgl/result/liblvgl.a
LFLAGS   = --gc-sections --wrap=outbyte --wrap=malloc --wrap=_malloc_r --wrap=free --wrap=_free_r --wrap=realloc --wrap=_realloc_r --wrap=calloc --wrap=_calloc_r
LINKMETHOD = ld

VERSION_COMPONENT = coreapp          # T221: ci/version.sh <component> names the SemVer file the TRXVSTAMP field starts from
include ../../../common/rules.mk

# the hash loop of Reload core (partial.cc) reads the whole partial bitstream: as in the host app
sha256.o: sha256.c
	@echo Compiling $(<F) optimized for speed
	@$(CC) $(COPTIONS) -O3 $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CC) $(COPTIONS) -O3 -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)
