// The C64-clock part of the c64ref core's cw window (T103; copy of trx64/trx_sys.v cut down to what the stock C64U
// set has). c64ref_io.v (the app's clock) forwards the byte accesses of the registers that live in the C64's clk
// (top.v sys_clk) here over a toggle handshake and acks when the answer is back.
//
// 32-bit registers, little endian (ARCH §10 / T21 §5.1 rules): writing bytes 0-2 only collects them, writing byte 3
// commits the word; reading byte 0 latches the word, bytes 1-3 come from that latch.
//
//   page 0 (offsets from 0x100; the app's 0x000-0x0FF are c64ref_io's)
//     100      GATE_CTRL   RW   b0 FRZ: the app's C64_STOP pauses the whole machine (the freeze; the app's accesses
//                               to it are bus cycles, dma_win_c64ref.v); 0 (reset value): it pauses only the CPU.
//                               R: b8 halted, b9 gate shut
//     140      TURBO_STAT  R    T29 (c64_turbo_c64ref.v, docs/specs/T29-turbo.md §12.7): {hold active b31, engine drives the
//                               CPU b30, badline timing in force b29, turbo ran since cleared b28, stops force
//                               normal speed b27, CPU cycles per PHI2 b22:16, enable word b10:8, $D030 bit b7,
//                               speed index b3:0}
//     144      TURBO_CTRL  W    b0: clear the sticky "turbo ran" bit
//     148      TURBO_W1    R    the engine's state, live: {$D031 b31:24, $D030 bit b11, enable word b10:8, PREFER b7:0}
//     14C      TURBO_W2    R    {9'd0, reset hold left, PHI2 cycles}
//   page 5
//     028      TX_VIEW     RW   what the C64 text layer (c64_text.v) takes for the VIC: b15 $D011 ECM, b14 BMM, b13
//                               $D016 MCM, b12:6 $D018 bits 7:1, b5:2 $D021 (background), b1:0 the CIA 2 bank pins
//                               (PRA | ~DDRA); reset value 0x0243 = text mode, screen $0400, charset $0800,
//                               background 0, bank 0 (the freeze menu). The app writes it before it shows the menu
//   page 7 expansion port (slot_port_c64ref.v, T16 §13.7)
//     000      SLOT_CTRL   RW   b1 ROM_WR_EN: ROML/ROMH may go low in a write cycle (reset 0: flash on the port is safe)
//     004      SLOT_STATUS R    b0 GAME pin, b1 EXROM, b2 DMA, b3 IRQ, b4 NMI (1 = high), b5 port RESET sensed, b11:8
//                               C64_BUS_EXTERNAL in effect, b15:12 INTERNAL, b16 armed for a cartridge (not for a socket), b17 fault, b18 ROM_WR_EN,
//                               b23:20 fault cause (1 DMA, 2 DIR turn, 3 D out vs DIR, 4 output while idle), b31 1
//     008      SLOT_CNT0   R    {routed reads, writes with a port strobe}
//     00C      SLOT_CNT1   R    {armed cycles, VIC ultimax fetches}
//     010      SLOT_LAST   R    last port access {read, strobe 1 ROML 2 ROMH 3 IO1 4 IO2 5 SID socket 1
//                               6 SID socket 2, data, address}
//   every other address reads 0 and ignores writes. The state bus, the engine, the debugger, the capture control, the
//   restore registers, the lens unit and the JTAG page of the reference core are not in this core.
module c64ref_sys (
    input  wire        clk,
    // from c64ref_io.v (the app's clock): a toggle per byte access
    input  wire        rq_tgl,
    input  wire [15:0] rq_addr,
    input  wire        rq_we,
    input  wire  [7:0] rq_wdata,
    output reg         rs_tgl = 1'b0,
    output reg   [7:0] rs_rdata = 8'h00,
    // controls
    output reg         gate_frz = 1'b0,
    input  wire [31:0] turbo_stat,
    input  wire [31:0] turbo_w1,
    input  wire [31:0] turbo_w2,
    output reg         turbo_ran_clr = 1'b0,
    output reg         slot_wr_en = 1'b0,
    input  wire [31:0] slot_status,
    input  wire [31:0] slot_cnt0,
    input  wire [31:0] slot_cnt1,
    input  wire [31:0] slot_last,
    input  wire        gate_shut,
    input  wire        halted,
    // the C64 text layer's VIC view (c64_text.v)
    output reg  [15:0] tx_view = 16'h0243
);
    // ---- one 32-bit operation at a time --------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg         c_pend = 1'b0;
    // The bytes of a 32-bit register are four byte accesses, and several masters (tasks of the firmware, an app)
    // interleave them: a write collects bytes 0-2 and commits on byte 3, a read latches the word on byte 0 and gives
    // bytes 1-3 from the latch. Both are kept per register word, for the two most recent words (tag = word address);
    // a byte access to a word with no entry takes the most recent one.
    reg         wt_nxt = 1'b0, wt_last = 1'b0;
    reg         rt_nxt = 1'b0, rt_last = 1'b0;
    function [1:0] tfind(input [1:0] vld, input [27:0] tag, input [13:0] a);   // {hit, index}
        tfind = (vld[0] && tag[13:0]  == a) ? 2'b10 :
                (vld[1] && tag[27:14] == a) ? 2'b11 : 2'b00;
    endfunction
    reg         op_we;
    reg  [15:2] op_a;
    reg  [31:0] op_wd;
    localparam [1:0] O_IDLE = 2'd0, O_RUN = 2'd1;
    reg   [1:0] ost = O_IDLE;

    // plain registers' read value (pages 0, 5, 7)
    reg  [31:0] rv;
    always @(*) begin
        rv = 32'd0;
        case (op_a)
        14'h0040: rv = {22'd0, gate_shut, halted, 7'd0, gate_frz};
        14'h0050: rv = turbo_stat;
        14'h0052: rv = turbo_w1;
        14'h0053: rv = turbo_w2;
        14'h1C00: rv = {30'd0, slot_wr_en, 1'b0};
        14'h1C01: rv = slot_status;
        14'h1C02: rv = slot_cnt0;
        14'h1C03: rv = slot_cnt1;
        14'h1C04: rv = slot_last;
        14'h140A: rv = {16'd0, tx_view};
        default: ;
        endcase
    end

    wire rq_ev = rq_s[2] != rq_s[1];

    reg  [15:0] c_addr;                     // the app's byte access
    reg         c_we;
    reg   [7:0] c_wd;
    reg  [31:0] res;
    reg         res_v = 1'b0;
    // the entries of the byte access (rq_addr is stable from the toggle on), of the write that commits (c_addr), and of
    // the read that completes (op_a)
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
    // the two write entries (word tag, valid, bytes 0-2) and the two read latches, as flip-flops with enables (no
    // variable-index writes: they cost several times the LUTs)
    wire        wr_b02 = rq_ev && rq_we && rq_addr[1:0] != 2'd3;      // a byte 0-2 write: collect
    wire        rd_lat = res_v && !op_we;                             // a read's word is complete: latch (not a write's ack)
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
        res_v    <= 1'b0;
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

        case (ost)
        O_IDLE:
            if (c_pend) begin
                op_we <= c_we;
                op_a  <= c_addr[15:2];
                op_wd <= {c_wd, c_wv};
                c_pend <= 1'b0;
                ost <= O_RUN;
            end
        O_RUN: begin
            if (op_we)
                case (op_a)
                14'h0040: gate_frz  <= op_wd[0];
                14'h0051: turbo_ran_clr <= op_wd[0];
                14'h1C00: slot_wr_en <= op_wd[1];
                14'h140A: tx_view   <= op_wd[15:0];
                default: ;
                endcase
            res   <= op_we ? 32'd0 : rv;
            res_v <= 1'b1;
            ost   <= O_IDLE;
        end
        default: ost <= O_IDLE;
        endcase

        if (res_v) begin
            if (!op_we) begin
                rt_last <= s_idx;
                if (!s_f[1]) rt_nxt <= ~rt_nxt;
            end
            rs_rdata <= res[7:0];
            rs_tgl   <= ~rs_tgl;
        end
    end
endmodule
