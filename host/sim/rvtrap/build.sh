#!/bin/sh
# Assemble prog.S into prog.hex (needs riscv32-unknown-elf-gcc: container u64-build-env:ci,
# e.g. docker run --rm -v $PWD:/w -w /w u64-build-env:ci sh build.sh). prog.hex is committed;
# run.sh needs only ghdl and python3.
set -e
riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -Wl,-T,link.ld -o prog.elf prog.S
riscv32-unknown-elf-objdump -d prog.elf > prog.lst
riscv32-unknown-elf-objcopy -O binary prog.elf prog.bin
python3 - <<'PY'
b = open("prog.bin", "rb").read()
b += b"\0" * (-len(b) % 4)
with open("prog.hex", "w") as f:
    for i in range(0, len(b), 4):
        f.write("%08x\n" % int.from_bytes(b[i:i+4], "little"))
print("prog.hex:", len(b) // 4, "words")
PY
