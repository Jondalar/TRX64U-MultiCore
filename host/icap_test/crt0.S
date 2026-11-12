/* Start-up of the ICAP loader test (T79): runs from DDR2 at 0x30000, started by the boot ROM's
   JTAG load (tools/trxdbg.sh riscv). Interrupts off, stack below 0x40000, then main. */
    .section .text.crt0
    .global _start
_start:
    csrw    mie, zero
    csrw    mstatus, zero
    la      sp, __stack_top
    call    main
1:  j       1b
