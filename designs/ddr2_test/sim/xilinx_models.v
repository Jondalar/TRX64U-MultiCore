// Behavioural models of the Xilinx 7-series primitives ddr2_lean uses, for iverilog.
// Not the vendor models: they implement what ddr2_lean assumes of the silicon (and
// nothing else), so a bench passing here says the RTL is consistent with these
// assumptions, not that the assumptions hold. The assumptions:
//   OSERDESE2 (DDR, 8): D1..D8 are taken at the CLKDIV edge, D1 is on OQ from that edge, one
//     bit per CLK half period (SLOT). T1 (BUF) follows at the same edge.
//   ISERDESE2 (DDR, 8): DDLY is sampled on both edges of CLK; at a CLKDIV edge Q1..Q8 hold
//     the eight samples before it, Q8 the oldest. A BITSLIP pulse moves the window one
//     sample back (the real primitive alternates +1/-3, all eight positions are reached
//     after eight pulses either way).
//   IDELAYE2 (VAR_LOAD): CNTVALUEIN is taken at C when LD is high; the delay is 78 ps per tap.
//   PLLE2_BASE: free-running clocks at VCO/CLKOUTn_DIVIDE, all rising together at time 0
//     (1667 ps slots: 3334 ps CLK300, 13336 ps ui_clk, both exact multiples of the slot).
`timescale 1ps/1ps
// -DTQ_DLY=<ps> / -DOQ_DLY=<ps> move the OSERDES tri-state / data output later (what if T and OQ do not have
// the same latency in the silicon).
`ifndef TQ_DLY
`define TQ_DLY 0
`endif
`ifndef OQ_DLY
`define OQ_DLY 0
`endif

module PLLE2_BASE #(
    parameter BANDWIDTH = "OPTIMIZED", parameter STARTUP_WAIT = "FALSE",
    parameter real REF_JITTER1 = 0.01, parameter real CLKIN1_PERIOD = 5.0,
    parameter integer DIVCLK_DIVIDE = 1, parameter integer CLKFBOUT_MULT = 5,
    parameter real CLKFBOUT_PHASE = 0.0,
    parameter integer CLKOUT0_DIVIDE = 1, parameter real CLKOUT0_PHASE = 0.0, parameter real CLKOUT0_DUTY_CYCLE = 0.5,
    parameter integer CLKOUT1_DIVIDE = 1, parameter real CLKOUT1_PHASE = 0.0, parameter real CLKOUT1_DUTY_CYCLE = 0.5,
    parameter integer CLKOUT2_DIVIDE = 1, parameter real CLKOUT2_PHASE = 0.0, parameter real CLKOUT2_DUTY_CYCLE = 0.5,
    parameter integer CLKOUT3_DIVIDE = 1, parameter real CLKOUT3_PHASE = 0.0, parameter real CLKOUT3_DUTY_CYCLE = 0.5,
    parameter integer CLKOUT4_DIVIDE = 1, parameter real CLKOUT4_PHASE = 0.0, parameter real CLKOUT4_DUTY_CYCLE = 0.5,
    parameter integer CLKOUT5_DIVIDE = 1, parameter real CLKOUT5_PHASE = 0.0, parameter real CLKOUT5_DUTY_CYCLE = 0.5
) (
    input  wire CLKIN1, CLKFBIN, PWRDWN, RST,
    output wire CLKFBOUT, CLKOUT0, CLKOUT1, CLKOUT2, CLKOUT3, CLKOUT4, CLKOUT5,
    output reg  LOCKED
);
    // a slot is half a VCO period, rounded to a whole ps; the outputs are whole multiples
    localparam real VCO_PS = CLKIN1_PERIOD * 1000.0 * DIVCLK_DIVIDE / CLKFBOUT_MULT;
    localparam integer SLOT = $rtoi(VCO_PS * 2.0 + 0.5);
    reg c0 = 1'b0, c1 = 1'b0, c2 = 1'b0, c3 = 1'b0, c4 = 1'b0, c5 = 1'b0, fb = 1'b0;
    // half period of an output = DIVIDE VCO half periods = DIVIDE * SLOT / 2 (SLOT = 1667 for 1200 MHz, 833.5 ps VCO)
    localparam integer H0 = CLKOUT0_DIVIDE * SLOT / 4, H1 = CLKOUT1_DIVIDE * SLOT / 4,
                       H2 = CLKOUT2_DIVIDE * SLOT / 4, H3 = CLKOUT3_DIVIDE * SLOT / 4,
                       H4 = CLKOUT4_DIVIDE * SLOT / 4, H5 = CLKOUT5_DIVIDE * SLOT / 4;
    always #(H0) c0 = ~c0;
    always #(H1) c1 = ~c1;
    always #(H2) c2 = ~c2;
    always #(H3) c3 = ~c3;
    always #(H4) c4 = ~c4;
    always #(H5) c5 = ~c5;
    always #(SLOT) fb = ~fb;
    assign CLKOUT0 = LOCKED ? c0 : 1'b0;
    assign CLKOUT1 = LOCKED ? c1 : 1'b0;
    assign CLKOUT2 = LOCKED ? c2 : 1'b0;
    assign CLKOUT3 = LOCKED ? c3 : 1'b0;
    assign CLKOUT4 = LOCKED ? c4 : 1'b0;
    assign CLKOUT5 = LOCKED ? c5 : 1'b0;
    assign CLKFBOUT = fb;
    initial LOCKED = 1'b0;
    always @(RST) begin
        if (RST) LOCKED <= 1'b0;
        else LOCKED <= #2000000 1'b1;           // 2 us
    end
endmodule

module MMCME2_BASE #(
    parameter real CLKIN1_PERIOD = 20.0, parameter integer DIVCLK_DIVIDE = 1,
    parameter real CLKFBOUT_MULT_F = 20.0, parameter real CLKOUT0_DIVIDE_F = 5.0
) (
    input  wire CLKIN1, CLKFBIN, PWRDWN, RST,
    output wire CLKFBOUT, CLKOUT0, CLKOUT0B, CLKOUT1, CLKOUT1B, CLKOUT2, CLKOUT2B, CLKOUT3,
                CLKOUT3B, CLKOUT4, CLKOUT5, CLKOUT6, CLKFBOUTB,
    output reg  LOCKED
);
    localparam integer HALF = $rtoi(CLKIN1_PERIOD * 1000.0 * DIVCLK_DIVIDE / CLKFBOUT_MULT_F * CLKOUT0_DIVIDE_F / 2.0);
    reg c0 = 1'b0;
    always #(HALF) c0 = ~c0;
    assign CLKOUT0 = LOCKED ? c0 : 1'b0;
    assign CLKFBOUT = 1'b0;
    assign {CLKOUT0B, CLKOUT1, CLKOUT1B, CLKOUT2, CLKOUT2B, CLKOUT3, CLKOUT3B, CLKOUT4, CLKOUT5, CLKOUT6, CLKFBOUTB} = 11'd0;
    initial LOCKED = 1'b0;
    always @(RST) begin
        if (RST) LOCKED <= 1'b0;
        else LOCKED <= #1000000 1'b1;
    end
endmodule

module BUFG (input wire I, output wire O); assign O = I; endmodule
module IBUF (input wire I, output wire O); assign O = I; endmodule
module OBUF (input wire I, output wire O); assign O = I; endmodule
module IOBUF (inout wire IO, input wire I, input wire T, output wire O);
    assign IO = T ? 1'bz : I;
    assign O = IO;
endmodule
module IOBUFDS (inout wire IO, inout wire IOB, input wire I, input wire T, output wire O);
    assign IO  = T ? 1'bz : I;
    assign IOB = T ? 1'bz : ~I;
    assign O   = (IO === 1'b1 && IOB !== 1'b1) ? 1'b1 : 1'b0;
endmodule

module BSCANE2 #(parameter integer JTAG_CHAIN = 1) (
    output wire CAPTURE, DRCK, RESET, RUNTEST, SEL, SHIFT, TCK, TDI, UPDATE, TMS, input wire TDO
);
    assign {CAPTURE, DRCK, RESET, RUNTEST, SEL, SHIFT, TCK, TDI, UPDATE, TMS} = 10'd0;
endmodule

module IDELAYCTRL (output reg RDY, input wire REFCLK, input wire RST);
    integer n = 0;
    initial RDY = 1'b0;
    always @(posedge REFCLK) begin
        if (RST) begin n <= 0; RDY <= 1'b0; end
        else begin
            if (n < 8) n <= n + 1;
            else RDY <= 1'b1;
        end
    end
endmodule

module IDELAYE2 #(
    parameter CINVCTRL_SEL = "FALSE", parameter DELAY_SRC = "IDATAIN",
    parameter HIGH_PERFORMANCE_MODE = "TRUE", parameter IDELAY_TYPE = "FIXED",
    parameter integer IDELAY_VALUE = 0, parameter PIPE_SEL = "FALSE",
    parameter real REFCLK_FREQUENCY = 200.0, parameter SIGNAL_PATTERN = "DATA"
) (
    input  wire       C, CE, CINVCTRL, DATAIN, IDATAIN, INC, LD, LDPIPEEN, REGRST,
    input  wire [4:0] CNTVALUEIN,
    output reg        DATAOUT,
    output wire [4:0] CNTVALUEOUT
);
    reg [4:0] tap;
    initial begin tap = IDELAY_VALUE; DATAOUT = 1'b0; end
    assign CNTVALUEOUT = tap;
    always @(posedge C) if (LD) tap <= CNTVALUEIN;
    always @(IDATAIN) DATAOUT <= #(tap * 78) IDATAIN;
endmodule

module ISERDESE2 #(
    parameter DATA_RATE = "DDR", parameter integer DATA_WIDTH = 8, parameter INTERFACE_TYPE = "NETWORKING",
    parameter IOBDELAY = "IFD", parameter integer NUM_CE = 1, parameter OFB_USED = "FALSE",
    parameter SERDES_MODE = "MASTER", parameter DYN_CLKDIV_INV_EN = "FALSE", parameter DYN_CLK_INV_EN = "FALSE",
    parameter INIT_Q1 = 1'b0, INIT_Q2 = 1'b0, INIT_Q3 = 1'b0, INIT_Q4 = 1'b0,
    parameter SRVAL_Q1 = 1'b0, SRVAL_Q2 = 1'b0, SRVAL_Q3 = 1'b0, SRVAL_Q4 = 1'b0
) (
    input  wire D, DDLY, CE1, CE2, CLK, CLKB, CLKDIV, CLKDIVP, OCLK, OCLKB, OFB, BITSLIP, RST,
               DYNCLKDIVSEL, DYNCLKSEL, SHIFTIN1, SHIFTIN2,
    output wire SHIFTOUT1, SHIFTOUT2, O,
    output reg  Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8
);
    reg [15:0] hist = 16'd0;                    // hist[0] newest sample
    reg [2:0]  k = 3'd0;
    function samp(input v);
        samp = (v === 1'b1) ? 1'b1 : (v === 1'b0) ? 1'b0 : ($random & 1);
    endfunction
    always @(posedge CLK or negedge CLK) hist <= {hist[14:0], samp(DDLY)};
    wire [7:0] win = hist >> k;
    always @(posedge CLKDIV or posedge RST) begin
        if (RST) begin
            k <= 3'd0; {Q8, Q7, Q6, Q5, Q4, Q3, Q2, Q1} <= 8'd0;
        end else begin
            if (BITSLIP) k <= k + 3'd1;
            {Q8, Q7, Q6, Q5, Q4, Q3, Q2, Q1} <= win;
        end
    end
    assign SHIFTOUT1 = 1'b0, SHIFTOUT2 = 1'b0, O = 1'b0;
endmodule

module OSERDESE2 #(
    parameter DATA_RATE_OQ = "DDR", parameter DATA_RATE_TQ = "BUF", parameter integer DATA_WIDTH = 8,
    parameter integer TRISTATE_WIDTH = 1, parameter SERDES_MODE = "MASTER",
    parameter INIT_OQ = 1'b0, parameter INIT_TQ = 1'b1, parameter SRVAL_OQ = 1'b0, parameter SRVAL_TQ = 1'b1,
    parameter TBYTE_CTL = "FALSE", parameter TBYTE_SRC = "FALSE"
) (
    output reg  OQ, TQ,
    output wire OFB, TFB, SHIFTOUT1, SHIFTOUT2, TBYTEOUT,
    input  wire CLK, CLKDIV, D1, D2, D3, D4, D5, D6, D7, D8, T1, T2, T3, T4, OCE, TCE, RST,
               SHIFTIN1, SHIFTIN2, TBYTEIN
);
    localparam integer SLOT = 1667;
    reg [7:0] d;
    initial begin OQ = INIT_OQ; TQ = INIT_TQ; end
    assign OFB = 1'b0, TFB = 1'b0, SHIFTOUT1 = 1'b0, SHIFTOUT2 = 1'b0, TBYTEOUT = 1'b0;
    always @(posedge CLKDIV or posedge RST) begin
        if (RST) begin
            OQ <= SRVAL_OQ; TQ <= SRVAL_TQ;
        end else begin
            d = {D8, D7, D6, D5, D4, D3, D2, D1};
            OQ <= #(`OQ_DLY) d[0];
            OQ <= #(`OQ_DLY + 1 * SLOT) d[1];
            OQ <= #(`OQ_DLY + 2 * SLOT) d[2];
            OQ <= #(`OQ_DLY + 3 * SLOT) d[3];
            OQ <= #(`OQ_DLY + 4 * SLOT) d[4];
            OQ <= #(`OQ_DLY + 5 * SLOT) d[5];
            OQ <= #(`OQ_DLY + 6 * SLOT) d[6];
            OQ <= #(`OQ_DLY + 7 * SLOT) d[7];
            TQ <= #(`TQ_DLY) T1;
        end
    end
endmodule
