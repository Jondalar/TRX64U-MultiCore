// T142 D3: the memory behind the SNES machine that is not in block RAM. The machine's cartridge ROM (up to 8 MiB), its 128 KiB of work RAM and the
// cartridge's battery RAM (up to 256 KiB) live in the DDR2 behind the host's real-time port c_* (ui_clk), at the base the application sets:
//   base + 0x000000   cartridge ROM, linear (the mapper's ROM_ADDR is a linear offset into it; the application writes the image there)
//   base + 0x800000   work RAM 128 KiB
//   base + 0x820000   battery RAM up to 256 KiB (the application reads it for the save and writes it from the save file)
// What the machine sees is a memory that always answers in time. Three direct mapped, write-through, 16 byte line caches (snes_cch.v: ROM 8 KiB,
// WRAM 4 KiB, battery RAM 2 KiB) answer its requests; a read that is not in its cache HOLDS THE WHOLE MACHINE (`stall` takes the clock enable away,
// snes_core.v) until the engine has the line, as the NES core does with its cartridge (cores/nes/trx/nes_mem.v) and the Atari core with a8_mem.vhd.
// Everything stops together and nothing is out of step; the machine only runs a little slower while it waits (the stall is counted).
// Writes: a write the machine commits (WRAM, battery RAM) updates its cache line at once when the line is there and goes to the DDR2 through a four
// entry queue, in order, as a one byte write with a byte mask; the machine is held only when the queue is nearly full. A line fill is queued behind
// every write before it, and the real-time port keeps its commands in order, so a read never sees older data than a write before it.
// Timing rule (the machine has one clock enable on cpu_clk and ticks no more often than every 4th clock): see snes_cch.v. The machine asks for a byte
// two ticks or more before it uses it (the ROM address register is loaded at SYSCLKR_CE, the S-CPU takes its data at SYSCLKF_CE, 3 ticks later at the
// fastest), so a miss that is seen by the second tick holds it in time; tools/snes_gen.py's machine and the bench in sim/mem prove that against a
// memory that is never late.
// Written for this project (TREX), no upstream code; the engine and its clock crossing are the circuit of nes_mem.v.
//
`timescale 1ns / 1ps
module snes_mem (
    input  wire        clk,                     // cpu_clk
    input  wire        rst,                     // the app's reset request: the caches are flushed at both of its edges
    output wire        busy,                    // the flush is running: the machine must stay in reset
    input  wire        cap,
    input  wire        adv,                     // the machine has ticked since the last cap: its bus cycle is a new one
    input  wire [31:0] base,                    // DDR2 byte address of the image (16 byte aligned; bits 3:0 ignored)
    // the machine (combinational, sampled at cap)
    input  wire [22:0] rom_a,
    input  wire        rom_rd,
    output wire [7:0]  rom_q,
    input  wire [16:0] wram_a,
    input  wire        wram_rd,
    input  wire        wram_wr,                 // a write that commits at the coming tick
    input  wire [7:0]  wram_d,
    output wire [7:0]  wram_q,
    input  wire [17:0] bs_a,
    input  wire        bs_rd,
    input  wire        bs_wr,
    input  wire [7:0]  bs_d,
    output wire [7:0]  bs_q,
    output wire        stall,                   // a register: the clock enable must not tick
    // the host's real-time DDR2 port (ui_clk)
    input  wire        ui_clk,
    input  wire        ui_rst,
    output reg  [26:0] c_addr = 27'd0,
    output reg  [2:0]  c_cmd = 3'd0,
    output wire        c_en,
    input  wire        c_rdy,
    output reg  [127:0] c_wdf_data = 128'd0,
    output reg  [15:0] c_wdf_mask = 16'hFFFF,
    output wire        c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,
    // counters for the bring-up (clk)
    output reg  [31:0] st_miss_rom = 32'd0,
    output reg  [31:0] st_miss_wram = 32'd0,
    output reg  [31:0] st_miss_bs = 32'd0,
    output reg  [31:0] st_stall = 32'd0,
    output reg  [31:0] st_wr = 32'd0
);
    // ------------------------------------------------------------------ the caches
    wire [18:0] r_line;  wire [8:0] r_idx;  wire r_miss;
    wire [12:0] w_line;  wire [7:0] w_idx;  wire w_miss;
    wire [13:0] b_line;  wire [6:0] b_idx;  wire b_miss;
    reg         rf_we = 1'b0, wf_we = 1'b0, bf_we = 1'b0;
    reg         rt_we = 1'b0, wt_we = 1'b0, bt_we = 1'b0;
    reg  [10:0] f_a = 11'd0;                    // {index, word}
    reg  [31:0] f_word = 32'd0;
    reg  [8:0]  t_a = 9'd0;
    reg         t_v = 1'b0;
    reg  [18:0] t_l = 19'd0;
    snes_cch #(.AW(23), .IB(9)) c_rom (
        .clk(clk), .cap(cap), .a(rom_a), .rd(rom_rd), .wr(1'b0), .wd(8'd0), .q(rom_q), .miss(r_miss), .m_line(r_line), .m_idx(r_idx),
        .f_we(rf_we), .f_a(f_a[10:0]), .f_d(f_word), .t_we(rt_we), .t_a(t_a[8:0]), .t_v(t_v), .t_l(t_l[18:0]));
    snes_cch #(.AW(17), .IB(8)) c_wram (
        .clk(clk), .cap(cap), .a(wram_a), .rd(wram_rd), .wr(wram_wr & adv), .wd(wram_d), .q(wram_q), .miss(w_miss), .m_line(w_line), .m_idx(w_idx),
        .f_we(wf_we), .f_a(f_a[9:0]), .f_d(f_word), .t_we(wt_we), .t_a(t_a[7:0]), .t_v(t_v), .t_l(t_l[12:0]));
    snes_cch #(.AW(18), .IB(7)) c_bs (
        .clk(clk), .cap(cap), .a(bs_a), .rd(bs_rd), .wr(bs_wr & adv), .wd(bs_d), .q(bs_q), .miss(b_miss), .m_line(b_line), .m_idx(b_idx),
        .f_we(bf_we), .f_a(f_a[8:0]), .f_d(f_word), .t_we(bt_we), .t_a(t_a[6:0]), .t_v(t_v), .t_l(t_l[13:0]));

    // ------------------------------------------------------------------ the write queue
    // entries {region (1 = battery RAM), byte address in the region, byte}; pushed at cap when the machine commits a write
    reg  [26:0] wq [0:3];
    reg  [1:0]  wq_w = 2'd0, wq_r = 2'd0;
    reg  [2:0]  wq_n = 3'd0;
    reg         wq_full = 1'b0;                 // registered: three entries or more
    wire        push_w = cap & adv & wram_wr;
    wire        push_b = cap & adv & bs_wr & ~wram_wr;
    wire        push = push_w | push_b;
    wire [26:0] push_d = push_w ? {2'b00, wram_a, wram_d} : {1'b1, bs_a, bs_d};
    wire        pop;
    // ------------------------------------------------------------------ flush
    reg  [9:0]  fl_cnt = 10'd512;               // the tag sweep (512 = done)
    reg         rst_q = 1'b0;
    wire        fl_busy = (fl_cnt != 10'd512);
    assign busy = fl_busy;

    reg  stall_r = 1'b1;
    assign stall = stall_r;
    always @(posedge clk) stall_r <= r_miss | w_miss | b_miss | wq_full;
    always @(posedge clk) begin
        if (stall) st_stall <= st_stall + 32'd1;
        if (push) st_wr <= st_wr + 32'd1;
    end

    // ------------------------------------------------------------------ the engine (clk side)
    localparam [2:0] M_IDLE = 3'd0, M_WAIT = 3'd1, M_FILL = 3'd2, M_COOL = 3'd3;
    reg [2:0]  ms = M_IDLE;
    reg        jw = 1'b0;                       // the job is a write
    reg [1:0]  jr = 2'd0;                       // read job region: 0 ROM, 1 WRAM, 2 battery RAM
    reg [18:0] j_line = 19'd0;
    reg [8:0]  j_idx = 9'd0;
    reg        rq_tgl = 1'b0;
    reg        rq_wr = 1'b0;
    reg [26:0] rq_addr = 27'd0;                 // DDR2 16 bit word address
    reg [3:0]  rq_lane = 4'd0;
    reg [7:0]  rq_byte = 8'd0;
    reg        dn_tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] dn_s = 3'b000;
    reg [127:0] ui_line = 128'd0;
    reg [2:0]  fill_i = 3'd0;
    reg [3:0]  cool = 4'd0;
    wire [31:0] base_b = {base[31:4], 4'd0};
    wire [26:0] qe = wq[wq_r];
    wire [31:0] w_byte = base_b + (qe[26] ? 32'h00820000 : 32'h00800000) + {14'd0, qe[25:8]};
    wire [31:0] r_byte = base_b + {9'd0, r_line, 4'd0};
    wire [31:0] wr_byte = base_b + 32'h00800000 + {15'd0, w_line, 4'd0};
    wire [31:0] bs_byte = base_b + 32'h00820000 + {14'd0, b_line, 4'd0};
    assign pop = (ms == M_IDLE) && !fl_busy && (wq_n != 3'd0) && (cool == 4'd0);
    always @(posedge clk) begin
        rf_we <= 1'b0; wf_we <= 1'b0; bf_we <= 1'b0; rt_we <= 1'b0; wt_we <= 1'b0; bt_we <= 1'b0;
        rst_q <= rst;
        dn_s <= {dn_s[1:0], dn_tgl};
        if (cool != 4'd0) cool <= cool - 4'd1;
        // the queue
        if (push) begin wq[wq_w] <= push_d; wq_w <= wq_w + 2'd1; end
        wq_n <= wq_n + {2'd0, push} - {2'd0, pop};
        if (pop) wq_r <= wq_r + 2'd1;
        wq_full <= (wq_n + {2'd0, push} - {2'd0, pop}) >= 3'd3;
        case (ms)
        M_IDLE: begin
            if (fl_busy) begin
                // sweep the tags of all three caches: one index a clock
                rt_we <= 1'b1; wt_we <= 1'b1; bt_we <= 1'b1; t_a <= fl_cnt[8:0]; t_v <= 1'b0; t_l <= 19'd0;
                fl_cnt <= fl_cnt + 10'd1;
            end else if (cool != 4'd0) begin
            end else if (wq_n != 3'd0) begin
                jw <= 1'b1;
                rq_addr <= {w_byte[27:4], 3'b000}; rq_lane <= w_byte[3:0]; rq_byte <= qe[7:0]; rq_wr <= 1'b1; rq_tgl <= ~rq_tgl; ms <= M_WAIT;
            end else if (r_miss) begin
                jw <= 1'b0; jr <= 2'd0; j_line <= r_line; j_idx <= r_idx;
                rq_addr <= r_byte[27:1]; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; ms <= M_WAIT;
                st_miss_rom <= st_miss_rom + 32'd1;
            end else if (w_miss) begin
                jw <= 1'b0; jr <= 2'd1; j_line <= {6'd0, w_line}; j_idx <= {1'b0, w_idx};
                rq_addr <= wr_byte[27:1]; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; ms <= M_WAIT;
                st_miss_wram <= st_miss_wram + 32'd1;
            end else if (b_miss) begin
                jw <= 1'b0; jr <= 2'd2; j_line <= {5'd0, b_line}; j_idx <= {2'd0, b_idx};
                rq_addr <= bs_byte[27:1]; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; ms <= M_WAIT;
                st_miss_bs <= st_miss_bs + 32'd1;
            end
        end
        M_WAIT: if (dn_s[2] != dn_s[1]) begin
            if (jw) begin ms <= M_IDLE; end
            else begin ms <= M_FILL; fill_i <= 3'd0; end
        end
        M_FILL: begin
            // f_word, f_a and t_a are registers and the write enables follow one clock later, with them
            fill_i <= fill_i + 3'd1;
            if (fill_i < 3'd4) begin
                f_word <= fill_i[1:0] == 2'd0 ? ui_line[31:0] : fill_i[1:0] == 2'd1 ? ui_line[63:32] : fill_i[1:0] == 2'd2 ? ui_line[95:64] : ui_line[127:96];
                f_a <= {j_idx, fill_i[1:0]};
                rf_we <= (jr == 2'd0); wf_we <= (jr == 2'd1); bf_we <= (jr == 2'd2);
            end else begin
                t_a <= j_idx; t_v <= 1'b1; t_l <= j_line;
                rt_we <= (jr == 2'd0); wt_we <= (jr == 2'd1); bt_we <= (jr == 2'd2);
                ms <= M_IDLE; cool <= 4'd9;
            end
        end
        default: ms <= M_IDLE;
        endcase
        if (rst != rst_q) fl_cnt <= 10'd0;                                  // the request to reset came or went: flush
    end
    // ------------------------------------------------------------------ the ui side
    localparam [1:0] U_IDLE = 2'd0, U_ISSUE = 2'd1, U_WAITR = 2'd2;
    reg [1:0]  us = U_IDLE;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg [26:0] u_addr = 27'd0;
    reg        u_wr = 1'b0;
    reg [3:0]  u_lane = 4'd0;
    reg [7:0]  u_byte = 8'd0;
    reg        c_en_i = 1'b0, c_wr_i = 1'b0;
    integer    li;
    always @(posedge ui_clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        if (ui_rst) begin
            us <= U_IDLE; c_en_i <= 1'b0; c_wr_i <= 1'b0;
        end else begin
            case (us)
            U_IDLE: if (rq_s[2] != rq_s[1]) begin
                u_addr <= rq_addr; u_wr <= rq_wr; u_lane <= rq_lane; u_byte <= rq_byte; us <= U_ISSUE;
            end
            U_ISSUE: begin
                c_en_i <= 1'b1;
                c_addr <= u_addr;
                if (u_wr) begin
                    c_cmd <= 3'b000; c_wr_i <= 1'b1;
                    for (li = 0; li < 16; li = li + 1) c_wdf_data[8 * li +: 8] <= u_byte;
                    c_wdf_mask <= 16'hFFFF;
                    c_wdf_mask[u_lane] <= 1'b0;
                end else begin
                    c_cmd <= 3'b001; c_wr_i <= 1'b0;
                end
                if (c_en_i && c_rdy) begin
                    c_en_i <= 1'b0; c_wr_i <= 1'b0;
                    if (u_wr) begin dn_tgl <= ~dn_tgl; us <= U_IDLE; end else us <= U_WAITR;
                end
            end
            U_WAITR: if (c_rd_valid) begin ui_line <= c_rd_data; dn_tgl <= ~dn_tgl; us <= U_IDLE; end
            default: us <= U_IDLE;
            endcase
        end
    end
    assign c_en = c_en_i;
    assign c_wdf_wren = c_wr_i;
endmodule
