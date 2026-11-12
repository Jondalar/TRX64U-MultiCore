/* T44 board test helper (tools/flashgate_test.py): runs on the RISC-V in
   place of the app, loaded over JTAG to 0x30000 by the boot ROM's magic, and
   carries out lists of accesses to the flash SPI window 0x10060200 that the
   host puts into DDR2. rv32i, no stack, no interrupts.

   Mailbox at 0x38000:
     +0x000  seq      host: a new list is there when seq != done
     +0x004  done     helper: seq of the last list carried out
     +0x008  ready    helper: 0x464C4741 once running
     +0x00C  status   helper: 0 = list done, 1 = stopped by a guard
     +0x010  nres     helper: bytes stored at +0x2000
     +0x100  ops      one word each, 0 ends the list:
                      [31:24] 1 = write [7:0] to window offset [12:8]
                              2 = read window offset [12:8], [23:16]+1 times
                              3 = guard: unless (last byte read & [15:8]) == [23:16],
                                  send WRDI (CTRL 0, 0x04), CTRL 3 (CS high) and
                                  stop the list
     +0x2000  results, one byte per read */
    .equ MBOX,   0x38000
    .equ WINDOW, 0x10060200
    .equ READY,  0x464C4741

    .section .text
    .global _start
_start:
    csrw    mie, zero
    li      s0, MBOX
    li      s1, WINDOW
    li      t0, READY
    sw      t0, 8(s0)
idle:
    lw      t0, 0(s0)               /* seq */
    lw      t1, 4(s0)               /* done */
    beq     t0, t1, idle
    addi    a0, s0, 0x100           /* op pointer */
    li      a1, MBOX + 0x2000       /* result pointer */
    li      a2, 0                   /* last byte read */
    li      a3, 0                   /* status */
next:
    lw      t2, 0(a0)
    addi    a0, a0, 4
    srli    t3, t2, 24              /* kind */
    beqz    t3, finish
    srli    t4, t2, 8
    andi    t4, t4, 0x1F
    add     t4, t4, s1              /* register address */
    li      t5, 1
    beq     t3, t5, op_write
    li      t5, 2
    beq     t3, t5, op_read
    li      t5, 3
    beq     t3, t5, op_guard
    j       next                    /* unknown kind: skipped */
op_write:
    sb      t2, 0(t4)
    j       next
op_read:
    srli    t5, t2, 16
    andi    t5, t5, 0xFF
    addi    t5, t5, 1               /* count */
1:  lbu     a2, 0(t4)
    sb      a2, 0(a1)
    addi    a1, a1, 1
    addi    t5, t5, -1
    bnez    t5, 1b
    j       next
op_guard:
    srli    t5, t2, 8
    andi    t5, t5, 0xFF            /* mask */
    and     t5, t5, a2
    srli    t6, t2, 16
    andi    t6, t6, 0xFF            /* expected */
    beq     t5, t6, next
    sb      zero, 8(s1)             /* stop: CTRL 0, WRDI in its own CS pulse, */
    li      t5, 0x04
    sb      t5, 0(s1)
    li      t5, 3
    sb      t5, 8(s1)               /* CTRL 3: CS high */
    li      a3, 1
finish:
    li      t5, MBOX + 0x2000
    sub     t5, a1, t5
    sw      t5, 16(s0)
    sw      a3, 12(s0)
    sw      t0, 4(s0)               /* done = seq, last */
    j       idle
