# TRX64 boot ROM for rvlite. Runs inside the 1541ultimate build image
# (u64-build-env, riscv32-unknown-elf-gcc); tools/build_bootrom.sh starts it.
CROSS  = riscv32-unknown-elf-
CFLAGS = -march=rv32i -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib \
         -ffunction-sections -fdata-sections -Wall
OUT    = build
PKG    = ../../host/bootrom_pkg.vhd
# T265 S2 negative control: the same ROM without the guard's decision, for the bench only
NOGUARD = ../sim/flash_boot/bootrom_noguard_pkg.vhd

all: $(PKG) $(NOGUARD)

$(OUT)/bootrom.elf: crt0.S bootrom.c linker.ld
	mkdir -p $(OUT)
	$(CROSS)gcc $(CFLAGS) -T linker.ld -Wl,--gc-sections -o $@ crt0.S bootrom.c
	$(CROSS)size $@

$(OUT)/bootrom.bin: $(OUT)/bootrom.elf
	$(CROSS)objcopy -O binary $< $@

$(PKG): $(OUT)/bootrom.bin mkpkg.py
	python3 mkpkg.py $< $@

$(OUT)/noguard.elf: crt0.S bootrom.c linker.ld
	mkdir -p $(OUT)
	$(CROSS)gcc $(CFLAGS) -DTRX_GUARD=0 -T linker.ld -Wl,--gc-sections -o $@ crt0.S bootrom.c
	$(CROSS)size $@

$(OUT)/noguard.bin: $(OUT)/noguard.elf
	$(CROSS)objcopy -O binary $< $@

$(NOGUARD): $(OUT)/noguard.bin mkpkg.py
	python3 mkpkg.py $< $@

clean:
	rm -rf $(OUT)
