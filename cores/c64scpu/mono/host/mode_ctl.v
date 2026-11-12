// System Mode (T40, docs/specs/T40-ntsc.md §9): PAL, NTSC, PAL-60, NTSC-50.
//
// The app writes C64_VIDEOFORMAT (ultimate_sys.vhd, video_fmt); its bit 5
// (CYCLES_65) and bit 0 (NTSC_ENCODING) give the mode:
//   vmode 0 PAL (63 cycles, 312 lines), 1 NTSC (65 x 263), 2 PAL-60 (65 x 263),
//   3 NTSC-50 (63 x 312).
// When the requested mode differs from the one in use, this runs the switch
// (all in clk50, the crystal, which never stops):
//   1 halt the machine (C64 and drive A); go on when halted, when the C64 is
//     in reset (it does not halt there) or after 84 ms;
//   2 park the app's I2C master between two transactions (app_i2c hold) and
//     the keyboard scan (kb_pause); go on when both are parked or after 40 ms;
//   3 stop sys and sid2x (sys_en, their BUFGCEs) and keep the lock loss away
//     from sys_rst (reconf);
//   4 reprogram mmcm_sys over its DRP (MMCM reset, read-modify-write of the
//     registers below, release, wait for its lock and mmcm_ref's), start the
//     clocks again;
//   5 the lock loss has restarted cdce_i2c, which now writes the synthesizer
//     setting of the new family (cdce_v60) and holds the GTPs in reset until
//     then; wait for it (i2c_done);
//   6 release the I2C buses, hand the new mode out (vmode) and the halt back.
// vmode changes only at the end; c64_core restarts the VIC's timing with it
// when the machine runs again.
//
// DRP words: AMD's XAPP888 functions (mmcm_pll_drp_func_7s_mmcm.vh) evaluated
// by tools/mmcm_drp_gen.v for D / M / O0 / O1 (O1 = O0 / 2, the SID engine's
// 2 x dot4x):
//   0 PAL     3 / 56.75  / 30 / 15   31,527,778 Hz
//   1 NTSC    3 / 47.125 / 24 / 12   32,725,694 Hz
//   2 PAL-60  3 / 43.25  / 22 / 11   32,765,152 Hz
//   3 NTSC-50 2 / 35.375 / 28 / 14   31,584,821 Hz
// Every read in a switch also checks the register against the table of the
// mode in use (the static PAL setting the first time): mism, with the address
// and value of the first difference.
// status  {switches done, state, requested mode, vmode, cdce_v60, sys_en, mism,
//          timeouts {lock/I2C, buses, halt}, 0, mism address}
// status2 {mism value, duration of the last switch in units of 1024 clocks}
module mode_ctl (
    input  wire        clk,             // 50 MHz
    input  wire        rst,
    input  wire  [7:0] fmt,             // C64_VIDEOFORMAT (any clock, quasi-static)
    output reg         halt_req = 1'b0,
    input  wire        halted,          // any clock
    input  wire        in_reset,        // the C64 CPU is held in reset (any clock)
    output reg         i2c_hold = 1'b0, // app_i2c: park between transactions
    input  wire        i2c_parked,      // app_i2c (any clock)
    output reg         kb_pause = 1'b0,
    input  wire        kb_parked,       // any clock
    input  wire        i2c_done,        // cdce_i2c (clk)
    output reg         cdce_v60 = 1'b0, // the synthesizer setting to write: 60 Hz family
    output reg         sys_en = 1'b1,   // sys and sid2x run
    output reg         reconf = 1'b0,   // mmcm_sys is being reconfigured
    output reg         mmcm_rst = 1'b0,
    output reg   [6:0] daddr = 7'd0,
    output reg  [15:0] di = 16'd0,
    input  wire [15:0] dout,
    output reg         den = 1'b0,
    output reg         dwe = 1'b0,
    input  wire        drdy,
    input  wire        sys_locked,      // async
    input  wire        ref_locked,      // async
    output reg   [1:0] vmode = 2'd0,    // the mode in use
    output wire [31:0] status,
    output wire [31:0] status2
);
    // ---- the requested mode ----------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [7:0] fmt_s1 = 8'h00, fmt_s2 = 8'h00;
    (* ASYNC_REG = "TRUE" *) reg [1:0] halted_s = 2'b00, inrst_s = 2'b00, ipark_s = 2'b00,
                                       kpark_s = 2'b00, slock_s = 2'b00, rlock_s = 2'b00;
    reg  [7:0] fmt_q = 8'h00;
    reg  [3:0] stable = 4'd0;
    reg  [1:0] want = 2'd0;
    // {CYCLES_65 xor NTSC_ENCODING, NTSC_ENCODING}: 63/PAL 0, 65/NTSC 1, 65/PAL 2, 63/NTSC 3
    wire [1:0] fmt_mode = {fmt_s2[5] != fmt_s2[0], fmt_s2[0]};
    always @(posedge clk) begin
        fmt_s1 <= fmt; fmt_s2 <= fmt_s1;
        halted_s <= {halted_s[0], halted};
        inrst_s  <= {inrst_s[0], in_reset};
        ipark_s  <= {ipark_s[0], i2c_parked};
        kpark_s  <= {kpark_s[0], kb_parked};
        slock_s  <= {slock_s[0], sys_locked};
        rlock_s  <= {rlock_s[0], ref_locked};
        // the bits of one write may arrive a clock apart: take them when steady
        fmt_q  <= fmt_s2;
        stable <= (fmt_s2 != fmt_q) ? 4'd0 : (stable == 4'd15 ? stable : stable + 4'd1);
        if (stable == 4'd15) want <= fmt_mode;
        if (rst) begin want <= 2'd0; stable <= 4'd0; end
    end
    function v60(input [1:0] m); v60 = (m == 2'd1) || (m == 2'd2); endfunction

    // ---- DRP words (tools/mmcm_drp_gen.v) ----------------------------------------
    localparam NREG = 14;
    function [38:0] drp(input [1:0] m, input [3:0] k);   // {address, bits kept, value}
        case ({m, k})
            {2'd0, 4'd0}: drp = {7'h28, 16'h0000, 16'hFFFF};
            {2'd0, 4'd1}: drp = {7'h08, 16'h1000, 16'h03CF};
            {2'd0, 4'd2}: drp = {7'h09, 16'h8000, 16'h0000};
            {2'd0, 4'd3}: drp = {7'h0A, 16'h1000, 16'h01C8};
            {2'd0, 4'd4}: drp = {7'h0B, 16'h8000, 16'h0080};
            {2'd0, 4'd5}: drp = {7'h16, 16'hC000, 16'h2042};
            {2'd0, 4'd6}: drp = {7'h14, 16'h1000, 16'h06DB};
            {2'd0, 4'd7}: drp = {7'h15, 16'h8000, 16'h6C00};
            {2'd0, 4'd8}: drp = {7'h13, 16'hC3FF, 16'h1C00};
            {2'd0, 4'd9}: drp = {7'h18, 16'hFC00, 16'h00FA};
            {2'd0, 4'd10}: drp = {7'h19, 16'h8000, 16'h7C01};
            {2'd0, 4'd11}: drp = {7'h1A, 16'h8000, 16'h7FE9};
            {2'd0, 4'd12}: drp = {7'h4E, 16'h66FF, 16'h1100};
            {2'd0, 4'd13}: drp = {7'h4F, 16'h666F, 16'h1800};
            {2'd1, 4'd0}: drp = {7'h28, 16'h0000, 16'hFFFF};
            {2'd1, 4'd1}: drp = {7'h08, 16'h1000, 16'h030C};
            {2'd1, 4'd2}: drp = {7'h09, 16'h8000, 16'h0000};
            {2'd1, 4'd3}: drp = {7'h0A, 16'h1000, 16'h0186};
            {2'd1, 4'd4}: drp = {7'h0B, 16'h8000, 16'h0000};
            {2'd1, 4'd5}: drp = {7'h16, 16'hC000, 16'h2042};
            {2'd1, 4'd6}: drp = {7'h14, 16'h1000, 16'h05D6};
            {2'd1, 4'd7}: drp = {7'h15, 16'h8000, 16'h1880};
            {2'd1, 4'd8}: drp = {7'h13, 16'hC3FF, 16'h2400};
            {2'd1, 4'd9}: drp = {7'h18, 16'hFC00, 16'h00FA};
            {2'd1, 4'd10}: drp = {7'h19, 16'h8000, 16'h7C01};
            {2'd1, 4'd11}: drp = {7'h1A, 16'h8000, 16'h7FE9};
            {2'd1, 4'd12}: drp = {7'h4E, 16'h66FF, 16'h1900};
            {2'd1, 4'd13}: drp = {7'h4F, 16'h666F, 16'h0100};
            {2'd2, 4'd0}: drp = {7'h28, 16'h0000, 16'hFFFF};
            {2'd2, 4'd1}: drp = {7'h08, 16'h1000, 16'h02CB};
            {2'd2, 4'd2}: drp = {7'h09, 16'h8000, 16'h0000};
            {2'd2, 4'd3}: drp = {7'h0A, 16'h1000, 16'h0146};
            {2'd2, 4'd4}: drp = {7'h0B, 16'h8000, 16'h0080};
            {2'd2, 4'd5}: drp = {7'h16, 16'hC000, 16'h2042};
            {2'd2, 4'd6}: drp = {7'h14, 16'h1000, 16'h0555};
            {2'd2, 4'd7}: drp = {7'h15, 16'h8000, 16'h2880};
            {2'd2, 4'd8}: drp = {7'h13, 16'hC3FF, 16'h2800};
            {2'd2, 4'd9}: drp = {7'h18, 16'hFC00, 16'h00FA};
            {2'd2, 4'd10}: drp = {7'h19, 16'h8000, 16'h7C01};
            {2'd2, 4'd11}: drp = {7'h1A, 16'h8000, 16'h7FE9};
            {2'd2, 4'd12}: drp = {7'h4E, 16'h66FF, 16'h0800};
            {2'd2, 4'd13}: drp = {7'h4F, 16'h666F, 16'h8000};
            {2'd3, 4'd0}: drp = {7'h28, 16'h0000, 16'hFFFF};
            {2'd3, 4'd1}: drp = {7'h08, 16'h1000, 16'h038E};
            {2'd3, 4'd2}: drp = {7'h09, 16'h8000, 16'h0000};
            {2'd3, 4'd3}: drp = {7'h0A, 16'h1000, 16'h01C7};
            {2'd3, 4'd4}: drp = {7'h0B, 16'h8000, 16'h0000};
            {2'd3, 4'd5}: drp = {7'h16, 16'hC000, 16'h0041};
            {2'd3, 4'd6}: drp = {7'h14, 16'h1000, 16'h0451};
            {2'd3, 4'd7}: drp = {7'h15, 16'h8000, 16'h3880};
            {2'd3, 4'd8}: drp = {7'h13, 16'hC3FF, 16'h2800};
            {2'd3, 4'd9}: drp = {7'h18, 16'hFC00, 16'h0113};
            {2'd3, 4'd10}: drp = {7'h19, 16'h8000, 16'h7C01};
            {2'd3, 4'd11}: drp = {7'h1A, 16'h8000, 16'h7FE9};
            {2'd3, 4'd12}: drp = {7'h4E, 16'h66FF, 16'h0900};
            {2'd3, 4'd13}: drp = {7'h4F, 16'h666F, 16'h1000};
            default: drp = {7'h28, 16'h0000, 16'hFFFF};
        endcase
    endfunction

    // ---- the switch ----------------------------------------------------------------
    localparam S_IDLE = 4'd0, S_HALT = 4'd1, S_BUS = 4'd2, S_GATE = 4'd3, S_RD = 4'd4,
               S_RDW = 4'd5, S_WR = 4'd6, S_WRW = 4'd7, S_LOCK = 4'd8, S_RUN = 4'd9,
               S_I2C = 4'd10, S_DONE = 4'd11;
    reg  [3:0]  st = S_IDLE;
    reg  [1:0]  tgt = 2'd0;
    reg  [3:0]  k = 4'd0;
    reg  [22:0] t = 23'd0;               // time in the current step (clk)
    reg  [31:0] dur = 32'd0;             // clocks of the switch so far
    reg  [31:0] last_dur = 32'd0;
    reg  [7:0]  count = 8'd0;            // switches done
    reg  [2:0]  tmo = 3'b000;            // timeouts seen: halt, buses, lock/I2C
    reg         mism = 1'b0;             // a register read back differed from the table
    reg  [6:0]  mism_a = 7'd0;
    reg  [15:0] mism_v = 16'd0;
    reg  [15:0] rd = 16'd0;
    wire [38:0] w = drp(tgt, k);
    wire [38:0] w_old = drp(vmode, k);
    always @(posedge clk) begin
        t   <= t + 23'd1;
        den <= 1'b0;
        dwe <= 1'b0;
        if (st != S_IDLE) dur <= dur + 32'd1;
        case (st)
            S_IDLE: if (want != vmode) begin
                tgt <= want; st <= S_HALT; t <= 23'd0; dur <= 32'd0;
                halt_req <= 1'b1;
            end
            S_HALT: if (halted_s[1] || inrst_s[1] || t[22]) begin
                if (!(halted_s[1] || inrst_s[1])) tmo[0] <= 1'b1;
                i2c_hold <= 1'b1; kb_pause <= 1'b1; st <= S_BUS; t <= 23'd0;
            end
            S_BUS: if ((t[8] && ipark_s[1] && kpark_s[1]) || t[21]) begin
                if (!(ipark_s[1] && kpark_s[1])) tmo[1] <= 1'b1;
                cdce_v60 <= v60(tgt);
                reconf <= 1'b1; sys_en <= 1'b0; st <= S_GATE; t <= 23'd0;
            end
            S_GATE: if (t[6]) begin             // the BUFGCEs have stopped
                mmcm_rst <= 1'b1; k <= 4'd0; st <= S_RD;
            end
            S_RD: begin
                daddr <= w[38:32]; den <= 1'b1; st <= S_RDW;
            end
            S_RDW: if (drdy) begin
                rd <= dout; st <= S_WR;
                if (!mism && ((dout & ~w_old[31:16]) != w_old[15:0])) begin
                    mism <= 1'b1; mism_a <= w[38:32]; mism_v <= dout;
                end
            end
            S_WR: begin
                di <= (rd & w[31:16]) | w[15:0]; den <= 1'b1; dwe <= 1'b1; st <= S_WRW;
            end
            S_WRW: if (drdy) begin
                if (k == NREG - 1) begin mmcm_rst <= 1'b0; st <= S_LOCK; t <= 23'd0; end
                else begin k <= k + 4'd1; st <= S_RD; end
            end
            S_LOCK: if ((slock_s[1] && rlock_s[1] && t[8]) || t[22]) begin
                if (!(slock_s[1] && rlock_s[1])) tmo[2] <= 1'b1;
                sys_en <= 1'b1; st <= S_RUN; t <= 23'd0;
            end
            S_RUN: if (t[6] && slock_s[1] && rlock_s[1]) begin   // sys_rst may see the locks now
                reconf <= 1'b0; st <= S_I2C; t <= 23'd0;
            end
            S_I2C: if ((i2c_done && t[8]) || t[22]) begin
                if (!i2c_done) tmo[2] <= 1'b1;
                i2c_hold <= 1'b0; kb_pause <= 1'b0;
                vmode <= tgt; halt_req <= 1'b0; st <= S_DONE;
            end
            default: begin                      // S_DONE
                last_dur <= dur; count <= count + 8'd1; st <= S_IDLE;
            end
        endcase
        if (rst) begin
            st <= S_IDLE; vmode <= 2'd0; tgt <= 2'd0; halt_req <= 1'b0; i2c_hold <= 1'b0;
            kb_pause <= 1'b0; cdce_v60 <= 1'b0; sys_en <= 1'b1; reconf <= 1'b0; mmcm_rst <= 1'b0;
            count <= 8'd0; tmo <= 3'b000; mism <= 1'b0;
        end
    end
    assign status  = {2'b00, count, st, want, vmode, cdce_v60, sys_en, mism, tmo, 1'b0, mism_a};
    assign status2 = {mism_v, last_dur[25:10]};
endmodule
