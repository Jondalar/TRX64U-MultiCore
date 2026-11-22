// The C64SCPU core's layer 2 (T182), forked from the C128 core's layer 2 (cores/c128/layer2_c128.v), which is the c64u core's (T103: Gideon's stock C64U
// feature set as one layer 2 for the host's contract, 247 ports) cut down: the C64 with its expansion port, cartridge model, REU, one SID block (two
// SIDs with -DSID2) and the sockets' audio, one drive (Gideon's mm_drive: 1541 or 1581), the GCR codec, the cassette (-DTAPE), POT and capability
// words in the cw_* window (c64u_cw.vh). The ROM image and the RAM are in the DDR2 behind a line cache and a slot cache (c64scpu_ddrx.v). NOT in it: the
// ring, the TRX64 engine, the debugger, the lens unit, the state bus, the 1571, drive B, ACIA/SwiftLink, the user port, the UCI, the UDP streams, the
// sampler. The build is flat (MONO): host and core in one design, so the ports of this module are not a partition boundary.
// Self-contained (Q5): every module that is also in the deleted trx64 core, cores/common or host/ is a copy here with the suffix _c64u.
`timescale 1ns / 1ps
`include "c64u_cw.vh"
module layer2_c64scpu #(
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
    // off) go through a DONT_TOUCH LUT1 (c64scpu_keep); outputs that are always constant (the ports of blocks this core does not
    // have) are one DONT_TOUCH flop per bit that holds its power-up value (c64scpu_tie_ff: a FF, no LUT).
    wire signed [23:0] k_audio_l, k_audio_r;
    wire [7:0] k_sid_lv_data;
    wire [26:0] k_smp_f_addr, k_c_addr;
    wire [23:0] k_reu_cmd_addr;
    wire [15:0] k_reu_wdata;
    wire [1:0] k_reu_wdata_we;
    wire k_smp_f_tgl, k_SLOT_ADDRDIR, k_drv_io_nc, k_gcr_io_nc, k_reu_cmd_valid, k_reu_cmd_we;
    c64scpu_keep #(.W(24)) keep_audio_l (.i(k_audio_l), .o(audio_l));
    c64scpu_keep #(.W(24)) keep_audio_r (.i(k_audio_r), .o(audio_r));
    c64scpu_keep #(.W(8))  keep_sid_lv  (.i(k_sid_lv_data), .o(sid_lv_data));
    c64scpu_keep #(.W(27)) keep_smp_a   (.i(k_smp_f_addr), .o(smp_f_addr));
    c64scpu_keep #(.W(1))  keep_smp_t   (.i(k_smp_f_tgl), .o(smp_f_tgl));
    c64scpu_keep #(.W(1))  keep_adir    (.i(k_SLOT_ADDRDIR), .o(SLOT_ADDRDIR));
    c64scpu_keep #(.W(1))  keep_drv_nc  (.i(k_drv_io_nc), .o(drv_io_nc));
    c64scpu_keep #(.W(1))  keep_gcr_nc  (.i(k_gcr_io_nc), .o(gcr_io_nc));
    c64scpu_keep #(.W(24)) keep_reu_a   (.i(k_reu_cmd_addr), .o(reu_cmd_addr));
    c64scpu_keep #(.W(1))  keep_reu_v   (.i(k_reu_cmd_valid), .o(reu_cmd_valid));
    c64scpu_keep #(.W(1))  keep_reu_w   (.i(k_reu_cmd_we), .o(reu_cmd_we));
    c64scpu_keep #(.W(16)) keep_reu_d   (.i(k_reu_wdata), .o(reu_wdata));
    c64scpu_keep #(.W(2))  keep_reu_dwe (.i(k_reu_wdata_we), .o(reu_wdata_we));
    c64scpu_keep #(.W(3))  keep_c_lo    (.i(k_c_addr[2:0]), .o(c_addr[2:0]));
    assign c_addr[26:3] = k_c_addr[26:3];

    // the ports of blocks this core does not have (the ring's APPLY, the debugger, its JTAG page, the drive debug map, the
    // reference core's second DDR2 port) and the POT adjust pin, which has no logic yet
`ifdef TAPE
    c64scpu_tie_ff #(.W(330)) tie_ff (.clk(cpu_clk),
        .o({drive_dbg,
            ap_addr, ap_req, ap_wdata,
            pp_data, pp_eof, pp_req, pp_valid,
            dbg_ev, dbg_own, dbg_stop,
            trx_j_rdata, trx_j_rvalid,
            paddle_adj_en, paddle_adj_o}));
`else
    c64scpu_tie_ff #(.W(337)) tie_ff (.clk(cpu_clk),
        .o({drive_dbg,
            ap_addr, ap_req, ap_wdata,
            c2n_motor_o, c2n_motor_en, c2n_read_o, c2n_read_en, c2n_write_o, c2n_write_en, c2n_sense_o,
            pp_data, pp_eof, pp_req, pp_valid,
            dbg_ev, dbg_own, dbg_stop,
            trx_j_rdata, trx_j_rvalid,
            paddle_adj_en, paddle_adj_o}));
`endif

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
`ifdef TAPE
    localparam [31:0] TAPE_CAPS = `CAPAB_C2N_STREAMER | `CAPAB_C2N_RECORDER;
`else
    localparam [31:0] TAPE_CAPS = 32'd0;                  // no cassette block (T249): the word does not name one
`endif
    wire [31:0] core_caps = host_caps | `CAPAB_HARDWARE_GCR | TAPE_CAPS |
                            `CAPAB_RAM_EXPANSION | `CAPAB_MM_DRIVE | `CAPAB_EXTENDED_REU | `CAPAB_COMMAND_INTF;

    // this core's own register page (c64scpu_regs.v) at cw_addr[16:12] = 4'hE; the c64u window answers everything else
    wire        io_ack, io_irq, rg_ack, rg_irq;
    wire  [7:0] io_rdata, rg_rdata;
    wire        pg_e = !cw_addr[16] && cw_addr[15:12] == 4'hE;
    reg         pg_e_q = 1'b0, pg_id_q = 1'b0;
    reg   [7:0] id_q = 8'd0;
    wire        pg_id = !cw_addr[16] && cw_addr[15:2] == 14'd0;     // bytes 000-003: the identity word is this core's ("SCPU"), not the c64u window's
    always @(posedge cpu_clk) if (cw_re | cw_we) begin
        pg_e_q <= pg_e; pg_id_q <= pg_id;
        id_q <= cw_addr[1:0] == 2'd0 ? "S" : cw_addr[1:0] == 2'd1 ? "C" : cw_addr[1:0] == 2'd2 ? "P" : "U";
    end
    assign cw_rdata = pg_id_q ? id_q : pg_e_q ? rg_rdata : io_rdata;
    assign cw_ack   = pg_e ? rg_ack : io_ack;
    assign cw_irq   = io_irq | rg_irq;
    wire        rg_joy, rom_flush_tgl;
    wire  [7:0] core_status;
    wire [31:0] rom_hits_w, rom_miss_w;
    wire        rom_busy_w;
    wire        chr_we_w, dbg_clr_w;                 // the character store's write port and the debug clear, from the register page
    wire [11:0] chr_a_w;
    wire  [7:0] chr_d_w, chr_q_w;
    wire [31:0] rom_waits_w;
    wire [15:0] rom_fmax_w;
    wire [79:0] ram_cnt_w;
    wire [19:0] fr_min_w, fr_max_w;
    wire        scpu_rv_w, scpu_rv_o_w, scpu_e_w, scpu_fast_w, scpu_bootmap_w, scpu_hw_w, scpu_dos_w;     // T182 step 3: T10's ROM_VALID, SROM_LOG2, SCPU_SW and the core's answers
    wire  [4:0] scpu_lg_w;
    wire  [1:0] scpu_sw_w;
    wire [31:0] scpu_dev_w, scpu_hit_w, scpu_miss_w;
    wire        scpu_s_req_tgl, scpu_s_done_tgl, scpu_sw_req_tgl, scpu_sw_done_tgl;
    wire [20:0] scpu_s_req_line;
    wire [24:0] scpu_sw_req_addr;
    wire  [7:0] scpu_sw_req_data;
    wire        cold_tgl_w, cold_busy_w;       // the cold start: COLD register (cpu_clk) -> the core (sys_clk) and its running state back
    c64scpu_regs regs (
        .clk(cpu_clk), .we(cw_we && pg_e), .re(cw_re && pg_e), .addr(cw_addr), .wdata(cw_wdata), .rdata(rg_rdata), .ack(rg_ack), .irq(rg_irq),
        .joy_swap(rg_joy), .rom_flush_tgl(rom_flush_tgl), .rom_valid_o(scpu_rv_w), .srom_log2_o(scpu_lg_w), .scpu_sw_o(scpu_sw_w),
        .dev_cyc(scpu_dev_w), .cache_hits(scpu_hit_w), .cache_misses(scpu_miss_w), .cold_tgl(cold_tgl_w), .cold_busy(cold_busy_w),
        .chr_we(chr_we_w), .chr_a(chr_a_w), .chr_d(chr_d_w), .chr_q(chr_q_w), .dbg_clr_tgl(dbg_clr_w),
        .rom_waits(rom_waits_w), .rom_fill_max(rom_fmax_w), .fr_min(fr_min_w), .fr_max(fr_max_w), .ram_cnt(ram_cnt_w),
        .status(core_status), .rom_busy(rom_busy_w), .rom_hits(rom_hits_w), .rom_misses(rom_miss_w));
    // STATUS: b3 held, b4 PAL (vmode_sys = 0)
    assign core_status = {3'd0, vmode_sys == 2'd0, c64_halted, scpu_e_w, scpu_fast_w, scpu_rv_o_w};
    wire [63:0] keys_m = c64_keys;

    c64u_io cwio (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(io_rdata),
        .ack(io_ack), .irq(io_irq),
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
    wire win_dbg_ack;
    dma_win_c64scpu win (
        .port_ack(win_dbg_ack),
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
    drive_leds_map drv_led_map (.drv_leds_n(drva_leds_n), .drive_leds_n(drive_leds_n));
    // the GCR codec the app's disk image code uses (0x10060500)
    c64u_gcr_l2 gcr (
        .clock(cpu_clk), .reset(cpu_rst),
        .gcr_io_read(gcr_io_rd), .gcr_io_write(gcr_io_wr), .gcr_io_addr(gcr_io_addr), .gcr_io_wdata(gcr_io_wdata),
        .gcr_io_rdata(gcr_io_rdata), .gcr_io_ack(gcr_io_ack), .gcr_io_nc(k_gcr_io_nc)
    );

    // ---- T113: no UCI (Alex: no UCI); the cassette port only with -DTAPE (an optional group, cost measured apart) -----------------
    wire [15:0] bus_addr_w, io_ev_addr;
    wire  [7:0] io_ev_data;
    wire        io_ev, io_ev_we, io_ev_io;
    wire        uci_freeze = 1'b0, uci_oe = 1'b0, uci_c64_irq = 1'b0;
    wire  [7:0] uci_dout = 8'd0;
    assign uci_rdata = 8'd0; assign uci_ack = 1'b1; assign uci_irq = 1'b0;
    wire tape_motor_n, tape_write;
`ifdef TAPE
    // ---- the cassette port ----------------------------------------------------------------------------------------------------
    // A pulse in cpu_clk per C64 cycle (the recorder's time base) from the C64's bus events (sys_clk, gated: none while paused).
    reg ev_tgl = 1'b0;
    always @(posedge sys_clk) if (io_ev) ev_tgl <= ~ev_tgl;
    (* ASYNC_REG = "TRUE" *) reg [2:0] ev_s = 3'b000;
    always @(posedge cpu_clk) ev_s <= {ev_s[1:0], ev_tgl};
    wire phi2_pulse = ev_s[2] != ev_s[1];
    // the machine's side of the port into cpu_clk
    (* ASYNC_REG = "TRUE" *) reg [1:0] mot_s = 2'b11, wr_s = 2'b11, rdp_s = 2'b11, snp_s = 2'b11;
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
    // pins: released until PIN_CTRL says otherwise (T103 §5: the direction of the C2N pins is not known from any source)
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

    wire trd_s_1 = trd_s[1], tsn_s_1 = tsn_s[1];
`else
    assign pb_rdata = 8'd0;  assign pb_ack = 1'b1;
    assign rc_rdata = 8'd0;  assign rc_ack = 1'b1;  assign rc_irq = 1'b0;
    wire trd_s_1 = 1'b1, tsn_s_1 = 1'b1;
`endif

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

    // ---- T113: no UDP streams, no sampler / Ultimate audio (Alex); the real SID sockets' ADC audio only with -DSOCK_AUDIO -----------
    wire signed [23:0] c_audio_l, c_audio_r;
`ifdef SOCK_AUDIO
    sid_sock_audio_c64u sock_audio (
        .mclk(mclk), .bclk(adc_bclk), .lrck(adc_lrck), .sdo(adc_sdo), .gain(sock_gain), .en(sock_en),
        .in_l(c_audio_l), .in_r(c_audio_r), .out_l(k_audio_l), .out_r(k_audio_r)
    );
`else
    assign k_audio_l = c_audio_l;
    assign k_audio_r = c_audio_r;
`endif

    // ---- the C64 -----------------------------------------------------------------------------------------------------------------
    wire        rom_req_tgl, rom_done_tgl;
    wire [12:0] rom_req_line;
    wire [127:0] rom_fill;
    wire [31:0] rom_n_hit, rom_n_miss, rom_n_wait;
    wire [15:0] rom_fill_max;
    wire [26:0] pw_addr; wire [2:0] pw_cmd; wire pw_en, pw_wren;
    wire [127:0] pw_wdf_data; wire [15:0] pw_wdf_mask;
    wire        ram_m_req_tgl, ram_m_done_tgl, ram_w_req_tgl, ram_w_done_tgl;
    wire [12:0] ram_m_req_line;
    wire [16:0] ram_w_req_addr;
    wire  [7:0] ram_w_req_data;
    wire [15:0] ram_n_vic_miss, ram_n_cpu_wait, ram_n_fetch, ram_n_wlost, ram_n_vic_blk1;
    assign rom_hits_w = rom_n_hit;
    assign rom_miss_w = rom_n_miss;
    assign rom_waits_w = rom_n_wait;
    assign rom_fmax_w  = rom_fill_max;
    assign ram_cnt_w = {ram_n_vic_blk1, ram_n_wlost, ram_n_fetch, ram_n_cpu_wait, ram_n_vic_miss};
    // T157: the joystick swap (c64scpu_joyswap.v): the register page's bit exchanges the two ports before the core
    wire [4:0] joy_a, joy_b;
    c64scpu_joyswap joysw (.clk(sys_clk), .swap(rg_joy), .joy1(c64_joy1), .joy2(c64_joy2), .joy1_o(joy_a), .joy2_o(joy_b));
    c64_core_c64scpu c64 (
        .turbo_en(turbo_en), .turbo_prefer(turbo_prefer), .turbo_upd_tgl(turbo_upd),
        .turbo_stat(turbo_stat), .turbo_w1(turbo_w1), .turbo_w2(turbo_w2), .turbo_ran_clr(turbo_ran_clr), .turbo_on(),
        .dbg_req(port_req), .d7ff_ev(d7ff_ev), .d7ff_data(d7ff_data),
        .clk(sys_clk), .gclk(c64_gclk), .gate_ce(c64_ce), .rst(sys_rst),
        .cold_req_tgl(cold_tgl_w), .cold_busy(cold_busy_w),
        .scpu_rom_valid(scpu_rv_w), .scpu_srom_log2(scpu_lg_w), .scpu_sw(scpu_sw_w), .scpu_rom_valid_o(scpu_rv_o_w),
        .scpu_s_req_tgl(scpu_s_req_tgl), .scpu_s_req_line(scpu_s_req_line), .scpu_s_done_tgl(scpu_s_done_tgl),
        .scpu_sw_req_tgl(scpu_sw_req_tgl), .scpu_sw_req_addr(scpu_sw_req_addr), .scpu_sw_req_data(scpu_sw_req_data), .scpu_sw_done_tgl(scpu_sw_done_tgl),
        .scpu_dev_cyc(scpu_dev_w), .scpu_n_hit(scpu_hit_w), .scpu_n_miss(scpu_miss_w), .scpu_bootmap(scpu_bootmap_w), .scpu_hwenable(scpu_hw_w), .scpu_dosext(scpu_dos_w), .scpu_e_flag(scpu_e_w), .scpu_fast(scpu_fast_w),
        // Every pause stops the whole machine, as in the other cores (vc20, c264, c128): the app's C64_STOP (app_stop; the host's app_pause gate is the C64
        // layer's own and this core's application does not set it), the JTAG pause (machine_ctl[1]) and the host's halts. Before, the app's stop reached
        // only cpu_pause_req (a CPU-only pause), STATUS b3 (the PAUSED bit the application's Freeze waits for) never came up, and the menu opened after
        // 200 ms with the machine running (board, 2026-10-24: "Freeze: no PAUSED after 200 ms (status 10)").
        .halt_req(c64_halt_req | machine_ctl[1] | app_stop | mc_halt_s[1]), .halted(c64_halted), .gate_shut(gate_shut), .vmode(vmode_sys),
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
        .keys(keys_m), .joy1(joy_a), .joy2(joy_b), .restore(c64_restore), .freeze_btn(c64_freeze),
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
        .tape_read(trd_s_1), .tape_sense_n(tsn_s_1), .tape_motor_n(tape_motor_n), .tape_write(tape_write),
        .pot_cmp(pc_s2), .pot_dis(pot_dis),
        .chr_wclk(cpu_clk), .chr_we(chr_we_w), .chr_a(chr_a_w), .chr_d(chr_d_w), .chr_q(chr_q_w),
        .dbg_clr_tgl(dbg_clr_w), .fr_min(fr_min_w), .fr_max(fr_max_w),
        .rom_req_tgl(rom_req_tgl), .rom_req_line(rom_req_line), .rom_done_tgl(rom_done_tgl), .rom_fill(rom_fill),
        .rom_n_hit(rom_n_hit), .rom_n_miss(rom_n_miss), .rom_n_wait(rom_n_wait), .rom_fill_max(rom_fill_max), .rom_flush_tgl(rom_flush_tgl), .rom_busy(rom_busy_w),
        .ram_m_req_tgl(ram_m_req_tgl), .ram_m_req_line(ram_m_req_line), .ram_m_done_tgl(ram_m_done_tgl),
        .ram_w_req_tgl(ram_w_req_tgl), .ram_w_req_addr(ram_w_req_addr), .ram_w_req_data(ram_w_req_data), .ram_w_done_tgl(ram_w_done_tgl),
        .dbg_ack(win_dbg_ack), .ram_n_vic_miss(ram_n_vic_miss), .ram_n_cpu_wait(ram_n_cpu_wait), .ram_n_fetch(ram_n_fetch), .ram_n_wlost(ram_n_wlost), .ram_n_vic_blk1(ram_n_vic_blk1)
    );

    // ---- the DDR2 side: the ROM line fetch, the RAM line fetch and the RAM byte write on the one command port (e_*) ---------------------------
    // c64scpu_ddrx.v serves all three, one command open at a time. The port has no other user in this core (no frame writer): pw_* are the constant
    // inputs of the select below, kept as they are in the flat build (tie_e).
    c64scpu_tie_ff #(.W(176)) tie_e (.clk(cpu_clk), .o({pw_addr, pw_cmd, pw_en, pw_wdf_data, pw_wdf_mask, pw_wren}));
    wire        rom_owns;
    wire        x_en;
    wire  [2:0] x_cmd;
    wire [26:0] x_addr;
    wire [127:0] x_wdata;
    wire [15:0] x_wmask;
    assign e_en   = rom_owns ? x_en : pw_en;
    assign e_addr = rom_owns ? x_addr : pw_addr;
    assign e_cmd  = rom_owns ? x_cmd : pw_cmd;
    assign e_wdf_wren = rom_owns ? (x_en && x_cmd == 3'b000) : pw_wren;
    assign e_wdf_data = rom_owns ? x_wdata : pw_wdf_data;
    assign e_wdf_mask = rom_owns ? x_wmask : pw_wdf_mask;
    c64scpu_ddrx #(.ROM_BASE_W(27'h0E00000), .RAM_BASE_W(27'h0E10000)) ddrx (
        .ui_clk(ui_clk), .ui_rst(ui_rst || !ddr_calib),
        .r_req_tgl(rom_req_tgl), .r_req_line(rom_req_line), .r_done_tgl(rom_done_tgl),
        .m_req_tgl(ram_m_req_tgl), .m_req_line(ram_m_req_line), .m_done_tgl(ram_m_done_tgl),
        .w_req_tgl(ram_w_req_tgl), .w_req_addr(ram_w_req_addr), .w_req_data(ram_w_req_data), .w_done_tgl(ram_w_done_tgl),
        .s_req_tgl(scpu_s_req_tgl), .s_req_line(scpu_s_req_line), .s_done_tgl(scpu_s_done_tgl),
        .sw_req_tgl(scpu_sw_req_tgl), .sw_req_addr(scpu_sw_req_addr), .sw_req_data(scpu_sw_req_data), .sw_done_tgl(scpu_sw_done_tgl),
        .fill(rom_fill), .owns(rom_owns), .en(x_en), .cmd(x_cmd), .addr(x_addr), .wdata(x_wdata), .wmask(x_wmask),
        .rdy(e_rdy), .rd_valid(e_rd_valid), .rd_data(c_rd_data));
    c64scpu_keep #(.W(64)) keep_romn (.i({rom_n_hit, rom_n_miss}), .o());
endmodule

// A buffer that synthesis and opt_design must keep: one LUT1 (O = I0) per bit, DONT_TOUCH.
module c64scpu_keep #(parameter W = 1) (input wire [W-1:0] i, output wire [W-1:0] o);
`ifdef MONO
    assign o = i;                       // flat build: no partition boundary to keep
`else
    genvar k;
    generate for (k = 0; k < W; k = k + 1) begin : b
        (* DONT_TOUCH = "TRUE" *) LUT1 #(.INIT(2'b10)) buf_i (.I0(i[k]), .O(o[k]));
    end endgenerate
`endif
endmodule

// One DONT_TOUCH flop per bit that holds its power-up value (INIT) for good: a real driver cell and net for a constant output.
module c64scpu_tie_ff #(parameter W = 1, parameter [W-1:0] INIT = 0) (input wire clk, output wire [W-1:0] o);
`ifdef MONO
    assign o = INIT;                    // flat build: a constant
`else
    genvar k;
    generate for (k = 0; k < W; k = k + 1) begin : b
        (* DONT_TOUCH = "TRUE" *) FDRE #(.INIT(INIT[k])) tie_i (.C(clk), .CE(1'b1), .R(1'b0), .D(INIT[k]), .Q(o[k]));
    end endgenerate
`endif
endmodule
