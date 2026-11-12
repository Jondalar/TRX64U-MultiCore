// T168: a simulation model of the Xilinx XPM macro xpm_memory_tdpram, only as far as DDT's framebuffer_bram.sv and linebuf_iter8.sv use it: two clocks, port A writes
// (byte write enable = one bit for the 8 bit words used), port B reads with one clock of latency and an enable. Written for this project (TREX); the real macro is
// Vivado's. Not read by the synthesis recipe.
`timescale 1ns / 1ps
module xpm_memory_tdpram #(
    parameter integer ADDR_WIDTH_A = 11,
    parameter integer ADDR_WIDTH_B = 11,
    parameter integer AUTO_SLEEP_TIME = 0,
    parameter integer BYTE_WRITE_WIDTH_A = 8,
    parameter integer BYTE_WRITE_WIDTH_B = 8,
    parameter integer CASCADE_HEIGHT = 0,
    parameter CLOCKING_MODE = "independent_clock",
    parameter ECC_MODE = "no_ecc",
    parameter MEMORY_INIT_FILE = "none",
    parameter MEMORY_INIT_PARAM = "0",
    parameter integer MEMORY_OPTIMIZATION = 0,
    parameter MEMORY_PRIMITIVE = "block",
    parameter integer MEMORY_SIZE = 16384,
    parameter integer MESSAGE_CONTROL = 0,
    parameter integer READ_DATA_WIDTH_A = 8,
    parameter integer READ_DATA_WIDTH_B = 8,
    parameter integer READ_LATENCY_A = 1,
    parameter integer READ_LATENCY_B = 1,
    parameter READ_RESET_VALUE_A = "0",
    parameter READ_RESET_VALUE_B = "0",
    parameter RST_MODE_A = "SYNC",
    parameter RST_MODE_B = "SYNC",
    parameter integer SIM_ASSERT_CHK = 0,
    parameter integer USE_EMBEDDED_CONSTRAINT = 0,
    parameter integer USE_MEM_INIT = 0,
    parameter WAKEUP_TIME = "disable_sleep",
    parameter integer WRITE_DATA_WIDTH_A = 8,
    parameter integer WRITE_DATA_WIDTH_B = 8,
    parameter WRITE_MODE_A = "write_first",
    parameter WRITE_MODE_B = "read_first"
) (
    input  wire clka, ena, input wire wea, input wire [ADDR_WIDTH_A-1:0] addra, input wire [7:0] dina, output reg [7:0] douta,
    input  wire regcea, rsta,
    input  wire clkb, enb, input wire web, input wire [ADDR_WIDTH_B-1:0] addrb, input wire [7:0] dinb, output reg [7:0] doutb,
    input  wire regceb, rstb, sleep, injectsbiterra, injectdbiterra, injectsbiterrb, injectdbiterrb,
    output wire sbiterra, dbiterra, sbiterrb, dbiterrb
);
    reg [7:0] mem [0:(1 << ADDR_WIDTH_A) - 1];
    integer i;
    initial begin
        for (i = 0; i < (1 << ADDR_WIDTH_A); i = i + 1) mem[i] = 8'd0;
        douta = 8'd0; doutb = 8'd0;
    end
    assign sbiterra = 1'b0, dbiterra = 1'b0, sbiterrb = 1'b0, dbiterrb = 1'b0;
    always @(posedge clka) if (ena && wea) begin mem[addra] <= dina; douta <= dina; end
    always @(posedge clkb) if (enb) doutb <= mem[addrb];
endmodule
