// T182 step 3 (D9, P-A, Alex 2026-11-05): the ONE 8 KiB write-through cache in front of everything the CPU owns in the DDR2 - SCPU SRAM (128 KiB at 0x03000000),
// SuperRAM (16 MiB at 0x02000000) and the SuperCPU ROM (at 0x03080000). Written for this project; the structure (512 lines of 16 bytes, direct mapped, the block
// RAM word {valid, tag, line}, the flush walker, the toggle handshake with c64scpu_ddrx.v) is that of c64scpu_romc.v.
//
// Address: `p` is the DDR2 byte address minus 0x02000000, 25 bits (bit 24 = 0x03000000 and up: SRAM and ROM; bit 24 = 0: SuperRAM). Index = p[12:4], tag = p[24:13].
// Clock: clk2x (the CPU's), ungated. T182 step 4 (spec 5.6): the lookup is continuous. `ix` is the index of the CPU's address (a_out[12:4]: the low 13 bits of p are the CPU
// address's low 13 bits for every class), `cq` (the line, valid bit and tag) follows it one clock later; scpu_mem.v compares the tag, picks the byte and decides. A miss is
// started by `f_go` (one clock, `f_line` = p >> 4): the line comes from the DDR2 (job S of c64scpu_ddrx.v), is written to cm and `cq` shows it a clock later; the CPU's enable
// is withheld meanwhile by scpu_mem.v (that is the DEV_CYC). A fetch starts only when no write is open and not within `quiet` clocks of the last one.
// Write (wr_go, one clock, in the clock after the CPU's enable; w_p and wr_d registered by scpu_mem.v at that enable; `cq` still shows the line of the cycle that ended):
// write-through. A hit replaces the byte in the line, every write also goes to the DDR2 as a byte write (job SW, one open at a time: `wbusy`). No write allocate.
// flush (a pulse): all valid bits clear in 512 clocks; `flushing_o` is high meanwhile (scpu_mem.v: lookups miss, no fetch); a line that arrives for a fetch that was open at the flush is dropped.
// `SCPU_MUT_NOFLUSH`: the flush is ignored (negative control for the ROM_CTRL flush of the bench).
`timescale 1ns / 1ps
module scpu_cache (
    input  wire         clk,
    input  wire         flush,
    input  wire  [8:0]  ix,                     // index of the CPU's address (continuous)
    output wire [140:0] cq_o,                   // {valid, tag, line} of ix, one clock later
    input  wire         f_go,                   // start the fetch of line f_line
    input  wire [20:0]  f_line,
    input  wire         rd_done,                // the CPU completed a cache read (for the hit counter)
    input  wire         wr_go,
    input  wire [24:0]  w_p,
    input  wire  [7:0]  wr_d,
    output wire         wbusy,
    output wire         flushing_o,

    output reg          s_req_tgl = 1'b0,
    output reg  [20:0]  s_req_line = 21'd0,     // p >> 4
    input  wire         s_done_tgl,
    input  wire [127:0] fill,
    output reg          sw_req_tgl = 1'b0,
    output reg  [24:0]  sw_req_addr = 25'd0,    // p
    output reg   [7:0]  sw_req_data = 8'd0,
    input  wire         sw_done_tgl,

    output reg  [31:0]  n_hit = 32'd0,
    output reg  [31:0]  n_miss = 32'd0
);
    (* ram_style = "block" *) reg [140:0] cm [0:511];
    integer qi;
    initial for (qi = 0; qi < 512; qi = qi + 1) cm[qi] = 141'd0;
    reg  [140:0] cq = 141'd0;
    assign cq_o = cq;
    wire [8:0]   idx = w_p[12:4];              // the write's line
    wire [11:0]  tag = w_p[24:13];
    wire [3:0]   off = w_p[3:0];

    (* ASYNC_REG = "TRUE" *) reg [2:0] sd_s = 3'b000, wd_s = 3'b000;
    always @(posedge clk) begin sd_s <= {sd_s[1:0], s_done_tgl}; wd_s <= {wd_s[1:0], sw_done_tgl}; end
    wire s_ev = sd_s[2] != sd_s[1];
    wire w_ev = wd_s[2] != wd_s[1];

    reg  [8:0]   fcnt = 9'd0;
    reg          flushing = 1'b0;
    assign flushing_o = flushing;
    reg          st_wait = 1'b0;
    reg          wb = 1'b0;
    assign wbusy = wb;
    reg          drop = 1'b0;
    reg  [2:0]   quiet = 3'd0;
    reg          missed = 1'b0;                 // this lookup needed a fetch

    wire tag_ok = cq[139:128] == tag;
    wire wr_hit  = cq[140] && tag_ok && !flushing;

    always @(posedge clk) begin
        cq <= cm[ix];
        if (rd_done) missed <= 1'b0;
        if (quiet != 3'd0) quiet <= quiet - 3'd1;
        if (f_go && !st_wait && !flushing && !wb && quiet == 3'd0) begin
            s_req_line <= f_line;
            s_req_tgl  <= ~s_req_tgl;
            st_wait    <= 1'b1;
            missed     <= 1'b1;
            n_miss     <= n_miss + 32'd1;
        end else if (st_wait && s_ev) begin
            st_wait <= 1'b0;
            quiet   <= 3'd4;
        end
        if (wr_go) begin
            sw_req_addr <= w_p; sw_req_data <= wr_d; sw_req_tgl <= ~sw_req_tgl; wb <= 1'b1;
        end else if (wb && w_ev) wb <= 1'b0;
        if (rd_done && !missed) n_hit <= n_hit + 32'd1;
        if (flush) drop <= st_wait; else if (s_ev) drop <= 1'b0;
    end

    wire         wr_fill = st_wait && s_ev && !drop && !flush;
    wire         wr_byte = wr_go && wr_hit;
    reg  [127:0] merged;
    always @(*) begin
        merged = cq[127:0];
        merged[8 * off +: 8] = wr_d;
    end
    wire [8:0]   wr_a = flushing ? fcnt : wr_byte ? idx : s_req_line[8:0];     // the open fetch's index
    wire [140:0] wr_w = flushing ? 141'd0 : wr_byte ? {1'b1, tag, merged} : {1'b1, s_req_line[20:9], fill};
    always @(posedge clk)
        if (flushing || wr_byte || wr_fill) cm[wr_a] <= wr_w;
    always @(posedge clk) begin
`ifdef SCPU_MUT_NOFLUSH
        flushing <= 1'b0;
`else
        if (flush) begin flushing <= 1'b1; fcnt <= 9'd0; end
        else if (flushing) begin
            fcnt <= fcnt + 9'd1;
            if (fcnt == 9'd511) flushing <= 1'b0;
        end
`endif
    end
endmodule
