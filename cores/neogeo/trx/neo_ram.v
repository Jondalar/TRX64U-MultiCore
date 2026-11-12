// T143: the two RAM modules the Neo Geo chip models instantiate (dpram, spram), written for block RAM. The upstream versions (rtl/mem/dpram.v)
// are Intel altsyncram wrappers and are not built. Same ports and parameters, same behaviour: a synchronous read (the address is registered, the
// data is there the next clock), a write is seen by a read of the same port in the same clock (altsyncram NEW_DATA_NO_NBE_READ = write first),
// two independent clocks. One write per port, each port in its own always block (the template Vivado maps onto one RAMB per 4 KB; a second
// write to an array from the same process turns it into flip-flops, docs/skills fpga-gateware 3).
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module dpram #(
    parameter ADDRWIDTH = 8,
    parameter DATAWIDTH = 8,
    parameter NUMWORDS = 1 << ADDRWIDTH,
    parameter MEM_INIT_FILE = ""
) (
    input  wire                 clock_a,
    input  wire [ADDRWIDTH-1:0] address_a,
    input  wire [DATAWIDTH-1:0] data_a,
    input  wire                 wren_a,
    output reg  [DATAWIDTH-1:0] q_a,
    input  wire                 clock_b,
    input  wire [ADDRWIDTH-1:0] address_b,
    input  wire [DATAWIDTH-1:0] data_b,
    input  wire                 wren_b,
    output reg  [DATAWIDTH-1:0] q_b
);
    (* ram_style = "block" *) reg [DATAWIDTH-1:0] mem [0:NUMWORDS-1];
    always @(posedge clock_a) begin
        if (wren_a) begin
            mem[address_a] <= data_a;
            q_a <= data_a;
        end else begin
            q_a <= mem[address_a];
        end
    end
    always @(posedge clock_b) begin
        if (wren_b) begin
            mem[address_b] <= data_b;
            q_b <= data_b;
        end else begin
            q_b <= mem[address_b];
        end
    end
endmodule

module spram #(
    parameter ADDRWIDTH = 8,
    parameter DATAWIDTH = 8,
    parameter NUMWORDS = 1 << ADDRWIDTH
) (
    input  wire                 clock,
    input  wire [ADDRWIDTH-1:0] address,
    input  wire [DATAWIDTH-1:0] data,
    input  wire                 wren,
    output reg  [DATAWIDTH-1:0] q
);
    (* ram_style = "block" *) reg [DATAWIDTH-1:0] mem [0:NUMWORDS-1];
    always @(posedge clock) begin
        if (wren) begin
            mem[address] <= data;
            q <= data;
        end else begin
            q <= mem[address];
        end
    end
endmodule
