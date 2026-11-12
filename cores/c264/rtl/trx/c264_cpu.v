// T90: the 8501 as the C16Nano builds it: a T65 (6502, Wallner et al., cores/c264/rtl/src/t65) with the 6510 style port at
// $0000/$0001 and the address bus released when AEC is low. The behaviour is that of mos8501.v of C16Nano (Hegedus, GPL-3.0-or-later);
// this file is our own text of it so that the port logic can be read in one place. `enable` is the only clock gate: with the
// TED's CPU enable and the core's TED clock enable behind it, the CPU stands still exactly when the TED does (the pause).
`timescale 1ns / 1ps
module c264_cpu (
    input  wire        clk,
    input  wire        reset,
    input  wire        enable,
    input  wire        irq_n,
    input  wire [7:0]  data_in,
    output wire [7:0]  data_out,
    output wire [15:0] address,
    output wire        rw,
    input  wire [7:0]  port_in,
    output wire [7:0]  port_out,
    input  wire        rdy,
    input  wire        aec
);
    wire        we;
    wire [15:0] core_address;
    wire [7:0]  core_data_out;
    wire [7:0]  dummy;
    reg  [7:0]  port_dir = 8'd0;
    reg  [7:0]  port_data = 8'd0;
    wire        port_access = ~|core_address[15:1];

    T65 cpu (
        .Mode(2'b00),
        .BCD_en(1'b1),
        .Res_n(~reset),
        .Enable(enable),
        .Clk(clk),
        .Rdy(rdy),
        .Abort_n(1'b1),
        .IRQ_n(irq_n),
        .NMI_n(1'b1),
        .SO_n(1'b1),
        .R_W_n(we),
        .A({dummy, core_address}),
        .DI(~we ? core_data_out : ~port_access ? data_in : address[0] ? (port_dir & port_data) | (~port_dir & port_in) : port_dir),
        .DO(core_data_out)
    );

    assign address  = aec ? core_address : 16'hFFFF;     // the tri-state bus, emulated for the wired-AND
    assign port_out = port_data;
    assign rw       = ~aec | we | port_access;
    assign data_out = rw ? 8'hFF : core_data_out;        // a write drives the data bus

    always @(posedge clk) begin
        if (reset) begin
            port_dir <= 8'd0;
            port_data <= 8'd0;
        end else if (enable) begin
            if (port_access & ~we) begin
                if (core_address[0] == 1'b0) port_dir <= core_data_out;
                else port_data <= core_data_out;
            end
        end
    end
endmodule
