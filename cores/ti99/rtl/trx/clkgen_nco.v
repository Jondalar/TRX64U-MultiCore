// clkgen_nco.v - drop-in replacement for Mega99's gateware/clkgen.v for a clock that is NOT a multiple
// of 10.738635 MHz or 3 MHz, plus ONE extra input: vmode (the host's System Mode, 2 bits).
//
// Mega99's clkgen divides clk by integers, which needs clk = N x 10.738635 MHz and N x 3 MHz.
// The multicore host gives a core a sys_clk that depends on its System Mode (host/mode_ctl.v) and the
// app can switch it while the core runs:
//     vmode 0 PAL      31,527,778 Hz      vmode 2 PAL-60   32,765,152 Hz
//     vmode 1 NTSC     32,725,694 Hz      vmode 3 NTSC-50  31,584,821 Hz
// Every clock-enable here comes from a 32-bit phase accumulator whose increment is chosen by vmode, so
// the AVERAGE rate is exact in every mode (error < 1 ppm); the enables jitter by one clk, which is
// harmless for a design that only uses clk plus clock enables.
//
// Rates (as in clkgen.v): vdp 5.3693175 MHz, vga 10.738635 MHz, sgc 3.579545 MHz, cpu / peripheral 3 MHz,
// grom = sgc/8, vsp 160 kHz. cpu_turbo: cpu_clk_en and grom_clk_en run at clk. The "_next" strobe comes
// one clk before the enable it announces. The *_multiplier parameters are accepted and ignored.
module clkgen(input      ext_reset_in,
	      input	 clk,
	      input [1:0] vmode,
	      input	 hold,       // pause: the CPU, GROM, sound, speech and 3 MHz peripheral enables stop; the VDP keeps running
	      input	 cpu_turbo,

	      output	 reset_out,
	      output	 vdp_clk_en,
	      output	 vdp_clk_en_next,
	      output	 vga_clk_en,
	      output	 overlay_clk_en,
	      output	 sgc_clk_en,
	      output reg cpu_clk_en,
	      output	 clk_3mhz_en,
	      output reg grom_clk_en,
	      output reg vsp_clk_en
	      );
   parameter integer vdp_clk_multiplier = 1;
   parameter integer cpu_clk_multiplier = 1;
   parameter integer vsp_clk_multiplier = 1;
   parameter integer generate_overlay_clk_en = 0;
   parameter integer CLK_HZ_0 = 31527778;   // vmode 0, PAL
   parameter integer CLK_HZ_1 = 32725694;   // vmode 1, NTSC
   parameter integer CLK_HZ_2 = 32765152;   // vmode 2, PAL-60
   parameter integer CLK_HZ_3 = 31584821;   // vmode 3, NTSC-50

   // increment = round(f * 2^32 / clk_hz); f is given as 2*f (half Hz) so the VDP's 5369317.5 Hz is exact
   function [31:0] inc;
      input [63:0] f2;
      input [63:0] c;
      reg [63:0] r;
      begin
	 r = ((f2 << 31) + (c >> 1)) / c;
	 inc = r[31:0];
      end
   endfunction

   localparam [63:0] F2_VDP = 64'd10738635;    // 5369317.5 Hz
   localparam [63:0] F2_VGA = 64'd21477270;
   localparam [63:0] F2_SGC = 64'd7159090;
   localparam [63:0] F2_CPU = 64'd6000000;
   localparam [63:0] F2_VSP = 64'd320000;

   reg [31:0] i_vdp, i_vga, i_sgc, i_cpu, i_vsp;
   always @(*)
     case (vmode)
       2'd0: begin i_vdp = inc(F2_VDP, CLK_HZ_0); i_vga = inc(F2_VGA, CLK_HZ_0); i_sgc = inc(F2_SGC, CLK_HZ_0);
		   i_cpu = inc(F2_CPU, CLK_HZ_0); i_vsp = inc(F2_VSP, CLK_HZ_0); end
       2'd1: begin i_vdp = inc(F2_VDP, CLK_HZ_1); i_vga = inc(F2_VGA, CLK_HZ_1); i_sgc = inc(F2_SGC, CLK_HZ_1);
		   i_cpu = inc(F2_CPU, CLK_HZ_1); i_vsp = inc(F2_VSP, CLK_HZ_1); end
       2'd2: begin i_vdp = inc(F2_VDP, CLK_HZ_2); i_vga = inc(F2_VGA, CLK_HZ_2); i_sgc = inc(F2_SGC, CLK_HZ_2);
		   i_cpu = inc(F2_CPU, CLK_HZ_2); i_vsp = inc(F2_VSP, CLK_HZ_2); end
       default: begin i_vdp = inc(F2_VDP, CLK_HZ_3); i_vga = inc(F2_VGA, CLK_HZ_3); i_sgc = inc(F2_SGC, CLK_HZ_3);
		   i_cpu = inc(F2_CPU, CLK_HZ_3); i_vsp = inc(F2_VSP, CLK_HZ_3); end
     endcase

   reg [31:0] acc_vdp = 0, acc_vga = 0, acc_sgc = 0, acc_cpu = 0, acc_vsp = 0;
   wire [32:0] s_vdp = {1'b0, acc_vdp} + i_vdp;
   wire [32:0] s_vga = {1'b0, acc_vga} + i_vga;
   wire [32:0] s_sgc = {1'b0, acc_sgc} + i_sgc;
   wire [32:0] s_cpu = {1'b0, acc_cpu} + i_cpu;
   wire [32:0] s_vsp = {1'b0, acc_vsp} + i_vsp;
   // the carry of this clk is the enable of the next one
   wire nxt_vdp = s_vdp[32], nxt_vga = s_vga[32], nxt_sgc = s_sgc[32], nxt_cpu = s_cpu[32], nxt_vsp = s_vsp[32];

   reg en_vdp = 0, en_vga = 0, en_sgc = 0, en_cpu = 0;
   reg [0:4] reset_cnt = 5'd0;
   reg	     init_done = 1'b0;
   reg [0:2] grom_cnt = 3'd0;

   assign reset_out       = ~reset_cnt[0];
   assign vdp_clk_en      = en_vdp;
   assign vdp_clk_en_next = nxt_vdp;
   assign vga_clk_en      = en_vga;
   assign sgc_clk_en      = en_sgc & ~hold;
   assign clk_3mhz_en     = en_cpu & ~hold;
   assign overlay_clk_en  = 1'b0;

   always @(posedge clk) begin
      acc_vdp <= s_vdp[31:0]; en_vdp <= nxt_vdp;
      acc_vga <= s_vga[31:0]; en_vga <= nxt_vga;
      acc_sgc <= s_sgc[31:0]; en_sgc <= nxt_sgc;
      acc_cpu <= s_cpu[31:0]; en_cpu <= nxt_cpu;
      acc_vsp <= s_vsp[31:0]; vsp_clk_en <= nxt_vsp & ~hold;

      if (!init_done) begin
	 cpu_clk_en <= 1'b1;
	 grom_clk_en <= 1'b1;
	 grom_cnt <= 3'd0;
	 reset_cnt <= 5'd0;
	 init_done <= 1'b1;
      end else begin
	 if (ext_reset_in)
	   reset_cnt <= 5'd0;
	 else if (reset_out && en_cpu)
	   reset_cnt <= reset_cnt + 5'd1;

	 cpu_clk_en  <= hold ? 1'b0 : (cpu_turbo ? 1'b1 : nxt_cpu);
	 grom_clk_en <= hold ? 1'b0 : (cpu_turbo ? 1'b1 : (nxt_sgc && grom_cnt == 3'd7));
	 if (nxt_sgc) grom_cnt <= grom_cnt + 3'd1;
      end
   end
endmodule
