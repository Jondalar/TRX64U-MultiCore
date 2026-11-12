// T194 / T140 section 18: which chip is being used. Per VIC frame, how often the CPU wrote to the VIC's registers ($D000-$D02E), to the colour RAM
// ($D800-$DBFF) and to the VDC's data register ($D601), the VIC's display enable (DEN, $D011 b4) at the end of the frame, and a frame number, so that the
// application can tell which of the two pictures a program is driving (c128_regs.v 0D0-0D4). The VDC cannot be measured by its picture: its hardware cursor
// blinks by itself; the CPU's writes to it are what a program in 80 columns makes, and the VIC's registers and colour RAM what a program in 40 columns makes.
// Counted in sys_clk on the rising edge of each strobe (a CPU access holds its chip select for several clocks), 8 bit, saturating at 255; the counts of the frame
// that has just ended are latched when the VIC's raster line wraps to 0 and the counters restart. While the machine is held (Freeze, Pause) nothing moves.
module c128_act (
    input  wire        clk,
    input  wire        rst,
    input  wire        vic_wr,          // a CPU write to a VIC register (level while the access lasts)
    input  wire        cram_wr,         // a CPU write to the colour RAM
    input  wire        vdc_wr,          // a CPU write to the VDC's data register
    input  wire        den,             // $D011 b4
    input  wire [8:0]  line,            // the VIC's raster line (the CPU-side frame: 50/60 Hz); the wrap to line 0 ends the frame
    output wire [39:0] q                // {seq, flags (b0 DEN), vdc, cram, vic}
);
    reg  [2:0] s_q = 3'd0;              // the strobes of the clock before
    reg  [8:0] ln_q = 9'd0;
    reg  [7:0] c_vic = 8'd0, c_cram = 8'd0, c_vdc = 8'd0;
    reg  [7:0] l_vic = 8'd0, l_cram = 8'd0, l_vdc = 8'd0, l_seq = 8'd0;
    reg        l_den = 1'b0;
    wire       e_vic  = vic_wr  && !s_q[0];
    wire       e_cram = cram_wr && !s_q[1];
    wire       e_vdc  = vdc_wr  && !s_q[2];
    wire       e_fr   = (line == 9'd0) && (ln_q != 9'd0);
    always @(posedge clk) begin
        s_q  <= {vdc_wr, cram_wr, vic_wr};
        ln_q <= line;
        if (rst) begin
            c_vic <= 8'd0; c_cram <= 8'd0; c_vdc <= 8'd0;
            l_vic <= 8'd0; l_cram <= 8'd0; l_vdc <= 8'd0; l_seq <= 8'd0; l_den <= 1'b0;
        end else if (e_fr) begin
            l_vic <= c_vic; l_cram <= c_cram; l_vdc <= c_vdc; l_den <= den; l_seq <= l_seq + 8'd1;
            c_vic <= {7'd0, e_vic}; c_cram <= {7'd0, e_cram}; c_vdc <= {7'd0, e_vdc};
        end else begin
            if (e_vic  && c_vic  != 8'hFF) c_vic  <= c_vic  + 8'd1;
            if (e_cram && c_cram != 8'hFF) c_cram <= c_cram + 8'd1;
            if (e_vdc  && c_vdc  != 8'hFF) c_vdc  <= c_vdc  + 8'd1;
        end
    end
    assign q = {l_seq, 7'd0, l_den, l_vdc, l_cram, l_vic};
endmodule
