// T175: the layer 2 of the Port Diagnostics core (Alex 2026-11-02, T175 Q6: "a measuring core": no machine). Register block (cores/diag/rtl/diag_regs.v), the probe engine
// (cores/diag/rtl/probe.v), a raster for the host's scaler and the pixel layer (page 6, forwarded) the core application draws on. Built from the ports of layer2_null.v plus the two
// that the open-drain joystick pads need: kbjoy_i (the pads' level) and kbjoy_low (drive low). mono/top.v is the only user; the file is not a layer-2 contract file (the
// contract check reads cores/<id>/layer2_<id>.v) because it has these two extra ports. The build renames the module "layer2" (mono/build_mono.tcl).
`timescale 1ns / 1ps
module layer2_diag (
    input  wire IEC_ATN_I,
    output wire IEC_ATN_O,
    input  wire IEC_CLOCK_I,
    output wire IEC_CLOCK_O,
    input  wire IEC_DATA_I,
    output wire IEC_DATA_O,
    output wire SLOT_ADDRDIR,
    output wire SLOT_ADDROEn,
    output wire SLOT_BA,
    input  wire [7:0] SLOT_DATA,
    output wire SLOT_DATADIR,
    output wire SLOT_DATAOEn,
    input  wire SLOT_DMAn,
    output wire SLOT_DOTCLK,
    input  wire SLOT_EXROMn,
    input  wire SLOT_GAMEn,
    output wire SLOT_IO1n,
    output wire SLOT_IO2n,
    input  wire SLOT_IRQn,
    input  wire SLOT_NMIn,
    output wire SLOT_PHI2,
    output wire SLOT_ROMHn,
    output wire SLOT_ROMLn,
    input  wire SLOT_RST_I,
    output wire [15:0] ap_addr,
    input  wire ap_gnt,
    output wire ap_req,
    output wire [7:0] ap_wdata,
    input  wire app_cart_force_tgl,
    input  wire app_cart_kill_tgl,
    input  wire app_pause,
    input  wire [5:0] app_s2,
    input  wire app_stop,
    output wire signed [23:0] audio_l,
    output wire signed [23:0] audio_r,
    input  wire audio_sample_en,
    input  wire [7:0] bus_m_s2,
    output wire c64_atn_o,
    output wire c64_ce,
    output wire c64_clk_o,
    output wire c64_data_o,
    input  wire c64_freeze,
    input  wire c64_gclk,
    input  wire c64_halt_req,
    output wire c64_halted,
    input  wire [4:0] c64_joy1,
    input  wire [4:0] c64_joy2,
    input  wire [63:0] c64_keys,
    output wire [4:0] c64_leds,
    input  wire c64_restore,
    output wire [26:0] c_addr,
    output wire [2:0] c_cmd,
    output wire c_en,
    input  wire [127:0] c_rd_data,
    input  wire c_rd_valid,
    input  wire c_rdy,
    output wire [127:0] c_wdf_data,
    output wire [15:0] c_wdf_mask,
    input  wire c_wdf_rdy,
    output wire c_wdf_wren,
    output wire cart_active,
    input  wire [7:0] cart_type_s2,
    input  wire core_reset_req,
    input  wire cpu_clk,
    input  wire cpu_rst,
    input  wire [9:0] cram_addr,
    output wire [3:0] cram_rdata,
    output wire cw_ack,
    input  wire [16:0] cw_addr,
    output wire cw_irq,
    output wire [7:0] cw_rdata,
    input  wire cw_re,
    input  wire [7:0] cw_wdata,
    input  wire cw_we,
    output wire [7:0] d7ff_data,
    output wire d7ff_ev,
    input  wire [23:0] dbg_addr,
    output wire dbg_ev,
    output wire dbg_own,
    input  wire dbg_re,
    output wire dbg_stop,
    input  wire dbg_trx_sel,
    input  wire [31:0] dbg_wdata,
    input  wire dbg_we,
    input  wire [63:0] ddr_cal,
    input  wire ddr_calib,
    input  wire [15:0] dma_addr,
    input  wire dma_bus,
    output wire dma_done_tgl,
    output wire [7:0] dma_rdata_q,
    input  wire dma_req_tgl,
    input  wire dma_rom,
    input  wire [7:0] dma_wdata,
    input  wire dma_we,
    output wire [255:0] drive_dbg,
    output wire [2:0] drive_leds_n,
    output wire [25:0] drv_addr,
    output wire [3:0] drv_be,
    input  wire drv_c64_rst_n,
    input  wire drv_c64_stop,
    output wire drv_clk_o,
    output wire drv_data_o,
    input  wire [127:0] drv_done_rdata,
    input  wire drv_done_tgl,
    input  wire [1:0] drv_halt_s,
    output wire drv_io_ack,
    input  wire [23:0] drv_io_addr,
    output wire drv_io_nc,
    input  wire drv_io_rd,
    output wire [7:0] drv_io_rdata,
    input  wire [7:0] drv_io_wdata,
    input  wire drv_io_wr,
    input  wire drv_t16,
    input  wire drv_t1k,
    input  wire drv_t4,
    output wire drv_tgl,
    output wire [31:0] drv_wdata,
    output wire drv_we,
    output wire [26:0] e_addr,
    output wire [2:0] e_cmd,
    output wire e_en,
    input  wire e_rd_valid,
    input  wire e_rdy,
    output wire [127:0] e_wdf_data,
    output wire [15:0] e_wdf_mask,
    output wire e_wdf_wren,
    output wire gate_frz,
    output wire gcr_io_ack,
    input  wire [23:0] gcr_io_addr,
    output wire gcr_io_nc,
    input  wire gcr_io_rd,
    output wire [7:0] gcr_io_rdata,
    input  wire [7:0] gcr_io_wdata,
    input  wire gcr_io_wr,
    input  wire iec_port_off,
    input  wire iec_rst_n_cpu,
    input  wire [4:0] machine_ctl,
    input  wire [1:0] mc_halt_s,
    input  wire mclk,
    input  wire mclk_rst,
    output wire [3:0] pix_color,
    output wire [9:0] pix_x,
    output wire [8:0] pix_y,
    input  wire [15:0] port_addr,
    input  wire port_req,
    input  wire port_rom,
    input  wire [7:0] port_wdata,
    input  wire port_we,
    input  wire px_ev_flip,
    input  wire px_ev_vsync,
    input  wire [7:0] px_rdata,
    output wire px_re,
    output wire px_we,
    output wire [7:0] ram_rdata,
    output wire [23:0] reu_cmd_addr,
    input  wire reu_cmd_ready,
    output wire reu_cmd_valid,
    output wire reu_cmd_we,
    input  wire [15:0] reu_rdata,
    input  wire reu_rdata_valid,
    input  wire [3:0] reu_s2,
    output wire [15:0] reu_wdata,
    output wire [1:0] reu_wdata_we,
    input  wire sid2x_clk,
    input  wire [15:0] sid_base,
    input  wire [3:0] sid_digi,
    input  wire [31:0] sid_gains,
    input  wire [31:0] sid_gains2,
    input  wire [4:0] sid_lv_addr,
    output wire [7:0] sid_lv_data,
    input  wire [15:0] sid_mask,
    input  wire [1:0] sid_res,
    input  wire [2:0] sid_split,
    input  wire [1:0] sid_waves,
    input  wire [11:0] sidcv_addr,
    input  wire [7:0] sidcv_wdata,
    input  wire sidcv_we,
    output wire [15:0] slot_a,
    output wire slot_ahi_t,
    output wire [7:0] slot_d_o,
    output wire slot_d_t,
    output wire slot_rst_o,
    output wire slot_rw,
    input  wire smp_en,
    output wire [26:0] smp_f_addr,
    input  wire [31:0] smp_f_data,
    input  wire smp_f_done_tgl,
    output wire smp_f_tgl,
    input  wire [25:0] snp_addr,
    input  wire [3:0] snp_be,
    input  wire [7:0] snp_dack_tag,
    input  wire snp_rack,
    input  wire [7:0] snp_rack_tag,
    input  wire [31:0] snp_rdata,
    input  wire snp_req,
    input  wire snp_rwn,
    input  wire [7:0] snp_tag,
    input  wire [31:0] snp_wdata,
    input  wire stop_tgl,
    input  wire sys_clk,
    input  wire sys_rst,
    output wire [31:0] trx_j_rdata,
    output wire trx_j_rvalid,
    input  wire [7:0] turbo_en,
    input  wire [7:0] turbo_prefer,
    input  wire turbo_upd,
    output wire [15:0] tx_view,
    input  wire adc_bclk,
    input  wire adc_lrck,
    input  wire adc_sdo,
    input  wire c2n_motor_i,
    output wire c2n_motor_en,
    output wire c2n_motor_o,
    input  wire c2n_read_i,
    output wire c2n_read_en,
    output wire c2n_read_o,
    input  wire c2n_sense_i,
    output wire c2n_sense_o,
    input  wire c2n_write_i,
    output wire c2n_write_en,
    output wire c2n_write_o,
    input  wire [31:0] host_caps,
    input  wire [3:0] paddle_cmp_i,
    output wire paddle_adj_en,
    output wire paddle_adj_o,
    output wire paddle_dis_en,
    output wire paddle_dis_o,
    output wire [7:0] pp_data,
    output wire pp_eof,
    input  wire pp_grant,
    input  wire pp_ready,
    output wire pp_req,
    output wire pp_valid,
    output wire sid_sock1_n,
    output wire sid_sock2_n,
    output wire sid_sock_addr,
    input  wire ui_clk,
    input  wire ui_rst,
    input  wire [31:0] usr_access,
    input  wire [1:0] vmode_sys,
    output wire [9:0] win_cram_addr,
    output wire win_cram_use,
    output wire [15:0] win_port_addr,
    output wire win_port_rom,
    output wire win_port_use,
    output wire [7:0] win_port_wdata,
    output wire win_port_we,
    input  wire [9:0] kbjoy_i,
    output wire [9:0] kbjoy_low
);

    // ---------------------------------------------------------------- the window the application reads
    wire        e_run, e_abort, e_go, e_clr, e_stwe, e_rd, e_ev;
    wire [7:0]  e_staddr, e_pc;
    wire [31:0] e_stdata, e_fq;
    wire [9:0]  e_lvl;
    wire [13:0] e_live;
    wire        e_running, e_waiting, e_done, e_ovf, e_err;
    wire [9:0]  e_low;
    diag_regs regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(usr_access), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_eng(e_ev),
        .run(e_run), .abort(e_abort), .go(e_go), .fifo_clr(e_clr), .st_we(e_stwe), .st_addr(e_staddr), .st_wdata(e_stdata),
        .status({e_err, e_ovf, e_done, e_waiting, e_running}), .pc(e_pc), .fifo_q(e_fq), .fifo_level(e_lvl), .fifo_rd(e_rd),
        .live(e_live), .drv_low(e_low)
    );
    probe eng (
        .clk(cpu_clk), .rst(cpu_rst), .run(e_run), .abort(e_abort), .go(e_go), .fifo_clr(e_clr),
        .st_we(e_stwe), .st_addr(e_staddr), .st_wdata(e_stdata),
        .pins_async({paddle_cmp_i, kbjoy_i}), .drv_low(e_low),
        .pad_dis_o(paddle_dis_o), .pad_dis_en(paddle_dis_en), .pad_adj_o(paddle_adj_o), .pad_adj_en(paddle_adj_en),
        .fifo_rd(e_rd), .fifo_q(e_fq), .fifo_level(e_lvl), .live(e_live),
        .running(e_running), .waiting(e_waiting), .done(e_done), .ovf(e_ovf), .err(e_err), .pc_o(e_pc), .ev(e_ev)
    );
    assign kbjoy_low = e_low;

    // the drive and GCR windows: nothing behind them, they answer at once with 0 (a stray access is no bus timeout)
    reg drv_ack_r = 1'b0, gcr_ack_r = 1'b0;
    always @(posedge cpu_clk) begin
        drv_ack_r <= drv_io_rd | drv_io_wr;
        gcr_ack_r <= gcr_io_rd | gcr_io_wr;
    end
    assign drv_io_ack = drv_ack_r;
    assign gcr_io_ack = gcr_ack_r;
    assign drv_io_rdata = 8'd0;
    assign gcr_io_rdata = 8'd0;
    assign drv_io_nc = 1'b0;
    assign gcr_io_nc = 1'b0;

    // ---------------------------------------------------------------- the raster the host's scaler locks to
    // 504 dots of 4 sys_clk, 312 lines, colour 0; not tied to any machine, never stops (the raster of the NES and ZX cores)
    reg [1:0] dph = 2'd0;
    reg [8:0] dx = 9'd0, dy = 9'd0;
    always @(posedge sys_clk) begin
        dph <= dph + 2'd1;
        if (dph == 2'd3) begin
            if (dx == 9'd503) begin
                dx <= 9'd0;
                dy <= (dy == 9'd311) ? 9'd0 : dy + 9'd1;
            end else dx <= dx + 9'd1;
        end
    end
    assign pix_color = 4'd0;
    assign pix_x = {1'b0, dx};
    assign pix_y = dy;

    // ---------------------------------------------------------------- idle values (the decoupler's, host/top.v)
    assign IEC_ATN_O = 1'b0;
    assign IEC_CLOCK_O = 1'b0;
    assign IEC_DATA_O = 1'b0;
    assign c64_atn_o = 1'b1;
    assign c64_clk_o = 1'b1;
    assign c64_data_o = 1'b1;
    assign drv_clk_o = 1'b1;
    assign drv_data_o = 1'b1;
    assign SLOT_ADDRDIR = 1'b1;
    assign SLOT_ADDROEn = 1'b1;
    assign SLOT_BA = 1'b1;
    assign SLOT_DATADIR = 1'b1;
    assign SLOT_DATAOEn = 1'b1;
    assign SLOT_DOTCLK = 1'b0;
    assign SLOT_IO1n = 1'b1;
    assign SLOT_IO2n = 1'b1;
    assign SLOT_PHI2 = 1'b0;
    assign SLOT_ROMHn = 1'b1;
    assign SLOT_ROMLn = 1'b1;
    assign slot_a = {16{1'b1}};
    assign slot_ahi_t = 1'b1;
    assign slot_d_o = {8{1'b1}};
    assign slot_d_t = 1'b1;
    assign slot_rst_o = 1'b0;
    assign slot_rw = 1'b1;
    assign ap_addr = 16'd0;
    assign ap_req = 1'b0;
    assign ap_wdata = 8'd0;
    assign audio_l = 24'sd0;
    assign audio_r = 24'sd0;
    assign c64_ce = 1'b1;               // the enable of the host's c64_gclk gate: 0 would stop that clock
    assign c64_halted = 1'b0;
    assign c64_leds = 5'd0;
    assign c_addr = 27'd0;
    assign c_cmd = 3'd0;
    assign c_en = 1'b0;
    assign c_wdf_data = 128'd0;
    assign c_wdf_mask = 16'd0;
    assign c_wdf_wren = 1'b0;
    assign cart_active = 1'b0;
    assign cram_rdata = 4'd0;
    assign d7ff_data = 8'd0;
    assign d7ff_ev = 1'b0;
    assign dbg_ev = 1'b0;
    assign dbg_own = 1'b0;
    assign dbg_stop = 1'b0;
    assign dma_done_tgl = 1'b0;
    assign dma_rdata_q = 8'd0;
    assign drive_dbg = 256'd0;
    assign drive_leds_n = 3'b111;
    assign drv_addr = 26'd0;
    assign drv_be = 4'd0;
    assign drv_tgl = 1'b0;
    assign drv_we = 1'b0;
    assign drv_wdata = 32'd0;
    assign e_addr = 27'd0;
    assign e_cmd = 3'd0;
    assign e_en = 1'b0;
    assign e_wdf_data = 128'd0;
    assign e_wdf_mask = 16'd0;
    assign e_wdf_wren = 1'b0;
    assign gate_frz = 1'b0;
    assign pp_data = 8'd0;
    assign pp_eof = 1'b0;
    assign pp_req = 1'b0;
    assign pp_valid = 1'b0;
    assign ram_rdata = 8'd0;
    assign reu_cmd_addr = 24'd0;
    assign reu_cmd_valid = 1'b0;
    assign reu_cmd_we = 1'b0;
    assign reu_wdata = 16'd0;
    assign reu_wdata_we = 2'd0;
    assign sid_lv_data = 8'd0;
    assign sid_sock1_n = 1'b1;
    assign sid_sock2_n = 1'b1;
    assign sid_sock_addr = 1'b0;
    assign smp_f_addr = 27'd0;
    assign smp_f_tgl = 1'b0;
    assign c2n_motor_en = 1'b0;
    assign c2n_motor_o = 1'b0;
    assign c2n_read_en = 1'b0;
    assign c2n_read_o = 1'b0;
    assign c2n_sense_o = 1'b0;
    assign c2n_write_en = 1'b0;
    assign c2n_write_o = 1'b0;
    assign trx_j_rdata = 32'd0;
    assign trx_j_rvalid = 1'b1;         // the JTAG read of the TRX window completes at once (the decoupler's value)
    assign tx_view = 16'd0;
    assign win_cram_addr = 10'd0;
    assign win_cram_use = 1'b0;
    assign win_port_addr = 16'd0;
    assign win_port_rom = 1'b0;
    assign win_port_use = 1'b0;
    assign win_port_wdata = 8'd0;
    assign win_port_we = 1'b0;

endmodule
