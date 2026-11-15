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
    reg hold = 1'b0;                                  // hold_q of cpc_core: ce_16 (and so ce_fdc) stops, the rest runs on
    always @(posedge sys_clk) begin cdiv <= cdiv + 4'd1; ce <= (cdiv[1:0] == 2'd0) & cdiv[2] & ~hold; end   // one in eight
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
    wire [1:0] dbg_ss0 = fdc.fdc.image_scan_state[0], dbg_ss1 = fdc.fdc.image_scan_state[1];
    wire dbg_lock = fdc.fdc.i_scan_lock;
    wire dbg_rdy0 = fdc.fdc.image_ready[0];
    wire [5:0] dbg_state = fdc.fdc.state;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en, c_rdy, c_rd_valid, c_wdf_wren; wire [127:0] c_rd_data, c_wdf_data; wire [15:0] c_wdf_mask;
    wire [63:0] dirty0, dirty1; wire busy, oob, ui_busy;
    cpc_fdc fdca (
        .clk(sys_clk), .rst(board_reset), .hold(hold),
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

    // T92 #17: a second mount/eject strobe during or after a scan, the way cpc_core delivers them (one sys clock wide, drive 0 first).
    //   kind 0 = mount drive 0 again, 1 = eject drive 0, 2 = mount drive 1 (size 0 in the DDR2 model: its scan ends at the header check)
    //   hold_at >= 0: hold_q for hold_len clocks starting there; in_reset: the first mount is delivered while board_reset is high (the T180 restore)
    // how long a scan takes: the first three mounts of drive 0 and the clocks until image_ready rises (the window in which a second event can find the lock set)
    integer m_t = 0, m_n = 0, clk_n = 0;
    reg rdy_d = 1'b0, mnt_d = 1'b0;
    always @(posedge sys_clk) begin
        clk_n <= clk_n + 1; rdy_d <= dbg_rdy0; mnt_d <= img_mounted[0];
        if (img_mounted[0] && !mnt_d) m_t <= clk_n;
        if (dbg_rdy0 && !rdy_d && m_n < 3) begin m_n <= m_n + 1; $display("  scan: image 0 ready %0d sys clocks after its mount strobe (%0d ns)", clk_n - m_t, (clk_n - m_t) * 31.7); end
    end
    integer two_bad, two_tot, g_bad, g_tot;
    task strobe(input integer kind);
        begin
            case (kind)
                0: begin img_mounted <= 2'b01; img_size <= 32'd194816; ready <= 2'b01; end
                1: begin img_mounted <= 2'b01; img_size <= 32'd0; ready <= 2'b00; end
                default: begin img_mounted <= 2'b10; img_size <= 32'd194816; ready <= 2'b11; end
            endcase
        end
    endtask
    task two(input integer kind, input integer off, input integer hold_at, input integer hold_len, input integer in_reset);
        integer t;
        reg ok;
        begin
            board_reset = 1'b1; ready = 2'b00; img_mounted = 2'b00; img_size = 0; hold = 1'b0;
            repeat (300) @(posedge sys_clk);
            if (!in_reset) board_reset = 1'b0;
            for (t = 0; t < 40000; t = t + 1) begin
                @(posedge sys_clk);
                img_mounted <= 2'b00;
                if (t == 100) strobe(0);
                if (t == 100 + off) strobe(kind);
                if (hold_at >= 0 && t == hold_at) hold <= 1'b1;
                if (hold_at >= 0 && t == hold_at + hold_len) hold <= 1'b0;
                if (in_reset && t == 8000) board_reset <= 1'b0;      // released after both events
            end
            hold = 1'b0;
            if (kind == 1) begin                                     // the eject leaves RQM up by itself: the next mount shows whether the lock came back
                @(posedge sys_clk); strobe(0);
                @(posedge sys_clk); img_mounted <= 2'b00;
                repeat (40000) @(posedge sys_clk);
            end
            waited = 0; rqm_seen = 0;
            while (!(rqm_seen && !busy && !sd_ack) && waited < 3000) begin
                @(posedge sys_clk); waited = waited + 1;
                if (dout[7]) rqm_seen = 1;
            end
            ok = rqm_seen && !busy && !sd_ack && (dbg_rdy0 || kind == 2);   // and the image of drive 0 has been scanned to the end (kind 2 only asks for RQM, see §5)
            two_tot = two_tot + 1; g_tot = g_tot + 1;
            if (!ok) begin
                two_bad = two_bad + 1; g_bad = g_bad + 1;
                if (g_bad <= 2) $display("  HANG: kind %0d off %0d hold %0d+%0d in_reset %0d: RQM low or image 0 not ready (rdy0 %0d), scan_state %0d/%0d i_scan_lock %0d u765 state %0d busy %0d",
                    kind, off, hold_at, hold_len, in_reset, dbg_rdy0, dbg_ss0, dbg_ss1, dbg_lock, dbg_state, busy);
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
        two_bad = 0; two_tot = 0;
        // (a) a second event at every 8th sys clock of the scan, all three kinds (mount drive 0 again, eject drive 0, mount drive 1)
        for (j = 0; j < 3; j = j + 1) begin
            g_bad = 0; g_tot = 0;
            for (i = 0; i < 100; i = i + 1) two(j, 8 * i + 2, -1, 0, 0);
            $display("  second event every 8th clock, kind %0d: %0d of %0d ended without RQM", j, g_bad, g_tot);
        end
        // the T180 restore: the events while the machine is in reset (released afterwards)
        for (j = 0; j < 3; j = j + 1) begin
            g_bad = 0; g_tot = 0;
            for (i = 0; i < 10; i = i + 1) two(j, 400 * i + 2, -1, 0, 1);
            $display("  both events in reset, kind %0d: %0d of %0d ended without RQM", j, g_bad, g_tot);
        end
        // hold_q (Freeze or the app's hold) landing anywhere in the scan, the second event during the hold
        for (j = 0; j < 3; j = j + 1) begin
            g_bad = 0; g_tot = 0;
            for (i = 0; i < 30; i = i + 1) two(j, 4000, 100 + 25 * i, 8000, 0);
            $display("  hold in the scan, event in the hold, kind %0d: %0d of %0d ended without RQM", j, g_bad, g_tot);
        end
        $display("%0s fdcscan2: %0d of %0d second-event timings ended with RQM up", two_bad ? "FAIL" : "PASS", two_tot - two_bad, two_tot);
        $display("%0s fdcscan: %0d of %0d mount/reset/ui_rst timings ended with the block service idle and RQM up", bad ? "FAIL" : "PASS", tot - bad, tot);
        $finish;
    end
endmodule
