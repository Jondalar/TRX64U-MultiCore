// I2S to the NAU88C22 (slave, 24 bit, MCLK = 256 fs), as its setup in
// c64u_keyboard.v expects: BCLK = MCLK / 4 (64 fs), LRCK = MCLK / 256, left
// channel while LRCK is low, the MSB one BCLK after each LRCK edge, data
// changing on the falling BCLK edge.
//
// The sample source runs in mclk too: frame_en asks it for the next pair
// once per frame (48 kHz), and the pair is taken at the following frame start.
//
// spk is a second data line on the same BCLK/LRCK for the C64U's built-in
// speaker amplifier (SPEAKER_DATA): the mono mix in both slots, 12 dB down
// (Gideon's i2s_serializer.vhd drives his i2s_spk the same way, one mono
// sample per slot), scaled by the app's speaker mixer gain and off unless
// it enabled the speaker.
module i2s_out (
    input  wire               mclk,         // 12.288 MHz
    output reg                frame_en,     // one mclk per frame: sample now
    input  wire signed [23:0] in_l, in_r,
    input  wire               spk_en,       // the app's speaker setting (async, static)
    input  wire         [7:0] spk_gain,     // the app's speaker mixer, 0x80 = 0 dB (async, static)
    output reg                bclk,
    output reg                lrck,
    output reg                sdata,
    output reg                spk
);
    reg [47:0] cur;
    reg [23:0] cur_spk;
    wire signed [24:0] mix = in_l + in_r;
    (* ASYNC_REG = "TRUE" *) reg [8:0] spk_s1, spk_s2;
    always @(posedge mclk) begin spk_s1 <= {spk_en, spk_gain}; spk_s2 <= spk_s1; end
    wire signed [33:0] spk_scaled = ($signed(mix[24:1]) >>> 2) * $signed({1'b0, spk_s2[7:0]});
    reg  [7:0] c = 8'd0;                        // 256 MCLKs per frame
    always @(posedge mclk) begin
        c <= c + 8'd1;
        frame_en <= (c == 8'd127);                  // half a frame ahead of its use
        if (c == 8'd255) begin                      // frame start: the new pair
            cur     <= {in_l, in_r};
            cur_spk <= spk_s2[8] ? spk_scaled[30:7] : 24'd0;   // mono, 12 dB down, x gain/128
        end
        bclk <= c[1];
        if (c[1:0] == 2'b11) begin                  // BCLK falls next: new data bit
            // bit b of the frame that starts now (b = 0..63)
            lrck  <= slot_lr(c + 8'd1);
            sdata <= slot_bit(c + 8'd1);
            spk   <= spk_bit(c + 8'd1);
        end
    end

    function slot_lr(input [7:0] cc);
        slot_lr = cc[7];
    endfunction

    function slot_bit(input [7:0] cc);
        reg [5:0] b;
        reg [4:0] pos;
        begin
            b   = cc[7:2];
            pos = b[4:0];
            if (pos >= 5'd1 && pos <= 5'd24)
                slot_bit = b[5] ? cur[24 - pos] : cur[24 + 24 - pos];
            else
                slot_bit = 1'b0;
        end
    endfunction
    function spk_bit(input [7:0] cc);
        reg [4:0] pos;
        begin
            pos = cc[6:2];
            spk_bit = (pos >= 5'd1 && pos <= 5'd24) ? cur_spk[24 - pos] : 1'b0;
        end
    endfunction
endmodule
