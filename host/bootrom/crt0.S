/* Start-up of the TRX64 boot ROM: interrupts off, stack at the top of the
   8 KB boot RAM, .bss cleared (the ROM can be restarted by a CPU reset over
   JTAG, so nothing may rely on power-on contents), then main. */
    .section .text.crt0
    .global _start
_start:
    csrw    mie, zero
    csrw    mstatus, zero
    la      t0, trap
    csrw    mtvec, t0
    la      sp, __stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:  call    main
3:  j       3b

    /* no trap is expected in the ROM: park here, visible as a PC in 0x8000xxxx */
    .align  2
trap:
    j       trap
