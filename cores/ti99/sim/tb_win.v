`timescale 1ns/1ps
// bench: ti99_win against a model of console_rom's wishbone side (same ack/read timing as the real file)
module tb_win;
  reg clk = 0; always #15 clk = ~clk;
  reg tgl = 0; reg [15:0] a = 0; reg we = 0; reg [7:0] wd = 0; reg [7:0] hi = 0; reg [7:0] add = 0;
  wire done; wire [7:0] rd;
  wire [23:0] m_adr; wire [7:0] m_dat; wire m_we, m_stb, m_cyc; reg [7:0] s_dat; reg s_ack = 0; wire busy;
  ti99_win dut(.clk(clk), .dma_req_tgl(tgl), .dma_addr(a), .dma_we(we), .dma_wdata(wd), .wbhi(hi), .wbadd(add),
               .dma_done_tgl(done), .dma_rdata(rd), .m_adr(m_adr), .m_dat(m_dat), .m_we(m_we), .m_stb(m_stb),
               .m_cyc(m_cyc), .s_dat(s_dat), .s_ack(s_ack), .busy(busy));
  // slave model: two 4K byte arrays selected by adr bit 12, console_rom style (ack one clock after stb, data valid with ack)
  reg [7:0] mem [0:'h1FFF]; reg [7:0] q;
  always @(posedge clk) begin
    s_ack <= m_cyc && m_stb && !s_ack && (m_adr[23:13] == 11'h008);       // only 0x010000..0x011FFF answers
    if (m_cyc && m_stb && !m_we) q <= mem[m_adr[12:0]];
    if (m_cyc && m_stb && m_we && s_ack) mem[m_adr[12:0]] <= m_dat;
  end
  always @* s_dat = q;
  integer errs = 0; integer i; reg d0;
  initial begin
    for (i = 0; i < 'h2000; i = i + 1) mem[i] = 0;
    repeat (10) @(posedge clk);
    for (i = 0; i < 16; i = i + 1) begin
      hi = 8'h01; repeat (6) @(posedge clk);
      a = 16'h4000 + i; we = 1; wd = 8'hA0 + i; d0 = done; tgl = ~tgl;
      wait (done != d0); @(posedge clk); repeat (4) @(posedge clk);
    end
    for (i = 0; i < 16; i = i + 1) begin
      hi = 8'h01; repeat (6) @(posedge clk);
      a = 16'h4000 + i; we = 0; wd = 0; d0 = done; tgl = ~tgl;
      wait (done != d0); @(posedge clk);
      if (rd !== 8'hA0 + i) begin errs = errs + 1; $display("FAIL rd %0d got %h", i, rd); end
      repeat (4) @(posedge clk);
    end
    // the low-bytes ROM at 0x011000: window offset 0x1000 with wbhi 1
    hi = 8'h01; repeat (6) @(posedge clk); a = 16'h5000; we = 1; wd = 8'h5A; d0 = done; tgl = ~tgl; wait (done != d0); repeat (5) @(posedge clk);
    if (mem['h1000] !== 8'h5A) begin errs = errs + 1; $display("FAIL lowbyte write"); end
    // an address nobody answers: wbhi 0x10 -> times out, reads 0, still completes
    hi = 8'h10; repeat (6) @(posedge clk); a = 16'h4000; we = 0; d0 = done; tgl = ~tgl;
    wait (done != d0); @(posedge clk);
    if (rd !== 8'h00) begin errs = errs + 1; $display("FAIL timeout read %h", rd); end
    // back to back requests without gaps from the app side (toggle again right after done)
    hi = 8'h01; repeat (6) @(posedge clk);
    for (i = 0; i < 8; i = i + 1) begin
      a = 16'h4100 + i; we = 1; wd = 8'h10 + i; d0 = done; tgl = ~tgl; wait (done != d0); @(posedge clk);
    end
    for (i = 0; i < 8; i = i + 1) if (mem['h100 + i] !== 8'h10 + i) begin errs = errs + 1; $display("FAIL b2b %0d", i); end
    // the host's block-relative addresses and WBADD: only m_adr is looked at (nobody answers wbhi 0x10 here: the request times out)
    begin : maps
      integer k; reg [15:0] ain [0:5]; reg [7:0] hin [0:5]; reg [7:0] adn [0:5]; reg [23:0] want [0:5];
      ain[0] = 16'h4000; hin[0] = 8'h10; adn[0] = 8'h00; want[0] = 24'h100000;     // BASIC block, offset 0           -> linear 0x0000
      ain[1] = 16'h5123; hin[1] = 8'h10; adn[1] = 8'h00; want[1] = 24'h101123;     // BASIC block, +0x1123            -> linear 0x1123
      ain[2] = 16'h2000; hin[2] = 8'h10; adn[2] = 8'h00; want[2] = 24'h102000;     // KERNAL block                    -> linear 0x2000
      ain[3] = 16'h0000; hin[3] = 8'h10; adn[3] = 8'h00; want[3] = 24'h104000;     // CHAR block                      -> linear 0x4000
      ain[4] = 16'h4000; hin[4] = 8'h10; adn[4] = 8'h40; want[4] = 24'h104000;     // GROM 2 through the BASIC block: WBADD = 2 * 0x20
      ain[5] = 16'h4ABC; hin[5] = 8'h10; adn[5] = 8'h20; want[5] = 24'h102ABC;     // GROM 1, +0x0ABC
      for (k = 0; k < 6; k = k + 1) begin
        hi = hin[k]; add = adn[k]; repeat (6) @(posedge clk);
        a = ain[k]; we = 0; d0 = done; tgl = ~tgl; wait (done != d0); @(posedge clk);
        if (m_adr !== want[k]) begin errs = errs + 1; $display("FAIL map %0d: %h want %h", k, m_adr, want[k]); end
        repeat (4) @(posedge clk);
      end
      add = 0;
    end
    if (errs == 0) $display("PASS ti99_win"); else $display("FAILED %0d", errs);
    $finish;
  end
  initial begin #20000000; $display("TIMEOUT"); $finish; end
endmodule
