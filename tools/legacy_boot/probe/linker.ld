MEMORY
{
    ram : ORIGIN = 0x00030000, LENGTH = 0x10000
}
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    .text : { *(.text.crt0) *(.text .text.*) *(.rodata .rodata.* .srodata .srodata.*) } > ram
    .data : { *(.data .data.* .sdata .sdata.*) } > ram
    .bss  : { __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) __bss_end = .; } > ram
    __stack_top = ORIGIN(ram) + LENGTH(ram);
    /DISCARD/ : { *(.comment) *(.riscv.attributes) }
}
