// T164 G4 (ADR-0060): the TMDS bit repeater between the encoder's 10-bit symbols and the GTP words. The GTP keeps its line rate (10 line bits per
// pix_clk, two pix_clk per 20-bit word on usrclk = pix_clk / 2); a pixel lasts `gear` pix_clk = 10 * gear line bits, and every bit of its symbol is
// sent `gear` times: in clock t (0..gear-1) of the pixel the line bit k (0..9, sent first = 0) is symbol bit (10 t + k) / gear. The wire then carries
// one symbol per pixel clock (AVI PR = 0). The clock lane takes the same path with the symbol 10'b00000_11111 (bits 4:0 = 1 are sent first), so its
// period is the pixel's 10 * gear line bits.
//
// Contract with the pixel chain: `ce` is high in the LAST pix_clk of a pixel (hdmi_timing.v); the three symbols are registers that change on the
// edge ending that clock, so in the first clock of a pixel (ph = 0) they already hold the new pixel. ph counts the pix_clk since then. gear is
// 1, 2, 3 or 5 and changes only while rst is high (a gear that is none of them sends zeros). Lane polarity (the swapped lane of the board) stays
// in the caller. The first bit of a pixel reaches a word 2 (or 3) pix_clk after its first clock, depending on which of the two clocks of a word it falls in.
module tmds_rep (
    input  wire        clk,                 // pix_clk
    input  wire        usrclk,              // clk / 2, from the same edge
    input  wire        rst,
    input  wire        ce,
    input  wire [3:0]  gear,
    input  wire [9:0]  sym0, sym1, sym2,    // the encoder's TMDS channels 0, 1, 2
    output reg  [19:0] w0, w1, w2,          // GTP words of lanes carrying channel 0, 1, 2
    output reg  [19:0] wc                   // the clock lane
);
    localparam [9:0] CLK_SYM = 10'b00000_11111;

    reg [2:0] ph = 3'd0;
    always @(posedge clk)
        if (rst || ce) ph <= 3'd0;
        else if (ph != 3'd7) ph <= ph + 3'd1;

    // out[k] = sym[(10 t + k) / gear] where gear == g and ph == t; the index is a constant of each (g, t, k) term
    function [39:0] rep(input [9:0] s0, input [9:0] s1, input [9:0] s2, input [9:0] sc);
        integer g, t, k;
        reg [39:0] r;
        begin
            r = 40'd0;
            for (g = 1; g <= 5; g = g + 1)
                for (t = 0; t < g; t = t + 1) begin
                    if (gear == g && ph == t) begin
                        for (k = 0; k < 10; k = k + 1) begin
                            r[k]      = s0[(10 * t + k) / g];
                            r[10 + k] = s1[(10 * t + k) / g];
                            r[20 + k] = s2[(10 * t + k) / g];
                            r[30 + k] = sc[(10 * t + k) / g];
                        end
                    end
                end
            rep = (gear == 4'd4) ? 40'd0 : r;
        end
    endfunction

    reg [39:0] o, o_prev;
    always @(posedge clk) begin
        o      <= rep(sym0, sym1, sym2, CLK_SYM);
        o_prev <= o;
    end
    always @(posedge usrclk) begin      // the older clock in the low half, sent first
        w0 <= {o[9:0],   o_prev[9:0]};
        w1 <= {o[19:10], o_prev[19:10]};
        w2 <= {o[29:20], o_prev[29:20]};
        wc <= {o[39:30], o_prev[39:30]};
    end
endmodule
