# Build path of an application of several sources, C and C++ (T104.3): the same image rules as lib.mk (rv32im, no compressed instructions, position independent
# without a dynamic linker, linked at 0 and relocated by the loader), for an application written as C files against app/sdk/u2p_app.h with a C++ glue.
#
#   APP_NAME = unzip          APP_VERSION = 1.0           APP_RES = transient | resident        APP_MENU = app.toml (optional)
#   APP_REG = 1               an application with screens: it has lib_register, the operations and state values they bind to
#   APP_CSRCS = src/unzip.c ...                           APP_CCSRCS = glue.cc                  APP_INC = -Isrc/miniz
#   include <repo>/app/lib/app.mk
#
# The compiler is the build image's (make RV_PREFIX=riscv32-unknown-elf-) or riscv64-elf-*. Division is app/sdk/libc.c's (-mno-div; libgcc's is not position independent).
APP_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
REPO     := $(abspath $(APP_DIR)../..)
RV_PREFIX ?= riscv32-unknown-elf-
CC_RV    ?= $(RV_PREFIX)gcc
CXX_RV   ?= $(RV_PREFIX)g++
LD_RV    ?= $(RV_PREFIX)ld
APP_VERSION ?= 1
APP_RES  ?= transient
APP_OUT  ?= $(APP_NAME).u2a
SDK      := $(REPO)/app/sdk
CORE_SRC := $(REPO)/coreapp/src
COMMON   = -march=rv32im -mabi=ilp32 -mno-div -Os -ffreestanding -fPIE -mcmodel=medany -mno-relax -msmall-data-limit=0 -ffunction-sections -fdata-sections \
           -fvisibility=hidden -include $(SDK)/hidden.h -Wall -I$(SDK) -I$(CORE_SRC) $(APP_INC)
CFLAGS_RV   = $(COMMON) -fno-builtin -fno-asynchronous-unwind-tables
CXXFLAGS_RV = $(COMMON) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
OBJS     = $(addprefix obj/,$(APP_CSRCS:.c=.o) $(APP_CCSRCS:.cc=.o) sdk/u2p_compat.o sdk/libc.o)
DEPMENU  = $(if $(APP_MENU),$(APP_MENU))

all: $(APP_OUT)

obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC_RV) $(CFLAGS_RV) -c $< -o $@

obj/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX_RV) $(CXXFLAGS_RV) -c $< -o $@

obj/sdk/%.o: $(SDK)/%.c
	@mkdir -p $(dir $@)
	$(CC_RV) $(CFLAGS_RV) -c $< -o $@

$(APP_NAME).elf: $(OBJS) $(APP_DIR)lib.ld
	$(LD_RV) -m elf32lriscv --emit-relocs --gc-sections -e app_main $(if $(APP_REG),-u lib_register) -T $(APP_DIR)lib.ld $(OBJS) -o $@

$(APP_OUT): $(APP_NAME).elf $(DEPMENU)
	python3 $(REPO)/tools/u2a_pack.py $< -o $@ --kind app --residence $(APP_RES) --name $(APP_NAME) --version $(APP_VERSION) $(if $(APP_MENU),--menu $(APP_MENU))

clean:
	rm -rf obj $(APP_NAME).elf $(APP_OUT)

.PHONY: all clean
