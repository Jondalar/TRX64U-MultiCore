// T85: the general DDR2 port e_* (ui_clk) of the Amiga core, shared by the frame writer (client A, writes only)
// and the sector service (client B, reads and writes). One command at a time: the arbiter of the host serves
// the e_ port after the real-time port c_*. Written for this project.
// e_* is a MIG-style command port: a command is taken in the clock where e_en and e_rdy are both high; a write
// carries its data with the command; reads come back in order with e_rd_valid, the data on the shared read bus.
`timescale 1ns / 1ps
module amiga_eport (
    input  wire         ui_clk,
    input  wire         ui_rst,
    // client A: frame writer
    input  wire         a_valid,
    input  wire [26:0]  a_addr,
    input  wire [127:0] a_data,
    output reg          a_ready = 1'b0,         // one clock: the command was taken
    // client B: sector service
    input  wire         b_valid,
    input  wire         b_we,
    input  wire [26:0]  b_addr,
    input  wire [127:0] b_data,
    output reg          b_ready = 1'b0,         // one clock: a write was taken / a read's data is in b_rdata
    output reg  [127:0] b_rdata = 128'd0,
    // the host
    output reg  [26:0]  e_addr = 27'd0,
    output reg  [2:0]   e_cmd = 3'd0,
    output reg          e_en = 1'b0,
    input  wire         e_rdy,
    output reg  [127:0] e_wdf_data = 128'd0,
    output wire [15:0]  e_wdf_mask,
    output wire         e_wdf_wren,
    input  wire         e_rd_valid,
    input  wire [127:0] rd_data
);
    assign e_wdf_mask = 16'h0000;
    assign e_wdf_wren = e_en & (e_cmd == 3'b000);
    localparam P_IDLE = 2'd0, P_ISSUE = 2'd1, P_READ = 2'd2, P_GAP = 2'd3;
    reg [1:0] p = P_IDLE;
    reg       who = 1'b0;                       // 0 A, 1 B
    always @(posedge ui_clk) begin
        a_ready <= 1'b0; b_ready <= 1'b0;
        if (ui_rst) begin
            p <= P_IDLE; e_en <= 1'b0;
        end else case (p)
        P_IDLE: begin
            if (a_valid) begin
                who <= 1'b0; e_addr <= a_addr; e_cmd <= 3'b000; e_wdf_data <= a_data; e_en <= 1'b1; p <= P_ISSUE;
            end else if (b_valid) begin
                who <= 1'b1; e_addr <= b_addr; e_cmd <= b_we ? 3'b000 : 3'b001; e_wdf_data <= b_data; e_en <= 1'b1;
                p <= P_ISSUE;
            end
        end
        P_ISSUE: if (e_rdy) begin
            e_en <= 1'b0;
            if (e_cmd == 3'b000) begin
                if (who) b_ready <= 1'b1; else a_ready <= 1'b1;
                p <= P_GAP;
            end else p <= P_READ;
        end
        P_READ: if (e_rd_valid) begin
            b_rdata <= rd_data; b_ready <= 1'b1; p <= P_GAP;
        end
        P_GAP: p <= P_IDLE;                   // the client sees its ready pulse and drops valid
        default: p <= P_IDLE;
        endcase
    end
endmodule
