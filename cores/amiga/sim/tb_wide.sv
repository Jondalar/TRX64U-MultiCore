// A1200 stripes: amiga_mem + a behavioural DDR2, the chip bus and the fast RAM port driven by hand. An AGA wide fetch
// (FMODE 1..3) takes the 8-byte aligned group: dout = word 0, dout48 = {word 1, word 2, word 3} (the order denise_bitplanes
// and nanomig use). Reads a known pattern: miss (critical-word path), hit (cache banks), every word of the line as the
// requested one, chip and fast RAM port. -DNEG feeds the consumers the old tie-off (48'd0) and must FAIL.
`timescale 1ns/1ps
module tb_wide;
    reg clk = 0, ui_clk = 0;
    always #15.86 clk = ~clk;
    always #6.667 ui_clk = ~ui_clk;
    reg por = 1, flush = 1; integer cyc = 0;
    always @(posedge clk) begin cyc <= cyc + 1; if (cyc == 20) por <= 0; if (cyc == 900) flush <= 0; end
    reg ce28 = 0, clk7_en = 0, cs = 0, we = 0; reg [21:0] addr = 0; reg [15:0] din = 0;
    reg p2_sel = 0; reg [21:0] p2_addr = 0;
    wire [15:0] dout, p2_dout; wire [47:0] dout48_w, p2_dout48_w; wire pend, p2_ready;
`ifdef NEG
    wire [47:0] dout48 = 48'd0, p2_dout48 = 48'd0;
`else
    wire [47:0] dout48 = dout48_w, p2_dout48 = p2_dout48_w;
`endif
    wire ui_rst, calib;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en; wire [127:0] c_wdf_data; wire [15:0] c_wdf_mask; wire c_wdf_wren;
    wire [127:0] app_rd_data; wire app_rd_valid, app_rdy, app_wdf_rdy;
    mig_ui_model #(.MEM_WORDS(1<<20), .RD_LAT(8), .MISS(0), .STALL_PCT(0), .CALIB(100)) ddr (
        .ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(c_addr), .app_cmd(c_cmd), .app_en(c_en & (c_cmd == 3'b001 | app_wdf_rdy)), .app_rdy(app_rdy),
        .app_wdf_data(c_wdf_data), .app_wdf_mask(c_wdf_mask), .app_wdf_wren(c_wdf_wren & app_rdy),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_valid));
    wire c_rdy = app_rdy & ((c_cmd == 3'b001) | app_wdf_rdy);
    amiga_mem #(.BASE(27'd0)) mem (
        .sys_clk(clk), .sys_rst(por), .flush(flush), .ce28(ce28), .clk7_en(clk7_en),
        .cs(cs), .we(we), .addr(addr), .be(2'b11), .din(din), .dout(dout), .dout48(dout48_w), .pend(pend),
        .p2_sel(p2_sel), .p2_wr(1'b0), .p2_addr(p2_addr), .p2_be(2'b11), .p2_din(16'd0),
        .p2_dout(p2_dout), .p2_dout48(p2_dout48_w), .p2_ready(p2_ready),
        .ui_clk(ui_clk), .ui_rst(ui_rst | ~calib), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(app_wdf_rdy),
        .c_rd_data(app_rd_data), .c_rd_valid(app_rd_valid));

    function [15:0] pat(input [21:0] a); pat = {a[7:0] ^ 8'h5A, a[11:4] + 8'h31} ^ {a[15:8], 4'h9, a[3:0]}; endfunction
    integer i, errs = 0, n = 0;
    initial for (i = 0; i < 65536; i = i + 1) ddr.mem[i >> 3][16*(i & 7) +: 16] = {pat(i)[7:0], pat(i)[15:8]};

    reg [15:0] sh [0:65535];   // what the memory holds now: pat, then the writes of the bench
    initial for (i = 0; i < 65536; i = i + 1) sh[i] = pat(i);
    // expected: the 8-byte aligned group of word address a
    task check(input [21:0] a, input [15:0] d, input [47:0] d48, input [127:0] tag);
        reg [21:0] g; begin
            g = {a[21:2], 2'b00}; n = n + 1;
            if (d !== sh[a] || d48 !== {sh[g + 1], sh[g + 2], sh[g + 3]}) begin
                errs = errs + 1;
                if (errs < 8) $display("FAIL %0s a=%h: dout %h (want %h) dout48 %h (want %h)", tag, a, d, sh[a], d48, {sh[g + 1], sh[g + 2], sh[g + 3]});
            end
        end endtask
    task chip_rd(input [21:0] a, input [127:0] tag); begin
        @(posedge clk); cs <= 1; addr <= a; ce28 <= 1; clk7_en <= 1;
        @(posedge clk); ce28 <= 0; clk7_en <= 0;
        repeat (3) @(posedge clk);
        while (pend) @(posedge clk);
        @(posedge clk);
        check(a, dout, dout48, tag);
        cs <= 0; repeat (4) @(posedge clk);
    end endtask
    task chip_wr(input [21:0] a, input [15:0] d); begin
        @(posedge clk); cs <= 1; we <= 1; addr <= a; din <= d; ce28 <= 1; clk7_en <= 1; sh[a] = d;
        @(posedge clk); ce28 <= 0; clk7_en <= 0;
        repeat (3) @(posedge clk);
        while (pend) @(posedge clk);
        @(posedge clk); cs <= 0; we <= 0; repeat (6) @(posedge clk);
    end endtask
    task p2_rd(input [21:0] a, input [127:0] tag); reg r; begin
        @(posedge clk); r = p2_ready; p2_sel <= 1; p2_addr <= a;
        while (p2_ready === r) @(posedge clk);
        @(posedge clk);
        check(a, p2_dout, p2_dout48, tag);
        p2_sel <= 0; repeat (6) @(posedge clk);
    end endtask
    integer w;
    initial begin
        wait (!flush); repeat (3000) @(posedge clk);
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h0400 + w, "chip miss");        // miss, each word of the line as the wanted one
        for (w = 7; w >= 0; w = w - 1) chip_rd(22'h0400 + w, "chip hit");        // hit
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h1238 + w, "chip miss2");
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h1238 + w, "chip hit2");
        for (w = 0; w < 8; w = w + 1) p2_rd(22'h2000 + w, "p2 miss");
        for (w = 0; w < 8; w = w + 1) p2_rd(22'h2000 + w, "p2 hit");
        p2_rd(22'h0400, "p2 hit chip line");
        chip_rd(22'h2008, "chip hit p2 line");
        // the picture is drawn into a cached line (CPU or blitter writes), then the display reads it: hit after writes
        for (w = 0; w < 8; w = w + 1) chip_wr(22'h0400 + w, 16'hA000 + w);
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h0400 + w, "chip hit after writes");
        for (w = 0; w < 8; w = w + 1) chip_wr(22'h3000 + w, 16'hB000 + w);   // line not cached: written, then read (miss)
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h3000 + w, "chip miss after writes");
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h3000 + w, "chip hit after miss");
        $display("wide fetch: %0d reads, %0d errors; amiga_mem reads %0d miss %0d", n, errs, mem.st_reads, mem.st_miss);
        if (errs == 0) $display("PASS tb_wide"); else $display("FAIL tb_wide");
        $finish;
    end
    initial begin #50000000; $display("FAIL timeout"); $finish; end
endmodule
