// ---------------------------------------------------------------------------
// T182 step 2 (T3): the 6510's on-chip I/O port ($00 direction, $01 data) outside the CPU. The SuperCPU's 65816 has no port; the C64 needs it, so the cartridge's
// glue (and here this module) answers $00/$01 and drives the memory configuration lines and the datasette.
//
// Taken out of vendor/icepi/cpu6510.v (Copyright 2026 Mateusz Nalewajski, GPL-3.0-or-later) with its behaviour kept: reset (direction 0, latch $FF), the bits 3, 6, 7 that float
// while they are inputs and read back the level last driven (6 and 7 fall to 0 after FALL_OFF_CYCLES, T3), bit 5 reading low as an input, the app's DMA write while the CPU is held
// (dp_*, T16 Q8: the first write of a hold saves the port, dp_restore puts it back). Cut: the turbo engine's t_mode paths and the state-bus load (sb_*).
// The port registers are written in the write cycle while PHI2 is high (clk_phi) on the clk2x edge that run2 passes.
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module scpu_pport #(
  parameter [18:0] FALL_OFF_CYCLES = 19'd350000
) (
  input  wire        clk,         // the C64's (gated) clock: PHI2-side registers
  input  wire        clk2x,       // the port registers
  input  wire        reset,
  input  wire        g_edge,      // this clk2x edge is one a gclk edge passes on
  input  wire        run2,        // any clk2x edge that passes
  input  wire        clk_phi,
  input  wire        phi2_n,

  input  wire        access_io,   // the CPU's address is $00:0000 or $00:0001 (a bank-0 cycle that is a real bus cycle)
  input  wire        sel_pdir,    // ... and it is $0000
  input  wire        we,          // a write cycle
  input  wire  [7:0] wdata,       // the CPU's data out
  input  wire  [5:0] pin,         // the C64 side of the port lines (datasette sense, others pulled up)

  output wire  [7:0] rdata,       // what a CPU read of $00 / $01 returns
  output wire  [5:0] pout,        // the lines as the C64 sees them: (latch & dir) | (pin & ~dir)
  output reg   [2:0] pout3_q,     // pout[2:0] as a register: what pout[2:0] is after the clk2x edge that updates dir / latch
  output wire  [7:0] st_pdir, st_pout, st_pread,

  input  wire        dp_we,       // the app's DMA while the CPU is held: write $00 (dp_hi 0) or $01 (dp_hi 1)
  input  wire        dp_hi,
  input  wire  [7:0] dp_data,
  input  wire        dp_restore
);

reg [5:0] pin_r;
reg [7:0] pdir   = 8'h00;
reg [7:0] pout_i = 8'hFF;
reg [7:0] pfloat = 8'h00;
reg [18:0] fall6 = 19'd0;
reg [18:0] fall7 = 19'd0;

wire [7:0] pread = (pout_i & pdir) | ({pfloat[7:6], 1'b0, pin_r[4], pfloat[3], pin_r[2:0]} & ~pdir);

assign rdata    = sel_pdir ? pdir : pread;
assign pout     = (pout_i[5:0] & pdir[5:0]) | (pin_r & ~pdir[5:0]);
assign st_pdir  = pdir;
assign st_pout  = pout_i;
assign st_pread = pread;

always @(posedge clk)
  if (!reset && phi2_n) pin_r <= pin;       // as the 6510's wrapper: no reset value, the level of the last PHI2 fall

wire       port_wr = clk_phi && run2 && we && access_io;
wire [7:0] pdir_n  = reset ? 8'h00 : (port_wr && sel_pdir)  ? wdata : pdir;
wire [7:0] pout_n  = reset ? 8'hFF : (port_wr && !sel_pdir) ? wdata : pout_i;
always @(posedge clk2x)
  pout3_q <= (pout_n[2:0] & pdir_n[2:0]) | (pin_r[2:0] & ~pdir_n[2:0]);

// the app's DMA while the CPU is held: dp_we / dp_restore come from the gclk domain, so they count only on the clk2x edges a gclk edge passes (g_edge)
reg         pt_valid = 1'b0;
reg  [61:0] pt_sv = 62'd0;      // {$00, $01 latch, floating bits, fall6, fall7}
wire        dpw = g_edge && dp_we;

always @(posedge clk2x) begin
  if (reset) begin
    pdir   <= 8'h00;
    pout_i <= 8'hFF;
  end else if (dpw) begin
    if (dp_hi) pout_i <= dp_data;
    else       pdir   <= dp_data;
  end else if (port_wr) begin
    if (sel_pdir) pdir   <= wdata;
    else          pout_i <= wdata;
  end
  if (dpw && !pt_valid) begin
    pt_sv    <= {pdir, pout_i, pfloat, fall6, fall7};
    pt_valid <= 1'b1;
  end
  if (g_edge && dp_restore && pt_valid) begin
    pdir     <= pt_sv[61:54];
    pout_i   <= pt_sv[53:46];
    pt_valid <= 1'b0;
  end
  if (reset) pt_valid <= 1'b0;
end

always @(posedge clk) begin
  if (reset) begin
    pfloat <= 8'h00;
  end else if (phi2_n) begin
    pfloat[3] <= pdir[3] ? pout_i[3] : pfloat[3];
    pfloat[6] <= pdir[6] ? pout_i[6] : pfloat[6] && fall6 != 0;
    pfloat[7] <= pdir[7] ? pout_i[7] : pfloat[7] && fall7 != 0;
    fall6 <= pdir[6] ? FALL_OFF_CYCLES : fall6 - (fall6 != 0);
    fall7 <= pdir[7] ? FALL_OFF_CYCLES : fall7 - (fall7 != 0);
  end
  if (dp_restore && pt_valid) begin
    pfloat <= pt_sv[45:38];
    fall6  <= pt_sv[37:19];
    fall7  <= pt_sv[18:0];
  end
end

endmodule
