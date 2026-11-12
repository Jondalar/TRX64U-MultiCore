// T85 (disk change): /DSKCHG of paula_floppy.v as Kickstart reads it. The bench plays trackdisk's poll: select DF0, read /CHNG, and when
// it is low step the head once (a low pulse on /STEP, the rising edge steps) and read it again. A mount announcement is what the application's
// MOUNT register produces (one ce28 period of sdc_image_mounted with sdc_image_size). Absolute checks, no reference model.
`timescale 1ns/1ps
module tb_dskchg;
reg clk = 0, ce28 = 0, clk7_en = 0, reset = 1;
reg [3:0] sel = 4'b1111;          // _sel, active low
reg step_n = 1'b1;                // _step
reg [3:0] mounted = 4'b0000;
reg [31:0] msize = 32'd0;
wire chg, tr0, rdy, wp, idx, blk, syn, led, dmal, dmas;
wire [15:0] dout; wire [3:0] rd, wr; wire [31:0] sec; wire [7:0] ob;

paula_floppy dut (
  .ce28(ce28), .clk(clk), .clk7_en(clk7_en), .clk7n_en(1'b0), .reset(reset), .ntsc(1'b1), .sof(1'b0), .enable(1'b1),
  .reg_address_in(8'h00), .data_in(16'h0000), .data_out(dout), .dmal(dmal), .dmas(dmas),
  ._step(step_n), .direc(1'b0), ._sel(sel), .side(1'b0), ._motor(1'b1), ._track0(tr0), ._change(chg),
  ._ready(rdy), ._wprot(wp), .index(idx), .blckint(blk), .syncint(syn), .wordsync(1'b0),
  .fdd_led(led), .floppy_drives(2'd1), .floppy_wrprot(1'b0), .sdc_image_mounted(mounted), .sdc_image_size(msize),
  .sdc_rd(rd), .sdc_wr(wr), .sdc_sector(sec), .sdc_busy(1'b0), .sdc_done(1'b0),
  .sdc_byte_in_strobe(1'b0), .sdc_byte_addr(9'd0), .sdc_byte_in_data(8'd0), .sdc_byte_out_data(ob));

always #5 clk = ~clk;
integer ce_n = 0;
always @(negedge clk) begin   // ce28 every 4th clock, clk7_en every 4th ce28
  ce_n = ce_n + 1;
  ce28 <= (ce_n % 4) == 0;
  clk7_en <= (ce_n % 16) == 0;
end

integer errs = 0, npass = 0;
task check(input ok, input [8*72-1:0] what); begin
  if (ok) begin npass = npass + 1; $display("ok   %0s", what); end
  else begin errs = errs + 1; $display("FAIL %0s", what); end
end endtask

task clks(input integer n); integer k; begin for (k = 0; k < n; k = k + 1) @(posedge clk); end endtask

// the announcement: one ce28 period of sdc_image_mounted (the application's MOUNT write, amiga_sdc.v)
task mount(input [3:0] which, input [31:0] size); begin
  @(posedge clk); while (!ce28) @(posedge clk);
  mounted <= which; msize <= size;
  @(posedge clk); while (!ce28) @(posedge clk);
  mounted <= 4'b0000;
  clks(64);
end endtask

reg chg_b, chg_a;
// trackdisk's poll of drive d (0..3): select, read /CHNG, step once when it is low, read again, deselect
task poll(input integer d); begin
  sel = 4'b1111; sel[d] = 1'b0; clks(64);
  chg_b = chg; chg_a = chg;
  if (!chg) begin
    step_n = 1'b0; clks(200); step_n = 1'b1; clks(200);
    chg_a = chg;
  end
  sel = 4'b1111; clks(64);
end endtask

task readchg(input integer d, output v); begin
  sel = 4'b1111; sel[d] = 1'b0; clks(64); v = chg; sel = 4'b1111; clks(64);
end endtask

reg v;
initial begin
  clks(40); reset = 0; clks(40);
  // 1. an empty drive: /CHNG low, and stepping does not release it
  poll(0); check(chg_b === 1'b0 && chg_a === 1'b0, "empty drive: /DSKCHG low, a step does not release it");
  poll(0); check(chg_b === 1'b0 && chg_a === 1'b0, "empty drive: still low at the second poll");
  // 2. the disk goes in (mount announcement): nothing to see until the head steps, as on a real drive
  mount(4'b0001, 32'd901120);
  readchg(0, v); check(v === 1'b0, "insert: /DSKCHG stays low until the head steps");
  poll(0); check(chg_b === 1'b0 && chg_a === 1'b1, "insert: the next poll's step releases /DSKCHG (disk noticed)");
  readchg(0, v); check(v === 1'b1, "insert: /DSKCHG stays released without further steps");
  poll(0); check(chg_b === 1'b1, "insert: later polls read it released");
  // 3. the disk comes out: /DSKCHG goes low by itself, a step does not bring it back
  mount(4'b0001, 32'd0);
  readchg(0, v); check(v === 1'b0, "eject: /DSKCHG low without any step");
  poll(0); check(chg_b === 1'b0 && chg_a === 1'b0, "eject: a step does not release it");
  // 4. in again
  mount(4'b0001, 32'd901120);
  poll(0); check(chg_b === 1'b0 && chg_a === 1'b1, "second insert: noticed at the next poll");
  // 5. a new disk announced while the old one is in (swap without the drive reporting empty): that is a disk change, /DSKCHG goes low
  mount(4'b0001, 32'd901120);
  readchg(0, v); check(v === 1'b0, "swap: a mount announcement of a present disk pulls /DSKCHG low");
  poll(0); check(chg_b === 1'b0 && chg_a === 1'b1, "swap: the next step releases it again (new disk noticed)");
  // 6. the step of another drive does not release this drive's latch
  mount(4'b0001, 32'd0); mount(4'b0001, 32'd901120);
  sel = 4'b1101; clks(64); step_n = 1'b0; clks(200); step_n = 1'b1; clks(200); sel = 4'b1111; clks(64);
  readchg(0, v); check(v === 1'b0, "a step of DF1 does not release DF0");
  if (errs == 0) $display("PASS tb_dskchg checks=%0d", npass); else $display("FAIL tb_dskchg errors=%0d", errs);
  $finish;
end
initial begin #200000000; $display("FAIL tb_dskchg timeout"); $finish; end
endmodule
