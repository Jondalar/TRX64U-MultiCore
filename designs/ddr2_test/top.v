// ddr2_test: bring up the C64U's DDR2 through our own controller (host/ddr2_lean.v)
// and test it.
//
// ddr2_lean: DDR2-300 (150 MHz memory clock, controller ui_clk 75 MHz), 16 bits, CL3 AL1 BL8,
// pins from Gideon's XDC in bank 35 (HSTL, pins.xdc), 200 MHz from our MMCM for the PLL and the
// IDELAY reference. The part is set up as a 1 Gbit x16 (MT47H64M16), 128 MB; the board also routes
// A13 (F4), which is held low here.
//
// No CPU: cal_seq runs the init sequence and the read calibration (Gideon's search) through
// ddr2_lean's register block, then ddr2_tester tests all 128 MB round after round (sequential and
// LFSR order, write masks, pingpong write/read, optional retention hold, refresh on all the time)
// and counts errors. Everything is readable over JTAG (USER1, the debug port of c64_basic:
// tools/ddr2_status.py, or Dbg.read() of tools/trxdbg.py), word addresses:
//   0x00 ID DD2A0002            0x01 STATUS: 0 init done, 1 calibration done, 2 calibration ok,
//        3 controller up (init_calib_complete), 4 tester run, 5 errors seen, 6 first error kept,
//        7 ui reset
//   0x02 CAL0: [4:0] tap lane 0, [12:8] tap lane 1, [18:16] read delay, [27:24] bitslip position
//   0x03 CAL1: [5:0] window width lane 0, [13:8] lane 1 (taps, 78 ps), [16]/[17] window cut by the
//        scan range, [23:20] bitslip positions that had a window
//   0x04 rounds   0x05 error words   0x06 first error: burst   0x07 round   0x08 pass
//   0x09-0x0C expected (bits 31:0 .. 127:96)   0x0D-0x10 actual   0x11 commands   0x12 pass/order now
//   0x13 AW (address bits of a burst)
//   0x20 CTL (rw): [0] LFSR order, [1] alternate the order every round, [2] pingpong pass, [3] write
//        masks, [4] run; reset value 0x1E   0x21 HOLD (rw): clocks of 13.3 ns between the masked write
//        pass and the read pass   0x22 any write: clear rounds, errors, commands, first error
//   0x100-0x10F ddr2_lean's register block (after the calibration; 0x10B: refresh interval, rw)
// LEDs (a fallback without JTAG): board LED blinks (alive); case LED 1 on: calibration ok;
// case LED 2 on: at least one round passed and no error so far, blinking: an error happened.
module top #(
    parameter AW = 23                           // bursts of 16 bytes: 2^23 = 128 MB
) (
    input  wire        RMII_REFCLK,
    output wire        LED_BOARDn, LED_CASE1n, LED_CASE2n,
    output wire        SLOT_ADDROEn, SLOT_DATAOEn, SLOT_ROMLn, SLOT_ROMHn,
    inout  wire [15:0] ddr2_dq,
    inout  wire [1:0]  ddr2_dqs_n, ddr2_dqs_p,
    output wire [12:0] ddr2_addr,
    output wire        SDRAM_A13,
    output wire [2:0]  ddr2_ba,
    output wire        ddr2_ras_n, ddr2_cas_n, ddr2_we_n,
    output wire [0:0]  ddr2_ck_p, ddr2_ck_n, ddr2_cke, ddr2_cs_n, ddr2_odt,
    output wire [1:0]  ddr2_dm
);
    assign SLOT_ADDROEn = 1'b1;
    assign SLOT_DATAOEn = 1'b1;
    assign SLOT_ROMLn   = 1'b1;
    assign SLOT_ROMHn   = 1'b1;
    assign SDRAM_A13    = 1'b0;

    // ---- 50 MHz, 200 MHz for ddr2_lean -----------------------------------------------
    wire clk50_ibuf, clk50;
    IBUF ib50 (.I(RMII_REFCLK), .O(clk50_ibuf));
    BUFG bg50 (.I(clk50_ibuf), .O(clk50));
    reg [15:0] por = 0;
    always @(posedge clk50) if (!por[15]) por <= por + 1;

    wire fb, clk200_mmcm, clk200, locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(20.0),   // VCO 1000 MHz
        .CLKOUT0_DIVIDE_F(5.0)                                            // 200 MHz
    ) mmcm (
        .CLKIN1(clk50_ibuf), .CLKFBIN(fb), .CLKFBOUT(fb), .CLKOUT0(clk200_mmcm),
        .LOCKED(locked), .RST(~por[15]), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg200 (.I(clk200_mmcm), .O(clk200));

    // ---- ddr2_lean ----------------------------------------------------------------------
    wire         ui_clk, ui_rst, calib_done;
    wire [26:0]  app_addr;
    wire [2:0]   app_cmd;
    wire         app_en, app_rdy, app_wdf_wren, app_wdf_rdy, app_rd_data_valid;
    wire [127:0] app_wdf_data, app_rd_data;
    wire [15:0]  app_wdf_mask;

    wire [23:0]  j_addr;                            // the JTAG debug port, below
    wire [31:0]  j_wdata;
    wire         j_re, j_we;
    reg  [31:0]  j_rdata = 32'd0;
    reg          j_rvalid = 1'b0;
    wire         r_we;
    wire [3:0]   r_addr;
    wire [31:0]  r_wdata, r_rdata;
    wire         cs_we;
    wire [3:0]   cs_addr;
    wire [31:0]  cs_wdata;
    reg          cal_done_r = 1'b0;                 // set below: the JTAG owns the register block from here on
    wire         j_lean = j_addr[23:8] == 16'h0001;
    assign r_we    = cal_done_r ? (j_we && j_lean) : cs_we;
    assign r_addr  = cal_done_r ? j_addr[3:0] : cs_addr;
    assign r_wdata = cal_done_r ? j_wdata : cs_wdata;

    ddr2_lean u_ddr (
        .ddr2_dq(ddr2_dq), .ddr2_dqs_n(ddr2_dqs_n), .ddr2_dqs_p(ddr2_dqs_p),
        .ddr2_addr(ddr2_addr), .ddr2_ba(ddr2_ba), .ddr2_ras_n(ddr2_ras_n),
        .ddr2_cas_n(ddr2_cas_n), .ddr2_we_n(ddr2_we_n), .ddr2_ck_p(ddr2_ck_p),
        .ddr2_ck_n(ddr2_ck_n), .ddr2_cke(ddr2_cke), .ddr2_cs_n(ddr2_cs_n),
        .ddr2_dm(ddr2_dm), .ddr2_odt(ddr2_odt),
        .sys_clk_i(clk200), .sys_rst(~locked),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en),
        .app_wdf_data(app_wdf_data), .app_wdf_end(app_wdf_wren), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren),
        .app_rd_data(app_rd_data), .app_rd_data_end(), .app_rd_data_valid(app_rd_data_valid),
        .app_rdy(app_rdy), .app_wdf_rdy(app_wdf_rdy),
        .ui_clk(ui_clk), .ui_clk_sync_rst(ui_rst), .init_calib_complete(calib_done),
        .r_we(r_we), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata)
    );

    // ---- init and calibration -----------------------------------------------------------
    wire        cs_init, cs_done, cs_ok;
    wire [4:0]  res_tap0, res_tap1;
    wire [2:0]  res_dly;
    wire [3:0]  res_bs, att_hits;
    wire [5:0]  res_w0, res_w1;
    wire        res_tr0, res_tr1;
    cal_seq cs (
        .clk(ui_clk), .rst(ui_rst), .r_we(cs_we), .r_addr(cs_addr), .r_wdata(cs_wdata), .r_rdata(r_rdata),
        .init_done(cs_init), .cal_done(cs_done), .cal_ok(cs_ok),
        .res_tap0(res_tap0), .res_tap1(res_tap1), .res_dly(res_dly), .res_bs(res_bs),
        .res_w0(res_w0), .res_w1(res_w1), .res_tr0(res_tr0), .res_tr1(res_tr1), .att_hits(att_hits)
    );
    // hand the register block to the JTAG when the sequence is over (one clock after cal_done:
    // its last write is out)
    always @(posedge ui_clk) cal_done_r <= cs_done && !ui_rst;

    // ---- tester -----------------------------------------------------------------------
    reg  [4:0]  ctl = 5'b11110;
    reg  [31:0] hold = 32'd0;
    reg         clear = 1'b0;
    wire [31:0] rounds, errors, cmds;
    wire        first_v;
    wire [AW-1:0] first_addr;
    wire [127:0] first_exp, first_act;
    wire [31:0] first_round;
    wire [3:0]  first_pass, pass_o;
    wire        order_o;
    ddr2_tester #(.AW(AW)) tester (
        .clk(ui_clk), .rst(ui_rst || !cs_ok || !calib_done), .run(ctl[4]), .ctl(ctl[3:0]), .clear(clear), .hold(hold),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_valid(app_rd_data_valid),
        .rounds(rounds), .errors(errors), .cmds(cmds), .first_v(first_v), .first_addr(first_addr),
        .first_exp(first_exp), .first_act(first_act), .first_round(first_round), .first_pass(first_pass),
        .pass_o(pass_o), .order_o(order_o)
    );

    // ---- JTAG register map ------------------------------------------------------------------
    jtag_dbg dbg (.clk(ui_clk), .addr(j_addr), .wdata(j_wdata), .re(j_re), .we(j_we), .rdata(j_rdata), .rvalid(j_rvalid));

    wire [31:0] first_a32 = {{(32-AW){1'b0}}, first_addr};
    always @(posedge ui_clk) begin
        clear <= 1'b0;
        j_rvalid <= j_re;
        if (j_we && j_addr[23:8] == 16'd0) case (j_addr[7:0])
        8'h20: ctl <= j_wdata[4:0];
        8'h21: hold <= j_wdata;
        8'h22: clear <= 1'b1;
        default: ;
        endcase
        if (j_re) begin
            if (j_lean) j_rdata <= r_rdata;
            else if (j_addr[23:8] != 16'd0) j_rdata <= 32'd0;
            else case (j_addr[7:0])
            8'h00: j_rdata <= 32'hDD2A0002;
            8'h01: j_rdata <= {24'd0, ui_rst, first_v, errors != 32'd0, ctl[4], calib_done, cs_ok, cs_done, cs_init};
            8'h02: j_rdata <= {4'd0, res_bs, 5'd0, res_dly, 3'd0, res_tap1, 3'd0, res_tap0};
            8'h03: j_rdata <= {8'd0, att_hits, 2'd0, res_tr1, res_tr0, 2'd0, res_w1, 2'd0, res_w0};
            8'h04: j_rdata <= rounds;
            8'h05: j_rdata <= errors;
            8'h06: j_rdata <= first_a32;
            8'h07: j_rdata <= first_round;
            8'h08: j_rdata <= {28'd0, first_pass};
            8'h09: j_rdata <= first_exp[31:0];
            8'h0A: j_rdata <= first_exp[63:32];
            8'h0B: j_rdata <= first_exp[95:64];
            8'h0C: j_rdata <= first_exp[127:96];
            8'h0D: j_rdata <= first_act[31:0];
            8'h0E: j_rdata <= first_act[63:32];
            8'h0F: j_rdata <= first_act[95:64];
            8'h10: j_rdata <= first_act[127:96];
            8'h11: j_rdata <= cmds;
            8'h12: j_rdata <= {27'd0, order_o, pass_o};
            8'h13: j_rdata <= AW;
            8'h20: j_rdata <= {27'd0, ctl};
            8'h21: j_rdata <= hold;
            default: j_rdata <= 32'd0;
            endcase
        end
    end

    // ---- LEDs --------------------------------------------------------------------------
    reg ok_f = 1'b0, err_f = 1'b0, pass_f = 1'b0;          // ui_clk, one bit each into the synchronizers
    always @(posedge ui_clk) begin
        ok_f   <= cs_ok && calib_done;
        err_f  <= errors != 32'd0;
        pass_f <= rounds != 32'd0;
    end
    (* ASYNC_REG = "TRUE" *) reg [2:0] cal_s, err_s, pass_s;
    always @(posedge clk50) begin
        cal_s  <= {cal_s[1:0], ok_f};
        err_s  <= {err_s[1:0], err_f};
        pass_s <= {pass_s[1:0], pass_f};
    end
    reg [25:0] blink = 0;
    always @(posedge clk50) blink <= blink + 1;
    assign LED_BOARDn = blink[25];
    assign LED_CASE1n = ~cal_s[2];
    assign LED_CASE2n = err_s[2] ? blink[22] : ~pass_s[2];
endmodule
