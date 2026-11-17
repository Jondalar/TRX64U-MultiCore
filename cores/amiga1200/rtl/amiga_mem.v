// T85: the Amiga core's memory (chip, slow and fast RAM and Kickstart) in the DDR2 through the host's
// real-time port c_* (mig_arb serves it before every other client), behind a line cache in block RAM and
// the elastic stall of the chipset clock. Written for this project.
//
// The Minimig asks for one 16-bit word per 7 MHz bus cycle (141 ns) and wants it inside the same cycle.
// The DDR2 cannot answer that fast, so:
//  - the chip bus request is sampled the clock after the cycle's start (take_d = the clock after a ce28 tick
//    with clk7_en); the answer is `dout`;
//  - `pend` (the request is not answered) lets amiga_ce keep the chipset from the tick that ends the cycle: the whole
//    machine stands still, so it stays exact; amiga_ce pays the lost time back by ticking in every clock afterwards, so the
//    average rate stays real time as long as the stalls take less than 10 % of the clocks (T85);
//  - a hit in the cache (2048 lines of 8 words, direct mapped, write-through, 32 KB) answers in two clocks, so
//    the machine does not stall; a miss fetches the 16-byte line (one BL8 burst) and the next line is
//    prefetched when a stream passes the middle of a line.
// Byte order in the DDR2: the Amiga word is stored big endian (byte 2w = high byte), so a file copied
// verbatim by the application is what the core sees. Word w of a 128-bit burst is bits [16w+15:16w] with the
// high byte in [16w+7:16w].
// Memory map (word address a[21:0] = byte address / 2, 8 MB): chip 0.., fast from 0x200000, slow, Kickstart
// 0x780000..; DDR2 word address = BASE + a (c_addr counts 16-bit words).
// T138 (A1200 copy, amiga_mem.diff): the fast RAM port also carries the Zorro III board of 32 MB, p2_addr[24] = 1 and p2_addr[23:0] its
// word address; it lives at DDR2 word address Z3_BASE + a (byte 0x6000000), outside the window. The cache keys on the 25 bits.
`timescale 1ns / 1ps
module amiga_mem #(
    parameter [26:0] BASE = 27'h0800000,         // DDR2 byte 0x1000000
    parameter [26:0] Z3_BASE = 27'h3000000       // DDR2 byte 0x6000000
) (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        flush,                     // hold the cache empty (the machine is in reset / being loaded)
    // clock enable
    input  wire        ce28,
    input  wire        clk7_en,
    // chip bus (valid from the clock after the cycle start)
    input  wire        cs,
    input  wire        we,
    input  wire [21:0] addr,
    input  wire [1:0]  be,                        // {high byte, low byte}, active high
    input  wire [15:0] din,
    output wire [15:0] dout,
    output wire [47:0] dout48,                    // AGA wide fetch: [47:32] is always the word at +2 bytes of the requested even word;
                                                  // for a request at 0 mod 8 bytes it is {+2, +4, +6}; valid with dout
    output wire        pend,
    output wire        quiet,                     // sys_clk: no request taken or in flight, the DDR2 queue empty (Freeze)
    // the CPU's direct RAM port (fast RAM), toggle handshake
    input  wire        p2_sel,
    input  wire        p2_wr,
    input  wire [24:0] p2_addr,                    // [24] the Zorro III board, [23:0] its word address (else [21:0] as the chip port)
    input  wire [1:0]  p2_be,
    input  wire [15:0] p2_din,
    output reg  [15:0] p2_dout = 16'd0,
    output reg  [47:0] p2_dout48 = 48'd0,         // same for the fast RAM port (68020 cache line fill), valid with p2_dout
    output reg         p2_ready = 1'b0,
    // DDR2 real-time port (ui_clk)
    input  wire        ui_clk,
    input  wire        ui_rst,
    output reg  [26:0] c_addr = 27'd0,
    output reg  [2:0]  c_cmd = 3'd0,
    output reg         c_en = 1'b0,
    input  wire        c_rdy,
    output reg  [127:0] c_wdf_data = 128'd0,
    output reg  [15:0] c_wdf_mask = 16'hFFFF,
    output reg         c_wdf_wren = 1'b0,
    input  wire        c_wdf_rdy,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,
    // statistics (sys_clk)
    output reg  [31:0] st_reads = 32'd0,
    output reg  [31:0] st_miss = 32'd0,
    output reg  [31:0] st_writes = 32'd0,
    output reg  [31:0] st_pf = 32'd0,
    output reg  [31:0] st_pf_used = 32'd0,
    output reg  [31:0] st_cycles = 32'd0,
    // watchdog: the longest time a chip bus request stayed unanswered (sys_clk), and what the machine looked like
    // when one stayed unanswered for 2^20 clocks (33 ms, 1,000 bus cycles)
    output reg  [31:0] st_age_max = 32'd0,
    output reg  [31:0] st_stall_info = 32'd0
);
    // ------------------------------------------------------------------ sys_clk side
    localparam IW = 11;                          // 2048 lines of 16 bytes = 32 KB (the byte lanes' block RAMs are 2K deep: no more of them than for 4 KB)
    reg         b_we = 1'b0;
    reg  [IW-1:0] b_idx = 0;
    reg  [127:0] b_line = 128'd0;
    reg take_d = 1'b0;
    always @(posedge sys_clk) take_d <= ce28 & clk7_en;
    wire take_chip = take_d & cs & ~flush;

    // active request
    localparam S_IDLE = 3'd0, S_LOOK = 3'd1, S_PUSHW = 3'd2, S_DONE = 3'd3, S_RELOOK = 3'd4;
    reg [2:0]  st = S_IDLE;
    reg        r_src = 1'b0;                     // 0 chip, 1 p2
    reg        r_we = 1'b0;
    reg [24:0] r_a = 25'd0;
    reg [1:0]  r_be = 2'b00;
    reg [15:0] r_din = 16'd0;
    // a chip request that arrives while the machine of this module is busy with a p2 request
    reg        q_valid = 1'b0;
    reg        q_we = 1'b0;
    reg [21:0] q_a = 22'd0;
    reg [1:0]  q_be = 2'b00;
    reg [15:0] q_din = 16'd0;
    reg        chip_pend = 1'b0;                  // a chip bus request is taken and not answered yet
    // the request is answered in this very clock (hit, or a write the FIFO takes): the clock enable need not wait
    wire answer_now;
    assign pend = chip_pend & ~answer_now;

    // p2 request detection
    reg p2_active = 1'b0;
    // quiet = nothing of this module is under way: the request machine is idle, no chip request taken or queued, no fast
    // RAM request open, no line coming in, no command on its way, and the ui side has nothing in the command FIFO and
    // nothing on the DDR2 port (u_idle crossed with two flops; the caller waits some clocks on top, for the FIFO's pointers)
    wire        u_idle;                           // ui_clk
    (* ASYNC_REG = "TRUE" *) reg [1:0] ui_idle_s = 2'b00;
    always @(posedge sys_clk) ui_idle_s <= {ui_idle_s[0], u_idle};
    wire p2_new = p2_sel & ~p2_active & ~flush;

    // which request starts now (IDLE only)
    wire        go_chip_live = (st == S_IDLE) & take_chip;
    wire        go_chip_q    = (st == S_IDLE) & ~take_chip & q_valid;
    wire        go_p2        = (st == S_IDLE) & ~take_chip & ~q_valid & p2_new;
    wire [24:0] c_a_live = go_chip_live ? {3'b000, addr} : (go_chip_q ? {3'b000, q_a} : p2_addr);
    wire [24:0] idx_a = (st == S_IDLE) ? c_a_live : r_a;           // address the cache is read with
    // the line index is hashed with the next address bits, so bitplanes at power-of-two distances do not collide
    function [IW-1:0] hidx(input [21:0] line);
        reg [21:0] h, f;
        begin
            h = line >> IW;                                          // the line bits above the index (they are all in the tag)
            f = (h << 3) ^ (h >> (IW - 3));                          // streams a power of two apart land 8 lines (not one) apart
            hidx = line[IW-1:0] ^ f[IW-1:0];
        end
    endfunction
    wire [IW-1:0] idx   = hidx(idx_a[24:3]);
    wire [21:0]   idx_nl = idx_a[24:3] + 22'd1;                  // the line after the one the cache is read with
    wire [IW-1:0] idx_n  = hidx(idx_nl);

    // tags: valid + 11 bits; two copies so the prefetch can look at the next line
    localparam TW = 1 + 22 - IW;                                   // valid + the line bits above the index
    reg [TW-1:0] tag_a [0:(1<<IW)-1];
    reg [TW-1:0] tag_b [0:(1<<IW)-1];
    reg [TW-1:0] tag_q = 0, tag_nq = 0;
    // data: 8 banks of 16 bits, port A here (sys_clk, byte enables), port B in ui_clk (fill)
    wire [15:0] bank_q [0:7];
    reg  [7:0]  a_we_hi = 8'd0, a_we_lo = 8'd0;
    reg  [15:0] a_din = 16'd0;

    // fill bookkeeping
    reg        fill_inflight = 1'b0;
    reg [21:0] fill_line = 22'd0;
    reg        fill_dirty = 1'b0;
    // a write to the line that is coming in, in this very clock, counts too (the install and the write meet)
    wire       dirty_now = fill_dirty | ((st == S_LOOK) & r_we & (fill_line == r_a[24:3]));
    // the install of a line takes three clocks to show in the lookup (tag write, tag read, tag_q): a write that looks the line
    // up in the clocks after the fill is seen as done finds no tag, so it does not update the cached copy; its DDR2 command
    // comes after the fill's read, so the copy that gets installed is the old one. That write invalidates the line again.
    reg        ins1 = 1'b0, ins2 = 1'b0;
    reg [21:0] ins_line = 22'd0;
    reg        fill_pf = 1'b0;
    reg  [15:0] crit = 16'd0;                    // ui_clk: the wanted word of the line that is coming in (stable when fill_tgl toggles)
    reg        crit_valid = 1'b0;                 // the demand miss can be answered with the critical word of the fill
    reg [15:0] crit_q = 16'd0;
    reg [47:0] crit48 = 48'd0;                    // ui_clk: [47:32] = word at +2 of the wanted word; at 0 mod 8 bytes {+2,+4,+6} (stable with crit)
    reg [47:0] crit48_q = 48'd0;
    wire       fill_done_tgl;                     // from the ui side
    (* ASYNC_REG = "TRUE" *) reg [2:0] fd_s = 3'b000;
    wire       fill_done = fd_s[2] ^ fd_s[1];
    // tag write port
    reg        tw_en = 1'b0;
    reg [IW-1:0] tw_idx = 0;
    reg [TW-1:0] tw_val = 0;
    reg [IW:0]   fl_cnt = 0;                     // flush loop

    // command FIFO sys -> ui
    reg         cf_wr = 1'b0;
    reg  [44:0] cf_wdata = 45'd0;
    wire        cf_full;
    wire [44:0] cf_rdata;
    wire        cf_empty;
    reg         cf_rd = 1'b0;
    trx_afifo #(.W(45), .AW(3)) cmdf (
        .wclk(sys_clk), .wrst(sys_rst), .wr(cf_wr), .wdata(cf_wdata), .full(cf_full),
        .rclk(ui_clk), .rrst(ui_rst), .rd(cf_rd), .rdata(cf_rdata), .empty(cf_empty));

    assign quiet = (st == S_IDLE) & ~chip_pend & ~q_valid & ~p2_active & ~fill_inflight & ~take_d & ~cf_wr & ui_idle_s[1];

    // lookup results (valid in LOOK)
    wire [TW-2:0] want_tag = r_a[24:3+IW];
    wire        hit = tag_q[TW-1] & (tag_q[TW-2:0] == want_tag);
    wire [2:0]  r_w = r_a[2:0];
    wire [15:0] hit_word = bank_q[r_w];
    // [47:32] = the word after the requested one (banks read with the same index in the same clock); [31:0] = words 2,3 of the group
    wire [47:0] hit48 = {bank_q[{r_w[2:1], 1'b1}], bank_q[{r_w[2], 2'b10}], bank_q[{r_w[2], 2'b11}]};
    assign answer_now = (st == S_LOOK) & ~r_src & (r_we ? ~cf_full : hit);
    wire [21:0] nxt_line = r_a[24:3] + 22'd1;
    wire [IW-1:0] nxt_idx = hidx(nxt_line);
    wire [TW-2:0] nxt_tag = nxt_line[21:IW];
    wire        nxt_cached = tag_nq[TW-1] & (tag_nq[TW-2:0] == nxt_tag);   // tag_nq: the next line's tag, read with the same address as tag_q
    wire        nxt_valid_addr = ~(r_a[24] ? (r_a[23:3] == 21'h1FFFFF) : (r_a[21:3] == 19'h7FFFF));   // the line after the last one of a region is none

    reg [15:0] chip_dout = 16'd0;
    assign dout = chip_dout;
    reg [47:0] chip_dout48 = 48'd0;
    assign dout48 = chip_dout48;

    integer k;
    initial begin
        for (k = 0; k < (1<<IW); k = k + 1) begin tag_a[k] = 0; tag_b[k] = 0; end
    end

    // ---- watchdog
    reg [31:0] age = 32'd0;
    always @(posedge sys_clk) begin
        if (chip_pend) begin
            age <= age + 32'd1;
            if (age + 32'd1 > st_age_max) st_age_max <= age + 32'd1;
            if (age == 32'd1048576 && !st_stall_info[31])
                st_stall_info <= {1'b1, r_we, r_src, st, fill_inflight, cf_full, cf_empty, crit_valid, r_a[21:0]};
        end else age <= 32'd0;
    end

    // ---- tag RAM: single write port, sync read for the demand lookup and for the prefetch lookup (two copies, both block RAM)
    always @(posedge sys_clk) begin
        if (tw_en) begin tag_a[tw_idx] <= tw_val; tag_b[tw_idx] <= tw_val; end
        tag_q <= tag_a[idx];
        tag_nq <= tag_b[idx_n];
    end

    // ---- cache data port A (sys_clk): byte writes on a hit
    genvar gi;
    generate for (gi = 0; gi < 8; gi = gi + 1) begin : bk
        reg [7:0] hi [0:(1<<IW)-1];
        reg [7:0] lo [0:(1<<IW)-1];
        reg [7:0] hq = 8'd0, lq = 8'd0;
        always @(posedge sys_clk) begin
            // one address for read and write on this port: the write comes in the clock after LOOK, when idx is still r_a's
            if (a_we_hi[gi]) hi[idx] <= a_din[15:8];
            if (a_we_lo[gi]) lo[idx] <= a_din[7:0];
            hq <= hi[idx];
            lq <= lo[idx];
        end
        assign bank_q[gi] = {hq, lq};
        // port B: the fill (ui_clk)
        always @(posedge ui_clk) if (b_we) begin
            hi[b_idx] <= b_line[16*gi + 8 +: 8];
            lo[b_idx] <= b_line[16*gi +: 8];
        end
    end endgenerate

    // ---- the request machine
    always @(posedge sys_clk) begin
        fd_s <= {fd_s[1:0], fill_done_tgl};
        ins1 <= 1'b0; ins2 <= ins1;
        tw_en <= 1'b0;
        a_we_hi <= 8'd0; a_we_lo <= 8'd0;
        cf_wr <= 1'b0;
        p2_ready <= p2_ready;
        st_cycles <= st_cycles + (take_chip ? 32'd1 : 32'd0);

        // fill install (tag), also for a prefetch
        if (fill_done && fill_inflight) begin
            fill_inflight <= 1'b0;
            tw_en <= 1'b1; tw_idx <= hidx(fill_line);
            ins1 <= 1'b1; ins_line <= fill_line;
            tw_val <= dirty_now ? {TW{1'b0}} : {1'b1, fill_line[21:IW]};
            crit_q <= crit; crit48_q <= crit48; crit_valid <= ~fill_pf & ~dirty_now;
        end

        if (flush || sys_rst) begin
            // clear the tags one entry per clock
            st <= S_IDLE; chip_pend <= 1'b0; q_valid <= 1'b0;
            fill_inflight <= 1'b0; p2_active <= 1'b0;
            if (fl_cnt != (1<<IW)) begin
                fl_cnt <= fl_cnt + 1'b1;
                tw_en <= 1'b1; tw_idx <= fl_cnt[IW-1:0]; tw_val <= 0;
            end
        end else begin
            fl_cnt <= 0;
            // a chip request that finds the machine busy waits in q
            if (take_chip && st != S_IDLE) begin
                q_valid <= 1'b1; q_we <= we; q_a <= addr; q_be <= be; q_din <= din;
            end
            if (take_chip) chip_pend <= 1'b1;
            case (st)
            S_IDLE: begin
                if (go_chip_live || go_chip_q || go_p2) begin
                    st <= S_LOOK;
                    r_src <= ~(go_chip_live | go_chip_q);
                    r_we  <= go_chip_live ? we : (go_chip_q ? q_we : p2_wr);
                    r_a   <= c_a_live;
                    r_be  <= go_chip_live ? be : (go_chip_q ? q_be : p2_be);
                    r_din <= go_chip_live ? din : (go_chip_q ? q_din : p2_din);
                    if (go_chip_q) q_valid <= 1'b0;
                    if (go_p2) p2_active <= 1'b1;
                end
            end
            S_LOOK: begin
                if (r_we) begin
                    // write-through: the command goes to the DDR2, the cached copy is updated on a hit
                    if (!cf_full) begin
                        cf_wr <= 1'b1;
                        cf_wdata <= {2'b00, r_a[24:3], r_a[2:0], r_din, r_be};
                        st_writes <= st_writes + 32'd1;
                        if (hit) begin
                            a_din <= r_din;
                            a_we_hi[r_w] <= r_be[1]; a_we_lo[r_w] <= r_be[0];
                        end
                        if (fill_inflight && fill_line == r_a[24:3]) fill_dirty <= 1'b1;
                        if ((ins1 || ins2) && ins_line == r_a[24:3]) begin
                            tw_en <= 1'b1; tw_idx <= hidx(r_a[24:3]); tw_val <= 0;
                        end
                        st <= S_DONE;
                        if (!r_src) chip_pend <= 1'b0;
                    end
                end else begin
                    if (hit) begin
                        if (!r_src) begin chip_dout <= hit_word; chip_dout48 <= hit48; chip_pend <= 1'b0; end
                        else begin p2_dout <= hit_word; p2_dout48 <= hit48; end
                        st <= S_DONE;
                        st_reads <= st_reads + 32'd1;
                        // prefetch the next line when a stream passes the middle of this one
                        if (r_w[2] && !fill_inflight && !nxt_cached && nxt_valid_addr && !cf_full) begin
                            cf_wr <= 1'b1;
                            cf_wdata <= {2'b01, nxt_line, 3'd0, 16'd0, 2'b00};
                            fill_inflight <= 1'b1; fill_line <= nxt_line; fill_dirty <= 1'b0; fill_pf <= 1'b1;
                            tw_en <= 1'b1; tw_idx <= nxt_idx; tw_val <= 0;
                            st_pf <= st_pf + 32'd1;
                        end
                    end else if (!fill_inflight && !cf_full) begin
                        // miss: fetch the line, wait for it
                        cf_wr <= 1'b1;
                        cf_wdata <= {2'b01, r_a[24:3], r_a[2:0], 16'd0, 2'b00};
                        fill_inflight <= 1'b1; fill_line <= r_a[24:3]; fill_dirty <= 1'b0; fill_pf <= 1'b0; crit_valid <= 1'b0;
                        tw_en <= 1'b1; tw_idx <= hidx(r_a[24:3]); tw_val <= 0;
                        st_miss <= st_miss + 32'd1;
                        st <= S_RELOOK;
                    end
                    // else a fill (a prefetch) is in flight: look again next clock
                end
            end
            S_RELOOK: begin
                // the fill came in: answer with its wanted word at once; or, if a write hit the line meanwhile
                // or a prefetch was what came, look again (the read of the cache is running)
                if (crit_valid) begin
                    crit_valid <= 1'b0;
                    if (!r_src) begin chip_dout <= crit_q; chip_dout48 <= crit48_q; chip_pend <= 1'b0; end
                    else begin p2_dout <= crit_q; p2_dout48 <= crit48_q; end
                    st_reads <= st_reads + 32'd1;
                    st <= S_DONE;
                end else if (!fill_inflight && !tw_en) st <= S_LOOK;
            end
            S_DONE: begin
                st <= S_IDLE;
                if (r_src) begin p2_ready <= ~p2_ready; p2_active <= 1'b0; end
            end
            default: st <= S_IDLE;
            endcase
        end
    end

    // ------------------------------------------------------------------ ui_clk side
    reg  [2:0]  us = 3'd0;
    localparam U_IDLE = 3'd0, U_ISSUE = 3'd1, U_WAITR = 3'd2;
    reg  [44:0] u_cmd = 45'd0;
    reg         fill_tgl = 1'b0;
    assign fill_done_tgl = fill_tgl;
    assign u_idle = (us == U_IDLE) & cf_empty & ~c_en & ~c_wdf_wren & ~cf_rd;

    // DDR2 line -> Amiga words: word w = {byte 2w, byte 2w+1}
    function [127:0] swap_line(input [127:0] x);
        integer j;
        begin for (j = 0; j < 8; j = j + 1) swap_line[16*j +: 16] = {x[16*j +: 8], x[16*j + 8 +: 8]}; end
    endfunction

    wire [127:0] sw_line = swap_line(c_rd_data);
    wire [1:0]  u_kind = u_cmd[44:43];
    wire [21:0] u_line = u_cmd[42:21];
    wire [2:0]  u_w    = u_cmd[20:18];
    wire [15:0] u_data = u_cmd[17:2];
    wire [1:0]  u_be   = u_cmd[1:0];
    wire [63:0]  sw_grp  = u_w[2] ? sw_line[127:64] : sw_line[63:0];   // the aligned 4-word group of the wanted word, word 0 in [15:0]

    always @(posedge ui_clk) begin
        cf_rd <= 1'b0;
        b_we <= 1'b0;
        if (ui_rst) begin
            us <= U_IDLE; c_en <= 1'b0; c_wdf_wren <= 1'b0;
        end else case (us)
        U_IDLE: if (!cf_empty && !cf_rd) begin
            u_cmd <= cf_rdata; cf_rd <= 1'b1; us <= U_ISSUE;
        end
        U_ISSUE: begin
            c_en <= 1'b1;
            c_addr <= u_line[21] ? (Z3_BASE + {3'd0, u_line[20:0], 3'd0}) : (BASE + {5'd0, u_line[18:0], 3'd0});
            if (u_kind == 2'b00) begin
                c_cmd <= 3'b000;
                c_wdf_wren <= 1'b1;
                c_wdf_data <= {8{u_data[7:0], u_data[15:8]}};
                c_wdf_mask <= 16'hFFFF & ~(({14'd0, u_be[0], u_be[1]}) << {u_w, 1'b0});
            end else begin
                c_cmd <= 3'b001;
                c_wdf_wren <= 1'b0;
            end
            if (c_en && c_rdy) begin
                c_en <= 1'b0; c_wdf_wren <= 1'b0;
                if (u_kind == 2'b00) us <= U_IDLE;
                else begin b_idx <= hidx(u_line); us <= U_WAITR; end
            end
        end
        U_WAITR: if (c_rd_valid) begin
            b_line <= swap_line(c_rd_data);
            crit <= swap_line(c_rd_data) >> (16 * u_w);
            crit48 <= {u_w[1] ? sw_grp[63:48] : sw_grp[31:16], sw_grp[47:32], sw_grp[63:48]};
            b_we <= 1'b1;
            fill_tgl <= ~fill_tgl;
            us <= U_IDLE;
        end
        default: us <= U_IDLE;
        endcase
    end
endmodule
