// T92 #17 (4a, 4b): cpc_fdc alone, driven the way u765 drives it (sd_rd / sd_wr with sd_lba, 512 bytes through sd_buff_*), on the DDR2 model.
//   4a: an image whose size is not a multiple of 512: the last block returns the bytes that are in the image and zeros after them, a write to it
//       stores the lines that are inside and drops the rest, and the dirty bit of its chunk is set.
//   4b: the dirty-clear toggle of the app side (cpu_clk) with its data arriving up to 1.2 sys clocks after the toggle (the skew of unsynchronised nets):
//       exactly the bits that were asked for are cleared.
`timescale 1ns / 1ps
module tb_fdcfix;
    reg sys_clk = 1'b0, ui_clk = 1'b0;
    always #15.86 sys_clk = ~sys_clk;
    always #6.667 ui_clk = ~ui_clk;
    reg ui_rst = 1'b1, rst = 1'b1;
    reg [31:0] sd_lba = 0;
    reg [1:0] sd_rd = 0, sd_wr = 0;
    wire sd_ack, sd_buff_wr; wire [8:0] sd_buff_addr; wire [7:0] sd_buff_dout; reg [7:0] sd_buff_din = 0;
    reg [31:0] d0_size = 32'd204544;
    reg clr_tgl = 0, clr_d = 0; reg [2:0] clr_i = 0; reg [7:0] clr_a = 0;
    wire [63:0] dirty0, dirty1; wire busy, oob, ui_busy;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en, c_rdy, c_rd_valid, c_wdf_wren; wire [127:0] c_rd_data, c_wdf_data; wire [15:0] c_wdf_mask;
    cpc_fdc f (
        .clk(sys_clk), .rst(rst), .hold(1'b0),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack), .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
        .sd_buff_wr(sd_buff_wr), .sd_buff_din(sd_buff_din),
        .d0_base(32'h01000000), .d0_size(d0_size), .d1_base(32'h01100000), .d1_size(32'd0), .dirty0(dirty0), .dirty1(dirty1),
        .clr_tgl(clr_tgl), .clr_d(clr_d), .clr_i(clr_i), .clr_a(clr_a), .busy(busy), .oob(oob),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid), .ui_busy(ui_busy)
    );
    ddr2c_model #(.BASE(32'h01000000)) ddr (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data),
        .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));

    reg [7:0] got [0:511];
    always @(posedge sys_clk) if (sd_buff_wr) got[sd_buff_addr] <= sd_buff_dout;
    reg [7:0] wbuf [0:511];
    always @(posedge sys_clk) sd_buff_din <= wbuf[sd_buff_addr];

    function [7:0] pat(input integer a); pat = (a * 7 + (a >> 8) * 13 + 1) & 8'hFF; endfunction
    integer bad = 0, tot = 0;
    integer i, j, n, a, w;
    task block(input wr, input [31:0] lba);
        begin
            @(posedge sys_clk);
            if (wr) sd_wr <= 2'b01; else sd_rd <= 2'b01;
            sd_lba <= lba;
            while (!sd_ack) @(posedge sys_clk);
            sd_rd <= 2'b00; sd_wr <= 2'b00;
            while (sd_ack) @(posedge sys_clk);
            repeat (4) @(posedge sys_clk);
        end
    endtask
    function [7:0] ddrbyte(input integer off);
        reg [127:0] wd;
        begin wd = ddr.mem[off >> 4]; ddrbyte = wd[8 * (off & 15) +: 8]; end
    endfunction
    reg [63:0] exp_d;
    integer skew, ph, pl, cl; reg [7:0] ca;
    initial begin
        for (i = 0; i < 204544 + 512; i = i + 1) begin : fill
            reg [127:0] wd;
            wd = ddr.mem[i >> 4];
            wd[8 * (i & 15) +: 8] = (i < 204544) ? pat(i) : 8'hEE;      // beyond the image the DDR2 holds other bytes: they must not come back
            ddr.mem[i >> 4] = wd;
        end
        repeat (20) @(posedge ui_clk); ui_rst = 1'b0;
        repeat (10) @(posedge sys_clk); rst = 1'b0;
        repeat (30) @(posedge ui_clk);
        // 4a read: the last block (399) holds 256 image bytes, then zeros
        block(1'b0, 32'd399);
        n = 0;
        for (i = 0; i < 512; i = i + 1) if (got[i] !== ((i < 256) ? pat(399 * 512 + i) : 8'h00)) n = n + 1;
        tot = tot + 1;
        if (n) begin bad = bad + 1; $display("  4a read: block 399 differs in %0d of 512 bytes (got[0]=%02x want %02x, got[255]=%02x want %02x, got[256]=%02x want 00)",
            n, got[0], pat(399 * 512), got[255], pat(399 * 512 + 255), got[256]); end
        // and a block entirely inside, a block entirely outside
        block(1'b0, 32'd398);
        n = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== pat(398 * 512 + i)) n = n + 1;
        tot = tot + 1; if (n) begin bad = bad + 1; $display("  4a read: block 398 differs in %0d bytes", n); end
        block(1'b0, 32'd400);
        n = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== 8'h00) n = n + 1;
        tot = tot + 1; if (n) begin bad = bad + 1; $display("  4a read: block 400 (outside) not zero in %0d bytes", n); end
        // 4a write: the last block written with 0xA5: the first 256 bytes land in the image, nothing lands beyond it, the chunk is dirty
        for (i = 0; i < 512; i = i + 1) wbuf[i] = 8'hA5;
        block(1'b1, 32'd399);
        n = 0;
        for (i = 0; i < 256; i = i + 1) if (ddrbyte(399 * 512 + i) !== 8'hA5) n = n + 1;
        for (i = 256; i < 512; i = i + 1) if (ddrbyte(399 * 512 + i) !== 8'hEE) n = n + 1;
        tot = tot + 1; if (n) begin bad = bad + 1; $display("  4a write: block 399 wrong in %0d of 512 bytes", n); end
        tot = tot + 1; if (!dirty0[399 / 32]) begin bad = bad + 1; $display("  4a write: dirty bit %0d not set", 399 / 32); end
        $display("%0s fdcfix 4a: last block of a 204,544 byte image (read, write, outside)", bad ? "FAIL" : "PASS");
        // 4b: set every chunk dirty (a write per chunk), then clear random bytes with the data arriving late
        d0_size = 32'd1048576;
        for (i = 0; i < 512; i = i + 1) wbuf[i] = 8'h11;
        for (i = 0; i < 64; i = i + 1) block(1'b1, i * 32);
        tot = tot + 1; if (dirty0 !== {64{1'b1}}) begin bad = bad + 1; $display("  4b: setup, dirty0 = %016x", dirty0); end
        exp_d = dirty0;
        w = bad;
        pl = 0;
        for (j = 0; j < 60; j = j + 1) begin
            cl = $urandom % 8; ca = $urandom;
            // every chunk of the lane of the last clear and of this one is dirty again (a game writing the disk between two flushes)
            for (i = 0; i < 8; i = i + 1) begin block(1'b1, (8 * pl + i) * 32); block(1'b1, (8 * cl + i) * 32); end
            exp_d[8 * pl +: 8] = 8'hFF; exp_d[8 * cl +: 8] = 8'hFF;
            repeat (6) @(posedge sys_clk);
            ph = $urandom % 31;                           // phase of the app's toggle against sys_clk
            skew = $urandom % 38;                         // ns the data lags the toggle (the sys period is 31.7 ns)
            #(ph);
            clr_tgl = ~clr_tgl;
            #(skew);
            clr_d = 1'b0; clr_i = cl; clr_a = ca;
            exp_d[8 * cl +: 8] = 8'hFF & ~ca;
            repeat (8) @(posedge sys_clk);
            tot = tot + 1;
            if (dirty0 !== exp_d) begin
                bad = bad + 1;
                if (bad - w <= 5) $display("  4b: clear %0d (byte %0d mask %02x after byte %0d, phase %0d skew %0d ns): dirty0 %016x expected %016x", j, cl, ca, pl, ph, skew, dirty0, exp_d);
            end
            pl = cl;
        end
        $display("%0s fdcfix 4b: %0d dirty-clear toggles with data skewed 0..37 ns behind the toggle", (bad - w) ? "FAIL" : "PASS", 60);
        $display("%0s fdcfix: %0d of %0d checks", bad ? "FAIL" : "PASS", tot - bad, tot);
        $finish;
    end
endmodule
