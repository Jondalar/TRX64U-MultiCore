// SPDX-License-Identifier: GPL-3.0-or-later
// U16 D6: the HDMI mode timing generator, ported from TRX64U-MultiCore's host/hdmi_timing.v (T164 WP2 there). One row of
// hdmi_modes.vh (tools/hdmi_modes.py gen) is taken while rst is high and kept; the generator then runs free. A mode that is not in the
// table (hm_valid 0) is refused: `valid` is 0 and the generator never starts (`running` stays 0). mode_req.v never hands one down.
//
// `align`: a level from vic_scaler (held until a ce takes it); on a locked row (every row of this design, hm_locked) it restarts the frame
// at the origin of the active area: the pixel after the one registered at the ce that takes it is cx = xact0, cy = yact0.
// `aligned` is set by the first accepted align and cleared by rst.
//
// Pixel repetition ("gear" g, 1/2/3/5): clk is the TMDS clock, g x the pixel clock; every video pixel lasts g clocks. `ce` is high in
// the last clock of a pixel; every register of the pixel chain after this module (vic_scaler, overlay, pix_layer, c64_text, the encoder)
// changes only when ce is high, so each pixel value is held for g clocks and tmds_rep.v sends each of its TMDS bits g times.
//
// Counters, as the pixel chain takes them (cx, cy): cx = 0 at the first clock after the active area, then front porch, sync, back porch
// (blanking B = hfp + hsync + hbp), the active area is cx B .. B + hact - 1; cy = 0 at the first line after the active lines, vertical
// front porch, sync, back porch (Bv), active lines Bv .. Bv + vact - 1. xact0 = B and yact0 = Bv go down to the layers as their origin
// (they replaced the 768 / 45 of vic_scaler's fixed 1080p before U16), htot_m1 / vtot_m1 to vic_scaler. hs/vs have the mode's polarity.
// rgb is black.
//
// For the encoder: sx_e = g * B (first active TMDS clock of the line), fw_m1 = g * htot - 1, sy = Bv, ey = Bv + vact = vtot, and the
// phase ph_e: clocks since the pixel registers last changed, so that g * cx + ph_e is the position in TMDS clocks. (c64_basic's encoder,
// hdmi_enc/hdmi.v, counts pixels and takes xact0 / yact0 instead; these stay for the bench.)
module hdmi_timing (
    input  wire        clk,
    input  wire        rst,             // sync to clk; mode is taken while it is high
    input  wire [3:0]  mode,            // family * 6 + hdmi (hdmi_modes.vh)
    input  wire        align,           // level (clk), taken at a ce: re-phase the frame on a locked row
    output wire        valid,           // the captured mode is a row of the table
    output reg         aligned,         // an align pulse was accepted since rst
    output reg         ce,
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,
    output reg  [10:0] cy,
    output reg         running,
    // the mode, static while running
    output reg  [11:0] hact,
    output reg  [10:0] vact,
    output reg  [11:0] xact0,
    output reg  [10:0] yact0,
    output reg  [3:0]  gear,
    output reg  [11:0] htot_m1,         // last cx of a line
    output reg  [10:0] vtot_m1,         // last cy of a frame
    output reg  [6:0]  vic,
    output reg  [31:0] rate,            // the PIXEL clock in Hz in the family's first System Mode (hm_rate); the TMDS clock is gear times it
    output reg  [12:0] sx_e,
    output reg  [12:0] fw_m1,
    output reg  [10:0] sy_e,
    output reg  [10:0] ey_e,
    output reg  [2:0]  ph_e,
    output wire [3:0]  mode_q           // the mode this generator runs (JTAG read-out)
);
`include "hdmi_modes.vh"

    // ---- the mode: captured while rst, derived values a clock later (rst is held for several clocks, running waits for them)
    reg  [3:0]   m = 4'd2;
    assign mode_q = m;
    assign valid = hm_valid(m);
    wire do_align = align && hm_locked(m);
    // MultiCore T164 run 1126 (-0.165 ns at 150 MHz: m -> table -> four-term sums -> vtot_m1 / htot_m1): the row is registered
    // before the sums. One clock more for the derived values; m is captured while rst and is stable for many clocks before rst falls.
    reg  [104:0] row = 105'd0;
    always @(posedge clk) row <= hm_row(m);
    wire [6:0]  r_vic   = row[104:98];
    wire [3:0]  r_gear  = row[97:94];
    wire [11:0] r_hact  = row[93:82];
    wire [11:0] r_hfp   = row[81:70];
    wire [11:0] r_hsync = row[69:58];
    wire [11:0] r_hbp   = row[57:46];
    wire        r_hpol  = row[45];
    wire [10:0] r_vact  = row[44:34];
    wire [10:0] r_vfp   = row[33:23];
    wire [10:0] r_vsync = row[22:12];
    wire [10:0] r_vbp   = row[11:1];
    wire        r_vpol  = row[0];
    reg  [11:0] hfp, hs0, hs1;               // hs0 .. hs1 - 1: the sync pulse
    reg         hpol, vpol;
    reg  [10:0] vfp_r, vs0, vs1;
    reg  [3:0]  wait_n = 4'd0;
    wire [12:0] htot = {1'b0, htot_m1} + 13'd1;
    always @(posedge clk) begin
        if (rst) m <= mode;
        vic <= r_vic; gear <= r_gear; hact <= r_hact; vact <= r_vact; hpol <= r_hpol; vpol <= r_vpol;
        rate <= hm_rate(m);
        hfp <= r_hfp; hs0 <= r_hfp; hs1 <= r_hfp + r_hsync;
        xact0 <= r_hfp + r_hsync + r_hbp;
        htot_m1 <= r_hfp + r_hsync + r_hbp + r_hact - 12'd1;
        vs0 <= r_vfp; vs1 <= r_vfp + r_vsync;
        yact0 <= r_vfp + r_vsync + r_vbp;
        vtot_m1 <= r_vfp + r_vsync + r_vbp + r_vact - 11'd1;
        sy_e <= r_vfp + r_vsync + r_vbp;
        ey_e <= r_vfp + r_vsync + r_vbp + r_vact;
    end
    // the encoder's TMDS-clock positions: g x
    function [12:0] times_g(input [11:0] v, input [3:0] g);
        case (g)
        4'd1:    times_g = {1'b0, v};
        4'd2:    times_g = {v, 1'b0};
        4'd3:    times_g = {v, 1'b0} + {1'b0, v};
        4'd4:    times_g = {v[10:0], 2'b00};
        default: times_g = {v[10:0], 2'b00} + {1'b0, v};     // 5
        endcase
    endfunction
    always @(posedge clk) begin
        sx_e  <= times_g(xact0, gear);
        fw_m1 <= times_g(htot_m1 + 12'd1, gear) - 13'd1;
    end

    // ---- pixel phase and counters
    reg  [2:0]  ph = 3'd0;
    wire [2:0]  ph_n = (ph == gear[2:0] - 3'd1) ? 3'd0 : ph + 3'd1;
    reg  [11:0] hc = 12'd0;
    reg  [10:0] vc = 11'd0;
    wire last_h = hc == htot_m1;
    always @(posedge clk) begin
        if (rst) begin
            running <= 1'b0; aligned <= 1'b0; wait_n <= 4'd0; ph <= 3'd0; ce <= 1'b0; ph_e <= 3'd0;
            hc <= 12'd0; vc <= 11'd0; de <= 1'b0; hs <= 1'b0; vs <= 1'b0; cx <= 12'd0; cy <= 11'd0; rgb <= 24'd0;
        end else if (!running) begin
            wait_n <= wait_n + 4'd1;
            if (wait_n == 4'd8 && valid) begin running <= 1'b1; ce <= (gear == 4'd1); ph <= 3'd0; end
        end else begin
            ph <= ph_n;
            ce <= (ph_n == gear[2:0] - 3'd1);
            ph_e <= ce ? 3'd0 : ph_e + 3'd1;
            if (ce) begin
                de <= (hc >= xact0) && (vc >= yact0);
                hs <= (hc >= hs0 && hc < hs1) ? hpol : ~hpol;
                vs <= (vc >= vs0 && vc < vs1) ? vpol : ~vpol;
                cx <= hc;
                cy <= vc;
                if (do_align) begin
                    hc <= xact0; vc <= yact0; aligned <= 1'b1;
                end else if (last_h) begin
                    hc <= 12'd0;
                    vc <= (vc == vtot_m1) ? 11'd0 : vc + 11'd1;
                end else
                    hc <= hc + 12'd1;
            end
        end
    end
endmodule
