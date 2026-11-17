// A1200 copy of the AGA wide fetch bench (T97, Discussion #94): amiga_mem (cores/amiga1200/rtl) + a behavioural DDR2, the chip bus and
// the fast RAM port (Z2 and Zorro III) driven by hand. Rule: dout48[47:32] is the word at +2 bytes of every even requested word
// address a (sh[a+1]); for a = 0 mod 4 words, dout48 = {sh[a+1], sh[a+2], sh[a+3]}. Odd word addresses: dout only.
// Miss (critical-word path) and hit (cache banks): groups of 8 words read word 0..7 in turn. Because the first read of a line is the miss,
// a group only proves the miss path for word 0; therefore "first read" tests read word 2 and word 6 (4 mod 8 bytes) of fresh lines first,
// for the chip port, the Zorro II fast port and the Zorro III port, and fail if mem.st_miss does not rise by one (a hit would not test crit48).
// Controls are chosen in run.sh by env var, which builds a patched copy of amiga_mem.v: OLD=1 = both old lines (hit48 and crit48),
// OLD=crit = only the crit48 line (hit48 stays new). Both must FAIL.
// Z3: Z3_BASE is overridden to DDR2 word 0x8000 so the model's 64K-word memory holds it; the address path p2_addr[24] is the real one.
`timescale 1ns/1ps
module tb_wide;
    reg clk = 0, ui_clk = 0;
    always #15.86 clk = ~clk;
    always #6.667 ui_clk = ~ui_clk;
    reg por = 1, flush = 1; integer cyc = 0;
    always @(posedge clk) begin cyc <= cyc + 1; if (cyc == 20) por <= 0; if (cyc == 900) flush <= 0; end
    reg ce28 = 0, clk7_en = 0, cs = 0, we = 0; reg [21:0] addr = 0; reg [15:0] din = 0;
    reg p2_sel = 0; reg [24:0] p2_addr = 0;
    wire [15:0] dout, p2_dout; wire [47:0] dout48_w, p2_dout48_w; wire pend, p2_ready;
    wire [47:0] dout48 = dout48_w, p2_dout48 = p2_dout48_w;
    wire ui_rst, calib;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en; wire [127:0] c_wdf_data; wire [15:0] c_wdf_mask; wire c_wdf_wren;
    wire [127:0] app_rd_data; wire app_rd_valid, app_rdy, app_wdf_rdy;
    mig_ui_model #(.MEM_WORDS(1<<20), .RD_LAT(8), .MISS(0), .STALL_PCT(0), .CALIB(100)) ddr (
        .ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(c_addr), .app_cmd(c_cmd), .app_en(c_en & (c_cmd == 3'b001 | app_wdf_rdy)), .app_rdy(app_rdy),
        .app_wdf_data(c_wdf_data), .app_wdf_mask(c_wdf_mask), .app_wdf_wren(c_wdf_wren & app_rdy),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_valid));
    wire c_rdy = app_rdy & ((c_cmd == 3'b001) | app_wdf_rdy);
    amiga_mem #(.BASE(27'd0), .Z3_BASE(27'h8000)) mem (
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
    // a = word address as the port sees it, ri = index into sh (differs for Zorro III: DDR2 word Z3_BASE + a)
    task check(input [24:0] a, input [31:0] ri, input [15:0] d, input [47:0] d48, input [127:0] tag);
        reg bad; reg [47:0] want; begin
            n = n + 1;
            want = {sh[ri + 1], sh[ri + 2], sh[ri + 3]};
            bad = d !== sh[ri];
            if (!a[0]) begin
                if (a[1]) bad = bad | (d48[47:32] !== want[47:32]);
                else      bad = bad | (d48 !== want);
            end
            if (bad) begin
                errs = errs + 1;
                if (errs < 8) $display("FAIL %0s a=%h: dout %h (want %h) dout48 %h (want %h)", tag, a, d, sh[ri], d48, want);
            end
        end endtask
    task chip_rd(input [21:0] a, input [127:0] tag); begin
        @(posedge clk); cs <= 1; addr <= a; ce28 <= 1; clk7_en <= 1;
        @(posedge clk); ce28 <= 0; clk7_en <= 0;
        repeat (3) @(posedge clk);
        while (pend) @(posedge clk);
        @(posedge clk);
        check({3'b000, a}, a, dout, dout48, tag);
        cs <= 0; repeat (4) @(posedge clk);
    end endtask
    task chip_wr(input [21:0] a, input [15:0] d); begin
        @(posedge clk); cs <= 1; we <= 1; addr <= a; din <= d; ce28 <= 1; clk7_en <= 1; sh[a] = d;
        @(posedge clk); ce28 <= 0; clk7_en <= 0;
        repeat (3) @(posedge clk);
        while (pend) @(posedge clk);
        @(posedge clk); cs <= 0; we <= 0; repeat (6) @(posedge clk);
    end endtask
    task p2_rd(input [24:0] a, input [127:0] tag); reg r; begin
        @(posedge clk); r = p2_ready; p2_sel <= 1; p2_addr <= a;
        while (p2_ready === r) @(posedge clk);
        @(posedge clk);
        check(a, a[24] ? 32'h8000 + a[23:0] : a, p2_dout, p2_dout48, tag);
        p2_sel <= 0; repeat (6) @(posedge clk);
    end endtask
    // first read of a fresh line at word w: must be a miss (st_miss +1), else the bench does not test the critical-word path
    integer m0;
    task chip_miss1(input [21:0] a, input [127:0] tag); begin
        m0 = mem.st_miss; chip_rd(a, tag);
        if (mem.st_miss != m0 + 1) begin errs = errs + 1; $display("FAIL %0s a=%h was not a miss (st_miss %0d -> %0d)", tag, a, m0, mem.st_miss); end
    end endtask
    task p2_miss1(input [24:0] a, input [127:0] tag); begin
        m0 = mem.st_miss; p2_rd(a, tag);
        if (mem.st_miss != m0 + 1) begin errs = errs + 1; $display("FAIL %0s a=%h was not a miss (st_miss %0d -> %0d)", tag, a, m0, mem.st_miss); end
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
        for (w = 0; w < 8; w = w + 1) p2_rd(25'h1000100 + w, "z3 miss");
        for (w = 7; w >= 0; w = w - 1) p2_rd(25'h1000100 + w, "z3 hit");
        for (w = 0; w < 8; w = w + 1) p2_rd(25'h1000118 + w, "z3 miss2");
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h0408 + w, "chip miss 2nd line");
        for (w = 0; w < 8; w = w + 1) chip_rd(22'h0408 + w, "chip hit 2nd line");
        // first reads (misses) at word 2 and word 6 of fresh lines, neighbours untouched (lines 0x40 words apart)
        chip_miss1(22'h5002, "chip miss w2");
        chip_miss1(22'h5106, "chip miss w6");
        p2_miss1(25'h6202, "z2 miss w2");
        p2_miss1(25'h6306, "z2 miss w6");
        p2_miss1(25'h1000402, "z3 miss w2");
        p2_miss1(25'h1000506, "z3 miss w6");
        $display("wide fetch: %0d reads, %0d errors; amiga_mem reads %0d miss %0d", n, errs, mem.st_reads, mem.st_miss);
        if (errs == 0) $display("PASS tb_wide"); else $display("FAIL tb_wide");
        $finish;
    end
    initial begin #50000000; $display("FAIL timeout"); $finish; end
endmodule
