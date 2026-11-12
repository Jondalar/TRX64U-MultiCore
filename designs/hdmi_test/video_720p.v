// 1280x720 at 60 Hz (CEA-861 VIC 4, 74.25 MHz), two pixels per clock.
// Colour bars with a white frame and a square that bounces, so a frozen
// picture and a running one can be told apart.
module video_720p (
    input  wire        clk,             // 37.125 MHz: pixel clock / 2
    input  wire        rst,
    output reg  [23:0] rgb0, rgb1,      // pixel 2n and 2n+1
    output reg         de, hs, vs
);
    localparam H_ACT = 1280, H_FP = 110, H_SYNC = 40, H_BP = 220, H_TOT = 1650;
    localparam V_ACT = 720,  V_FP = 5,   V_SYNC = 5,  V_BP = 20,  V_TOT = 750;

    reg [10:0] hc;          // even pixel index
    reg [9:0]  vc;
    reg [10:0] bx; reg [9:0] by; reg dx, dy;

    function [23:0] colour;
        input [10:0] x; input [9:0] y;
        begin
            if (x < 4 || x >= H_ACT - 4 || y < 4 || y >= V_ACT - 4)
                colour = 24'hFFFFFF;
            else if (x >= bx && x < bx + 96 && y >= by && y < by + 96)
                colour = 24'hFFFFFF;
            else case (x[10:7])                            // 10 bars of 128 px
                4'd0: colour = 24'hC0C0C0;
                4'd1: colour = 24'hC0C000;
                4'd2: colour = 24'h00C0C0;
                4'd3: colour = 24'h00C000;
                4'd4: colour = 24'hC000C0;
                4'd5: colour = 24'hC00000;
                4'd6: colour = 24'h0000C0;
                4'd7: colour = 24'h202020;
                4'd8: colour = 24'h6C5EB5;             // C64 light blue
                default: colour = 24'h352879;          // C64 blue
            endcase
        end
    endfunction

    always @(posedge clk) begin
        if (rst) begin
            hc <= 0; vc <= 0; bx <= 100; by <= 100; dx <= 1; dy <= 1;
            de <= 0; hs <= 0; vs <= 0; rgb0 <= 0; rgb1 <= 0;
        end else begin
            if (hc == H_TOT - 2) begin
                hc <= 0;
                if (vc == V_TOT - 1) begin
                    vc <= 0;
                    bx <= dx ? bx + 4 : bx - 4;
                    by <= dy ? by + 3 : by - 3;
                    if (dx && bx >= H_ACT - 96 - 8) dx <= 0;
                    if (!dx && bx <= 8) dx <= 1;
                    if (dy && by >= V_ACT - 96 - 8) dy <= 0;
                    if (!dy && by <= 8) dy <= 1;
                end else
                    vc <= vc + 1;
            end else
                hc <= hc + 2;
            de   <= (hc < H_ACT) && (vc < V_ACT);
            hs   <= (hc >= H_ACT + H_FP) && (hc < H_ACT + H_FP + H_SYNC);    // positive
            vs   <= (vc >= V_ACT + V_FP) && (vc < V_ACT + V_FP + V_SYNC);    // positive
            rgb0 <= colour(hc, vc);
            rgb1 <= colour(hc + 1, vc);
        end
    end
endmodule
