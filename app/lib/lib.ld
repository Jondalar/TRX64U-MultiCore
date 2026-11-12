/* Linker script of a library or an application image (T104.1 D10). The image is linked at 0, so an offset into it is its link-time
   address; the packer (tools/u2a_pack.py) lists every absolute word as a relocation and the loader adds the address it chose. For
   tests the base can be moved with --defsym=__lib_base=0x1000: the result has to equal the image at 0 relocated by 0x1000. */
OUTPUT_FORMAT("elf32-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(lib_register)
SECTIONS
{
    . = DEFINED(__lib_base) ? __lib_base : 0;
    .text : {
        KEEP(*(.text.lib_register))
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    __bss_start = .;
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    __bss_end = .;
    /DISCARD/ : { *(.eh_frame .eh_frame_hdr .comment .note.* .got .got.plt .plt .rela.* .interp .dynamic .dynsym .dynstr) }
}
