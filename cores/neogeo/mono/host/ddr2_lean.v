// ddr2_lean: our own DDR2 controller and PHY for the C64U's MT47H64M16 (OPT-ddr2).
//
// Replaces mig_ddr2 behind mig_arb: the same app_* port, ui_clk 75 MHz, one
// 128-bit word per command (BL8), reads come back in command order. The
// memory runs at 150 MHz (DDR2-300), CL3 AL1 BL8, closed page (every access is
// ACTIVE + READ/WRITE with auto-precharge, issued as one slot pair).
//
// Clocks (one PLLE2 from clk200): clk300 (CLK of every OSERDES/ISERDES, 600 Mbit/s
// slots of 1.667 ns, eight per ui_clk) and ui_clk (CLKDIV and the whole fabric).
// Everything in the fabric is ui_clk: no clock crossing except the pads.
//
// Slot grid (TRX64-Ultimate/docs/specs/OPT-ddr2.md §7): a ui_clk cycle is 8 slots = two memory
// clocks. What this module registers in cycle n is serialized in the following cycle
// (OSERDES takes D at the next CLKDIV edge); all pins have the same delay. A command is
// launched in cycle n: slot 0 = ACTIVE, slot 1 = READ/WRITE+AP; a command value is held
// for 4 slots and CK rises in the middle of it (3.33 ns setup and hold). Write data:
// DQS rises on a CK edge (tDQSS = 0) WL = 3 clocks after the WRITE, DQ is centred on the
// DQS edges (one slot later than the DQS grid), 2 slots per beat. The data of a write
// launched in cycle n is launched in cycles n+2 (beats 0-3, preamble in its first two
// slots) and n+3 (beats 4-7); slot 0 of a cycle repeats the last beat of the one before.
// Reads: DQ and DQS are sampled by ISERDES on clk300 behind an IDELAYE2 (DQS as data,
// Gideon's scheme), the odd one of the eight samples of a cycle is the beat's;
// bitslip, tap and read delay are set by software (the register block) as in
// ddr2_calibrator_u64ii.c.
//
// Register block (r_*, ui_clk, word addresses): see TRX64-Ultimate/docs/specs/OPT-ddr2.md §7.
module ddr2_lean (
    inout  wire [15:0] ddr2_dq,
    inout  wire [1:0]  ddr2_dqs_n,
    inout  wire [1:0]  ddr2_dqs_p,
    output wire [12:0] ddr2_addr,
    output wire [2:0]  ddr2_ba,
    output wire        ddr2_ras_n,
    output wire        ddr2_cas_n,
    output wire        ddr2_we_n,
    output wire [0:0]  ddr2_ck_p,
    output wire [0:0]  ddr2_ck_n,
    output wire [0:0]  ddr2_cke,
    output wire [0:0]  ddr2_cs_n,
    output wire [1:0]  ddr2_dm,
    output wire [0:0]  ddr2_odt,

    input  wire        sys_clk_i,           // 200 MHz: PLL input and IDELAYCTRL reference
    input  wire        sys_rst,             // active high

    input  wire [26:0] app_addr,            // 16-bit word address, bits 2:0 ignored
    input  wire [2:0]  app_cmd,             // 000 write, 001 read
    input  wire        app_en,
    output wire        app_rdy,
    input  wire [127:0] app_wdf_data,
    input  wire        app_wdf_end,         // unused (one word per command)
    input  wire [15:0] app_wdf_mask,        // 1 = byte not written
    input  wire        app_wdf_wren,
    output wire        app_wdf_rdy,
    output wire [127:0] app_rd_data,
    output wire        app_rd_data_end,
    output wire        app_rd_data_valid,

    output wire        ui_clk,
    output wire        ui_clk_sync_rst,
    output wire        init_calib_complete,

    input  wire        r_we,                // register block, ui_clk
    input  wire [3:0]  r_addr,
    input  wire [31:0] r_wdata,
    output reg  [31:0] r_rdata
);
    // first capture window word after the launch cycle; lane read delays count from here
    localparam C0 = 2;

    // ---- clocks and reset -----------------------------------------------------
    wire pll_fb, pll_300, pll_75, pll_locked, clk300, idc_rdy;
    PLLE2_BASE #(
        .BANDWIDTH("OPTIMIZED"), .STARTUP_WAIT("FALSE"), .REF_JITTER1(0.01),
        .CLKIN1_PERIOD(5.0), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT(6), .CLKFBOUT_PHASE(0.0),   // VCO 1200 MHz
        .CLKOUT0_DIVIDE(4),  .CLKOUT0_PHASE(0.0), .CLKOUT0_DUTY_CYCLE(0.5),                // 300 MHz
        .CLKOUT1_DIVIDE(16), .CLKOUT1_PHASE(0.0), .CLKOUT1_DUTY_CYCLE(0.5)                 // 75 MHz
    ) pll (
        .CLKIN1(sys_clk_i), .CLKFBIN(pll_fb), .CLKFBOUT(pll_fb),
        .CLKOUT0(pll_300), .CLKOUT1(pll_75), .CLKOUT2(), .CLKOUT3(), .CLKOUT4(), .CLKOUT5(),
        .LOCKED(pll_locked), .PWRDWN(1'b0), .RST(sys_rst)
    );
    // equal insertion delay: CLK and CLKDIV of the SERDES must stay phase aligned
    (* CLOCK_DELAY_GROUP = "ddr2_lean_clks" *) wire clk300_g;
    (* CLOCK_DELAY_GROUP = "ddr2_lean_clks" *) wire ui_clk_g;
    BUFG bg300 (.I(pll_300), .O(clk300_g));
    BUFG bg75  (.I(pll_75),  .O(ui_clk_g));
    assign clk300 = clk300_g;
    assign ui_clk = ui_clk_g;

    (* IODELAY_GROUP = "ddr2_lean_idly" *)
    IDELAYCTRL idelayctrl (.RDY(idc_rdy), .REFCLK(sys_clk_i), .RST(sys_rst));

    (* ASYNC_REG = "TRUE" *) reg [1:0] lk_s = 2'b00;
    reg [4:0] rst_cnt = 5'd0;
    reg       rst = 1'b1;
    always @(posedge ui_clk) begin
        lk_s <= {lk_s[0], pll_locked & idc_rdy};
        if (!lk_s[1]) begin
            rst_cnt <= 5'd0;
            rst     <= 1'b1;
        end else if (rst_cnt != 5'd31) rst_cnt <= rst_cnt + 5'd1;
        else rst <= 1'b0;
    end
    assign ui_clk_sync_rst = rst;

    // ---- register block -------------------------------------------------------
    reg        ck_en = 1'b0, odt_on = 1'b0, ref_en = 1'b0, cke_on = 1'b0, calib_done = 1'b0;
    reg [4:0]  tap0 = 5'd0, tap1 = 5'd0;
    reg [2:0]  dly0 = 3'd2, dly1 = 3'd2;
    reg [3:0]  bsc0 = 4'd0, bsc1 = 4'd0;
    reg [15:0] refi = 16'd560;                  // 7.47 us
    reg        tap_ld = 1'b0;
    reg [1:0]  slip = 2'b00;
    reg        dc_pend = 1'b0;
    reg [18:0] dc_word = 19'd0;
    reg        cal_req = 1'b0, cal_busy = 1'b0, rb_valid = 1'b0;
    reg [63:0] rb0 = 64'd0, rb1 = 64'd0;
    reg [31:0] ref_count = 32'd0;
    reg [3:0]  oewin = 4'b1001;                 // DQ/DQS drive window: cycles early [1:0] and late [3:2] around the data (board 2026-10-06: 0x9)
    reg [3:0]  rb_i = 4'd0;
    reg        rb_act = 1'b0;
    wire       rb_done = rb_act && rb_i == 4'd7;
    wire       cmd_busy = dc_pend;

    assign init_calib_complete = calib_done;

    // ---- command queue (two entries, strict order) and write data (two words) ----
    reg         q0_v = 1'b0, q1_v = 1'b0, q0_rd = 1'b0, q1_rd = 1'b0;
    reg  [26:0] q0_a, q1_a;
    wire        acc_c = app_en && app_rdy;
    assign app_rdy = calib_done && !rst && !q1_v;

    reg [127:0] wd_e0, wd_e1;
    reg [15:0]  wm_e0, wm_e1;
    reg         w_wptr = 1'b0, w_cptr = 1'b0;      // fill and commit (issue) entry
    reg [1:0]   wcnt = 2'd0, wnew = 2'd0;          // entries held, entries not yet issued
    wire        acc_w = app_wdf_wren && app_wdf_rdy;
    assign app_wdf_rdy = calib_done && !rst && wcnt != 2'd2;

    // ---- issue logic ------------------------------------------------------------
    reg [2:0]  bt [0:7];                           // cycles before the bank takes another ACTIVE
    reg [2:0]  since = 3'd7;                       // cycles since the last access was issued
    reg        last_wr = 1'b0;
    reg [3:0]  rfc = 4'd0;
    reg [15:0] refcnt = 16'd0;
    reg        ref_due = 1'b0;
    integer    bi;
    initial for (bi = 0; bi < 8; bi = bi + 1) bt[bi] = 3'd0;

    wire        use_cal = cal_req && !calib_done;
    wire        h_v     = use_cal || (q0_v && calib_done);
    wire        h_rd    = use_cal || q0_rd;
    wire [2:0]  h_bank  = use_cal ? 3'd0 : q0_a[5:3];
    wire [12:0] h_row   = use_cal ? 13'd0 : q0_a[25:13];
    wire [9:0]  h_col   = use_cal ? 10'd0 : {q0_a[12:6], 3'b000};
    wire        bank_free = bt[h_bank] == 3'd0;
    wire        all_idle  = bt[0] == 3'd0 && bt[1] == 3'd0 && bt[2] == 3'd0 && bt[3] == 3'd0 &&
                            bt[4] == 3'd0 && bt[5] == 3'd0 && bt[6] == 3'd0 && bt[7] == 3'd0;
    // minimum distance between two accesses (launch cycles): data bus and turn-around
    wire [2:0]  need   = h_rd ? (last_wr ? 3'd6 : 3'd2) : (last_wr ? 3'd2 : 3'd6);
    wire        gap_ok = since >= need;
    wire        run_ok = ck_en && cke_on && !rst;
    wire        do_dc  = dc_pend && run_ok;
    wire        do_ref = !dc_pend && ref_en && ref_due && all_idle && rfc == 4'd0 && run_ok;
    wire        do_acc = !dc_pend && !ref_due && h_v && bank_free && gap_ok && rfc == 4'd0 &&
                         (h_rd || wnew != 2'd0) && run_ok;
    wire        pop_q  = do_acc && !use_cal;

    reg         c0_ras_n = 1'b1, c0_cas_n = 1'b1, c0_we_n = 1'b1;
    reg         c1_ras_n = 1'b1, c1_cas_n = 1'b1, c1_we_n = 1'b1;
    reg [12:0]  c0_a = 13'd0, c1_a = 13'd0;
    reg [2:0]   c0_ba = 3'd0, c1_ba = 3'd0;

    reg [11:0]  rdp = 12'd0, rdc = 12'd0;          // rdp[k]: a read was launched k cycles ago (rdc: a calibration read)
    reg [6:0]   wp = 7'd0;                         // same for writes (wp[k]: launched k cycles ago)
    reg         wi0 = 1'b0, wi1 = 1'b0, wi2 = 1'b0;



    always @(posedge ui_clk) begin
        tap_ld <= 1'b0;
        slip   <= 2'b00;
        if (r_we) case (r_addr)
        4'd1: begin
            ck_en <= r_wdata[0]; odt_on <= r_wdata[1]; ref_en <= r_wdata[2];
            cke_on <= r_wdata[3]; calib_done <= r_wdata[4];
        end
        4'd2: if (!calib_done && !dc_pend) begin
            dc_pend <= 1'b1;
            dc_word <= r_wdata[18:0];
        end
        4'd3: begin
            tap0 <= r_wdata[4:0]; tap1 <= r_wdata[12:8];
            tap_ld <= 1'b1;
        end
        4'd4: begin dly0 <= r_wdata[2:0]; dly1 <= r_wdata[6:4]; end
        4'd5: begin
            slip <= r_wdata[1:0];
            if (r_wdata[0]) bsc0 <= bsc0 + 4'd1;
            if (r_wdata[1]) bsc1 <= bsc1 + 4'd1;
        end
        4'd10: if (!calib_done && !cal_busy) begin
            cal_req  <= 1'b1;
            cal_busy <= 1'b1;
            rb_valid <= 1'b0;
        end
        4'd11: refi <= r_wdata[15:0];
        4'd13: oewin <= r_wdata[3:0];
        default: ;
        endcase
        if (do_dc) dc_pend <= 1'b0;
        if (do_acc && use_cal) cal_req <= 1'b0;
        if (do_ref) ref_count <= ref_count + 32'd1;
        if (rb_done) begin cal_busy <= 1'b0; rb_valid <= 1'b1; end
    end

    always @(*) begin
        case (r_addr)
        4'd0:  r_rdata = {16'hDD20, 8'd0, calib_done, ref_due, cmd_busy, rb_valid, cal_busy, rst, idc_rdy, pll_locked};
        4'd1:  r_rdata = {27'd0, calib_done, cke_on, ref_en, odt_on, ck_en};
        4'd2:  r_rdata = {13'd0, dc_word};
        4'd3:  r_rdata = {19'd0, tap1, 3'd0, tap0};
        4'd4:  r_rdata = {25'd0, dly1, 1'b0, dly0};
        4'd5:  r_rdata = {24'd0, bsc1, bsc0};
        4'd6:  r_rdata = rb0[31:0];
        4'd7:  r_rdata = rb0[63:32];
        4'd8:  r_rdata = rb1[31:0];
        4'd9:  r_rdata = rb1[63:32];
        4'd11: r_rdata = {16'd0, refi};
        4'd12: r_rdata = ref_count;
        4'd13: r_rdata = {28'd0, oewin};
        default: r_rdata = 32'd0;
        endcase
    end

    always @(posedge ui_clk) begin
        // refresh timer
        if (!ref_en) begin
            refcnt <= refi;
            ref_due <= 1'b0;
        end else if (refcnt == 16'd0) begin
            refcnt  <= refi;
            ref_due <= 1'b1;
        end else refcnt <= refcnt - 16'd1;
        if (do_ref) ref_due <= 1'b0;

        for (bi = 0; bi < 8; bi = bi + 1)
            if (bt[bi] != 3'd0) bt[bi] <= bt[bi] - 3'd1;
        if (rfc != 4'd0) rfc <= rfc - 4'd1;
        if (since != 3'd7) since <= since + 3'd1;

        c0_ras_n <= 1'b1; c0_cas_n <= 1'b1; c0_we_n <= 1'b1;
        c1_ras_n <= 1'b1; c1_cas_n <= 1'b1; c1_we_n <= 1'b1;
        if (do_dc) begin
            {c0_ras_n, c0_cas_n, c0_we_n} <= dc_word[18:16];
            c0_ba <= dc_word[15:13];
            c0_a  <= dc_word[12:0];
        end else if (do_ref) begin
            c0_ras_n <= 1'b0; c0_cas_n <= 1'b0; c0_we_n <= 1'b1;
            rfc <= 4'd9;                       // tRFC 127.5 ns: the next ACTIVE 10 cycles later
        end else if (do_acc) begin
            c0_ras_n <= 1'b0; c0_cas_n <= 1'b1; c0_we_n <= 1'b1;
            c0_a <= h_row; c0_ba <= h_bank;
            c1_ras_n <= 1'b1; c1_cas_n <= 1'b0; c1_we_n <= !h_rd ? 1'b0 : 1'b1;
            c1_a <= {2'b00, 1'b1, h_col}; c1_ba <= h_bank;    // A10 = auto-precharge
            bt[h_bank] <= h_rd ? 3'd4 : 3'd6;                  // tRC 5 / write recovery + tRP 7 cycles
            since   <= 3'd1;
            last_wr <= !h_rd;
        end

        // pipelines: what was launched k cycles ago
        rdp <= {rdp[10:0], do_acc && h_rd};
        rdc <= {rdc[10:0], do_acc && use_cal};
        wp  <= {wp[5:0], do_acc && !h_rd};
        wi0 <= w_cptr; wi1 <= wi0; wi2 <= wi1;
        if (do_acc && !h_rd) w_cptr <= !w_cptr;

        // command queue
        if (pop_q) begin
            if (q1_v) begin
                q0_a <= q1_a; q0_rd <= q1_rd;
                if (acc_c) begin q1_a <= app_addr; q1_rd <= app_cmd[0]; end
                else q1_v <= 1'b0;
            end else if (acc_c) begin
                q0_a <= app_addr; q0_rd <= app_cmd[0];
            end else q0_v <= 1'b0;
        end else if (acc_c) begin
            if (!q0_v) begin q0_a <= app_addr; q0_rd <= app_cmd[0]; q0_v <= 1'b1; end
            else begin q1_a <= app_addr; q1_rd <= app_cmd[0]; q1_v <= 1'b1; end
        end

        // write data entries
        if (acc_w) begin
            if (w_wptr) begin wd_e1 <= app_wdf_data; wm_e1 <= app_wdf_mask; end
            else        begin wd_e0 <= app_wdf_data; wm_e0 <= app_wdf_mask; end
            w_wptr <= !w_wptr;
        end
        wcnt <= wcnt + {1'b0, acc_w} - {1'b0, wp[2]};
        wnew <= wnew + {1'b0, acc_w} - {1'b0, do_acc && !h_rd};

        if (rst) begin
            q0_v <= 1'b0; q1_v <= 1'b0; wcnt <= 2'd0; wnew <= 2'd0;
            w_wptr <= 1'b0; w_cptr <= 1'b0; rdp <= 12'd0; rdc <= 12'd0; wp <= 7'd0;
            ref_due <= 1'b0; since <= 3'd7; rfc <= 4'd0;
            for (bi = 0; bi < 8; bi = bi + 1) bt[bi] <= 3'd0;
        end
    end

    // ---- write data towards the PHY: launched in cycle n+2 (first half) and n+3 --------
    reg [63:0] wd_launch = 64'd0;
    reg [7:0]  wm_launch = 8'd0;
    reg        act_launch = 1'b0;
    reg        oe_launch = 1'b0;
    // bit m of {wp, do_w} is a write launched m-1 cycles ago (m = 0: about to be launched); the
    // drive for cycle n+m (n = the write's launch cycle) is on when bit m is, for m from 2 - E
    // (the data's first launch cycle is n+2) to 4 + L (its last is n+3, the tail n+4)
    wire [7:0] oe_mask;
    genvar     gm;
    generate for (gm = 0; gm < 8; gm = gm + 1) begin : oem
        assign oe_mask[gm] = (gm + oewin[1:0] >= 2) && (gm <= 4 + oewin[3:2]);
    end endgenerate
    wire       w_ent = wp[1] ? wi1 : wi2;
    wire [127:0] w_dat = w_ent ? wd_e1 : wd_e0;
    wire [15:0]  w_msk = w_ent ? wm_e1 : wm_e0;
    always @(posedge ui_clk) begin
        if (wp[1]) begin wd_launch <= w_dat[63:0];   wm_launch <= w_msk[7:0];  end
        if (wp[2]) begin wd_launch <= w_dat[127:64]; wm_launch <= w_msk[15:8]; end
        act_launch <= wp[1] || wp[2];
        // DQ/DQS drive from `E` cycles before the data's first cycle to `L` cycles after its last
        // (oewin): the silicon's latency of the OSERDES tri-state path against its data path is
        // not known to this code; do_w is the write about to be launched (decision cycle)
        oe_launch  <= |({wp, do_acc && !h_rd} & oe_mask);
    end

    // ---- PHY ---------------------------------------------------------------------
    wire [31:0] rd_dq0, rd_dq1;
    wire [7:0]  rd_dqs0, rd_dqs1;
    ddr2_phy phy (
        .clk300(clk300), .ui_clk(ui_clk), .rst(rst),
        .ck_en(ck_en), .cke(cke_on), .odt(odt_on),
        .c0_a(c0_a), .c0_ba(c0_ba), .c0_ras_n(c0_ras_n), .c0_cas_n(c0_cas_n), .c0_we_n(c0_we_n),
        .c1_a(c1_a), .c1_ba(c1_ba), .c1_ras_n(c1_ras_n), .c1_cas_n(c1_cas_n), .c1_we_n(c1_we_n),
        .wd(wd_launch), .wm(wm_launch), .wr_act(act_launch), .wr_oe(oe_launch),
        .tap0(tap0), .tap1(tap1), .tap_ld(tap_ld), .slip(slip),
        .rd_dq0(rd_dq0), .rd_dq1(rd_dq1), .rd_dqs0(rd_dqs0), .rd_dqs1(rd_dqs1),
        .ddr2_dq(ddr2_dq), .ddr2_dqs_p(ddr2_dqs_p), .ddr2_dqs_n(ddr2_dqs_n),
        .ddr2_addr(ddr2_addr), .ddr2_ba(ddr2_ba), .ddr2_ras_n(ddr2_ras_n),
        .ddr2_cas_n(ddr2_cas_n), .ddr2_we_n(ddr2_we_n), .ddr2_ck_p(ddr2_ck_p),
        .ddr2_ck_n(ddr2_ck_n), .ddr2_cke(ddr2_cke), .ddr2_cs_n(ddr2_cs_n),
        .ddr2_dm(ddr2_dm), .ddr2_odt(ddr2_odt)
    );

    // ---- read data: per lane A (beats 0-3) and B (beats 4-7) at the lane's own delay -----
    wire [2:0]  dmax = dly0 > dly1 ? dly0 : dly1;
    wire [63:0] q0, q1;
    ddr2_rdlane #(.C0(C0)) lane0 (.clk(ui_clk), .rdp(rdp), .dly(dly0), .w(rd_dq0), .q(q0));
    ddr2_rdlane #(.C0(C0)) lane1 (.clk(ui_clk), .rdp(rdp), .dly(dly1), .w(rd_dq1), .q(q1));
    reg rd_valid = 1'b0;
    always @(posedge ui_clk) rd_valid <= rdp[C0 + 2 + dmax] && !rdc[C0 + 2 + dmax] && !rst;
    assign app_rd_data_valid = rd_valid;
    assign app_rd_data_end   = rd_valid;
    genvar gk;
    generate for (gk = 0; gk < 8; gk = gk + 1) begin : beat
        assign app_rd_data[16*gk +: 16] = {q1[8*gk +: 8], q0[8*gk +: 8]};
    end endgenerate

    // ---- DQS capture for the calibration: 8 words from cycle n+C0 of a calibration read ----
    always @(posedge ui_clk) begin
        if (rdp[C0] && rdc[C0]) begin
            rb_i <= 4'd1; rb_act <= 1'b1;
            rb0[7:0] <= rd_dqs0; rb1[7:0] <= rd_dqs1;
        end else if (rb_act) begin
            rb0[8*rb_i[2:0] +: 8] <= rd_dqs0;
            rb1[8*rb_i[2:0] +: 8] <= rd_dqs1;
            rb_i <= rb_i + 4'd1;
            if (rb_i == 4'd7) rb_act <= 1'b0;
        end
    end
endmodule

// One byte lane's read path: the lane's 32-bit word (4 beats) of every cycle goes into A at
// cycle n+C0+dly, B one cycle later, and both are copied to q one cycle after that. q stays
// for two cycles, so the lanes may differ by one cycle of dly (the module above reads q in
// cycle n+C0+dmax+3).
module ddr2_rdlane #(
    parameter C0 = 2
) (
    input  wire        clk,
    input  wire [11:0] rdp,
    input  wire [2:0]  dly,
    input  wire [31:0] w,
    output reg  [63:0] q = 64'd0
);
    reg [31:0] a1 = 32'd0, b1 = 32'd0;
    wire sa = rdp[C0 + dly];
    wire sb = rdp[C0 + 1 + dly];
    wire sc = rdp[C0 + 2 + dly];
    always @(posedge clk) begin
        if (sa) a1 <= w;
        if (sb) b1 <= w;
        if (sc) q  <= {b1, a1};
    end
endmodule

// PHY: OSERDES/ISERDES/IDELAY and the pads. Everything registered in ui_clk (CLKDIV).
module ddr2_phy (
    input  wire        clk300,
    input  wire        ui_clk,
    input  wire        rst,

    input  wire        ck_en,
    input  wire        cke,
    input  wire        odt,
    input  wire [12:0] c0_a, c1_a,
    input  wire [2:0]  c0_ba, c1_ba,
    input  wire        c0_ras_n, c0_cas_n, c0_we_n, c1_ras_n, c1_cas_n, c1_we_n,

    input  wire [63:0] wd,                  // four beats of 16 bits, beat 0 in [15:0]
    input  wire [7:0]  wm,                  // DM of the four beats, {lane 1, lane 0} each
    input  wire        wr_act,              // this cycle carries four beats
    input  wire        wr_oe,               // DQ and DQS drive

    input  wire [4:0]  tap0, tap1,
    input  wire        tap_ld,
    input  wire [1:0]  slip,

    output wire [31:0] rd_dq0, rd_dq1,      // [8*beat + bit]
    output wire [7:0]  rd_dqs0, rd_dqs1,    // raw DQS samples, bit = sample (time order)

    inout  wire [15:0] ddr2_dq,
    inout  wire [1:0]  ddr2_dqs_p,
    inout  wire [1:0]  ddr2_dqs_n,
    output wire [12:0] ddr2_addr,
    output wire [2:0]  ddr2_ba,
    output wire        ddr2_ras_n, ddr2_cas_n, ddr2_we_n,
    output wire [0:0]  ddr2_ck_p, ddr2_ck_n, ddr2_cke, ddr2_cs_n,
    output wire [1:0]  ddr2_dm,
    output wire [0:0]  ddr2_odt
);
    // one value per memory clock: slot 0 for the first four slots, slot 1 for the next four
    function [7:0] two(input s0, input s1);
        two = {s1, s1, s1, s1, s0, s0, s0, s0};
    endfunction

    // ---- command, address, control ---------------------------------------------------
    genvar i;
    generate for (i = 0; i < 13; i = i + 1) begin : a
        ddr2_ser8o s (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(two(c0_a[i], c1_a[i])), .pad(ddr2_addr[i]));
    end endgenerate
    generate for (i = 0; i < 3; i = i + 1) begin : b
        ddr2_ser8o s (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(two(c0_ba[i], c1_ba[i])), .pad(ddr2_ba[i]));
    end endgenerate
    ddr2_ser8o s_ras (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(two(c0_ras_n, c1_ras_n)), .pad(ddr2_ras_n));
    ddr2_ser8o s_cas (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(two(c0_cas_n, c1_cas_n)), .pad(ddr2_cas_n));
    ddr2_ser8o s_we (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(two(c0_we_n, c1_we_n)),   .pad(ddr2_we_n));
    ddr2_ser8o s_cs (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(8'h00),         .pad(ddr2_cs_n[0]));
    ddr2_ser8o s_cke (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d({8{cke}}),      .pad(ddr2_cke[0]));
    ddr2_ser8o s_odt (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d({8{odt}}),      .pad(ddr2_odt[0]));
    // CK rises in the middle of a command (slots 2 and 6); CK# is a pin of its own (HSTL_I_18, as Gideon's XDC)
    ddr2_ser8o s_ckp (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(ck_en ? 8'hCC : 8'h00), .pad(ddr2_ck_p[0]));
    ddr2_ser8o s_ckn (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(ck_en ? 8'h33 : 8'hFF), .pad(ddr2_ck_n[0]));

    // ---- write data: slot 0 = last beat of the previous cycle, then two slots per beat ---------
    reg [15:0] pv  = 16'd0;                 // beat 3 of the previous cycle (DQ)
    reg [1:0]  pvm = 2'd0;                  // and its DM
    always @(posedge ui_clk) begin
        pv  <= wd[63:48];
        pvm <= wm[7:6];
    end
    wire t_hz = !wr_oe;
    generate for (i = 0; i < 16; i = i + 1) begin : dq
        wire [7:0] d = {wd[48+i], wd[32+i], wd[32+i], wd[16+i], wd[16+i], wd[i], wd[i], pv[i]};
        wire       pad_i;
        wire       oq, tq;
        ddr2_ser8 s (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(d), .t(t_hz), .oq(oq), .tq(tq));
        IOBUF iob (.IO(ddr2_dq[i]), .I(oq), .T(tq), .O(pad_i));
        wire [7:0] smp;
        ddr2_des8 r (.clk300(clk300), .ui_clk(ui_clk), .rst(rst), .pad(pad_i),
                     .tap(i < 8 ? tap0 : tap1), .ld(tap_ld), .slip(i < 8 ? slip[0] : slip[1]), .s(smp));
        if (i < 8) begin : l0
            assign rd_dq0[i]    = smp[1];
            assign rd_dq0[8+i]  = smp[3];
            assign rd_dq0[16+i] = smp[5];
            assign rd_dq0[24+i] = smp[7];
        end else begin : l1
            assign rd_dq1[i-8]  = smp[1];
            assign rd_dq1[i]    = smp[3];
            assign rd_dq1[8+i]  = smp[5];
            assign rd_dq1[16+i] = smp[7];
        end
    end endgenerate
    generate for (i = 0; i < 2; i = i + 1) begin : dm
        wire [7:0] d = {wm[6+i], wm[4+i], wm[4+i], wm[2+i], wm[2+i], wm[i], wm[i], pvm[i]};
        ddr2_ser8o s (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(d), .pad(ddr2_dm[i]));
    end endgenerate
    generate for (i = 0; i < 2; i = i + 1) begin : dqs
        wire oq, tq, pad_i;
        ddr2_ser8 s (.clk(clk300), .clkdiv(ui_clk), .rst(rst), .d(wr_act ? 8'hCC : 8'h00), .t(t_hz), .oq(oq), .tq(tq));
        IOBUFDS iob (.IO(ddr2_dqs_p[i]), .IOB(ddr2_dqs_n[i]), .I(oq), .T(tq), .O(pad_i));
        wire [7:0] smp;
        ddr2_des8 r (.clk300(clk300), .ui_clk(ui_clk), .rst(rst), .pad(pad_i),
                     .tap(i == 0 ? tap0 : tap1), .ld(tap_ld), .slip(slip[i]), .s(smp));
        if (i == 0) begin : l0
            assign rd_dqs0 = smp;
        end else begin : l1
            assign rd_dqs1 = smp;
        end
    end endgenerate
endmodule

// 8:1 DDR serializer on clk300/ui_clk, D1 = d[0] goes out first. T in BUF mode (t = 1
// puts the pad's driver in high-Z), registered like the data.
module ddr2_ser8 (
    input  wire       clk,
    input  wire       clkdiv,
    input  wire       rst,
    input  wire [7:0] d,
    input  wire       t,
    output wire       oq,
    output wire       tq
);
    OSERDESE2 #(
        .DATA_RATE_OQ("DDR"), .DATA_RATE_TQ("BUF"), .DATA_WIDTH(8), .TRISTATE_WIDTH(1),
        .SERDES_MODE("MASTER"), .INIT_OQ(1'b0), .INIT_TQ(1'b1), .SRVAL_OQ(1'b0), .SRVAL_TQ(1'b1),
        .TBYTE_CTL("FALSE"), .TBYTE_SRC("FALSE")
    ) ser (
        .OQ(oq), .TQ(tq), .OFB(), .TFB(), .SHIFTOUT1(), .SHIFTOUT2(), .TBYTEOUT(),
        .CLK(clk), .CLKDIV(clkdiv),
        .D1(d[0]), .D2(d[1]), .D3(d[2]), .D4(d[3]), .D5(d[4]), .D6(d[5]), .D7(d[6]), .D8(d[7]),
        .T1(t), .T2(1'b0), .T3(1'b0), .T4(1'b0),
        .OCE(1'b1), .TCE(1'b1), .RST(rst),
        .SHIFTIN1(1'b0), .SHIFTIN2(1'b0), .TBYTEIN(1'b0)
    );
endmodule

// The same into an output buffer (command, address, DM, CK).
module ddr2_ser8o (
    input  wire       clk,
    input  wire       clkdiv,
    input  wire       rst,
    input  wire [7:0] d,
    output wire       pad
);
    wire oq;
    ddr2_ser8 s (.clk(clk), .clkdiv(clkdiv), .rst(rst), .d(d), .t(1'b0), .oq(oq), .tq());
    OBUF ob (.I(oq), .O(pad));
endmodule

// 1:8 DDR deserializer behind an IDELAYE2 (VAR_LOAD): s[0] is the earliest of the eight
// samples of a ui_clk cycle (the first bit of an ISERDESE2 word arrives on Q8).
module ddr2_des8 (
    input  wire       clk300,
    input  wire       ui_clk,
    input  wire       rst,
    input  wire       pad,
    input  wire [4:0] tap,
    input  wire       ld,
    input  wire       slip,
    output wire [7:0] s
);
    wire dly;
    wire q1, q2, q3, q4, q5, q6, q7, q8;
    (* IODELAY_GROUP = "ddr2_lean_idly" *)
    IDELAYE2 #(
        .CINVCTRL_SEL("FALSE"), .DELAY_SRC("IDATAIN"), .HIGH_PERFORMANCE_MODE("TRUE"),
        .IDELAY_TYPE("VAR_LOAD"), .IDELAY_VALUE(0), .PIPE_SEL("FALSE"),
        .REFCLK_FREQUENCY(200.0), .SIGNAL_PATTERN("DATA")
    ) idly (
        .C(ui_clk), .CE(1'b0), .CINVCTRL(1'b0), .CNTVALUEIN(tap), .CNTVALUEOUT(),
        .DATAIN(1'b0), .DATAOUT(dly), .IDATAIN(pad), .INC(1'b0), .LD(ld),
        .LDPIPEEN(1'b0), .REGRST(1'b0)
    );
    ISERDESE2 #(
        .DATA_RATE("DDR"), .DATA_WIDTH(8), .INTERFACE_TYPE("NETWORKING"), .IOBDELAY("IFD"),
        .NUM_CE(1), .OFB_USED("FALSE"), .SERDES_MODE("MASTER"), .DYN_CLKDIV_INV_EN("FALSE"),
        .DYN_CLK_INV_EN("FALSE"), .INIT_Q1(1'b0), .INIT_Q2(1'b0), .INIT_Q3(1'b0), .INIT_Q4(1'b0),
        .SRVAL_Q1(1'b0), .SRVAL_Q2(1'b0), .SRVAL_Q3(1'b0), .SRVAL_Q4(1'b0)
    ) des (
        .D(1'b0), .DDLY(dly), .CE1(1'b1), .CE2(1'b1), .CLK(clk300), .CLKB(~clk300),
        .CLKDIV(ui_clk), .CLKDIVP(1'b0), .OCLK(1'b0), .OCLKB(1'b0), .OFB(1'b0),
        .BITSLIP(slip), .RST(rst), .DYNCLKDIVSEL(1'b0), .DYNCLKSEL(1'b0),
        .SHIFTIN1(1'b0), .SHIFTIN2(1'b0), .SHIFTOUT1(), .SHIFTOUT2(),
        .Q1(q1), .Q2(q2), .Q3(q3), .Q4(q4), .Q5(q5), .Q6(q6), .Q7(q7), .Q8(q8), .O()
    );
    assign s = {q1, q2, q3, q4, q5, q6, q7, q8};
endmodule
