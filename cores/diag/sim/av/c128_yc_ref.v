// T140 D1 (room, 2026-10-30): the C128's chroma/luma encoder - the same numbers as MiSTer's yc_out.sv (vendor/mister, GPL v2+,
// Mike Simone) in less logic. The palette index goes through pal_ram (read by idx), the colour matrix (y = 306 R + 601 G + 117 B,
// u = (B - y) x 504 >>> 10, v = (R - y) x 898 >>> 10, white level 11 y / 16) is multiplications by constants that sit in DSP48s
// (yc_out builds them from shift-add chains in LUTs), then the same carrier arithmetic: sine ROM x U, cosine ROM x V, PAL
// switching, burst. Differences to yc_out: PHASE_INC has WIDTH bits instead of 40 (c128_avout.v), the C register stages are gone
// (3 clocks less latency, sync, blanking and picture shift together), the levels and the composite sum of c128_avout are in here.
// VINV (T140 D24 item 2): 1 = the V sign of every PAL line is the opposite of yc_out.sv's against the same burst swing - the board's finding of
// 2026-10-31 (Sony TV: blue right, light green magenta, i.e. V inverted, U not); 0 = yc_out.sv's own. NTSC is not touched.
// With WIDTH = 40 the DAC words are the old chain's, delayed by 3 clocks (compared in simulation, random palette and pixels).
//
// Timeline: idx / de / csync / hsync arrive aligned at t0 (yc_out's din), tK = K clocks later. U and V are at t4, the luma at t7,
// the carrier chain ends at t7 and the DAC words are registered at t8.
module c128_yc_ref #(parameter integer WIDTH = 26, parameter VINV = 1) (
    input  wire             clk,
    input  wire [WIDTH-1:0] inc,            // fsc / f_clk x 2^WIDTH
    input  wire             pal_en,
    input  wire [16:0]      burst,          // {start, end} in samples after the sync's trailing edge
    input  wire [7:0]       blk,            // the blanking level
    input  wire             hsync,          // t0
    input  wire             csync,
    input  wire             de,
    input  wire [3:0]       idx,            // palette index
    output wire [3:0]       sidx,           // the sweeper's palette read address ...
    input  wire [23:0]      rgb_s,          // ... and what pal_ram gives for it (R G B)
    (* IOB = "TRUE" *) output reg [7:0] y_o,
    (* IOB = "TRUE" *) output reg [7:0] c_o,
    (* IOB = "TRUE" *) output reg [7:0] cv_o
);
    // ---- the colour matrix, per pixel, in DSP48s ---------------------------------------------------------------------------
    // y17 = 306 R + 601 G + 117 B (the BT.601 luma x 1024), u = (B - y) x 504 >>> 10, v = (R - y) x 898 >>> 10, ys = 11 y / 16
    // (the DAC's white level). The multiplications by constants are what yc_out.sv builds from shift-add chains in LUTs.
    assign sidx = idx;
    wire [7:0] pr = rgb_s[23:16], pg = rgb_s[15:8], pb = rgb_s[7:0];
    reg [7:0] r1 = 8'd0, g1 = 8'd0, b1 = 8'd0;                        // t1
    (* use_dsp = "yes" *) reg [17:0] y17 = 18'd0;                      // t2
    reg [7:0] r2 = 8'd0, b2 = 8'd0;
    reg signed [9:0] u0 = 10'sd0, v0 = 10'sd0;                        // t3
    (* use_dsp = "yes" *) reg signed [19:0] um = 20'sd0, vm = 20'sd0;  // t4: u0 x 504, v0 x 898
    wire [7:0] y8 = y17[17:10];
    reg [7:0] yd3 = 8'd0, yd4 = 8'd0, yd5 = 8'd0;                     // y8 at t3, t4, t5
    (* use_dsp = "yes" *) reg [11:0] y11 = 12'd0;                      // 11 y8, at t6
    always @(posedge clk) begin
        r1 <= pr; g1 <= pg; b1 <= pb;
        y17 <= 18'd306 * r1 + 18'd601 * g1 + 18'd117 * b1;
        r2 <= r1; b2 <= b1;
        u0 <= $signed({2'b00, b2}) - $signed({2'b00, y8});
        v0 <= $signed({2'b00, r2}) - $signed({2'b00, y8});
        um <= u0 * 11'sd504;
        vm <= v0 * 11'sd898;
        yd3 <= y8; yd4 <= yd3; yd5 <= yd4;
        y11 <= yd5 * 12'd11;
    end
    reg [5:0] dq = 6'd0;                              // dq[n] = de at t(n+1)
    reg [6:0] cq = 7'd0;                              // cq[n] = csync at t(n+1)
    wire signed [8:0] u1 = um[18:10];                 // yc_out's phase[1].u / .v after >>>10, at t4
    wire signed [8:0] v1 = vm[18:10];
    reg [7:0] ys = 8'd0;                              // 11/16 luma (0 outside the picture), at t7
    always @(posedge clk) begin
        dq <= {dq[4:0], de};
        cq <= {cq[5:0], csync};
        ys <= dq[5] ? y11[11:4] : 8'd0;
    end

    // ---- carrier, burst, chroma (yc_out's arithmetic) ----------------------------------------------------------------------
    localparam [7:0] vref = 8'd128;
    function automatic [7:0] qs;                      // first quarter of the 8 bit sine: (sin(2 pi t / 255) x 255) + 0.5
        input [5:0] i;
        case (i)
        6'd0: qs = 8'd0;
        6'd1: qs = 8'd6;
        6'd2: qs = 8'd13;
        6'd3: qs = 8'd19;
        6'd4: qs = 8'd25;
        6'd5: qs = 8'd31;
        6'd6: qs = 8'd38;
        6'd7: qs = 8'd44;
        6'd8: qs = 8'd50;
        6'd9: qs = 8'd56;
        6'd10: qs = 8'd62;
        6'd11: qs = 8'd68;
        6'd12: qs = 8'd74;
        6'd13: qs = 8'd80;
        6'd14: qs = 8'd86;
        6'd15: qs = 8'd92;
        6'd16: qs = 8'd98;
        6'd17: qs = 8'd104;
        6'd18: qs = 8'd109;
        6'd19: qs = 8'd115;
        6'd20: qs = 8'd121;
        6'd21: qs = 8'd126;
        6'd22: qs = 8'd132;
        6'd23: qs = 8'd137;
        6'd24: qs = 8'd142;
        6'd25: qs = 8'd147;
        6'd26: qs = 8'd152;
        6'd27: qs = 8'd157;
        6'd28: qs = 8'd162;
        6'd29: qs = 8'd167;
        6'd30: qs = 8'd172;
        6'd31: qs = 8'd176;
        6'd32: qs = 8'd181;
        6'd33: qs = 8'd185;
        6'd34: qs = 8'd190;
        6'd35: qs = 8'd194;
        6'd36: qs = 8'd198;
        6'd37: qs = 8'd202;
        6'd38: qs = 8'd205;
        6'd39: qs = 8'd209;
        6'd40: qs = 8'd213;
        6'd41: qs = 8'd216;
        6'd42: qs = 8'd219;
        6'd43: qs = 8'd222;
        6'd44: qs = 8'd225;
        6'd45: qs = 8'd228;
        6'd46: qs = 8'd231;
        6'd47: qs = 8'd234;
        6'd48: qs = 8'd236;
        6'd49: qs = 8'd238;
        6'd50: qs = 8'd241;
        6'd51: qs = 8'd243;
        6'd52: qs = 8'd244;
        6'd53: qs = 8'd246;
        6'd54: qs = 8'd248;
        6'd55: qs = 8'd249;
        6'd56: qs = 8'd250;
        6'd57: qs = 8'd251;
        6'd58: qs = 8'd252;
        6'd59: qs = 8'd253;
        6'd60: qs = 8'd254;
        6'd61: qs = 8'd254;
        6'd62: qs = 8'd255;
        6'd63: qs = 8'd255;
        endcase
    endfunction
    function automatic signed [10:0] csin;
        input [7:0] x;
        reg [7:0] q;
        begin
            q = qs(x[6] ? ~x[5:0] : x[5:0]);
            csin = x[7] ? -$signed({3'b000, q}) : $signed({3'b000, q});
        end
    endfunction

    reg [WIDTH-1:0] phase_accum = {WIDTH{1'b0}};
    reg [7:0] chroma_LUT = 8'd0;
    reg signed [10:0] chroma_COS = 11'sd0, chroma_SIN = 11'sd0, chroma_BURST = 11'sd0;
    reg PAL_FLIP = 1'b0, PAL_line_count = 1'b0;
    reg [10:0] cburst_phase = 11'd0;
    reg signed [20:0] ph2_u = 21'sd0, ph2_v = 21'sd0, ph3_u = 21'sd0, ph3_v = 21'sd0;
    reg ph2_burst = 1'b0, ph2_chroma_en = 1'b0, ph3_burst = 1'b0, ph3_chroma_en = 1'b0;
    reg [7:0] ph4_c = 8'd128;
    always @(posedge clk) begin
        phase_accum <= phase_accum + inc;
        chroma_LUT  <= phase_accum[WIDTH-1:WIDTH-8];
        if (pal_en) chroma_BURST <= csin(chroma_LUT + (PAL_FLIP ? 8'd160 : 8'd96));
        else        chroma_BURST <= csin(chroma_LUT + 8'd128);
        chroma_SIN <= csin(chroma_LUT);
        chroma_COS <= csin(chroma_LUT + 8'd64);

        if (hsync) begin
            cburst_phase <= 11'd0;
            ph2_u <= 21'sd0; ph2_v <= 21'sd0; ph2_burst <= 1'b0; ph2_chroma_en <= 1'b0;
            ph4_c <= vref;
            if (PAL_line_count) begin
                PAL_FLIP <= ~PAL_FLIP;
                PAL_line_count <= 1'b0;
            end
        end else begin
            if (cburst_phase >= {4'd0, burst[16:10]} && cburst_phase <= {1'b0, burst[9:0]}) begin
                ph2_u <= $signed({chroma_BURST, 5'd0});
                ph2_v <= 21'sd0;
                ph2_burst <= 1'b1;
                ph2_chroma_en <= 1'b0;
                if (pal_en) ph3_u <= $signed(ph2_u[20:8]) + $signed(ph2_u[20:11]) + $signed(ph2_u[20:12]) + $signed(ph2_u[20:14]);
                else        ph3_u <= $signed(ph2_u[20:8]) + $signed(ph2_u[20:11]) + $signed(ph2_u[20:12]) + $signed(ph2_u[20:13]);
                ph3_v <= ph2_v;
            end else begin
                ph2_u <= u1 * chroma_SIN;
                ph2_v <= v1 * chroma_COS;
                ph2_burst <= 1'b0;
                ph2_chroma_en <= dq[3];
                ph3_u <= $signed(ph2_u[20:9]) + $signed(ph2_u[20:10]) + $signed(ph2_u[20:14]);
                ph3_v <= $signed(ph2_v[20:9]) + $signed(ph2_v[20:10]) + $signed(ph2_v[20:14]);
            end
            if (cburst_phase <= {1'b0, burst[9:0]}) cburst_phase <= cburst_phase + 11'd1;
            if (ph3_burst || ph3_chroma_en) begin
                if (pal_en) begin
                    ph4_c <= (PAL_FLIP ^ VINV[0]) ? (vref + ph3_u[7:0] - ph3_v[7:0]) : (vref + ph3_u[7:0] + ph3_v[7:0]);
                    PAL_line_count <= 1'b1;
                end else ph4_c <= vref + ph3_u[7:0] + ph3_v[7:0];
            end else ph4_c <= vref;
        end
        ph3_burst <= ph2_burst;
        ph3_chroma_en <= ph2_chroma_en;
    end

    // ---- levels, sync, composite: Y word = blanking level + 11/16 luma, C word = 128 + chroma, CV = clamp(Y + C - 128) ---------
    wire [8:0] yv = {1'b0, blk} + {1'b0, ys};
    wire [9:0] sm = {1'b0, yv} + {2'b00, ph4_c} - 10'd128;
    wire       s7 = cq[6];
    always @(posedge clk) begin
        y_o  <= s7 ? 8'd0   : yv[7:0];
        c_o  <= s7 ? 8'd128 : ph4_c;
        cv_o <= s7 ? 8'd0 : (sm[9] ? 8'd0 : (sm[8] ? 8'd255 : sm[7:0]));
    end
endmodule
