CROSS  = riscv32-unknown-elf-
CFLAGS = -march=rv32i -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -Wall
all: build/probe.bin
build/probe.elf: crt0.S probe.c linker.ld
	mkdir -p build
	$(CROSS)gcc $(CFLAGS) -T linker.ld -o $@ crt0.S probe.c -lgcc
	$(CROSS)size $@
build/probe.bin: build/probe.elf
	$(CROSS)objcopy -O binary $< $@
