# Build path of an application of several sources, C and C++ (T104.3): the same image rules as lib.mk (rv32im, no compressed instructions, position independent
# without a dynamic linker, linked at 0 and relocated by the loader), for an application written as C files against app/sdk/u2p_app.h with a C++ glue.
#
#   APP_NAME = unzip          APP_VERSION = 1.0           APP_RES = transient | resident        APP_MENU = app.toml (optional)
#   APP_KIND = lib            a library (T197): resident, entry lib_register, no u2p_compat; APP_NEEDS = "base>=3.1" "other>=1.0" (space separated, quoted)
#   APP_EXPORTS = ring=1 trx=1                                 (APP_KIND = lib) the names the library exports, as LIB_EXPORTS of lib.mk
#   APP_BUDGET = image=65536 heap_peak=1048576 ops=4          the budget of the file, as LIB_BUDGET of lib.mk (the loader refuses what is over it)
#   APP_REG = 1               an application with screens: it has lib_register, the operations and state values they bind to
#   APP_CSRCS = src/unzip.c ...                           APP_CCSRCS = glue.cc                  APP_INC = -Isrc/miniz
#   include <repo>/app/lib/app.mk          (in the SDK package: include <package>/mk/app.mk)
#
# Tail calls are off (-fno-optimize-sibling-calls), as in lib.mk: a call into api->mem, fs, net or job must carry the return address of this image (owner_at, api.md 4.2.2.2).
# A constructor or a destructor in an object is refused before the link (u2a_pack.py --check-objects, T239.3 D19): the loader runs none.
# The compiler is the build image's (make RV_PREFIX=riscv32-unknown-elf-) or riscv64-elf-*. Division is sdk/libc.c's (-mno-div; libgcc's is not position independent).
APP_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
include $(APP_DIR)sdkroot.mk
RV_PREFIX ?= riscv32-unknown-elf-
CC_RV    ?= $(RV_PREFIX)gcc
CXX_RV   ?= $(RV_PREFIX)g++
LD_RV    ?= $(RV_PREFIX)ld
APP_VERSION ?= 1
APP_KIND ?= app
APP_RES  ?= $(if $(filter lib,$(APP_KIND)),resident,transient)
APP_OUT  ?= $(APP_NAME).u2a
COMMON   = -march=rv32im -mabi=ilp32 -mno-div -Os -ffreestanding -fPIE -mcmodel=medany -mno-relax -msmall-data-limit=0 -ffunction-sections -fdata-sections \
           -fvisibility=hidden -fno-optimize-sibling-calls -include $(SDK_HIDDEN) -Wall $(SDK_INC) $(APP_INC)
CFLAGS_RV   = $(COMMON) -fno-builtin -fno-asynchronous-unwind-tables
CXXFLAGS_RV = $(COMMON) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
OBJS     = $(addprefix obj/,$(APP_CSRCS:.c=.o) $(APP_CCSRCS:.cc=.o) $(if $(filter lib,$(APP_KIND)),,sdk/u2p_compat.o) sdk/libc.o)
DEPMENU  = $(if $(APP_MENU),$(APP_MENU))

all: $(APP_OUT)

obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC_RV) $(CFLAGS_RV) -c $< -o $@

obj/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX_RV) $(CXXFLAGS_RV) -c $< -o $@

obj/sdk/%.o: $(SDK_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC_RV) $(CFLAGS_RV) -c $< -o $@

$(APP_NAME).elf: $(OBJS) $(APP_DIR)lib.ld
	python3 $(SDK_TOOLS)/u2a_pack.py --check-objects $(OBJS)
	$(LD_RV) -m elf32lriscv --emit-relocs --gc-sections -e $(if $(filter lib,$(APP_KIND)),lib_register,app_main) $(if $(APP_REG),-u lib_register) -T $(APP_DIR)lib.ld $(OBJS) -o $@

$(APP_OUT): $(APP_NAME).elf $(DEPMENU)
	python3 $(SDK_TOOLS)/u2a_pack.py $< -o $@ --kind $(APP_KIND) --residence $(APP_RES) --name $(APP_NAME) --version $(APP_VERSION) $(foreach n,$(APP_NEEDS),--needs '$(n)') $(if $(APP_MENU),--menu $(APP_MENU)) $(foreach b,$(APP_BUDGET),--budget '$(b)') $(foreach x,$(APP_EXPORTS),--export '$(x)')

clean:
	rm -rf obj $(APP_NAME).elf $(APP_OUT)

.PHONY: all clean
