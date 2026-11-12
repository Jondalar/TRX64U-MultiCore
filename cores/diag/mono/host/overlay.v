// The Ultimate app's menu overlay (U64-II, 0x10140000) over the picture.
//
// CPU side (clk_sys, the app's IO bus through ultimate_sys), offsets from 0x10140000:
//   0x0000-0x000D  registers as Gideon's char_generator_regs (write only, read 0)
//   0x1000-0x1FFF  screen RAM: char code, bit 7 = reverse (read back by the UI)
//   0x2000-0x2FFF  colour RAM: bits 3:0 foreground, 7:4 background (read back)
//   0x5000-0x503F  HDMI palette: entry n at 4n, bytes R, G, B (write only)
//   0x4000 (HDMI timing) and the rest: ignored, read 0.
// Pixel side (clk): drawn as Gideon's char_generator_slave12 draws it (8-pixel
// ROM font or the 12 x 24 font_pkg with big_font, stretch_y, the 9th line, reverse,
// one colour index made transparent), with differences for the host's HDMI modes (T164 WP6):
//   - X_ON / Y_ON are not used: the app sets them for the HDMI mode it thinks it
//     drives. The box is centred on the mode's active area (hact x vact, at xact0 / yact0
//     of the counters) instead, and nothing is drawn outside it (de_in).
//   - Every character pixel is SX x SY output pixels, each 1..4: SY the largest whole factor that keeps
//     the box inside the active height, SX the largest that keeps it inside the width, at most 2 x SY
//     (40 x 25 cells of 12 x 23: 480 x 575 shows as 2 x 1). A 960 x 540 canvas is therefore shown at
//     max(1, floor(vact / 540)) in every mode and a canvas of the mode's own size at 1; a box wider than the
//     active area is cut at its right (S9), never shifted.
//   - Pixel repetition: every register of the pixel side changes only when ce is high (hdmi_timing.v).
// Registers and palette cross into clk as quasi-static values (taken at the
// start of each frame); the RAMs are dual-ported block RAM.
// Video in: the picture's rgb and counters (cx xact0 .. xact0 + hact - 1, cy yact0 .. yact0 + vact - 1 active);
// out: the same, five ce ticks later, with the overlay on top.
module overlay (
    input  wire        clk_sys,
    input  wire        we,
    input  wire [14:0] addr,
    input  wire [7:0]  wdata,
    output reg  [7:0]  rdata,           // one clk_sys after addr

    input  wire        clk,
    input  wire        ce,              // T107: pixel enable (hdmi_timing.v)
    input  wire [11:0] hact,            // the mode's active area and where it starts in cx / cy
    input  wire [10:0] vact,
    input  wire [11:0] xact0,
    input  wire [10:0] yact0,
    input  wire [23:0] rgb_in,
    input  wire        de_in, hs_in, vs_in,
    input  wire [11:0] cx_in,
    input  wire [10:0] cy_in,
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,
    output reg  [10:0] cy,
    output wire        own_keyboard     // clk_sys: menu shown and it takes the keys
);
    // ---- registers (clk_sys), reset values of char_generator_pkg ---------------
    reg [3:0]  r_cw   = 4'd0;
    reg [4:0]  r_ch   = 5'd9;
    reg        r_big  = 1'b0, r_stretch = 1'b0;
    reg [7:0]  r_cpl  = 8'd60;
    reg [5:0]  r_nl   = 6'd30;
    reg [14:0] r_ptr  = 15'd0;
    reg [3:0]  r_tr   = 4'h5;
    reg        r_on   = 1'b0;
    reg        r_own  = 1'b0;
    assign own_keyboard = r_on & r_own;

    (* ram_style = "block" *) reg [7:0] scr [0:4095];
    (* ram_style = "block" *) reg [7:0] col [0:4095];
    (* ram_style = "distributed" *) reg [7:0] pal_r [0:15];
    (* ram_style = "distributed" *) reg [7:0] pal_g [0:15];
    (* ram_style = "distributed" *) reg [7:0] pal_b [0:15];
    integer i;
    initial begin
        for (i = 0; i < 4096; i = i + 1) begin scr[i] = 8'h20; col[i] = 8'h0F; end
        // Colodore, as vic_scaler; the app writes its own at boot
        {pal_r[0],  pal_g[0],  pal_b[0]}  = 24'h000000;
        {pal_r[1],  pal_g[1],  pal_b[1]}  = 24'hFFFFFF;
        {pal_r[2],  pal_g[2],  pal_b[2]}  = 24'h813338;
        {pal_r[3],  pal_g[3],  pal_b[3]}  = 24'h75CEC8;
        {pal_r[4],  pal_g[4],  pal_b[4]}  = 24'h8E3C97;
        {pal_r[5],  pal_g[5],  pal_b[5]}  = 24'h56AC4D;
        {pal_r[6],  pal_g[6],  pal_b[6]}  = 24'h2E2C9B;
        {pal_r[7],  pal_g[7],  pal_b[7]}  = 24'hEDF171;
        {pal_r[8],  pal_g[8],  pal_b[8]}  = 24'h8E5029;
        {pal_r[9],  pal_g[9],  pal_b[9]}  = 24'h553800;
        {pal_r[10], pal_g[10], pal_b[10]} = 24'hC46C71;
        {pal_r[11], pal_g[11], pal_b[11]} = 24'h4A4A4A;
        {pal_r[12], pal_g[12], pal_b[12]} = 24'h7B7B7B;
        {pal_r[13], pal_g[13], pal_b[13]} = 24'hA9FF9F;
        {pal_r[14], pal_g[14], pal_b[14]} = 24'h706DEB;
        {pal_r[15], pal_g[15], pal_b[15]} = 24'hB2B2B2;
    end

    wire [2:0] blk = addr[14:12];
    reg  [1:0] sel_r;
    reg  [7:0] scr_qa, col_qa;
    always @(posedge clk_sys) begin
        if (we && blk == 3'd0 && addr[11:4] == 8'd0)
            case (addr[3:0])
            4'h2: r_cw <= wdata[3:0];
            4'h3: begin r_ch <= wdata[4:0]; r_big <= wdata[6]; r_stretch <= wdata[7]; end
            4'h4: r_cpl <= wdata;
            4'h5: r_nl <= wdata[5:0];
            4'hA: r_ptr[14:8] <= wdata[6:0];
            4'hB: r_ptr[7:0] <= wdata;
            4'hD: begin r_tr <= wdata[3:0]; r_own <= wdata[6]; r_on <= wdata[7]; end
            default: ;                                             // line clocks, X/Y_ON, sync
            endcase
        if (we && blk == 3'd1) scr[addr[11:0]] <= wdata;
        if (we && blk == 3'd2) col[addr[11:0]] <= wdata;
        if (we && blk == 3'd5 && addr[11:6] == 6'd0)
            case (addr[1:0])
            2'd0: pal_r[addr[5:2]] <= wdata;
            2'd1: pal_g[addr[5:2]] <= wdata;
            2'd2: pal_b[addr[5:2]] <= wdata;
            default: ;
            endcase
        scr_qa <= scr[addr[11:0]];
        col_qa <= col[addr[11:0]];
        sel_r  <= (blk == 3'd1) ? 2'd1 : (blk == 3'd2) ? 2'd2 : 2'd0;
    end
    always @(*)
        rdata = (sel_r == 2'd1) ? scr_qa : (sel_r == 2'd2) ? col_qa : 8'h00;

    // ---- frame settings (clk): taken at the top of each frame ------------------
    reg [3:0]  cw;
    reg [4:0]  ch;
    reg        big, stretch, on;
    reg [7:0]  cpl;
    reg [5:0]  nl;
    reg [11:0] ptr;
    reg [3:0]  tr;
    always @(posedge clk) if (ce)
        if (cy_in == 11'd0 && cx_in == 12'd0) begin
            cw <= r_cw; ch <= r_ch; big <= r_big; stretch <= r_stretch; on <= r_on;
            cpl <= r_cpl; nl <= r_nl; ptr <= r_ptr[11:0]; tr <= r_tr;
        end

    // geometry, settled a few clocks after the settings
    reg [4:0]  lpr;                     // output lines per character row (big font skips 3 of 4)
    reg [11:0] w;                       // box in character pixels
    reg [10:0] h;
    reg [2:0]  sx0, sx, sy;             // scale 1..4
    reg [12:0] ws;
    reg [11:0] hs_;
    // the area the box may cover: the whole active area of the mode
    wire [11:0] aw = hact;
    wire [10:0] ah = vact;
    reg [11:0] xs_m1;                   // cx of the first box pixel, minus one
    reg [10:0] y0;                      // first box line (0 = first active line)
    always @(posedge clk) if (ce) begin
        lpr <= big ? ch - {2'b00, ch[4:2]} : ch;
        w   <= cpl * cw;
        h   <= nl * lpr;
        sx0 <= (4 * w <= aw) ? 3'd4 : (3 * w <= aw) ? 3'd3 : (2 * w <= aw) ? 3'd2 : 3'd1;
        sy  <= (4 * h <= ah) ? 3'd4 : (3 * h <= ah) ? 3'd3 : (2 * h <= ah) ? 3'd2 : 3'd1;
        sx  <= (sx0 > {sy, 1'b0}) ? {sy[1:0], 1'b0} : sx0;       // at most 2 x SY (only SY 1 can cut)
        ws  <= w * sx;
        hs_ <= h * sy;
        xs_m1 <= xact0 - 12'd1 + ((ws < hact) ? (hact - ws[11:0]) >> 1 : 12'd0);
        y0    <= (hs_ < vact) ? (vact - hs_[10:0]) >> 1 : 11'd0;
    end

    // ---- per line: which character line is drawn ------------------------------
    reg [11:0] row_ptr;
    reg [4:0]  char_y;
    reg [2:0]  sv;                      // repeat of the current character line
    reg [5:0]  rows_left;
    reg        started, line_on;
    wire [10:0] vy = cy_in - yact0;
    always @(posedge clk) if (ce) begin
        if (cx_in == 12'd16) begin
            if (cy_in == 11'd0) begin
                row_ptr <= ptr; char_y <= 5'd0; sv <= 3'd0; rows_left <= nl;
                started <= 1'b0; line_on <= 1'b0;
            end else if (line_on) begin
                if (sv != sy - 3'd1)
                    sv <= sv + 3'd1;
                else begin
                    sv <= 3'd0;
                    if (char_y == ch - 5'd1) begin
                        char_y    <= 5'd0;
                        row_ptr   <= row_ptr + cpl;
                        rows_left <= rows_left - 6'd1;
                    end else if (big && char_y[1:0] == 2'b10)
                        char_y <= char_y + 5'd2;
                    else
                        char_y <= char_y + 5'd1;
                end
            end
        end
        if (cx_in == 12'd32) begin
            if (cy_in >= yact0 && vy == y0) started <= 1'b1;
            line_on <= on && w != 12'd0 && rows_left != 6'd0 &&
                       (started || (cy_in >= yact0 && vy == y0));
        end
    end

    // ---- per pixel: stage A, counters for the pixel at cx_in -------------------
    reg        run;
    reg [2:0]  sh;
    reg [3:0]  bit_;
    reg [7:0]  char_x;
    always @(posedge clk) if (ce)
        if (cx_in == xs_m1) begin
            run <= line_on; sh <= 3'd0; bit_ <= cw - 4'd1; char_x <= 8'd0;
        end else if (run) begin
            if (sh != sx - 3'd1)
                sh <= sh + 3'd1;
            else begin
                sh <= 3'd0;
                if (bit_ == 4'd0) begin
                    bit_   <= cw - 4'd1;
                    char_x <= char_x + 8'd1;
                    if (char_x == cpl - 8'd1) run <= 1'b0;
                end else
                    bit_ <= bit_ - 4'd1;
            end
        end

    // stage B: screen and colour RAM
    reg [7:0] scr_q, col_q;
    reg       run_b;
    reg [3:0] bit_b;
    always @(posedge clk) if (ce) begin
        scr_q <= scr[row_ptr + char_x];
        col_q <= col[row_ptr + char_x];
        run_b <= run;
        bit_b <= bit_;
    end

    // stage C: fonts
    (* rom_style = "block" *) reg [7:0]  font8  [0:1023];
    (* rom_style = "block" *) reg [35:0] font12 [0:1023];
    initial begin
        $readmemh("../cores/diag/mono/host/mem/ovl_font8.mem", font8);
        $readmemh("../cores/diag/mono/host/mem/ovl_font12.mem", font12);
    end
    wire [2:0] row8 = stretch ? char_y[3:1] : char_y[3] ? 3'd7 : char_y[2:0];
    reg [7:0]  f8;
    reg [35:0] f12;
    reg        run_c, rev_c;
    reg [3:0]  bit_c;
    reg [7:0]  col_c;
    always @(posedge clk) if (ce) begin
        f8    <= font8[{scr_q[6:0], row8}];
        f12   <= font12[{scr_q[6:0], char_y[4:2]}];
        run_c <= run_b;
        rev_c <= scr_q[7];
        bit_c <= bit_b;
        col_c <= col_q;
    end

    // stage D: pixel colour
    wire [11:0] line12 = (char_y[1:0] == 2'd0) ? f12[11:0] :
                         (char_y[1:0] == 2'd1) ? f12[23:12] : f12[35:24];
    // the 9th line is blank unless the 8th is a vertical line (0x18)
    wire [7:0]  line8  = (char_y[3] && !stretch && f8 != 8'h18) ? 8'h00 : f8;
    wire [11:0] glyph  = big ? line12 : {4'h0, line8};
    // the glyph row in a register of its own: font RAM -> row select -> bit
    // select -> colour -> transparency was one 6.5 ns path at 151.6 MHz
    reg  [11:0] glyph_d;
    reg         run_d, rev_d;
    reg  [3:0]  bit_d;
    reg  [7:0]  col_d;
    always @(posedge clk) if (ce) begin
        glyph_d <= glyph;
        run_d   <= run_c;
        rev_d   <= rev_c;
        bit_d   <= bit_c;
        col_d   <= col_c;
    end
    wire        fg     = glyph_d[bit_d] ^ rev_d;
    wire [3:0]  idx    = fg ? col_d[3:0] : col_d[7:4];
    reg  [3:0]  p_idx;
    reg         p_opq, p_dim;
    always @(posedge clk) if (ce) begin
        p_idx <= idx;
        p_opq <= run_d && idx != tr;
        p_dim <= run_d && idx == tr;    // inside the box: the C64 at half brightness
    end

    // ---- blend, video delayed to match --------------------------------------------
    reg [23:0] rgb_d [1:4];
    reg [2:0]  ctl_d [1:4];
    reg [11:0] cx_d  [1:4];
    reg [10:0] cy_d  [1:4];
    always @(posedge clk) if (ce) begin
        rgb_d[1] <= rgb_in;   rgb_d[2] <= rgb_d[1]; rgb_d[3] <= rgb_d[2]; rgb_d[4] <= rgb_d[3];
        ctl_d[1] <= {de_in, hs_in, vs_in}; ctl_d[2] <= ctl_d[1]; ctl_d[3] <= ctl_d[2]; ctl_d[4] <= ctl_d[3];
        cx_d[1]  <= cx_in;    cx_d[2]  <= cx_d[1];  cx_d[3]  <= cx_d[2];  cx_d[4]  <= cx_d[3];
        cy_d[1]  <= cy_in;    cy_d[2]  <= cy_d[1];  cy_d[3]  <= cy_d[2];  cy_d[4]  <= cy_d[3];
        rgb <= !ctl_d[4][2] ? rgb_d[4] :
               p_opq ? {pal_r[p_idx], pal_g[p_idx], pal_b[p_idx]} :
               p_dim ? {1'b0, rgb_d[4][23:17], 1'b0, rgb_d[4][15:9], 1'b0, rgb_d[4][7:1]} :
                       rgb_d[4];
        {de, hs, vs} <= ctl_d[4];
        cx <= cx_d[4];
        cy <= cy_d[4];
    end
endmodule
