// T134: the PET picture. Written for this project (TREX), no upstream code (the character ccell, the reverse bit and the blank / graphics
// controls follow what pet2001vid.v of Pet2001_Arty does with them: charaddr = {gfx, code[6:0], scan}, bit 7 of the code reverses, MSB leftmost).
//
// One raster of its own: 800 dots x 657 lines (59.98 Hz) or 788 lines (50.01 Hz) at one dot per sys_clk (31.5278 MHz), 640 x 400 visible. A text
// ccell is 8 x 8 source pixels, shown doubled in height (16 lines) and, in the 40 column picture, doubled in width (16 dots). The CRTC models
// (8032, 8096, SuperPET) show 80 columns while R1 >= 30 (a CRTC character is two columns on these boards: the editor ROMs program R1 = 40; below 30 a 40 column picture), else 40. The frame writer (pet_pixw) takes the visible window; the dot strobe is 1.
//
// The fetch runs one ccell ahead on the clock count `ph` inside the ccell: ph 0 video RAM address, ph 2 character ROM address, ph 4 the byte is
// latched (reverse and cursor applied), at the next ccell's ph 0 it goes into the shifter. The ccell after the lastc of a line is ccell 0 of the
// next line (`lastc`), so the first ccell of every line is ready when the dot count starts.
//
// Video RAM address = start + row * columns + column (CRTC start address R12/R13 on the CRTC models, 0 otherwise); the video RAM answers to
// the low 11 bits. The cursor (R10, R11, R14/R15) is the CRTC's: block between the scan lines R10[4:0] and R11[4:0], mode R10[6:5].
`timescale 1ns / 1ps
module pet_vid (
    input  wire        clk,
    input  wire        hz50,
    input  wire [1:0]  phos,                 // 0 green, 1 amber, 2 white
    input  wire        alt,                  // the upper pair of character sets (CRTC R12 bit 5 = MA13 on the CRTC models, T216 P6)
    input  wire        crtc_m,               // the model has a CRTC
    input  wire [7:0]  r1,                   // CRTC R1 displayed characters: the 80 column boards show two columns per CRTC character, so the real editors program 40 (T216 P7)
    input  wire [11:0] start,                // CRTC R12[3:0], R13
    input  wire [13:0] cur,                  // CRTC R14[5:0], R15
    input  wire [7:0]  r10,
    input  wire [4:0]  r11,
    input  wire        gfx,                  // 1: graphics character set, 0: lower case set
    input  wire        blank,                // the PIA's screen blanking
    output reg  [10:0] v_a = 11'd0,
    input  wire [7:0]  v_q,
    output reg  [11:0] c_a = 12'd0,
    input  wire [7:0]  c_q,
    output reg         hblank = 1'b1,
    output reg         vblank = 1'b1,
    output wire        video_on,
    output wire        dotst,
    output reg  [15:0] rgb565 = 16'd0,
    output reg         frame_end = 1'b0      // a one-clock pulse at the end of every frame
);
    reg  [9:0] x = 10'd0;
    reg  [9:0] y = 10'd0;
    reg        col80 = 1'b0;
    wire [9:0] ht = hz50 ? 10'd788 : 10'd657;
    wire       xlast = (x == 10'd799);
    wire       ylast = (y == ht - 10'd1);
    wire [3:0] ph = col80 ? {1'b0, x[2:0]} : x[3:0];
    wire [6:0] ccell = col80 ? x[9:3] : {1'b0, x[9:4]};
    wire       lastc = col80 ? (ccell == 7'd99) : (ccell == 7'd49);
    wire [6:0] fcell = lastc ? 7'd0 : ccell + 7'd1;
    wire [9:0] fy = lastc ? (ylast ? 10'd0 : y + 10'd1) : y;
    wire [4:0] frow = fy[8:4];
    wire [2:0] fscan = fy[3:1];
    wire [11:0] rowoff = col80 ? ({7'd0, frow} << 6) + ({7'd0, frow} << 4) : ({7'd0, frow} << 5) + ({7'd0, frow} << 3);
    wire [13:0] faddr = (crtc_m ? {2'b00, start} : 14'd0) + {2'b00, rowoff} + {7'd0, fcell};

    reg [2:0]  scan_f = 3'd0;
    reg        cur_f = 1'b0, inv_f = 1'b0;
    reg [7:0]  nb = 8'd0, sh = 8'd0;
    reg        pix = 1'b0;
    reg [5:0]  fcnt = 6'd0;
    wire       blink_on = (r10[6:5] == 2'd0) ? 1'b1 : (r10[6:5] == 2'd1) ? 1'b0 : (r10[6:5] == 2'd2) ? fcnt[4] : fcnt[5];
    wire       cur_scan = (scan_f >= r10[2:0]) && (scan_f <= r11[2:0]) && !r10[3] && !r11[3] && !r10[4] && !r11[4];
    wire       pe = col80 | ~x[0];
    reg        hb0 = 1'b1, vb0 = 1'b1;
    wire [15:0] on_rgb = (phos == 2'd0) ? 16'h27E6 : (phos == 2'd1) ? 16'hFD80 : 16'hFFFF;

    assign video_on = ~vblank;
    assign dotst = 1'b1;

    always @(posedge clk) begin
        frame_end <= 1'b0;
        // raster
        if (xlast) begin
            x <= 10'd0;
            if (ylast) begin
                y <= 10'd0;
                col80 <= crtc_m && (r1 >= 8'd30);
                fcnt <= fcnt + 6'd1;
                frame_end <= 1'b1;
            end else y <= y + 10'd1;
        end else x <= x + 10'd1;
        // fetch pipeline
        if (ph == 4'd0) begin
            v_a <= faddr[10:0];
            scan_f <= fscan;
            cur_f <= crtc_m && (faddr == cur);
        end
        if (ph == 4'd2) begin
            c_a <= {alt, gfx, v_q[6:0], scan_f};
            inv_f <= v_q[7];
        end
        if (ph == 4'd4) nb <= (c_q ^ {8{inv_f}}) ^ {8{cur_f & blink_on & cur_scan}};
        // dots
        if (ph == 4'd0) begin
            sh <= {nb[6:0], 1'b0};
            pix <= nb[7];
        end else if (pe) begin
            sh <= {sh[6:0], 1'b0};
            pix <= sh[7];
        end
        hb0 <= (x >= 10'd640);                     // pix is the dot of the position one clock back, hb0 and vb0 describe the same position
        vb0 <= (y >= 10'd400);
        hblank <= hb0;
        vblank <= vb0;
        rgb565 <= (pix && !hb0 && !vb0 && !blank) ? on_rgb : 16'h0000;
    end
endmodule
