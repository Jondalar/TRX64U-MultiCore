/* rvlite boot BRAM: 8 KB at the reset vector 0x80000000, code and data alike. */
MEMORY
{
    rom : ORIGIN = 0x80000000, LENGTH = 0x2000
}
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    .text : {
        *(.text.crt0)
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
    } > rom
    .data : { *(.data .data.* .sdata .sdata.*) } > rom
    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > rom
    __stack_top = ORIGIN(rom) + LENGTH(rom);
    /DISCARD/ : { *(.comment) *(.riscv.attributes) }
}
