// T97 step 2: the Amiga 1200 core's CPU. cpu_wrapper.v (shared with the OCS core, not edited) instantiates
// TG68KdotC_Kernel when ENABLE_TG68K is defined and hands it cpucfg, the 2-bit CPU type that nanomig takes from
// the application's CPU register (00 = 68000, 01 = 68010, 11 = 68020). The A1200 application writes CPU=00
// (cores/amiga1200/app_regs.txt), which would run this kernel as a 68000 and Kickstart 40.68, built for the
// 68EC020, would die on its first 020 instruction. This module takes the kernel's name and ties CPU to 11, so
// the 020 does not depend on a byte an application may or may not write. The real kernel is
// TG68KdotC_Kernel_core (rtl/tg68k/, entity renamed by tools/vendor_tg68k.sh).
// Ports as the wrapper uses them: VHDL-style names for Vivado, the lower-case names of the Verilog translation
// when VERILATOR is defined (the wrapper makes the same split).
module TG68KdotC_Kernel #(
    parameter SR_Read = 2, VBR_Stackframe = 2, extAddr_Mode = 2, MUL_Mode = 2, DIV_Mode = 2, BitField = 2, MUL_Hardware = 1
) (
`ifdef VERILATOR
    input         clk, nreset, clkena_in,
    input  [15:0] data_in,
    input  [2:0]  ipl,
    input         ipl_autovector,
    input  [1:0]  cpu,
    output [31:0] addr_out,
    output [15:0] data_write,
    output        nwr, nuds, nlds,
    output [1:0]  busstate,
    output        longword,
    output        nresetout,
    output [3:0]  cacr_out,
    output [31:0] vbr_out,
    output [31:0] regin_out
);
    wire [1:0] cpu_unused = cpu;
    TG68KdotC_Kernel_core k (
        .clk(clk), .nreset(nreset), .clkena_in(clkena_in), .data_in(data_in), .ipl(ipl),
        .ipl_autovector(ipl_autovector), .berr(1'b0), .cpu(2'b11), .addr_out(addr_out),
        .data_write(data_write), .nwr(nwr), .nuds(nuds), .nlds(nlds), .busstate(busstate),
        .longword(longword), .nresetout(nresetout), .fc(), .clr_berr(), .skipFetch(),
        .regin_out(regin_out), .cacr_out(cacr_out), .vbr_out(vbr_out));
`else
    input         clk, nReset, clkena_in,
    input  [15:0] data_in,
    input  [2:0]  IPL,
    input         IPL_autovector,
    input         berr,
    input  [1:0]  CPU,
    output [31:0] addr_out,
    output [15:0] data_write,
    output        nWr, nUDS, nLDS,
    output [1:0]  busstate,
    output        longword,
    output        nResetOut,
    output [2:0]  FC,
    output        clr_berr,
    output        skipFetch,
    output [31:0] regin_out,
    output [3:0]  CACR_out,
    output [31:0] VBR_out
);
    wire [1:0] cpu_unused = CPU;
    TG68KdotC_Kernel_core #(
        .SR_Read(SR_Read), .VBR_Stackframe(VBR_Stackframe), .extAddr_Mode(extAddr_Mode),
        .MUL_Mode(MUL_Mode), .DIV_Mode(DIV_Mode), .BitField(BitField), .MUL_Hardware(MUL_Hardware)
    ) k (
        .clk(clk), .nReset(nReset), .clkena_in(clkena_in), .data_in(data_in), .IPL(IPL),
        .IPL_autovector(IPL_autovector), .berr(berr), .CPU(2'b11), .addr_out(addr_out),
        .data_write(data_write), .nWr(nWr), .nUDS(nUDS), .nLDS(nLDS), .busstate(busstate),
        .longword(longword), .nResetOut(nResetOut), .FC(FC), .clr_berr(clr_berr), .skipFetch(skipFetch),
        .regin_out(regin_out), .CACR_out(CACR_out), .VBR_out(VBR_out));
`endif
endmodule
