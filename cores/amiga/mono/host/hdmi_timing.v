// T164 WP2 (the host, all cores): the HDMI mode timing generator, from the A1200's (T107 D7). One of the 9 modes of host/hdmi_modes.vh
// (tools/hdmi_modes.py host-gen) is taken while rst is high and kept; the generator then runs free. A mode that is not in the table
// (hm_valid 0: family 0 with hdmi 3..5, or mode 12..15) is refused: `valid` is 0 and the generator never starts (`running` stays 0).
//
// `align` (T164 G5, Q4): a one-clock pulse from the VIC path; only on a locked row (hm_locked: the two 1080p rows) it restarts the frame at
// the origin of the active area (the pixel after the one registered at the pulse clock is cx = xact0, cy = yact0), as vic_scaler's `frame_seen && !locked` did with its own counters.
// On every other row it is ignored. `aligned` is set by the first accepted pulse and cleared by rst.
//
// Pixel repetition ("gear" g, 1/2/3/5): clk is the TMDS clock, g x the pixel clock; every video pixel lasts g clocks. `ce` is high in
// the last clock of a pixel; every register of the pixel chain after this module (overlay, pix_layer) changes only when ce is high,
// so each pixel value is held for g clocks and the encoder (hdmi.v fork, clocked every cycle) sends it g times.
//
// Counters, as the pixel chain takes them (cx, cy): cx = 0 at the first clock after the active area, then front porch, sync, back porch
// (blanking B = hfp + hsync + hbp), the active area is cx B .. B + hact - 1; cy = 0 at the first line after the active lines, vertical
// front porch, sync, back porch (Bv), active lines Bv .. Bv + vact - 1. xact0 = B and yact0 = Bv go down to the layers as their origin
// (they replaced the 768 / 45 of vic_scaler's fixed 1080p). hs/vs have the mode's polarity. rgb is black.
//
// For the encoder: sx_e = g * B (first active TMDS clock of the line), fw_m1 = g * htot - 1, sy = Bv, ey = Bv + vact = vtot, and the
// phase ph_e: clocks since the pixel registers last changed, so that g * cx + ph_e is the position in TMDS clocks.
// T188 D3 (the Amiga copies only, FOLLOW = 1): on the 50 Hz rows (m 0..2) the frame length follows the core. core_tgl is a toggle of the core's
// frame-complete event (sys_clk domain, crossed here). At every frame start the phase (lines since the last core event, `age`) is compared with the
// set point S = 21/32 of the frame (13 ms of 20: after the application's flip latency, before the next core frame) and the frame gets
// E = -(age - S) / 4 lines (rounded, limits EMIN .. EMAX): E > 0 lines repeated at the last front porch line before the sync pulse, E < 0 lines taken
// out of the back porch, right after the sync pulse. A phase that runs away (output faster than the core) shortens the next frame, a lagging one lengthens it. No core event for three frames, a 60 Hz
// row or FOLLOW = 0: E = 0, the frame of the table. cy values of the active area, cy = 0 and cy = 1 are never repeated or skipped.
module hdmi_timing #(
    parameter FOLLOW = 0
) (
    input  wire        clk,
    input  wire        rst,             // sync to clk; mode is taken while it is high
    input  wire [3:0]  mode,            // family * 6 + hdmi (hdmi_modes.vh)
    input  wire        align,           // pulse (clk): re-phase the frame on a locked row
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
    output reg  [6:0]  vic,
    output reg  [31:0] rate,            // the PIXEL clock in Hz (hm_rate); the TMDS clock is gear times it
    output reg  [12:0] sx_e,
    output reg  [12:0] fw_m1,
    output reg  [10:0] sy_e,
    output reg  [10:0] ey_e,
    output reg  [2:0]  ph_e,
    output wire [3:0]  mode_q,          // the mode this generator runs (JTAG read-out)
    input  wire        core_tgl,        // T188 D3: toggle, one per complete core frame (any clock domain; crossed here). Unused with FOLLOW = 0
    output wire signed [5:0] follow_e   // T188 D3: the lines the frame has more (+) or fewer (-) than the table's, for the readout and the bench
);
`include "hdmi_modes.vh"

    // ---- the mode: captured while rst, derived values a clock later (rst is held for several clocks, running waits for them)
    reg  [3:0]   m = 4'd2;
    assign mode_q = m;
    assign valid = hm_valid(m);
    wire do_align = align && hm_locked(m);
    // T164 run 1126 (cpc -0.165 ns, nes -0.039 ns at 150 MHz: m -> table -> four-term sums -> vtot_m1 / htot_m1): the row is registered
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
    reg  [11:0] hfp, hs0, hs1, htot_m1;      // hs0 .. hs1 - 1: the sync pulse
    reg         hpol, vpol;
    reg  [10:0] vfp_r, vs0, vs1, vtot_m1;
    reg  [10:0] vs0m1, vs1m1;                // last front porch line before the sync pulse, last sync line
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
        vs0m1 <= r_vfp - 11'd1; vs1m1 <= r_vfp + r_vsync - 11'd1;
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

    // ---- T188 D3: the frame follows the core (see the header)
    localparam signed [5:0] EMIN = -6'sd4, EMAX = 6'sd16;
    reg  [15:0] s_a = 16'd0;                 // 21 x vtot, two stages after the row (m is stable long before running)
    reg  [10:0] s_set = 11'd0;               // the set point (lines)
    always @(posedge clk) begin
        s_a   <= {ey_e, 4'd0} + {3'd0, ey_e, 2'd0} + {5'd0, ey_e};
        s_set <= s_a[15:5];
    end
    wire        fol_en = (FOLLOW != 0) && (m < 4'd3);
    (* ASYNC_REG = "TRUE" *) reg [2:0] cs = 3'd0;
    always @(posedge clk) cs <= {cs[1:0], core_tgl};
    wire        cev = cs[2] ^ cs[1];
    reg         cseen = 1'b0;                // a core frame event since the last line end
    reg         fr_seen = 1'b0;              // ... since the frame start
    reg  [11:0] age = 12'd0;                 // lines since the last core event (saturates)
    reg  [1:0]  miss = 2'd3;                 // frames without a core event (3 = none for three frames: the core is not running)
    reg  signed [5:0] fe = 6'sd0;
    reg  [4:0]  pad = 5'd0, skip = 5'd0;
    assign follow_e = fe;
    wire [11:0] age_n = cseen ? 12'd0 : (&age ? age : age + 12'd1);
    wire signed [12:0] err = $signed({1'b0, age_n}) - $signed({2'b0, s_set});
    wire        alive_n = fr_seen || cseen || (miss != 2'd3);
    wire signed [12:0] tgt = -((err + 13'sd2) >>> 2);              // proportional: a quarter of the phase error in lines, rounded; the frame's lines follow the phase
    wire signed [5:0] fe_n = !(fol_en && alive_n) ? 6'sd0 : (tgt > $signed({7'd0, EMAX})) ? EMAX : (tgt < $signed(-{7'd0, -EMIN})) ? EMIN : tgt[5:0];

    // ---- pixel phase and counters
    reg  [2:0]  ph = 3'd0;
    wire [2:0]  ph_n = (ph == gear[2:0] - 3'd1) ? 3'd0 : ph + 3'd1;
    reg  [11:0] hc = 12'd0;
    reg  [10:0] vc = 11'd0;
    wire last_h = hc == htot_m1;
    always @(posedge clk) begin
        if (cev) cseen <= 1'b1;
        if (rst) begin
            fe <= 6'sd0; pad <= 5'd0; skip <= 5'd0; age <= 12'd0; miss <= 2'd3; cseen <= 1'b0; fr_seen <= 1'b0;
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
                    // T188 D3: pad lines repeat the last front porch line, skipped lines are taken from the start of the back porch
                    if (fol_en && vc == vs0m1 && pad != 5'd0) begin
                        pad <= pad - 5'd1;
                    end else if (fol_en && vc == vs1m1 && skip != 5'd0) begin
                        vc <= vc + 11'd1 + {6'd0, skip}; skip <= 5'd0;
                    end else
                        vc <= (vc == vtot_m1) ? 11'd0 : vc + 11'd1;
                    age <= age_n; cseen <= cev;
                    if (vc == vtot_m1) begin             // the next line starts a frame: phase against the set point, the length of this frame
                        fe <= fe_n; fr_seen <= 1'b0;
                        miss <= (fr_seen || cseen) ? 2'd0 : (miss == 2'd3 ? miss : miss + 2'd1);
                        pad  <= (fe_n > 6'sd0) ? fe_n[4:0] : 5'd0;
                        skip <= (fe_n < 6'sd0) ? -fe_n[4:0] : 5'd0;
                    end else if (cseen) fr_seen <= 1'b1;
                end else
                    hc <= hc + 12'd1;
            end
        end
    end
endmodule
