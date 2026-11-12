# T49: the blob region of a TRX64 Ultimate update package, one file built by tools/trx_pkg.py:
# trx_manifest, bitstream, app, (config pages in a restore bundle), each 4-aligned, offsets in the manifest.
# The region is the tail of the image (target/u64ii/riscv/update_trx/linker.x) and begins with the manifest.
# apps/softboot/src/softboot.c find_probe/find_app need the bitstream followed by the app within 16 bytes.
.section ".trxblobs","a"
.align 2
.incbin "trx_blobs.bin"
