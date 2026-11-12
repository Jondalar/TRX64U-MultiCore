// T141/A500 (2026-11-08): copy of cores/amiga1200/rtl/paula_floppy_fifo.v for the Amiga 500 core (block RAM instead of ~1,000 LUTRAM).
// T97 5.8: the Amiga 1200 core's copy of the shared cores/amiga/rtl/src/minimig-aga/paula_floppy_fifo.v.
// Same ports and the same cycle behaviour; the difference is where the read lands. The shared file writes the
// RAM read and the empty-write bypass (out <= in) into one register, so Vivado cannot put the read register into a
// block RAM and builds 2048 x 16 as distributed RAM (about 1,000 LUT in pb_l2, run 909). Here the RAM read has its
// own register q, the bypass its own register byp, and out is a mux behind them. Proven equal to the shared file by
// cores/amiga1200/sim/fifo/run.sh (both side by side, random stimulus, every enabled cycle compared).

module paula_floppy_fifo
( input wire ce28,
	input		  clk, // bus clock
	input		  clk7_en,
	input		  reset, // reset
	input [15:0]	  in, // data in
	output [15:0]	  out, // data out
	input		  rd, // read from fifo
	input		  wr, // write to fifo
	output  	  empty, // fifo is empty
	output		  full  // fifo is full
);

(* ram_style = "block" *) reg [15:0] mem [2047:0];	// 2048 words by 16 bit (2 MFM-encoded sectors)
reg	[11:0] in_ptr;		// fifo input pointer
reg	[11:0] out_ptr;		// fifo output pointer
reg	[15:0] q;		// RAM read register (block RAM output)
reg	[15:0] byp;		// bypass register: the word written into an empty fifo
reg	       sel;		// 1: out shows byp, 0: out shows q
reg	       empty_write;

wire   equal = (in_ptr[10:0]==out_ptr[10:0]);
assign empty = (equal && (in_ptr[11] == out_ptr[11]));
assign full =  (equal && (in_ptr[11] != out_ptr[11]));
assign out = sel ? byp : q;

wire en = ce28 & clk7_en;
wire do_rd = en & ~reset & rd & ~empty;
wire do_wr = en & ~reset & wr & ~full;

// the RAM: one write port, one read port with its own enable and register
always @(posedge clk) begin
   if (do_wr) mem[in_ptr[10:0]] <= in;
   if (do_rd) q <= mem[out_ptr[10:0] + 11'd1];
end

always @(posedge clk) if (en) begin
   empty_write <= 1'b0;
   if (reset) begin
      in_ptr <= 12'd0;
      out_ptr <= 12'd0;
   end else begin
      if (rd && !empty) begin
	 byp <= in;
	 sel <= empty_write;
	 out_ptr <= out_ptr + 12'd1;
      end
      if (wr && !full) begin
	 in_ptr <= in_ptr + 12'd1;
	 if (empty) begin
	    byp <= in;
	    sel <= 1'b1;
	    empty_write <= 1'b1;
	 end
      end
   end
end

initial begin q = 16'd0; byp = 16'd0; sel = 1'b0; empty_write = 1'b0; end

endmodule
