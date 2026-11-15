// T164 WP8 smoke bench (T222: this core's hdmi_out.v, the VIC path in every row; in 576p the pixel layer's rectangle is checked, the VIC picture around it is not): hdmi_out.v end to end in one HDMI mode (MODE: 2 = 1080p50 with the VIC path, 0 = 576p50 at gear 5 with the pixel
// layer). What leaves the module is the four GTP lane words (hdmi_gtp is a model, models.v): the bench serialises them (20 bits per usrclk, bit 0
// first, lane 3 is the TMDS clock), finds the pixels the way a sink does (the rising edge of the clock lane starts a pixel of 10 * gear line
// bits, a symbol bit is the middle one of its `gear` repetitions, lane 2 carries channel 0 inverted: the board's swapped pair) and decodes the
// symbols: control periods, video preamble / guard bands / pixels, TERC4 data islands. For the first complete frame after the anchor it checks:
//   - the frame geometry (the vsync edge anchors the position; preamble, guards, island and control periods sit where the mode says)
//   - every video pixel against the picture the sources draw (1080p: the VIC window of the pattern pix = px ^ py through the Colodore palette,
//     1600 x 1080 centred, black bars; 576p: a 640 x 480 RGB565 picture from DDR2 at (40, 48), black around it)
//   - every data island: BCH parity, AVI InfoFrame (checksum, PR = 0, VIC, picture aspect), ACR (N = 6144, CTS against the pixel clock), audio sample
//     packets (the samples the bench hands over arrive in order, none lost or repeated), SPD, audio InfoFrame, one island per line
// Before that, in MODE 0 the request 0x4F00 = 0 goes through the overlay window; the status registers 0x4F04..07 are read back in both modes.
// Time unit 1 fs: the 1080p50 pixel clock and the VIC dot clock are in the exact ratio 6875 / 1456 of the lock (hdmi_modes.vh).
`timescale 1fs/1fs
module tb;
    parameter MODE = 2;
    localparam CHK = 1;
`include "hdmi_modes.vh"
    localparam [104:0] ROW = hm_row(MODE);
    localparam [6:0]  R_VIC  = ROW[104:98];
    localparam integer GEAR  = ROW[97:94];
    localparam integer HACT  = ROW[93:82];
    localparam integer HFP   = ROW[81:70];
    localparam integer HSYN  = ROW[69:58];
    localparam integer HBP   = ROW[57:46];
    localparam        HPOL   = ROW[45];
    localparam integer VACT  = ROW[44:34];
    localparam integer VFP   = ROW[33:23];
    localparam integer VSYN  = ROW[22:12];
    localparam integer VBP   = ROW[11:1];
    localparam        VPOL   = ROW[0];
    localparam integer B     = HFP + HSYN + HBP;
    localparam integer HTOT  = B + HACT;
    localparam integer BV    = VFP + VSYN + VBP;
    localparam integer VTOT  = BV + VACT;
    localparam integer PIX   = HTOT * VTOT;
    localparam [31:0] RATE   = hm_rate(MODE);
    localparam [1:0]  ASPECT = hm_aspect(MODE);
    localparam integer MAXP  = 3;

    // ---- clocks
    localparam integer P_VIC = 31721250;                 // dot4x; the 1080p50 TMDS clock below is 1456 / 6875 of it
    integer ref_half = 3358992;                          // 1080p50 until the synthesizer is written for the mode
    reg refclk = 1'b0, cpu_clk = 1'b0, clk50 = 1'b0, sys_clk = 1'b0, ui_clk = 1'b0;
    always begin #(ref_half) refclk = ~refclk; end
    always #5000000  cpu_clk = ~cpu_clk;
    always #10000000 clk50 = ~clk50;
    always #(P_VIC / 2) sys_clk = ~sys_clk;
    always #6667000  ui_clk = ~ui_clk;

    // ---- the synthesizer: the bytes are checked by host/sim/modepath; here the reference moves when the sequence is done
    reg cdce_rst = 1'b1;
    initial begin #100000000 cdce_rst = 1'b0; end
    wire i2c_done;
    always @(posedge i2c_done) ref_half = (MODE == 0) ? 3694500 : 3358992;
    reg [7:0] settle = 8'd0;
    always @(posedge clk50) if (!i2c_done) settle <= 8'd0; else if (!settle[7]) settle <= settle + 8'd1;
    wire gt_rst = ~settle[7];

    // ---- the application's buses
    reg ovl_we = 1'b0; reg [14:0] ovl_addr = 15'd0; reg [7:0] ovl_wdata = 8'd0; wire [7:0] ovl_rdata;
    reg px_we = 1'b0; reg [11:0] px_addr = 12'd0; reg [7:0] px_wdata = 8'd0;
    task ovl_wr(input [14:0] a, input [7:0] d);
        begin @(negedge cpu_clk); ovl_addr = a; ovl_wdata = d; ovl_we = 1'b1; @(negedge cpu_clk); ovl_we = 1'b0; end
    endtask
    task ovl_rd(input [14:0] a, output [7:0] d);
        begin @(negedge cpu_clk); ovl_addr = a; @(negedge cpu_clk); @(negedge cpu_clk); d = ovl_rdata; end
    endtask
    task px_wr8(input [11:0] a, input [7:0] d);
        begin @(negedge cpu_clk); px_addr = a; px_wdata = d; px_we = 1'b1; @(negedge cpu_clk); px_we = 1'b0; end
    endtask
    task px_wr32(input [11:0] a, input [31:0] d);
        begin px_wr8(a, d[7:0]); px_wr8(a + 12'd1, d[15:8]); px_wr8(a + 12'd2, d[23:16]); px_wr8(a + 12'd3, d[31:24]); end
    endtask

    // ---- DDR2 model on the pixel layer's client port
    wire ui_rst_m, calib;
    wire [26:0] v_addr; wire [2:0] v_cmd; wire v_en, v_rdy, v_wdf_wren, v_valid, v_urgent;
    wire [127:0] v_wdf_data, v_rd_data; wire [15:0] v_wdf_mask;
    mig_ui_model #(.MEM_WORDS(1 << 19)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst_m), .calib_done(calib),
        .app_addr(v_addr), .app_cmd(v_cmd), .app_en(v_en), .app_rdy(v_rdy),
        .app_wdf_data(v_wdf_data), .app_wdf_mask(v_wdf_mask), .app_wdf_wren(v_wdf_wren), .app_wdf_rdy(),
        .app_rd_data(v_rd_data), .app_rd_data_valid(v_valid));

    // ---- the VIC raster model (63 x 312, a dot every 4 dot4x clocks), pix = px ^ py
    reg [1:0] vph = 2'd0; reg [9:0] vpx = 10'd0; reg [8:0] vpy = 9'd0;
    always @(posedge sys_clk) begin
        vph <= vph + 2'd1;
        if (vph == 2'd3) begin
            vpx <= (vpx == 10'd503) ? 10'd0 : vpx + 10'd1;
            if (vpx == 10'd503) vpy <= (vpy == 9'd311) ? 9'd0 : vpy + 9'd1;
        end
    end
    wire [3:0] vpix = vpx[3:0] ^ vpy[3:0];

    // ---- audio: a new pair whenever the encoder took one
    reg [47:0] snd_hold = 48'd0; reg snd_tgl = 1'b0; reg [23:0] snd_n = 24'd0;
    always @(posedge dut.pix_clk) if (dut.hdmi_sample_en) begin
        snd_n <= snd_n + 24'd1;
        snd_hold <= {snd_n + 24'd1, ~(snd_n + 24'd1)};
        snd_tgl <= ~snd_tgl;
    end
    initial begin #50000000 snd_hold = {24'd1, ~24'd1}; snd_tgl = 1'b1; snd_n = 24'd1; end

    wire gt_ready, vl_s; wire [31:0] meas; wire [9:0] x_text;
    wire [7:0] pxl_rdata, tx_rdata; wire pxe_v, pxe_f, ovl_own;
    wire [2:0] dp, dn; wire cp, cn; wire scl, sda;
    hdmi_out dut (
        .clk50(clk50), .cpu_clk(cpu_clk), .sys_clk(sys_clk), .cdce_rst(cdce_rst), .gt_rst(gt_rst), .fam_v60(1'b0), .vmode(2'd0),
        .refclk_p(refclk), .refclk_n(~refclk), .data_p(dp), .data_n(dn), .clk_p(cp), .clk_n(cn),
        .scl_low(scl), .sda_low(sda), .i2c_done(i2c_done),
        .ovl_we(ovl_we), .ovl_addr(ovl_addr), .ovl_wdata(ovl_wdata), .ovl_rdata(ovl_rdata), .ovl_own(ovl_own),
        .px_we(px_we), .px_re(1'b0), .px_addr(px_addr), .px_wdata(px_wdata), .pxl_rdata(pxl_rdata), .px_ev_vsync(pxe_v), .px_ev_flip(pxe_f),
        .ui_clk(ui_clk), .ui_rst(ui_rst_m | ~calib), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy),
        .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(v_rd_data), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .tx_rdata(tx_rdata), .c64_halted(1'b0), .tx_view(16'd0), .tx_req(), .tx_paddr(), .tx_rom(), .tx_go(1'b0), .ram_rdata(8'd0),
        .tx_cram_addr(), .cram_grant(1'b0), .cram_rdata(4'd0),
        .pix_color(vpix), .pix_x(vpx), .pix_y(vpy), .v60(1'b0), .scanlines(1'b0),
        .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0), .x_text(x_text),
        .snd_hold(snd_hold), .snd_tgl(snd_tgl),
        .gt_ready(gt_ready), .video_locked_s(vl_s), .hdmi_meas(meas)
    );

    // ---- the 576p picture in DDR2: 640 x 480 RGB565 at BUF0, 1920 bytes a line
    localparam [31:0] BUF0 = 32'h080000;
    function [15:0] fbpix(input integer sx, input integer sy);
        reg [15:0] t;
        begin
            t = {sx[4:0], sy[5:0], sx[9:5]} ^ (sx * 37 + sy * 11);
            fbpix = t;
        end
    endfunction
    function [23:0] w565(input [15:0] v);
        w565 = {v[15:11], v[15:13], v[10:5], v[10:9], v[4:0], v[4:2]};
    endfunction
    integer fx, fy;
    initial if (MODE == 0) begin
        for (fy = 0; fy < 481; fy = fy + 1)
            for (fx = 0; fx < 648; fx = fx + 8)
                mig.mem[(BUF0 + fy * 1920 + fx * 2) >> 4] =
                    {fbpix(fx + 7, fy), fbpix(fx + 6, fy), fbpix(fx + 5, fy), fbpix(fx + 4, fy),
                     fbpix(fx + 3, fy), fbpix(fx + 2, fy), fbpix(fx + 1, fy), fbpix(fx, fy)};
    end

    // ---- what the picture must be at active pixel (ax, ay)
    function [23:0] exp_rgb(input integer ax, input integer ay);
        integer sx, sl, idx;
        begin
            exp_rgb = 24'h000000;
            if (MODE == 2) begin
                if (ax >= 160 && ax < 1760) begin
                    sx = 96 + (ax - 160) / 4;
                    sl = 16 + ay / 4;
                    idx = (sx & 15) ^ (sl & 15);
                    exp_rgb = {dut.scaler.pal.pal_r[idx], dut.scaler.pal.pal_g[idx], dut.scaler.pal.pal_b[idx]};
                end
            end else begin
                if (ax >= 40 && ax < 680 && ay >= 48 && ay < 528) exp_rgb = w565(fbpix(ax - 40, ay - 48));
            end
        end
    endfunction

    // ---- TMDS symbols (as tb_hdmi_enc.v)
    localparam [9:0] C00 = 10'b1101010100, C01 = 10'b0010101011, C10 = 10'b0101010100, C11 = 10'b1010101011;
    localparam [9:0] VG0 = 10'b1011001100, VG1 = 10'b0100110011, DG12 = 10'b0100110011;
    function [9:0] ctl_sym(input [1:0] c);
        case (c) 2'b00: ctl_sym = C00; 2'b01: ctl_sym = C01; 2'b10: ctl_sym = C10; default: ctl_sym = C11; endcase
    endfunction
    function [2:0] ctl_dec(input [9:0] s);                  // {valid, c}
        case (s) C00: ctl_dec = 3'b100; C01: ctl_dec = 3'b101; C10: ctl_dec = 3'b110; C11: ctl_dec = 3'b111; default: ctl_dec = 3'b000; endcase
    endfunction
    function [9:0] dg0_sym(input [1:0] c);
        case (c) 2'b00: dg0_sym = 10'b1010001110; 2'b01: dg0_sym = 10'b1001110001; 2'b10: dg0_sym = 10'b0101100011; default: dg0_sym = 10'b1011000011; endcase
    endfunction
    function [4:0] terc4_dec(input [9:0] s);
        case (s)
        10'b1010011100: terc4_dec = 5'b10000;  10'b1001100011: terc4_dec = 5'b10001;
        10'b1011100100: terc4_dec = 5'b10010;  10'b1011100010: terc4_dec = 5'b10011;
        10'b0101110001: terc4_dec = 5'b10100;  10'b0100011110: terc4_dec = 5'b10101;
        10'b0110001110: terc4_dec = 5'b10110;  10'b0100111100: terc4_dec = 5'b10111;
        10'b1011001100: terc4_dec = 5'b11000;  10'b0100111001: terc4_dec = 5'b11001;
        10'b0110011100: terc4_dec = 5'b11010;  10'b1011000110: terc4_dec = 5'b11011;
        10'b1010001110: terc4_dec = 5'b11100;  10'b1001110001: terc4_dec = 5'b11101;
        10'b0101100011: terc4_dec = 5'b11110;  10'b1011000011: terc4_dec = 5'b11111;
        default: terc4_dec = 5'b00000;
        endcase
    endfunction
    function [7:0] video_dec(input [9:0] q);
        reg [8:0] qm;
        integer i;
        begin
            qm[7:0] = q[9] ? ~q[7:0] : q[7:0];
            qm[8] = q[8];
            video_dec[0] = qm[0];
            for (i = 1; i < 8; i = i + 1)
                video_dec[i] = qm[8] ? (qm[i] ^ qm[i-1]) : ~(qm[i] ^ qm[i-1]);
        end
    endfunction
    function [7:0] ecc_step(input [7:0] e, input b);
        ecc_step = (e >> 1) ^ ((e[0] ^ b) ? 8'b10000011 : 8'd0);
    endfunction
    function hs_at(input integer x);
        hs_at = (x >= HFP && x < HFP + HSYN) ? HPOL : ~HPOL;
    endfunction
    function vs_at(input integer y);
        vs_at = (y >= VFP && y < VFP + VSYN) ? VPOL : ~VPOL;
    endfunction

    // ---- failures: the first 3 of every kind are printed
    integer errs = 0;
    reg [511:0] msg_t [0:31];
    integer msg_n [0:31];
    integer msg_cnt = 0;
    task fail(input [511:0] what, input integer a, input integer b2);
        integer i, f;
        begin
            errs = errs + 1;
            f = -1;
            for (i = 0; i < msg_cnt; i = i + 1) if (msg_t[i] == what) f = i;
            if (f < 0 && msg_cnt < 32) begin f = msg_cnt; msg_t[f] = what; msg_n[f] = 0; msg_cnt = msg_cnt + 1; end
            if (f >= 0) begin
                msg_n[f] = msg_n[f] + 1;
                if (msg_n[f] <= 3) $display("  FAIL mode %0d: %0s  (%0d, %0d)", MODE, what, a, b2);
            end
        end
    endtask

    // ---- the data island parser (tb_hdmi_enc.v's)
    reg [3:0] n0 [0:511];
    reg [3:0] n1 [0:511];
    reg [3:0] n2 [0:511];
    integer n_acr = 0, n_asp = 0, n_avi = 0, n_spd = 0, n_ainfo = 0, n_null = 0, n_isl = 0, n_vid = 0;
    integer n_vdbg = 0;
    integer acr_cts_sum = 0, acr_cts_min = 1 << 30, acr_cts_max = 0;
    integer exp_word = -1;
    integer n_samples_seen = 0;
    task process_island(input integer x_lead, input integer npix, input integer x_trail);
        integer p, k, s, np;
        reg [31:0] h;
        reg [63:0] sb [0:3];
        reg [7:0] eh, es;
        reg [7:0] pb [0:27];
        integer j, cts, nn, sum, w;
        reg [63:0] fcts;
        begin
            n_isl = n_isl + 1;
            if (x_lead != 12) fail("leading guard not at cx 12", x_lead, 0);
            if (npix != 32 * MAXP) fail("island length", npix, 32 * MAXP);
            if (x_trail + 2 + 4 + 10 > B) fail("island does not leave 4 control + video preamble in the blanking", x_trail + 2, B);
            np = npix / 32;
            for (p = 0; p < np; p = p + 1) begin
                h = 32'd0;
                for (s = 0; s < 4; s = s + 1) sb[s] = 64'd0;
                for (k = 0; k < 32; k = k + 1) begin
                    h[k] = n0[32*p + k][2];
                    for (s = 0; s < 4; s = s + 1) begin
                        sb[s][2*k]   = n1[32*p + k][s];
                        sb[s][2*k+1] = n2[32*p + k][s];
                    end
                end
                eh = 8'd0;
                for (k = 0; k < 24; k = k + 1) eh = ecc_step(eh, h[k]);
                if (eh !== h[31:24]) fail("header parity", h[7:0], h[31:24] * 256 + eh);
                for (s = 0; s < 4; s = s + 1) begin
                    es = 8'd0;
                    for (k = 0; k < 56; k = k + 1) es = ecc_step(es, sb[s][k]);
                    if (es !== sb[s][63:56]) fail("subpacket parity", h[7:0], s * 65536 + sb[s][63:56] * 256 + es);
                end
                case (h[7:0])
                8'h00: n_null = n_null + 1;
                8'h01: begin
                    n_acr = n_acr + 1;
                    nn  = {sb[0][35:32], sb[0][47:40], sb[0][55:48]};
                    cts = {sb[0][11:8], sb[0][23:16], sb[0][31:24]};
                    if (nn != 6144) fail("ACR N", nn, 6144);
                    fcts = cts;
                    fcts = fcts * 128 * 48000 / nn;
                    if (cts == 0) fail("CTS is zero", 0, 0);
                    else if (fcts > RATE + RATE / 10000 || fcts + RATE / 10000 < RATE) fail("CTS is not the pixel clock", fcts, RATE);
                    acr_cts_sum = acr_cts_sum + cts;
                    acr_cts_min = (cts < acr_cts_min) ? cts : acr_cts_min;
                    acr_cts_max = (cts > acr_cts_max) ? cts : acr_cts_max;
                end
                8'h02: begin
                    n_asp = n_asp + 1;
                    if (h[11:8] != 4'b1111) fail("audio sample packet: present bits", h[11:8], 0);
                    for (s = 0; s < 4; s = s + 1) begin
                        w = sb[s][23:0];
                        if (sb[s][47:24] != ~sb[s][23:0]) fail("audio right word", sb[s][47:24], 0);
                        if (exp_word >= 0 && w != exp_word) fail("audio sample lost or repeated", w, exp_word);
                        exp_word = (w + 1) & 32'hFFFFFF;
                        n_samples_seen = n_samples_seen + 1;
                    end
                end
                8'h82: begin
                    n_avi = n_avi + 1;
                    for (s = 0; s < 4; s = s + 1) for (j = 0; j < 7; j = j + 1) pb[s*7 + j] = sb[s][8*j +: 8];
                    sum = h[23:16] + h[15:8] + h[7:0];
                    for (j = 0; j < 28; j = j + 1) sum = sum + pb[j];
                    if ((sum & 255) != 0) fail("AVI checksum", sum & 255, 0);
                    if (pb[5][3:0] != 4'd0) fail("AVI PR", pb[5][3:0], 0);
                    if (pb[4][6:0] != R_VIC || pb[4][7]) fail("AVI VIC", pb[4], R_VIC);
                    if (pb[2][5:4] != ASPECT) fail("AVI picture aspect", pb[2][5:4], ASPECT);
                    if (pb[1][6:5] != 2'b00) fail("AVI colour format is not RGB", pb[1][6:5], 0);
                end
                8'h83: n_spd = n_spd + 1;
                8'h84: n_ainfo = n_ainfo + 1;
                default: fail("unknown packet type", h[7:0], 0);
                endcase
            end
        end
    endtask

    // ---- the sink: serialise the lane words, find the pixels by the clock lane, take the middle bit of each group of `gear`
    reg        mon_en = 1'b0;
    reg [49:0] sh0 = 0, sh1 = 0, sh2 = 0, shc = 0;
    integer    o = 0, lat = 0;
    reg        lat_set = 1'b0, cvs_known = 1'b0, cvs = 1'b0;
    integer    st = 0, isl_n = 0, isl_x0 = 0, gcount = 0, bi, k_in, x, y, fr, i;
    reg [9:0]  s0, s1, s2;
    reg [4:0]  d0, d1, d2;
    reg [2:0]  cd;
    reg [49:0] w0w, w1w, w2w;
    reg [23:0] want;
    function [9:0] symof(input [49:0] win, input inv);
        integer q;
        begin
            for (q = 0; q < 10; q = q + 1) symof[q] = win[10 * GEAR - 1 - (q * GEAR + GEAR / 2)] ^ inv;
        end
    endfunction
    task on_pixel(input [9:0] t0, input [9:0] t1, input [9:0] t2);
        begin
            s0 = t0; s1 = t1; s2 = t2;
            if (!lat_set) begin
                cd = ctl_dec(s0);
                if (cd[2] && s1 == C00 && s2 == C00) begin
                    if (cvs_known && cvs != VPOL && cd[1] == VPOL) begin
                        lat = o - VFP * HTOT; lat_set = 1'b1;
                    end
                    cvs = cd[1]; cvs_known = 1'b1;
                end
            end
            k_in = o - lat;
            if (lat_set && k_in >= 0) begin
                x = k_in % HTOT; y = (k_in / HTOT) % VTOT; fr = k_in / PIX;
                if (fr == CHK) begin
                    if (st == 1) begin
                        if (s1 == DG12 && s2 == DG12) begin
                            if (s0 != dg0_sym({vs_at(y), hs_at(x)})) fail("trailing guard ch0", x, 0);
                            process_island(isl_x0, isl_n, x);
                            st = 2;
                        end else begin
                            d0 = terc4_dec(s0); d1 = terc4_dec(s1); d2 = terc4_dec(s2);
                            if (!d0[4] || !d1[4] || !d2[4]) begin
                                fail("island: not a terc4 symbol", x, y); st = 0; gcount = 0;
                            end else begin
                                n0[isl_n] = d0[3:0]; n1[isl_n] = d1[3:0]; n2[isl_n] = d2[3:0];
                                if (d0[1:0] != {vs_at(y), hs_at(x)}) fail("island: sync bits", x, y);
                                isl_n = isl_n + 1;
                            end
                        end
                    end else if (st == 2) begin
                        if (!(s1 == DG12 && s2 == DG12)) fail("trailing guard: second pixel", x, y);
                        st = 0; gcount = 0;
                    end else if (s1 == DG12 && s2 == DG12 && x < B - 10) begin
                        if (gcount == 0) begin
                            if (s0 != dg0_sym({vs_at(y), hs_at(x)})) fail("leading guard ch0", x, 0);
                            isl_x0 = x; gcount = 1;
                        end else begin st = 1; isl_n = 0; gcount = 0; end
                    end else begin
                        gcount = 0;
                        if (y >= BV && x >= B - 10 && x < B - 2) begin
                            if (s1 != C01 || s2 != C00 || s0 != ctl_sym({vs_at(y), hs_at(x)})) fail("video preamble", x, y);
                        end else if (y >= BV && x >= B - 2 && x < B) begin
                            if (s0 != VG0 || s1 != VG1 || s2 != VG0) fail("video guard band", x, y);
                        end else if (y >= BV && x >= B) begin
                            n_vid = n_vid + 1;
                            want = exp_rgb(x - B, y - BV);
                            if (!(MODE == 0 && !(x - B >= 40 && x - B < 680 && y - BV >= 48 && y - BV < 528)) &&
                                (video_dec(s0) != want[7:0] || video_dec(s1) != want[15:8] || video_dec(s2) != want[23:16])) begin
                                if (n_vdbg < 4) begin
                                    n_vdbg = n_vdbg + 1;
                                    $display("  video pixel (%0d, %0d): got %02x%02x%02x want %06x", x - B, y - BV, video_dec(s2), video_dec(s1), video_dec(s0), want);
                                end
                                fail("video pixel", x - B, y - BV);
                            end
                        end else if (x >= 4 && x < 12) begin
                            if (s1 != C01 || s2 != C01 || s0 != ctl_sym({vs_at(y), hs_at(x)})) fail("data island preamble", x, y);
                        end else if (x < 4 || x >= 14 + 32 * MAXP + 2) begin
                            if (s1 != C00 || s2 != C00 || s0 != ctl_sym({vs_at(y), hs_at(x)})) fail("control period", x, y);
                        end
                    end
                end
            end
            o = o + 1;
        end
    endtask
    always @(negedge dut.usrclk) if (mon_en) begin
        for (bi = 0; bi < 20; bi = bi + 1) begin
            sh0 = {sh0[48:0], dut.gtp.tx0[bi]};
            sh1 = {sh1[48:0], dut.gtp.tx1[bi]};
            sh2 = {sh2[48:0], dut.gtp.tx2[bi]};
            shc = {shc[48:0], dut.gtp.tx3[bi]};
            if (shc[0] && !shc[1]) begin
                w0w = sh0 >> 1; w1w = sh1 >> 1; w2w = sh2 >> 1;
                on_pixel(symof(w2w, 1'b1), symof(w1w, 1'b0), symof(w0w, 1'b0));    // channel 0 = lane 2 (inverted), 1 = lane 1, 2 = lane 0
            end
        end
    end

    // ---- the run
    reg [7:0] rd;
    integer exp_samples, exp_acr, tmo;
    reg [63:0] exp_samples_w;
    initial begin
        #1;
        wait (dut.running && gt_ready);
        // status register (the module on the bus, not the sequence)
        ovl_rd(15'h4F04, rd); if (rd !== 8'h48) fail("status magic 0x4F04", rd, 8'h48);
        ovl_rd(15'h4F05, rd); if (rd !== 8'd2) fail("status version 0x4F05", rd, 2);
        ovl_rd(15'h4F06, rd); if (rd[2:0] !== 3'b111) fail("status flags 0x4F06 (running, pix_locked, gt_ready)", rd, 7);
        if (MODE == 0) begin
            ovl_wr(15'h4F00, 8'd0);                                  // 576p50
            tmo = 0;
            while (!(dut.running && dut.m_mode == 4'd0 && gt_ready) && tmo < 4000000) begin @(posedge clk50); tmo = tmo + 1; end
            if (tmo >= 4000000) begin $display("hdmi_out mode %0d FAIL the request did not reach the timing", MODE); $finish; end
            repeat (200) @(posedge clk50);
            // the pixel layer's picture (576p), written once the pixel clock runs (the layer's commit is a toggle that only a running pixel clock sees): 640 x 480 at (40, 48), format 1, nearest, global alpha 255
            wait (calib);
            px_wr32(12'h084, BUF0);
            px_wr32(12'h094, 32'd1920);
            px_wr32(12'h090, {16'd480, 16'd640});
            px_wr32(12'h0A8, {16'd480, 16'd640});
            px_wr32(12'h098, {16'd48, 16'd40});
            px_wr32(12'h09C, 32'd255);
            px_wr32(12'h080, 32'h00000003);
        end
        ovl_rd(15'h4F07, rd); if (rd[3:0] !== MODE[3:0]) fail("status mode 0x4F07", rd, MODE);
        if (MODE == 2) begin tmo = 0; while (!dut.mtim.aligned && tmo < 40000000) begin @(posedge dut.pix_clk); tmo = tmo + 1; end
            if (!dut.mtim.aligned) begin $display("hdmi_out mode %0d FAIL the VIC lock never came", MODE); $finish; end end
        mon_en = 1'b1;
        wait (lat_set);
        wait (o - lat >= (CHK + 1) * PIX + 8);
        mon_en = 1'b0;
        if (MODE == 0) $display("  pixel layer: f_en %0d ctrl %02x w %0d h %0d ow %0d oh %0d x0 %0d y0 %0d stride %0d under %0d frames %0d",
                                dut.pxl.f_en, dut.pxl.c_ctrl, dut.pxl.c_w, dut.pxl.c_h, dut.pxl.c_ow, dut.pxl.c_oh, dut.pxl.c_x0, dut.pxl.c_y0,
                                dut.pxl.c_stride, dut.pxl.n_under, dut.pxl.n_frames);
        exp_samples_w = PIX; exp_samples_w = exp_samples_w * 48000 / RATE; exp_samples = exp_samples_w;
        if (n_avi != 1) fail("AVI InfoFrames in the frame", n_avi, 1);
        if (n_spd != 1) fail("SPD InfoFrames in the frame", n_spd, 1);
        if (n_ainfo != 1) fail("audio InfoFrames in the frame", n_ainfo, 1);
        exp_acr = n_asp * 4 / 48;                                // one ACR per 48 samples sent, as many as the sample packets say, +- 1 for the window's edges
        if (n_acr < exp_acr - 1 || n_acr > exp_acr + 1) fail("ACR packets in the frame", n_acr, exp_acr);
        if (n_asp * 4 < exp_samples - 8 || n_asp * 4 > exp_samples + 8) fail("audio sample packets in the frame", n_asp * 4, exp_samples);
        if (n_isl != VTOT) fail("data islands in the frame (one per line)", n_isl, VTOT);
        if (n_vid != HACT * VACT) fail("video pixels in the frame", n_vid, HACT * VACT);
        $display("  mode %0d: vic %0d aspect %0d gear %0d rate %0d  video pixels %0d islands %0d  packets/frame acr %0d audio %0d avi %0d spd %0d ainfo %0d",
                 MODE, R_VIC, ASPECT, GEAR, RATE, n_vid, n_isl, n_acr, n_asp, n_avi, n_spd, n_ainfo);
        if (errs == 0) $display("hdmi_out mode %0d PASS", MODE);
        else $display("hdmi_out mode %0d FAIL (%0d errors)", MODE, errs);
        $finish;
    end
    initial begin
        #(64'd500000000000000);   // 500 ms: the run is about eight frames of 20 ms
        $display("hdmi_out mode %0d FAIL timeout", MODE);
        $finish;
    end
endmodule
