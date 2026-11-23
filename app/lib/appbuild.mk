# The app of one core, linked from the SDK package (T239.3 D31, P3-2). Run by `python3 tools/app_build.py build` (never by hand): the working directory is the build's
# output directory, which holds what the tool generated from the core's TOML A and TOML B:
#   build.mk       SDK, SIZED_SRCS (the sources of the package's base/ that depend on the core), SRC_DIRS (the core's own source directories), LIB_SRCS (the sources of the
#                  libraries its TOML B names), OWN_SRCS ([app] sources), BASE_SOURCES ([app] base_sources), CORE_DESC ([app] desc, empty for a core described by its record)
#   appsizes.h     the maxima of its [sizes] table                       drv/drvtab.h, drv/drvtab.c   the driver table of its TOML A
#   lib/libtab_table.cc  the library table of its TOML B                 io.ld        the I/O addresses of its TOML A as linker symbols
# What the package holds, beside this file (mk/):
#   base/src/<repository path>/  the sources that depend on the core, and the sources of the shared libraries     base/inc/<repository directory>/  the headers of the build's include path
#   base/SIZED.txt, base/incdirs.txt, base/linker.x     lib/libtrxbase.a, liblwip.a, liblvgl.a, crt0.o, port_asm.o   (the objects of every source that depends on nothing the core decides)
# The options are mk/appflags.mk's, which is the core app Makefile's (tools/app_build.py flags). Objects are named by their source's stem, as in the tree's build.
SDK ?= $(error SDK is the package directory: run tools/app_build.py build)
include $(SDK)/mk/appflags.mk
include build.mk

BASE        := $(SDK)/base
INCDIRS     := $(addprefix $(BASE)/inc/,$(shell cat $(BASE)/incdirs.txt))
PATH_INC    := -I. -Idrv -Ilib $(addprefix -I,$(INCDIRS))
OPTIONS     += -include appsizes.h
# the sources of the cores that ship with the shared app (coreapp/cores/) are not part of an app unless its TOML B says so ([app] base_sources); the libraries' sources are named by LIB_SRCS
IS_CORE      = $(filter src/coreapp/cores/%,$1)
SIZED_USED   = $(foreach s,$(SIZED_SRCS),$(if $(call IS_CORE,$(s)),$(if $(filter $(notdir $(s)),$(BASE_SOURCES)),$(s)),$(s)))
BASE_SRCS    = $(addprefix $(BASE)/,$(SIZED_USED))
BASE_DIRS    = $(sort $(dir $(shell find $(BASE)/src -type f)))
vpath %.cc $(SRC_DIRS) $(BASE_DIRS)
vpath %.c  $(SRC_DIRS) $(BASE_DIRS)

OBJ_NAMES    = $(sort $(notdir $(basename $(BASE_SRCS))) $(basename $(LIB_SRCS)) $(basename $(OWN_SRCS)) libtab_table)
OBJS         = $(addprefix obj/,$(addsuffix .o,$(OBJ_NAMES))) obj/drvtab.o
APPS         = app.elf app.bin

all: $(APPS)
	@$(SIZE) app.elf

obj:
	@mkdir -p obj

obj/%.o: %.cc | obj
	@echo Compiling $(<F)
	@$(CPP) $(CPPOPT) $(PATH_INC) -MMD -MP -c -o $@ $<

obj/%.o: %.c | obj
	@echo Compiling $(<F)
	@$(CC) $(COPTIONS) $(PATH_INC) -MMD -MP -c -o $@ $<

# the generated files are in this directory
obj/libtab_table.o: lib/libtab_table.cc | obj
	@$(CPP) $(CPPOPT) $(PATH_INC) -MMD -MP -c -o $@ $<
obj/drvtab.o: drv/drvtab.c | obj
	@$(CC) $(COPTIONS) $(PATH_INC) -MMD -MP -c -o $@ $<

# the core table holds the one core of this app (the core app Makefile's target `own` does the same for trx64u's image); [app] desc names its descriptor symbol. A core that
# is described by its record (D17) has no descriptor in the app: its table is empty and the start makes the descriptor from the bundle's record
obj/table.o: CPPOPT += -UCORE_OWN_IMAGE_C64 $(if $(CORE_DESC),-DCORE_ONLY=$(CORE_DESC),-DCORE_NONE)

# sha256 is optimised for speed in the tree's build as well (the hash loop of Reload core)
obj/sha256.o: COPTIONS += -O3

# The base's drivers register themselves through global constructors in objects that nothing references (usb_hub, usb_scsi, usb_hid, usb_xinput, filesystem_fat, wifi):
# libtrxbase.a is linked whole, as the tree links its objects, and --gc-sections drops what is unused. libc and libgcc are the toolchain's, picked by LIBMARCH as the tree's ld.mk does.
LIBC_A    = $(shell $(CC) -march=$(LIBMARCH) -mabi=$(MABI) -print-file-name=libc.a)
LIBGCC_A  = $(shell $(CC) -march=$(LIBMARCH) -mabi=$(MABI) -print-libgcc-file-name)

app.elf: $(OBJS) $(SDK)/base/linker.x io.ld
	@echo Linking $@
	@$(LD) $(LFLAGS) -T $(SDK)/base/linker.x -Map=app.map -o $@ $(SDK)/lib/crt0.o $(SDK)/lib/port_asm.o $(OBJS) \
	    --whole-archive $(SDK)/lib/libtrxbase.a --no-whole-archive \
	    --start-group $(SDK)/lib/liblwip.a $(SDK)/lib/liblvgl.a $(LIBC_A) $(LIBGCC_A) --end-group io.ld

app.bin: app.elf
	@$(OBJCOPY) -O binary $< $@

-include $(OBJS:.o=.d)
.PHONY: all
