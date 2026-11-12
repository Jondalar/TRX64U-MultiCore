// T92: the FDC image scan under reset/mount timing (u765 + cpc_fdc + the DDR2 model, no machine). Diagnostic: after every mount (with a
// reset of the machine at a chosen distance from it) the main status register must show RQM within SCAN_MS, the way AMSDOS waits for it.
`timescale 1ns / 1ps
module tb_fdcscan;
    reg sys_clk = 1'b0, ui_clk = 1'b0;
    always #15.86 sys_clk = ~sys_clk;
    always #6.667 ui_clk = ~ui_clk;
    reg ui_rst = 1'b1;
    reg board_reset = 1'b1;
    reg [3:0] cdiv = 4'd0;
    reg ce = 1'b0;
    always @(posedge sys_clk) begin cdiv <= cdiv + 4'd1; ce <= (cdiv[1:0] == 2'd0) & cdiv[2]; end   // one in eight
    reg [1:0] ready = 2'b00, img_mounted = 2'b00;
    reg [31:0] img_size = 32'd0;
    wire [7:0] dout;
    wire [31:0] sd_lba; wire [1:0] sd_rd, sd_wr; wire sd_ack, sd_buff_wr; wire [8:0] sd_buff_addr; wire [7:0] sd_buff_dout, sd_buff_din;
    u765 fdc (
        .reset(board_reset), .clk_sys(sys_clk), .ce(ce), .ready(ready), .motor(2'b11), .available(2'b11), .fast(1'b0),
        .a0(1'b0), .nRD(1'b0), .nWR(1'b1), .din(8'd0), .dout(dout),
        .img_mounted(img_mounted), .img_wp(1'b0), .img_size(img_size),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout), .sd_buff_din(sd_buff_din), .sd_buff_wr(sd_buff_wr)
    );
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en, c_rdy, c_rd_valid, c_wdf_wren; wire [127:0] c_rd_data, c_wdf_data; wire [15:0] c_wdf_mask;
    wire [63:0] dirty0, dirty1; wire busy, oob, ui_busy;
    cpc_fdc fdca (
        .clk(sys_clk), .rst(board_reset), .hold(1'b0),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack), .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
        .sd_buff_wr(sd_buff_wr), .sd_buff_din(sd_buff_din),
        .d0_base(32'h01000000), .d0_size(32'd194816), .d1_base(32'h01100000), .d1_size(32'd0), .dirty0(dirty0), .dirty1(dirty1),
        .clr_tgl(1'b0), .clr_d(1'b0), .clr_i(3'd0), .clr_a(8'd0), .busy(busy), .oob(oob),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid), .ui_busy(ui_busy)
    );
    ddr2c_model #(.BASE(32'h01000000)) ddr (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data),
        .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));

    integer uirst_at = -1;
    integer trial, d_mount, d_reset, bad, tot, waited;
    integer seed = 7;
    reg rqm_seen;
    task one(input integer mount_at, input integer reset_at, input integer reset_len);
        integer t;
        begin
            // power-up state: reset held, then released
            board_reset = 1'b1; ready = 2'b00; img_mounted = 2'b00; img_size = 0;
            repeat (300) @(posedge sys_clk);
            board_reset = 1'b0;
            for (t = 0; t < 40000; t = t + 1) begin
                @(posedge sys_clk);
                img_mounted <= 2'b00;
                if (t == mount_at) begin img_mounted <= 2'b01; img_size <= 32'd194816; ready <= 2'b01; end
                if (t == reset_at) board_reset <= 1'b1;
                if (uirst_at >= 0 && t == mount_at + uirst_at) ui_rst <= 1'b1;
                if (uirst_at >= 0 && t == mount_at + uirst_at + 4) ui_rst <= 1'b0;
                if (t == reset_at + reset_len) board_reset <= 1'b0;
            end
            // quiet: the image block service is idle again and the main status shows RQM
            waited = 0; rqm_seen = 0;
            while (!(rqm_seen && !busy && !sd_ack) && waited < 400000) begin
                @(posedge sys_clk); waited = waited + 1;
                if (dout[7]) rqm_seen = 1;
            end
            tot = tot + 1;
            if (!(rqm_seen && !busy && !sd_ack)) begin
                bad = bad + 1;
                $display("  stuck: mount at %0d reset at %0d len %0d uirst %0d: rqm %0d busy %0d ack %0d", mount_at, reset_at, reset_len, uirst_at, rqm_seen, busy, sd_ack);
            end
        end
    endtask
    integer i, j;
    reg [1023:0] ddrfile;
    initial begin
        bad = 0; tot = 0;
        if (!$value$plusargs("DDR=%s", ddrfile)) begin $display("FAIL fdcscan: +DDR=<hex of the 40 track DSK> missing"); $finish; end
        $readmemh(ddrfile, ddr.mem);
        repeat (20) @(posedge ui_clk);
        ui_rst = 1'b0;
        repeat (20) @(posedge ui_clk);
        one(100, 1000000, 10);                        // plain mount, no reset
        for (i = 0; i < 24; i = i + 1)
            one(100, 100 + 37 * i + 3, 256);          // the machine's reset a little after the mount
        for (i = 0; i < 24; i = i + 1)
            one(400 + 23 * i, 100, 256);              // reset first, mount after
        for (i = 0; i < 40; i = i + 1)
            one(100, 100 + 400 * i, 256);             // reset anywhere in the scan
        // the host's DDR2 side is reset (4 ui clocks) at every 25th sys clock of the scan: the block service must come back by itself
        for (i = 0; i < 80; i = i + 1) begin uirst_at = 40 + 25 * i; one(100, 1000000, 10); end
        $display("%0s fdcscan: %0d of %0d mount/reset/ui_rst timings ended with the block service idle and RQM up", bad ? "FAIL" : "PASS", tot - bad, tot);
        $finish;
    end
endmodule
