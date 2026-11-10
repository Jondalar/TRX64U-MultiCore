// T175: the register block of the Port Diagnostics core at the application's core window (cw_*, cpu_clk, the bytes at 0x101A0000), laid out like null_regs.v / nes_regs.v.
// Written for this project (TREX). Byte offsets in page 0; the window reads 0 and acknowledges everything not listed.
//   000-003 ID        "DIAG"
//   004 CAPS 0xC0     b6 page 6 (the host's pixel layer), b7 the pixel layer is RGB565 (the host's meaning; the diag bits are DCAPS)
//   005 CAPS2 0x02    b1 BUILD is there
//   006 DCAPS         b0 the joystick pins are open-drain outputs in the top, b1 PADDLE_DIS / PADDLE_ADJ are driven by the engine
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   00C-00F TICKS_US  R  engine clocks per microsecond x 256 (cpu_clk 100 MHz: 0x00006400)
//   010 IRQ_EN RW, 014 IRQ_CAUSE R/W1C   b7 page flip done, b6 video frame start, b5 the engine stopped or waits for the user
//   011 STATUS        R  b0 running, b1 waiting for GO, b2 done, b3 result FIFO overflow (sticky until CLEAR), b4 step error
//   012 CTRL          W  b0 RUN, b1 ABORT, b2 GO, b3 CLEAR FIFO   (each bit is a one-clock pulse; RUN is taken when the engine is idle)
//   016 PC            R  the engine's step index
//   020-023 STEP_DATA W  a step word, little endian; the write of 023 stores it at STEP_ADDR and increments STEP_ADDR
//   024 STEP_ADDR     RW 8 bit index of the next step to store
//   030-033 FIFO_DATA R  head word of the result FIFO, little endian; the read of 033 pops it
//   034-035 FIFO_LEVEL R words in the FIFO (0..512)
//   040-041 LIVE      R  {2'b0, PADDLE_CMP[3:0], KBJOY_2[4:0], KBJOY_1[4:0]} synchronised, high = pin high
//   042-043 DRV       R  {6'b0, drive-low mask[9:0]}: the pins the engine drives low now (bit order of LIVE without the CMP bits)
//   page 6 (0x6000-0x6FFF)  forwarded to the host's pixel layer (px_we, px_re, px_rdata)
`timescale 1ns / 1ps
module diag_regs #(
    parameter [31:0] TICKS_US_X256 = 32'h00006400
) (
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
    input  wire        ev_flip,
    input  wire        ev_eng,
    // to / from the probe engine
    output reg         run, abort, go, fifo_clr,
    output reg         st_we,
    output wire [7:0]  st_addr,
    output wire [31:0] st_wdata,
    input  wire [4:0]  status,                      // {err, ovf, done, waiting, running}
    input  wire [7:0]  pc,
    input  wire [31:0] fifo_q,
    input  wire [9:0]  fifo_level,
    output wire        fifo_rd,
    input  wire [13:0] live,
    input  wire [9:0]  drv_low
);
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    reg [7:0]  saddr = 8'd0;
    reg [31:0] sdata = 32'd0;
    wire        win  = addr[16];
    wire  [3:0] page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    wire  [7:0] ev   = {ev_flip, ev_vsync, ev_eng, 5'b00000};
    wire        r0   = !win && page == 4'h0;
    assign fifo_rd = re && r0 && off == 12'h033;       // pops in the clock of the read, so the next read sees the next word

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ack   = ack_r;
    assign irq   = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;
    assign st_addr  = saddr;
    assign st_wdata = sdata;

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= re && !win && page == 4'h6;
        run <= 1'b0; abort <= 1'b0; go <= 1'b0; fifo_clr <= 1'b0; st_we <= 1'b0;

        if (st_we) saddr <= saddr + 8'd1;      // the clock after the write of 023: st_addr is still the old value in this clock
        if (we && r0) begin
            case (off)
                12'h010: irq_en <= wdata;
                12'h014: cause <= (cause & ~wdata) | ev;
                12'h012: begin run <= wdata[0]; abort <= wdata[1]; go <= wdata[2]; fifo_clr <= wdata[3]; end
                12'h020: sdata[7:0] <= wdata;
                12'h021: sdata[15:8] <= wdata;
                12'h022: sdata[23:16] <= wdata;
                12'h023: begin sdata[31:24] <= wdata; st_we <= 1'b1; end    // the RAM write is in the next clock; saddr moves on then
                12'h024: saddr <= wdata;
                default: ;
            endcase
        end

        q <= 8'h00;
        if (r0) begin
            case (off)
                12'h000: q <= 8'h44;                // "D"
                12'h001: q <= 8'h49;                // "I"
                12'h002: q <= 8'h41;                // "A"
                12'h003: q <= 8'h47;                // "G"
                12'h004: q <= 8'hC0;
                12'h005: q <= 8'h02;
                12'h006: q <= 8'h03;
                12'h008: q <= build[7:0];
                12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16];
                12'h00B: q <= build[31:24];
                12'h00C: q <= TICKS_US_X256[7:0];
                12'h00D: q <= TICKS_US_X256[15:8];
                12'h00E: q <= TICKS_US_X256[23:16];
                12'h00F: q <= TICKS_US_X256[31:24];
                12'h010: q <= irq_en;
                12'h011: q <= {3'b000, status};
                12'h014: q <= cause;
                12'h016: q <= pc;
                12'h024: q <= saddr;
                12'h030: q <= fifo_q[7:0];
                12'h031: q <= fifo_q[15:8];
                12'h032: q <= fifo_q[23:16];
                12'h033: q <= fifo_q[31:24];
                12'h034: q <= fifo_level[7:0];
                12'h035: q <= {6'd0, fifo_level[9:8]};
                12'h040: q <= live[7:0];
                12'h041: q <= {2'b00, live[13:8]};
                12'h042: q <= drv_low[7:0];
                12'h043: q <= {6'd0, drv_low[9:8]};
                default: ;
            endcase
        end
    end
endmodule
