// The SID sockets' audio (T37 §4.3): the real chips' analog outputs go into the codec's AUX inputs, its ADCs send them back
// on AUDIO_SDO (adc_sdo) in the I2S frame the host drives (host/i2s_out.v: BCLK = MCLK / 4, LRCK = MCLK / 256, left
// while LRCK is low, the MSB one BCLK after each LRCK edge, data changing on the falling BCLK edge, 24 bits; the codec's
// setup in host/c64u_keyboard.v puts LAUXIN on the left ADC, RAUXIN on the right one). Left = socket 1, right = socket 2
// (T37 §2.5 H, M5 on the board settles it).
//
// mclk: BCLK and LRCK are the host's registers in this clock (i2s_out: BCLK rises with c = 3 mod 4, falls with c = 1).
// A bit is taken two clocks after the rising BCLK is seen, from the synchronized SDO: the pin one MCLK after the rise,
// two MCLKs after the codec changed it on the falling BCLK and one before its next change. LRCK goes through the same
// two flip-flops, so a slot's first bit (the delay bit) is the one the codec sent after it saw the LRCK edge. After the right channel, one multiplier makes the four products in turn, with the app's mixer bytes 4-7
// (U64_AUDIO_MIXER: socket 1 R, L, socket 2 R, L; 0x80 = 0 dB, the law of T25.3 §1.1); a socket that is not enabled adds
// nothing. The sum is added to the core's SID mix every clock and saturated to 24 bits.
module sid_sock_audio_c64ref (
    input  wire               mclk,
    input  wire               bclk,             // the host's I2S clocks (this clock)
    input  wire               lrck,
    input  wire               sdo,              // the codec's ADC data (the pin)
    input  wire        [31:0] gain,             // mixer bytes 7..4 (any clock, quasi-static)
    input  wire         [1:0] en,               // C64_SID1/2_EN (any clock, quasi-static)
    input  wire signed [23:0] in_l, in_r,       // the core's mix (this clock)
    output reg  signed [23:0] out_l = 24'sd0,
    output reg  signed [23:0] out_r = 24'sd0
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] sdo_s = 2'b00;
    reg  [1:0] lr_s = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [33:0] cfg_s1 = 34'd0, cfg_s2 = 34'd0;
    reg        bclk_d1 = 1'b0, bclk_d2 = 1'b0, lr_q = 1'b0;
    reg  [4:0] cnt = 5'd31;
    reg [22:0] sh = 23'd0;
    reg signed [23:0] adc_l = 24'sd0, adc_r = 24'sd0;
    reg        go = 1'b0;
    always @(posedge mclk) begin
        sdo_s   <= {sdo_s[0], sdo};
        lr_s    <= {lr_s[0], lrck};
        cfg_s1  <= {en, gain};
        cfg_s2  <= cfg_s1;
        bclk_d1 <= bclk;
        bclk_d2 <= bclk_d1;
        go      <= 1'b0;
        if (bclk_d1 && !bclk_d2) begin
            if (lr_s[1] != lr_q) begin          // bit 0 of a slot: the delay bit
                lr_q <= lr_s[1];
                cnt  <= 5'd0;
            end else begin
                if (cnt != 5'd31) cnt <= cnt + 5'd1;
                if (cnt < 5'd23) sh <= {sh[21:0], sdo_s[1]};
                if (cnt == 5'd23) begin
                    if (lr_q) begin adc_r <= {sh, sdo_s[1]}; go <= 1'b1; end
                    else         adc_l <= {sh, sdo_s[1]};
                end
            end
        end
    end

    // the four products, one per clock: {socket 1 L, socket 2 L, socket 1 R, socket 2 R}
    wire [1:0] e = cfg_s2[33:32];
    wire [7:0] g4 = cfg_s2[7:0], g5 = cfg_s2[15:8], g6 = cfg_s2[23:16], g7 = cfg_s2[31:24];
    reg  [2:0] st = 3'd4;
    reg signed [23:0] mx = 24'sd0;
    reg signed  [8:0] mg = 9'sd0;
    reg               ms = 1'b0;            // the product goes to the right sum
    reg               mv = 1'b0;
    reg signed [32:0] pr = 33'sd0;
    reg               pv = 1'b0, ps = 1'b0, pf = 1'b0;
    reg               mf = 1'b0;            // first product of a sum
    reg signed [26:0] acc_l = 27'sd0, acc_r = 27'sd0, sum_l = 27'sd0, sum_r = 27'sd0;
    // the running sum of the side that the product belongs to: the one adder serves acc and sum
    wire signed [26:0] nxt = (pf ? 27'sd0 : (ps ? acc_r : acc_l)) + (pr >>> 7);
    always @(posedge mclk) begin
        mv <= 1'b0;
        if (go) st <= 3'd0;
        else if (st != 3'd4) st <= st + 3'd1;
        if (st != 3'd4) begin
            mv <= 1'b1;
            case (st[1:0])
            2'd0: begin mx <= e[0] ? adc_l : 24'sd0; mg <= {1'b0, g5}; ms <= 1'b0; mf <= 1'b1; end
            2'd1: begin mx <= e[1] ? adc_r : 24'sd0; mg <= {1'b0, g7}; ms <= 1'b0; mf <= 1'b0; end
            2'd2: begin mx <= e[0] ? adc_l : 24'sd0; mg <= {1'b0, g4}; ms <= 1'b1; mf <= 1'b1; end
            default: begin mx <= e[1] ? adc_r : 24'sd0; mg <= {1'b0, g6}; ms <= 1'b1; mf <= 1'b0; end
            endcase
        end
        pr <= mx * mg;
        pv <= mv;
        ps <= ms;
        pf <= mf;
        if (pv) begin
            if (!ps) acc_l <= nxt;
            else     acc_r <= nxt;
            if (!ps && !pf) sum_l <= nxt;
            if (ps && !pf)  sum_r <= nxt;
        end
        out_l <= sat({{4{in_l[23]}}, in_l} + {sum_l[26], sum_l});
        out_r <= sat({{4{in_r[23]}}, in_r} + {sum_r[26], sum_r});
    end

    function signed [23:0] sat(input signed [27:0] v);
        if (v > 28'sd8388607)       sat = 24'sd8388607;
        else if (v < -28'sd8388608) sat = -24'sd8388608;
        else                        sat = v[23:0];
    endfunction
endmodule
