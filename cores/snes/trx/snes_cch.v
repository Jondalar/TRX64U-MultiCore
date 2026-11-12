// T142: one direct mapped, write-through line cache of the SNES core's memory (snes_mem.v has three: cartridge ROM, WRAM, battery RAM). Written for this
// project (TREX), no upstream code; the lookup is the circuit of cores/nes/trx/nes_mem.v (the captured request, a tag read, a compare, a registered
// miss that holds the machine's clock enable) with the changes the SNES needs: a write that hits updates the byte at once (the machine reads its
// own write back a few cycles later and the engine's write to the DDR2 may still be on its way), and the index is the line number folded on itself.
//
// Timing (cpu_clk; the machine ticks no more often than every 4th clock, `cap` is the clock before a tick):
//   cap       a, rd, wr, wd are taken into registers (the machine's request signals are combinational functions of its registers and have settled
//             over the clocks since the last tick)
//   cap+1     the data and tag RAMs are read at the captured address
//   cap+2     tag compare: `hit`; a write that hits is written into the data RAM (the word of cap+1 with the byte replaced); the read byte is `q` (comb from the data register)
//   cap+3     `miss` (registered at cap+2) is valid. It stays until a new cap evaluates the request again - after the engine's fill and the tag write that
//             `done` tells about, the next lookup hits.
// The captured request is not changed before the next cap (at least 4 clocks after the last), which is what the engine relies on to read `m_line`.
//
`timescale 1ns / 1ps
module snes_cch #(
    parameter integer AW = 23,                  // byte address bits of the region
    parameter integer IB = 9                    // index bits: 2**IB lines of 16 bytes
) (
    input  wire              clk,
    input  wire              cap,
    input  wire [AW-1:0]     a,                 // the machine's request, combinational
    input  wire              rd,
    input  wire              wr,                // a write that commits at the coming tick (the caller has made it a one-shot per bus cycle)
    input  wire [7:0]        wd,
    output wire [7:0]        q,                 // the byte at the captured address (valid from cap+2)
    output reg               miss = 1'b0,       // registered: the captured read is not in the cache
    output wire [AW-5:0]     m_line,            // line number of the captured request
    output wire [IB-1:0]     m_idx,
    // the engine
    input  wire              f_we,
    input  wire [IB+1:0]     f_a,               // {index, word}
    input  wire [31:0]       f_d,
    input  wire              t_we,
    input  wire [IB-1:0]     t_a,
    input  wire              t_v,               // the tag written: valid and the line number
    input  wire [AW-5:0]     t_l
);
    localparam integer LB = AW - 4;             // line number bits
    // ------------------------------------------------------------------ the captured request
    reg [AW-1:0] a_r = {AW{1'b0}};
    reg          rd_r = 1'b0, wr_r = 1'b0;
    reg [7:0]    wd_r = 8'd0;
    reg [2:0]    cp = 3'd0;                     // cap, cap+1, cap+2 as a shift register
    always @(posedge clk) begin
        cp <= {cp[1:0], cap};
        if (cap) begin a_r <= a; rd_r <= rd; wr_r <= wr; wd_r <= wd; end
    end
    wire [LB-1:0] line = a_r[AW-1:4];
    wire [3*IB-1:0] lp = {{(3*IB-LB){1'b0}}, line};
    // index: the line number folded on itself, so that two banks 2**(IB+4) bytes apart (a fixed bank and a switched one, code and its tables) do not share a line
    wire [IB-1:0] idx = lp[IB-1:0] ^ lp[2*IB-1:IB] ^ lp[3*IB-1:2*IB];
    wire [LB-1:0] want = line;
    assign m_line = line;
    assign m_idx = idx;
    // ------------------------------------------------------------------ the RAMs
    (* ram_style = "block" *) reg [31:0] data [0:(4 << IB) - 1];
    reg [31:0] dq = 32'd0;
    reg [LB:0] tq = {(LB + 1){1'b0}};
    reg [LB:0] tags [0:(1 << IB) - 1];
    integer ti;
    initial begin
        for (ti = 0; ti < (1 << IB); ti = ti + 1) tags[ti] = {(LB + 1){1'b0}};
    end
    reg [LB-1:0] want_q = {LB{1'b0}};
    reg [1:0]    bo_q = 2'd0;
    wire         hit = tq[LB] && (tq[LB-1:0] == want_q);
    wire         whit = cp[1] && wr_r && hit;
    wire         wen = whit;
    wire [IB+1:0] a_addr = {idx, a_r[3:2]};
    // a write that hits: the word read at cap+1 with its byte replaced, written back (a whole word, not four byte lanes: byte write enables cost a block RAM per lane)
    wire [4:0]   sh = {a_r[1:0], 3'b000};
    wire [31:0]  merged = (dq & ~(32'h000000FF << sh)) | ({24'd0, wd_r} << sh);
    always @(posedge clk) begin
        // port A: the lookup (read first) and the write-through byte
        if (wen) data[a_addr] <= merged;
        dq <= data[a_addr];
    end
    always @(posedge clk) begin
        // port B: the engine's line fill
        if (f_we) data[f_a] <= f_d;
    end
    always @(posedge clk) begin
        tq <= tags[idx];
        want_q <= want;
        bo_q <= a_r[1:0];
        if (t_we) tags[t_a] <= {t_v, t_l};
        // cap+2: the lookup is complete; the registered result is valid from cap+3 and stays until the next cap's lookup
        if (cp[1]) miss <= rd_r && !hit;
    end
    assign q = bo_q == 2'd0 ? dq[7:0] : bo_q == 2'd1 ? dq[15:8] : bo_q == 2'd2 ? dq[23:16] : dq[31:24];
endmodule
