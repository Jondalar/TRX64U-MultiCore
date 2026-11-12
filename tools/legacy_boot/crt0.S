/* Start-up of the legacy-boot PoC: runs from DDR2 at 0x02800000, started by the boot ROM's
   JTAG load (tools/trxdbg.sh riscv). Interrupts off, bss cleared, stack at the top of its 64 KB. */
    .section .text.crt0
    .global _start
_start:
    csrw    mie, zero
    csrw    mstatus, zero
    la      sp, __stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:  call    main
3:  j       3b
