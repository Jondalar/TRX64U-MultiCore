# Build path of a library or application image (T104.1 D10). Include it and name the pieces:
#
#   LIB_NAME = testlib        LIB_VERSION = 1       LIB_NEEDS = "base>=3.0"      LIB_KIND = lib
#   LIB_SRCS = testlib.cc     LIB_CAP =             (LIB_RES = resident|transient; LIB_MANIFEST = manifest.json)
#   include <repo>/app/lib/lib.mk
#
# The compiler is the RISC-V one of the build image (make RV_PREFIX=riscv32-unknown-elf-, what ci/trxapps.sh does) or riscv64-elf-* (the
# default, a Mac with the rv32 multilib). The CPU is the application's: rv32im with no divider and NO compressed instructions
# (CPU_EXTENSION_RISCV_C is false in the cpu_unit wrapper, the core application builds -march=rv32im -mno-div), so an image built with
# the C extension would trap on its first instruction. The code is
# position independent without a dynamic linker: -fPIE -mcmodel=medany -mno-relax makes every jump and data access pc-relative, and
# the only absolute words left are the pointers in tables, which `ld --emit-relocs` keeps as R_RISCV_32 relocations for the packer.
# (A bare-metal binutils does not build -static-pie; where one does, the packer reads its R_RISCV_RELATIVE table the same way.)
LIB_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
RV_PREFIX ?= riscv64-elf-
CXX_RV   ?= $(RV_PREFIX)g++
LD_RV    ?= $(RV_PREFIX)ld
LIB_KIND ?= lib
LIB_RES  ?= $(if $(filter app,$(LIB_KIND)),transient,resident)
LIB_OUT  ?= $(LIB_NAME).u2a
LIB_BASE ?= 0
CORE_SRC ?= $(LIB_DIR)../../coreapp/src
LIB_CXXFLAGS = -march=rv32im -mabi=ilp32 -mno-div -Os -ffreestanding -fPIE -mcmodel=medany -mno-relax -msmall-data-limit=0 \
               -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables -fvisibility=hidden \
               -std=gnu++11 -I$(CORE_SRC)

all: $(LIB_OUT)

$(LIB_NAME).o: $(LIB_SRCS)
	$(CXX_RV) $(LIB_CXXFLAGS) -c $< -o $@

$(LIB_NAME).elf: $(LIB_NAME).o $(LIB_DIR)lib.ld
	$(LD_RV) -m elf32lriscv --emit-relocs --defsym=__lib_base=$(LIB_BASE) -T $(LIB_DIR)lib.ld $< -o $@

$(LIB_OUT): $(LIB_NAME).elf
	python3 $(LIB_DIR)../../tools/u2a_pack.py $< -o $@ --kind $(LIB_KIND) --residence $(LIB_RES) --name $(LIB_NAME) \
	    --version $(LIB_VERSION) $(foreach n,$(LIB_NEEDS),--needs '$(n)') $(if $(LIB_CAP),--capability $(LIB_CAP)) \
	    $(if $(LIB_MANIFEST),--manifest $(LIB_MANIFEST)) --link-base $(LIB_BASE)

clean:
	rm -f $(LIB_NAME).o $(LIB_NAME).elf $(LIB_OUT)

.PHONY: all clean
