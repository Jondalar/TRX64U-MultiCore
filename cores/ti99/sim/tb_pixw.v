`timescale 1ns/1ps
// bench: a synthetic 342 x 262 raster (284 x 243 visible, colour = (x + y) & 15) through ti99_pixw into a model of the host's e_ port.
module tb_pixw;
  reg sclk = 0, uclk = 0; always #15.86 sclk = ~sclk; always #3.33 uclk = ~uclk;
  reg [2:0] div = 0; reg ce = 0; reg [3:0] colr = 0; reg hb = 1, vb = 1;
  wire ftgl; wire [7:0] latest, seq; wire [15:0] over;
  wire [26:0] ea; wire [2:0] ecmd; wire een; reg erdy = 1; wire [127:0] ewd; wire [15:0] emask; wire ewr;
  ti99_pixw dut(.sys_clk(sclk),.sys_rst(1'b0),.ce_pix(ce),.colour(colr),.hblank(hb),.vblank(vb),.frame_tgl(ftgl),.latest(latest),.seq(seq),.overruns(over),
    .dbgw(128'd0),.fb_base(32'h01D00000),.fb_stride(32'h00100000),.ui_clk(uclk),.ui_rst(1'b0),.e_addr(ea),.e_cmd(ecmd),.e_en(een),.e_rdy(erdy),.e_wdf_data(ewd),.e_wdf_mask(emask),.e_wdf_wren(ewr));
  // DDR2 model: 128-bit words, address in 16-bit units
  reg [127:0] mem [0:(1<<22)-1];
  always @(posedge uclk) begin
    erdy <= ($random & 3) != 0;                      // the port is not always ready
    if (een && erdy) mem[ea >> 3] <= ewd;
  end
  // raster: 342 pixels a line, 262 lines. visible x 13..296, visible y 16..258 (the first 13 + 3 lines are blanking)
  integer px = 0, py = 0, frames = 0;
  always @(posedge sclk) begin
    ce <= 0;
    div <= (div == 5) ? 0 : div + 1;
    if (div == 0) begin
      ce <= 1;
      hb <= !(px >= 13 && px < 297);
      vb <= !(py >= 16 && py < 259);
      colr <= ((px - 13) + (py - 16)) & 15;
      if (px == 341) begin px <= 0; if (py == 261) begin py <= 0; frames <= frames + 1; end else py <= py + 1; end else px <= px + 1;
    end
  end
  function [15:0] palx(input [3:0] c);
    case (c)
      0: palx = 16'h0000;
      1: palx = 16'h0000;
      2: palx = 16'h2648;
      3: palx = 16'h5EEF;
      4: palx = 16'h52BD;
      5: palx = 16'h7BBF;
      6: palx = 16'hD289;
      7: palx = 16'h475E;
      8: palx = 16'hFAAA;
      9: palx = 16'hFBCF;
      10: palx = 16'hD60A;
      11: palx = 16'hE670;
      12: palx = 16'h2587;
      13: palx = 16'hCAD7;
      14: palx = 16'hCE79;
      15: palx = 16'hFFFF;
    endcase
  endfunction
  integer r, c, e = 0, nb;
  reg [127:0] w; reg [15:0] got, want;
  initial begin
    wait (frames == 3); #1000;
    $display("overruns %0d seq %0d latest %h", over, seq, latest);
    if (seq < 2) begin e = e + 1; $display("FAIL: fewer than 2 frames reported"); end
    if (over != 0) begin e = e + 1; $display("FAIL overruns"); end
    // frame 0 sits in buffer 0: row r, column c; source pixel x = X0 + c, y = Y0 + r, colour (x + y) & 15
    for (r = 0; r < 240; r = r + 1) for (c = 0; c < 280; c = c + 1) begin
      w = mem[((27'h01D00000 >> 1) + r * 280 + (c & ~7)) >> 3];
      got = w >> ((c & 7) * 16);
      want = palx(((2 + c) + (3 + r)) & 15);
      if (got !== want) begin e = e + 1; if (e < 6) $display("FAIL r%0d c%0d got %h want %h", r, c, got, want); end
    end
    if (e == 0) $display("PASS ti99_pixw"); else $display("FAILED %0d", e);
    $finish;
  end
  initial begin #60000000; $display("TIMEOUT"); $finish; end
endmodule
