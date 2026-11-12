// T123: the register block of the null core, the part of the TRX IO block (trx64/trx_io.v, the bytes at 0x101A0000 in the app's
// window cw_*) that the host boot app reads. Written for this project (TREX); the layout is trx_io.v's and nes_regs.v's.
//   000-003 ID        "TRX2" (the boot app takes the block for ours only with it)
//   004 CAPS 0xC0     b6 page 6 (the host's pixel layer), b7 the pixel layer is RGB565
//   005 CAPS2 0x02    b1 BUILD is there
//   008-00B BUILD     the bitstream's build stamp (usr_access: the host key the bitstream carries in AXSS)
//   010 IRQ_EN RW, 014 IRQ_CAUSE R/W1C   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync)
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer: px_we / px_re at once, its read data back (px_rdata) with the ack
//   everything else, the EEPROM window (addr[16]) included, reads 0 and is acknowledged, so no access runs into the host's bus timeout.
// The block runs in cpu_clk, the clock of the window and of the pixel layer's events; nothing crosses a clock here.
`timescale 1ns / 1ps
module null_regs (
    input  wire        clk,                         // cpu_clk
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,
    input  wire [7:0]  wdata,
    output wire [7:0]  rdata,
    output wire        ack,
    output wire        irq,
    input  wire [31:0] build,
    output wire        px_we,
    output wire        px_re,
    input  wire [7:0]  px_rdata,
    input  wire        ev_vsync,
    input  wire        ev_flip
);
    reg [7:0] irq_en = 8'd0, cause = 8'd0;
    reg [7:0] q = 8'd0;
    reg       q_p6 = 1'b0, ack_r = 1'b0;
    wire        win  = addr[16];
    wire  [3:0] page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    wire  [7:0] ev   = {ev_flip, ev_vsync, 6'b000000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ack   = ack_r;
    assign irq   = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;                 // one clock after the address, as trx_io.v does for its pages in this clock
        q_p6 <= re && !win && page == 4'h6;

        if (we && !win && page == 4'h0) begin
            if (off == 12'h010) irq_en <= wdata;
            if (off == 12'h014) cause <= (cause & ~wdata) | ev;
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                12'h000: q <= 8'h54;                // ID0 "T"
                12'h001: q <= 8'h52;                // "R"
                12'h002: q <= 8'h58;                // "X"
                12'h003: q <= 8'h32;                // "2"
                12'h004: q <= 8'hC0;                // CAPS
                12'h005: q <= 8'h02;                // CAPS2
                12'h008: q <= build[7:0];
                12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16];
                12'h00B: q <= build[31:24];
                12'h010: q <= irq_en;
                12'h014: q <= cause;
                default: ;
            endcase
        end
    end
endmodule
