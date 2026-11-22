// T182 step 3 (D9, P-A, Alex 2026-11-05): the ONE write-through cache in front of everything the CPU owns in the DDR2 - SCPU SRAM (128 KiB at 0x03000000),
// SuperRAM (16 MiB at 0x02000000) and the SuperCPU ROM (at 0x03080000). Written for this project; the structure (2^IW lines of 16 bytes, direct mapped, the block
// RAM word {valid, tag, line}, the flush walker, the toggle handshake with c64scpu_ddrx.v) is that of c64scpu_romc.v.
// Size (T241 D4, 2026-11-21): IW = 11, 2048 lines, 32 KiB (8 RAMB36), was 9 (512 lines, 8 KiB, 2 RAMB36). The T241.1 suite measured every miss at about 0.3 PHI2 cycles
// at 20 MHz, and scpu6's loads that an 8 KiB direct-mapped cache misses (two reads 8 KiB apart, code blocks 1 KiB apart) ran 1.5 to 1.9 times slower than the real
// SuperCPU's static RAM. 11 is the most the continuous lookup allows: scpu_mem.v's class C_KS adds $8000, so only the low 15 bits of p are the CPU address's in every class.
//
// Address: `p` is the DDR2 byte address minus 0x02000000, 25 bits (bit 24 = 0x03000000 and up: SRAM and ROM; bit 24 = 0: SuperRAM). Index = p[IW+3:4], tag = p[24:IW+4].
// Clock: clk2x (the CPU's), ungated. T182 step 4 (spec 5.6): the lookup is continuous. `ix` is the index of the CPU's address (a_out[IW+3:4]: the low 15 bits of p are the
// CPU address's low 15 bits for every class), `cq` (the line, valid bit and tag) follows it one clock later; scpu_mem.v compares the tag, picks the byte and decides. A miss is
// started by `f_go` (one clock, `f_line` = p >> 4): the line comes from the DDR2 (job S of c64scpu_ddrx.v), is written to cm and `cq` shows it a clock later; the CPU's enable
// is withheld meanwhile by scpu_mem.v (that is the DEV_CYC). A fetch starts only when no write is open and not within `quiet` clocks of the last one.
// Write (wr_go, one clock, in the clock after the CPU's enable; w_p and wr_d registered by scpu_mem.v at that enable; `cq` still shows the line of the cycle that ended):
// write-through. A hit replaces the byte in the line, every write also goes to the DDR2 as a byte write (job SW, one open at a time: `wbusy`). No write allocate.
// flush (a pulse): all valid bits clear in 2^IW clocks; `flushing_o` is high meanwhile (scpu_mem.v: lookups miss, no fetch); a line that arrives for a fetch that was open at the flush is dropped.
// `SCPU_MUT_NOFLUSH`: the flush is ignored (negative control for the ROM_CTRL flush of the bench). `SCPU_MUT_NOKILL`: the write-to-prefetched-line guard is off, `SCPU_MUT_NOPGOWR`: a prefetch may start in the clock of a CPU write `SCPU_MUT_NOINVDEFER`: the invalidate of a killed line drops a byte write to another line (controls of the mdust smc bench and of tb_scpu_cache.v). Next-line prefetch of program-stream lines (T208): see below.
`timescale 1ns / 1ps
module scpu_cache #(
    parameter IW = 11                           // index bits: 2^IW lines of 16 bytes (11: 32 KiB; tb_scpu_cache.v runs 9)
) (
    input  wire         clk,
    input  wire         flush,
    input  wire [IW-1:0] ix,                    // index of the CPU's address (continuous)
    output wire [149-IW:0] cq_o,                // {valid, tag (21 - IW bits), line} of ix, one clock later
    input  wire         f_go,                   // start the fetch of line f_line
    input  wire         f_if,                   // ... for a program-stream fetch (vpa)
    input  wire [20:0]  f_line,
    input  wire         rd_done,                // the CPU completed a cache read (for the hit counter)
    input  wire         rd_if,                  // ... a program-stream read (one clock, with rd_ln = its line)
    input  wire [20:0]  rd_ln,
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
    (* ram_style = "block" *) reg [149-IW:0] cm [0:(1 << IW) - 1];
    integer qi;
    initial for (qi = 0; qi < (1 << IW); qi = qi + 1) cm[qi] = {(150 - IW){1'b0}};
    reg  [149-IW:0] cq = {(150 - IW){1'b0}};
    assign cq_o = cq;
    wire [IW-1:0]   idx = w_p[IW+3:4];          // the write's line
    wire [20-IW:0]  tag = w_p[24:IW+4];
    wire [3:0]   off = w_p[3:0];

    (* ASYNC_REG = "TRUE" *) reg [2:0] sd_s = 3'b000, wd_s = 3'b000;
    always @(posedge clk) begin sd_s <= {sd_s[1:0], s_done_tgl}; wd_s <= {wd_s[1:0], sw_done_tgl}; end
    wire s_ev = sd_s[2] != sd_s[1];
    wire w_ev = wd_s[2] != wd_s[1];

    reg  [IW-1:0] fcnt = {IW{1'b0}};
    reg          flushing = 1'b0;
    assign flushing_o = flushing;
    reg          st_wait = 1'b0;
    reg          wb = 1'b0;
    assign wbusy = wb;
    reg          drop = 1'b0;
    reg  [2:0]   quiet = 3'd0;
    reg          missed = 1'b0;                 // this lookup needed a fetch

    // Next-line prefetch (T208): `stream` is the line the program stream is in (set by a program-stream miss, advanced by a prefetch). When the CPU reads a program byte in that line
    // the next line is fetched (pf_pend), as soon as the fetch engine is idle and no demand miss waits. A demand miss always goes first; a prefetch in flight holds a miss only until it ends.
    // Coherence: the CPU runs while the line is on its way, so a write to that line (wr_go, same p >> 4 as s_req_line) while the fetch is open or in the 3 clocks after its fill (`cq`
    // may still show the old line then and the write cannot update it) is remembered in kill_q: one clock later the line's index is invalidated (inval), and a fill still to come is
    // dropped (as a flush does). A demand fill never needs this: the CPU is held.
    wire tag_ok = cq[148-IW:128] == tag;
    wire wr_hit  = cq[149-IW] && tag_ok && !flushing;
    wire wr_byte = wr_go && wr_hit;
    reg  [20:0]  stream = 21'd0;
    reg          stream_v = 1'b0;
    reg          pf_pend = 1'b0;
    reg          pf_cur = 1'b0;                 // the open (or last) fetch is a prefetch
    reg  [2:0]   rcnt = 3'd0;                   // clocks since the last fill ended
    reg          kill_q = 1'b0;
`ifdef SCPU_MUT_NOINVDEFER
    wire         inval = kill_q;                // control (tb_scpu_cache race_c): the invalidate takes the port from a byte write to another line
`else
    wire         inval = kill_q && !wr_byte;    // a byte write to another line has the port first; the invalidate waits one clock (kill_q stays)
`endif
    wire         dgo = f_go && !st_wait && !flushing && !wb && quiet == 3'd0;
`ifdef SCPU_MUT_NOPGOWR
    wire         pg_wr = 1'b0;                  // control (tb_scpu_cache race_a, race_b): a prefetch may start in the clock of a CPU write
`else
    wire         pg_wr = wr_go;
`endif
    wire         pgo = pf_pend && !f_go && !st_wait && !flushing && !wb && !pg_wr && quiet == 3'd0;      // !wr_go: a CPU write opens in this clock (wb is still 0); kill_q cannot see it against the line this fetch starts, so it goes first and the fetch follows the write (wb)
    wire [20:0]  stream_nx = stream + 21'd1;


    always @(posedge clk) begin
        cq <= cm[ix];
        if (rd_done) missed <= 1'b0;
        if (quiet != 3'd0) quiet <= quiet - 3'd1;
        if (rcnt != 3'd0) rcnt <= rcnt - 3'd1;
`ifdef SCPU_MUT_NOKILL
        kill_q <= 1'b0;                                 // control (smc bench): a write to a prefetched line neither drops the fill nor invalidates the line
`else
        kill_q <= (wr_go && pf_cur && (st_wait || rcnt != 3'd0) && w_p[24:4] == s_req_line) || (kill_q && wr_byte);
`endif
        if (rd_if && stream_v && rd_ln == stream) pf_pend <= 1'b1;
        if (dgo) begin
            s_req_line <= f_line;
            s_req_tgl  <= ~s_req_tgl;
            st_wait    <= 1'b1;
            missed     <= 1'b1;
            pf_cur     <= 1'b0;
            n_miss     <= n_miss + 32'd1;
            if (f_if) begin stream <= f_line; stream_v <= 1'b1; pf_pend <= 1'b0; end
        end else if (pgo) begin
            s_req_line <= stream_nx;
            s_req_tgl  <= ~s_req_tgl;
            st_wait    <= 1'b1;
            pf_cur     <= 1'b1;
            stream     <= stream_nx;
            pf_pend    <= 1'b0;
        end else if (st_wait && s_ev) begin
            st_wait <= 1'b0;
            quiet   <= 3'd4;
            rcnt    <= 3'd3;
        end
        if (wr_go) begin
            sw_req_addr <= w_p; sw_req_data <= wr_d; sw_req_tgl <= ~sw_req_tgl; wb <= 1'b1;
        end else if (wb && w_ev) wb <= 1'b0;
        if (rd_done && !missed) n_hit <= n_hit + 32'd1;
        if (flush) drop <= st_wait; else if (s_ev) drop <= 1'b0; else if (kill_q && st_wait) drop <= 1'b1;
    end

    wire         wr_fill = st_wait && s_ev && !drop && !flush;
    reg  [127:0] merged;
    always @(*) begin
        merged = cq[127:0];
        merged[8 * off +: 8] = wr_d;
    end
    wire [IW-1:0] wr_a = flushing ? fcnt : (wr_byte && !inval) ? idx : s_req_line[IW-1:0];     // the open fetch's index (also the line to invalidate: s_req_line cannot change before kill_q has acted, quiet / wb hold every new fetch; a pending inval is deferred while a byte write has the port)
    wire [149-IW:0] wr_w = (flushing || inval) ? {(150 - IW){1'b0}} : wr_byte ? {1'b1, tag, merged} : {1'b1, s_req_line[20:IW], fill};
    always @(posedge clk)
        if (flushing || inval || wr_byte || wr_fill) cm[wr_a] <= wr_w;
    always @(posedge clk) begin
`ifdef SCPU_MUT_NOFLUSH
        flushing <= 1'b0;
`else
        if (flush) begin flushing <= 1'b1; fcnt <= {IW{1'b0}}; end
        else if (flushing) begin
            fcnt <= fcnt + 1'b1;
            if (fcnt == {IW{1'b1}}) flushing <= 1'b0;
        end
`endif
    end
endmodule
