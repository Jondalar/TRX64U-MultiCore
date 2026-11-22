`timescale 1ns/1ps
// run: iverilog -g2005 -Ptb.MODE=<0..3> -o tb clkgen_nco.v tb_clkgen.v && vvp tb
module tb;
  parameter integer MODE = 1;
  real F;
  real HP;
  reg clk = 0, rst = 1, turbo = 0;
  wire reset_out, vdp, vdpn, vga, ovl, sgc, cpu, m3, grom, vsp;
  clkgen dut(.ext_reset_in(rst), .clk(clk), .vmode(MODE[1:0]), .hold(1'b0), .cpu_turbo(turbo), .reset_out(reset_out), .vdp_clk_en(vdp),
    .vdp_clk_en_next(vdpn), .vga_clk_en(vga), .overlay_clk_en(ovl), .sgc_clk_en(sgc), .cpu_clk_en(cpu),
    .clk_3mhz_en(m3), .grom_clk_en(grom), .vsp_clk_en(vsp));
  initial begin
    case (MODE) 0: F = 31527778.0; 1: F = 32725694.0; 2: F = 32765152.0; default: F = 31584821.0; endcase
    HP = 1e9 / (2.0 * F);
    forever #(HP) clk = ~clk;
  end
  integer n_vdp=0, n_vga=0, n_sgc=0, n_cpu=0, n_m3=0, n_grom=0, n_vsp=0, cyc=0, bad_next=0;
  reg vdpn_q = 0;
  always @(posedge clk) begin
    if (vdpn_q !== vdp && cyc > 100) bad_next = bad_next + 1;   // vdp_clk_en must equal vdp_clk_en_next one clk later
    vdpn_q <= vdpn;
    if (!reset_out) begin
      cyc = cyc + 1;
      n_vdp = n_vdp + vdp; n_vga = n_vga + vga; n_sgc = n_sgc + sgc; n_cpu = n_cpu + cpu;
      n_m3 = n_m3 + m3; n_grom = n_grom + grom; n_vsp = n_vsp + vsp;
    end
  end
  real t;
  initial begin
    #200 rst = 0;
    wait (!reset_out);
    #20000000;                 // 20 ms after reset
    t = cyc / F;
    $display("mode %0d (%0d Hz): window %f s, bad_next=%0d", MODE, $rtoi(F), t, bad_next);
    $display("  vdp  %f MHz (want 5.369318)   vga %f (want 10.738635)", n_vdp / t / 1e6, n_vga / t / 1e6);
    $display("  sgc  %f MHz (want 3.579545)   cpu %f (want 3.000000)", n_sgc / t / 1e6, n_cpu / t / 1e6);
    $display("  grom %f kHz (want 447.443)    vsp %f kHz (want 160.000)", n_grom / t / 1e3, n_vsp / t / 1e3);
    $finish;
  end
endmodule
