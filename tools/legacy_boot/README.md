# legacy_boot — PoC of T115 (docs/specs/done/T115-full-bitstream-cores.md)

Starts Gideon's complete firmware from a vanilla `.cfw` on a board with our host in flash. Bare-metal RISC-V,
loaded over JTAG into the running host; built with `riscv32-unknown-elf-gcc` (u64-build-env, `make`).

1. Gideon's 50T `.bit` (from the `.cfw`) into the flash drive as one file, e.g. `curl -T G315A50T.BIT ftp://<board>/Flash/`.
2. `tools/trxdbg.sh ddrload <vanilla.cfw> 0x01000000`, the parameter block (see `legacy_boot.c`) at `0x02810000`.
3. `tools/trxdbg.sh riscv build/legacy_boot.bin 0x02800000` — mode 0 checks only, mode 1 sends IPROG.

`g4.py` drives Gideon's USER4 chain (console, memory) over our USB-Blaster: IR `USER4` again before every data scan,
as his `recover.py` does. `measure.py` / `verify.py` measured the DDR2 address map after a mode-3 run (T115 section 3).
