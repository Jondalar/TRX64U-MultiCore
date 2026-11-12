# T79 ICAP loader test program for rvlite, built in the 1541ultimate build image
# (u64-build-env, riscv32-unknown-elf-gcc): tools/build_icap_test.sh. Same flags as the boot ROM.
CROSS  = riscv32-unknown-elf-
CFLAGS = -march=rv32i -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -Wall
OUT    = build

all: $(OUT)/icap_test.bin

$(OUT)/icap_test.elf: crt0.S icap_test.c linker.ld
	mkdir -p $(OUT)
	$(CROSS)gcc $(CFLAGS) -T linker.ld -o $@ crt0.S icap_test.c
	$(CROSS)size $@

$(OUT)/icap_test.bin: $(OUT)/icap_test.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -rf $(OUT)
