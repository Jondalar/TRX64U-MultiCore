// Prints the MMCM DRP words of host/mode_ctl.v (T40 §9.2) from
// AMD's XAPP888 functions, which are not ours and stay in the Vivado image:
//   docker run --rm vivado-env:2024.1 cat \
//     /opt/Xilinx/Vivado/2024.1/data/ip/xilinx/clk_wiz_v6_0/mmcm_pll_drp_func_7s_mmcm.vh > x/mmcm_pll_drp_func_7s_mmcm.vh
//   iverilog -g2012 -I x -o gen tools/mmcm_drp_gen.v && vvp gen
// One line per register: mode, DRP address, mask of the bits kept (XAPP888's), value.
module mmcm_drp_gen;
`define FRAC_PRECISION 10
`define FIXED_WIDTH 32
`include "mmcm_pll_drp_func_7s_mmcm.vh"
    reg [37:0] fb, o0, o1;
    reg [13:0] dv;
    reg [39:0] lk;
    reg [9:0]  fl;
    integer m;
    task w(input integer mode, input [6:0] a, input [15:0] keep, input [15:0] v);
        $display("%0d %02h %04h %04h", mode, a, keep, v & ~keep);
    endtask
    // mode: D, M (integer part, fraction in 1/1000), O0 (even; O1 = O0 / 2)
    task one(input integer mode, input integer d, input integer mi, input integer mf, input integer o);
    begin
        fb = mf == 0 ? mmcm_pll_count_calc(mi, 0, 50000) : mmcm_frac_count_calc(mi, 0, 50000, mf);
        o0 = mmcm_pll_count_calc(o, 0, 50000);
        o1 = mmcm_pll_count_calc(o / 2, 0, 50000);
        dv = mmcm_pll_divider(d, 50000);
        lk = mmcm_pll_lock_lookup(mi);
        fl = mmcm_pll_filter_lookup(mi, "OPTIMIZED");
        w(mode, 7'h28, 16'h0000, 16'hFFFF);
        w(mode, 7'h08, 16'h1000, o0[15:0]);
        w(mode, 7'h09, 16'h8000, o0[31:16]);
        w(mode, 7'h0A, 16'h1000, o1[15:0]);
        w(mode, 7'h0B, 16'h8000, o1[31:16]);
        w(mode, 7'h16, 16'hC000, {2'b00, dv[13:12], dv[11:0]});
        w(mode, 7'h14, 16'h1000, fb[15:0]);
        w(mode, 7'h15, 16'h8000, fb[31:16]);
        w(mode, 7'h13, 16'hC3FF, {2'b00, fb[35:32], 10'd0});
        w(mode, 7'h18, 16'hFC00, {6'h00, lk[29:20]});
        w(mode, 7'h19, 16'h8000, {1'b0, lk[34:30], lk[9:0]});
        w(mode, 7'h1A, 16'h8000, {1'b0, lk[39:35], lk[19:10]});
        w(mode, 7'h4E, 16'h66FF, {fl[9], 2'b00, fl[8:7], 2'b00, fl[6], 8'h00});
        w(mode, 7'h4F, 16'h666F, {fl[5], 2'b00, fl[4:3], 2'b00, fl[2:1], 2'b00, fl[0], 4'h0});
    end
    endtask
    initial begin
        one(0, 3, 56, 750, 30);     // PAL      31.527778 MHz
        one(1, 3, 47, 125, 24);     // NTSC     32.725694 MHz
        one(2, 3, 43, 250, 22);     // PAL-60   32.765152 MHz
        one(3, 2, 35, 375, 28);     // NTSC-50  31.584821 MHz
    end
endmodule
