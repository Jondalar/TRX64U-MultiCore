# rvlite trap/mret bench program (T97 5.13). Two tasks under a FreeRTOS-style
# trap handler: ecall = yield (context switch), the level interrupt = tick
# (no switch). The handler saves x1,x5-x31 + mstatus on the task stack, logs
# (mcause, mepc, mstatus), and returns through csrw mepc / csrw mstatus / mret
# exactly like portASM. Each task re-checks mstatus.MIE every iteration (the
# interrupt guard of coreapp/src/run.cc) and counts a failure at FAILMIE.
# Results are independent of when interrupts arrive: the testbench compares them
# with a run without interrupts.
    .equ IO_ACK,   0x10000000
    .equ TCB_A,    0x4000
    .equ TCB_B,    0x4010
    .equ CURTCB,   0x4100
    .equ LOGPTR,   0x4104
    .equ FAILMIE,  0x4108
    .equ DONEA,    0x410C
    .equ DONEB,    0x4110
    .equ RESA,     0x4114
    .equ RESB,     0x4118
    .equ LOGSTART, 0x5000
    .equ LOGEND,   0x5C00
    .equ DATA_A,   0x6000
    .equ DATA_B,   0x6400
    .equ STACK_A,  0x7000
    .equ STACK_B,  0x7800
    .equ NITER_A,  260
    .equ NITER_B,  330

    .section .text.vec,"ax"
    .globl _start
_start:
    j init

    .section .text.handler,"ax"
    .align 2
handler:
    addi sp, sp, -128
    sw x1, 4(sp)
    sw x5, 8(sp)
    sw x6, 12(sp)
    sw x7, 16(sp)
    sw x8, 20(sp)
    sw x9, 24(sp)
    sw x10, 28(sp)
    sw x11, 32(sp)
    sw x12, 36(sp)
    sw x13, 40(sp)
    sw x14, 44(sp)
    sw x15, 48(sp)
    sw x16, 52(sp)
    sw x17, 56(sp)
    sw x18, 60(sp)
    sw x19, 64(sp)
    sw x20, 68(sp)
    sw x21, 72(sp)
    sw x22, 76(sp)
    sw x23, 80(sp)
    sw x24, 84(sp)
    sw x25, 88(sp)
    sw x26, 92(sp)
    sw x27, 96(sp)
    sw x28, 100(sp)
    sw x29, 104(sp)
    sw x30, 108(sp)
    sw x31, 112(sp)
    csrr t0, mstatus
    sw   t0, 116(sp)
    li   t1, CURTCB
    lw   t1, 0(t1)
    sw   sp, 0(t1)
    csrr a0, mcause
    csrr a1, mepc
    li   t2, LOGPTR
    lw   t3, 0(t2)
    li   t4, LOGEND
    bgeu t3, t4, 1f
    sw   a0, 0(t3)
    sw   a1, 4(t3)
    sw   t0, 8(t3)
    addi t3, t3, 12
    sw   t3, 0(t2)
1:
    bge  a0, x0, sync
    sw   a1, 0(sp)
    li   t5, IO_ACK
    sb   x0, 0(t5)
    j    restore
sync:
    addi a1, a1, 4
    sw   a1, 0(sp)
    li   t1, CURTCB
    lw   t2, 0(t1)
    xori t2, t2, 0x10
    sw   t2, 0(t1)
restore:
    li   t1, CURTCB
    lw   t1, 0(t1)
    lw   sp, 0(t1)
    lw   t0, 0(sp)
    csrw mepc, t0
    lw   t0, 116(sp)
    csrw mstatus, t0
    lw x1, 4(sp)
    lw x5, 8(sp)
    lw x6, 12(sp)
    lw x7, 16(sp)
    lw x8, 20(sp)
    lw x9, 24(sp)
    lw x10, 28(sp)
    lw x11, 32(sp)
    lw x12, 36(sp)
    lw x13, 40(sp)
    lw x14, 44(sp)
    lw x15, 48(sp)
    lw x16, 52(sp)
    lw x17, 56(sp)
    lw x18, 60(sp)
    lw x19, 64(sp)
    lw x20, 68(sp)
    lw x21, 72(sp)
    lw x22, 76(sp)
    lw x23, 80(sp)
    lw x24, 84(sp)
    lw x25, 88(sp)
    lw x26, 92(sp)
    lw x27, 96(sp)
    lw x28, 100(sp)
    lw x29, 104(sp)
    lw x30, 108(sp)
    lw x31, 112(sp)
    addi sp, sp, 128
    mret

    .section .text,"ax"
    .align 2
init:
    la   t0, handler
    csrw mtvec, t0
    li   t0, STACK_A - 128
    la   t1, taskA
    sw   t1, 0(t0)
    li   t1, 0x1880
    sw   t1, 116(t0)
    li   t2, TCB_A
    sw   t0, 0(t2)
    li   t0, STACK_B - 128
    la   t1, taskB
    sw   t1, 0(t0)
    li   t1, 0x1880
    sw   t1, 116(t0)
    li   t2, TCB_B
    sw   t0, 0(t2)
    li   t1, CURTCB
    li   t2, TCB_A
    sw   t2, 0(t1)
    li   t1, LOGPTR
    li   t2, LOGSTART
    sw   t2, 0(t1)
    li   t0, 0x800
    csrw mie, t0
    j    restore

# task A: yield, multiply chain, store/load, call into the conflicting page
taskA:
    li   s0, 0
    li   s1, NITER_A
    li   s2, DATA_A
    li   s3, 1234567
    li   s4, 7
A_loop:
    csrr t0, mstatus
    andi t0, t0, 8
    bnez t0, 1f
    li   t1, FAILMIE
    lw   t2, 0(t1)
    addi t2, t2, 1
    sw   t2, 0(t1)
    csrsi mstatus, 8
1:
    ecall
    mul  a0, s3, s4
    addi s3, a0, 13
    mulh a1, s3, s4
    xor  s0, s0, a1
    add  s0, s0, a0
    sw   s0, 0(s2)
    lw   a2, 0(s2)
    add  s0, s0, a2
    jal  ra, alt_a
    andi t3, s1, 3
    bnez t3, 2f
    ecall
    mulhu a3, s0, s3
    add  s0, s0, a3
2:
    addi s1, s1, -1
    bnez s1, A_loop
    li   t1, RESA
    sw   s0, 0(t1)
    li   t1, DONEA
    li   t2, 1
    sw   t2, 0(t1)
A_end:
    ecall
    j    A_end

# task B: the Core-poll shape: yield and now_ms-style multiply, tightly
    .section .text.b,"ax"
    .align 2
taskB:
    li   s0, 0
    li   s1, NITER_B
    li   s2, DATA_B
    li   s3, 5
    li   s4, 625
B_loop:
    csrr t0, mstatus
    andi t0, t0, 8
    bnez t0, 1f
    li   t1, FAILMIE
    lw   t2, 0(t1)
    addi t2, t2, 1
    sw   t2, 0(t1)
    csrsi mstatus, 8
1:
    ecall
    lw   a5, 0(s2)
    addi a5, a5, 3
    sw   a5, 0(s2)
    mul  a0, a5, s4
    ecall
    mul  a1, a0, s3
    add  s0, s0, a1
    ecall
    mulhsu a2, a1, a0
    xor  s0, s0, a2
    lw   a3, 4(s2)
    add  s0, s0, a3
    sw   s0, 4(s2)
    jal  ra, alt_b
    addi s1, s1, -1
    bnez s1, B_loop
    li   t1, RESB
    sw   s0, 0(t1)
    li   t1, DONEB
    li   t2, 1
    sw   t2, 0(t1)
B_end:
    ecall
    j    B_end

# these two sit 0x800 x n above the task code: same I-cache index, so every
# call and return refills lines
    .section .text.alt,"ax"
    .align 2
alt_a:
    mul  a4, s0, s3
    mul  a5, a4, s4
    sw   a5, 8(s2)
    lw   a6, 8(s2)
    mulh a7, a6, a4
    add  s0, s0, a7
    ret
alt_b:
    lw   a4, 8(s2)
    mul  a4, a4, s4
    sw   a4, 8(s2)
    mulhu a5, a4, s3
    add  s0, s0, a5
    ret
