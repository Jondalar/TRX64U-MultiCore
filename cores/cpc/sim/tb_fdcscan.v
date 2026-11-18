// T92: the FDC image scan under reset/mount timing (u765 + cpc_fdc + the DDR2 model, no machine). Diagnostic: after every mount (with a
// reset of the machine at a chosen distance from it) the main status register must show RQM within SCAN_MS, the way AMSDOS waits for it.
// Groups: fdcscan (mount/reset/ui_rst), fdcscan2 (a second strobe in the scan, hold_q), fdcscan3 (a second strobe over the scan end, the trackinfo load, the
// search/data phase of a READ DATA and of a WRITE DATA, drive 1 ejected during its seek, both mounts in reset, a machine reset with two disks in, a
// strobe over the scan end that brings a DIFFERENT image; then a READ DATA of drive 0). Drive 1's image is not a copy of drive 0's (img_b: 6 sectors on
// track 0 instead of 9, sector data 0xA5 instead of 0xE5): the cases that mount it check the track table and the first data byte of each drive.
`timescale 1ns / 1ps
`ifndef S3_CYCLES
`define S3_CYCLES 125                          // u765 CYCLES = the drive's clocks per ms (the real core: 4000): a byte clock every CYCLES*32/1000 visits of a drive (2 ce each, one ce per 8 sys clocks here, about 4 in the core) = 64 sys clocks per byte (core about 1000), a revolution 0.41M sys clocks (core about 6.5M): the drive is 16x faster than the core's
`endif
`ifndef S3_PW
`define S3_PW 12                              // sys clocks of each third of a data register access (u765 sees /RD and /WR on its ce, one in 8): the bench CPU takes about 42 sys clocks per byte (measured, the reference line),
`endif                                         // less than the drive's 64 as on the board (the Z80 reads a sector in about 6 ms, the drive takes 16 ms for it)
`ifndef S3A
`define S3A 60
`endif
`ifndef S3B
`define S3B 40
`endif
`ifndef S3C
`define S3C 30
`endif
`ifndef S3C0
`define S3C0 330000
`endif
`ifndef S3CS
`define S3CS 3000
`endif
`ifndef S3W
`define S3W 16
`endif
`ifndef S3W0
`define S3W0 380000
`endif
`ifndef S3WS
`define S3WS 2500
`endif
`ifndef S3D
`define S3D 60
`endif
`ifndef S3R
`define S3R 28                                   // typ 4: both mounts in reset, 1..8 then 100..2000 clocks apart
`endif
`ifndef S3X
`define S3X 16                                   // typ 5: a machine reset with a disk in A: and B: at 80,000 + 0..15 clocks
`endif
`ifndef S3M
`define S3M 60                                   // typ 6: a second strobe over the scan end with a different image behind it (the scan-end timings of group 1)
`endif
`ifndef S3D0
`define S3D0 200
`endif
`ifndef S3DS
`define S3DS 400
`endif
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
    reg b_a0 = 1'b0, b_nrd = 1'b1, b_nwr = 1'b1;     // the CPU side of the FDC: idle = main status on dout (fdcscan3 drives commands over it)
    reg [7:0] b_din = 8'd0;
    reg [31:0] img_size = 32'd0;
    reg [31:0] d1sz = 32'd0;                          // the size of drive 1's image in cpc_fdc (typ 2, 4, 5, 6 kind 1 and the H3 case give it a disk)
    wire [7:0] dout;
    wire [31:0] sd_lba; wire [1:0] sd_rd, sd_wr; wire sd_ack, sd_buff_wr, sd_err; wire [8:0] sd_buff_addr; wire [7:0] sd_buff_dout, sd_buff_din;
    u765 #(.CYCLES(`S3_CYCLES)) fdc (
        .reset(board_reset), .clk_sys(sys_clk), .ce(ce), .ready(ready), .motor(2'b11), .available(2'b11), .fast(1'b0),
        .a0(b_a0), .nRD(b_nrd), .nWR(b_nwr), .din(b_din), .dout(dout),
        .img_mounted(img_mounted), .img_wp(1'b0), .img_size(img_size),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack), .sd_err(sd_err),
        .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout), .sd_buff_din(sd_buff_din), .sd_buff_wr(sd_buff_wr)
    );
    wire [1:0] dbg_ss0 = fdc.fdc.image_scan_state[0], dbg_ss1 = fdc.fdc.image_scan_state[1];
    wire dbg_lock = fdc.fdc.i_scan_lock;
    wire dbg_rdy0 = fdc.fdc.image_ready[0];
    wire [5:0] dbg_state = fdc.fdc.state;
    wire dbg_tl = fdc.tinfo_lock, dbg_dirty = fdc.fdc.image_trackinfo_dirty[0];
    wire [2:0] dbg_seek = fdc.fdc.seek_state[0];
    wire [7:0] dbg_ts0 = fdc.fdc.i_current_track_sectors[0][0], dbg_ts1 = fdc.fdc.i_current_track_sectors[1][0];
    wire dbg_rdy1 = fdc.fdc.image_ready[1];
    wire [2:0] dbg_sss = fdc.fdc.sector_search_state;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en, c_rdy, c_rd_valid, c_wdf_wren; wire [127:0] c_rd_data, c_wdf_data; wire [15:0] c_wdf_mask;
    wire [63:0] dirty0, dirty1; wire busy, oob, ui_busy;
    cpc_fdc fdca (
        .clk(sys_clk), .rst(board_reset), .hold(hold),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack), .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
        .sd_buff_wr(sd_buff_wr), .sd_buff_din(sd_buff_din), .sd_err(sd_err),
        .d0_base(32'h01000000), .d0_size(32'd194816), .d1_base(32'h01100000), .d1_size(d1sz), .dirty0(dirty0), .dirty1(dirty1),
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
                4: ;                                                                         // no event (debugging)
                5: begin img_mounted <= 2'b01; img_size <= 32'd194816; ready <= 2'b11; end       // mount drive 0 while drive 1 holds a disk
                3: begin img_mounted <= 2'b10; img_size <= 32'd0; ready <= 2'b01; end       // eject drive 1 (drive 0 stays)
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

    // T92 #17 (fdcscan3): the second mount/eject strobe over the places where u765 keeps a lock (the scan end, the trackinfo load with seek states
    // 4..7, a READ DATA in its command/search phase), with hold_q (the menu freeze) over the strobe pair; every case must then finish a READ DATA
    // (cmd 0x46, track 0, sector 0xC1, 512 bytes + 7 result bytes) with RQM back high and the main status idle (0x80). The strobes and the hold come
    // from one injector (clk_n - ev_zero), the READ DATA runs over the CPU bus (b_*) like AMSDOS: byte by byte on RQM/DIO, never while hold_q is up.
    integer ev_zero = 0, ev_t0 = -1, ev_t1 = -1, ev_t2 = -1, ev_k0 = 0, ev_k1 = 0, ev_k2 = 0, hd_t0 = -1, hd_t1 = -1;
    reg ev_active = 1'b0;
    reg ev_swap = 1'b0;                                     // strobe 2 comes with a different image in drive 0's DDR2 slot (typ 6 kind 0)
    reg s3v;
    initial s3v = $test$plusargs("S3V");                    // +S3V: trace the events and the FDC state of the fdcscan3 cases (debugging)
    always @(posedge sys_clk) if (ev_active) begin
        img_mounted <= 2'b00;
        if (s3v && (clk_n - ev_zero == ev_t0 || clk_n - ev_zero == ev_t1 || clk_n - ev_zero == hd_t0 || clk_n - ev_zero == hd_t1)) $display("  ev %0d: t0 %0d t1 %0d hd %0d..%0d hold %0d state %0d status %02x", clk_n - ev_zero, ev_t0, ev_t1, hd_t0, hd_t1, hold, dbg_state, dout);
        if (clk_n - ev_zero == ev_t0) strobe(ev_k0);
        if (clk_n - ev_zero == ev_t1) begin if (ev_swap) mk_img_b(0); strobe(ev_k1); end
        if (clk_n - ev_zero == ev_t2) strobe(ev_k2);
        if (clk_n - ev_zero == hd_t0) hold <= 1'b1;
        if (clk_n - ev_zero == hd_t1) hold <= 1'b0;
    end
    reg [5:0] st_d = 6'd0; reg [7:0] su_d = 8'd0;
    always @(posedge sys_clk) if (s3v && ev_active && clk_n - ev_zero < 200000 && (dbg_state != st_d || dout != su_d)) begin
        st_d <= dbg_state; su_d <= dout; $display("  trace %0d: u765 state %0d status %02x bus a0 %0d nrd %0d nwr %0d ss %0d/%0d ready0 %0d", clk_n - ev_zero, dbg_state, dout, b_a0, b_nrd, b_nwr, dbg_ss0, dbg_ss1, dbg_rdy0);
    end

    // invariants of the lock scheme (counted, a case that raises one is bad): two owners of tinfo_lock at once (seek 4..7 with sector search 2..4), a scan
    // running beside a trackinfo owner, a scan that parses header byte 0 although no mount block was served since it took the lock (a dropped request)
    integer dbl_n = 0, h4_n = 0, lost_n = 0;
    reg lk_d = 1'b0, bm_d = 1'b0, served = 1'b0;
    wire tseek = (fdc.fdc.seek_state[0] >= 3'd4) || (fdc.fdc.seek_state[1] >= 3'd4);
    wire tsrch = (fdc.fdc.sector_search_state >= 3'd2);
    wire scanrun = (fdc.fdc.image_scan_state[0] >= 2'd2) || (fdc.fdc.image_scan_state[1] >= 2'd2);
    always @(posedge sys_clk) begin
        lk_d <= fdc.fdc.i_scan_lock; bm_d <= fdc.sd_busy_mount;
        if (fdc.fdc.i_scan_lock && !lk_d) served <= 1'b0;
        else if (fdc.sd_busy_mount && !bm_d) served <= 1'b1;
        if (!board_reset && tseek && tsrch) begin dbl_n <= dbl_n + 1; if (dbl_n < 3) $display("  MON two trackinfo owners at %0d: seek %0d/%0d search %0d", clk_n - ev_zero, fdc.fdc.seek_state[0], fdc.fdc.seek_state[1], fdc.fdc.sector_search_state); end
        if (!board_reset && scanrun && (tseek || tsrch)) begin h4_n <= h4_n + 1; if (h4_n < 3) $display("  MON a scan beside a trackinfo owner at %0d", clk_n - ev_zero); end
        if (!board_reset && ce && fdc.fdc.image_scan_state[fdc.fdc.i_current_drive] == 2'd2 && !fdc.sd_busy_mount && fdc.sd_rd_mount == 2'b00 && !fdc.buff_wait && fdc.buff_addr == 9'd0 && fdc.fdc.i_scan_lock && !served) begin
            lost_n <= lost_n + 1; if (lost_n < 3) $display("  MON a scan reads header byte 0 with no mount block served, drive %0d at %0d (busy tinfo %0d sector %0d)", fdc.fdc.i_current_drive, clk_n - ev_zero, fdc.sd_busy_tinfo, fdc.sd_busy_sector);
        end
    end
    integer nbytes, svc_clk;
    reg [7:0] dfirst = 8'h00;                                // the first data byte of the last READ DATA
    reg svc_ok;
    reg [55:0] rbytes = 56'd0;                               // the last 7 bytes read from the data register (the result phase: ST0 ST1 ST2 C H R N)
    task pulse_rd;                                           // one data-register read, the way the Z80 holds /RD (it waits while frozen)
        begin
            while (hold) @(posedge sys_clk);
            b_a0 <= 1'b1; b_nrd <= 1'b0;
            repeat (`S3_PW) @(posedge sys_clk);
            while (hold) @(posedge sys_clk);
            repeat (`S3_PW) @(posedge sys_clk);
            rbytes = {rbytes[47:0], dout};
            if (nbytes == 0) dfirst = dout;
            b_nrd <= 1'b1; @(posedge sys_clk); b_a0 <= 1'b0;
            repeat (`S3_PW) @(posedge sys_clk);
            while (hold) @(posedge sys_clk);
            nbytes = nbytes + 1; svc_clk = svc_clk + 3 * `S3_PW + 1;
        end
    endtask
    task pulse_wr(input [7:0] v);
        begin
            while (hold) @(posedge sys_clk);
            b_a0 <= 1'b1; b_din <= v; b_nwr <= 1'b0;
            repeat (`S3_PW) @(posedge sys_clk);
            while (hold) @(posedge sys_clk);
            repeat (`S3_PW) @(posedge sys_clk);
            b_nwr <= 1'b1; @(posedge sys_clk); b_a0 <= 1'b0;
            repeat (`S3_PW) @(posedge sys_clk);
            while (hold) @(posedge sys_clk);
            svc_clk = svc_clk + 3 * `S3_PW + 1;
        end
    endtask
    // READ DATA (wr 0) or WRITE DATA (wr 1), drive 0 head 0 C=0 H=0 R=0xC1 N=2 EOT=0xC1 GPL=0x2A DTL=0xFF; svc_ok = main status (bits 7..4) back to 0x80
    // within maxclk sys clocks. A write sends 0xE5 whenever the FDC asks for a byte in the execution phase (DIO low, EXM set): nwrote counts them.
    integer nwrote, rw_t0 = 0;
    reg [7:0] rw_unit = 8'h00;                               // the unit of the READ/WRITE DATA (0 = A:, 1 = B:)
    reg [7:0] rd_mask = 8'hFF;                               // the bits of the main status that must read 0x80 at the end (0xF0: the drives' seek-busy bits are not asked)
    task rw_data(input integer wr, input integer maxclk);
        integer k;
        reg [7:0] cb [0:8];
        begin
            cb[0] = wr ? 8'h45 : 8'h46; cb[1] = rw_unit; cb[2] = 8'h00; cb[3] = 8'h00; cb[4] = 8'hC1; cb[5] = 8'h02; cb[6] = 8'hC1; cb[7] = 8'h2A; cb[8] = 8'hFF;
            nbytes = 0; nwrote = 0; svc_clk = 0; svc_ok = 1'b1; rw_t0 = clk_n; x6_clk = 0; x8_clk = 0; x6_at = -1; x8_at = -1;
            for (k = 0; k < 9; k = k + 1) begin
                while (!(dout[7] && !dout[6]) && svc_clk < maxclk) begin @(posedge sys_clk); svc_clk = svc_clk + 1; end
                if (svc_clk < maxclk) pulse_wr(cb[k]);
            end
            nbytes = 0; nwrote = 0;
            repeat (40) @(posedge sys_clk);
            while ((dout & rd_mask) != (8'h80 & rd_mask) && svc_clk < maxclk) begin
                @(posedge sys_clk); svc_clk = svc_clk + 1;
                if (dout[7] && dout[6]) pulse_rd;
                else if (wr && dout[7] && dout[5]) begin pulse_wr(8'hE5); nwrote = nwrote + 1; end
            end
            svc_ok = ((dout & rd_mask) == (8'h80 & rd_mask)) && (svc_clk < maxclk);
        end
    endtask
    task read_data(input integer maxclk);
        rw_data(0, maxclk);
    endtask
    // SEEK drive 1 to track ncn (3 command bytes, returns when the FDC has them; the head steps on)
    task seek1(input integer ncn);
        integer k;
        reg [7:0] cb [0:2];
        begin
            cb[0] = 8'h0F; cb[1] = 8'h01; cb[2] = ncn;
            svc_clk = 0;
            for (k = 0; k < 3; k = k + 1) begin
                while (!(dout[7] && !dout[6]) && svc_clk < 100000) begin @(posedge sys_clk); svc_clk = svc_clk + 1; end
                pulse_wr(cb[k]);
            end
        end
    endtask
    // how long a READ DATA stays in EXEC6 (the CPU takes the bytes; state 13 in u765's enum) and EXEC8 (waits for the end of the sector, GAP3; state 15)
    integer x6_clk = 0, x8_clk = 0, x6_at = -1, x8_at = -1;
    always @(posedge sys_clk) begin
        if (dbg_state == 6'd13) begin x6_clk <= x6_clk + 1; if (x6_at < 0) x6_at <= clk_n - rw_t0; end
        if (dbg_state == 6'd15) begin x8_clk <= x8_clk + 1; if (x8_at < 0) x8_at <= clk_n - rw_t0; end
    end
    reg [1023:0] ddrfile;
    integer ci;
    // img_b: the blank 40 track DSK with track 0 changed: 6 sectors instead of 9 (Track-Info byte 0x15) and 0xA5 in the sectors instead of 0xE5. `base` is the
    // word index of the slot in the DDR2 model (0: drive 0 at d0_base, 65536: drive 1 at d1_base)
    integer bi;
    task mk_img_b(input integer base);
        begin
            ddr.mem[base + 17][47:40] = 8'd6;                  // byte 0x115 = 256 + 0x15: the sector count of track 0
            for (bi = 32; bi < 320; bi = bi + 1) ddr.mem[base + bi] = {16{8'hA5}};   // bytes 0x200..0x13FF: the 9 sectors of track 0
        end
    endtask
    task load_ddr;                                           // the image again (a WRITE DATA of an earlier case changed it); drive 1's slot holds img_b
        begin
            $readmemh(ddrfile, ddr.mem);
            for (ci = 0; ci < 12176; ci = ci + 1) ddr.mem[65536 + ci] = ddr.mem[ci];
            mk_img_b(65536);
        end
    endtask
    integer f6_at = -1, f6_clk = 0, f8_at = -1, f8_clk = 0;   // EXEC6/EXEC8 of the first READ/WRITE DATA of a typ 1/3 case
    integer gsel;
    integer s3_bad, s3_tot, s3_gbad, s3_gtot, s3_soft, s3_mon = 0, rd_max = 800000;
    // READ DATA of unit u (0 = A:, 1 = B:) must deliver its full sector (519 bytes with the result phase) from ITS image: the first data byte (0xE5 image A,
    // 0xA5 img_b) and the sectors on track 0 in the track table of that drive (9 / 6) are the ones of the image mounted there
    reg chk_ok;
    task chk_unit(input [7:0] u, input [7:0] first, input [7:0] tsx);
        reg [7:0] tsn;
        begin
            rw_unit = u; read_data(rd_max);
            tsn = u ? dbg_ts1 : dbg_ts0;
            chk_ok = svc_ok && nbytes == 519 && dfirst == first && tsn == tsx;
            if (!chk_ok && s3_gbad < 4) $display("  READ DATA of %0s: status %02x, %0d bytes (519), first data byte %02x (%02x), sectors on track 0 %0d (%0d), result %014x", u ? "B:" : "A:", dout, nbytes, dfirst, first, tsn, tsx, rbytes);
            rw_unit = 8'h00;
        end
    endtask
    // typ 0: strobe 2 at 100+p after strobe 1 (scan end / trackinfo window); typ 1: strobe 2 at p after the READ DATA command was started;
    // typ 3: the same with a WRITE DATA; typ 2 (H3): both drives hold a disk, a SEEK of drive 1 is started, drive 1 is ejected p clocks later
    // (kind is not used), then a READ DATA of drive 0 must end (drive 1's trackinfo reload is what drive 0's sector search would wait for)
    // typ 4: both mounts in reset p clocks apart (kind 0 A: first, 1 B: first), the reset released, then a READ DATA of A: and of B: (519 bytes each) must end
    // typ 5: A: and B: mounted 20,000 clocks apart out of reset, settled, a machine reset of 300 clocks at 80,000 + p (both drives ready and dirty), then A: and B:
    // typ 6: strobe 2 over the scan end, 100+p after the mount of A:, with a different image behind it: kind 0 drive 0 is remounted with img_b (its DDR2 slot is
    //   rewritten at the strobe), kind 1 B: (img_b) is mounted; afterwards every mounted drive must read its own image and show its own track table
    // kind 0 = mount again, 1 = eject (followed by a mount 30,000 clocks later); hd = hold_q from 300 clocks before strobe 2 for 4300 clocks
    task three(input integer typ, input integer kind, input integer p, input integer hd);
        integer base, t_end, mon0;
        reg ok1, ok2, monbad;
        begin
            ev_active = 1'b0; hold = 1'b0;
            load_ddr;
            board_reset = 1'b1; ready = 2'b00; img_mounted = 2'b00; img_size = 0; b_a0 = 1'b0; b_nrd = 1'b1; b_nwr = 1'b1;
            repeat (100) @(posedge sys_clk);
            // every case starts from the same drive state: both drives ejected (the reset does not clear image_ready), the sector search idle
            img_mounted = 2'b01; @(posedge sys_clk); img_mounted = 2'b10; @(posedge sys_clk); img_mounted = 2'b00;
            repeat (300) @(posedge sys_clk);
            if (typ != 4) board_reset = 1'b0;                 // typ 4: the machine stays in reset over both mounts (the T180 restore)
            ok1 = 1'b1; svc_clk = 0;
            d1sz = (typ == 2 || typ == 4 || typ == 5 || (typ == 6 && kind == 1)) ? 32'd194816 : 32'd0;
            mon0 = dbl_n + h4_n + lost_n;
            ev_zero = clk_n; ev_t0 = 100; ev_k0 = 0; ev_t1 = -1; ev_t2 = -1; hd_t0 = -1; hd_t1 = -1; ev_swap = 1'b0; ev_active = 1'b1;
            if (typ == 0) begin
                base = 100 + p;
                ev_t1 = base; ev_k1 = kind;
                if (kind == 1) begin ev_t2 = base + 30000; ev_k2 = 0; end
                if (hd) begin hd_t0 = base - 300; hd_t1 = base + 4000; end
                t_end = base + 70000;
                while (clk_n - ev_zero < t_end) @(posedge sys_clk);
            end else if (typ == 4) begin
                ev_t0 = 100; ev_k0 = kind ? 2 : 0;            // kind 0: A: then B:, kind 1: B: then A:; p clocks apart, both in reset, then the reset is released
                ev_t1 = 100 + p; ev_k1 = kind ? 5 : 2;
                while (clk_n - ev_zero < 8000) @(posedge sys_clk);
                board_reset = 1'b0;
                while (clk_n - ev_zero < 60000) @(posedge sys_clk);
                if (s3v) $display("  typ 4 before the reads: sectors/track %0d %0d ready %0d %0d", dbg_ts0, dbg_ts1, dbg_rdy0, dbg_rdy1);
                chk_unit(8'h00, 8'hE5, 8'd9); ok1 = chk_ok;
                chk_unit(8'h01, 8'hA5, 8'd6); ok1 = ok1 && chk_ok;
                t_end = 0;
            end else if (typ == 5) begin
                ev_t1 = 20000; ev_k1 = 2;
                while (clk_n - ev_zero < 80000 + p) @(posedge sys_clk);
                board_reset = 1'b1; repeat (300) @(posedge sys_clk); board_reset = 1'b0;
                while (clk_n - ev_zero < 160000) @(posedge sys_clk);
                chk_unit(8'h00, 8'hE5, 8'd9); ok1 = chk_ok;
                chk_unit(8'h01, 8'hA5, 8'd6); ok1 = ok1 && chk_ok;
                t_end = 0;
            end else if (typ == 6) begin
                base = 100 + p;
                ev_t1 = base; ev_k1 = kind ? 2 : 0; ev_swap = (kind == 0);
                if (hd) begin hd_t0 = base - 300; hd_t1 = base + 4000; end
                while (clk_n - ev_zero < base + 70000) @(posedge sys_clk);
                hold = 1'b0;
                chk_unit(8'h00, kind ? 8'hE5 : 8'hA5, kind ? 8'd9 : 8'd6); ok1 = chk_ok;
                if (kind) begin chk_unit(8'h01, 8'hA5, 8'd6); ok1 = ok1 && chk_ok; end
                t_end = 0;
            end else if (typ == 2) begin
                ev_t1 = 20000; ev_k1 = 2;                     // drive 1 gets its own image img_b (the DDR2 model holds it at d1_base)
                while (clk_n - ev_zero < 80000) @(posedge sys_clk);
                if (!$test$plusargs("H3NOSEEK")) seek1(20);
                ev_zero = clk_n; ev_t0 = p; ev_k0 = (kind == 0) ? 3 : kind; ev_t1 = -1;
                if (hd) begin hd_t0 = (p > 300) ? p - 300 : 0; hd_t1 = p + 4000; end
                t_end = p + 70000;
                while (clk_n - ev_zero < t_end) @(posedge sys_clk);
                rd_mask = 8'hF0;
            end else begin
                while (clk_n - ev_zero < 40000) @(posedge sys_clk);
                ev_zero = clk_n; ev_t0 = p; ev_k0 = kind; ev_t1 = -1;
                if (kind == 1) begin ev_t1 = p + 30000; ev_k1 = 0; end
                if (hd) begin hd_t0 = (p > 300) ? p - 300 : 0; hd_t1 = p + 4000; end
                rw_data(typ == 3, rd_max);                   // READ/WRITE DATA 1, the strobe lands in it
                ok1 = svc_ok; f6_at = x6_at; f6_clk = x6_clk; f8_at = x8_at; f8_clk = x8_clk;
                t_end = p + 70000;
                while (clk_n - ev_zero < t_end) @(posedge sys_clk);
            end
            hold = 1'b0;
            read_data(rd_max);                               // the following READ DATA
            ok2 = svc_ok;
            rd_mask = 8'hFF;
            if (typ == 2 && ok2 && nbytes != 519) ok2 = 1'b0;   // H3: the READ DATA of drive 0 must also deliver its sector
            s3_tot = s3_tot + 1; s3_gtot = s3_gtot + 1;
            if (ok2 && nbytes != 519) s3_soft = s3_soft + 1;
            monbad = (dbl_n + h4_n + lost_n) != mon0;          // an invariant of the lock scheme broke during the case (the MON lines above)
            if (monbad) s3_mon = s3_mon + 1;
            if (!ok2 || !ok1 || monbad) begin
                s3_bad = s3_bad + 1; s3_gbad = s3_gbad + 1;
                if (s3_gbad <= 12) $display("  HANG3: typ %0d kind %0d p %0d hold %0d: READ DATA 1 %0s, the following one %0s (monitors %0d, status %02x, %0d bytes, u765 state %0d, scan %0d/%0d lock %0d, ready0 %0d, tinfo_lock %0d dirty %0d seek %0d search %0d)",
                    typ, kind, p, hd, ok1 ? "ended" : "HUNG", ok2 ? "ended" : "HUNG", monbad, dout, nbytes, dbg_state, dbg_ss0, dbg_ss1, dbg_lock, dbg_rdy0, dbg_tl, dbg_dirty, dbg_seek, dbg_sss);
            end
            ev_active = 1'b0;
        end
    endtask
    integer i, j, kd;
    initial begin
        bad = 0; tot = 0;
        if (!$value$plusargs("DDR=%s", ddrfile)) begin $display("FAIL fdcscan: +DDR=<hex of the 40 track DSK> missing"); $finish; end
        load_ddr;
        repeat (20) @(posedge ui_clk);
        ui_rst = 1'b0;
        repeat (20) @(posedge ui_clk);
        if ($test$plusargs("TWOONE")) begin                      // +TWOONE +TWOK=kind +TWOO=off +TWOH=hold_at +TWOL=hold_len: one fdcscan2 case (debugging)
            integer tk, to, th, tl;
            if (!$value$plusargs("TWOK=%d", tk)) tk = 0; if (!$value$plusargs("TWOO=%d", to)) to = 0;
            if (!$value$plusargs("TWOH=%d", th)) th = -1; if (!$value$plusargs("TWOL=%d", tl)) tl = 0;
            g_bad = 0; g_tot = 0; two_bad = 0; two_tot = 0;
            two(tk, to, th, tl, 0);
            $display("  two one: kind %0d off %0d hold %0d+%0d: %0d bad", tk, to, th, tl, g_bad);
            $finish;
        end
        if (!$test$plusargs("S3ONLY")) begin                  // +S3ONLY: only the fdcscan3 group (debugging)
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
        end
        if ($test$plusargs("S3ONE")) begin                       // +S3ONE +S3T=typ +S3K=kind +S3P=p +S3H=hold: one case, verbose (debugging)
            integer ot, ok_, op, oh;
            if (!$value$plusargs("S3T=%d", ot)) ot = 0; if (!$value$plusargs("S3K=%d", ok_)) ok_ = 0;
            if (!$value$plusargs("S3P=%d", op)) op = 0; if (!$value$plusargs("S3H=%d", oh)) oh = 0;
            s3_bad = 0; s3_tot = 0; s3_soft = 0; s3_gbad = 0; s3_gtot = 0;
            if ($test$plusargs("S3REFFIRST")) three(0, 0, 100000, 0);
            s3_bad = 0;
            three(ot, ok_, op, oh);
            $display("  rdata one: typ %0d kind %0d p %0d hold %0d: bad %0d, status %02x, %0d bytes, hold %0d, state %0d; first command: EXEC6 from %0d for %0d, EXEC8 from %0d for %0d clocks; last 7 bytes read %014x; sectors/track %0d %0d ready1 %0d", ot, ok_, op, oh, s3_bad, dout, nbytes, hold, dbg_state, f6_at, f6_clk, f8_at, f8_clk, rbytes, dbg_ts0, dbg_ts1, dbg_rdy1);
            $finish;
        end
        // fdcscan3 (+S3SKIP leaves it out, CI runs it as its own bench)
        if (!$test$plusargs("S3SKIP")) begin
        s3_bad = 0; s3_tot = 0; s3_soft = 0;
        s3_gbad = 0; s3_gtot = 0;
        if (!$value$plusargs("S3G=%d", gsel)) gsel = 255;      // +S3G=<mask>: groups 1 scan end, 2 inside READ DATA (early), 4 READ DATA data phase, 8 WRITE DATA data phase, 16 H3, 32 both mounts in reset, 64 machine reset with two disks, 128 scan end with a different image (debugging)
        three(0, 0, 100000, 0);                              // reference: one mount, a READ DATA: how long it takes
        $display("  rdata reference: READ DATA took %0d sys clocks for %0d bytes, ended %0d; the data phase (EXEC6) from %0d for %0d clocks = %0d per byte (the drive: 64), the wait for the end of the sector (EXEC8) from %0d for %0d clocks",
            svc_clk, nbytes, svc_ok, x6_at, x6_clk, x6_clk / 512, x8_at, x8_clk);
        if ($test$plusargs("S3REF")) $finish;                // +S3REF: only the reference READ DATA (debugging)
        for (kd = 0; kd < 2; kd = kd + 1) for (j = 0; j < 2; j = j + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3A && gsel[0] && !$test$plusargs("S3NOA"); i = i + 1) three(0, kd, 3000 + 35 * i, j);
            $display("  rdata strobe over the scan end and the trackinfo window, kind %0d hold %0d: %0d of %0d ended without RQM", kd, j, s3_gbad, s3_gtot);
        end
        for (kd = 0; kd < 2; kd = kd + 1) for (j = 0; j < 2; j = j + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3B && gsel[1]; i = i + 1) three(1, kd, (i < 12) ? 1500 + 307 * i : (i < 24) ? 5000 + 1999 * (i - 12) : 30000 + 17011 * (i - 24), j);
            $display("  rdata strobe inside a READ DATA, kind %0d hold %0d: %0d of %0d ended without RQM", kd, j, s3_gbad, s3_gtot);
        end
        // the data phase and the wait for the end of the sector (EXEC6..EXEC8, about 350k..430k after the command), READ DATA and WRITE DATA
        for (kd = 0; kd < 2; kd = kd + 1) for (j = 0; j < 2; j = j + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3C && gsel[2]; i = i + 1) three(1, kd, `S3C0 + `S3CS * i, j);
            $display("  rdata strobe in the data phase of a READ DATA, kind %0d hold %0d: %0d of %0d ended without RQM", kd, j, s3_gbad, s3_gtot);
        end
        for (kd = 0; kd < 2; kd = kd + 1) for (j = 0; j < 2; j = j + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3W && gsel[3]; i = i + 1) three(3, kd, `S3W0 + `S3WS * i, j);
            $display("  rdata strobe in the data phase of a WRITE DATA, kind %0d hold %0d: %0d of %0d ended without RQM", kd, j, s3_gbad, s3_gtot);
        end
        // H3: drive 1 ejected while it seeks, then a READ DATA of drive 0
        for (j = 0; j < 2; j = j + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3D && gsel[4]; i = i + 1) three(2, 0, `S3D0 + `S3DS * i, j);
            $display("  rdata drive 1 ejected during its seek, then a READ DATA of drive 0, hold %0d: %0d of %0d ended without RQM", j, s3_gbad, s3_gtot);
        end
        // the T180 restore: both disks mounted in reset, 1..2000 clocks apart (A: first / B: first), then a READ DATA of A: and of B:
        for (kd = 0; kd < 2; kd = kd + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3R && gsel[5]; i = i + 1) three(4, kd, (i < 8) ? 1 + i : 100 * (i - 7), 0);
            $display("  rdata both mounts in reset, %0s first, then a READ DATA of A: and of B:: %0d of %0d ended without 519 bytes / RQM", kd ? "B:" : "A:", s3_gbad, s3_gtot);
        end
        // a machine reset (300 clocks) with a disk in A: and in B:, 16 clock phases, then a READ DATA of A: and of B: (each its own image, full sector)
        s3_gbad = 0; s3_gtot = 0;
        for (i = 0; i < `S3X && gsel[6]; i = i + 1) three(5, 0, i, 0);
        $display("  rdata machine reset with a disk in A: and B:, then a READ DATA of A: and of B:: %0d of %0d ended without their own sector / RQM", s3_gbad, s3_gtot);
        // the scan-end strobe of group 1 with a different image: drive 0 remounted with img_b / B: (img_b) mounted over drive 0's block in flight
        for (kd = 0; kd < 2; kd = kd + 1) for (j = 0; j < 2; j = j + 1) begin
            s3_gbad = 0; s3_gtot = 0;
            for (i = 0; i < `S3M && gsel[7]; i = i + 1) three(6, kd, 3000 + 35 * i, j);
            $display("  rdata strobe over the scan end with %0s, hold %0d: %0d of %0d ended without their own image (data + track table) / RQM", kd ? "B: mounted" : "A: remounted with another image", j, s3_gbad, s3_gtot);
        end
        $display("%0s fdcscan3: %0d of %0d strobe-pair timings ended a READ DATA with RQM up (%0d without the 519 bytes; %0d broke a lock invariant: two trackinfo owners %0d, scan beside an owner %0d, scan on a lost block %0d clocks)", s3_bad ? "FAIL" : "PASS", s3_tot - s3_bad, s3_tot, s3_soft, s3_mon, dbl_n, h4_n, lost_n);
        end
        $finish;
    end
endmodule
