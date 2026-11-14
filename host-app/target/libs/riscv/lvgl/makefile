# LVGL (software/lvgl, pinned copy; software/lvgl_port/lv_conf.h) as a static
# library for the RISC-V applications (TRX64-Ultimate TRX64-Ultimate/docs/specs/T24-lvgl-ui.md).
#
# Every C file under software/lvgl/src is compiled; what lv_conf.h turns off
# compiles to nothing, and --gc-sections in the application drops what is not
# called. Objects keep their directory under output/, since LVGL has file names
# that occur in more than one directory.

CROSS   = riscv32-unknown-elf-
CC      = $(CROSS)gcc
AR      = $(CROSS)ar

# Same code model as target/u64ii/riscv/ultimate (rv32im, no divider, soft float).
MARCH  ?= rv32im
MABI   ?= ilp32

PATH_SW = ../../../../software
LVGL    = $(PATH_SW)/lvgl
# the host app's own lv_conf.h (host-app/host-app)
PORT    = $(PATH_SW)/hostapp

OUTPUT  = output
RESULT  = result
PRJ     = liblvgl

CFLAGS  = -march=$(MARCH) -mabi=$(MABI) -mno-div -O2 -std=gnu99 \
          -ffunction-sections -fdata-sections \
          -DLV_CONF_INCLUDE_SIMPLE -I$(PORT) -I$(LVGL)

SRCS   := $(shell cd $(LVGL) && find src -name '*.c' | sort)
OBJS   := $(addprefix $(OUTPUT)/,$(SRCS:.c=.o))
DEPS   := $(OBJS:.o=.d)

.PHONY: all clean
all: $(RESULT)/$(PRJ).a

$(RESULT)/$(PRJ).a: $(OBJS)
	@mkdir -p $(RESULT)
	@rm -f $@
	@$(AR) rcs $@ $(OBJS)
	@echo Library $@: $(words $(OBJS)) objects

$(OUTPUT)/%.o: $(LVGL)/%.c $(PORT)/lv_conf.h
	@mkdir -p $(dir $@)
	@echo Compiling LVGL $*.c
	@$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

clean:
	@rm -rf $(OUTPUT) $(RESULT)

-include $(DEPS)
