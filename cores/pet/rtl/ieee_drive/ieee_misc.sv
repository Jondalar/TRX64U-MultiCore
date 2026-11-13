/*
 * Commodore 4040/8250 IEEE drive implementation
 *
 * Copyright (C) 2024, Erik Scheffers (https://github.com/eriks5)
 *
 * This file is part of PET2001_MiSTer.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

module ieeedrv_sync #(parameter WIDTH = 1)
(
	input                  clk,
	input      [WIDTH-1:0] in,
	output reg [WIDTH-1:0] out
);

reg [WIDTH-1:0] s1,s2;
always @(posedge clk) begin
	s1 <= in;
	s2 <= s1;
	if(s1 == s2) out <= s2;
end

endmodule

module ieeedrv_bus_sync
(
	input              clk,
	input  st_ieee_bus in,
	output st_ieee_bus out
);

st_ieee_bus s1,s2;
always @(posedge clk) begin
	s1 <= in;
	s2 <= s1;
	if(s1 == s2) out <= s2;
end

endmodule

// T134 (TREX): the two altsyncram wrappers of the origin are replaced by inferred true dual port block RAMs (same ports, same registered reads).
// The origin's INITFILE (.mif DOS / controller ROMs) is not used: Commodore ROM bytes are not in this repository, the application loads them (rom_wr).
module ieeedrv_rom #(
	parameter DATAWIDTH,
	parameter ADDRWIDTH,
	parameter NUMWORDS,
	parameter INITFILE="UNUSED"
)(
	input	                     clock_a,
	input	     [ADDRWIDTH-1:0] address_a,
	input	     [DATAWIDTH-1:0] data_a,
	input	                     wren_a,
	output reg [DATAWIDTH-1:0] q_a,

	input	                     clock_b,
	input	     [ADDRWIDTH-1:0] address_b,
	input	     [DATAWIDTH-1:0] data_b,
	input	                     wren_b,
	output reg [DATAWIDTH-1:0] q_b
);

(* ram_style = "block" *) reg [DATAWIDTH-1:0] mem [0:NUMWORDS-1];

always @(posedge clock_a) begin
	q_a <= mem[address_a];
end

always @(posedge clock_b) begin
	if (wren_b) mem[address_b] <= data_b;
	q_b <= mem[address_b];
end

endmodule

module ieeedrv_mem #(
	parameter DATAWIDTH,
	parameter ADDRWIDTH
)(
	input	                     clock_a,
	input	     [ADDRWIDTH-1:0] address_a,
	input	     [DATAWIDTH-1:0] data_a,
	input	                     wren_a,
	output reg [DATAWIDTH-1:0] q_a,

	input	                     clock_b,
	input	     [ADDRWIDTH-1:0] address_b,
	input	     [DATAWIDTH-1:0] data_b,
	input	                     wren_b,
	output reg [DATAWIDTH-1:0] q_b
);

(* ram_style = "block" *) reg [DATAWIDTH-1:0] mem [0:(1<<ADDRWIDTH)-1];

always @(posedge clock_a) begin
	if (wren_a) mem[address_a] <= data_a;
	q_a <= mem[address_a];
end

always @(posedge clock_b) begin
	if (wren_b) mem[address_b] <= data_b;
	q_b <= mem[address_b];
end

endmodule

module ieee_rommux #(
	parameter NDR = 4,
	parameter ADDRWIDTH = 14
)(
	input                  clk,
	input                  ph2,
	input  [ADDRWIDTH-1:0] drv_addr[NDR],
	output           [1:0] drv_select,
	output reg [ADDRWIDTH-1:0] rom_addr,
	
	input            [7:0] rom_q,
	output reg       [7:0] drv_data[NDR]
);

localparam OFFSET = 3;

reg [2:0] mux_state;

assign drv_select = mux_state[1:0] - 2'(OFFSET);

always @(posedge clk) begin
	if (~&mux_state) 
		mux_state <= mux_state + 1'd1;
	if (ph2)
		mux_state <= 0;

	if (mux_state < NDR)
		rom_addr <= drv_addr[mux_state[1:0]];
	
	if (mux_state >= OFFSET && mux_state < NDR+OFFSET)
		drv_data[drv_select] <= rom_q;
end

endmodule
