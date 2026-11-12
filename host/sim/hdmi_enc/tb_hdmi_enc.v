// T164 WP4 / scenario S4: the host's HDMI encoder fork (host/hdmi_enc) in one mode of hdmi_modes.vh, at the gear of the row.
// A pixel generator with the counters of hdmi_timing (cx = 0 is the first pixel after the active area) drives the encoder with `ce` once per
// gear clocks; the bench decodes what the three TMDS channels carry (control, guard bands, TERC4 data islands, video) and checks, for the
// second frame (the first one only opens the data islands):
//   - the TMDS symbols change only on the clock after a ce (the bit repeater of T164 G4 relies on it)
//   - the video preamble, guard bands and the pixel values (TMDS decoded) sit where cx / cy say; sync bits follow hsync / vsync
//   - every data island: preamble at cx 4 .. 11, guards at 12 / 13 and 14 + 32 n, n packets, BCH parity of every packet right,
//     the trailing guard + 4 control clocks + the video preamble (10) end inside the blanking of the row
//   - AVI InfoFrame: checksum, PR = 0, VIC and picture aspect of the row
//   - audio clock regeneration: N = 6144, CTS x 128 x 48000 / N = the PIXEL clock of the row within 100 ppm (every packet, and the mean)
//   - audio sample packets: the sample words arrive without gap or repeat; the number of ACR / sample packets fits the frame
// Parameters: MODE (the row), MAXP (MAX_PACKETS of the encoder under test; the control with 4 overflows the blanking of 576p).
`timescale 1ns/1ps
module tb;
    parameter MODE = 0;
    parameter MAXP = 3;
    localparam CHK = 2;                  // the checked frame: the third (frame 0 has no islands, the pending ACR of frame 1 is sent late)
    parameter [3:0] EXPECT_PR = 4'd0;
`include "hdmi_modes.vh"

    localparam [104:0] ROW = hm_row(MODE);
    localparam [6:0]  R_VIC  = ROW[104:98];
    localparam [3:0]  GEAR   = ROW[97:94];
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

    reg clk = 1'b0;
    always #1 clk = ~clk;

    // ---- gear phase and pixel generator
    reg [2:0] ph = 3'd0;
    wire ce = (ph == GEAR[2:0] - 3'd1);
    always @(posedge clk) ph <= ce ? 3'd0 : ph + 3'd1;
    reg [11:0] cx = 12'd0;
    reg [10:0] cy = 11'd0;
    always @(posedge clk) if (ce) begin
        if (cx == HTOT - 1) begin cx <= 12'd0; cy <= (cy == VTOT - 1) ? 11'd0 : cy + 11'd1; end
        else cx <= cx + 12'd1;
    end
    function hs_at(input integer x);
        hs_at = (x >= HFP && x < HFP + HSYN) ? HPOL : ~HPOL;
    endfunction
    function vs_at(input integer y);
        vs_at = (y >= VFP && y < VFP + VSYN) ? VPOL : ~VPOL;
    endfunction
    function [23:0] rgb_at(input integer x, input integer y);
        rgb_at = {x[7:0], y[7:0], x[7:0] + y[7:0]};         // R, G, B
    endfunction
    wire de = (cx >= B) && (cy >= BV);
    wire [23:0] rgb = de ? rgb_at(cx, cy) : 24'h0;
    wire hsync = hs_at(cx);
    wire vsync = vs_at(cy);

    reg rst = 1'b1;
    integer rst_n = 0;
    always @(posedge clk) if (ce) begin
        if (rst_n == 3) rst <= 1'b0; else rst_n <= rst_n + 1;
    end

    // ---- audio source: a counter, taken at every sample strobe
    reg [23:0] w0 = 24'd0;
    wire [23:0] w1 = ~w0;
    wire sample_en;
    always @(posedge clk) if (sample_en) w0 <= w0 + 24'd1;

    wire [9:0] tmds_0, tmds_1, tmds_2;
    hdmi #(
        .DVI_OUTPUT(1'b0), .IT_CONTENT(1'b1), .VIDEO_ID_CODE(0), .VIDEO_RATE(165_000_000), .RUNTIME(1), .MAX_PACKETS(MAXP),
        .AUDIO_RATE(48_000), .AUDIO_BIT_WIDTH(24), .BIT_WIDTH(12), .BIT_HEIGHT(11),
        .VENDOR_NAME(64'h5452583634000000), .PRODUCT_DESCRIPTION(128'h43363420556C74696D61746500000000), .SOURCE_DEVICE_INFORMATION(8'h08)
    ) dut (
        .clk_pixel_x5(1'b0), .clk_pixel(clk), .ce(ce), .reset(rst),
        .rgb(rgb), .hsync(hsync), .vsync(vsync), .cx(cx), .cy(cy), .de(de),
        .vic_code(R_VIC), .video_rate(RATE), .aspect(ASPECT),
        .sx_e(B[11:0]), .fw_m1(HTOT[11:0] - 12'd1), .sy_e(BV[10:0]), .ey_e(VTOT[10:0]),
        .audio_sample_word_0(w0), .audio_sample_word_1(w1), .audio_sample_en(sample_en),
        .tmds_0(tmds_0), .tmds_1(tmds_1), .tmds_2(tmds_2)
    );

    // ---- TMDS symbols
    localparam [9:0] C00 = 10'b1101010100, C01 = 10'b0010101011, C10 = 10'b0101010100, C11 = 10'b1010101011;
    localparam [9:0] VG0 = 10'b1011001100, VG1 = 10'b0100110011, DG12 = 10'b0100110011;
    function [9:0] ctl_sym(input [1:0] c);
        case (c) 2'b00: ctl_sym = C00; 2'b01: ctl_sym = C01; 2'b10: ctl_sym = C10; default: ctl_sym = C11; endcase
    endfunction
    function [9:0] dg0_sym(input [1:0] c);
        case (c) 2'b00: dg0_sym = 10'b1010001110; 2'b01: dg0_sym = 10'b1001110001; 2'b10: dg0_sym = 10'b0101100011; default: dg0_sym = 10'b1011000011; endcase
    endfunction
    function [4:0] terc4_dec(input [9:0] s);                 // {valid, nibble}
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

    // ---- the monitor
    integer errs = 0;
    // the first 3 occurrences of every message are printed, the rest only counted
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

    integer o = 0;                       // output pixel index (one per ce)
    integer lat = -1;                    // pipeline latency in pixels, measured at the first video guard
    reg [29:0] last_sym = 30'd0;
    reg ce_q = 1'b0;
    always @(posedge clk) begin
        ce_q <= ce;
        last_sym <= {tmds_2, tmds_1, tmds_0};
        if (o > 12 && !ce_q && {tmds_2, tmds_1, tmds_0} != last_sym) fail("symbol changed between two pixels", o, 0);
    end

    // island capture
    integer isl_n = 0;                   // terc4 pixels collected
    integer isl_x0 = 0;
    integer st = 0;
    reg [3:0] n0 [0:511];
    reg [3:0] n1 [0:511];
    reg [3:0] n2 [0:511];
    integer gcount = 0;                  // leading guard pixels seen

    // counters of the checked frame (frame 1)
    integer n_acr = 0, n_asp = 0, n_avi = 0, n_spd = 0, n_ainfo = 0, n_null = 0, n_isl = 0, n_vid = 0;
    integer acr_cts_sum = 0, acr_cts_min = 1 << 30, acr_cts_max = 0;
    integer max_trail_end = 0;
    integer exp_word = -1;               // next expected left sample
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
            if (x_trail + 2 > max_trail_end) max_trail_end = x_trail + 2;
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
                // BCH parity: header 24 bits, subpackets 56 bits
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
                8'h01: begin                                  // audio clock regeneration
                    n_acr = n_acr + 1;
                    nn  = {sb[0][35:32], sb[0][47:40], sb[0][55:48]};
                    cts = {sb[0][11:8], sb[0][23:16], sb[0][31:24]};
                    if (sb[0][7:0] != 8'd0 || sb[0][15:12] != 4'd0 || sb[0][39:36] != 4'd0) fail("ACR reserved bits", sb[0][15:0], 0);
                    for (s = 1; s < 4; s = s + 1) if (sb[s][55:0] != sb[0][55:0]) fail("ACR subpackets differ", s, 0);
                    if (nn != 6144) fail("ACR N", nn, 6144);
                    // CTS x 128 x 48000 / N against the pixel clock of the row: 100 ppm
                    fcts = cts;
                    fcts = fcts * 128 * 48000 / nn;
                    if (cts == 0) fail("CTS is zero", 0, 0);
                    else if (fcts > RATE + RATE / 10000 || fcts + RATE / 10000 < RATE) fail("CTS is not the pixel clock", fcts, RATE);
                    acr_cts_sum = acr_cts_sum + cts;
                    acr_cts_min = (cts < acr_cts_min) ? cts : acr_cts_min;
                    acr_cts_max = (cts > acr_cts_max) ? cts : acr_cts_max;
                end
                8'h02: begin                                  // audio samples
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
                8'h82: begin                                  // AVI InfoFrame
                    n_avi = n_avi + 1;
                    for (s = 0; s < 4; s = s + 1) for (j = 0; j < 7; j = j + 1) pb[s*7 + j] = sb[s][8*j +: 8];
                    sum = h[23:16] + h[15:8] + h[7:0];
                    for (j = 0; j < 28; j = j + 1) sum = sum + pb[j];
                    if ((sum & 255) != 0) fail("AVI checksum", sum & 255, 0);
                    if (h[23:16] != 8'd13 || h[15:8] != 8'd2) fail("AVI length / version", h[23:8], 0);
                    if (pb[5][3:0] != EXPECT_PR) fail("AVI PR", pb[5][3:0], EXPECT_PR);
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

    integer x, y, fr, k_in, ex;
    reg [9:0] s0, s1, s2;
    reg [4:0] d0, d1, d2;
    reg [23:0] want;
    always @(posedge clk) if (ce) begin
        s0 = tmds_0; s1 = tmds_1; s2 = tmds_2;
        k_in = o - lat;
        // latency from the first video guard band: its first pixel is cx = B - 2 of the first active line
        if (lat < 0 && o > 16 && s0 == VG0 && s1 == VG1 && s2 == VG0) begin
            lat = o - (BV * HTOT + B - 2);
            if (lat < 1 || lat > 12) begin fail("pipeline latency", lat, 0); lat = 4; end
            $display("  mode %0d: encoder latency %0d pixels (gear %0d)", MODE, lat, GEAR);
        end
        if (lat >= 0 && k_in >= 0) begin
            x = k_in % HTOT; y = (k_in / HTOT) % VTOT; fr = k_in / PIX;
            if (fr == CHK) begin
                // ---- data island: preamble, guards, terc4 (st: 0 outside, 1 inside, 2 second trailing guard pixel due)
                if (st == 1) begin
                    if (s1 == DG12 && s2 == DG12) begin       // first trailing guard pixel
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
                end else if (s1 == DG12 && s2 == DG12 && x < B - 10) begin     // (a video pixel can carry the same two symbols)
                    if (gcount == 0) begin
                        if (s0 != dg0_sym({vs_at(y), hs_at(x)})) fail("leading guard ch0", x, 0);
                        isl_x0 = x; gcount = 1;
                    end else begin st = 1; isl_n = 0; gcount = 0; end
                end else begin
                    gcount = 0;
                    // outside an island: the control periods and the video period
                    if (y >= BV && x >= B - 10 && x < B - 2) begin
                        if (s1 != C01 || s2 != C00 || s0 != ctl_sym({vs_at(y), hs_at(x)})) fail("video preamble", x, y);
                    end else if (y >= BV && x >= B - 2 && x < B) begin
                        if (s0 != VG0 || s1 != VG1 || s2 != VG0) fail("video guard band", x, y);
                    end else if (y >= BV && x >= B) begin
                        n_vid = n_vid + 1;
                        want = rgb_at(x, y);
                        if (video_dec(s0) != want[7:0] || video_dec(s1) != want[15:8] || video_dec(s2) != want[23:16]) fail("video pixel", x, y);
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

    reg [63:0] exp_samples_w;
    integer exp_samples, exp_acr;
    initial begin
        // run to the end of frame 1 (plus the latency and the following line)
        #1;
        wait (o >= (CHK + 1) * PIX + 40 && lat >= 0);
        wait (o - lat >= (CHK + 1) * PIX + 8);
        // ---- frame totals (frame 1)
        exp_samples_w = PIX; exp_samples_w = exp_samples_w * 48000 / RATE; exp_samples = exp_samples_w;                  // samples in one frame
        exp_acr = exp_samples / 48;
        if (n_avi != 1) fail("AVI InfoFrames in the frame", n_avi, 1);
        if (n_spd != 1) fail("SPD InfoFrames in the frame", n_spd, 1);
        if (n_ainfo != 1) fail("audio InfoFrames in the frame", n_ainfo, 1);
        if (n_acr < exp_acr - 1 || n_acr > exp_acr + 1) fail("ACR packets in the frame", n_acr, exp_acr);
        if (n_asp * 4 < exp_samples - 8 || n_asp * 4 > exp_samples + 8) fail("audio sample packets in the frame", n_asp * 4, exp_samples);
        if (n_acr > 0) begin
            // mean CTS against the pixel clock: 100 ppm
            if ((acr_cts_sum / n_acr) * 1000 + (RATE / 10000) < RATE || (acr_cts_sum / n_acr) * 1000 > RATE + RATE / 10000)
                fail("mean CTS is not the pixel clock", (acr_cts_sum / n_acr) * 1000, RATE);
        end
        if (n_isl != VTOT) fail("data islands in the frame (one per line)", n_isl, VTOT);
        if (n_vid != HACT * VACT) fail("video pixels in the frame", n_vid, HACT * VACT);
        $display("  mode %0d: vic %0d aspect %0d gear %0d rate %0d  B %0d  island ends cx %0d (video preamble from %0d)  packets/frame acr %0d audio %0d avi %0d spd %0d ainfo %0d  CTS %0d..%0d",
                 MODE, R_VIC, ASPECT, GEAR, RATE, B, max_trail_end, B - 10, n_acr, n_asp, n_avi, n_spd, n_ainfo, acr_cts_min, acr_cts_max);
        if (errs == 0) $display("hdmi_enc mode %0d PASS", MODE);
        else $display("hdmi_enc mode %0d FAIL (%0d errors)", MODE, errs);
        $finish;
    end
    initial begin
        #(PIX * GEAR * 2 * 5 + 1000);
        $display("hdmi_enc mode %0d FAIL timeout", MODE);
        $finish;
    end
endmodule
