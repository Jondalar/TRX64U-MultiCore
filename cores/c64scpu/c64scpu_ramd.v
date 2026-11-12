// T140 D25 (first made for the C128 core, whose 128 KiB RAM it holds): the RAM in the DDR2 (the design is in docs/specs/T140-analog-video-out.md, "D25"). Core clock.
// In the C64SCPU core only RAM block 0 (64 KiB) is used: the block bit of every address is 0 (T182 step 1; step 3 replaces this memory path).
//
// The DDR2 holds the truth (c64scpu_ddrx.v, `e_*`). Next to it, in block RAM:
//   NSLOT slots, each a mirror of one 16 KiB bank ({block, A15, A14}) - the VIC reads these and only these, the clock after its command, as the block RAM build
//   gave it; a page (256 bytes) that is not valid, or a bank that is no slot, gives $FF and is counted, nothing waits;
//   a direct mapped line cache for the CPU (512 lines of 16 bytes, 8 KiB): a CPU read that neither a valid slot page nor the cache answers is a miss
//   (`rd_miss`: the CPU is held and repeats the read, T113 5.7) and the line is fetched.
// Every write goes to the DDR2 through an 8 entry FIFO (in order, one byte, `w_*`), into the slot whose tag it matches (whatever the page's state) and into
// the cache line when present. A DDR2 read starts only when the FIFO is empty and no write is open; a write to the line being fetched marks the fetch dirty
// (a cache line is dropped, a slot line is fetched again).
// PIN = 1 (NSLOT = 4): the four slots are the four banks of RAM block 0, the block the VIC sees,
// swept once after configuration and then always resident: no first-touch miss, no eviction. A VIC read of block 1 (the tag's top bit; always 0 in this core) would be
// answered $FF and counted (`n_vic_blk1`, also in `n_vic_miss`); the CPU would reach block 1 through the line cache.
// Without PIN: a bank becomes a slot when {vic block, va15, va14} (`vic_tag`) has been the same for 4 clocks (commands never allocate: the CAS glitch of a bank change
// would evict a slot); the slot is then swept page by page (the page of the last VIC miss first), lines of 16 bytes, 8 copy clocks per line.
//
// The second port (`pj_*`) is a one-job sequencer: a job takes the clock in which no command is presented (one operation per clock); a read is repeated
// until the byte is in a slot or the cache.
//
// Mutation hooks of the C128 core's bench (cores/c128/sim/tb_ramd.v; this core carries the hooks, not that bench): RAMD_MUT_NOSWEEP (no page is ever fetched), RAMD_MUT_NOWT (a write does not update a resident
// slot), RAMD_MUT_NOCUPD (a write does not update the cache line), RAMD_MUT_NODIRTY (a write to the line in flight is not noticed), RAMD_MUT_CMDALLOC (a VIC
// command allocates a slot: the glitch case), RAMD_MUT_NOWAITFIFO (a read starts before the FIFO is empty).
`timescale 1ns / 1ps
module c64scpu_ramd #(
    parameter NSLOT = 2,
    parameter PIN = 0                            // 1: slot i is bank i of RAM block 0 for good (NSLOT = 4: the whole block 0), nothing is ever allocated
) (
    input  wire         clk,
    // the arbiter's RAM command (c64_mem_c64scpu.v): word address {block, 15 bit}; one clock
    input  wire         cmd_valid,
    input  wire         cmd_we,
    input  wire [15:0]  cmd_wa,
    input  wire [15:0]  cmd_wdata,
    input  wire  [1:0]  cmd_wlane,
    input  wire         cmd_vic,
    input  wire  [2:0]  vic_tag,                 // {the VIC's RAM block, va15, va14}
    // the answer in the clock after a read command
    output wire         rd_v,                    // the word is valid
    output wire [15:0]  rd_word,
    output wire         rd_miss,                 // a CPU read that could not be answered: hold the CPU, it repeats the read
    output wire         hold,                    // the write FIFO is nearly full
    // the second port, byte address of block 0
    input  wire         pj_req,
    input  wire [15:0]  pj_addr,
    input  wire         pj_we,
    input  wire  [7:0]  pj_wdata,
    input  wire         pj_rom,
    output reg          pj_ack = 1'b0,           // a pulse; pj_rdata is valid with it and stays
    output reg   [7:0]  pj_rdata = 8'hFF,
    // DDR2 side (c64scpu_ddrx.v)
    output reg          m_req_tgl = 1'b0,
    output reg  [12:0]  m_req_line = 13'd0,
    input  wire         m_done_tgl,
    input  wire [127:0] fill,
    output reg          w_req_tgl = 1'b0,
    output reg  [16:0]  w_req_addr = 17'd0,
    output reg   [7:0]  w_req_data = 8'd0,
    input  wire         w_done_tgl,
    // counters for the application and the benches
    output reg  [15:0]  n_vic_miss = 16'd0,      // VIC reads answered $FF
    output reg  [15:0]  n_cpu_wait = 16'd0,      // CPU reads held
    output reg  [15:0]  n_fetch = 16'd0,         // DDR2 line reads (cache and slots)
    output reg  [15:0]  n_wlost = 16'd0,         // writes that found the FIFO full (must stay 0)
    output reg  [15:0]  n_vic_blk1 = 16'd0,      // VIC reads of RAM block 1 (PIN: answered $FF, not a slot)
    output wire [NSLOT*3-1:0] slot_tags,
    output wire [NSLOT-1:0]   slot_act,
    output wire [NSLOT-1:0]   slot_full
);
    localparam SW = (NSLOT <= 2) ? 1 : 2;

    // ---- the operation of this clock: a command, else a port job --------------------------------------------------------------------
    reg         pj_pend = 1'b0, pj_wr = 1'b0, pj_busy = 1'b0;
    reg  [15:0] pj_a = 16'd0;
    reg  [7:0]  pj_d = 8'd0;
    wire        fifo_room;
    wire        pj_fire = pj_pend && !pj_busy && !cmd_valid && (fifo_room || !pj_wr);   // one operation per clock, a command first
    wire        o_v    = cmd_valid || pj_fire;
    wire        o_we   = cmd_valid ? cmd_we : pj_wr;
    wire [15:0] o_wa   = cmd_valid ? cmd_wa : {1'b0, pj_a[15:1]};
    wire [15:0] o_wd   = cmd_valid ? cmd_wdata : {pj_d, pj_d};
    wire  [1:0] o_lane = cmd_valid ? cmd_wlane : (pj_a[0] ? 2'b10 : 2'b01);
    wire        o_vic  = cmd_valid && cmd_vic;
    wire        o_port = !cmd_valid;
    wire        o_wr   = o_v && o_we;
    wire        o_rd   = o_v && !o_we;

    wire  [2:0] o_tag  = o_wa[15:13];
    wire  [5:0] o_page = o_wa[12:7];
    wire [12:0] o_line = o_wa[15:3];

    // ---- slot state -----------------------------------------------------------------------------------------------------------------------
    reg  [NSLOT-1:0] s_act = PIN ? {NSLOT{1'b1}} : {NSLOT{1'b0}};
    reg  [2:0]       s_tag [0:NSLOT-1];
    reg  [63:0]      s_pv  [0:NSLOT-1];
    reg  [1:0]       s_gen [0:NSLOT-1];
    reg  [1:0]       s_age [0:NSLOT-1];
    integer si;
    initial for (si = 0; si < NSLOT; si = si + 1) begin s_tag[si] = PIN ? si[2:0] : 3'd0; s_pv[si] = 64'd0; s_gen[si] = 2'd0; s_age[si] = si[1:0]; end
    genvar g;
    assign slot_act = s_act;
    generate for (g = 0; g < NSLOT; g = g + 1) begin : gs
        assign slot_tags[3*g +: 3] = s_tag[g];
        assign slot_full[g] = &s_pv[g];
    end endgenerate

    wire [NSLOT-1:0] m, hit;                     // tag match, tag match with a valid page
    generate for (g = 0; g < NSLOT; g = g + 1) begin : gm
        assign m[g]   = s_act[g] && s_tag[g] == o_tag;
        assign hit[g] = m[g] && s_pv[g][o_page];
    end endgenerate

    // ---- the slots' block RAM -------------------------------------------------------------------------------------------------------------
    wire [12:0] cp_wa;                           // the copy's word address in the bank
    wire  [2:0] cp_i;
    wire [NSLOT-1:0] cp_en;                      // the copy writes slot s when no operation uses the port
    wire [127:0] fill_w = fill;
    wire [15:0] cp_word = fill_w[16 * cp_i +: 16];
    wire [12:0] sw_a = o_v ? o_wa[12:0] : cp_wa;
    wire [15:0] sw_d = o_v ? o_wd : cp_word;
    reg  [7:0]  sq_lo [0:NSLOT-1];
    reg  [7:0]  sq_hi [0:NSLOT-1];
    generate for (g = 0; g < NSLOT; g = g + 1) begin : gb
        (* ram_style = "block" *) reg [7:0] lo [0:8191];
        (* ram_style = "block" *) reg [7:0] hi [0:8191];
`ifdef RAMD_MUT_NOWT
        wire wr_o = 1'b0;
`else
        wire wr_o = o_wr && m[g];
`endif
        wire we_lo = (wr_o && o_lane[0]) || (!o_v && cp_en[g]);
        wire we_hi = (wr_o && o_lane[1]) || (!o_v && cp_en[g]);
        always @(posedge clk) begin
            if (we_lo) lo[sw_a] <= sw_d[7:0];
            if (we_hi) hi[sw_a] <= sw_d[15:8];
            sq_lo[g] <= lo[sw_a];
            sq_hi[g] <= hi[sw_a];
        end
    end endgenerate

    // ---- the CPU's line cache -----------------------------------------------------------------------------------------------------------
    // [127:0] the line, [132:128] {valid, tag}, the rest spare (17 byte lanes: one byte enable each)
    (* ram_style = "block" *) reg [135:0] cm [0:511];
    integer qi;
    initial for (qi = 0; qi < 512; qi = qi + 1) cm[qi] = 136'd0;
    wire  [8:0] o_idx = o_line[8:0] ^ {o_line[12:9], 5'b0};
    reg [135:0] cq;
    always @(posedge clk) cq <= cm[o_idx];
    reg  [16:0] cm_we;
    reg   [8:0] cm_wa;
    reg [135:0] cm_wd;
    integer ci;
    always @(posedge clk) for (ci = 0; ci < 17; ci = ci + 1) if (cm_we[ci]) cm[cm_wa][8 * ci +: 8] <= cm_wd[8 * ci +: 8];

    // ---- stage 1: the registered operation ----------------------------------------------------------------------------------------------
    reg         r_v = 1'b0, r_vic = 1'b0, r_port = 1'b0, r_we = 1'b0;
    reg  [15:0] r_wa = 16'd0;
    reg  [15:0] r_wd = 16'd0;
    reg   [1:0] r_lane = 2'd0;
    reg  [NSLOT-1:0] r_hit = {NSLOT{1'b0}}, r_m = {NSLOT{1'b0}};
    always @(posedge clk) begin
        r_v <= o_v; r_vic <= o_vic; r_port <= o_port; r_we <= o_we;
        r_wa <= o_wa; r_wd <= o_wd; r_lane <= o_lane;
        r_hit <= hit; r_m <= m;
    end
    wire [12:0] r_line = r_wa[15:3];
    wire  [8:0] r_idx  = r_line[8:0] ^ {r_line[12:9], 5'b0};
    wire        c_hit = cq[132] && cq[131:128] == r_line[12:9];
    wire        s_hit = |r_hit;
    reg  [15:0] s_word;
    integer k;
    always @(*) begin
        s_word = 16'd0;
        for (k = 0; k < NSLOT; k = k + 1) if (r_hit[k]) s_word = s_word | {sq_hi[k], sq_lo[k]};
    end
    wire [15:0] c_word = cq[16 * r_wa[2:0] +: 16];
    wire        r_rd = r_v && !r_we;
    assign rd_v    = r_rd && !r_port && (r_vic || s_hit || c_hit);
    assign rd_word = s_hit ? s_word : (r_vic ? 16'hFFFF : c_word);
    wire        cpu_miss = r_rd && !r_vic && !s_hit && !c_hit;     // CPU and port reads
    assign rd_miss = cpu_miss && !r_port;

    // ---- the write FIFO and its transfer to the DDR2 ------------------------------------------------------------------------------------
    reg  [24:0] ff [0:7];
    reg   [2:0] ff_w = 3'd0, ff_r = 3'd0;
    reg   [3:0] ff_n = 4'd0;
    assign fifo_room = ff_n < 4'd8;
    assign hold = ff_n >= 4'd5;
    reg         w_busy = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] wds = 3'b000;
    wire        w_ev = wds[2] != wds[1];
    wire        ff_push = o_wr && fifo_room;
    wire        ff_pop  = !w_busy && ff_n != 4'd0;
    wire        fifo_empty = ff_n == 4'd0 && !w_busy;
    always @(posedge clk) begin
        wds <= {wds[1:0], w_done_tgl};
        if (ff_push) begin ff[ff_w] <= {o_wa, o_lane[1], (o_lane[1] ? o_wd[15:8] : o_wd[7:0])}; ff_w <= ff_w + 3'd1; end
        if (o_wr && !fifo_room) n_wlost <= n_wlost + 16'd1;
        ff_n <= ff_n + (ff_push ? 4'd1 : 4'd0) - (ff_pop ? 4'd1 : 4'd0);
        if (ff_pop) begin
            w_req_addr <= ff[ff_r][24:8];
            w_req_data <= ff[ff_r][7:0];
            w_req_tgl  <= ~w_req_tgl;
            w_busy     <= 1'b1;
            ff_r       <= ff_r + 3'd1;
        end
        if (w_busy && w_ev) w_busy <= 1'b0;
    end

    // ---- the fill engine: one DDR2 read at a time ---------------------------------------------------------------------------------------
    localparam F_IDLE = 2'd0, F_ISSUE = 2'd1, F_WAIT = 2'd2, F_COPY = 2'd3;
    reg   [1:0] fs = F_IDLE;
    reg         f_cache = 1'b0;
    reg  [12:0] f_line = 13'd0;
    reg [SW-1:0] f_slot = {SW{1'b0}};
    reg   [1:0] f_gen = 2'd0;
    reg   [5:0] f_page = 6'd0;
    reg         f_dirty = 1'b0;
    reg   [2:0] f_ci = 3'd0;
    reg         cm_req = 1'b0;                   // a CPU miss waits for its line
    reg  [12:0] cm_req_line = 13'd0;
    reg         dem_v = 1'b0;                    // a VIC miss in a resident slot waits for its page
    reg [SW-1:0] dem_s = {SW{1'b0}};
    reg   [5:0] dem_p = 6'd0;
    reg         inst_pend = 1'b0;                // a fetched cache line waits for the write port
    reg  [12:0] inst_line = 13'd0;
    reg [127:0] inst_data = 128'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] mds = 3'b000;
    wire        d_ev = mds[2] != mds[1];
    always @(posedge clk) mds <= {mds[1:0], m_done_tgl};
    assign cp_i  = f_ci;
    assign cp_wa = {f_line[9:0], f_ci};
    generate for (g = 0; g < NSLOT; g = g + 1) begin : gc
        assign cp_en[g] = fs == F_COPY && !f_cache && f_slot == g && s_gen[g] == f_gen;
    end endgenerate

    // the sweep's choice: the slot of the lowest age that is not full, and in it the first page not valid
    reg  [SW-1:0] sw_slot;
    reg           sw_any;
    reg  [5:0]    sw_pg;
    reg  [1:0]    best;
    integer j, b;
    reg [63:0] pvv;
    always @(*) begin
        sw_any = 1'b0; sw_slot = {SW{1'b0}}; best = 2'd3;
        for (j = 0; j < NSLOT; j = j + 1)
            if (s_act[j] && !(&s_pv[j]) && (!sw_any || s_age[j] < best)) begin sw_any = 1'b1; sw_slot = j[SW-1:0]; best = s_age[j]; end
        pvv = s_pv[sw_slot];
        sw_pg = 6'd0;
        for (b = 63; b >= 0; b = b - 1) if (!pvv[b]) sw_pg = b[5:0];
    end
`ifdef RAMD_MUT_NOWAITFIFO
    wire can_issue = 1'b1;
`else
    wire can_issue = fifo_empty && !o_wr;
`endif
`ifdef RAMD_MUT_NOSWEEP
    wire sweep_ok = 1'b0;
`else
    wire sweep_ok = 1'b1;
`endif

    // ---- which bank is a slot: the bits the VIC addresses with, stable for 4 clocks -----------------------------------------------------
    reg  [2:0] vt1 = 3'd0, vt2 = 3'd0, vt3 = 3'd0;
    always @(posedge clk) begin vt1 <= vic_tag; vt2 <= vt1; vt3 <= vt2; end
    wire       vt_stable = vt1 == vic_tag && vt2 == vic_tag && vt3 == vic_tag;
    reg  [NSLOT-1:0] vt_m;
    reg  [SW-1:0] vt_s, victim;
    reg           alloc, touch_v;
    reg  [SW-1:0] touch_s;
    reg  [2:0]    alloc_t;
    reg  [1:0]    vage;
    integer a;
    always @(*) begin
        vt_m = {NSLOT{1'b0}}; vt_s = {SW{1'b0}};
        for (a = 0; a < NSLOT; a = a + 1) if (s_act[a] && s_tag[a] == vic_tag) begin vt_m[a] = 1'b1; vt_s = a[SW-1:0]; end
        // the victim: an unused slot, else the oldest
        victim = {SW{1'b0}}; vage = 2'd0;
        for (a = 0; a < NSLOT; a = a + 1) if (s_act[a] && s_age[a] >= vage) begin victim = a[SW-1:0]; vage = s_age[a]; end
        for (a = NSLOT - 1; a >= 0; a = a - 1) if (!s_act[a]) victim = a[SW-1:0];
        alloc = !PIN && vt_stable && !(|vt_m);
        alloc_t = vic_tag;
`ifdef RAMD_MUT_CMDALLOC
        if (!PIN && !alloc && o_vic && o_rd && !(|m)) begin alloc = 1'b1; alloc_t = o_tag; end
`endif
        touch_v = 1'b0; touch_s = {SW{1'b0}};
        if (alloc) begin touch_v = 1'b1; touch_s = victim; end
        else if (vt_stable && (|vt_m)) begin touch_v = 1'b1; touch_s = vt_s; end
    end

    // ---- the cache's write port: a write's update (one clock after the command), else an install of a fetched line --------------------------
    wire        u_hit  = r_v && r_we && cq[132] && cq[131:128] == r_line[12:9]
`ifdef RAMD_MUT_NOCUPD
                         && 1'b0
`endif
                         ;
    // an install is given up when a write to its line comes before it is done (the CPU repeats its read and the line is fetched again)
`ifdef RAMD_MUT_NODIRTY
    wire        inst_hit = 1'b0;
`else
    wire        inst_hit = o_wr && o_line == inst_line;
`endif
    wire        inst_go  = inst_pend && !u_hit && !inst_hit;
    integer ii;
    always @(*) begin
        cm_we = 17'd0; cm_wa = r_idx; cm_wd = {8'd0, {8{r_wd}}};
        if (u_hit) begin
            cm_we[2 * r_wa[2:0]]     = r_lane[0];
            cm_we[2 * r_wa[2:0] + 1] = r_lane[1];
        end else if (inst_go) begin
            cm_we = 17'h1FFFF;
            cm_wa = inst_line[8:0] ^ {inst_line[12:9], 5'b0};
            cm_wd = {3'b000, 1'b1, inst_line[12:9], inst_data};
        end
    end

    // ---- the sequential logic of the fill engine, the slots and the port job ----------------------------------------------------------------
    wire [15:0] p_word = s_hit ? s_word : c_word;
    integer t2;
    always @(posedge clk) begin
        pj_ack <= 1'b0;
        // ---- ages: 0 = most recently used, NSLOT-1 = oldest
        if (touch_v) for (t2 = 0; t2 < NSLOT; t2 = t2 + 1) begin
            if (t2 == touch_s) s_age[t2] <= 2'd0;
            else if (s_age[t2] < s_age[touch_s]) s_age[t2] <= s_age[t2] + 2'd1;
        end
        if (alloc) begin
            s_act[victim] <= 1'b1; s_tag[victim] <= alloc_t; s_pv[victim] <= 64'd0; s_gen[victim] <= s_gen[victim] + 2'd1;
            if (dem_v && dem_s == victim) dem_v <= 1'b0;
        end
        // ---- a VIC read of a resident slot whose page is not valid asks for the page; every other miss is only counted
        if (r_rd && r_vic && !s_hit) begin
            n_vic_miss <= n_vic_miss + 16'd1;
            if (r_wa[15]) n_vic_blk1 <= n_vic_blk1 + 16'd1;
            for (t2 = 0; t2 < NSLOT; t2 = t2 + 1) if (r_m[t2] && !dem_v) begin dem_v <= 1'b1; dem_s <= t2[SW-1:0]; dem_p <= r_wa[12:7]; end
        end
        // ---- a CPU (or port) read that nothing answered: fetch its line, unless it is the one in flight
        if (cpu_miss) begin
            if (!r_port) n_cpu_wait <= n_cpu_wait + 16'd1;
            if (!cm_req && !(fs != F_IDLE && f_cache && f_line == r_line) && !(inst_pend && inst_line == r_line)) begin
                cm_req <= 1'b1; cm_req_line <= r_line;
            end
        end
        // ---- a write to the line in flight
`ifndef RAMD_MUT_NODIRTY
        if (o_wr && fs != F_IDLE && f_line == o_line) f_dirty <= 1'b1;
`endif
        if (inst_pend && (inst_go || inst_hit)) inst_pend <= 1'b0;
        // ---- the engine
        case (fs)
        F_IDLE: begin
            if (cm_req) begin
                cm_req <= 1'b0; f_cache <= 1'b1; f_line <= cm_req_line; fs <= F_ISSUE;
            end else if (dem_v && sweep_ok) begin
                dem_v <= 1'b0;
                if (s_act[dem_s]) begin
                    f_cache <= 1'b0; f_slot <= dem_s; f_gen <= s_gen[dem_s]; f_page <= dem_p; f_line <= {s_tag[dem_s], dem_p, 4'd0}; fs <= F_ISSUE;
                end
            end else if (sw_any && sweep_ok) begin
                f_cache <= 1'b0; f_slot <= sw_slot; f_gen <= s_gen[sw_slot]; f_page <= sw_pg; f_line <= {s_tag[sw_slot], sw_pg, 4'd0}; fs <= F_ISSUE;
            end
        end
        F_ISSUE: begin
            if (!f_cache && s_gen[f_slot] != f_gen) fs <= F_IDLE;      // the slot was reallocated
            else if (can_issue) begin
                f_dirty <= 1'b0;
                m_req_line <= f_line; m_req_tgl <= ~m_req_tgl; n_fetch <= n_fetch + 16'd1; fs <= F_WAIT;
            end
        end
        F_WAIT: if (d_ev) begin
            if (f_cache) begin
                if (!f_dirty && !inst_pend) begin
                    inst_pend <= 1'b1; inst_line <= f_line; inst_data <= fill;
                end
                fs <= F_IDLE;
            end else if (s_gen[f_slot] != f_gen) fs <= F_IDLE;
            else begin f_ci <= 3'd0; fs <= F_COPY; end
        end
        default: begin                                                  // F_COPY
            if (s_gen[f_slot] != f_gen) fs <= F_IDLE;
            else if (!o_v) begin
                f_ci <= f_ci + 3'd1;
                if (f_ci == 3'd7) begin
                    if (f_dirty) fs <= F_ISSUE;                          // a write hit the line meanwhile: fetch it again
                    else if (f_line[3:0] == 4'd15) begin if (!(alloc && victim == f_slot)) s_pv[f_slot][f_page] <= 1'b1; fs <= F_IDLE; end
                    else begin f_line <= f_line + 13'd1; fs <= F_ISSUE; end
                end
            end
        end
        endcase
        // ---- the port job: take a request, fire it in a quiet clock, finish it from stage 1
        if (pj_req && !pj_pend && !pj_ack) begin
            pj_a <= pj_addr; pj_wr <= pj_we; pj_d <= pj_wdata;
            if (pj_rom) begin pj_ack <= 1'b1; pj_rdata <= 8'hFF; end   // the ROM block is not behind this port in this build
            else pj_pend <= 1'b1;
        end
        if (pj_fire) pj_busy <= 1'b1;
        if (r_v && r_port) pj_busy <= 1'b0;
        if (pj_fire && pj_wr) begin pj_pend <= 1'b0; pj_ack <= 1'b1; end
        // the byte of a port read: the lane of the word that the job's address names (pj_a[0] is stable until the job is done)
        if (r_v && r_port && !r_we && (s_hit || c_hit)) begin
            pj_rdata <= pj_a[0] ? p_word[15:8] : p_word[7:0];
            pj_pend <= 1'b0; pj_ack <= 1'b1;
        end
    end
endmodule
