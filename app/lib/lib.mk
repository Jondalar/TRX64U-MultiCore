# Build path of a library or application image (T104.1 D10). Include it and name the pieces:
#
#   LIB_NAME = testlib        LIB_VERSION = 1       LIB_NEEDS = "base>=3.0"      LIB_KIND = lib
#   LIB_SRCS = testlib.cc     LIB_CAP =             (LIB_RES = resident|transient; LIB_MANIFEST = manifest.json)
#   include <repo>/app/lib/lib.mk          (in the SDK package: include <package>/mk/lib.mk)
#
# LIB_BUDGET = image=8192 heap_keep=2048 heap_peak=4096 ops=3 settings=2 declares what the library will take (T239.3 S13): image (code, data and bss, bytes), heap_keep (bytes it holds
# through api->mem when idle; the loader refuses the library when image and this figure do not fit the room of the heap), heap_peak (the most it holds at once: a ceiling), ops, states, events, nodes, file_actions, settings, tabs and polls (what lib_register and the menu record bring). The packer refuses an image over its own
# image figure; the loader refuses the library at load when anything is over, naming the figure and the limit. A figure left out is the platform's limit. With a budget
# the file needs base >= 3.2 (the packer sees to it).
#
# LIB_EXPORTS = ring=1 trx=1 lists the names the library exports with the major of each table (LibRecord export_list, api.md section 9): the file carries them so that a folder of libraries is
# loaded with the exporter in front of whatever needs that name (LIB_NEEDS = "ring>=1"); the packer adds `base>=3.2`.
#
# The packer refuses an image with a constructor or a destructor (D19): a non-empty .init_array, .fini_array, .ctors or .dtors, or a reference to __cxa_atexit or atexit (a global object
# with a constructor or destructor brings one). The loader runs none; initialise in lib_register. The check runs on the objects in front of ld (u2a_pack.py --check-objects): ld
# itself would stop on the undefined __cxa_atexit of a destructor before the packer saw the image, so make shows the packer's message, not ld's.
#
# LIB_SRCS may list any number of .cc and .c files; each one is compiled (with hidden.h, C and C++: -fvisibility=hidden leaves extern declarations alone,
# so a call or a data reference to another file would go through the PLT and GOT, which the loader does not fill) and all are linked into one image. LIB_MENU = menu.toml carries the library's
# menu record in the image (tools/descrec.py). LIB_INC = -Idir ... adds include paths. LIB_LIBC = 1 links the memcpy/memset/division of sdk/libc.c.
#
# The compiler is the RISC-V one of the build image (make RV_PREFIX=riscv32-unknown-elf-, what ci/trxapps.sh does). It needs the newlib headers:
# the public headers include <stdlib.h> and <string.h> (fsif.h), so a riscv64-elf-* with an rv32 multilib but no libc headers (the default below,
# a Homebrew Mac) does not compile them. The CPU is the application's: rv32im with no divider and NO compressed instructions
# (CPU_EXTENSION_RISCV_C is false in the cpu_unit wrapper, the core application builds -march=rv32im -mno-div), so an image built with
# the C extension would trap on its first instruction. The code is
# position independent without a dynamic linker: -fPIE -mcmodel=medany -mno-relax makes every jump and data access pc-relative, and
# the only absolute words left are the pointers in tables, which `ld --emit-relocs` keeps as R_RISCV_32 relocations for the packer.
# (A bare-metal binutils does not build -static-pie; where one does, the packer reads its R_RISCV_RELATIVE table the same way.)
LIB_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
include $(LIB_DIR)sdkroot.mk
RV_PREFIX ?= riscv64-elf-
CXX_RV   ?= $(RV_PREFIX)g++
LD_RV    ?= $(RV_PREFIX)ld
LIB_KIND ?= lib
LIB_RES  ?= $(if $(filter app,$(LIB_KIND)),transient,resident)
LIB_OUT  ?= $(LIB_NAME).u2a
LIB_BASE ?= 0
# No sibling calls: the base takes the account of an allocation from the return address of the call (libload.cc owner_at), which a tail call would replace by the caller's.
LIB_CXXFLAGS = -march=rv32im -mabi=ilp32 -mno-div -Os -ffreestanding -fPIE -mcmodel=medany -mno-relax -msmall-data-limit=0 \
               -fno-optimize-sibling-calls -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables -fvisibility=hidden \
               -std=gnu++11 $(SDK_INC) $(LIB_INC) -include $(SDK_HIDDEN)
LIB_CFLAGS   = -march=rv32im -mabi=ilp32 -mno-div -Os -ffreestanding -fPIE -mcmodel=medany -mno-relax -msmall-data-limit=0 -fno-builtin -fno-optimize-sibling-calls \
               -fno-asynchronous-unwind-tables -fvisibility=hidden $(SDK_INC) $(LIB_INC) -include $(SDK_HIDDEN)
CC_RV    ?= $(RV_PREFIX)gcc
LIB_OBJS = $(addsuffix .o,$(basename $(LIB_SRCS))) $(if $(LIB_LIBC),libc_sdk.o)

all: $(LIB_OUT)

%.o: %.cc
	$(CXX_RV) $(LIB_CXXFLAGS) -MMD -MP -c $< -o $@

%.o: %.c
	$(CC_RV) $(LIB_CFLAGS) -MMD -MP -c $< -o $@

libc_sdk.o: $(SDK_SRC)/libc.c
	$(CC_RV) $(LIB_CFLAGS) -MMD -MP -c $< -o $@

$(LIB_NAME).elf: $(LIB_OBJS) $(LIB_DIR)lib.ld
	python3 $(SDK_TOOLS)/u2a_pack.py --check-objects $(LIB_OBJS)
	$(LD_RV) -m elf32lriscv --emit-relocs --defsym=__lib_base=$(LIB_BASE) -T $(LIB_DIR)lib.ld $(LIB_OBJS) -o $@

$(LIB_OUT): $(LIB_NAME).elf $(LIB_MENU)
	python3 $(SDK_TOOLS)/u2a_pack.py $< -o $@ --kind $(LIB_KIND) --residence $(LIB_RES) --name $(LIB_NAME) \
	    --version $(LIB_VERSION) $(foreach n,$(LIB_NEEDS),--needs '$(n)') $(if $(LIB_CAP),--capability $(LIB_CAP)) \
	    $(if $(LIB_MANIFEST),--manifest $(LIB_MANIFEST)) $(if $(LIB_MENU),--menu $(LIB_MENU)) $(foreach b,$(LIB_BUDGET),--budget '$(b)') $(foreach x,$(LIB_EXPORTS),--export '$(x)') --link-base $(LIB_BASE)

clean:
	rm -f $(LIB_OBJS) $(LIB_OBJS:.o=.d) $(LIB_NAME).elf $(LIB_OUT)

-include $(LIB_OBJS:.o=.d)

.PHONY: all clean
