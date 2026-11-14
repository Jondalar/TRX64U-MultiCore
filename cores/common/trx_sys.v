// The C64-clock part of our IO block 0x101A0000 (ARCH-lean-state-access §10):
// the pages whose registers live in the C64's clk (top.v sys_clk): 0 from
// 0x100, 1, 2, 3, 5, 7. trx_io.v
// (the app's clock) forwards their byte accesses here over a toggle
// handshake and acks when the answer is back; JTAG reaches them as 32-bit
// words through the JTAG page: dbg_addr 0x101000-0x1013FF = byte offset
// {JTAG_PAGE, dbg_addr[9:0], 00}, 0x101400 = JTAG_PAGE itself (from any
// page), 0x101401-0x101FFF read 0. JTAG word writes only (jtag_dbg op 2).
//
// 32-bit registers, little endian (ARCH §10 / T21 §5.1 rules): writing bytes
// 0-2 only collects them, writing byte 3 commits the word; reading byte 0
// latches the word (and makes its side effect: SB_DATA's increment), bytes
// 1-3 come from that latch. JTAG reads and writes whole words.
//
//   page 0 (offsets from 0x100; the app's 0x000-0x0FF are trx_io's)
//     000      ID          R    "TRX2" (JTAG only; the app reads trx_io's)
//     100      GATE_CTRL   RW   b0 FRZ: the app's C64_STOP pauses the whole
//                               machine (the freeze; the app's accesses to it are
//                               bus cycles, dma_win.v);
//                               0 (reset value): it pauses only the CPU, as
//                               before T12 B, so Gideon's freeze shows on the
//                               C64 screen (fallback until our firmware draws
//                               the menu, T12 P2). JTAG register 9 bit 4 still
//                               forces CPU-only. R: b8 halted, b9 gate shut
//     104      CAP_CTRL    RW   b7:0 frame capture every n frames (0 = none)
//     108      CAP_STAT    R    {frames whose snap did not come (16), CAP_GEN (16)}
//     10C      JTAG_PAGE   RW   b3:0 the page JTAG sees
//     110      RESUME_PHASE RW  b19:0 the next stop's end comes this many
//                               dot4x clocks after whole frames, once (for a
//                               restored VIC position, T20 §8.1 step 7); R:
//                               still pending (0 once used)
//     114      STOP_POS    RW   b5:0 cycle, b8 armed: stop on line 0 before
//                               the phi2 falling edge that ends that cycle
//                               (one shot; the machine stays stopped until
//                               b9 is written 1); R b16 stopped there
//     118-13F  debug: owner, counters, CYC_CMP (trx_dbg.v has the map)
//     140      TURBO_STAT  R    T29 (c64_turbo.v, TRX64-Ultimate/docs/specs/T29-turbo.md §12.7): {hold active
//                               b31, engine drives the CPU b30, badline timing in force b29, turbo ran
//                               since cleared b28, stops force normal speed b27, CPU cycles per PHI2
//                               b22:16, enable word b10:8, $D030 bit b7, speed index b3:0}
//     144      TURBO_CTRL  W    b0: clear the sticky "turbo ran" bit
//     148      TURBO_W1    R    the engine's state, live (the words chip 0 w1 of the state bus
//                               shows as shadows): {$D031 b31:24, $D030 bit b11, enable word b10:8,
//                               PREFER b7:0}
//     14C      TURBO_W2    R    {9'd0, reset hold left, PHI2 cycles}
//   page 3 debug (trx_dbg.v, drv_dbg.v have the map): word accesses go there
//   page 2 engine (trx_engine.v has the map): word accesses go there as they are
//   page 1 state bus (c64_core.v has the word map)
//     000      SB_ADDR     RW   b11:0 {chip, word}
//     004      SB_DATA     RW   the word at SB_ADDR; reading it (byte 0, or a
//                               JTAG read) increments SB_ADDR. W: the staged
//                               word at SB_ADDR (hidden-state words only,
//                               c64_core.v), then increments; refused unless
//                               the gate is shut (SB_STATUS b1)
//     008      SB_STATUS   R    b0 the gate is shut (the shadows follow the
//                               machine), b1 a write was refused; W: clears b1
//     00C      SB_LOAD     RW   b1 CPU, b3 CIA 1, b4 CIA 2: their staged
//                               words load at the next opening edge; R: armed
//   page 7 expansion port (slot_port.v, T16 §13.7)
//     000      SLOT_CTRL   RW   b1 ROM_WR_EN: ROML/ROMH may go low in a write
//                               cycle (reset 0: flash on the port is safe)
//     004      SLOT_STATUS R    b0 GAME pin, b1 EXROM, b2 DMA, b3 IRQ, b4 NMI
//                               (1 = high), b5 port RESET sensed, b11:8
//                               C64_BUS_EXTERNAL in effect, b15:12 INTERNAL,
//                               b16 armed, b17 fault, b18 ROM_WR_EN, b23:20
//                               fault cause (1 DMA, 2 DIR turn, 3 D out vs
//                               DIR, 4 output while idle), b31 1
//     008      SLOT_CNT0   R    {routed reads, writes with a port strobe}
//     00C      SLOT_CNT1   R    {armed cycles, VIC ultimax fetches}
//     010      SLOT_LAST   R    last port access {read, strobe 1 ROML 2 ROMH
//                               3 IO1 4 IO2, data, address}
//   page 5 app window, lens unit, text layer view (dma_win.v; SLIM-state-path.md)
//     000-01C  SH_CTRL, SH_STATUS, SH_ACCESSES, SH_REAL, SH_GCLK, SH_UNMAPPED, SH_LIST,
//              SH_COMMIT: the freeze session responder of T12 is gone (SLIM D1, D3):
//              writes are ignored, reads give 0
//     020      LK_REQ      W    the lens unit (T21 §3.4.1, CAPS2 b4): b15:0 address,
//                               b17:16 class (0 ROML, 1 ROMH, 2 IO1, 3 IO2), b18
//                               REAL: one bus cycle with the select forced (the
//                               machine must be paused) instead of the look
//     024      LK_RES      R    b7:0 data, b8 done, b9 hit, b10 ext (the real port
//                               serves it: take REAL), b11 data from a bus cycle,
//                               b12 refused (REAL, machine not paused or no cycle
//                               came), b31 busy
//     028      TX_VIEW     RW   what the C64 text layer (c64_text.v) takes for the VIC:
//                               b15 $D011 ECM, b14 BMM, b13 $D016 MCM, b12:6 $D018 bits
//                               7:1, b5:2 $D021 (background), b1:0 the CIA 2 bank pins
//                               (PRA | ~DDRA); reset value 0x0243 = text mode, screen $0400,
//                               charset $0800, background 0, bank 0 (the freeze menu).
//                               The app writes it before it shows the menu (TX_CTRL b0)
module trx_sys (
    input  wire        clk,
    // from trx_io.v (the app's clock): a toggle per byte access
    input  wire        rq_tgl,
    input  wire [15:0] rq_addr,
    input  wire        rq_we,
    input  wire  [7:0] rq_wdata,
    output reg         rs_tgl = 1'b0,
    output reg   [7:0] rs_rdata = 8'h00,
    // JTAG page: word address within the page, pulses
    input  wire        j_re,
    input  wire        j_we,
    input  wire [11:0] j_addr,
    input  wire [31:0] j_wdata,
    output reg  [31:0] j_rdata = 32'd0,
    output reg         j_rvalid = 1'b0,
    // controls
    output reg         gate_frz = 1'b0,
    output reg   [7:0] cap_every = 8'd0,
    input  wire [31:0] turbo_stat,
    input  wire [31:0] turbo_w1,
    input  wire [31:0] turbo_w2,
    output reg         turbo_ran_clr = 1'b0,
    output reg         slot_wr_en = 1'b0,
    input  wire [31:0] slot_status,
    input  wire [31:0] slot_cnt0,
    input  wire [31:0] slot_cnt1,
    input  wire [31:0] slot_last,
    // lens unit (dma_win.v; T21 §3.4.1): a request pulse, the result word
    output reg         lk_go = 1'b0,
    output reg  [18:0] lk_req = 19'd0,
    input  wire [31:0] lk_res,
    // restore: staged writes, loads, stop position, resume phase (c64_core.v)
    output reg         sb_we = 1'b0,
    output reg  [11:0] sb_waddr = 12'd0,
    output reg  [31:0] sb_wdata = 32'd0,
    output reg         sb_ldreq = 1'b0,
    output reg   [4:0] sb_ldmask = 5'd0,
    input  wire  [4:0] sb_ldarm,
    output reg         pos_set = 1'b0,
    output reg         pos_arm_in = 1'b0,
    output reg   [5:0] pos_cyc = 6'd0,
    output reg         pos_release = 1'b0,
    input  wire        pos_arm,
    input  wire        pos_hold,
    output reg  [19:0] resume_phase = 20'd0,
    output reg         rp_set = 1'b0,
    input  wire [19:0] rp_pend,
    // page 2 (trx_engine.v): one word access, pulses; p2_rdata from p2_addr
    output wire        p2_we,
    output wire        p2_re,
    output wire  [9:0] p2_addr,
    output wire [31:0] p2_wdata,
    input  wire [31:0] p2_rdata,
    // debug (trx_dbg.v in c64_core, drv_dbg.v): page 3 and page 0 0x118-0x13F,
    // one word access, pulses; dr_rdata from dr_addr = {page 3, word}
    output wire        dr_we,
    output wire        dr_re,
    output wire [10:0] dr_addr,
    output wire [31:0] dr_wdata,
    input  wire [31:0] dr_rdata,
    // state bus: shared by the SB_DATA reads here and the engine's capture sequencer
    input  wire        seq_sb_req,
    input  wire [11:0] seq_sb_addr,
    output wire        seq_sb_gnt,
    output wire [11:0] sb_addr,
    input  wire [31:0] sb_rdata,
    input  wire        sb_frozen,
    input  wire        halted,
    input  wire [15:0] cap_gen,
    input  wire [15:0] cap_nosnap,
    // the C64 text layer's VIC view (c64_text.v)
    output reg  [15:0] tx_view = 16'h0243
);
    reg  [11:0] sb_a = 12'd0;
    reg         sb_err = 1'b0;
    reg   [3:0] jtag_page = 4'd0;

    // ---- one 32-bit operation at a time --------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg         c_pend = 1'b0, j_pend = 1'b0;
    // The bytes of a 32-bit register are four byte accesses, and several masters (tasks of the firmware,
    // an app) interleave them: a write collects bytes 0-2 and commits on byte 3, a read latches the word
    // on byte 0 and gives bytes 1-3 from the latch. One buffer for everything let another master's
    // byte 0 replace the latch under a reader (done/busy of LK_RES read from another register: the lens
    // read $00) and another master's bytes under a writer (a lens request or a register of the other
    // master with the wrong bytes). Both are kept per register word now, for the four most recent words
    // (tag = word address); a byte access to a word with no entry takes the most recent one, as the
    // single buffer did.
    reg         wt_nxt = 1'b0, wt_last = 1'b0;
    reg         rt_nxt = 1'b0, rt_last = 1'b0;
    function [1:0] tfind(input [1:0] vld, input [27:0] tag, input [13:0] a);   // {hit, index}
        tfind = (vld[0] && tag[13:0]  == a) ? 2'b10 :
                (vld[1] && tag[27:14] == a) ? 2'b11 : 2'b00;
    endfunction
    reg         op_j;                       // the running op is JTAG's
    reg         j_we_q = 1'b0;
    always @(posedge clk) if (j_re || j_we) j_we_q <= j_we;
    reg         op_we;
    reg  [15:2] op_a;
    reg  [31:0] op_wd;
    localparam [2:0] O_IDLE = 3'd0, O_RUN = 3'd1, O_SB0 = 3'd2, O_SB1 = 3'd3, O_SB2 = 3'd4;
    reg   [2:0] ost = O_IDLE;
    assign sb_addr    = (seq_sb_req && ost != O_SB0) ? seq_sb_addr : sb_a;
    assign seq_sb_gnt = seq_sb_req && ost != O_SB0;

    wire op_dbg = op_a[15:12] == 4'd3 ||
                  (op_a[15:12] == 4'd0 && op_a[11:2] >= 10'h046 && op_a[11:2] < 10'h050);
    // plain registers' read value (pages 0, 1, 5)
    reg  [31:0] rv;
    always @(*) begin
        rv = 32'd0;
        case (op_a)
        14'h0000: rv = 32'h32585254;                            // "TRX2"
        14'h0040: rv = {22'd0, sb_frozen, halted, 7'd0, gate_frz};
        14'h0041: rv = {24'd0, cap_every};
        14'h0042: rv = {cap_nosnap, cap_gen};
        14'h0043: rv = {28'd0, jtag_page};
        14'h0044: rv = {12'd0, rp_pend};
        14'h0045: rv = {15'd0, pos_hold, 7'd0, pos_arm, 2'd0, pos_cyc};
        14'h0050: rv = turbo_stat;
        14'h0052: rv = turbo_w1;
        14'h0053: rv = turbo_w2;
        14'h0403: rv = {27'd0, sb_ldarm};
        14'h0400: rv = {20'd0, sb_a};
        14'h0402: rv = {30'd0, sb_err, sb_frozen};
        14'h1C00: rv = {30'd0, slot_wr_en, 1'b0};
        14'h1C01: rv = slot_status;
        14'h1C02: rv = slot_cnt0;
        14'h1C03: rv = slot_cnt1;
        14'h1C04: rv = slot_last;
        14'h1409: rv = lk_res;
        14'h140A: rv = {16'd0, tx_view};
        default: if (op_a[15:12] == 4'd2) rv = p2_rdata;
                 else if (op_dbg) rv = dr_rdata;
        endcase
    end
    assign p2_we    = ost == O_RUN && op_we && op_a[15:12] == 4'd2;
    assign p2_re    = ost == O_RUN && !op_we && op_a[15:12] == 4'd2;
    assign p2_addr  = op_a[11:2];
    assign p2_wdata = op_wd;
    assign dr_we    = ost == O_RUN && op_we && op_dbg;
    assign dr_re    = ost == O_RUN && !op_we && op_dbg;
    assign dr_addr  = {op_a[15:12] == 4'd3, op_a[11:2]};
    assign dr_wdata = op_wd;
    wire op_sb = op_a == 14'h0401;                              // SB_DATA

    wire rq_ev = rq_s[2] != rq_s[1];

    reg  [15:0] c_addr;                     // the app's byte access
    reg         c_we;
    reg   [7:0] c_wd;
    reg  [31:0] res;
    reg         res_v = 1'b0;
    // the entries of the byte access (rq_addr is stable from the toggle on), of the write that commits
    // (c_addr), and of the read that completes (op_a)
    wire [27:0] wt_tag, rt_tag;
    wire  [1:0] wt_vld, rt_vld;
    wire [47:0] wt_val;
    wire [63:0] rt_val;
    wire [1:0]  w_f   = tfind(wt_vld, wt_tag, rq_addr[15:2]);
    wire        w_idx = w_f[1] ? w_f[0] : wt_nxt;
    wire [1:0]  r_f   = tfind(rt_vld, rt_tag, rq_addr[15:2]);
    wire        r_idx = r_f[1] ? r_f[0] : rt_last;
    wire [31:0] r_hit = r_idx ? rt_val[63:32] : rt_val[31:0];
    wire [1:0]  c_f   = tfind(wt_vld, wt_tag, c_addr[15:2]);
    wire        c_idx = c_f[1] ? c_f[0] : wt_last;
    wire [23:0] c_wv  = c_idx ? wt_val[47:24] : wt_val[23:0];
    wire [1:0]  s_f   = tfind(rt_vld, rt_tag, op_a);
    wire        s_idx = s_f[1] ? s_f[0] : rt_nxt;
    // the two write entries (word tag, valid, bytes 0-2) and the two read latches, as flip-flops with enables
    // (no variable-index writes: they cost several times the LUTs)
    wire        wr_b02 = rq_ev && rq_we && rq_addr[1:0] != 2'd3;      // a byte 0-2 write: collect
    wire        rd_lat = res_v && !op_j && !op_we;                    // a read's word is complete: latch (not a write's ack)
    genvar gi;
    generate
        for (gi = 0; gi < 2; gi = gi + 1) begin : ent
            reg [13:0] wtag = 14'd0;
            reg        wv = 1'b0;
            reg [23:0] wval = 24'd0;
            reg [13:0] rtag = 14'd0;
            reg        rv = 1'b0;
            reg [31:0] rval = 32'd0;
            always @(posedge clk) begin
                if (wr_b02 && w_idx == gi) begin
                    wtag <= rq_addr[15:2];
                    wv   <= 1'b1;
                    if (rq_addr[1:0] == 2'd0) wval[7:0]   <= rq_wdata;
                    if (rq_addr[1:0] == 2'd1) wval[15:8]  <= rq_wdata;
                    if (rq_addr[1:0] == 2'd2) wval[23:16] <= rq_wdata;
                end
                if (rd_lat && s_idx == gi) begin
                    rtag <= op_a;
                    rv   <= 1'b1;
                    rval <= res;
                end
            end
        end
    endgenerate
    assign wt_tag = {ent[1].wtag, ent[0].wtag};
    assign wt_vld = {ent[1].wv, ent[0].wv};
    assign wt_val = {ent[1].wval, ent[0].wval};
    assign rt_tag = {ent[1].rtag, ent[0].rtag};
    assign rt_vld = {ent[1].rv, ent[0].rv};
    assign rt_val = {ent[1].rval, ent[0].rval};

    always @(posedge clk) begin
        rq_s     <= {rq_s[1:0], rq_tgl};
        j_rvalid <= 1'b0;
        lk_go    <= 1'b0;
        res_v    <= 1'b0;
        sb_we    <= 1'b0;
        sb_ldreq <= 1'b0;
        pos_set  <= 1'b0;
        pos_release <= 1'b0;
        rp_set   <= 1'b0;
        turbo_ran_clr <= 1'b0;

        // the app's byte accesses: collect, commit on byte 3, latch on byte 0
        if (rq_ev) begin
            c_addr <= rq_addr; c_we <= rq_we; c_wd <= rq_wdata;
            if (rq_we && rq_addr[1:0] != 2'd3) begin
                wt_last <= w_idx;
                if (!w_f[1]) wt_nxt <= ~wt_nxt;
                rs_tgl <= ~rs_tgl;
            end else if (!rq_we && rq_addr[1:0] != 2'd0) begin
                rs_rdata <= r_hit[8 * rq_addr[1:0] +: 8];
                rs_tgl   <= ~rs_tgl;
            end else
                c_pend <= 1'b1;
        end
        if (j_re || j_we) j_pend <= 1'b1;

        case (ost)
        O_IDLE:
            if (c_pend || j_pend) begin
                op_j  <= !c_pend;
                if (c_pend) begin
                    op_we <= c_we;
                    op_a  <= c_addr[15:2];
                    op_wd <= {c_wd, c_wv};
                    c_pend <= 1'b0;
                end else begin
                    op_we <= j_we_q;
                    op_a  <= j_addr == 12'h400 ? 14'h0043 :
                             j_addr[11:10] != 2'd0 ? 14'h3FFF : {jtag_page, j_addr[9:0]};
                    op_wd <= j_wdata;
                    j_pend <= 1'b0;
                end
                ost <= O_RUN;
            end
        O_RUN:
            if (op_we) begin
                case (op_a)
                14'h0040: gate_frz  <= op_wd[0];
                14'h0041: cap_every <= op_wd[7:0];
                14'h0043: jtag_page <= op_wd[3:0];
                14'h0400: sb_a      <= op_wd[11:0];
                14'h0401: if (sb_frozen) begin
                    sb_we    <= 1'b1;
                    sb_waddr <= sb_a;
                    sb_wdata <= op_wd;
                    sb_a     <= sb_a + 1'b1;
                end else
                    sb_err <= 1'b1;
                14'h0403: begin sb_ldreq <= 1'b1; sb_ldmask <= op_wd[4:0]; end
                14'h0044: begin resume_phase <= op_wd[19:0]; rp_set <= 1'b1; end
                14'h0045: begin
                    pos_set     <= 1'b1;
                    pos_arm_in  <= op_wd[8];
                    pos_cyc     <= op_wd[5:0];
                    pos_release <= op_wd[9];
                end
                14'h0402: sb_err    <= 1'b0;
                14'h0051: turbo_ran_clr <= op_wd[0];
                14'h1C00: slot_wr_en <= op_wd[1];
                14'h1408: begin lk_go <= 1'b1; lk_req <= op_wd[18:0]; end
                14'h140A: tx_view   <= op_wd[15:0];
                default: ;
                endcase
                res   <= 32'd0;
                res_v <= 1'b1;
                ost   <= O_IDLE;
            end else if (op_sb)
                ost <= O_SB0;
            else begin
                res   <= rv;
                res_v <= 1'b1;
                ost   <= O_IDLE;
            end
        O_SB0: ost <= O_SB1;                            // sb_a is on the bus this clock
        O_SB1: ost <= O_SB2;
        O_SB2: begin
            res   <= sb_rdata;
            res_v <= 1'b1;
            sb_a  <= sb_a + 1'b1;
            ost   <= O_IDLE;
        end
        default: ost <= O_IDLE;
        endcase

        if (res_v) begin
            if (op_j) begin
                j_rdata  <= res;
                j_rvalid <= !op_we;
            end else begin
                if (!op_we) begin
                    rt_last <= s_idx;
                    if (!s_f[1]) rt_nxt <= ~rt_nxt;
                end
                rs_rdata <= res[7:0];
                rs_tgl   <= ~rs_tgl;
            end
        end
    end
endmodule
