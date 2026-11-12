// The c64u core (T103): Gideon's stock C64U feature set as one layer 2 for the host's contract (contract v2, 247 ports). The C64
// with its expansion port, cartridge model, REU, turbo, the sampler and the 8-SID reDIP; one drive (Gideon's mm_drive: 1541 or
// 1581), the GCR codec, the UCI, the UDP video and audio streams on the packet port, the tape streamer and recorder,
// the cassette, POT and capability words in the cw_* window (c64u_cw.vh). NOT in it: the ring, the TRX64 engine, the debugger,
// the lens unit, the state bus (trx_engine, trx_dbg, drv_dbg, trx_sys, trx_io of the reference core), the 1571, drive B,
// ACIA/SwiftLink and the user port (T103 §4).
// Self-contained (Q5): every module that is also in the deleted trx64 core, cores/common or host/ is a copy here with the suffix _c64u.
`timescale 1ns / 1ps
`include "c64u_cw.vh"
module layer2_c64u #(
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

    // ---- boundary rule (T87, runs 396 to 398; T103 tie-offs) -------------------------------------------------------------
    // An output that is a constant must not be a constant at the partition boundary: it is a GNDNet in the linked design, the
    // static's load pins are routed as part of it, and a core carved out and put back has no net to drive them
    // ([Route 35-341]). Outputs that can be constant (audio low bits, bits of the DDR2 addresses, the NC flags, a feature that is
    // off) go through a DONT_TOUCH LUT1 (c64u_keep); outputs that are always constant (the ports of blocks this core does not
    // have) are one DONT_TOUCH flop per bit that holds its power-up value (c64u_tie_ff: a FF, no LUT).
    wire signed [23:0] k_audio_l, k_audio_r;
    wire [7:0] k_sid_lv_data;
    wire [26:0] k_smp_f_addr, k_c_addr;
    wire [23:0] k_reu_cmd_addr;
    wire [15:0] k_reu_wdata;
    wire [1:0] k_reu_wdata_we;
    wire k_smp_f_tgl, k_SLOT_ADDRDIR, k_drv_io_nc, k_gcr_io_nc, k_reu_cmd_valid, k_reu_cmd_we;
    c64u_keep #(.W(24)) keep_audio_l (.i(k_audio_l), .o(audio_l));
    c64u_keep #(.W(24)) keep_audio_r (.i(k_audio_r), .o(audio_r));
    c64u_keep #(.W(8))  keep_sid_lv  (.i(k_sid_lv_data), .o(sid_lv_data));
    c64u_keep #(.W(27)) keep_smp_a   (.i(k_smp_f_addr), .o(smp_f_addr));
    c64u_keep #(.W(1))  keep_smp_t   (.i(k_smp_f_tgl), .o(smp_f_tgl));
    c64u_keep #(.W(1))  keep_adir    (.i(k_SLOT_ADDRDIR), .o(SLOT_ADDRDIR));
    c64u_keep #(.W(1))  keep_drv_nc  (.i(k_drv_io_nc), .o(drv_io_nc));
    c64u_keep #(.W(1))  keep_gcr_nc  (.i(k_gcr_io_nc), .o(gcr_io_nc));
    c64u_keep #(.W(24)) keep_reu_a   (.i(k_reu_cmd_addr), .o(reu_cmd_addr));
    c64u_keep #(.W(1))  keep_reu_v   (.i(k_reu_cmd_valid), .o(reu_cmd_valid));
    c64u_keep #(.W(1))  keep_reu_w   (.i(k_reu_cmd_we), .o(reu_cmd_we));
    c64u_keep #(.W(16)) keep_reu_d   (.i(k_reu_wdata), .o(reu_wdata));
    c64u_keep #(.W(2))  keep_reu_dwe (.i(k_reu_wdata_we), .o(reu_wdata_we));
    c64u_keep #(.W(3))  keep_c_lo    (.i(k_c_addr[2:0]), .o(c_addr[2:0]));
    assign c_addr[26:3] = k_c_addr[26:3];

    // the ports of blocks this core does not have (the ring's APPLY, the debugger, its JTAG page, the drive debug map, the
    // reference core's second DDR2 port) and the POT adjust pin, which has no logic yet
    c64u_tie_ff #(.W(495), .INIT({{256{1'b0}}, 176'd0, 25'h0000000, 3'd0, 33'd0, 1'b0, 1'b0})) tie_ff (.clk(cpu_clk),
        .o({drive_dbg,
            e_addr, e_cmd, e_en, e_wdf_data, e_wdf_mask, e_wdf_wren,
            ap_addr, ap_req, ap_wdata,
            dbg_ev, dbg_own, dbg_stop,
            trx_j_rdata, trx_j_rvalid,
            paddle_adj_en, paddle_adj_o}));

    // ---- the application's window (cw_*) ------------------------------------------------------------------------------------
    wire        rq_tgl, rs_tgl, sm_tgl, smp_a_rs_tgl_w, gate_shut, turbo_ran_clr, slot_wr_en;
    wire [31:0] turbo_stat, turbo_w1, turbo_w2, slot_status, slot_cnt0, slot_cnt1, slot_last;
    wire [15:0] rq_addr;
    wire        rq_we;
    wire  [7:0] rq_wdata, rs_rdata, sm_addr, sm_wdata, sm_rdata;
    wire        sm_we;
    wire        cart_fl_enable;
    wire [15:0] cart_fl_magic1, cart_fl_magic2, cart_fl_mask;
    wire [23:0] cart_fl_id;
    wire [343:0] cart_desc;
    wire        cart_evt_tgl, cart_fl_ack_tgl, cart_inval_tgl, cart_ee_tgl;
    wire [33:0] cart_evt_word;
    wire  [1:0] cart_rom_sel;
    wire [10:0] cart_ee_addr;
    wire        cart_ee_we;
    wire  [7:0] cart_ee_wdata, cart_ee_rdata;
    wire        sid_resid8;
    wire [15:0] sock_base, sock_mask;
    wire  [1:0] sock_en;
    wire  [2:0] sock_split;
    wire [31:0] sock_gain;
    wire [49:0] sid_rs8;
    wire  [7:0] pin_ctrl;
    wire        udp_we, stream_mute;
    wire  [7:0] udp_addr, udp_wdata, stream_ena;
    wire        uci_re, uci_we, pb_re, pb_we, rc_re, rc_we, uci_ack, pb_ack, rc_ack;
    wire [12:0] uci_addr;
    wire  [7:0] uci_rdata, pb_rdata, rc_rdata;
    wire        uci_irq, drva_irq, rc_irq;
    wire [31:0] core_caps = host_caps | `CAPAB_HARDWARE_GCR | `CAPAB_C2N_STREAMER | `CAPAB_C2N_RECORDER |
                            `CAPAB_RAM_EXPANSION | `CAPAB_MM_DRIVE | `CAPAB_EXTENDED_REU | `CAPAB_COMMAND_INTF;

    c64u_io cwio (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata),
        .ack(cw_ack), .irq(cw_irq),
        .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata), .rs_tgl(rs_tgl), .rs_rdata(rs_rdata),
        .sm_tgl(sm_tgl), .sm_addr(sm_addr), .sm_we(sm_we), .sm_wdata(sm_wdata), .sm_rs_tgl(smp_a_rs_tgl_w), .sm_rdata(sm_rdata),
        .p6_we(px_we), .p6_re(px_re), .p6_rdata(px_rdata), .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip),
        .fl_enable(cart_fl_enable), .fl_magic1(cart_fl_magic1), .fl_magic2(cart_fl_magic2),
        .fl_mask(cart_fl_mask), .fl_id(cart_fl_id), .cart_desc(cart_desc), .evt_tgl(cart_evt_tgl), .evt_word(cart_evt_word),
        .fl_ack_tgl(cart_fl_ack_tgl), .inval_tgl(cart_inval_tgl), .rom_sel(cart_rom_sel), .ee_tgl(cart_ee_tgl),
        .ee_addr(cart_ee_addr), .ee_we(cart_ee_we), .ee_wdata(cart_ee_wdata), .ee_rdata(cart_ee_rdata),
        .build(usr_access), .ddr_cal(ddr_cal),
        .sid_resid8(sid_resid8), .rs8_cfg(sid_rs8),
        .core_caps(core_caps), .level_irq({rc_irq, 1'b0, drva_irq, uci_irq}), .pin_ctrl(pin_ctrl),
        .sock_base(sock_base), .sock_mask(sock_mask), .sock_en(sock_en), .sock_split(sock_split), .sock_gain(sock_gain),
        .udp_we(udp_we), .udp_addr(udp_addr), .udp_wdata(udp_wdata), .stream_ena(stream_ena), .stream_mute(stream_mute),
        .uci_re(uci_re), .uci_we(uci_we), .uci_addr(uci_addr), .uci_rdata(uci_rdata), .uci_ack(uci_ack),
        .pb_re(pb_re), .pb_we(pb_we), .pb_rdata(pb_rdata), .pb_ack(pb_ack),
        .rc_re(rc_re), .rc_we(rc_we), .rc_rdata(rc_rdata), .rc_ack(rc_ack)
    );

    c64u_sys csys (
        .clk(sys_clk), .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata),
        .rs_tgl(rs_tgl), .rs_rdata(rs_rdata),
        .gate_frz(gate_frz), .turbo_stat(turbo_stat), .turbo_w1(turbo_w1), .turbo_w2(turbo_w2),
        .turbo_ran_clr(turbo_ran_clr), .slot_wr_en(slot_wr_en),
        .slot_status(slot_status), .slot_cnt0(slot_cnt0), .slot_cnt1(slot_cnt1), .slot_last(slot_last),
        .gate_shut(gate_shut), .halted(c64_halted), .tx_view(tx_view)
    );

    // ---- cartridge image and RAM in DDR2 (ui_clk) ----------------------------------------------------------------------------
    wire        cart_req_tgl, cart_req_we, cart_req_cached, cart_ack_tgl;
    wire [23:0] cart_req_addr;
    wire  [7:0] cart_req_data;
    wire  [5:0] cart_req_slot;
    wire  [8:0] cart_fill_addr;
    wire  [7:0] cart_fill_be;
    wire [63:0] cart_fill_data;
    cart_fill_c64u #(.ROM_BASE_BLOCK({1'b0, REU_BASE})) cfill (
        .ui_clk(ui_clk), .ui_rst(ui_rst || !ddr_calib), .rom_sel(cart_rom_sel),
        .req_tgl(cart_req_tgl), .req_we(cart_req_we), .req_addr(cart_req_addr),
        .req_data(cart_req_data), .req_slot(cart_req_slot), .req_cached(cart_req_cached),
        .ack_tgl(cart_ack_tgl),
        .c_be(cart_fill_be), .c_addr(cart_fill_addr), .c_data(cart_fill_data),
        .app_addr(k_c_addr), .app_cmd(c_cmd), .app_en(c_en), .app_rdy(c_rdy),
        .app_wdf_data(c_wdf_data), .app_wdf_mask(c_wdf_mask), .app_wdf_wren(c_wdf_wren),
        .app_wdf_rdy(c_wdf_rdy), .app_rd_data(c_rd_data), .app_rd_data_valid(c_rd_valid)
    );

    // ---- the app's DMA window into the C64 -----------------------------------------------------------------------------------
    wire        dma_cyc_req, dma_cyc_we, dma_cyc_done, cram_we_w;
    wire [15:0] dma_cyc_addr;
    wire  [7:0] dma_cyc_wdata, dma_cyc_rdata;
    wire  [3:0] cram_wdata_w;
    dma_win_c64u win (
        .clk(sys_clk),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata),
        .dma_bus(dma_bus), .dma_rom(dma_rom), .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata_q),
        .dbg_busy(dbg_re || dbg_we), .port_use(win_port_use), .port_addr(win_port_addr),
        .port_rom(win_port_rom), .port_we(win_port_we), .port_wdata(win_port_wdata),
        .port_rdata(ram_rdata),
        .cyc_req(dma_cyc_req), .cyc_addr(dma_cyc_addr), .cyc_we(dma_cyc_we), .cyc_wdata(dma_cyc_wdata),
        .cram_addr(win_cram_addr), .cram_we(cram_we_w), .cram_wdata(cram_wdata_w), .cram_rdata(cram_rdata),
        .cram_use(win_cram_use),
        .cyc_done(dma_cyc_done), .cyc_rdata(dma_cyc_rdata)
    );

    // ---- the serial bus: the C64, the drive, the board port ----------------------------------------------------------------
    wire c64_atn_i, c64_clk_i, c64_data_i;
    wire drv_atn_i, drv_clk_i, drv_data_i;
    wire drva_clk_o, drva_data_o;
    iec_bus_c64u iec (
        .clk(sys_clk), .c64_atn_o(c64_atn_o), .c64_clk_o(c64_clk_o), .c64_data_o(c64_data_o),
        .c64_atn_i(c64_atn_i), .c64_clk_i(c64_clk_i), .c64_data_i(c64_data_i),
        .cpu_clk(cpu_clk), .drv_clk_o(drva_clk_o), .drv_data_o(drva_data_o),
        .drv_atn_i(drv_atn_i), .drv_clk_i(drv_clk_i), .drv_data_i(drv_data_i),
        .port_off(iec_port_off),
        .port_atn_i(IEC_ATN_I), .port_clk_i(IEC_CLOCK_I), .port_data_i(IEC_DATA_I),
        .port_atn_o(IEC_ATN_O), .port_clk_o(IEC_CLOCK_O), .port_data_o(IEC_DATA_O)
    );

    // ---- the drive (Gideon's mm_drive: a 1541 or a 1581 as the app selects) at 0x10020000 -----------------------------------
    // One drive (Alex, 2026-10-17, translated: "Let drive B go .. 1541 and 1581 work, just as one drive each"): no drive B,
    // no CAPAB_DRIVE_1541_2, so the app creates no second drive; the drive talks to the drv_* port directly, as in trx64.
    wire  [2:0] drva_leds_n;
    wire        drv_halt = drv_halt_s[1];
    c64u_drive_l2 #(
        .g_cpu_tag(8'h01), .g_floppy_tag(8'h02), .g_disk_tag(8'h03), .g_audio_tag(8'h04),
        .g_audio_base(28'h0EC0000), .g_ram_base(28'h0EE0000)
    ) drva (
        .clock(cpu_clk), .reset(cpu_rst), .tick_16MHz(drv_t16), .tick_4MHz(drv_t4), .tick_1kHz(drv_t1k),
        .io_read(drv_io_rd), .io_write(drv_io_wr), .io_addr(drv_io_addr), .io_wdata(drv_io_wdata),
        .io_rdata(drv_io_rdata), .io_ack(drv_io_ack), .io_nc(k_drv_io_nc), .io_irq(drva_irq),
        .dp_req_tgl(drv_tgl), .dp_addr(drv_addr), .dp_we(drv_we), .dp_wdata(drv_wdata), .dp_be(drv_be),
        .dp_done_tgl(drv_done_tgl), .dp_rdata(drv_done_rdata),
        .snoop_tag(snp_tag), .snoop_request(snp_req), .snoop_rwn(snp_rwn), .snoop_addr(snp_addr),
        .snoop_be(snp_be), .snoop_wdata(snp_wdata), .snoop_rdata(snp_rdata), .snoop_rack(snp_rack),
        .snoop_rack_tag(snp_rack_tag), .snoop_dack_tag(snp_dack_tag),
        .drive_stop(drv_c64_stop), .halt(drv_halt), .c64_reset_n(drv_c64_rst_n), .iec_reset_n(iec_rst_n_cpu),
        .iec_atn_i(drv_atn_i), .iec_clk_i(drv_clk_i), .iec_data_i(drv_data_i),
        .iec_clk_o(drva_clk_o), .iec_data_o(drva_data_o), .leds_n(drva_leds_n)
    );
    assign drive_leds_n = drva_leds_n;
    // the GCR codec the app's disk image code uses (0x10060500)
    c64u_gcr_l2 gcr (
        .clock(cpu_clk), .reset(cpu_rst),
        .gcr_io_read(gcr_io_rd), .gcr_io_write(gcr_io_wr), .gcr_io_addr(gcr_io_addr), .gcr_io_wdata(gcr_io_wdata),
        .gcr_io_rdata(gcr_io_rdata), .gcr_io_ack(gcr_io_ack), .gcr_io_nc(k_gcr_io_nc)
    );

    // ---- the UCI (Gideon's command interface in the C64's clock, 8 KB window in the cw page A/B) -------------------------
    wire [15:0] bus_addr_w, io_ev_addr;
    wire  [7:0] io_ev_data, uci_dout;
    wire        io_ev, io_ev_we, io_ev_io, uci_oe, uci_c64_irq, uci_freeze;
    c64u_uci_l2 uci (
        .io_clock(cpu_clk), .io_reset(cpu_rst),
        .io_read(uci_re), .io_write(uci_we), .io_addr(uci_addr), .io_wdata(cw_wdata),
        .io_rdata(uci_rdata), .io_ack(uci_ack), .io_irq(uci_irq),
        .c64_clock(sys_clk), .bus_addr(bus_addr_w), .ev(io_ev), .ev_addr(io_ev_addr), .ev_data(io_ev_data),
        .ev_we(io_ev_we), .ev_io(io_ev_io), .dout(uci_dout), .oe(uci_oe), .c64_irq(uci_c64_irq), .freeze(uci_freeze)
    );

    // ---- the cassette port ----------------------------------------------------------------------------------------------------
    // A pulse in cpu_clk per C64 cycle (the recorder's time base) from the C64's bus events (sys_clk, gated: none while paused).
    reg ev_tgl = 1'b0;
    always @(posedge sys_clk) if (io_ev) ev_tgl <= ~ev_tgl;
    (* ASYNC_REG = "TRUE" *) reg [2:0] ev_s = 3'b000;
    always @(posedge cpu_clk) ev_s <= {ev_s[1:0], ev_tgl};
    wire phi2_pulse = ev_s[2] != ev_s[1];
    // the machine's side of the port into cpu_clk
    (* ASYNC_REG = "TRUE" *) reg [1:0] mot_s = 2'b11, wr_s = 2'b11, rdp_s = 2'b11, snp_s = 2'b11;
    wire tape_motor_n, tape_write;
    always @(posedge cpu_clk) begin
        mot_s <= {mot_s[0], tape_motor_n};
        wr_s  <= {wr_s[0], tape_write};
        rdp_s <= {rdp_s[0], c2n_read_i};
        snp_s <= {snp_s[0], c2n_sense_i};
    end
    wire motor_on = !mot_s[1];
    wire sense_pull, c2n_rd_o, c2n_rd_en, c2n_wr_o, c2n_wr_en;
    wire sense_low = sense_pull || (pin_ctrl[4] && !snp_s[1]);   // the SENSE line is low: our own pull, or an external datasette's key
    c64u_c2n_l2 c2n (
        .clock(cpu_clk), .reset(cpu_rst), .c64_stopped(drv_halt), .phi2_tick(phi2_pulse),
        .pb_read(pb_re), .pb_write(pb_we), .pb_addr(cw_addr[11:0]), .pb_wdata(cw_wdata), .pb_rdata(pb_rdata), .pb_ack(pb_ack),
        .rc_read(rc_re), .rc_write(rc_we), .rc_addr(cw_addr[11:0]), .rc_wdata(cw_wdata), .rc_rdata(rc_rdata), .rc_ack(rc_ack),
        .rc_irq(rc_irq),
        .motor_on(motor_on), .sense_pressed_i(sense_low), .sense_pull_o(sense_pull),
        .read_i(pin_ctrl[4] ? rdp_s[1] : 1'b1), .write_i(wr_s[1]),
        .read_o(c2n_rd_o), .read_en(c2n_rd_en), .write_o(c2n_wr_o), .write_en(c2n_wr_en)
    );
    // and back into the C64's clock: CIA 1 FLAG = the streamer's line while it drives it, else the pin if used, else idle high
    wire rd_line = c2n_rd_en ? c2n_rd_o : (pin_ctrl[4] ? rdp_s[1] : 1'b1);
    (* ASYNC_REG = "TRUE" *) reg [1:0] trd_s = 2'b11, tsn_s = 2'b11;
    always @(posedge sys_clk) begin
        trd_s <= {trd_s[0], rd_line};
        tsn_s <= {tsn_s[0], !sense_low};
    end
    // pins: released until PIN_CTRL says otherwise. Directions (same board, same C2N pins in every core): MOTOR out, READ and SENSE in (proven: a real datasette LOADs on the PET, GitHub #26, 2026-11-12), WRITE out (not proven by a SAVE on real tape yet; T214)
    reg r_mot_o = 1'b0, r_wr_o = 1'b1, r_rd_o = 1'b1, r_sn_o = 1'b0;
    always @(posedge cpu_clk) begin
        r_mot_o <= motor_on; r_wr_o <= wr_s[1]; r_rd_o <= c2n_rd_o; r_sn_o <= sense_pull;
    end
    assign c2n_motor_o   = r_mot_o;
    assign c2n_motor_en  = pin_ctrl[0];
    assign c2n_write_o   = r_wr_o;
    assign c2n_write_en  = pin_ctrl[1];
    assign c2n_read_o    = r_rd_o;
    assign c2n_read_en   = pin_ctrl[2] && c2n_rd_en;
    assign c2n_sense_o   = pin_ctrl[3] && r_sn_o;

    // ---- the POT lines (the joystick ports' comparators) -----------------------------------------------------------------------
    // Hypothesis (T47 4.6): PADDLE_CMP[i] = 1 once the capacitor of that pot has reached the threshold, PADDLE_DIS discharges
    // while the SID's POT counter is in its discharge phase. Nothing is driven until PIN_CTRL b5.
    (* ASYNC_REG = "TRUE" *) reg [3:0] pc_s1 = 4'd0, pc_s2 = 4'd0;
    always @(posedge sys_clk) begin pc_s1 <= paddle_cmp_i; pc_s2 <= pc_s1; end
    wire pot_dis;
    (* ASYNC_REG = "TRUE" *) reg [1:0] pd_s = 2'b00;
    always @(posedge cpu_clk) pd_s <= {pd_s[0], pot_dis};
    assign paddle_dis_o  = pd_s[1];
    assign paddle_dis_en = pin_ctrl[5];

    // ---- the UDP video and audio streams on the packet port (cpu_clk) ----------------------------------------------------------
    // The audio the streams carry is muted while the app has the C64 stopped or the machine is paused, as the host mutes it for
    // the I2S (host/top.v c64_mute), and when the app sets STREAM_MUTE.
    (* ASYNC_REG = "TRUE" *) reg [2:0] mute_s = 3'b000;
    reg mute_c = 1'b0;
    always @(posedge sys_clk) mute_c <= c64_halted | app_s2[1];
    always @(posedge mclk) mute_s <= {mute_s[1:0], mute_c | stream_mute};
    reg signed [23:0] snd_l, snd_r;
    always @(posedge mclk) begin
        snd_l <= mute_s[2] ? 24'sd0 : audio_l;
        snd_r <= mute_s[2] ? 24'sd0 : audio_r;
    end
    wire v60 = (vmode_sys == 2'd1) || (vmode_sys == 2'd2);
    pktport_core_c64u pkt (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst), .vclk(sys_clk),
        .pix(pix_color), .px(pix_x), .py(pix_y), .v60(v60),
        .snd_clk(mclk), .snd_en(audio_sample_en), .in_l(snd_l), .in_r(snd_r),
        .udp_we(udp_we), .udp_addr(udp_addr), .udp_wdata(udp_wdata), .stream_ena(stream_ena),
        .pp_req(pp_req), .pp_grant(pp_grant), .pp_data(pp_data), .pp_valid(pp_valid), .pp_eof(pp_eof), .pp_ready(pp_ready)
    );

    // ---- the SID sockets' audio from the codec's ADCs, added to the core's mix (T37 §4.3) -----------------------------------
    wire signed [23:0] c_audio_l, c_audio_r;
    sid_sock_audio_c64u sock_audio (
        .mclk(mclk), .bclk(adc_bclk), .lrck(adc_lrck), .sdo(adc_sdo), .gain(sock_gain), .en(sock_en),
        .in_l(c_audio_l), .in_r(c_audio_r), .out_l(k_audio_l), .out_r(k_audio_r)
    );

    // ---- the C64 -----------------------------------------------------------------------------------------------------------------
    c64_core_c64u c64 (
        .turbo_en(turbo_en), .turbo_prefer(turbo_prefer), .turbo_upd_tgl(turbo_upd),
        .turbo_stat(turbo_stat), .turbo_w1(turbo_w1), .turbo_w2(turbo_w2), .turbo_ran_clr(turbo_ran_clr), .turbo_on(),
        .dbg_req(port_req), .d7ff_ev(d7ff_ev), .d7ff_data(d7ff_data),
        .clk(sys_clk), .gclk(c64_gclk), .gate_ce(c64_ce), .rst(sys_rst),
        .halt_req(c64_halt_req | mc_halt_s[1]), .halted(c64_halted), .gate_shut(gate_shut), .vmode(vmode_sys),
        .snd_clk(mclk), .snd_rst(mclk_rst),
        .audio_sample_en(audio_sample_en), .audio_l(c_audio_l), .audio_r(c_audio_r),
        .clk2x(sid2x_clk), .sid_cfg_base(sid_base), .sid_cfg_mask(sid_mask), .sid_cfg_split(sid_split),
        .sid_cfg_8580(sid_waves), .sid_cfg_res(sid_res), .sid_cfg_digi(sid_digi), .sid_cfg_rs8(sid_rs8), .sid_resid8(sid_resid8),
        .sid_cv_clk(cpu_clk), .sid_cv_we(sidcv_we), .sid_cv_addr(sidcv_addr), .sid_cv_wdata(sidcv_wdata),
        .sid_lv_addr(sid_lv_addr), .sid_lv_data(k_sid_lv_data), .sid_mix_gains(sid_gains),
        .sid_mix_gains2(sid_gains2), .smp_en(smp_en),
        .smp_a_tgl(sm_tgl), .smp_a_we(sm_we), .smp_a_addr(sm_addr), .smp_a_wdata(sm_wdata),
        .smp_a_rs_tgl(smp_a_rs_tgl_w), .smp_a_rdata(sm_rdata),
        .smp_f_req_tgl(k_smp_f_tgl), .smp_f_addr(k_smp_f_addr), .smp_f_done_tgl(smp_f_done_tgl), .smp_f_data(smp_f_data),
        .dbg_addr(port_addr), .dbg_rom(port_rom), .dbg_we(port_we),
        .dbg_wdata(port_wdata), .dbg_rdata(ram_rdata),
        .reu_cmd_addr(k_reu_cmd_addr), .reu_cmd_we(k_reu_cmd_we), .reu_cmd_valid(k_reu_cmd_valid),
        .reu_cmd_ready(reu_cmd_ready), .reu_wdata(k_reu_wdata), .reu_wdata_we(k_reu_wdata_we),
        .reu_rdata(reu_rdata), .reu_rdata_valid(reu_rdata_valid),
        .cpu_reset_req(core_reset_req),
        .cpu_pause_req(machine_ctl[1] | (app_stop & !app_pause) | uci_freeze),
        .reu_en(reu_s2[0]), .reu_size(reu_s2[3:1]),
        .cart_type(cart_type_s2), .cart_kill_tgl(app_cart_kill_tgl),
        .cart_force_tgl(app_cart_force_tgl), .cart_active(cart_active),
        .cart_wclk(ui_clk), .cart_req_tgl(cart_req_tgl), .cart_req_we(cart_req_we),
        .cart_req_addr(cart_req_addr), .cart_req_data(cart_req_data), .cart_req_slot(cart_req_slot),
        .cart_req_cached(cart_req_cached), .cart_ack_tgl(cart_ack_tgl),
        .cart_fill_be(cart_fill_be), .cart_fill_addr(cart_fill_addr), .cart_fill_data(cart_fill_data),
        .cart_fl_enable(cart_fl_enable), .cart_fl_magic1(cart_fl_magic1), .cart_fl_magic2(cart_fl_magic2),
        .cart_fl_mask(cart_fl_mask), .cart_fl_id(cart_fl_id), .cart_desc(cart_desc), .cart_evt_tgl(cart_evt_tgl),
        .cart_evt_word(cart_evt_word), .cart_fl_ack_tgl(cart_fl_ack_tgl), .cart_inval_tgl(cart_inval_tgl),
        .cart_ee_tgl(cart_ee_tgl), .cart_ee_clk(cpu_clk), .cart_ee_addr(cart_ee_addr), .cart_ee_we(cart_ee_we),
        .cart_ee_wdata(cart_ee_wdata), .cart_ee_rdata(cart_ee_rdata),
        .dma_req(dma_cyc_req), .dma_addr(dma_cyc_addr), .dma_we(dma_cyc_we), .dma_wdata(dma_cyc_wdata),
        .dma_done(dma_cyc_done), .dma_rdata(dma_cyc_rdata), .ultimax(app_s2[5]),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .leds(c64_leds),
        .keys(c64_keys), .joy1(c64_joy1), .joy2(c64_joy2), .restore(c64_restore), .freeze_btn(c64_freeze),
        .iec_atn_o(c64_atn_o), .iec_clk_o(c64_clk_o), .iec_data_o(c64_data_o),
        .iec_atn_i(c64_atn_i), .iec_clk_i(c64_clk_i), .iec_data_i(c64_data_i),
        .cram_addr(cram_addr), .cram_we(cram_we_w), .cram_wdata(cram_wdata_w), .cram_rdata(cram_rdata),
        .bus_addr(bus_addr_w), .io_ev(io_ev), .io_ev_addr(io_ev_addr), .io_ev_data(io_ev_data),
        .io_ev_we(io_ev_we), .io_ev_io(io_ev_io), .io_oe(uci_oe), .io_dout(uci_dout),
        .io_irq(uci_c64_irq),
        .slot_int(bus_m_s2[3:0]), .slot_ext(bus_m_s2[7:4]), .slot_wr_en(slot_wr_en),
        .slot_status(slot_status), .slot_cnt0(slot_cnt0), .slot_cnt1(slot_cnt1), .slot_last(slot_last),
        .slot_a(slot_a), .slot_ahi_t(slot_ahi_t), .slot_rw(slot_rw), .slot_d_o(slot_d_o),
        .slot_d_t(slot_d_t), .slot_d_i(SLOT_DATA),
        .slot_adir(k_SLOT_ADDRDIR), .slot_aoen(SLOT_ADDROEn), .slot_ddir(SLOT_DATADIR), .slot_doen(SLOT_DATAOEn),
        .slot_roml(SLOT_ROMLn), .slot_romh(SLOT_ROMHn), .slot_io1(SLOT_IO1n), .slot_io2(SLOT_IO2n),
        .slot_phi2(SLOT_PHI2), .slot_dotclk(SLOT_DOTCLK), .slot_ba(SLOT_BA), .slot_rst_o(slot_rst_o),
        .slot_game(SLOT_GAMEn), .slot_exrom(SLOT_EXROMn), .slot_dma(SLOT_DMAn), .slot_irq(SLOT_IRQn),
        .slot_nmi(SLOT_NMIn), .slot_rst_i(SLOT_RST_I),
        .sock_base(sock_base), .sock_mask(sock_mask), .sock_en(sock_en), .sock_split(sock_split),
        .slot_sid1(sid_sock1_n), .slot_sid2(sid_sock2_n), .slot_sid_addr(sid_sock_addr),
        .tape_read(trd_s[1]), .tape_sense_n(tsn_s[1]), .tape_motor_n(tape_motor_n), .tape_write(tape_write),
        .pot_cmp(pc_s2), .pot_dis(pot_dis)
    );
endmodule

// A buffer that synthesis and opt_design must keep: one LUT1 (O = I0) per bit, DONT_TOUCH.
module c64u_keep #(parameter W = 1) (input wire [W-1:0] i, output wire [W-1:0] o);
    genvar k;
    generate for (k = 0; k < W; k = k + 1) begin : b
        (* DONT_TOUCH = "TRUE" *) LUT1 #(.INIT(2'b10)) buf_i (.I0(i[k]), .O(o[k]));
    end endgenerate
endmodule

// One DONT_TOUCH flop per bit that holds its power-up value (INIT) for good: a real driver cell and net for a constant output.
module c64u_tie_ff #(parameter W = 1, parameter [W-1:0] INIT = 0) (input wire clk, output wire [W-1:0] o);
    genvar k;
    generate for (k = 0; k < W; k = k + 1) begin : b
        (* DONT_TOUCH = "TRUE" *) FDRE #(.INIT(INIT[k])) tie_i (.C(clk), .CE(1'b1), .R(1'b0), .D(INIT[k]), .Q(o[k]));
    end endgenerate
endmodule
