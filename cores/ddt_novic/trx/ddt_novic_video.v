// T168: the picture logic of look_ma_no_vic, from video_pattern.v (CC BY 4.0, DDT; rtl/NOTICE, rtl/video_pattern.v.diff is the difference to his file).
// Kept as he wrote it: the zoomed XOR luma background and its zoom phase (with the two combinational dividers), the 44 x 25 text screen (screen_matrix, font),
// the scroller (scroller.v, his file) and its extent, the hue ramp along x and per frame (hue_anim, 91 / 256 of x, SATURATION 44).
// Changed: his output was luma + a modulated PAL subcarrier (chroma phase accumulator, sine table, burst, sync, blanking, line alternation). This module has no analogue
// signal: it renders the colour that a PAL receiver with the nominal burst shows for it. The subcarrier sin(wt + hue) of an even line is the colour vector
// (U, V) = (A cos(hue), A sin(hue)) with A = 44 sine64 / 128 (his active_scaled); with B - Y = U / 0.493 and R - Y = V / 0.877 (PAL) that is a tint added to his luma.
// His raster counted 704 samples a line, two to a pixel; this one counts framebuffer pixels, and x feeds his logic as the sample 2 x (an even sample: the text and the
// scroller are made of pixel pairs; the background differs from the odd sample in the lowest bit only). y is the picture line (his y - BORDER_TOP).
`timescale 1ns / 1ps
module ddt_novic_video (
    input  wire        clk,                   // mclk
    input  wire        frame_start,           // one clock per frame
    input  wire [8:0]  px,                    // picture column 0..351 (+ blanking), stable for the pixel
    input  wire [8:0]  py,                    // picture line 0..199 (+ blanking)
    output wire [15:0] rgb565
);
    localparam integer IMG_LINES = 200;
    localparam integer IMG_COLS  = 352;

    wire img_active = (px < IMG_COLS) && (py < IMG_LINES);
    wire [9:0] x = {px, 1'b0};                               // his sample coordinate
    wire [8:0] y_img = py;

    // -------------------------------------------------------------------------
    // Existing monochrome background zoom effect.
    // -------------------------------------------------------------------------
    localparam integer BG_ZCX = 352;
    localparam integer BG_ZCY = 100;
    reg [31:0] inv_bzx_16p16 = 32'h00008000;
    reg [31:0] inv_bzy_16p16 = 32'h00010000;
    reg [8:0]  zoom_phase = 9'd0;

    // These values are combinational calculations from the current zoom phase; only inverse zoom factors.
    wire [7:0] zoom_t = zoom_phase[8] ? (8'hFF - zoom_phase[7:0]) : zoom_phase[7:0];
    wire [31:0] zoom_dx = (32'h00000600 * zoom_t) / 8'd255;
    wire [31:0] zoom_dy = (32'h00000300 * zoom_t) / 8'd255;
    wire [15:0] zoom_x_new = 16'h0200 + zoom_dx[15:0];
    wire [15:0] zoom_y_new = 16'h0100 + zoom_dy[15:0];
    wire [31:0] inv_x_new = 32'd16777216 / zoom_x_new;
    wire [31:0] inv_y_new = 32'd16777216 / zoom_y_new;

    always @(posedge clk) begin
        if (frame_start) begin
            zoom_phase <= zoom_phase + 9'd1;
            inv_bzx_16p16 <= inv_x_new;
            inv_bzy_16p16 <= inv_y_new;
        end
    end

    wire signed [11:0] bg_dx = $signed({1'b0, x}) - $signed(BG_ZCX[11:0]);
    wire signed [11:0] bg_dy = $signed({1'b0, y_img}) - $signed(BG_ZCY[11:0]);
    wire signed [43:0] bg_dx_mul = bg_dx * $signed(inv_bzx_16p16);
    wire signed [43:0] bg_dy_mul = bg_dy * $signed(inv_bzy_16p16);
    wire signed [11:0] bg_dx_s = bg_dx_mul >>> 16;
    wire signed [11:0] bg_dy_s = bg_dy_mul >>> 16;
    wire signed [11:0] bg_xs = $signed(BG_ZCX[11:0]) + bg_dx_s;
    wire signed [11:0] bg_ys = $signed(BG_ZCY[11:0]) + bg_dy_s;
    wire signed [11:0] bg_u = bg_xs - $signed(BG_ZCX[11:0]);
    wire signed [11:0] bg_v = bg_ys - $signed(BG_ZCY[11:0]);
    wire [7:0] luma_bg = bg_u[7:0] ^ bg_v[7:0];

    // Chars are 16 output pixels wide and 8 lines high.
    wire [5:0] cx = x[9:4];
    wire [4:0] cy = y_img[7:3];
    wire [2:0] font_x = x[3:1];
    wire [2:0] font_y = y_img[2:0];

    wire [7:0] char_code_raw;
    screen_matrix u_scr (.cx(cx), .cy(cy), .code(char_code_raw));
    wire [7:0] char_code = (char_code_raw >= 8'd65 && char_code_raw <= 8'd90) ?
                           (char_code_raw + 8'd32) : char_code_raw;
    wire [10:0] font_addr = {char_code, font_y};
    wire [7:0] font_row;
    c64_font_rom u_font (.addr(font_addr), .data(font_row));
    wire font_bit = font_row[7 - font_x];

    // Clamp the animated background to the lowest Luma floor (i.e. BG_FLOOR).
    localparam [7:0] BG_FLOOR = 8'd16;
    wire [7:0] base_pix = font_bit ? 8'hFF :
                          ((luma_bg < BG_FLOOR) ? BG_FLOOR : luma_bg);

    wire scroll_on;
    scroller u_scroll (
        .clk(clk), .frame_start(frame_start), .en(img_active),
        .x(x), .y(y_img), .pix_on(scroll_on)
    );

    // -------------------------------------------------------------------------
    // The hue ramp, as a colour vector instead of a subcarrier phase.
    // -------------------------------------------------------------------------
    reg [7:0]  hue_anim = 8'd0;
    always @(posedge clk) begin
        if (frame_start)
            hue_anim <= hue_anim + 8'd1;
    end

    function signed [7:0] sine64;
        input [5:0] phase;
        begin
            case (phase)
            6'd 0: sine64 = 8'sd0;
            6'd 1: sine64 = 8'sd12;
            6'd 2: sine64 = 8'sd25;
            6'd 3: sine64 = 8'sd37;
            6'd 4: sine64 = 8'sd49;
            6'd 5: sine64 = 8'sd60;
            6'd 6: sine64 = 8'sd71;
            6'd 7: sine64 = 8'sd81;
            6'd 8: sine64 = 8'sd90;
            6'd 9: sine64 = 8'sd98;
            6'd10: sine64 = 8'sd106;
            6'd11: sine64 = 8'sd112;
            6'd12: sine64 = 8'sd117;
            6'd13: sine64 = 8'sd122;
            6'd14: sine64 = 8'sd125;
            6'd15: sine64 = 8'sd126;
            6'd16: sine64 = 8'sd127;
            6'd17: sine64 = 8'sd126;
            6'd18: sine64 = 8'sd125;
            6'd19: sine64 = 8'sd122;
            6'd20: sine64 = 8'sd117;
            6'd21: sine64 = 8'sd112;
            6'd22: sine64 = 8'sd106;
            6'd23: sine64 = 8'sd98;
            6'd24: sine64 = 8'sd90;
            6'd25: sine64 = 8'sd81;
            6'd26: sine64 = 8'sd71;
            6'd27: sine64 = 8'sd60;
            6'd28: sine64 = 8'sd49;
            6'd29: sine64 = 8'sd37;
            6'd30: sine64 = 8'sd25;
            6'd31: sine64 = 8'sd12;
            6'd32: sine64 = 8'sd0;
            6'd33: sine64 = -8'sd12;
            6'd34: sine64 = -8'sd25;
            6'd35: sine64 = -8'sd37;
            6'd36: sine64 = -8'sd49;
            6'd37: sine64 = -8'sd60;
            6'd38: sine64 = -8'sd71;
            6'd39: sine64 = -8'sd81;
            6'd40: sine64 = -8'sd90;
            6'd41: sine64 = -8'sd98;
            6'd42: sine64 = -8'sd106;
            6'd43: sine64 = -8'sd112;
            6'd44: sine64 = -8'sd117;
            6'd45: sine64 = -8'sd122;
            6'd46: sine64 = -8'sd125;
            6'd47: sine64 = -8'sd126;
            6'd48: sine64 = -8'sd127;
            6'd49: sine64 = -8'sd126;
            6'd50: sine64 = -8'sd125;
            6'd51: sine64 = -8'sd122;
            6'd52: sine64 = -8'sd117;
            6'd53: sine64 = -8'sd112;
            6'd54: sine64 = -8'sd106;
            6'd55: sine64 = -8'sd98;
            6'd56: sine64 = -8'sd90;
            6'd57: sine64 = -8'sd81;
            6'd58: sine64 = -8'sd71;
            6'd59: sine64 = -8'sd60;
            6'd60: sine64 = -8'sd49;
            6'd61: sine64 = -8'sd37;
            6'd62: sine64 = -8'sd25;
            6'd63: sine64 = -8'sd12;
            default: sine64 = 8'sd0;
            endcase
        end
    endfunction

    // One full hue rotation across the 720 hor pixels: floor(x * 256 / 720).
    // Use 91/256 as an approximation of 256/720.
    wire [17:0] hue_mul = x * 18'd91;
    wire [7:0]  hue_x = hue_mul[15:8];
    wire [7:0]  hue_phase = hue_x + hue_anim;

    localparam [7:0] SATURATION = 8'd44;
    wire signed [7:0] hue_sin = sine64(hue_phase[7:2]);
    wire signed [7:0] hue_cos = sine64(hue_phase[7:2] + 6'd16);
    // his active_scaled: (sine * SATURATION) >>> 7; here the two axes of the vector
    wire signed [16:0] u_prod = $signed(hue_cos) * $signed({1'b0, SATURATION});
    wire signed [16:0] v_prod = $signed(hue_sin) * $signed({1'b0, SATURATION});
    wire signed [9:0]  u_c = u_prod >>> 7;                                // U = A cos(hue)
    wire signed [9:0]  v_c = v_prod >>> 7;                                // V = A sin(hue)

    // PAL: B - Y = U / 0.493 (x 260 / 128), R - Y = V / 0.877 (x 146 / 128), G - Y = -0.509 (R - Y) - 0.194 (B - Y) (x 65 and 25 / 128); constants and floors are within 2 counts of the exact matrix
    wire signed [19:0] d_b_p = u_c * 20'sd260;
    wire signed [19:0] d_r_p = v_c * 20'sd146;
    wire signed [10:0] d_b = d_b_p >>> 7;
    wire signed [10:0] d_r = d_r_p >>> 7;
    wire signed [19:0] d_g_p = -((d_r * 20'sd65) + (d_b * 20'sd25));
    wire signed [10:0] d_g = d_g_p >>> 7;

    wire plain = scroll_on || font_bit;                                   // text and scroller: luma FF, no chroma
    wire [7:0] luma = plain ? 8'hFF : base_pix;
    wire signed [10:0] luma_s = {3'b000, luma};
    wire signed [11:0] r_s = luma_s + (plain ? 11'sd0 : d_r);
    wire signed [11:0] g_s = luma_s + (plain ? 11'sd0 : d_g);
    wire signed [11:0] b_s = luma_s + (plain ? 11'sd0 : d_b);
    wire [7:0] r8 = r_s[11] ? 8'd0 : (r_s > 12'sd255) ? 8'd255 : r_s[7:0];
    wire [7:0] g8 = g_s[11] ? 8'd0 : (g_s > 12'sd255) ? 8'd255 : g_s[7:0];
    wire [7:0] b8 = b_s[11] ? 8'd0 : (b_s > 12'sd255) ? 8'd255 : b_s[7:0];

    assign rgb565 = img_active ? {r8[7:3], g8[7:2], b8[7:3]} : 16'h0000;
endmodule
