CROSS  = riscv32-unknown-elf-
CFLAGS = -march=rv32i -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -Wall
OUT    = build
all: $(OUT)/legacy_boot.bin
$(OUT)/legacy_boot.elf: crt0.S legacy_boot.c linker.ld
	mkdir -p $(OUT)
	$(CROSS)gcc $(CFLAGS) -T linker.ld -o $@ crt0.S legacy_boot.c -lgcc
	$(CROSS)size $@
$(OUT)/legacy_boot.bin: $(OUT)/legacy_boot.elf
	$(CROSS)objcopy -O binary $< $@
clean:
	rm -rf $(OUT)
