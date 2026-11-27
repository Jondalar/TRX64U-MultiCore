// T265 P4: av_rgb (the host's RGB out) with the real pix_layer (its second requester in the DDR2 fetcher, with the HDMI pixel side running on the
// same buffer), mig_arb and the MIG UI model. A synthetic RGB565 picture in the DDR2 model; the words on the pins are compared, every sys_clk of
// every checked frame, with a model written out here from the spec (T265 P4, D10):
//   raster of each standard (line = 2016 / 2080 clocks, frame = 312 / 263 lines, hsync 148 / 152 clocks, three broad lines), the window
//   (91 dots = 364 clocks after the sync edge, 1600 clocks wide, black outside), FB, the levels (x 11/16: black 0, white 175), the two-line
//   average (a source taller than 1.25 x the window), 1:1 with bars or crop, the 4:3 squeeze (960 source pixels over the window), the flip
//   to the other buffer, the standard follows the core's pix_y raster (std 0), mode off.
// PASS only when every phase checked at least one frame without a mismatch and the HDMI side counted no underrun.
`timescale 1ps/1ps
module tb;
    reg cpu_clk = 0, ui_clk = 0, pix_clk = 0, sys_clk = 0;
    always #5000  cpu_clk = ~cpu_clk;
    always #6667  ui_clk  = ~ui_clk;
    reg hdmi_on = 1;
    always #3299  if (hdmi_on) pix_clk = ~pix_clk;
    always #15860 sys_clk = ~sys_clk;

    localparam [31:0] BUF0 = 32'h080000, BUF1 = 32'h280000, BUF2 = 32'h480000;

    // ---- MIG and arbiter (only the video client is used)
    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(1 << 19)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));
    wire [26:0] v_addr; wire [2:0] v_cmd; wire v_en, v_rdy, v_wdf_wren, v_wdf_rdy, v_valid, v_urgent;
    wire [127:0] v_wdf_data; wire [15:0] v_wdf_mask;
    wire [127:0] cl_line, k_data; wire k_rdy, k_valid; wire [63:0] cl_rdata;
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl(3'b000), .addr(78'd0), .we(3'b000), .wdata(96'd0), .be(12'd0),
        .done_tgl(), .rdata(cl_rdata), .rdata_line(cl_line),
        .c_addr(27'd0), .c_cmd(3'b001), .c_en(1'b0), .c_rdy(k_rdy), .c_wdf_data(128'd0),
        .c_wdf_mask(16'hFFFF), .c_wdf_wren(1'b0), .c_wdf_rdy(), .c_rd_data(k_data), .c_rd_data_valid(k_valid),
        .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy), .v_wdf_data(v_wdf_data),
        .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren), .v_wdf_rdy(v_wdf_rdy), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .e_addr(27'd0), .e_cmd(3'd0), .e_en(1'b0), .e_rdy(), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    // ---- the pixel layer: registers written like the app does, the HDMI side on a 1080p timing
    reg         p_we = 0, p_re = 0; reg [11:0] p_addr = 0; reg [7:0] p_wdata = 0; wire [7:0] p_rdata;
    reg [11:0] h = 0; reg [10:0] vv = 0;
    always @(posedge pix_clk) begin
        h <= (h == 2687) ? 0 : h + 1;
        if (h == 2687) vv <= (vv == 1124) ? 0 : vv + 1;
    end
    wire [6:0] av_ctrl; wire [15:0] av_w, av_h, av_stride; wire [31:0] av_front, av_back; wire av_cfg, av_fr, av_bk;
    wire a_rq_tgl, a_rq_two, a_rq_pair, a_done_tgl, a_we_a, a_we_b; wire [26:4] a_rq_addr; wire [15:0] a_rq_stride; wire [7:0] a_rq_n, a_wa;
    wire [127:0] a_wd;
    reg  [8:0]  pix_y = 0;
    pix_layer dut (.clk_sys(cpu_clk), .we(p_we), .re(p_re), .addr(p_addr), .wdata(p_wdata), .rdata(p_rdata), .ev_vsync(), .ev_flip(),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy),
        .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(k_data), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .av_ctrl(av_ctrl), .av_w(av_w), .av_h(av_h), .av_stride(av_stride), .av_front(av_front), .av_back(av_back),
        .av_cfg_tgl(av_cfg), .av_front_tgl(av_fr), .av_back_tgl(av_bk),
        .a_rq_tgl(a_rq_tgl), .a_rq_addr(a_rq_addr), .a_rq_stride(a_rq_stride), .a_rq_n(a_rq_n), .a_rq_two(a_rq_two), .a_rq_pair(a_rq_pair),
        .a_done_tgl(a_done_tgl), .a_we_a(a_we_a), .a_we_b(a_we_b), .a_wa(a_wa), .a_wd(a_wd),
        .vclk(sys_clk), .pix(4'd0), .px(10'd0), .py(9'd0), .v60(1'b0),
        .clk(pix_clk), .ce(1'b1), .hact(12'd1920), .vact(11'd1080), .xact0(12'd768), .yact0(11'd45), .rgb_in(24'd0), .de_in(h >= 768 && vv >= 45),
        .hs_in(1'b0), .vs_in(1'b0), .cx_in(h), .cy_in(vv),
        .rgb(), .de(), .hs(), .vs(), .cx(), .cy());

    reg [1:0] r_mode = 2'd1, r_std = 2'd1;
    wire [7:0] r_o, g_o, b_o; wire syn_n, fb;
    av_rgb avr (.cpu_clk(cpu_clk), .r_mode(r_mode), .r_std(r_std),
        .r_ctrl(av_ctrl), .r_w(av_w), .r_h(av_h), .r_stride(av_stride), .r_front(av_front), .r_back(av_back),
        .cfg_tgl(av_cfg), .front_tgl(av_fr), .back_tgl(av_bk), .pix_y(pix_y),
        .a_rq_tgl(a_rq_tgl), .a_rq_addr(a_rq_addr), .a_rq_stride(a_rq_stride), .a_rq_n(a_rq_n), .a_rq_two(a_rq_two), .a_rq_pair(a_rq_pair),
        .a_done_tgl(a_done_tgl), .ui_clk(ui_clk), .a_we_a(a_we_a), .a_we_b(a_we_b), .a_wa(a_wa), .a_wd(a_wd),
        .clk(sys_clk), .r_o(r_o), .g_o(g_o), .b_o(b_o), .syn_n(syn_n), .fb(fb));

    // ---- the core's raster for std 0: lines per frame set by the phase
    integer core_lines = 312, core_hc = 0;
    always @(posedge sys_clk) begin
        core_hc <= (core_hc == 2015) ? 0 : core_hc + 1;
        if (core_hc == 2015) pix_y <= (pix_y >= core_lines - 1) ? 9'd0 : pix_y + 9'd1;
    end

    // ---- the pictures (id 0: BUF0, 1: BUF1, 2: BUF2), also written into the DDR2 model
    reg [15:0] img0 [0:960*540-1];
    reg [15:0] img1 [0:960*540-1];
    reg [15:0] img2 [0:640*384-1];
    function [15:0] hashpix(input integer id, input integer x, input integer y);
        integer t;
        begin
            t = x * 37 + y * 101 + id * 7919;
            hashpix = {t[4:0] ^ x[4:0], y[5:0] ^ t[9:4], x[4:0] + y[4:0]} ^ ((x == 0) ? 16'hFFFF : 16'h0000);
        end
    endfunction
    integer ix, iy, iw;
    initial begin
        for (iy = 0; iy < 540; iy = iy + 1)
            for (ix = 0; ix < 960; ix = ix + 1) begin
                img0[iy * 960 + ix] = hashpix(0, ix, iy);
                img1[iy * 960 + ix] = hashpix(1, ix, iy);
            end
        for (iy = 0; iy < 384; iy = iy + 1)
            for (ix = 0; ix < 640; ix = ix + 1) img2[iy * 640 + ix] = hashpix(2, ix, iy);
        img0[5] = 16'hFFFF; img0[6] = 16'h0000; img0[960 + 5] = 16'hFFFF; img0[960 + 6] = 16'h0000;       // white and black, both lines of a pair
        // buffers: BUF0 and BUF1 960 x 540 stride 1920; BUF2 holds the 376 x 283 and the 640 x 384 pictures (stride 1280, 640 x 384 as the base)
        for (iy = 0; iy < 540; iy = iy + 1)
            for (ix = 0; ix < 960; ix = ix + 8) begin
                mig.mem[(BUF0 + iy * 1920 + ix * 2) >> 4] = {img0[iy*960+ix+7], img0[iy*960+ix+6], img0[iy*960+ix+5], img0[iy*960+ix+4],
                                                            img0[iy*960+ix+3], img0[iy*960+ix+2], img0[iy*960+ix+1], img0[iy*960+ix]};
                mig.mem[(BUF1 + iy * 1920 + ix * 2) >> 4] = {img1[iy*960+ix+7], img1[iy*960+ix+6], img1[iy*960+ix+5], img1[iy*960+ix+4],
                                                            img1[iy*960+ix+3], img1[iy*960+ix+2], img1[iy*960+ix+1], img1[iy*960+ix]};
            end
        for (iy = 0; iy < 384; iy = iy + 1)
            for (ix = 0; ix < 640; ix = ix + 8)
                mig.mem[(BUF2 + iy * 1280 + ix * 2) >> 4] = {img2[iy*640+ix+7], img2[iy*640+ix+6], img2[iy*640+ix+5], img2[iy*640+ix+4],
                                                            img2[iy*640+ix+3], img2[iy*640+ix+2], img2[iy*640+ix+1], img2[iy*640+ix]};
    end
    function [15:0] srcpix(input integer bsel, input integer x, input integer y);
        begin srcpix = (bsel == 0) ? img0[y * 960 + x] : (bsel == 1) ? img1[y * 960 + x] : img2[y * 640 + x]; end
    endfunction

    // ---- register writes as the app does them (byte 3 commits)
    task wr32(input [11:0] a, input [31:0] v); begin
        @(posedge cpu_clk); #1; p_we = 1; p_addr = a;     p_wdata = v[7:0];
        @(posedge cpu_clk); #1;            p_addr = a + 1; p_wdata = v[15:8];
        @(posedge cpu_clk); #1;            p_addr = a + 2; p_wdata = v[23:16];
        @(posedge cpu_clk); #1;            p_addr = a + 3; p_wdata = v[31:24];
        @(posedge cpu_clk); #1; p_we = 0;
    end endtask
    reg [31:0] st;
    task rd_status; begin
        @(posedge cpu_clk); #1; p_re = 1; p_addr = 12'h0A4;
        @(posedge cpu_clk); #1; p_re = 0;
        @(posedge cpu_clk); #1; st[7:0] = p_rdata; p_addr = 12'h0A5;
        @(posedge cpu_clk); #1; st[15:8] = p_rdata;
    end endtask

    // ---- what is expected (phase parameters)
    integer ph_buf = 0, ph_w = 960, ph_h = 540, ph_stride = 1920, ph_60 = 0, ph_on = 1, ph_hdmi = 1;
    integer chk = 0, nframes = 0, nchk_frames = 0, nsamp = 0, errs = 0, shown_err = 0;
    integer e_act, e_va0, e_lt, e_ht, e_hs, e_two, e_hsn, e_crop, e_over, e_rows, e_top, e_src0;
    task derive; begin
        e_act = ph_60 ? 240 : 288; e_va0 = ph_60 ? 18 : 22; e_lt = ph_60 ? 263 : 312; e_ht = ph_60 ? 2080 : 2016; e_hs = ph_60 ? 152 : 148;
        e_two = ph_h * 4 > e_act * 5;                                   // taller than 1.25 x the window
        e_hsn = e_two ? ph_h / 2 : ph_h;
        e_crop = e_hsn > e_act; e_over = e_hsn - e_act;
        e_rows = e_crop ? e_act : e_hsn;
        e_top  = e_crop ? 0 : (e_act - e_hsn) / 2;
        e_src0 = e_crop ? (e_two ? (e_over / 2) * 2 : e_over / 2) : 0;
    end endtask
    function [7:0] levl(input integer v); begin levl = (v * 11) / 16; end endfunction     // the average x 11/16
    function [7:0] w5(input [4:0] c); begin w5 = {c, c[4:2]}; end endfunction
    function [7:0] w6(input [5:0] c); begin w6 = {c, c[5:4]}; end endfunction
    function [23:0] want(input integer t, input integer r);               // window clock t of shown row r
        integer xs, y0, y1; reg [15:0] a, b; integer ar, ag, ab, br, bg, bb;
        begin
            xs = (t * ph_w) / 1600;
            y0 = e_src0 + (e_two ? 2 * r : r); y1 = e_two ? y0 + 1 : y0;
            a = srcpix(ph_buf, xs, y0); b = srcpix(ph_buf, xs, y1);
            ar = w5(a[15:11]); ag = w6(a[10:5]); ab = w5(a[4:0]); br = w5(b[15:11]); bg = w6(b[10:5]); bb = w5(b[4:0]);
            want = {levl((ar + br) / 2), levl((ag + bg) / 2), levl((ab + bb) / 2)};
        end
    endfunction

    // ---- the observer: lines and frames from the sync alone, then the model against every sample
    reg syn_d = 1; integer c = 0, lineno = -1, lowlen = 0, prev_long = 0, linelen = 0, nlines_frame = 0, first_frame_seen = 0;
    integer cnt_hs_ok = 0, cnt_long = 0, bad_line = 0, bad_hs = 0, bad_frame = 0, last_line_c = 0;
    reg [23:0] wv; integer r, t;
    always @(negedge sys_clk) begin
        // falling edge of SYNCn = a line's start
        if (!syn_n && syn_d) begin
            if (lineno >= 0 && chk && c != e_ht - 1) begin bad_line = bad_line + 1; errs = errs + 1; if (shown_err < 8) $display("FAIL line %0d length %0d want %0d", lineno, c + 1, e_ht); shown_err = shown_err + 1; end
            c = 0; lineno = lineno + 1;
            lowlen = 0;
        end else c = c + 1;
        if (!syn_n) lowlen = lowlen + 1;
        if (syn_n && !syn_d) begin                  // rising edge: the low pulse's width
            if (lowlen > 1000) begin
                if (!prev_long) begin
                    // first broad line of a frame: the line count of the frame before
                    if (first_frame_seen && chk && lineno != e_lt) begin bad_frame = bad_frame + 1; $display("FAIL frame has %0d lines want %0d", lineno, e_lt); errs = errs + 1; end
                    if (first_frame_seen) begin nframes = nframes + 1; if (chk) nchk_frames = nchk_frames + 1; end
                    first_frame_seen = 1; lineno = 0;
                end
                cnt_long = cnt_long + 1; prev_long = 1;
            end else begin
                if (chk && lineno >= 3 && lowlen != e_hs) begin bad_hs = bad_hs + 1; errs = errs + 1; if (shown_err < 8) $display("FAIL hsync %0d wide want %0d (line %0d)", lowlen, e_hs, lineno); shown_err = shown_err + 1; end
                if (chk && lineno >= 3 && lowlen == e_hs) cnt_hs_ok = cnt_hs_ok + 1;
                prev_long = 0;
            end
        end
        syn_d = syn_n;
        // the samples of a checked frame
        if (chk && ((first_frame_seen && lineno >= 0) || !ph_on)) begin
            wv = {r_o, g_o, b_o};
            nsamp = nsamp + 1;
            if (fb !== (ph_on ? 1'b1 : 1'b0)) begin errs = errs + 1; if (shown_err < 8) $display("FAIL FB %b", fb); shown_err = shown_err + 1; end
            if (!ph_on) begin
                if (wv !== 24'd0 || syn_n !== 1'b1) begin errs = errs + 1; if (shown_err < 8) $display("FAIL off: %h syn %b", wv, syn_n); shown_err = shown_err + 1; end
            end else if (lineno >= e_va0 + e_top && lineno < e_va0 + e_top + e_rows && c >= 364 && c < 364 + 1600) begin
                t = c - 364; r = lineno - e_va0 - e_top;
                if (wv !== want(t, r)) begin errs = errs + 1; if (shown_err < 8) $display("FAIL line %0d row %0d clock %0d: %h want %h (src x %0d)", lineno, r, t, wv, want(t, r), (t * ph_w) / 1600); shown_err = shown_err + 1; end
            end else if (wv !== 24'd0) begin errs = errs + 1; if (shown_err < 8) $display("FAIL line %0d clock %0d: %h outside the window", lineno, c, wv); shown_err = shown_err + 1; end
        end
    end

    // ---- the phases
    integer last_nf = -1;
    always @(posedge sys_clk) if (nframes != last_nf) begin last_nf = nframes; $display("[%0t ps] frame %0d", $time, nframes); $fflush; end
    integer fr0, under0, i, only = 0;
    function go(input integer n); begin go = (only == 0) || (only == n); end endfunction
    task phase(input integer nm, input integer bsel, input integer w, input integer hh, input integer stride, input integer s60, input integer coreln, input integer on);
    begin
        chk = 0; ph_buf = bsel; ph_w = w; ph_h = hh; ph_stride = stride; ph_60 = s60; ph_on = on; derive;
        core_lines = coreln;
        wr32(12'h094, stride); wr32(12'h090, {hh[15:0], w[15:0]});
        wr32(12'h084, (bsel == 0) ? BUF0 : (bsel == 1) ? BUF1 : BUF2);
        wr32(12'h080, 32'h03);
        r_mode = on ? 2'd1 : 2'd0;
        if (on) begin
            fr0 = nframes; while (nframes < fr0 + 2) @(posedge sys_clk);        // the new frame is taken at the next frame start, the std follows
            chk = 1; fr0 = nframes; nsamp = 0; shown_err = 0; i = errs;
            while (nframes < fr0 + 1) @(posedge sys_clk);
            chk = 0;
        end else begin                                                         // off: no sync, so no frames to count: two frame times, then one checked
            #(64'd40000000000);
            chk = 1; nsamp = 0; shown_err = 0; i = errs;
            #(64'd20000000000);
            chk = 0;
        end
        $display("phase %0d: %0s %0dx%0d stride %0d, %0d frame checked, %0d samples, window rows %0d top %0d (S=%0d, src0 %0d), %0d errors", nm,
                 s60 ? "60 Hz" : "50 Hz", w, hh, stride, 1, nsamp, e_rows, e_top, e_two ? 2 : 1, e_src0, errs - i);
    end endtask

    initial begin
        wait (calib);
        repeat (50) @(posedge cpu_clk);
        wr32(12'h098, 0); wr32(12'h0A8, {16'd1080, 16'd1920});               // HDMI: origin, 960x540 at scale 2 (other sizes: nearest, any size <= 2x)
        wr32(12'h09C, 255);
        r_std = 2'd1;
        if ($value$plusargs("only=%d", only)) hdmi_on = 0;                  // +only=N: one phase, without the HDMI side (a quick rerun)
        if (go(1)) phase(1, 0, 960, 540, 1920, 0, 312, 1);                                // the menu, 50 Hz: two lines averaged, 270 rows, bars
        // the flip: the other buffer by PX_BACK
        if (go(1)) begin
        chk = 0; ph_buf = 1; wr32(12'h088, BUF1);
        fr0 = nframes; while (nframes < fr0 + 2) @(posedge sys_clk);
        chk = 1; fr0 = nframes; nsamp = 0; shown_err = 0; i = errs;
        while (nframes < fr0 + 1) @(posedge sys_clk);
        chk = 0; $display("phase 1b: flip to the second buffer, %0d samples, %0d errors", nsamp, errs - i);
        end
        rd_status; $display("HDMI side while both read the DDR2: lines not shown for underrun %0d", st[15:8]); if (st[15:8] != 0) errs = errs + 1;
        hdmi_on = 0;                                                           // the rest without the HDMI pixel side (simulation time)
        r_std = 2'd2;
        if (go(2)) phase(2, 0, 960, 540, 1920, 1, 312, 1);                                // 60 Hz: 270 rows cropped to 240
        r_std = 2'd0;
        if (go(3)) phase(3, 2, 640, 384, 1280, 1, 263, 1);                                // std 0, the core's raster 263 lines: 60 Hz; 384 lines two into one (192 rows)
        if (go(4)) phase(4, 0, 960, 283, 1920, 0, 312, 1);                                // std 0, the core's raster 312 lines: 50 Hz; 283 lines 1:1, centred
        r_std = 2'd1;
        if (go(5)) phase(5, 2, 640, 384, 1280, 0, 312, 1);                                // 50 Hz, 384 lines: two into one, 192 rows
        if (go(6)) phase(6, 0, 960, 540, 1920, 0, 312, 0);                                // off
        if (only == 0 && (nchk_frames < 6 || cnt_hs_ok < 1000 || cnt_long < 10)) begin errs = errs + 1; $display("FAIL too little observed: %0d frames, %0d hsyncs, %0d broad", nchk_frames, cnt_hs_ok, cnt_long); end
        if (errs == 0 && only != 0) $display("PASS avrgb (phase %0d only)", only);
        else if (errs == 0) $display("PASS avrgb: %0d frames, %0d hsyncs, %0d broad pulses, every sample of 7 phases (50/60 Hz, 2:1 average, 1:1, crop, flip, follow, off)", nchk_frames, cnt_hs_ok, cnt_long);
        else $display("FAIL avrgb: %0d errors", errs);
        $finish;
    end
    initial begin #(64'd3000000000000); $display("FAIL avrgb: timeout"); $finish; end
endmodule
