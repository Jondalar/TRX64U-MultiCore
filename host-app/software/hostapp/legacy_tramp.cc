// The last step of a full-firmware start (T115 D4), as machine code that runs from DDR2 outside the host app: the host app
// itself lives at 0x30000, where the firmware's application goes, so the copy cannot run from the host's own image.
// Position independent, touches no stack: legacy.cc copies it to TRAMPOLINE_ADDR (as core_jump copies tb_copy) and calls it
//   a0 = destination (0x30000)   a1 = the staged application   a2 = length in bytes (a multiple of 4)
//   a3 = the ICAP byte sequence  a4 = its length
// It copies, reads the last word back (the stores are done in DDR2 before the FPGA is touched), takes the decouple away,
// enables the ICAP loader of layer 1 and sends the sequence byte by byte to its data register, then waits for the
// reconfiguration. Register addresses as in coreboot.cc: DC 0x10060620, EN 0x10060614, DATA 0x10060610.
extern "C" void tb_legacy_start(void);
extern "C" void tb_legacy_end(void);
__asm__(".pushsection .text.tb_legacy, \"ax\", @progbits\n"
        ".balign 4\n"
        ".global tb_legacy_start\n"
        "tb_legacy_start:\n"
        "1: blez a2, 2f\n"
        "   lw   t0, 0(a1)\n"
        "   sw   t0, 0(a0)\n"
        "   addi a0, a0, 4\n"
        "   addi a1, a1, 4\n"
        "   addi a2, a2, -4\n"
        "   j    1b\n"
        "2: fence\n"
        "   lw   t0, -4(a0)\n"
        "   li   t1, 0x10060620\n"
        "   sb   zero, 0(t1)\n"
        "   li   t1, 0x10060614\n"
        "   li   t2, 1\n"
        "   sb   t2, 0(t1)\n"
        "   li   t1, 0x10060610\n"
        "3: blez a4, 4f\n"
        "   lbu  t2, 0(a3)\n"
        "   sb   t2, 0(t1)\n"
        "   addi a3, a3, 1\n"
        "   addi a4, a4, -1\n"
        "   j    3b\n"
        "4: j    4b\n"
        ".global tb_legacy_end\n"
        "tb_legacy_end:\n"
        ".popsection\n");
