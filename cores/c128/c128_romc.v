// T113 §5.4, §5.7: the C128's 72 KiB ROM image in the DDR2 behind a line cache (the scheme of cores/nes/trx/nes_mem.v: T95 D13 - direct mapped,
// 16 byte lines, keyed by the linear address). Since 5.7 a miss does NOT hold the machine: the answer to a lookup is always there in the clock after
// the command - `rvalid` on a hit, `rmiss` on a miss - and the line is fetched in the background. The caller (c64_top_c128.v) holds only the CPU
// that asked (RDY for the 8502, the clock enable of the Z80) and the CPU repeats the read in its next cycle, which hits once the line is in. The
// VIC, RAM and every other command are never kept waiting (before 5.7 `cmd_ready` went low and `vic_stall` froze the VIC's raster with the CPU:
// the frame came late by every fill, and the host scaler's one lock moved the picture).
//
// Address: the C128 map gives the ROM image as 4 KiB banks (c128_map.v: system ROM banks 0-15 for the CPU; the character ROMs, banks 16 and 17, are
// not here any more: 5.7), so the linear byte address is bank * 4096 + offset; `cmd_word` is that address / 2 (16 bit words, as c64_mem_c128.v
// receives it). The bank switch needs no invalidation: another bank is another linear address. The index is the 9 line bits xor the top 4 bits of
// the line number, so a fixed bank and a switched one do not fight over the same lines (as nes_mem.v).
//
// Cache RAM: 512 lines x (128 bit line + 4 bit tag + valid) = 133 bits in one block RAM word (2 RAMB36 as 2 x 72 bit); 8 KiB.
// The line comes from c128_rome.v (ui_clk, DDR2 port) across a toggle handshake; `fill` is stable while `done_tgl` toggles. One fetch at a time:
// a lookup that misses while a fetch is open only reports the miss (the CPU repeats it).
// `flush` (a pulse, from the core's reset) clears all valid bits in 512 clocks; lookups miss meanwhile and start no fetch.
//
// Counters for the application (c128_regs.v): n_hit, n_miss (fetches started), n_wait (lookups that missed = CPU cycles held), fill_max (the
// longest fetch, request to line, in clk).
//
`timescale 1ns / 1ps
module c128_romc (
    input  wire        clk,
    input  wire        flush,
    input  wire        cmd_valid,           // a ROM read (the caller has decoded is_rom and !we)
    output wire        cmd_ready,           // always 1 (kept for the port list)
    input  wire [15:0] cmd_word,            // linear byte address >> 1 (72 KiB = 0x9000 words)
    output wire        rvalid,              // the clock after the command: the word is in rdata
    output wire        rmiss,               // the clock after the command: the line is not in the cache; the caller repeats the read
    output wire [15:0] rdata,               // {hi byte, lo byte} of the word, valid with rvalid

    output reg         req_tgl = 1'b0,      // toggles for a line to fetch
    output reg  [12:0] req_line = 13'd0,    // line number = byte address >> 4, stable until the next request
    input  wire        done_tgl,            // toggles when `fill` holds the line (ui_clk, crossed here)
    input  wire [127:0] fill,

    output reg  [31:0] n_hit = 32'd0,
    output reg  [31:0] n_miss = 32'd0,
    output reg  [31:0] n_wait = 32'd0,
    output reg  [15:0] fill_max = 16'd0,
    output wire        fill_busy,           // a fetch is open
    output wire        busy                 // the flush walker is running
);
    // ---- the cache RAM -----------------------------------------------------------------------------------------------------------
    (* ram_style = "block" *) reg [132:0] cm [0:511];
    integer qi;
    initial for (qi = 0; qi < 512; qi = qi + 1) cm[qi] = 133'd0;      // power-up: all lines invalid (block RAM INIT 0)
    reg  [132:0] cq;
    reg  [8:0]   fcnt = 9'd0;
    reg          flushing = 1'b0;
    assign busy = flushing;
    wire [12:0]  line_in = cmd_word[15:3];
    wire [8:0]   idx_in  = line_in[8:0] ^ {line_in[12:9], 5'b0};

    // lookup of the command presented in this clock: the registered word is there in the next one
    always @(posedge clk) cq <= cm[idx_in];

    // ---- state ---------------------------------------------------------------------------------------------------------------------
    reg         st_wait = 1'b0;             // a fetch is open
    reg         pend = 1'b0;                // a command was accepted in the previous clock: cq is its line
    reg [15:0]  sv_word = 16'd0;
    wire [12:0] sv_line = sv_word[15:3];
    wire [3:0]  rq_tag  = req_line[12:9];
    wire [8:0]  rq_idx  = req_line[8:0] ^ {req_line[12:9], 5'b0};
    reg [15:0]  fcl = 16'd0;                // clocks of the open fetch
    assign fill_busy = st_wait;

    (* ASYNC_REG = "TRUE" *) reg [2:0] done_s = 3'b000;
    always @(posedge clk) done_s <= {done_s[1:0], done_tgl};
    wire done_ev = done_s[2] != done_s[1];

    wire tag_ok =
        (cq[131:128] == sv_line[12:9]);
    wire hit_c = pend && cq[132] && tag_ok && !flushing;
    wire hit = hit_c;
    wire miss = pend && !hit;

    assign cmd_ready = 1'b1;
    wire acc = cmd_valid;

    always @(posedge clk) begin
        pend <= acc;
        if (acc) sv_word <= cmd_word;
        if (miss && !st_wait && !flushing) begin
            req_line <= sv_line;
            req_tgl  <= ~req_tgl;
            n_miss   <= n_miss + 32'd1;
            st_wait  <= 1'b1;
            fcl      <= 16'd0;
        end else if (st_wait) begin
            if (fcl != 16'hFFFF) fcl <= fcl + 16'd1;
            if (done_ev) begin
                st_wait <= 1'b0;
                if (fcl > fill_max) fill_max <= fcl;
            end
        end
        if (hit) n_hit <= n_hit + 32'd1;
        if (miss) n_wait <= n_wait + 32'd1;
    end

    // write port: the flush walker, else the fill (the cycle done_ev is seen); the line is the one requested
    reg         drop = 1'b0;                // a flush came while the fetch was open: its line is of the old image and is not written
    always @(posedge clk) begin
        if (flush) drop <= st_wait;
        else if (done_ev) drop <= 1'b0;
    end
    wire        wr_fill = st_wait && done_ev && !drop && !flush;
    wire [8:0]   wr_a = flushing ? fcnt : rq_idx;                 // one write port: the address and the data are muxed, not the statements
    wire [132:0] wr_d = flushing ? 133'd0 : {1'b1, rq_tag, fill};
    always @(posedge clk)
        if (flushing || wr_fill) cm[wr_a] <= wr_d;
    always @(posedge clk) begin
        if (flush) begin flushing <= 1'b1; fcnt <= 9'd0; end
        else if (flushing) begin
            fcnt <= fcnt + 9'd1;
            if (fcnt == 9'd511) flushing <= 1'b0;
        end
    end

    // ---- answer --------------------------------------------------------------------------------------------------------------------
    wire [2:0]   w    = sv_word[2:0];
    wire [127:0] line = cq[127:0];
    assign rdata  = line[16 * w +: 16];
    assign rvalid = hit;
    assign rmiss  = miss;
endmodule
