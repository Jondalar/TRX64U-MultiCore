// The C64 part of the c64ref core (T103, cut by T265 D12): the machine and nothing around it - CPU, VIC-II, both CIAs, RAM,
// ROMs and colour RAM (c64_mem_c64ref), the keyboard/joystick scan, the POT lines and the SID stand-in of c64_top_c64ref
// (T107 D9: silence), their video and audio out. No cartridge model, no expansion port, no UCI, no REU, no turbo, no
// DMA window, no drive: what used to drive those inputs is a constant here. Copied from trx64/c64_core.v (T103 Q5) and cut down.
// Synthesized out of context and cached (tools/vivado_lib.tcl).
//
// Machine pause (T12 B): the C64 runs on gclk, clk through a BUFGCE in top.v whose CE is gate_ce. halt_req stops
// that clock right after a phi2 falling edge, so the VIC, CPU (mid-instruction as it is), CIAs with their TOD
// and keyboard scan all stop on the same cycle boundary; only c64_mem's second port stays on clk. Resume waits
// until the clock has been stopped for a whole number of VIC frames, so the VIC stays where vic_scaler's 1080p
// frame expects it.
//
// System Mode (T40, vmode from mode_ctl.v: 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50; it changes only while the machine
// is halted or the clock stopped): the mode in use (vm) follows it with the gate open: the VIC's timing restarts
// with the new chip (kawari reset_req for 8 clocks, its reset kept from the CPU and the other chips for 64),
// everything else keeps its state.
module c64_core_c64ref #(
    // flags as icepi's firmware/main.h numbers them: CIA and SID model, dual SID, pan, auto mono, VA delay and fire =
    // space as in icepi's reset value; joystick invert off (joy1 = port 1, joy2 = port 2); REU, overlay, DolphinDOS
    // ROM, IEC disconnect and cartridge (bit 11) off. Bit 0 = 1: 8521 CIAs, 0: 6526.
    parameter [15:0] C64_FLAGS = 16'b0000_0001_0011_1111,
    parameter [22:0] TURBO_HOLD = 23'h400000    // T29: 2^22 PHI2 cycles at normal speed after a reset
) (
    input  wire       clk,                  // dot4x: 31.5278 MHz PAL (T40: per System Mode)
    input  wire       gclk,                 // clk through the BUFGCE, CE = gate_ce
    output wire       gate_ce,
    input  wire       rst,                  // sync to clk
    input  wire       halt_req,             // pause the whole machine (clk)
    output wire       halted,               // it is paused
    input  wire [1:0] vmode,                // System Mode (T40), see above
    input  wire       snd_clk,              // the SID's sample side (audio MCLK, 12.29 MHz)
    input  wire       snd_rst,              // sync to snd_clk
    input  wire       audio_sample_en,      // snd_clk, 48 kHz (the I2S frame)
    output wire signed [23:0] audio_l, audio_r, // snd_clk
    // the SID ports below are inert (T107 D9: no SID engine in this core): clk2x = 2 x clk from the same MMCM, not gated; the app's UltiSID registers
    // (quasi-static), filter curve writes (sid_cv_clk = the app's clock), envelope levels (asynchronous read)
    input  wire       clk2x,
    input  wire [15:0] sid_cfg_base,
    input  wire [15:0] sid_cfg_mask,
    input  wire [2:0] sid_cfg_split,
    input  wire [1:0] sid_cfg_8580,
    input  wire [1:0] sid_cfg_res,
    input  wire [3:0] sid_cfg_digi,
    input  wire       sid_cv_clk,
    input  wire       sid_cv_we,
    input  wire [11:0] sid_cv_addr,
    input  wire [7:0] sid_cv_wdata,
    input  wire [4:0] sid_lv_addr,
    output wire [7:0] sid_lv_data,
    input  wire [31:0] sid_mix_gains,       // U64_AUDIO_MIXER bytes 0-3 (quasi-static)
    input  wire [31:0] sid_mix_gains2,      // bytes 8-11: Sampler L / R (quasi-static; there is no sampler in this core)
    output wire [3:0] pix_color,            // VIC native pixel stream (clk)
    output wire [9:0] pix_x,
    output wire [8:0] pix_y,
    output wire [4:0] leds,
    input  wire [63:0] keys,                // board keyboard (clk), col*8 + row
    input  wire [4:0] joy1, joy2,           // up, down, left, right, fire (clk)
    input  wire       restore,
    input  wire [15:0] dbg_addr,            // the host's second port into the C64 RAM (clk)
    input  wire       dbg_we,
    input  wire [7:0] dbg_wdata,
    output wire [7:0] dbg_rdata,
    input  wire       dbg_rom,              // the second RAM port reads and writes the ROM block (c64_mem_c64ref.v)
    input  wire       dbg_req,              // the outside wants c64_mem's second port in this clock
    input  wire       cpu_reset_req,        // hold the CPU in reset (clk)
    input  wire       cpu_pause_req,        // stop the CPU at the next read cycle (clk)
    // T62 G17: a CPU write to $D7FF (I/O mapped), one clock after the phi2 falling edge that ends it
    output reg        d7ff_ev = 1'b0,
    output reg  [7:0] d7ff_data = 8'd0,
    // serial bus (clk), '0' = line low: the C64's own pulls out. Nothing else is on this bus: the C64 reads its own pulls back,
    // one clock late, as the bus module did.
    output wire       iec_atn_o, iec_clk_o, iec_data_o,
    // colour RAM second port (clk), read data one clock later
    input  wire [9:0] cram_addr,
    output wire [3:0] cram_rdata,
    // the POT lines (c64_top_c64ref.v): comparator levels of the two ports, the SID's discharge phase
    input  wire [3:0] pot_cmp,
    output wire       pot_dis
);
    // what used to be driven by blocks that are not in this core: constants
    wire [7:0] turbo_en = 8'd0, turbo_prefer = 8'd0;
    wire       turbo_upd_tgl = 1'b0, turbo_ran_clr = 1'b0;
    wire       reu_en = 1'b0, reu_cmd_ready = 1'b0, reu_rdata_valid = 1'b0;
    wire [15:0] reu_rdata = 16'd0;
    wire [2:0] reu_size = 3'd0;
    wire       ultimax = 1'b0, dma_req = 1'b0, dma_we = 1'b0;
    wire [15:0] dma_addr = 16'd0;
    wire  [7:0] dma_wdata = 8'd0;
    wire       io_oe = 1'b0, io_irq = 1'b0;
    wire  [7:0] io_dout = 8'd0;
    wire       cram_we = 1'b0;
    wire  [3:0] cram_wdata = 4'd0;
    wire       tape_read = 1'b1, tape_sense_n = 1'b1;
    wire       tape_motor_n, tape_write;
    wire [31:0] turbo_stat, turbo_w1, turbo_w2;
    wire [23:0] reu_cmd_addr;
    wire        reu_cmd_we, reu_cmd_valid;
    wire [15:0] reu_wdata;
    wire  [1:0] reu_wdata_we;
    wire        turbo_on;
    wire [23:0] mem_cmd_addr;
    wire        mem_cmd_we, mem_cmd_valid, mem_cmd_ready;
    wire [15:0] mem_wdata, mem_rdata;
    wire  [1:0] mem_wdata_we;
    wire        mem_wdata_ready, mem_rdata_valid;
    wire        usb_dp_0, usb_dn_0, usb_dp_1, usb_dn_1;

    // no expansion port and no cartridge model: nothing is on the bus but the C64's own pulls
    wire        cpu_rst_in = cpu_reset_req;
    wire [15:0] flags = C64_FLAGS;

    wire [15:0] xc_addr;
    wire  [7:0] xc_din;
    wire        xc_we, xc_roml, xc_romh, xc_io1, xc_io2, xc_phi2_p, xc_phi2_h, xc_phi2_n, xc_reset;
    wire        xc_vic, xc_ba;
    wire [11:0] xc_vaddr;
    wire        x_io_sel, x_io_we, x_dbg_we;
    // T29: the turbo CPU's port into c64_mem's second port
    wire [15:0] tb_addr;
    wire [14:0] tb_raddr;
    wire        tb_we, tb_took;
    wire  [7:0] tb_wdata, tb_ram_lo, tb_ram_hi, tb_rom_lo, tb_rom_hi;
    wire        turbo_inh;

    // the serial bus with the C64 alone on it
    reg  [2:0]  iec_r = 3'b111;
    always @(posedge clk) iec_r <= {iec_atn_o, iec_clk_o, iec_data_o};

    // ---- the gate (clk) ---------------------------------------------------------
    // ce is the CE for the next gclk edge. Pulses into the C64 are handed over only with ce set.
    // dot4x clocks per VIC frame: 312 x 63 x 32 (PAL, NTSC-50), 263 x 65 x 32 (NTSC, PAL-60)
    reg  [1:0]  vm = 2'd0;                  // the System Mode the VIC runs in
    wire [19:0] FRAME = (vm == 2'd1 || vm == 2'd2) ? 20'd547040 : 20'd628992;
    reg        ce = 1'b1;
    reg        halted_r = 1'b0;
    reg [19:0] shut = 20'd0;                // gclk edges left out, modulo FRAME
    wire       dma_busy;
    wire       run = !halt_req || rst || cpu_rst_in;
    reg  [19:0] res_at = 20'd628991;        // shut count at which a stop may end (whole frames)
    always @(posedge clk) res_at <= FRAME - 1'b1;
    always @(posedge clk) begin
        if (!ce) shut <= shut >= FRAME - 1 ? 20'd0 : shut + 1'b1;
        if (ce) begin
            // stop with this edge, the phi2 falling one
            if ((!run || halted_r) && xc_phi2_n && !dma_busy && !turbo_on) begin
                ce       <= 1'b0;
                halted_r <= 1'b1;
            end
        end else if (run && shut == res_at) begin
            ce       <= 1'b1;               // whole frames left out: go on
            halted_r <= 1'b0;
            shut     <= 20'd0;              // the frame starts here
        end
        if (rst) begin
            ce <= 1'b1; halted_r <= 1'b0; shut <= 20'd0;
        end
    end

    // a CPU write to $D7FF: the edge that ends a CPU half (ce: gclk takes it)
    always @(posedge clk) begin
        d7ff_ev <= ce && xc_phi2_n && x_dbg_we;
        if (ce && xc_phi2_n && x_dbg_we) d7ff_data <= xc_din;
    end

    // T40: a new System Mode restarts the VIC's timing with the gate open
    reg  [5:0]  trst = 6'd0;                // clocks of the restart left
    always @(posedge clk) begin
        if (trst != 6'd0) begin
            if (ce) trst <= trst - 6'd1;
        end else if (ce && vm != vmode) begin
            vm   <= vmode;
            trst <= 6'd63;
        end
        if (rst) begin vm <= vmode; trst <= 6'd0; end
    end
    wire vic_trst      = trst > 6'd55;      // kawari reset_req: 8 gclk edges
    wire vic_trst_mask = trst != 6'd0;      // its reset (31 clocks after) kept from the CPU and the other chips

    assign gate_ce = ce;
    assign halted  = halted_r;

    // no sampler in this core: nothing added to the mix
    wire signed [22:0] smp_l = 23'sd0, smp_r = 23'sd0;

    c64_top_c64ref #(
        .C1541_EXTERNAL_ROM(0),
        .C1541(0),                          // the drives are outside (c64ref_drives.v, iec_bus_c64ref.v)
        .TURBO_HOLD(TURBO_HOLD),
        .SYS_CLK_FREQUENCY(31_527_778),
        .TMDS_CLK_FREQUENCY(27_586_806)
    ) c64 (
        .clk(gclk), .rst(rst),
        .vic_reset_req(1'b0), .cpu_reset_req(cpu_rst_in), .cpu_pause_req(cpu_pause_req || dma_busy),
        .gate_ce(ce), .turbo_inhibit(turbo_inh),
        .tcfg_en(turbo_en), .tcfg_prefer(turbo_prefer), .tcfg_upd_tgl(turbo_upd_tgl),
        .tb_addr(tb_addr), .tb_raddr(tb_raddr), .tb_we(tb_we), .tb_wdata(tb_wdata),
        .tb_ram_lo(tb_ram_lo), .tb_ram_hi(tb_ram_hi), .tb_rom_lo(tb_rom_lo), .tb_rom_hi(tb_rom_hi), .tb_took(tb_took),
        .turbo_stat(turbo_stat), .turbo_w1(turbo_w1), .turbo_w2(turbo_w2), .turbo_pack(),
        .turbo_ld(1'b0), .turbo_ld_pack(32'd0),
        .turbo_ran_clr(turbo_ran_clr), .turbo_on(turbo_on),
        .vmode(vm), .vic_trst(vic_trst), .vic_trst_mask(vic_trst_mask),

        .mem_cmd_addr(mem_cmd_addr), .mem_cmd_we(mem_cmd_we), .mem_cmd_valid(mem_cmd_valid),
        .mem_cmd_ready(mem_cmd_ready), .mem_wdata(mem_wdata), .mem_wdata_we(mem_wdata_we),
        .mem_wdata_ready(mem_wdata_ready), .mem_rdata(mem_rdata), .mem_rdata_valid(mem_rdata_valid),

        .reu_mem_cmd_addr(reu_cmd_addr), .reu_mem_cmd_we(reu_cmd_we),
        .reu_mem_cmd_valid(reu_cmd_valid), .reu_mem_cmd_ready(reu_cmd_ready),
        .reu_mem_wdata(reu_wdata), .reu_mem_wdata_we(reu_wdata_we), .reu_mem_wdata_ready(1'b1),
        .reu_mem_rdata(reu_rdata), .reu_mem_rdata_valid(reu_rdata_valid),

        .leds(leds),
        // kawari's own 576p DVI output is not used (vic_scaler takes the native pixels); this clock times the
        // SID's sample side.
        .tmds_clk(snd_clk), .tmds_rst(snd_rst),
        .tmds_0(), .tmds_1(), .tmds_2(),

        .usb_clk(gclk), .usb_rst(1'b1),
        .usb_dp_0(usb_dp_0), .usb_dn_0(usb_dn_0), .usb_pullup_dp_0(), .usb_pullup_dn_0(),
        .usb_dp_1(usb_dp_1), .usb_dn_1(usb_dn_1), .usb_pullup_dp_1(), .usb_pullup_dn_1(),

        .flags(flags), .reu_size(reu_size),

        .iec_data_out(iec_data_o), .iec_clk_out(iec_clk_o), .iec_atn_out(iec_atn_o),
        .iec_data_in(iec_r[0]), .iec_clk_in(iec_r[1]), .iec_atn_in(iec_r[2]),

        .drive_rom_en(), .drive_rom_addr(), .drive_rom_dout(32'hFFFFFFFF),
        .drive_shmem_en(), .drive_shmem_addr(), .drive_shmem_din(),
        .drive_shmem_dout(32'h0), .drive_shmem_we(),
        .block_lba(), .block_cnt(), .block_rd(), .block_wr(), .block_ack(1'b0),
        .img_id(16'h0), .img_readonly(1'b1), .img_present(1'b0), .img_mounted(1'b0),

        .overlay_pixel(24'h0), .overlay_pixel_x(), .overlay_pixel_y(), .overlay_pixel_valid(1'b0),
        .hid_key_report_out(), .hid_key_modifiers_out(), .hid_key_0_out(),

        .tape_read(tape_read), .tape_sense_n(tape_sense_n), .tape_cass_motor_n(tape_motor_n), .tape_write(tape_write),
        .pot_cmp(pot_cmp), .pot_dis(pot_dis),
        .ps2_fifo_rd_en(), .ps2_fifo_rd_data(8'h0), .ps2_fifo_rd_valid(1'b0),

        .cart_flags(16'h0300),          // icepi's own cartridge: unused (ext_cart_en)
        .flash_enable(1'b0), .flash_magic1(16'h0), .flash_magic2(16'h0), .flash_mask(16'h0),
        .flash_id_mfr(8'h0), .flash_id_dev(8'h0), .flash_id_ofs(8'h0),
        .flash_evt_0(), .flash_evt_pulse(), .flash_ack(1'b0), .eeprom_written(),

        .kbd_overlay_pulse(), .kbd_reset_pulse(), .kbd_tape_play_pulse(),

        .vic_pix_color(pix_color), .vic_pix_x(pix_x), .vic_pix_y(pix_y),

        .ext_keys(keys), .ext_joy1(joy1), .ext_joy2(joy2), .ext_restore(restore),
        .ext_audio_sample_en(audio_sample_en), .audio_word_0(audio_l), .audio_word_1(audio_r),
        .clk2x(clk2x), .sid_cfg_base(sid_cfg_base), .sid_cfg_mask(sid_cfg_mask),
        .sid_cfg_split(sid_cfg_split), .sid_cfg_8580(sid_cfg_8580), .sid_cfg_res(sid_cfg_res),
        .sid_cfg_digi(sid_cfg_digi), .sid_cfg_rs8(50'd0), .sid_cv_clk(sid_cv_clk), .sid_cv_we(sid_cv_we),
        .sid_cv_addr(sid_cv_addr), .sid_cv_wdata(sid_cv_wdata),
        .sid_lv_addr(sid_lv_addr), .sid_lv_data(sid_lv_data), .sid_mix_gains(sid_mix_gains),
        .sid_mix_gains2(sid_mix_gains2), .smp_l(smp_l), .smp_r(smp_r),
        .sock_base(16'h0101), .sock_mask(16'hFEFE), .sock_en(2'b00), .sock_hit(), .sock_dout(8'h00),
        .ext_io2_wr(), .ext_io_addr(), .ext_io_data(),
        .ext_dma_req(dma_req), .ext_dma_addr(dma_addr), .ext_dma_we(dma_we),
        .ext_dma_wdata(dma_wdata), .ext_dma_done(), .ext_dma_rdata(),
        .ext_ultimax(ultimax), .ext_dma_force(2'b00),
        .ext_io_sel(x_io_sel), .ext_io_we(x_io_we), .ext_dbg_we(x_dbg_we), .ext_io_oe(io_oe), .ext_io_dout(io_dout),
        .ext_io_irq(io_irq),
        .ext_cart_en(1'b1),
        .ext_cart_addr(xc_addr), .ext_cart_din(xc_din), .ext_cart_we(xc_we),
        .ext_cart_roml(xc_roml), .ext_cart_romh(xc_romh), .ext_cart_io1_cen(xc_io1),
        .ext_cart_io2_cen(xc_io2), .ext_cart_phi2_p(xc_phi2_p), .ext_cart_phi2_h(xc_phi2_h),
        .ext_cart_phi2_n(xc_phi2_n), .ext_cart_reset(xc_reset),
        .ext_cart_vic(xc_vic), .ext_cart_vaddr(xc_vaddr), .ext_cart_ba(xc_ba),
        // no cartridge, no expansion port: EXROM and GAME high, nothing asserted, no wait
        .ext_cart_exrom(1'b1), .ext_cart_game(1'b1),
        .ext_cart_dout(8'h00),
        .ext_cart_nmi(1'b0), .ext_cart_irq(1'b0),
        .ext_cart_io2_we_valid(1'b0),
        .ext_cart_wait(1'b0), .ext_dma_busy(dma_busy),
        // no state bus in this core: its shadows, staging and loads are tied off
        .sb_vic(), .sb_cia1(), .sb_cia2(), .sb_cia1_port(), .sb_cia2_port(), .sb_cpu_regs(), .sb_cpu_next(),
        .sb_cpu_pc(), .sb_cpu_sync(), .sb_cpu_intg(), .sb_cpu_rdy(), .sb_cpu_bus(),
        .sb_ld_cpu(1'b0), .sb_stg_cpu(209'd0), .sb_cur_cpu(),
        .sb_ld_cia1(1'b0), .sb_stg_cia1(363'd0), .sb_cur_cia1(),
        .sb_ld_cia2(1'b0), .sb_stg_cia2(363'd0), .sb_cur_cia2(),
        .sb_vic_snap_ok(), .sb_vic_access(), .sb_reu_busy(),
        .sb_cpu_port(), .sb_cart(),
        .pk_reu_addr(4'd0), .pk_reu(), .pk_reu_dout(),
        .cram_clk(clk), .cram_addr(cram_addr), .cram_we(cram_we), .cram_wdata(cram_wdata),
        .cram_rdata(cram_rdata),
        .cram_tap_we(), .cram_tap_addr(), .cram_tap_din(), .cram_tap_old()
    );

    c64_mem_c64ref mem (
        .clk(gclk), .dbg_clk(clk), .clk2x(clk2x), .dbg_req(dbg_req),
        .tb_addr(tb_addr), .tb_raddr(tb_raddr), .tb_we(tb_we), .tb_wdata(tb_wdata),
        .tb_ram_lo(tb_ram_lo), .tb_ram_hi(tb_ram_hi), .tb_rom_lo(tb_rom_lo), .tb_rom_hi(tb_rom_hi), .tb_took(tb_took),
        .cmd_addr(mem_cmd_addr), .cmd_we(mem_cmd_we), .cmd_valid(mem_cmd_valid),
        .cmd_ready(mem_cmd_ready), .wdata(mem_wdata), .wdata_we(mem_wdata_we),
        .wdata_ready(mem_wdata_ready), .rdata(mem_rdata), .rdata_valid(mem_rdata_valid),
        .dbg_addr(dbg_addr), .dbg_rom(dbg_rom), .dbg_we(dbg_we), .dbg_wdata(dbg_wdata),
        .dbg_rdata(dbg_rdata)
    );

    // T29 (spec §12.2 D7): a stop, a VIC restart: the engine falls back to normal speed first
    assign turbo_inh = halt_req || cpu_rst_in || halted_r || trst != 6'd0 || rst;

endmodule
