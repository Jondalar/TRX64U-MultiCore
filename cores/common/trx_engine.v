// The engine (ARCH-lean-state-access §6, T20 §4): one DDR2 client for the
// frame ring. Two halves:
//
//   C64 clock (clk): page 2 of our IO block (registers below, reached through
//   trx_sys.v), the frame markers, the capture sequencer that copies the
//   shadows over the state bus after a frame capture while the machine runs
//   on, and the record former in front of one FIFO (512 x 67 bit, one block
//   RAM) into the ui_clk half, {F4 follows, tag (2 bits), record (64)}: tag 0 stream
//   0, 1 stream 1, 2 stream 2, 3 stream 1 cartridge RAM (old byte from
//   cart_fill); "F4 follows" is set only on a frame's F3 marker.
//   ui_clk: the drain, which writes every record into its stream's circular
//   area in DDR2 (one masked 128-bit MIG write per record), and the mover
//   (COPY, FILL), on mig_arb's engine client (e_*).
//
// Streams (record formats T20 §4.2, §6.1; the areas are fixed, below):
//   0  C64 RAM writes, 4 bytes {new, old, addr16} (TRX64 WriteRec, LE): the
//      machine's own (c64_mem port A) and, with LOG_CTRL b1, writes from
//      outside through the second port (JTAG, the app's window; APPLY is not
//      logged; marker flag b3)
//   1  other memories, 8 bytes (T20 §4.2 extended record): [27:0] address in
//      the space, [31:28] space, [39:32] old, [47:40] new, [62:48] phi2 cycle
//      count, low 15 bits (minus F2's: the cycle in the frame), [63] from
//      outside. Spaces: 1 colour RAM (low
//      nibbles; LOG_CTRL b3, port A: the machine's and the app window's bus
//      cycles; the app's colour RAM aperture through the second port as outside), 2 cartridge RAM (LOG_CTRL b2; cart_fill reads the old byte
//      from DDR2 first), 3 cartridge flash (no source: the software LOG_PUT
//      that T16.2 planned is gone, SLIM D6), the debug block's LOG action
//      records (trx_dbg.v dbg_put: any space the observer names); others
//      reserved (4 EEPROM, 5 drive A RAM, 6 I/O, 7 GCR, T20 §4.2)
//   2  frame records and chip blocks, 8 bytes each, type in bits 63:56:
//      F1 {F1, flags 8, CAP_GEN 16, frame 32}       a frame boundary (marker):
//         flags b0 snap (else line 16, no snap came), b1 a chip block of this
//         capture follows, b2 records were lost since the previous marker,
//         b3 the frame that ends here had writes from outside (RAM port B),
//         b4 the snap is a fetch that an interrupt drops (the IRQ/NMI sequence
//         follows: a .c64re boundary with the interrupt pending), b5 capture
//         skipped (the previous block was still being copied), b6 the REU
//         moved data in the frame that ends here (a re-run may differ)
//      F2 {F2, 32'd0, phi2 cycles 24}                cycle count at the boundary, low
//         24 bits (the app adds up the steps: a frame is about 19,656 cycles)
//      F3 {F3, 7'd0, line 9, 6'd0, x 10, 24'd0}      VIC position of the edge
//      F4 {F4, stream 1 count 28, stream 0 count 28} records of streams 0 and 1
//         written before the boundary (mod 2^28; written by the drain)
//      B1 {B1, table entries 8, 8'd0, CAP_GEN 16, frame 24}   chip block header
//         then the state-bus words in table order, two per record (word 2i in
//         bits 31:0), the last one padded with 0
//      B2 {B2, flags 8, words 16, CAP_GEN at the end 16, CAP_GEN at the start 16}
//         flags b0 torn (a capture came during the copy: do not use the block)
//   Every record of streams 0/1 made at or before the boundary edge is in DDR2
//   before the marker's F4 count, every later one after it: undo of frame k =
//   stream 0/1 records [F4 of k, F4 of k+1) in reverse.
//
// Page 2 (0x101A2000), 32-bit registers (trx_sys.v rules):
// Configuration registers are write-only (the app keeps what it wrote).
//   000 LOG_CTRL   RW  b0 on: a marker every frame, the chip block of every
//                      capture (CAP_CTRL), C64 RAM writes; b1 writes from
//                      outside (RAM and colour RAM second ports), b2 cartridge
//                      RAM, b3 colour RAM; b8 (W) restart: counts, positions
//                      and lost-record counters to 0
//   004 LOG_STAT   R   b0 on, b1 a chip block is being copied, b2 the mover
//                      runs, b3 records wait in the FIFO, b4 a cartridge RAM
//                      record's old byte did not come (written with old = new)
//   008 FRAME      R   markers made since the restart
//   00C DROPPED    R   b15:0 records lost (FIFO full while a tap's holding
//                      register was still busy)
//   (torn and skipped blocks are in stream 2 only: B2 b0, F1 b5)
//   020-044     (LOG_BASE/LOG_MASK of the three areas: gone, SLIM D4; writes are
//               ignored, reads give 0. Record n of stream s lies at
//               base | ((n * 4 or 8) & (size - 1)) with the fixed areas below)
//   028/038/048 LOG_CNT[0..2]   R  records written to DDR2 since the restart (mod 2^28)
//   060 CAP_TAB_IDX  W   b5:0 the table entry CAP_TAB_DATA reaches
//   064 CAP_TAB_DATA W   {count 7 (b22:16), first word {chip, word} 12 (b11:0)};
//                        writing increments CAP_TAB_IDX; refused while a
//                        block is copied (the table has one port)
//   068 CAP_TAB_LEN  W   b6:0 entries the sequencer walks (reset 9: the words
//                        built today, c64_core.v's map: 58 words)
//   050, 054    (LOG_PUT_LO/HI: gone, SLIM D6; writes are ignored)
//   080 ENG_CMD    W   b3:0 1 COPY (ENG_LEN bytes ENG_SRC -> ENG_DST), 2 FILL
//                      (ENG_ARG to ENG_LEN bytes at ENG_DST); R: b0 busy
//   084 ENG_SRC, 088 ENG_DST  W  DDR2 byte addresses (16-byte aligned)
//   08C ENG_LEN    W   bytes (multiple of 16)
//   090 ENG_ARG    W   FILL pattern (the 32-bit word repeated)
//   094 AP_START   W   APPLY: record index (28 bit) of the first stream 0 record
//                      to apply, in application order
//   098 AP_COUNT   W   records to apply (0: nothing)
//   09C AP_CMD     W   b0 1 undo (index downwards, the old byte) / 0 redo (index
//                      upwards, the new byte); starts the job unless one runs
//                      (COPY/FILL included). R: b0 busy. The bytes go through the
//                      RAM's second port (ap_*), not into the log; T20 §17.3
//   016 LOG_CNT[3] R   instruction records written to DDR2 since the restart (stream 3)
//   LOG_CTRL b4 (W) trace on, b9 (W) flush the pending instruction
// Stream 3 (T21 §4.11.1): the trace former. One record per instruction, 24 bytes =
// three 8-byte words (TRX64's DeltaEntry) through a FIFO of its own (64 x 512, one
// block RAM) into the fixed area 0x6800000, 349,525 records (1,048,575 words, so a
// record never straddles the wrap). The record of instruction I is written at the IB
// of I+1:
//   word 0  [23:0] phi2 count after I's fetch edge (what F2 of a snap on it carries)
//   word 1  [31:0] write_start (stream 0 records pushed when I began), [47:32] pc,
//           [63:48] write_count (stream 0 records pushed up to I+1's IB)
//   word 2  A, X, Y, SP, P (before I), opcode, b1, b2 (bus data of cycles 2 and 3)
// LOG_CNT[3] wraps at 349,525 * 768 so its residue is the slot.
// The areas (fixed, SLIM D4): T20 §5.1's ring data 0x5C00000-0x7FFFFFF, each aligned to
// its size: stream 2 4 MB at 0x5C00000, stream 1 8 MB at 0x6000000, stream 0 16 MB at
// 0x7000000, stream 3 8 MB at 0x6800000 (T21 §4.11.1). ui_clk values come back through a
// free-running handshake.
module trx_engine (
    input  wire         clk,                // C64 clock (sys_clk)
    // page 2 (trx_sys.v): a word access, pulses
    input  wire         r_we,
    input  wire         r_re,
    input  wire   [9:0] r_addr,             // word in the page
    input  wire  [31:0] r_wdata,
    output reg   [31:0] r_rdata,            // combinational from r_addr
    // c64_core
    output wire         log_on,
    output wire         seq_busy,           // c64_core's cap_block
    input  wire         mk,
    input  wire         mk_snap,
    input  wire         mk_cap,
    input  wire         mk_skip,
    input  wire         mk_intg,
    input  wire         reu_busy,
    input  wire   [9:0] mk_x,
    input  wire   [8:0] mk_line,
    input  wire         phi2_tick,
    // the debug block's log action (trx_dbg.v): a stream 1 record, one clock
    input  wire         dbg_put,
    input  wire  [63:0] dbg_rec,
    input  wire  [15:0] cap_gen,
    // the trace former's feed (trx_dbg.v, c64_core.v): tr_ib one clock after an IB,
    // pc and opcode of that fetch, the bus data of the two cycles after it, and the
    // registers with the previous write-back done (state bus chip 1 w4/w5)
    input  wire         tr_ib,
    input  wire  [15:0] tr_pc,
    input  wire   [7:0] tr_op,
    input  wire   [7:0] tr_b1,
    input  wire   [7:0] tr_b2,
    input  wire  [39:0] tr_regs,
    // state bus master (after trx_sys's SB_DATA reads)
    output reg          sb_req = 1'b0,
    output reg   [11:0] sb_addr = 12'd0,
    input  wire         sb_gnt,
    input  wire  [31:0] sb_rdata,
    // write-log taps: *_we in the clock whose edge writes, *_old valid from the
    // next clock (read-first block RAM); b = C64 RAM second port (outside),
    // d = colour RAM second port (outside: the app's colour RAM aperture)
    input  wire         wa_we,
    input  wire  [15:0] wa_addr,
    input  wire   [7:0] wa_new,
    input  wire   [7:0] wa_old,
    input  wire         wb_we,
    input  wire  [15:0] wb_addr,
    input  wire   [7:0] wb_new,
    input  wire   [7:0] wb_old,
    input  wire         wc_we,
    input  wire   [9:0] wc_addr,
    input  wire   [3:0] wc_new,
    input  wire   [3:0] wc_old,
    input  wire         wd_we,
    input  wire   [9:0] wd_addr,
    input  wire   [3:0] wd_new,
    input  wire   [3:0] wd_old,
    input  wire         wk_we,
    input  wire  [14:0] wk_addr,
    input  wire   [7:0] wk_new,
    output wire         cart_rbw,           // cart_fill: read the old byte first
    // APPLY: a byte for the RAM's second port, taken in the clock ap_gnt is high
    output wire         ap_req,
    output wire  [15:0] ap_addr,
    output wire   [7:0] ap_wdata,
    input  wire         ap_gnt,
    // events for trx_io's interrupt causes (toggles)
    output wire         eng_tgl,            // a mover or APPLY job is done
    output reg          gap_tgl = 1'b0,     // records were lost

    // ui_clk half: mig_arb's engine client; reads answer on rd_data
    input  wire         ui_clk,
    input  wire         ui_rst,
    output reg   [26:0] e_addr,
    output reg    [2:0] e_cmd,
    output reg          e_en = 1'b0,
    input  wire         e_rdy,
    output reg  [127:0] e_wdf_data,
    output reg   [15:0] e_wdf_mask,
    output reg          e_wdf_wren = 1'b0,
    input  wire [127:0] e_rd_data,
    input  wire         e_rd_data_valid,
    // cart_fill's old bytes of cartridge RAM writes, in write order
    input  wire         ob_v,
    input  wire  [14:0] ob_addr,
    input  wire   [7:0] ob_old
);
    // =============================================================================
    // C64 clock half
    // =============================================================================
    reg         on = 1'b0;
    reg   [2:0] tap_en = 3'd0;              // b1 outside, b2 cartridge RAM, b3 colour RAM
    reg         tr_en = 1'b0;               // b4 trace on
    wire        tr_on = on && tr_en;
    assign cart_rbw = on && tap_en[1];
    reg   [5:0] tab_idx = 6'd0;
    reg   [6:0] tab_len = 7'd9;
    reg  [18:0] tab [0:63];                 // {count 7, sb address 12}
    integer ti;
    initial begin
        for (ti = 0; ti < 64; ti = ti + 1) tab[ti] = 19'd0;
        tab[0] = {7'd1,  5'd0, 7'd0};       // glue
        tab[1] = {7'd3,  5'd1, 7'd0};       // 6510
        tab[2] = {7'd12, 5'd2, 7'd0};       // VIC
        tab[3] = {7'd5,  5'd3, 7'd0};       // CIA 1
        tab[4] = {7'd5,  5'd4, 7'd0};       // CIA 2
        tab[5] = {7'd1,  5'd7, 7'd0};       // cartridge
        tab[6] = {7'd7,  5'd1, 7'd8};       // 6510: every flip-flop
        tab[7] = {7'd12, 5'd3, 7'd8};       // CIA 1: every flip-flop
        tab[8] = {7'd12, 5'd4, 7'd8};       // CIA 2: every flip-flop
    end
    // the fixed areas (16-byte units): base | (offset & mask)
    wire [26:4] base [0:3];
    wire [26:4] mask [0:3];
    assign base[0] = 23'h700000; assign mask[0] = 23'h0FFFFF;
    assign base[1] = 23'h600000; assign mask[1] = 23'h07FFFF;
    assign base[2] = 23'h5C0000; assign mask[2] = 23'h03FFFF;
    assign base[3] = 23'h680000; assign mask[3] = 23'h07FFFF;
    reg  [26:0] e_src = 27'd0, e_dst = 27'd0, e_len = 27'd0;
    reg  [31:0] e_arg = 32'd0;
    reg   [1:0] e_op = 2'd0;
    reg         go_tgl = 1'b0, rst_tgl = 1'b0;
    reg  [31:0] frame = 32'd0;
    reg  [15:0] dropped = 16'd0;
    reg  [23:0] cycles = 24'd0;             // phi2 cycles, low 24 bits (the app adds them up)
    // from ui_clk (handshake below)
    reg  [27:0] st_cnt0 = 28'd0, st_cnt1 = 28'd0, st_cnt2 = 28'd0, st_cnt3 = 28'd0;
    reg         st_fifo = 1'b0, st_tfifo = 1'b0;
    reg         st_kmiss = 1'b0;
    assign log_on = on;
    reg         eng_tgl_m = 1'b0, ap_tgl = 1'b0;
    assign eng_tgl = eng_tgl_m ^ ap_tgl;
    reg  [27:0] ap_start = 28'd0, ap_count = 28'd0;
    reg         a_run = 1'b0, a_dir = 1'b0, ap_go_tgl = 1'b0;
    reg  [27:0] a_rem = 28'd0;
    reg   [1:0] a_lane = 2'd0;
    reg         a_pop = 1'b0;                   // between popping a word and its lanes
    reg         a_have = 1'b0;                  // a word is in ap_rd, its lanes go on
    wire        ap_empty;
    wire [127:0] ap_rd;
    wire [5:0]  ap_lvl;                         // words in the FIFO as ui_clk sees them
    wire [31:0] a_rec = ap_rd[32 * a_lane +: 32];
    wire        a_re = a_run && !a_pop && !a_have && !ap_empty;

    // ---- FIFO write side ----------------------------------------------------------
    reg         f_we = 1'b0;
    reg  [66:0] f_wd;
    wire        f_full;

    // ---- trace former (T21 §4.11.1) -------------------------------------------------
    // N: the instruction that started at the last IB (loaded two clocks after it,
    // the registers 12 clocks after it); E: the copy made at the next IB, whose
    // three words go out over three clocks. wcnt counts the stream 0 records pushed.
    reg  [27:0] wcnt = 28'd0;
    reg         tr_d = 1'b0;                // tr_ib a clock later
    reg  [15:0] n_pc = 16'd0;
    reg   [7:0] n_op = 8'd0;
    reg  [23:0] n_cyc = 24'd0;
    reg  [27:0] n_ws = 28'd0;
    reg  [39:0] n_regs = 40'd0;
    reg         n_v = 1'b0;
    reg   [3:0] n_dly = 4'd0;
    reg  [23:0] te_cyc = 24'd0;
    reg  [27:0] te_ws = 28'd0;
    reg  [15:0] te_wc = 16'd0, te_pc = 16'd0;
    reg  [39:0] te_regs = 40'd0;
    reg   [7:0] te_op = 8'd0, te_b1 = 8'd0, te_b2 = 8'd0;
    reg   [1:0] t_n = 2'd0;                 // the word going out next (1-3), 0 idle
    wire        t_full;
    wire        t_we = t_n != 2'd0 && !t_full;
    wire [63:0] t_wd = t_n == 2'd1 ? {40'd0, te_cyc} :
                       t_n == 2'd2 ? {te_wc, te_pc, 4'd0, te_ws} :
                                     {te_b2, te_b1, te_op, te_regs};

    // ---- frame marker: three records, after every older record ----------------------
    reg         m_pend = 1'b0;
    reg   [1:0] m_n = 2'd0;
    reg  [63:0] m_w0, m_w1, m_w2;           // shift out through m_w0
    reg         gap = 1'b0;                 // lost records since the last marker
    reg         ext = 1'b0;                 // outside writes in the frame
    reg         reu = 1'b0;                 // REU transfers in the frame
    reg         ep = 1'b0, m_ep = 1'b0;     // frame parity; the marker's

    // ---- taps: holding registers 0 C64 RAM, 1 outside RAM, 2 colour RAM,
    // 3 outside colour RAM, 4 cartridge RAM, 5 the debug block's record; each with the frame
    // parity of its write. 0 to 3 take their old byte a clock later (stage 1).
    reg   [3:0] s1_v = 4'd0, s1_ep = 4'd0;
    reg  [15:0] s1_a [0:3];
    reg   [7:0] s1_n [0:3];
    reg  [14:0] s1_c [0:3];
    reg   [5:0] h_v = 6'd0, h_ep = 6'd0;
    reg  [63:0] h_rec [0:5];
    wire        tep = ep ^ (mk && on);      // a write at the boundary's clock is the next frame's
    wire  [5:0] h_old = h_v & ~(h_ep ^ {6{m_ep}});
    wire  [3:0] s1_old = s1_v & ~(s1_ep ^ {4{m_ep}});
    wire  [5:0] h_ok = m_pend ? h_old : h_v;    // a marker waits: only the frame it ends
    wire  [2:0] h_sel = h_ok[0] ? 3'd0 : h_ok[1] ? 3'd1 : h_ok[2] ? 3'd2 :
                        h_ok[3] ? 3'd3 : h_ok[4] ? 3'd4 : 3'd5;
    wire        h_go = h_ok != 6'd0 && !f_full;
    wire  [1:0] h_tag = h_sel < 3'd2 ? 2'd0 : h_sel == 3'd4 ? 2'd3 : 2'd1;
    wire  [5:0] h_freed = h_go ? 6'd1 << h_sel : 6'd0;

    // ---- capture sequencer --------------------------------------------------------
    localparam [3:0] Q_IDLE = 4'd0, Q_HEAD = 4'd1, Q_ENT = 4'd2, Q_RD = 4'd3, Q_W1 = 4'd4,
                     Q_W2 = 4'd5, Q_PAD = 4'd6, Q_TAIL = 4'd7, Q_END = 4'd8;
    reg   [3:0] q = Q_IDLE;
    reg         q_go = 1'b0;                // a block to copy after the marker
    reg  [31:0] q_frame;
    reg  [15:0] q_gen0;
    reg   [6:0] q_ent, q_cnt;
    reg  [15:0] q_words;
    reg         q_half = 1'b0;              // word 2i is in q_lo
    reg  [31:0] q_lo;
    reg         q_v = 1'b0;                 // q_rec waits for the FIFO
    reg  [63:0] q_rec;
    wire  [5:0] tab_a = q == Q_IDLE ? tab_idx : q_ent[5:0];
    wire [18:0] q_tab = tab[tab_a];
    assign seq_busy = q != Q_IDLE || q_go;

    // the one FIFO write per clock: a chip block once started goes on to its
    // tail (stream 2 stays in one piece), then the marker (after every older
    // record), then the sequencer's next block
    wire m_take = m_pend && !f_full && q == Q_IDLE && h_old == 6'd0 && s1_old == 4'd0 && !h_go;
    wire q_take = q_v && !f_full && !h_go;
    wire busy;                              // a mover job runs (ui_clk side)

    integer i;
    always @(posedge clk) begin
        f_we <= 1'b0;
        if (phi2_tick) cycles <= cycles + 1'b1;

        // ---- trace former ----
        tr_d <= tr_ib;
        if (h_go && h_tag == 2'd0) wcnt <= wcnt + 1'b1;
        if (t_we) t_n <= t_n == 2'd3 ? 2'd0 : t_n + 1'b1;
        if (tr_ib) begin
            n_dly <= 4'd1;
            if (n_v && t_n == 2'd0) begin           // the previous instruction goes out
                te_cyc <= n_cyc;
                te_ws  <= n_ws;
                te_wc  <= wcnt[15:0] - n_ws[15:0];
                te_pc  <= n_pc;
                te_regs <= n_regs;
                te_op  <= n_op;
                te_b1  <= tr_b1;
                te_b2  <= tr_b2;
                t_n   <= 2'd1;
            end else if (n_v) begin                 // the last one is still going out
                dropped <= dropped + 1'b1;
                gap <= 1'b1;
                if (!gap) gap_tgl <= ~gap_tgl;
            end
        end else if (n_dly != 4'd0) begin
            n_dly <= n_dly + 1'b1;
            if (n_dly == 4'd12) begin
                n_regs <= tr_regs;
                n_dly  <= 4'd0;
            end
        end
        if (tr_d) begin                             // the new instruction
            n_pc  <= tr_pc;
            n_op  <= tr_op;
            n_cyc <= cycles;
            n_ws  <= wcnt;
            n_v   <= tr_on;
        end

        // ---- taps ----
        s1_v <= {wd_we && on && tap_en[0] && tap_en[2], wc_we && on && tap_en[2],
                 wb_we && on && tap_en[0], wa_we && on};
        s1_ep <= {4{tep}};
        s1_a[0] <= wa_addr;          s1_n[0] <= wa_new;
        s1_a[1] <= wb_addr;          s1_n[1] <= wb_new;
        s1_a[2] <= {6'd0, wc_addr};  s1_n[2] <= {4'd0, wc_new};
        s1_a[3] <= {6'd0, wd_addr};  s1_n[3] <= {4'd0, wd_new};
        for (i = 0; i < 4; i = i + 1) s1_c[i] <= cycles[14:0];
        h_v <= h_v & ~h_freed;
        for (i = 0; i < 4; i = i + 1)
            if (s1_v[i]) begin
                if (h_v[i] && !h_freed[i]) begin
                    dropped <= dropped + 1'b1;
                    gap <= 1'b1;
                    if (!gap) gap_tgl <= ~gap_tgl;
                end else begin
                    h_v[i]  <= 1'b1;
                    h_ep[i] <= s1_ep[i];
                end
            end
        if (s1_v[0] && (!h_v[0] || h_freed[0])) h_rec[0] <= {32'd0, s1_n[0], wa_old, s1_a[0]};
        if (s1_v[1] && (!h_v[1] || h_freed[1])) h_rec[1] <= {32'd0, s1_n[1], wb_old, s1_a[1]};
        if (s1_v[2] && (!h_v[2] || h_freed[2]))
            h_rec[2] <= {1'b0, s1_c[2], 4'd0, s1_n[2][3:0], 4'd0, wc_old, 4'd1, 18'd0, s1_a[2][9:0]};
        if (s1_v[3] && (!h_v[3] || h_freed[3]))
            h_rec[3] <= {1'b1, s1_c[3], 4'd0, s1_n[3][3:0], 4'd0, wd_old, 4'd1, 18'd0, s1_a[3][9:0]};
        if (wk_we && on && tap_en[1]) begin
            if (h_v[4] && !h_freed[4]) begin
                dropped <= dropped + 1'b1;
                gap <= 1'b1;
                if (!gap) gap_tgl <= ~gap_tgl;
            end else begin
                h_v[4]   <= 1'b1;
                h_ep[4]  <= tep;
                h_rec[4] <= {1'b0, cycles[14:0], wk_new, 8'd0, 4'd2, 13'd0, wk_addr};
            end
        end
        if (dbg_put && on) begin                                   // the debug block's log action
            if (h_v[5] && !h_freed[5]) begin
                dropped <= dropped + 1'b1;
                gap <= 1'b1;
                if (!gap) gap_tgl <= ~gap_tgl;
            end else begin
                h_v[5]   <= 1'b1;
                h_ep[5]  <= tep;
                h_rec[5] <= dbg_rec;
            end
        end
        if (mk && on) ep <= ~ep;
        if (h_go) begin
            f_we <= 1'b1;
            f_wd <= {1'b0, h_tag, h_rec[h_sel]};
        end

        // a boundary: the marker's words (CAP_GEN is the new one when it captured)
        if (mk && on) begin
            if (m_pend) begin
                dropped <= dropped + 1'b1;  // a whole frame of FIFO wait
                gap <= 1'b1;
                if (!gap) gap_tgl <= ~gap_tgl;
            end else begin
                m_pend <= 1'b1;
                m_n    <= 2'd0;
                m_w0   <= {8'hF1, 1'b0, reu, mk_skip, mk_intg, ext, gap, mk_cap, mk_snap, cap_gen, frame};
                m_w1   <= {8'hF2, 32'd0, cycles + phi2_tick};
                m_w2   <= {8'hF3, 7'd0, mk_line, 6'd0, mk_x, 24'd0};
                gap    <= 1'b0;
                ext    <= 1'b0;
                reu    <= 1'b0;
                m_ep   <= ep;
                q_go   <= mk_cap;
                q_frame <= frame;
                q_gen0  <= cap_gen;
            end
            frame <= frame + 1'b1;
        end

        if ((wb_we || wd_we) && on && tap_en[0]) ext <= 1'b1;
        if (reu_busy) reu <= 1'b1;

        if (m_take) begin
            f_we <= 1'b1;
            f_wd <= {m_n == 2'd2, 2'd2, m_w0};   // the F3 marker: an F4 follows
            m_w0 <= m_w1;
            m_w1 <= m_w2;
            m_n  <= m_n + 1'b1;
            if (m_n == 2'd2) m_pend <= 1'b0;
        end else if (q_take) begin
            f_we <= 1'b1;
            f_wd <= {1'b0, 2'd2, q_rec};
            q_v  <= 1'b0;
        end

        // the sequencer: header, the table's words (one state-bus read at a
        // time, data two clocks after the address is on the bus), tail
        sb_req <= 1'b0;
        case (q)
        Q_IDLE:
            if (q_go && !m_pend) begin
                q_go  <= 1'b0;
                q_rec <= {8'hB1, 1'b0, tab_len, 8'd0, q_gen0, q_frame[23:0]};
                q_v   <= 1'b1;
                q_ent <= 7'd0;
                q_words <= 16'd0;
                q_half  <= 1'b0;
                q <= Q_HEAD;
            end
        Q_HEAD:
            if (!q_v) q <= Q_ENT;
        Q_ENT:
            if (q_ent == tab_len) q <= q_half ? Q_PAD : Q_TAIL;
            else begin
                q_cnt <= 7'd0;
                q <= q_tab[18:12] == 7'd0 ? Q_ENT : Q_RD;
                if (q_tab[18:12] == 7'd0) q_ent <= q_ent + 1'b1;
            end
        Q_RD:
            if (!q_v) begin
                sb_req  <= 1'b1;
                sb_addr <= q_tab[11:0] + q_cnt;
                if (sb_req && sb_gnt) begin
                    sb_req <= 1'b0;
                    q <= Q_W1;
                end
            end
        Q_W1: q <= Q_W2;
        Q_W2: begin
            q_words <= q_words + 1'b1;
            q_half  <= !q_half;
            if (q_half) begin
                q_rec <= {sb_rdata, q_lo};
                q_v   <= 1'b1;
            end else
                q_lo <= sb_rdata;
            if (q_cnt + 1'b1 == q_tab[18:12]) begin
                q_ent <= q_ent + 1'b1;
                q <= Q_ENT;
            end else begin
                q_cnt <= q_cnt + 1'b1;
                q <= Q_RD;
            end
        end
        Q_PAD:
            if (!q_v) begin
                q_rec  <= {32'd0, q_lo};
                q_v    <= 1'b1;
                q_half <= 1'b0;
                q <= Q_TAIL;
            end
        Q_TAIL:
            if (!q_v) begin
                q_rec <= {8'hB2, 7'd0, cap_gen != q_gen0, q_words, cap_gen, q_gen0};
                q_v   <= 1'b1;
                q <= Q_END;
            end
        Q_END:
            if (!q_v) q <= Q_IDLE;
        default: q <= Q_IDLE;
        endcase
        if (!on) begin
            q <= Q_IDLE; q_go <= 1'b0; q_v <= 1'b0; m_pend <= 1'b0;
            h_v <= 6'd0;
        end

        // registers
        if (r_we)
            case (r_addr)
            10'h000: begin
                on <= r_wdata[0];
                tap_en <= r_wdata[3:1];
                tr_en <= r_wdata[4];
                if (r_wdata[9]) n_v <= 1'b0;
                if (r_wdata[8]) begin
                    rst_tgl <= ~rst_tgl;
                    frame <= 32'd0; dropped <= 16'd0;
                    gap <= 1'b0; ext <= 1'b0;
                    wcnt <= 28'd0; n_v <= 1'b0;
                end
            end
            10'h018: tab_idx <= r_wdata[5:0];
            10'h019: if (q == Q_IDLE) begin
                tab[tab_a] <= {r_wdata[22:16], r_wdata[11:0]};
                tab_idx <= tab_idx + 1'b1;
            end
            10'h01A: tab_len <= r_wdata[6:0] > 7'd64 ? 7'd64 : r_wdata[6:0];
            10'h020: if (!busy && !a_run && r_wdata[3:0] != 4'd0) begin
                e_op   <= r_wdata[1:0];
                go_tgl <= ~go_tgl;
            end
            10'h021: e_src <= {r_wdata[26:4], 4'd0};
            10'h022: e_dst <= {r_wdata[26:4], 4'd0};
            10'h023: e_len <= {r_wdata[26:4], 4'd0};
            10'h024: e_arg <= r_wdata;
            10'h025: ap_start <= r_wdata[27:0];
            10'h026: ap_count <= r_wdata[27:0];
            default: ;
            endcase
    end

    // ---- APPLY, the sys_clk half (T20 §17.3): pops the 128-bit words the ui_clk
    // half read, applies their lanes in direction order through ap_*
    assign ap_req   = a_run && a_have;
    assign ap_addr  = a_rec[15:0];
    assign ap_wdata = a_dir ? a_rec[23:16] : a_rec[31:24];
    always @(posedge clk) begin
        if (a_re) begin a_pop <= 1'b1; end      // ap_rd is valid the clock after
        if (a_pop) begin a_pop <= 1'b0; a_have <= 1'b1; end
        if (ap_req && ap_gnt) begin
            a_rem <= a_rem - 1'b1;
            if (a_rem == 28'd1) begin
                a_run <= 1'b0; a_have <= 1'b0;
                ap_tgl <= ~ap_tgl;
            end else if (a_dir ? a_lane == 2'd0 : a_lane == 2'd3) begin
                a_have <= 1'b0;
                a_lane <= a_dir ? 2'd3 : 2'd0;
            end else
                a_lane <= a_dir ? a_lane - 1'b1 : a_lane + 1'b1;
        end
        if (r_we && r_addr == 10'h027 && !a_run && !busy && ap_count != 28'd0) begin   // refused while a COPY/FILL runs
            a_run <= 1'b1; a_dir <= r_wdata[0]; a_rem <= ap_count; a_lane <= ap_start[1:0];
            a_have <= 1'b0; a_pop <= 1'b0;
            ap_go_tgl <= ~ap_go_tgl;
        end
    end

    always @(*) begin
        r_rdata = 32'd0;
        case (r_addr)
        10'h001: r_rdata = {26'd0, st_tfifo, st_kmiss, st_fifo, busy, seq_busy, on};
        10'h002: r_rdata = frame;
        10'h003: r_rdata = {16'd0, dropped};
        10'h00A: r_rdata = {4'd0, st_cnt0};
        10'h00E: r_rdata = {4'd0, st_cnt1};
        10'h012: r_rdata = {4'd0, st_cnt2};
        10'h016: r_rdata = {4'd0, st_cnt3};
        10'h020: r_rdata = {31'd0, busy};
        10'h027: r_rdata = {31'd0, a_run};
        default: ;
        endcase
    end

    // ---- status from ui_clk: free-running toggle handshake --------------------------
    reg         st_req = 1'b0;
    wire        st_ack;
    (* ASYNC_REG = "TRUE" *) reg [2:0] st_ack_s = 3'b000;
    reg  [27:0] u_cnt0_h, u_cnt1_h, u_cnt2_h, u_cnt3_h;   // ui_clk, stable while st_req == st_ack
    reg         u_fifo_h = 1'b0, u_tfifo_h = 1'b0, u_done_h = 1'b0;
    reg         u_kmiss_h = 1'b0;
    reg         done_s = 1'b0;              // the go toggle of the last job done
    assign busy = go_tgl != done_s;
    always @(posedge clk) begin
        st_ack_s <= {st_ack_s[1:0], st_ack};
        if (st_ack_s[2] == st_req) begin
            st_cnt0 <= u_cnt0_h; st_cnt1 <= u_cnt1_h; st_cnt2 <= u_cnt2_h; st_cnt3 <= u_cnt3_h;
            st_fifo <= u_fifo_h; st_tfifo <= u_tfifo_h;
            st_kmiss <= u_kmiss_h;
            done_s  <= u_done_h;
            if (u_done_h != done_s) eng_tgl_m <= ~eng_tgl_m;
            st_req  <= ~st_req;
        end
    end

    // =============================================================================
    // FIFO (C64 clock -> ui_clk)
    // =============================================================================
    wire        f_empty;
    wire        f_re;                       // the drain takes the next record (D_IDLE)
    wire [66:0] f_rd;
    trx_afifo #(.W(67), .AW(9)) fifo (
        .wclk(clk), .we(f_we), .wd(f_wd), .full(f_full),
        .rclk(ui_clk), .re(f_re), .rd(f_rd), .empty(f_empty), .wlevel()
    );
    // the trace former's words (T21 §4.11.1)
    wire        t_empty;
    wire        t_re;
    wire [63:0] t_rd;
    trx_afifo #(.W(64), .AW(9)) tfifo (
        .wclk(clk), .we(t_we), .wd(t_wd), .full(t_full),
        .rclk(ui_clk), .re(t_re), .rd(t_rd), .empty(t_empty), .wlevel()
    );

    // =============================================================================
    // ui_clk half
    // =============================================================================
    (* ASYNC_REG = "TRUE" *) reg [2:0] u_go_s = 3'b000, u_rst_s = 3'b000, u_req_s = 3'b000;
    reg         u_ack = 1'b0;
    assign st_ack = u_ack;
    reg  [27:0] u_cnt [0:3];                // [3]: stream 3's word index, 0 to 1,048,574
    reg  [27:0] u_rc3 = 28'd0;              // stream 3 records written (wraps at 349,525 * 768)
    reg   [1:0] u_ph3 = 2'd0;               // words of the record so far
    reg  [26:4] u_off = 23'd0;              // mover: 16-byte words done
    reg   [1:0] u_op;
    reg         u_busy = 1'b0, u_job = 1'b0, u_done = 1'b0;
    integer s;
    initial begin
        for (s = 0; s < 4; s = s + 1) u_cnt[s] = 28'd0;
    end

    localparam [3:0] D_IDLE = 4'd0, D_POP = 4'd1, D_WR = 4'd2, D_F4 = 4'd3, D_MRD = 4'd4,
                     D_MWAIT = 4'd5, D_MWR = 4'd6, D_K = 4'd7, D_MFILL = 4'd8, D_TPOP = 4'd9;

    // cart_fill's old bytes, 16 deep, in write order; emptied while the
    // cartridge tap is off. A cartridge record takes the first one with its
    // address (older ones are from writes made before the tap was on).
    (* ASYNC_REG = "TRUE" *) reg [1:0] u_rbw_s = 2'b00;
    reg  [22:0] kq [0:15];
    reg   [3:0] kq_wr = 4'd0, kq_rd = 4'd0;
    wire        kq_empty = kq_wr == kq_rd;
    wire [22:0] kq_head = kq[kq_rd];
    reg   [7:0] k_wait = 8'd0;
    reg         u_kmiss = 1'b0;
    reg   [3:0] d = D_IDLE;
    // APPLY (T20 §17.3): reads the stream 0 words of the job, in order, with
    // the credit rule inflight + FIFO level < 25 (the FIFO holds 32)
    (* ASYNC_REG = "TRUE" *) reg [2:0] u_ap_s = 3'b000;
    reg         u_ap = 1'b0, u_ap_dir = 1'b0;
    reg  [25:0] u_ap_w = 26'd0, u_ap_left = 26'd0;      // next word index, words to read
    reg   [5:0] u_ap_iss = 6'd0, u_ap_ret = 6'd0;
    wire  [5:0] u_ap_infl = u_ap_iss - u_ap_ret;
    wire        u_ap_credit = {1'b0, u_ap_infl} + {1'b0, ap_lvl} < 7'd25;
    wire [27:0] u_ap_last = ap_start + (a_dir ? 28'd1 - ap_count : ap_count - 28'd1);
    // APPLY's words (ui_clk -> C64 clock)
    trx_afifo #(.W(128), .AW(5)) apfifo (
        .wclk(ui_clk), .we(e_rd_data_valid && u_ap), .wd(e_rd_data), .full(),
        .rclk(clk), .re(a_re), .rd(ap_rd), .empty(ap_empty), .wlevel(ap_lvl)
    );
    // the mover's address: source for the read (issued from D_IDLE), else destination
    wire [26:4] u_a = (d == D_IDLE ? e_src[26:4] : e_dst[26:4]) + u_off;
    reg   [1:0] d_st;                       // the record's stream
    reg  [63:0] d_rec;
    reg         d_f4 = 1'b0;                // after this record, the F4 counts

    // a record's place: 4-byte records in stream 0 (d_rec holds two copies)
    wire        w_four = d_st == 2'd0;
    wire [30:0] w_off  = w_four ? {1'b0, u_cnt[d_st], 2'b00} : {u_cnt[d_st], 3'b000};
    wire [26:0] w_addr = {base[d_st] | (w_off[26:4] & mask[d_st]), w_off[3:0]};
    assign f_re = d == D_IDLE && !f_empty;
    assign t_re = d == D_IDLE && f_empty && !t_empty;

    always @(posedge ui_clk) begin
        u_go_s  <= {u_go_s[1:0], go_tgl};
        u_rst_s <= {u_rst_s[1:0], rst_tgl};
        u_req_s <= {u_req_s[1:0], st_req};

        if (u_rst_s[2] != u_rst_s[1]) begin
            for (s = 0; s < 4; s = s + 1) u_cnt[s] <= 28'd0;
            u_rc3 <= 28'd0; u_ph3 <= 2'd0;
            u_kmiss <= 1'b0;
        end
        u_rbw_s <= {u_rbw_s[0], cart_rbw};
        if (ob_v) begin
            kq[kq_wr] <= {ob_addr, ob_old};
            if (kq_wr + 1'b1 != kq_rd) kq_wr <= kq_wr + 1'b1;
        end
        if (!u_rbw_s[1]) kq_rd <= kq_wr;
        if (u_req_s[2] != u_ack) begin
            u_cnt0_h <= u_cnt[0]; u_cnt1_h <= u_cnt[1]; u_cnt2_h <= u_cnt[2]; u_cnt3_h <= u_rc3;
            u_fifo_h <= !f_empty; u_tfifo_h <= !t_empty; u_done_h <= u_done; u_kmiss_h <= u_kmiss;
            u_ack    <= u_req_s[2];
        end
        if (u_go_s[2] != u_go_s[1]) begin   // the C64 side sends one job at a time
            u_busy <= 1'b1;
            u_job  <= u_go_s[1];
            u_op   <= e_op;
            u_off  <= 23'd0;
        end

        // APPLY: a job starts on the toggle (its registers are still), reads come back
        // in order into the FIFO
        u_ap_s <= {u_ap_s[1:0], ap_go_tgl};
        if (u_ap_s[2] != u_ap_s[1]) begin
            u_ap      <= 1'b1;
            u_ap_dir  <= a_dir;
            u_ap_w    <= ap_start[27:2];
            u_ap_left <= (a_dir ? ap_start[27:2] - u_ap_last[27:2] : u_ap_last[27:2] - ap_start[27:2]) + 26'd1;
            u_ap_iss  <= 6'd0;
            u_ap_ret  <= 6'd0;
        end
        if (e_rd_data_valid && u_ap) u_ap_ret <= u_ap_ret + 1'b1;
        if (u_ap && u_ap_left == 26'd0 && u_ap_iss == u_ap_ret) u_ap <= 1'b0;

        if (e_en && e_rdy) begin
            e_en <= 1'b0;
            e_wdf_wren <= 1'b0;
        end

        case (d)
        D_IDLE:
            if (!f_empty) begin
                d <= D_POP;
            end else if (!t_empty) begin
                d <= D_TPOP;
            end else if (u_busy && !e_en) begin
                if (u_off == e_len[26:4]) begin
                    u_busy <= 1'b0;
                    u_done <= u_job;
                end else if (u_op == 2'd2) begin
                    d_rec <= {2{e_arg}};
                    d <= D_MFILL;
                end else if (u_op == 2'd1) begin
                    e_addr <= {u_a, 3'b000};
                    e_cmd  <= 3'b001;
                    e_en <= 1'b1;
                    d <= D_MRD;
                end else begin
                    u_busy <= 1'b0;
                    u_done <= u_job;
                end
            end else if (u_ap && !e_en && u_ap_left != 26'd0 && u_ap_credit) begin
                e_addr <= {base[0] | (u_ap_w[22:0] & mask[0]), 3'b000};
                e_cmd  <= 3'b001;
                e_en   <= 1'b1;
                u_ap_w    <= u_ap_dir ? u_ap_w - 26'd1 : u_ap_w + 26'd1;
                u_ap_left <= u_ap_left - 26'd1;
                u_ap_iss  <= u_ap_iss + 1'b1;
            end
        D_POP: begin                        // the FIFO's data is out now
            d_st  <= f_rd[65:64] == 2'd3 ? 2'd1 : f_rd[65:64];
            d_rec <= f_rd[65:64] == 2'd0 ? {2{f_rd[31:0]}} : f_rd[63:0];
            d_f4  <= f_rd[66];              // a frame's F3 marker, not a block word
            k_wait <= 8'd0;
            d <= f_rd[65:64] == 2'd3 ? D_K : D_WR;
        end
        D_TPOP: begin                       // a trace word: stream 3's next 8 bytes
            d_st  <= 2'd3;
            d_rec <= t_rd;
            d_f4  <= 1'b0;
            d <= D_WR;
        end
        D_K:                                // a cartridge RAM record: its old byte
            if (!kq_empty) begin
                kq_rd <= kq_rd + 1'b1;
                if (kq_head[22:8] == d_rec[14:0]) begin
                    d_rec[39:32] <= kq_head[7:0];
                    d <= D_WR;
                end
            end else if (k_wait == 8'hFF) begin
                d_rec[39:32] <= d_rec[47:40];   // not come: old = new, flagged
                u_kmiss <= 1'b1;
                d <= D_WR;
            end else
                k_wait <= k_wait + 1'b1;
        D_WR:
            if (!e_en) begin
                e_addr <= {w_addr[26:4], 3'b000};
                e_cmd  <= 3'b000;
                e_wdf_data <= {2{d_rec}};
                e_wdf_mask <= w_four ? ~(16'h000F << {w_addr[3:2], 2'b00})
                                     : ~(16'h00FF << {w_addr[3], 3'b000});
                e_en <= 1'b1; e_wdf_wren <= 1'b1;
                if (d_st == 2'd3) begin
                    u_cnt[3] <= u_cnt[3] == 28'd1048574 ? 28'd0 : u_cnt[3] + 1'b1;
                    u_ph3 <= u_ph3 == 2'd2 ? 2'd0 : u_ph3 + 1'b1;
                    if (u_ph3 == 2'd2) u_rc3 <= u_rc3 == 28'd268435199 ? 28'd0 : u_rc3 + 1'b1;
                end else
                    u_cnt[d_st] <= u_cnt[d_st] + 1'b1;
                if (d_f4) begin
                    d_f4  <= 1'b0;
                    d_rec <= {8'hF4, u_cnt[1], u_cnt[0]};
                    d <= D_F4;
                end else
                    d <= D_IDLE;
            end
        D_F4: d <= D_WR;                    // d_st stays 2
        D_MFILL: begin
            e_addr <= {u_a, 3'b000};
            e_cmd  <= 3'b000;
            e_wdf_data <= {2{d_rec}};
            e_wdf_mask <= 16'h0000;
            e_en <= 1'b1; e_wdf_wren <= 1'b1;
            d <= D_MWR;
        end
        D_MRD:
            if (!e_en) d <= D_MWAIT;
        D_MWAIT:
            if (e_rd_data_valid) begin
                e_addr <= {u_a, 3'b000};
                e_cmd  <= 3'b000;
                e_wdf_data <= e_rd_data;
                e_wdf_mask <= 16'h0000;
                e_en <= 1'b1; e_wdf_wren <= 1'b1;
                d <= D_MWR;
            end
        D_MWR:
            if (!e_en) begin
                u_off  <= u_off + 1'b1;
                d <= D_IDLE;
            end
        default: d <= D_IDLE;
        endcase

        if (ui_rst) begin
            e_en <= 1'b0; e_wdf_wren <= 1'b0;
            d <= D_IDLE;
        end
    end
endmodule

// Dual-clock FIFO: W bits, 2^AW entries (block RAM), Gray-coded pointers.
// rd is valid the clock after re with !empty.
module trx_afifo #(
    parameter W  = 67,
    parameter AW = 9
) (
    input  wire         wclk,
    input  wire         we,
    input  wire [W-1:0] wd,
    output wire         full,
    input  wire         rclk,
    input  wire         re,
    output reg  [W-1:0] rd,
    output wire         empty,
    output wire [AW:0]  wlevel             // entries written minus the read pointer wclk last saw
);
    (* ram_style = "block" *) reg [W-1:0] mem [0:(1 << AW) - 1];
    reg  [AW:0] wp = 0, wp_g = 0, rp = 0, rp_g = 0;
    (* ASYNC_REG = "TRUE" *) reg [AW:0] rp_g1 = 0, rp_g2 = 0, wp_g1 = 0, wp_g2 = 0;
    wire [AW:0] wp_n = wp + 1'b1;
    wire [AW:0] rp_n = rp + 1'b1;
    assign full  = wp_g == {~rp_g2[AW:AW-1], rp_g2[AW-2:0]};
    assign empty = rp_g == wp_g2;
    function [AW:0] gray2bin(input [AW:0] g);
        integer gi;
        begin
            gray2bin[AW] = g[AW];
            for (gi = AW - 1; gi >= 0; gi = gi - 1) gray2bin[gi] = gray2bin[gi + 1] ^ g[gi];
        end
    endfunction
    wire [AW:0] rp_b2 = gray2bin(rp_g2);
    assign wlevel = wp - rp_b2;
    always @(posedge wclk) begin
        rp_g1 <= rp_g; rp_g2 <= rp_g1;
        if (we && !full) begin
            mem[wp[AW-1:0]] <= wd;
            wp   <= wp_n;
            wp_g <= wp_n ^ (wp_n >> 1);
        end
    end
    always @(posedge rclk) begin
        wp_g1 <= wp_g; wp_g2 <= wp_g1;
        if (re && !empty) begin
            rd   <= mem[rp[AW-1:0]];
            rp   <= rp_n;
            rp_g <= rp_n ^ (rp_n >> 1);
        end
    end
endmodule
