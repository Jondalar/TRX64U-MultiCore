// ddr2_ctrl: the DDR2 controller of c64_basic, a drop-in for the MIG (OPT-ddr2 §9).
//
//   ddr2_lean  our controller and PHY (ddr2_lean.v), `app_*` as MIG's, ui_clk 75 MHz
//   cal_seq    init and read calibration in hardware through ddr2_lean's register block
//              (cal_seq.v: the sequence that ran 4 hours on the board in ddr2_test)
//
// `init_calib_complete` rises when cal_seq is done and ok; it stays low for ever when the
// calibration finds no window (then `cal_done` = 1, `cal_ok` = 0 and `cal_res` shows where it
// stopped). The calibration result, readable by the app (trx_io page 0, 0x101A0020) and by
// trxdbg (JTAG 0x100051, tools/trxdbg.py ddr2), is `cal_res`, no registers of its own: cal_seq's
// result registers are final once `cal_done` is set (ui_clk; the readers are in other clock
// domains and look at the word only after they have seen `cal_done` through their own
// synchronizer or after the boot ROM has seen DDR2 ready):
//   [31:0]  CAL0  [4:0] tap lane 0, [12:8] tap lane 1 (IDELAY taps, 78 ps), [18:16] read delay,
//                 [27:24] bitslip position (on failure: the position reached), [28] init done,
//                 [29] calibration done, [30] calibration ok, [31] controller up (= init_calib_complete)
//   [63:32] CAL1  [5:0] window width lane 0, [13:8] lane 1 (taps), [16] / [17] window cut by the
//                 32-tap scan range, [23:20] bitslip positions that had a window for lane 0
module ddr2_ctrl (
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

    input  wire        sys_clk_i,           // 200 MHz
    input  wire        sys_rst,             // active high

    input  wire [26:0] app_addr,
    input  wire [2:0]  app_cmd,
    input  wire        app_en,
    output wire        app_rdy,
    input  wire [127:0] app_wdf_data,
    input  wire [15:0] app_wdf_mask,
    input  wire        app_wdf_wren,
    output wire        app_wdf_rdy,
    output wire [127:0] app_rd_data,
    output wire        app_rd_data_valid,

    output wire        ui_clk,
    output wire        ui_clk_sync_rst,
    output wire        init_calib_complete,

    output wire        cal_done,            // the sequence ended (ok or not)
    output wire        cal_ok,
    output wire [63:0] cal_res
);
    wire        r_we;
    wire [3:0]  r_addr;
    wire [31:0] r_wdata, r_rdata;
    ddr2_lean u_lean (
        .ddr2_dq(ddr2_dq), .ddr2_dqs_n(ddr2_dqs_n), .ddr2_dqs_p(ddr2_dqs_p),
        .ddr2_addr(ddr2_addr), .ddr2_ba(ddr2_ba), .ddr2_ras_n(ddr2_ras_n),
        .ddr2_cas_n(ddr2_cas_n), .ddr2_we_n(ddr2_we_n), .ddr2_ck_p(ddr2_ck_p),
        .ddr2_ck_n(ddr2_ck_n), .ddr2_cke(ddr2_cke), .ddr2_cs_n(ddr2_cs_n),
        .ddr2_dm(ddr2_dm), .ddr2_odt(ddr2_odt),
        .sys_clk_i(sys_clk_i), .sys_rst(sys_rst),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en),
        .app_wdf_data(app_wdf_data), .app_wdf_end(app_wdf_wren), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren),
        .app_rd_data(app_rd_data), .app_rd_data_end(), .app_rd_data_valid(app_rd_data_valid),
        .app_rdy(app_rdy), .app_wdf_rdy(app_wdf_rdy),
        .ui_clk(ui_clk), .ui_clk_sync_rst(ui_clk_sync_rst), .init_calib_complete(init_calib_complete),
        .r_we(r_we), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata)
    );

    wire        cs_init;
    wire [4:0]  res_tap0, res_tap1;
    wire [2:0]  res_dly;
    wire [3:0]  res_bs, att_hits;
    wire [5:0]  res_w0, res_w1;
    wire        res_tr0, res_tr1;
    cal_seq cs (
        .clk(ui_clk), .rst(ui_clk_sync_rst), .r_we(r_we), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata),
        .init_done(cs_init), .cal_done(cal_done), .cal_ok(cal_ok),
        .res_tap0(res_tap0), .res_tap1(res_tap1), .res_dly(res_dly), .res_bs(res_bs),
        .res_w0(res_w0), .res_w1(res_w1), .res_tr0(res_tr0), .res_tr1(res_tr1), .att_hits(att_hits)
    );

    assign cal_res = {8'd0, att_hits, 2'd0, res_tr1, res_tr0, 2'd0, res_w1, 2'd0, res_w0,
                      init_calib_complete, cal_ok, cal_done, cs_init, res_bs, 5'd0, res_dly, 3'd0, res_tap1, 3'd0, res_tap0};
endmodule
