// T92 D5/D9/D10: the host's real-time DDR2 port c_* (ui_clk) for two clients that each run one transaction at a time: the tape engine
// (zxcpc_tape: a read of one 128-bit line) and the FDC's sector service (cpc_fdc: reads and writes of lines). A client says it is busy from
// the clock its transaction starts to the clock it is done (for a read: when the data has come back); the port is given to the first
// that is busy and stays with it until it is not. Read data goes to the client that has the port. Written for this project (TREX).
`timescale 1ns / 1ps
module cpc_ddr_mux (
    input  wire         ui_clk,
    input  wire         ui_rst,
    // client A (tape, reads only)
    input  wire         a_busy,
    input  wire [26:0]  a_addr,
    input  wire [2:0]   a_cmd,
    input  wire         a_en,
    // client B (FDC)
    input  wire         b_busy,
    input  wire [26:0]  b_addr,
    input  wire [2:0]   b_cmd,
    input  wire         b_en,
    input  wire [127:0] b_wdf_data,
    input  wire         b_wdf_wren,
    // the host
    output wire [26:0]  c_addr,
    output wire [2:0]   c_cmd,
    output wire         c_en,
    output wire [127:0] c_wdf_data,
    output wire [15:0]  c_wdf_mask,
    output wire         c_wdf_wren,
    input  wire         c_rdy,
    input  wire         c_rd_valid,
    output wire         a_rdy,                       // the host's ready / read data valid, only for the client that has the port
    output wire         a_valid,
    output wire         b_rdy,
    output wire         b_valid
);
    reg [1:0] g = 2'd0;                              // 0 free, 1 A, 2 B
    always @(posedge ui_clk) begin
        if (ui_rst) g <= 2'd0;
        else case (g)
            2'd0: if (a_busy) g <= 2'd1; else if (b_busy) g <= 2'd2;
            2'd1: if (!a_busy) g <= 2'd0;
            2'd2: if (!b_busy) g <= 2'd0;
            default: g <= 2'd0;
        endcase
    end
    wire ga = (g == 2'd1) | ((g == 2'd0) & a_busy);
    wire gb = (g == 2'd2) | ((g == 2'd0) & ~a_busy & b_busy);
    assign c_addr = ga ? a_addr : b_addr;
    assign c_cmd = ga ? a_cmd : b_cmd;
    assign c_en = (ga & a_en) | (gb & b_en);
    assign c_wdf_data = b_wdf_data;
    assign c_wdf_mask = 16'h0000;
    assign c_wdf_wren = gb & b_wdf_wren;
    assign a_rdy = ga & c_rdy;
    assign a_valid = ga & c_rd_valid;
    assign b_rdy = gb & c_rdy;
    assign b_valid = gb & c_rd_valid;
endmodule
