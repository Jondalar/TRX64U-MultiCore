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
			heap_5.c \
			heapcfg.c \
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
			http_protocol.c \
			multipart.c \
			url.c \
			gitinfo.c \
			drvtab.c

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
			blockdev_ram.cc \
			table.cc \
			cfgfile.cc \
			settings.cc \
			picture.cc \
			picmodes.cc \
			volume.cc \
			tone.cc \
			baseset.cc \
			hookhost.cc \
			hooklib.cc \
			hotkeys.cc \
			ops.cc \
			ops_glue.cc \
			exec.cc \
			libload.cc \
			libsvc.cc \
			libexports.cc \
			libscan.cc \
			libboard.cc \
			libguard.cc \
			bootcfg.cc \
			service.cc \
			contrib.cc \
			bindcheck.cc \
			descrec.cc \
			overlay.cc \
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
			textcopy.cc \
			romset.cc \
			rawkeys.cc \
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
			picklist.cc \
			lineedit.cc \
			boot.cc \
			apple2.cc \
			apple2_disk.cc \
			apple2_nib.cc \
			apple2_woz.cc \
			apple2_hdd.cc \
			d1541_fs.cc \
			snes.cc \
			snes_rom.cc \
			diag.cc \
			bugreport.cc \
			corereport.cc \
			netfs.cc \
			netops.cc \
			netsvc.cc \
			hostwifi.cc \
			libfiles.cc \
			kickstart.cc \
			fm_fs.cc \
			fm_medium.cc \
			mediasniff.cc \
			mediaui.cc \
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
VPATH +=	$(PATH_SW)/../../vendor/gideon/software/appsdk/include
VPATH +=	$(PATH_SW)/httpd/c-version/lib
VPATH +=	$(PATH_SW)/../../vendor/gideon/software/network

# T239.3 P2-3 (D31, D34): the driver table. The app is linked against one TOML A (the shared app: coreapp/gateware/shared.toml, today's addresses); `descrec.py drivers`
# writes drvtab.h (the blocks, registers, fields and states as macros) and drvtab.c (the table of the blocks) into $(OUTPUT)/drv before any source is compiled, each
# file only when its content changes, so an unchanged TOML A recompiles nothing. A driver names a register only through drvtab.h; a block the TOML A lacks (the ICAP loader of
# an app linked for c64_basic) is not there to name.
DRV_TOMLA = $(PATH_SW)/../../coreapp/gateware/shared.toml
DRV_DIR   = $(OUTPUT)/drv
DRV_GEN  := $(shell python3 $(PATH_SW)/../../tools/descrec.py drivers $(DRV_TOMLA) $(DRV_DIR) --maps $(PATH_SW)/../../app/sdkpkg/maps 2>&1 || echo "drvtab: the driver table was not generated")
ifneq ($(strip $(DRV_GEN)),)
$(error $(DRV_GEN))
endif
VPATH += $(DRV_DIR)

# T239.3 V-3b (D30 point 1): the blocks of the base in the TOML A the app is linked with (the shared app and the host app: coreapp/gateware/host.toml; an app of one core: make
# BASETAB_TOMLA=<its TOML A>). `descrec.py basetab` writes basetab.h, the table descrec.cc refuses a [[window]] against, into $(OUTPUT)/io before any source is compiled, the file only
# when its content changes. Nothing is read at run time.
BASETAB_TOMLA ?= $(PATH_SW)/../../coreapp/gateware/host.toml
BASETAB_DIR    = $(OUTPUT)/io
BASETAB_GEN   := $(shell python3 $(PATH_SW)/../../tools/descrec.py basetab $(BASETAB_TOMLA) $(BASETAB_DIR) 2>&1 || echo "basetab: the table of the base blocks was not generated")
ifneq ($(strip $(BASETAB_GEN)),)
$(error $(BASETAB_GEN))
endif
VPATH += $(BASETAB_DIR)

# T239.3 P3-1 (D31): the libraries. The app is linked with the libraries its TOML B names (the shared app: coreapp/libs/shared.toml); `libtab.py table` writes the library table
# (libtab_table.cc: what the generic application calls at the start, by the entries the TOML B names) and libtab.mk (LIB_SRCS_CC: the sources of those libraries) into
# $(OUTPUT)/lib, each file only when its content changes. The sources of a library are not in SRCS_CC above: an app whose TOML B leaves a library out has none of its code.
LIB_TOMLB = $(PATH_SW)/../../coreapp/libs/shared.toml
LIB_DIR   = $(OUTPUT)/lib
LIB_GEN  := $(shell python3 $(PATH_SW)/../../tools/libtab.py table $(LIB_TOMLB) $(LIB_DIR) 2>&1 || echo "libtab: the library table was not generated")
ifneq ($(strip $(LIB_GEN)),)
$(error $(LIB_GEN))
endif
include $(LIB_DIR)/libtab.mk

# T239.3 P3-2 (D31): the maxima of the registry, the contributions, the report and the record are the [sizes] table of the TOML B (empty here: the headers' defaults). `app_build.py sizes`
# writes appsizes.h, and every object is compiled with it (OPTIONS below), so the objects that tools/app_build.py sized names and the ones of the SDK package agree by construction.
SIZES_DIR = $(OUTPUT)/sizes
SIZES_GEN := $(shell python3 $(PATH_SW)/../../tools/app_build.py sizes $(LIB_TOMLB) $(SIZES_DIR) 2>&1 || echo "app_build: the sizes were not generated")
ifneq ($(strip $(SIZES_GEN)),)
$(error $(SIZES_GEN))
endif
SRCS_CC += $(LIB_SRCS_CC) libtab.cc libtab_table.cc
SRCS_C  += $(LIB_SRCS_C)
VPATH += $(LIB_DIR)

SRCS_ASM =
SRCS_ASMS = crt0.S port_asm.S
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-div
OPTIONS += -gdwarf-2 -Os -DRISCV -DU64=2 -DUSB2513 -DOS -DCLOCK_FREQ=100000000 -Wno-write-strings -DFP_SUPPORT=1
# coreapp/ffconf first: FatFs finds its ffconf.h there (the original with FF_USE_FASTSEEK on, T104 5.16), before chan_fat/full and hostapp
OPTIONS += -I$(PATH_SW)/../../coreapp/ffconf
OPTIONS += -DFLASH_EXTRA_PROTECTION=1 -DTRX_HOSTAPP=1 -DTRX_COREAPP=1 -I$(PATH_SW)/hostapp
OPTIONS += -I$(PATH_SW)/lvgl -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
# T239.3 D15: heap_5 with one region defined at the first allocation (coreapp/src/heapcfg.c); its size is the heap word of the image, set per core by the packer
OPTIONS += -include $(SIZES_DIR)/appsizes.h    # T239.3 P3-2: the [sizes] of the TOML B, see above
OPTIONS += $(COREAPP_EXTRA)     # #93: -DLISTGROW_START=40 -DLISTGROW_HARD=60 for the mock-up image that shows the "more entries not shown" line; never for a board
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LWIP_A  = ../../../libs/riscv/lwip/result/liblwip.a
LVGL_A  = ../../../libs/riscv/lvgl/result/liblvgl.a
LIBS    = $(LWIP_A)
LIBS   += $(LVGL_A)
# T239.3 V-3 (D30, D31): IOBASE, U2P_IO_BASE and the icap registers (iomap.h, u2p.h, hw.cc) are linker symbols, none is a -D or a literal here. They come from the TOML A of the host
# bitstream (coreapp/gateware/host.toml) through `descrec.py ldfrag`, which writes $(OUTPUT)/io/iofrag.ld before any source is compiled, the file only when its content changes. An app of
# one core links the ldfrag of its own TOML A: make IOFRAG=<its .ld>.
ifndef IOFRAG
HOST_TOMLA = $(PATH_SW)/../../coreapp/gateware/host.toml
IOFRAG     = $(OUTPUT)/io/iofrag.ld
IOFRAG_GEN := $(shell mkdir -p $(OUTPUT)/io && python3 $(PATH_SW)/../../tools/descrec.py ldfrag $(HOST_TOMLA) $(IOFRAG).new --maps $(PATH_SW)/../../app/sdkpkg/maps 2>&1 && { cmp -s $(IOFRAG).new $(IOFRAG) || cp $(IOFRAG).new $(IOFRAG); } || echo "iofrag: the link fragment was not generated")
ifneq ($(strip $(IOFRAG_GEN)),)
$(error $(IOFRAG_GEN))
endif
endif
LIBS    += $(IOFRAG)
LFLAGS   = --gc-sections --wrap=pvPortMalloc --wrap=_sbrk --wrap=outbyte --wrap=malloc --wrap=_malloc_r --wrap=free --wrap=_free_r --wrap=realloc --wrap=_realloc_r --wrap=calloc --wrap=_calloc_r
LINKMETHOD = ld

VERSION_COMPONENT = coreapp          # T221: ci/version.sh <component> names the SemVer file the TRXVSTAMP field starts from
include ../../../common/rules.mk

$(OUTPUT)/$(PRJ).out: $(IOFRAG)

# T239.3 P3-2 (D31): what the SDK package takes from this build (tools/sdk_pack.sh --base-dir): `make sdkbase` after the build. The sources whose include closure holds a header that the
# core's TOML A or TOML B decides (tools/app_build.py sized) are compiled again for each core, from the package's sources; the objects of every other source go into one archive
# (libtrxbase.a, linked whole), with the two libraries (lwip, lvgl) and the start-up objects, the linker script and the include path. The registry, the contributions and the report are
# sized per core, so most of the application is in the first group; the vendor drivers, FreeRTOS, FatFs and the sources that touch none of those headers are the archive.
SDKBASE       = $(OUTPUT)/sdkbase
SDK_GENERATED = drvtab.c libtab_table.cc
SDK_START     = crt0.o port_asm.o
SDK_SRCS_ALL  = $(filter-out $(SDK_GENERATED) $(LIB_SRCS_CC) $(LIB_SRCS_C),$(SRCS_C) $(SRCS_CC))
SDK_PATHS     = $(foreach f,$(SDK_SRCS_ALL),$(firstword $(wildcard $(addsuffix /$(f),$(VPATH)))))
sdkbase: all
	@rm -rf $(SDKBASE)/lib $(SDKBASE)/incdirs.txt $(SDKBASE)/archive.objs
	@mkdir -p $(SDKBASE)/lib
	@# the include path of the build, in order, as real paths: ffconf first, then the directories the sources are found in
	@for d in $(PATH_SW)/../../coreapp/ffconf $(PATH_SW)/lvgl $(PATH_SW)/hostapp $(VPATH); do [ -d "$$d" ] && (cd "$$d" && pwd -P); done | awk '!s[$$0]++' > $(SDKBASE)/incdirs.txt
	@(cd $(PATH_SW)/../.. && pwd -P) > $(SDKBASE)/root.txt
	@python3 $(PATH_SW)/../../tools/app_build.py split $(SDKBASE)/incdirs.txt $(SDKBASE) $(SDK_PATHS)
	@# the code of the cores that ship with the shared app (coreapp/cores/) is no part of the archive: an app links it only when its TOML B names it ([app] base_sources, a library)
	@grep -v '/coreapp/cores/' $(SDKBASE)/unsized.real | sed 's|.*/\([^/]*\)\.[a-z]*$$|$(OUTPUT)/\1.o|' > $(SDKBASE)/archive.objs
	@# the USB controller's program (SRCS_NANO, embedded as an object by rules.mk): usb_base.cc reads its _nano_minimal_b_* symbols
	@for o in $(OBJS_NANO); do echo $(OUTPUT)/$$o; done >> $(SDKBASE)/archive.objs
	@$(AR) rcs $(SDKBASE)/lib/libtrxbase.a `cat $(SDKBASE)/archive.objs`
	@for o in $(SDK_START); do cp $(OUTPUT)/$$o $(SDKBASE)/lib/; done
	@cp $(LWIP_A) $(LVGL_A) $(SDKBASE)/lib/
	@# the shipped objects carry no debug sections: they name the checkout they were compiled in, and an app linked from the package must name none
	@$(CROSS)strip --strip-debug $(SDKBASE)/lib/*.a $(SDKBASE)/lib/*.o
	@cp $(LINK) $(SDKBASE)/linker.x
	@mkdir -p $(SDKBASE)/gen && cp $(OUTPUT)/gitinfo.h $(SDKBASE)/gen/gitinfo.h
	@echo "sdkbase: `wc -l < $(SDKBASE)/sized.real | tr -d ' '` sources are compiled for the core, `wc -l < $(SDKBASE)/archive.objs | tr -d ' '` objects are in libtrxbase.a"

.PHONY: sdkbase

# the hash loop of Reload core (partial.cc) reads the whole partial bitstream: as in the host app
sha256.o: sha256.c
	@echo Compiling $(<F) optimized for speed
	@$(CC) $(COPTIONS) -O3 $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CC) $(COPTIONS) -O3 -MM -MP $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)
