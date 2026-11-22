`timescale 1ns/1ps
// bench: the app's GROM load. The app writes at aperture offsets (BASIC +0x0000, KERNAL +0x2000, CHAR +0x4000); the host turns them into dma_addr as
// cores/common/dma_win.v describes (char 0x0000, KERNAL 0x2000, BASIC 0x4000); ti99_win undoes that, WBHI = 0x10 and WBADD = n * 0x20 pick GROM n;
// the mainboard's decode (verbatim) hands the byte to grom_storage_bram (6 slots). The bytes are checked through the same path and in the arrays.
module tb_load;
  reg clk = 0; always #15 clk = ~clk;
  reg tgl = 0; reg [15:0] a = 0; reg we = 0; reg [7:0] wd = 0; reg [7:0] hi = 8'h10; reg [7:0] add = 0;
  wire done; wire [7:0] rd;
  wire [23:0] m_adr; wire [7:0] m_dat; wire m_we, m_stb, m_cyc; wire [7:0] s_dat; wire s_ack; wire busy;
  ti99_win dut(.clk(clk), .dma_req_tgl(tgl), .dma_addr(a), .dma_we(we), .dma_wdata(wd), .wbhi(hi), .wbadd(add),
               .dma_done_tgl(done), .dma_rdata(rd), .m_adr(m_adr), .m_dat(m_dat), .m_we(m_we), .m_stb(m_stb),
               .m_cyc(m_cyc), .s_dat(s_dat), .s_ack(s_ack), .busy(busy));
  wire [0:23] wb_adr_i = m_adr;
  reg wb_stb_grom; reg [0:7] wb_dat_o; reg wb_ack_o;
  wire [0:7] wb_dat_grom; wire wb_ack_grom;
  always @(*) begin
    wb_dat_o <= 8'h00; wb_ack_o <= 1'b0; wb_stb_grom <= 1'b0;
    if (wb_adr_i[0]) begin end
    else case (wb_adr_i[1 +: 3])
      3'h1: begin wb_stb_grom <= m_stb; wb_dat_o <= wb_dat_grom; wb_ack_o <= wb_ack_grom; end
      default: ;
    endcase
  end
  assign s_dat = wb_dat_o; assign s_ack = wb_ack_o;
  grom_storage_bram #(.NUM_GROMS(6)) gs(.clk(clk), .prefetch_grom_set(4'd0), .prefetch_grom_addr(16'd0), .do_prefetch(1'b0),
      .prefetch_complete(), .prefetch_data(), .wb_adr_i(wb_adr_i[23 -: 16]), .wb_dat_i(m_dat), .wb_dat_o(wb_dat_grom), .wb_we_i(m_we),
      .wb_sel_i(1'b1), .wb_stb_i(wb_stb_grom), .wb_ack_o(wb_ack_grom), .wb_cyc_i(m_cyc));

  // the host: app aperture offset -> dma_addr
  function [15:0] host(input [15:0] ofs);
    begin
      if (ofs < 16'h2000)       host = 16'h4000 + ofs;               // BASIC
      else if (ofs < 16'h4000)  host = ofs;                          // KERNAL
      else if (ofs < 16'h7000)  host = {4'h0, ofs[11:0]};            // CHAR, mirrored
      else                      host = 16'h6000 + {6'd0, ofs[9:0]};  // colour RAM
    end
  endfunction
  // the app: rom_write / rom_read of one byte at an aperture offset
  task xfer(input [15:0] ofs, input w, input [7:0] d); reg d0; begin
    @(posedge clk); #1 a = host(ofs); we = w; wd = d; d0 = done; tgl = ~tgl; wait (done != d0); @(posedge clk); repeat (3) @(posedge clk);
  end endtask

  reg [7:0] img [0:2][0:6143];
  integer n, i, errs = 0;
  initial begin
    for (n = 0; n < 3; n = n + 1) for (i = 0; i < 6144; i = i + 1) img[n][i] = $random;
    repeat (10) @(posedge clk);
    for (n = 0; n < 3; n = n + 1) begin
      hi = 8'h10; add = n * 8'h20;
      for (i = 0; i < 6144; i = i + 1) xfer(i, 1'b1, img[n][i]);
    end
    // read back through the same path
    for (n = 0; n < 3; n = n + 1) begin
      hi = 8'h10; add = n * 8'h20;
      for (i = 0; i < 6144; i = i + 1) begin xfer(i, 1'b0, 8'h00); if (rd !== img[n][i]) begin errs = errs + 1; if (errs < 8) $display("FAIL rb GROM %0d +%0d got %h want %h", n, i, rd, img[n][i]); end end
    end
    // and in the arrays: GROM n page p byte o is file byte p * 2048 + o
    for (i = 0; i < 2048; i = i + 1) begin
      if (gs.gr[0].m0[i] !== img[0][i] || gs.gr[0].m1[i] !== img[0][2048 + i] || gs.gr[0].m2[i] !== img[0][4096 + i]) begin errs = errs + 1; if (errs < 8) $display("FAIL array GROM 0 +%0d", i); end
      if (gs.gr[1].m0[i] !== img[1][i] || gs.gr[1].m1[i] !== img[1][2048 + i] || gs.gr[1].m2[i] !== img[1][4096 + i]) begin errs = errs + 1; if (errs < 8) $display("FAIL array GROM 1 +%0d", i); end
      if (gs.gr[2].m0[i] !== img[2][i] || gs.gr[2].m1[i] !== img[2][2048 + i] || gs.gr[2].m2[i] !== img[2][4096 + i]) begin errs = errs + 1; if (errs < 8) $display("FAIL array GROM 2 +%0d", i); end
    end
    if (errs == 0) $display("PASS tb_load (3 GROMs, 18432 bytes, through the host's address map)"); else $display("FAILED %0d", errs);
    $finish;
  end
  initial begin #2000000000; $display("TIMEOUT"); $finish; end
endmodule
