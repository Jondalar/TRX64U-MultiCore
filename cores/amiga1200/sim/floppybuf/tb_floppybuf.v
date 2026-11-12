// T97 5.11: the A1200 copy of paula_floppy (sector buffer in block RAM) against the shared file, side by side, driven like the
// machine drives it: DSKLEN twice, the CPU draining DSKDATR (DMA read of two sectors, the sd card streaming 512 bytes per
// sector request) and the CPU pushing a valid MFM sector through DSKDAT (DMA write: sync, header with checksum, payload with
// checksum), after which the sd card model reads the buffer back byte by byte. Every output of both modules is compared on
// every clock. Besides the compare the write path has an absolute check: the 512 bytes the sd card reads back must be the
// 512 data bytes the bench encoded into the MFM stream. While the sd card is idle its address wanders at random (the
// registered read on the sd side is then exercised at arbitrary addresses).
`timescale 1ns/1ps
module tb_floppybuf;
reg clk = 0, ce28 = 0, clk7_en = 0, reset = 1;
reg [7:0]  reg_a = 8'h00;
reg [15:0] data_in = 16'h0000;
reg sdc_busy = 0, sdc_strobe = 0;
reg [8:0] sdc_addr = 0;
reg [7:0] sdc_din = 0;

wire [15:0] do_r, do_n; wire dmal_r, dmal_n, dmas_r, dmas_n;
wire tr0_r, tr0_n, chg_r, chg_n, rdy_r, rdy_n, wp_r, wp_n, idx_r, idx_n, blk_r, blk_n, syn_r, syn_n, led_r, led_n;
wire [3:0] rd_r, rd_n, wr_r, wr_n; wire [31:0] sec_r, sec_n; wire [7:0] ob_r, ob_n;

`define PORTS(sfx) \
  .ce28(ce28), .clk(clk), .clk7_en(clk7_en), .clk7n_en(1'b0), .reset(reset), .ntsc(1'b1), .sof(1'b0), .enable(1'b1), \
  .reg_address_in(reg_a), .data_in(data_in), .data_out(do_``sfx), .dmal(dmal_``sfx), .dmas(dmas_``sfx), \
  ._step(1'b0), .direc(1'b0), ._sel(4'b1110), .side(1'b0), ._motor(1'b0), ._track0(tr0_``sfx), ._change(chg_``sfx), \
  ._ready(rdy_``sfx), ._wprot(wp_``sfx), .index(idx_``sfx), .blckint(blk_``sfx), .syncint(syn_``sfx), .wordsync(1'b0), \
  .fdd_led(led_``sfx), .floppy_drives(2'd1), .floppy_wrprot(1'b0), .sdc_image_mounted(4'b0001), .sdc_image_size(32'd901120), \
  .sdc_rd(rd_``sfx), .sdc_wr(wr_``sfx), .sdc_sector(sec_``sfx), .sdc_busy(sdc_busy), .sdc_done(1'b0), \
  .sdc_byte_in_strobe(sdc_strobe), .sdc_byte_addr(sdc_addr), .sdc_byte_in_data(sdc_din), .sdc_byte_out_data(ob_``sfx)

paula_floppy_ref r(`PORTS(r));
paula_floppy     n(`PORTS(n));

wire [127:0] cmp_r = {do_r, dmal_r, dmas_r, tr0_r, chg_r, rdy_r, wp_r, idx_r, blk_r, syn_r, led_r, rd_r, wr_r, sec_r, ob_r};
wire [127:0] cmp_n = {do_n, dmal_n, dmas_n, tr0_n, chg_n, rdy_n, wp_n, idx_n, blk_n, syn_n, led_n, rd_n, wr_n, sec_n, ob_n};

integer errs = 0, cycles = 0;
reg [15:0] pre_do; reg [7:0] pre_a;
integer sd_reads = 0, sd_writes = 0, cpu_words = 0, cpu_words_data = 0, rb_ok = 0, rb_bad = 0, rb_nonzero = 0, rb_random = 0;
always #5 clk = ~clk;

// ce28 on a random three of four clocks; clk7_en with every fourth ce28
reg ce_v; integer ce_n2 = 0;
always @(negedge clk) begin
  ce_v = ($random & 3) != 0;
  if (ce_v) ce_n2 = ce_n2 + 1;
  ce28 <= ce_v;
  clk7_en <= ce_v && (ce_n2 % 4) == 0;
end

// the compare, on every clock
reg cmp_bad; integer cmp_i;
always @(posedge clk) if (!reset || cycles > 8) begin
  cycles <= cycles + 1;
`ifdef NETLIST
  // the synthesised netlist powers up 0 where the RTL reference is X (registers without reset): an X bit of the reference is not compared
  // (and not for the first 100 clocks, the reset release: the netlist's flip-flops without INIT are X there, the reference's 0)
  cmp_bad = 0;
  if (cycles > 100) for (cmp_i = 0; cmp_i < 128; cmp_i = cmp_i + 1) if (cmp_r[cmp_i] !== 1'bx && cmp_r[cmp_i] !== cmp_n[cmp_i]) cmp_bad = 1;
`else
  cmp_bad = (cmp_r !== cmp_n);
`endif
  if (cmp_bad) begin
    errs = errs + 1;
    if (errs < 8) $display("MISMATCH t=%0t ref=%h new=%h", $time, cmp_r, cmp_n);
  end
end

// ---- the sd card model, driven by the shared file's requests (the copy is compared equal) ----
reg [7:0] rd_data [0:511];     // the sector the sd card streams to the machine
reg [7:0] odd_b [0:255]; reg [7:0] even_b [0:255];
reg [7:0] wr_data [0:511];     // the 512 data bytes the bench encodes into the written track
integer sdst = 0, sdi = 0, sddly = 0;
always @(posedge clk) if (ce28) begin
  sdc_strobe <= 1'b0;
  case (sdst)
    0: begin   // idle: the address wanders
      sdc_addr <= $random;
      if (|rd_r) begin sdst <= 1; sddly <= ($random & 7) + 2; end
      else if (|wr_r) begin sdst <= 10; sddly <= ($random & 7) + 2; end
    end
    1: if (sddly == 0) begin sdc_busy <= 1'b1; sdi <= 0; sdst <= 2; sd_reads = sd_reads + 1; end else sddly <= sddly - 1;
    2: begin
      if (($random & 3) == 0) begin   // a gap
      end else begin
        sdc_strobe <= 1'b1; sdc_addr <= sdi; sdc_din <= rd_data[sdi];
        if (sdi == 511) sdst <= 3; else sdi <= sdi + 1;
      end
    end
    3: begin sddly <= ($random & 7) + 1; sdst <= 4; end
    4: if (sddly == 0) begin sdc_busy <= 1'b0; sdst <= 0; end else sddly <= sddly - 1;
    10: if (sddly == 0) begin sdc_busy <= 1'b1; sdi <= 0; sddly <= 0; sdst <= 11; sd_writes = sd_writes + 1; end else sddly <= sddly - 1;
    11: begin   // present address sdi, read the register 3 ce28 later
      sdc_addr <= sdi;
      if (sddly == 6) begin
        sddly <= 0; sdst <= 12;
      end else sddly <= sddly + 1;
    end
    12: begin   // one more ce28 for the registered read, then take it
      sdc_addr <= sdi; sdst <= 13;
    end
    13: begin
      sdc_addr <= sdi;
      if (ob_r === wr_data[sdi]) rb_ok = rb_ok + 1; else begin rb_bad = rb_bad + 1; if (rb_bad < 4) $display("READBACK @%0d got %h want %h", sdi, ob_r, wr_data[sdi]); end
      if (wr_data[sdi] != 0) rb_nonzero = rb_nonzero + 1;
      if (sdi == 511) begin sdst <= 14; sddly <= 2; end else begin sdi <= sdi + 1; sddly <= 0; sdst <= 11; end
    end
    14: if (sddly == 0) begin sdc_busy <= 1'b0; sdst <= 0; end else sddly <= sddly - 1;
  endcase
end

// ---- the CPU ----
localparam [7:0] A_DSKLEN = 8'h12, A_DSKDAT = 8'h13, A_DSKDATR = 8'h04;
// wait for the clk7 slot, then (after the edge) apply an access that the next slot samples
task slot_edge; begin
  @(posedge clk); while (!(ce28 && clk7_en)) @(posedge clk);
  pre_do = do_r; pre_a = reg_a;
  if (pre_a == A_DSKDATR) begin cpu_words = cpu_words + 1; if (pre_do !== 16'h0000 && pre_do !== 16'haaaa) cpu_words_data = cpu_words_data + 1; end
  #1;
end endtask
task cpu_reg(input [7:0] a, input [15:0] d); begin
  slot_edge; reg_a = a; data_in = d;
end endtask
task cpu_idle; begin slot_edge; reg_a = 8'h00; end endtask

reg [15:0] ws [0:1023];
integer wn, k, i, cnt;
reg [15:0] wtmp; reg [31:0] H; reg [7:0] b, csl, csh, pl, ph, hb;
reg [7:0] sc_track;

function [15:0] wd8(input [7:0] by);    // data byte in the even bit positions 2i of a word, clock bits 0
  integer ii; reg [15:0] tmp; begin tmp = 16'h0000; for (ii = 0; ii < 8; ii = ii + 1) tmp[2*ii] = by[ii]; wd8 = tmp; end
endfunction
function [7:0] dbyte_of(input [15:0] w);
  integer ii; reg [7:0] tmp; begin tmp = 8'h00; for (ii = 0; ii < 8; ii = ii + 1) tmp[ii] = w[2*ii]; dbyte_of = tmp; end
endfunction

task build_write(input [7:0] sector); begin
  // data bytes -> odd/even bit planes: D[2k] = {o7,e7,o6,e6,o5,e5,o4,e4}, D[2k+1] = {o3,e3,o2,e2,o1,e1,o0,e0}
  for (k = 0; k < 512; k = k + 1) wr_data[k] = $random;
  for (k = 0; k < 256; k = k + 1) begin
    odd_b[k]  = {wr_data[2*k][7], wr_data[2*k][5], wr_data[2*k][3], wr_data[2*k][1], wr_data[2*k+1][7], wr_data[2*k+1][5], wr_data[2*k+1][3], wr_data[2*k+1][1]};
    even_b[k] = {wr_data[2*k][6], wr_data[2*k][4], wr_data[2*k][2], wr_data[2*k][0], wr_data[2*k+1][6], wr_data[2*k+1][4], wr_data[2*k+1][2], wr_data[2*k+1][0]};
  end
  wn = 0;
  for (k = 0; k < 4; k = k + 1) begin ws[wn] = 16'haaaa; wn = wn + 1; end
  ws[wn] = 16'h4489; wn = wn + 1; ws[wn] = 16'h4489; wn = wn + 1;
  H = {8'hff, 8'd1, sector, 8'd11 - sector};     // track byte 1: side 0 of cylinder 0 as the module computes it
  csl = 0; csh = 0;
  for (cnt = 0; cnt < 22; cnt = cnt + 1) begin
    if (cnt < 4) begin
      wtmp = 16'h0000;
      for (i = 0; i < 8; i = i + 1) wtmp[2*i] = H[{~cnt[0], i[2:0], ~cnt[1]}];
      ws[wn] = wtmp;
    end else ws[wn] = wd8($random);
    if (cnt[0]) csh = csh ^ dbyte_of(ws[wn]); else csl = csl ^ dbyte_of(ws[wn]);
    wn = wn + 1;
  end
  ws[wn] = wd8(csl); wn = wn + 1; ws[wn] = wd8(csh); wn = wn + 1;
  // payload: 256 odd-plane bytes, then 256 even-plane bytes (the module writes the first 256 to odd, the next to even)
  pl = 0; ph = 0;
  for (k = 0; k < 512; k = k + 1) begin
    b = (k < 256) ? odd_b[k] : even_b[k-256];
    if (k[0]) ph = ph ^ b; else pl = pl ^ b;
  end
  ws[wn] = wd8($random); wn = wn + 1; ws[wn] = wd8($random); wn = wn + 1;
  ws[wn] = wd8(pl); wn = wn + 1; ws[wn] = wd8(ph); wn = wn + 1;
  for (k = 0; k < 512; k = k + 1) begin ws[wn] = wd8((k < 256) ? odd_b[k] : even_b[k-256]); wn = wn + 1; end
  for (k = 0; k < 2; k = k + 1) begin ws[wn] = 16'haaaa; wn = wn + 1; end
end endtask

integer round, guard, wi, t, gap = 0;
reg seen_blk;
initial begin
  r.fd_dma_is_writing = 1'b0;      // powers up 0 in the FPGA; X in simulation
`ifndef NETLIST
  n.fd_dma_is_writing = 1'b0;
`endif
  r.fd_dma_buffer_trigger_wr = 1'b0;
`ifndef NETLIST
  n.fd_dma_buffer_trigger_wr = 1'b0;
`endif
  r.cpu_wr_state = 4'd0; r.cpu_wr_cnt = 9'd0;   // no reset in the module; 0 in the FPGA
`ifndef NETLIST
  n.cpu_wr_state = 4'd0; n.cpu_wr_cnt = 9'd0;   // the synthesised netlist powers up 0 by itself
`endif
  repeat (40) @(posedge clk);
  reset = 1'b0;
  for (round = 0; round < 4; round = round + 1) begin
    // ---- DMA read, two sectors ----
    for (k = 0; k < 512; k = k + 1) rd_data[k] = $random;
    cpu_reg(A_DSKLEN, 16'h8000 | 16'd1088);
    cpu_reg(A_DSKLEN, 16'h8000 | 16'd1088);
    guard = 0; seen_blk = 0;
    while (!seen_blk && guard < 300000) begin
      slot_edge;
      if (dmal_r) begin reg_a = A_DSKDATR; end else reg_a = 8'h00;
      guard = guard + 1;
      if (blk_r) seen_blk = 1;
      if ((guard % 97) == 0) for (k = 0; k < 512; k = k + 1) rd_data[k] = $random;    // a new sector now and then
    end
    if (!seen_blk) begin errs = errs + 1; $display("TIMEOUT: read DMA did not finish (round %0d)", round); end
    repeat (20) cpu_idle;
    // ---- DMA write of one sector, then the sd card reads the buffer back ----
    build_write(round * 3 + 1);
    cpu_reg(A_DSKLEN, 16'hc000 | wn[13:0]);
    cpu_reg(A_DSKLEN, 16'hc000 | wn[13:0]);
    wi = 0; guard = 0; seen_blk = 0;
    while (!seen_blk && guard < 600000) begin
      slot_edge;
      // one disk DMA word every few slots, as the chipset slots deliver them; the fifo's read-ahead needs the writer to be slower than the reader
      if (gap > 0) gap = gap - 1;
      if (dmas_r && wi < wn && gap == 0) begin reg_a = A_DSKDAT; data_in = ws[wi]; wi = wi + 1; gap = 3 + ($random & 3); end else reg_a = 8'h00;
      guard = guard + 1;
      if (blk_r) seen_blk = 1;
    end
    if (!seen_blk) begin errs = errs + 1; $display("TIMEOUT: write DMA did not finish (round %0d, %0d of %0d words)", round, wi, wn);
      $display("  dbg: fdst=%0d cpust=%0d cnt=%0d dsklen=%h empty=%b trackwr=%b busy=%b wrptr=%0d csum=%h", r.fd_dma_buf_wr_state, r.cpu_wr_state, r.cpu_wr_cnt, r.dsklen, r.fifo_empty, r.trackwr, sdc_busy, r.fd_dma_wr_ptr, r.wr_sector_csum); end
    t = 0; while ((sdst != 0 || !(|wr_r === 1'b0)) && t < 20000) begin @(posedge clk); t = t + 1; end
    repeat (400) @(posedge clk);
  end
  repeat (200) @(posedge clk);
  if (sd_reads < 4 || sd_writes < 4 || cpu_words_data < 3000 || rb_ok < 4 * 512) begin
    errs = errs + 1; $display("COVERAGE too thin: sd_reads=%0d sd_writes=%0d cpu_words_data=%0d rb_ok=%0d", sd_reads, sd_writes, cpu_words_data, rb_ok);
  end
  if (rb_bad != 0) errs = errs + 1;
  $display("tb_floppybuf: cycles=%0d sd_reads=%0d sd_writes=%0d cpu_words=%0d (data %0d) readback ok=%0d bad=%0d nonzero=%0d errs=%0d",
           cycles, sd_reads, sd_writes, cpu_words, cpu_words_data, rb_ok, rb_bad, rb_nonzero, errs);
  if (errs == 0) $display("PASS tb_floppybuf"); else $display("FAIL tb_floppybuf");
  $finish;
end
initial begin #400000000; $display("FAIL tb_floppybuf (watchdog)"); $finish; end
endmodule
