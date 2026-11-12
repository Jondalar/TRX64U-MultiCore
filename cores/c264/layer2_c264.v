// T90: the Commodore 264 core (C16, C116, Plus/4) as a layer-2 module of the TRX64 Ultimate DFX design. Same ports as
// layer2.v (the partition's edge is fixed, T79.1), a TED machine behind them (cores/c264/rtl, licences: NOTICE there). The
// 1541 (drive_l2.vhd) and the IEC bus logic are the C64 module's own, instantiated the same way (as layer2_vc20.v); the
// frame buffer of the host's pixel layer is written through the general DDR2 port e_* (c264_pixw); what this module does
// not use sits at the idle value the host's decoupler holds (top.v, T79.1 section 7). Built only with TRX_CORE=c264
// (host/build.tcl).
`timescale 1ns / 1ps
module layer2_c264 #(
    parameter [25:0] REU_BASE = 26'h0800000
) (
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
    output wire win_port_we
);

    wire c64_atn_i, c64_clk_i, c64_data_i;
    wire drv_atn_i, drv_clk_i, drv_data_i;
    wire c2_halted, c2_running;

    // the 1541 and the serial bus, as in layer2.v; the debug master of the C64 module is not here
    wire [2:0] drva_leds_n;
    drive_leds_map drv_led_map (.drv_leds_n(drva_leds_n), .drive_leds_n(drive_leds_n));
    drive_l2 drv (
        .clock(cpu_clk), .reset(cpu_rst), .tick_16MHz(drv_t16), .tick_4MHz(drv_t4), .tick_1kHz(drv_t1k),
        .drv_io_read(drv_io_rd), .drv_io_write(drv_io_wr), .drv_io_addr(drv_io_addr), .drv_io_wdata(drv_io_wdata),
        .drv_io_rdata(drv_io_rdata), .drv_io_ack(drv_io_ack), .drv_io_nc(drv_io_nc),
        .gcr_io_read(gcr_io_rd), .gcr_io_write(gcr_io_wr), .gcr_io_addr(gcr_io_addr), .gcr_io_wdata(gcr_io_wdata),
        .gcr_io_rdata(gcr_io_rdata), .gcr_io_ack(gcr_io_ack), .gcr_io_nc(gcr_io_nc),
        .dp_req_tgl(drv_tgl), .dp_addr(drv_addr), .dp_we(drv_we), .dp_wdata(drv_wdata), .dp_be(drv_be),
        .dp_done_tgl(drv_done_tgl), .dp_rdata(drv_done_rdata),
        .snoop_tag(snp_tag), .snoop_request(snp_req), .snoop_rwn(snp_rwn), .snoop_addr(snp_addr),
        .snoop_be(snp_be), .snoop_wdata(snp_wdata), .snoop_rdata(snp_rdata), .snoop_rack(snp_rack),
        .snoop_rack_tag(snp_rack_tag), .snoop_dack_tag(snp_dack_tag),
        .drive_stop(drv_c64_stop), .halt(1'b0), .c64_reset_n(drv_c64_rst_n), .iec_reset_n(iec_rst_n_cpu),
        .iec_atn_i(drv_atn_i), .iec_clk_i(drv_clk_i), .iec_data_i(drv_data_i),
        .iec_clk_o(drv_clk_o), .iec_data_o(drv_data_o), .leds_n(drva_leds_n),
        .dbg(drive_dbg), .cpu_trace(), .cpu_trace_valid()
    );

    iec_bus iec (
        .clk(sys_clk), .c64_atn_o(c64_atn_o), .c64_clk_o(c64_clk_o), .c64_data_o(c64_data_o),
        .c64_atn_i(c64_atn_i), .c64_clk_i(c64_clk_i), .c64_data_i(c64_data_i),
        .cpu_clk(cpu_clk), .drv_clk_o(drv_clk_o), .drv_data_o(drv_data_o),
        .drv_atn_i(drv_atn_i), .drv_clk_i(drv_clk_i), .drv_data_i(drv_data_i),
        .port_off(iec_port_off),
        .port_atn_i(IEC_ATN_I), .port_clk_i(IEC_CLOCK_I), .port_data_i(IEC_DATA_I),
        .port_atn_o(IEC_ATN_O), .port_clk_o(IEC_CLOCK_O), .port_data_o(IEC_DATA_O)
    );

    // the machine: its glue, memories and registers are in cores/c264/rtl/trx.
    // Every pause stops the whole machine (T83 Q6, changed): the app's C64_STOP (app_stop, the host's app_pause gate
    // is the C64 layer's own and stays 0 here), the JTAG pause (machine_ctl[1]) and the host's halts. The core gates its one
    // TED clock enable (c264_core.v), mutes the sound and keeps the dummy raster for the scaler running.
    wire tp_motor_on, tp_write;
    wire [7:0] tp_pin;
    c264_core #(.ID_TRX2(0)) c2 (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .reset_req(core_reset_req), .halt_req(c64_halt_req | machine_ctl[1] | app_stop | mc_halt_s[1]), .halted(c2_halted), .running(c2_running),
        .keys(c64_keys), .joy1(c64_joy1), .joy2(c64_joy2), .restore(c64_restore),
        .iec_atn_i(c64_atn_i), .iec_clk_i(c64_clk_i), .iec_data_i(c64_data_i),
        .iec_atn_o(c64_atn_o), .iec_clk_o(c64_clk_o), .iec_data_o(c64_data_o),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata_q), .dbg_busy(dbg_re | dbg_we),
        .win_port_use(win_port_use), .win_port_addr(win_port_addr), .win_port_rom(win_port_rom),
        .win_port_we(win_port_we), .win_port_wdata(win_port_wdata),
        .port_req(port_req), .port_addr(port_addr), .port_rom(port_rom), .port_we(port_we),
        .port_wdata(port_wdata), .ram_rdata(ram_rdata),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata),
        .cw_ack(cw_ack), .cw_irq(cw_irq),
        .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata), .px_ev_vsync(px_ev_vsync), .px_ev_flip(px_ev_flip),
        .build(usr_access), .ddr_cal(ddr_cal),
        .dbg_re(dbg_re && dbg_trx_sel), .dbg_addr(dbg_addr[11:0]), .j_rdata(trx_j_rdata), .j_rvalid(trx_j_rvalid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(e_wdf_wren),
        .tp_read_pin(c2n_read_i), .tp_sense_pin(c2n_sense_i), .tp_motor_on(tp_motor_on), .tp_write(tp_write), .tp_pin(tp_pin)
    );
    assign c64_halted = c2_halted;
    assign c64_leds = {4'b0000, c2_running};

    // idle values of everything the C264 does not use (the decoupler's values, top.v)
    assign SLOT_ADDRDIR = 1'b1;
    assign SLOT_ADDROEn = 1'b1;
    assign SLOT_BA = 1'b1;
    assign SLOT_DATADIR = 1'b1;
    assign SLOT_DATAOEn = 1'b1;
    assign SLOT_DOTCLK = 'd0;
    assign SLOT_IO1n = 1'b1;
    assign SLOT_IO2n = 1'b1;
    assign SLOT_PHI2 = 'd0;
    assign SLOT_ROMHn = 1'b1;
    assign SLOT_ROMLn = 1'b1;
    assign ap_addr = 'd0;
    assign ap_req = 'd0;
    assign ap_wdata = 'd0;
    assign c64_ce = 1'b1;
    assign c_addr = 'd0;
    assign c_cmd = 'd0;
    assign c_en = 'd0;
    assign c_wdf_data = 'd0;
    assign c_wdf_mask = 'd0;
    assign c_wdf_wren = 'd0;
    assign cart_active = 'd0;
    assign cram_rdata = 'd0;
    assign d7ff_data = 'd0;
    assign d7ff_ev = 'd0;
    assign dbg_ev = 'd0;
    assign dbg_own = 'd0;
    assign dbg_stop = 'd0;
    assign gate_frz = 'd0;
    assign reu_cmd_addr = 'd0;
    assign reu_cmd_valid = 'd0;
    assign reu_cmd_we = 'd0;
    assign reu_wdata = 'd0;
    assign reu_wdata_we = 'd0;
    assign sid_lv_data = 'd0;
    assign slot_a = {16{1'b1}};
    assign slot_ahi_t = 1'b1;
    assign slot_d_o = {8{1'b1}};
    assign slot_d_t = 1'b1;
    assign slot_rst_o = 'd0;
    assign slot_rw = 1'b1;
    assign smp_f_addr = 'd0;
    assign smp_f_tgl = 'd0;
    assign tx_view = 'd0;
    // T103 contract v2: pins and the packet port, tied off (outputs idle, pads released, sockets deselected)
    // the cassette port (T214): every pin is released until TAPE_PIN (0x0B2) says otherwise. MOTOR out, WRITE out (not proven by a SAVE on real tape yet),
    // READ and SENSE in; the C16/Plus4 has no file streamer, so READ and SENSE are never driven.
    reg r_mot = 1'b0, r_wr = 1'b1;
    always @(posedge sys_clk) begin r_mot <= tp_motor_on; r_wr <= tp_write; end
    assign c2n_motor_o = r_mot;
    assign c2n_motor_en = tp_pin[0];
    assign c2n_write_o = r_wr;
    assign c2n_write_en = tp_pin[1];
    assign c2n_read_en = 1'b0;
    assign c2n_read_o = 1'b1;
    assign c2n_sense_o = 1'b0;
    assign paddle_adj_en = 1'b0;
    assign paddle_adj_o = 1'b0;
    assign paddle_dis_en = 1'b0;
    assign paddle_dis_o = 1'b0;
    assign pp_data = 8'd0;
    assign pp_eof = 1'b0;
    assign pp_req = 1'b0;
    assign pp_valid = 1'b0;
    assign sid_sock1_n = 1'b1;
    assign sid_sock2_n = 1'b1;
    assign sid_sock_addr = 1'b0;
    assign win_cram_addr = 'd0;
    assign win_cram_use = 'd0;

endmodule
