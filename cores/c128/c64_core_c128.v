// The C64 part of the c64u core (T103): icepi's c64_top with its unused interfaces tied off, block RAM for its
// memory port, the real expansion port, the cartridge model, REU, turbo and the sampler - and nothing of the
// TRX64 reference core's ring, state bus, capture, debugger or stop-position logic. Copied from
// trx64/c64_core.v (T103 Q5: the c64u core is self-contained; the original is not touched) and cut down.
// Synthesized out of context and cached (tools/vivado_lib.tcl).
//
// Machine pause (T12 B): the C64 runs on gclk, clk through a BUFGCE in top.v whose CE is gate_ce. halt_req stops
// that clock right after a phi2 falling edge, so the VIC, CPU (mid-instruction as it is), CIAs with their TOD, SID,
// REU, cartridge and keyboard scan all stop on the same cycle boundary; only c64_mem's second port (the app's RAM
// DMA, the text layer) stays on clk. A bus DMA byte while halted opens the clock until its cycle is over: the
// first one also holds the CPU (RDY, as under the REU) for the rest of the pause, so each byte costs the machine
// one cycle, the way a real DMA does. Resume waits until the clock has been stopped for a whole number of VIC
// frames, so the VIC keeps its phase (the 1080p frame of vic_scaler needed it; since T194 the C128's build has no vic_scaler, the whole-frame resume stays).
//
// System Mode (T40, vmode from mode_ctl.v: 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50; it changes only while the machine
// is halted or the clock stopped): the mode in use (vm) follows it with the gate open: the VIC's timing restarts
// with the new chip (kawari reset_req for 8 clocks, its reset kept from the CPU and the other chips for 64),
// everything else keeps its state.
module c64_core_c128 #(
    // flags as icepi's firmware/main.h numbers them: CIA and SID model, dual SID, pan, auto mono, VA delay and fire =
    // space as in icepi's reset value; joystick invert off (joy1 = port 1, joy2 = port 2); REU off (bit 12 comes from
    // reu_en); overlay, DolphinDOS ROM and IEC disconnect off; cartridge (bit 11) when the app has set a cartridge
    // type. Bit 0 = 1: 8521 CIAs, 0: 6526.
`ifdef SID2
    parameter [15:0] C64_FLAGS = 16'b0000_0001_0011_1111,   // C128: two SIDs (bit 2 = dual SID on, the reDIP block's second voice set)
`else
    parameter [15:0] C64_FLAGS = 16'b0000_0001_0011_1011,   // C128: one SID (bit 2 = dual SID off)
`endif
    parameter        SLOT_EN = 1,               // the real expansion port (slot_port_c64u.v); 0: idle
    parameter [22:0] TURBO_HOLD = 23'h400000    // T29: 2^22 PHI2 cycles at normal speed after a reset
) (
    input  wire       clk,                  // dot4x: 31.5278 MHz PAL (T40: per System Mode)
    input  wire       gclk,                 // clk through the BUFGCE, CE = gate_ce
    output wire       gate_ce,
    input  wire       rst,                  // sync to clk: the power-on reset (the host's); the cold start below is the same reset again, in phase
    input  wire       cold_req_tgl,         // cpu_clk: a toggle asks for a cold start (c128_regs.v COLD, T113 5.6)
    output wire       cold_busy,            // the cold start is running (clk)
    input  wire       halt_req,             // pause the whole machine (clk)
    output wire       halted,               // it is paused (also during a DMA's cycle)
    output wire       gate_shut,            // the gate is shut and no DMA cycle is open (GATE_CTRL b9)
    input  wire [1:0] vmode,                // System Mode (T40), see above
    input  wire       snd_clk,              // the SID's sample side (audio MCLK, 12.29 MHz)
    input  wire       snd_rst,              // sync to snd_clk
    input  wire       audio_sample_en,      // snd_clk, 48 kHz (the I2S frame)
    output wire signed [23:0] audio_l, audio_r, // snd_clk
    // SID engine (cores/c64u/trx_sid): clk2x = 2 x clk from the same MMCM, not gated; the app's UltiSID registers
    // (quasi-static), filter curve writes (sid_cv_clk = the app's clock), envelope levels (asynchronous read)
    input  wire       clk2x,
    input  wire [15:0] sid_cfg_base,
    input  wire [15:0] sid_cfg_mask,
    input  wire [2:0] sid_cfg_split,
    input  wire [1:0] sid_cfg_8580,
    input  wire [1:0] sid_cfg_res,
    input  wire [3:0] sid_cfg_digi,
    input  wire [49:0] sid_cfg_rs8,
    output wire       sid_resid8,       // CAPS3 b0 of c64u_io: the 8-SID reDIP with the T25.5 filter control
    input  wire       sid_cv_clk,
    input  wire       sid_cv_we,
    input  wire [11:0] sid_cv_addr,
    input  wire [7:0] sid_cv_wdata,
    input  wire [4:0] sid_lv_addr,
    output wire [7:0] sid_lv_data,
    input  wire [31:0] sid_mix_gains,       // U64_AUDIO_MIXER bytes 0-3 (T25.3; quasi-static)
    input  wire [31:0] sid_mix_gains2,      // bytes 8-11: Sampler L / R (T25.6; quasi-static)
    // T25.6 sampler (trx_sampler_c64u.v): enable (C64_SAMPLER_ENABLE, any clock), the app's byte window (toggle
    // handshake, c64u_io page 8), the DDR2 read it asks the REU's port for (toggle handshake)
    input  wire       smp_en,
    input  wire       smp_a_tgl,
    input  wire       smp_a_we,
    input  wire [7:0] smp_a_addr,
    input  wire [7:0] smp_a_wdata,
    output wire       smp_a_rs_tgl,
    output wire [7:0] smp_a_rdata,
    output wire       smp_f_req_tgl,
    output wire [26:0] smp_f_addr,
    input  wire       smp_f_done_tgl,
    input  wire [31:0] smp_f_data,
    output wire [3:0] pix_color,            // VIC native pixel stream (clk)
    output wire [9:0] pix_x,
    output wire [8:0] pix_y,
    output wire [4:0] leds,
    input  wire [63:0] keys,                // board keyboard (clk), col*8 + row
    input  wire [4:0] joy1, joy2,           // up, down, left, right, fire (clk)
    input  wire       restore,
    input  wire       freeze_btn,           // a freezer cartridge's button, held (clk)
    input  wire [15:0] dbg_addr,            // the host's second port into the C64 RAM (clk)
    input  wire       dbg_we,
    input  wire [7:0] dbg_wdata,
    output wire [7:0] dbg_rdata,
    input  wire       dbg_rom,              // the second RAM port reads and writes the ROM block (c64_mem_c128.v)
    // REU memory (icepi's p1 port: 16-bit words, valid/ready, rdata_valid later)
    output wire [23:0] reu_cmd_addr,
    output wire       reu_cmd_we,
    output wire       reu_cmd_valid,
    input  wire       reu_cmd_ready,
    output wire [15:0] reu_wdata,
    output wire [1:0] reu_wdata_we,
    input  wire [15:0] reu_rdata,
    input  wire       reu_rdata_valid,
    input  wire       reu_en,               // the app's REU_ENABLE (clk, quasi-static): REU present
    input  wire [2:0] reu_size,             // the app's REU_SIZE: 0 = 128 KB ... 7 = 16 MB (address mask)
    input  wire       cpu_reset_req,        // hold the CPU in reset (clk)
    input  wire       cpu_pause_req,        // stop the CPU at the next read cycle (clk)
    // T29 turbo (c64_turbo_c64u.v): the app's setting (page "011" 0x02, 0x2D and the strobe 0x2E as a toggle;
    // quasi-static), the engine's status words and the clear of its sticky "ran" bit (a clk pulse), whether the
    // engine drives the CPU now; dbg_req: the outside wants c64_mem's second port in this clock
    input  wire [7:0] turbo_en,
    input  wire [7:0] turbo_prefer,
    input  wire       turbo_upd_tgl,
    output wire [31:0] turbo_stat,
    output wire [31:0] turbo_w1,
    output wire [31:0] turbo_w2,
    input  wire       turbo_ran_clr,
    output wire       turbo_on,
    input  wire       dbg_req,
    // cartridge (cart_slot_c64u): the app's CART_TYPE (taken at every CPU reset and with a force), kill/force
    // toggles, ACTIVE back; its requests to DDR2 (line fetches, byte writes) for cart_fill_c64u, which writes the
    // cache's block RAM in cart_wclk; for c64u_io in the app's clock: the flash responder's configuration (static
    // while used), its pending operation, ACK and cache invalidation (toggles), EEPROM changes, the GMod2
    // EEPROM's second port
    input  wire [7:0] cart_type,
    input  wire       cart_kill_tgl,
    input  wire       cart_force_tgl,
    output wire       cart_active,
    input  wire       cart_wclk,
    output wire       cart_req_tgl,
    output wire       cart_req_we,
    output wire [23:0] cart_req_addr,
    output wire [7:0] cart_req_data,
    output wire [5:0] cart_req_slot,
    output wire       cart_req_cached,
    input  wire       cart_ack_tgl,
    input  wire [7:0] cart_fill_be,
    input  wire [8:0] cart_fill_addr,
    input  wire [63:0] cart_fill_data,
    input  wire       cart_fl_enable,
    input  wire [15:0] cart_fl_magic1,
    input  wire [15:0] cart_fl_magic2,
    input  wire [15:0] cart_fl_mask,
    input  wire [23:0] cart_fl_id,
    input  wire [343:0] cart_desc,      // the cartridge's descriptor (c64u_io page 4; static around reset)
    output wire       cart_evt_tgl,
    output wire [33:0] cart_evt_word,
    input  wire       cart_fl_ack_tgl,
    input  wire       cart_inval_tgl,
    output wire       cart_ee_tgl,
    input  wire       cart_ee_clk,
    input  wire [10:0] cart_ee_addr,
    input  wire       cart_ee_we,
    input  wire [7:0] cart_ee_wdata,
    output wire [7:0] cart_ee_rdata,
    // the Ultimate app's DMA as C64 bus cycles (clk), while the CPU is paused: req pulses with addr/we/wdata,
    // done pulses with rdata
    input  wire       dma_req,
    input  wire [15:0] dma_addr,
    input  wire       dma_we,
    input  wire [7:0] dma_wdata,
    output wire       dma_done,
    output wire [7:0] dma_rdata,
    input  wire       ultimax,              // C64_MODE bit 1 (clk)
    // serial bus (clk), '0' = line low: the C64's own pulls out, the bus in
    output wire       iec_atn_o, iec_clk_o, iec_data_o,
    input  wire       iec_atn_i, iec_clk_i, iec_data_i,
    // colour RAM second port (clk), low nibble, read data one clock later
    input  wire [9:0] cram_addr,
    input  wire       cram_we,
    input  wire [3:0] cram_wdata,
    output wire [3:0] cram_rdata,
    // the UCI's slot side (uci_unit_c64u.vhd, T28; clk): the bus address now; io_ev pulses one clock after each
    // phi2 falling edge with that edge's address, data, direction and whether it was a real access to $DE00-$DFFF
    // with I/O mapped; a read takes io_dout while io_oe (before the REU and the cartridge); io_irq is one more
    // C64 IRQ source
    output wire [15:0] bus_addr,
    output reg        io_ev = 1'b0,
    output reg [15:0] io_ev_addr = 16'd0,
    output reg  [7:0] io_ev_data = 8'd0,
    output reg        io_ev_we = 1'b0,
    output reg        io_ev_io = 1'b0,
    // T62 G17: a CPU write to $D7FF (I/O mapped), one clock after the phi2 falling edge that ends it
    output reg        d7ff_ev = 1'b0,
    output reg  [7:0] d7ff_data = 8'd0,
    input  wire       io_oe,
    input  wire [7:0] io_dout,
    input  wire       io_irq,
    // the real expansion port (T16, slot_port_c64u.v; clk): the app's C64_BUS_INTERNAL/EXTERNAL (quasi-static),
    // SLOT_CTRL's ROM write enable, SLOT_STATUS and the counters; the pins (top.v has the buffers)
    input  wire [3:0] slot_int,
    input  wire [3:0] slot_ext,
    input  wire       slot_wr_en,
    output wire [31:0] slot_status,
    output wire [31:0] slot_cnt0,
    output wire [31:0] slot_cnt1,
    output wire [31:0] slot_last,
    output wire [15:0] slot_a,
    output wire       slot_ahi_t,           // 1: A12-A15 and R/W released
    output wire       slot_rw,
    output wire [7:0] slot_d_o,
    output wire       slot_d_t,             // 1: D0-D7 not driven
    input  wire [7:0] slot_d_i,
    output wire       slot_adir, slot_aoen, slot_ddir, slot_doen,
    output wire       slot_roml, slot_romh, slot_io1, slot_io2,
    output wire       slot_phi2, slot_dotclk, slot_ba, slot_rst_o,
    input  wire       slot_game, slot_exrom, slot_dma, slot_irq, slot_nmi, slot_rst_i,
    // T37 SID sockets on the port's bus: the app's addressing (c64u_io SOCK_*, the app's clock, quasi-static), the selects
    input  wire [15:0] sock_base,
    input  wire [15:0] sock_mask,
    input  wire [1:0] sock_en,
    input  wire [2:0] sock_split,
    output wire       slot_sid1, slot_sid2, slot_sid_addr,
    // the cassette port of the machine (c64u_tape.v does the streaming and recording): CIA 1 FLAG level, the
    // SENSE level the CPU port reads (0 = key down), the CPU port's motor bit (0 = on) and write line
    input  wire       tape_read,
    input  wire       tape_sense_n,
    output wire       tape_motor_n,
    output wire       tape_write,
    // the POT lines (c64_top_c128.v): comparator levels of the two ports, the SID's discharge phase
    input  wire [3:0] pot_cmp,
    output wire       pot_dis,
    // T113: the C128's extras
    output wire [3:0] vdc_rgbi,             // variant 1 (whole VDC): its pixel stream for the frame writer (clk)
    output wire       vdc_ce, vdc_hb, vdc_vb,
    output wire       c128_fast,            // $D030 bit 0: 2 MHz requested
    output wire [2:0] c128_stat,            // {the Z80 has the bus, 2 MHz is on, C64 mode}
    input  wire [23:0] x_keys,              // the extra matrix columns K0-K2 x 8 rows (1 = pressed), quasi-static (cpu_clk)
    input  wire       x_capslk,             // the CAPS LOCK latch is down
    input  wire       x_d4080,              // the 40/80 DISPLAY switch is down (80 columns at reset)
    input  wire       x_boot64,             // start as a C64 (taken at reset)
    input  wire       vd_clk,               // variant 2: the application's window into the VDC (VRAM, registers, dirty bits)
    input  wire [15:0] vd_addr,
    input  wire       vd_we,
    input  wire [7:0] vd_wdata,
    output wire [7:0] vd_rdata,
    // T113 5.7: the character ROM store (c128_chr.v), written and read by the application (chr_wclk = cpu_clk)
    input  wire       chr_wclk,
    input  wire       chr_we,
    input  wire [12:0] chr_a,
    input  wire [7:0] chr_d,
    output wire [7:0] chr_q,
    // T113 5.7: the VIC's frame length as the core measures it (clk between two frame starts, shortest and longest since the last clear)
    input  wire       dbg_clr_tgl,          // cpu_clk: a toggle clears fr_min / fr_max / the ROM counters' maxima
    output wire [19:0] fr_min,
    output wire [19:0] fr_max,
    output wire [39:0] act_q                // T194: which chip is being used (c128_act.v, sys_clk)
`ifdef ROM_CACHE
    ,
    // T113 §5.4: the ROM image in the DDR2 behind a line cache (c128_romc.v), the DDR2 side is in layer2_c128.v (c128_rome.v)
    output wire        rom_req_tgl,
    output wire [12:0] rom_req_line,
    input  wire        rom_done_tgl,
    input  wire [127:0] rom_fill,
    output wire [31:0] rom_n_hit, rom_n_miss, rom_n_wait,
    output wire [15:0] rom_fill_max,
    input  wire        rom_flush_tgl,        // cpu_clk: a toggle asks for a flush of the line cache (c128_regs.v ROM_CTRL)
    output wire        rom_busy
`endif
`ifdef RAM_DDR2
    ,
    // T140 D25: the RAM in the DDR2 (c128_ramd.v); the DDR2 side is c128_ddrx.v in layer2_c128.v
    output wire        ram_m_req_tgl,
    output wire [12:0] ram_m_req_line,
    input  wire        ram_m_done_tgl,
    output wire        ram_w_req_tgl,
    output wire [16:0] ram_w_req_addr,
    output wire  [7:0] ram_w_req_data,
    input  wire        ram_w_done_tgl,
    output wire        dbg_ack,              // the second port's job is done (the application's window waits for it)
    output wire [15:0] ram_n_vic_miss, ram_n_cpu_wait, ram_n_fetch, ram_n_wlost, ram_n_vic_blk1
`endif
);
    wire        mem_rom_miss, mem_fill_busy, mem_ram_hold, mem_cmd_vic;
    wire  [2:0] mem_vic_tag;
    wire [23:0] mem_cmd_addr;
    wire        mem_cmd_we, mem_cmd_valid, mem_cmd_ready;
    wire [15:0] mem_wdata, mem_rdata;
    wire  [1:0] mem_wdata_we;
    wire        mem_wdata_ready, mem_rdata_valid;
    wire        usb_dp_0, usb_dn_0, usb_dp_1, usb_dn_1;

    // the real port's side (slot_port_c64u below)
    wire        s_rom, s_io1_rd, s_io2_rd, s_io2_ext, s_irq_ext, s_present, s_game, s_exrom;
    wire        s_irq, s_nmi, s_rst_req, xc_vic, xc_ba;
    wire [11:0] xc_vaddr;
    wire  [7:0] s_dout;
    wire        port_rd;                    // this read is the port's
    wire  [1:0] sock_hit;                   // this access hits SID socket 1/2 (c64_top_c128)
    // the sockets' addressing into this clock: two flip-flops, as trx_sid_c64u takes the UltiSID's
    (* ASYNC_REG = "TRUE" *) reg [36:0] sk_c1 = {3'd0, 2'b00, 16'hFEFE, 16'h0101}, sk_c = {3'd0, 2'b00, 16'hFEFE, 16'h0101};
    always @(posedge clk) begin
        sk_c1 <= {sock_split, sock_en, sock_mask, sock_base};
        sk_c  <= sk_c1;
    end
    wire        cpu_rst_in = cpu_reset_req || s_rst_req;

    // bit 11, the cartridge: a CRT, or a resource of the real port (taken at reset)
    wire [15:0] flags = C64_FLAGS | {3'd0, reu_en, cart_type[4:0] != 5'd0 || s_present, 11'd0};

    wire [15:0] xc_addr;
    wire  [7:0] xc_din, xc_dout;
    wire        xc_we, xc_roml, xc_romh, xc_io1, xc_io2, xc_phi2_p, xc_phi2_h, xc_phi2_n, xc_reset;
    wire        xc_exrom, xc_game, xc_nmi, xc_irq, xc_io2_we_valid, xc_wait;
    wire        x_io_sel, x_io_we, x_dbg_we;
    // T29: the turbo CPU's port into c64_mem's second port
    wire [15:0] tb_addr;
    wire [14:0] tb_raddr;
    wire        tb_we, tb_took;
    wire  [7:0] tb_wdata, tb_ram_lo, tb_ram_hi, tb_rom_lo, tb_rom_hi;
    wire        turbo_inh;
    wire [2:0]  l_cart_unused;
    wire [15:0] l_bank;
    wire        c_pk_hit, c_pk_mem;
    wire  [7:0] c_pk_data;
    wire [23:0] c_pk_maddr;

    // ---- the gate (clk) ---------------------------------------------------------
    // ce is the CE for the next gclk edge; ce_d the one for the edge just past, so a gclk register read here is
    // fresh when ce_d is set. Pulses into the C64 are handed over only with ce set; pulses out of it count only
    // with ce_d set (a stopped register keeps showing its last value).
    // dot4x clocks per VIC frame: 312 x 63 x 32 (PAL, NTSC-50), 263 x 65 x 32 (NTSC, PAL-60)
    reg  [1:0]  vm = 2'd0;                  // the System Mode the VIC runs in
    wire [19:0] FRAME = (vm == 2'd1 || vm == 2'd2) ? 20'd547040 : 20'd628992;
    reg        ce = 1'b1, ce_d = 1'b1;
    reg        halted_r = 1'b0;
    reg        held = 1'b0;                 // a DMA came with a pause on: CPU held until resume
    reg [19:0] shut = 20'd0;                // gclk edges left out, modulo FRAME
    reg        dma_hold = 1'b0;             // DMA request not yet taken by the C64
    reg        rv_hold = 1'b0;              // REU read data not yet taken
    wire       dma_busy;
    wire       g_dma_done, g_reu_valid;
    // T113 5.6 / 5.7: ph_cnt is real time modulo one VIC frame since the power-on release; the VIC starts its frame at the release. The cold start
    // releases its reset only at ph_cnt == FRAME - 1, so the VIC starts its frame at the same phase as after the power-on (T194: no scaler lock in this build any more; the hold stays).
    // Since 5.7 nothing else moves the VIC against real time: a ROM miss holds only the CPU (c64_top_c128.v rom_hold), the pause is a whole number of
    // frames (`shut`), so the repayment of 5.6 (al_hold) is gone.
    reg        cold_rst = 1'b0;
    wire       rsti = rst || cold_rst;
    wire       run = !halt_req || rsti || cpu_rst_in;
    reg  [19:0] res_at = 20'd628991;        // shut count at which a stop may end (whole frames)
    always @(posedge clk) res_at <= FRAME - 1'b1;
    reg  [19:0] ph_cnt = 20'd0;
    always @(posedge clk)
        if (rst) ph_cnt <= 20'd0;
        else ph_cnt <= (ph_cnt >= FRAME - 1'b1) ? 20'd0 : ph_cnt + 1'b1;
    (* ASYNC_REG = "TRUE" *) reg [2:0] cold_s = 3'b000;
    always @(posedge clk) cold_s <= {cold_s[1:0], cold_req_tgl};
    wire       cold_ev = cold_s[2] != cold_s[1];
    reg  [8:0] cold_cnt = 9'd0;
    always @(posedge clk) begin
        if (rst) cold_rst <= 1'b0;
        else if (cold_ev) begin cold_rst <= 1'b1; cold_cnt <= 9'd256; end
        else if (cold_rst) begin
            if (cold_cnt != 9'd0) cold_cnt <= cold_cnt - 1'b1;
            else if (ph_cnt == FRAME - 1'b1) cold_rst <= 1'b0;
        end
    end
    assign cold_busy = cold_rst;
    // The VIC's frame as measured here (5.7: it must be 628,992 clocks, every frame): clk between two falls of the raster line, shortest and longest.
    // The first frames after a reset are not counted (the raster starts at 0 and a cold start restarts it).
    (* ASYNC_REG = "TRUE" *) reg [2:0] dclr_s = 3'b000;
    always @(posedge clk) dclr_s <= {dclr_s[1:0], dbg_clr_tgl};
    wire        dclr = dclr_s[2] != dclr_s[1];
    reg  [19:0] fr_cnt = 20'd0, fr_min_r = 20'hFFFFF, fr_max_r = 20'd0;
    reg  [8:0]  fr_py = 9'd0;
    reg  [1:0]  fr_skip = 2'd3;
    wire        fr_wrap = pix_y + 9'd100 < fr_py;
    always @(posedge clk) begin
        fr_py  <= pix_y;
        fr_cnt <= fr_cnt + 20'd1;
        if (rsti || dclr) begin
            fr_skip <= rsti ? 2'd3 : 2'd1;
            fr_min_r <= 20'hFFFFF; fr_max_r <= 20'd0; fr_cnt <= 20'd0;
        end else if (fr_wrap) begin
            fr_cnt <= 20'd1;
            if (fr_skip != 2'd0) fr_skip <= fr_skip - 2'd1;
            else begin
                if (fr_cnt < fr_min_r) fr_min_r <= fr_cnt;
                if (fr_cnt > fr_max_r) fr_max_r <= fr_cnt;
            end
        end
    end
    assign fr_min = fr_min_r;
    assign fr_max = fr_max_r;
    // The VIC's registers are the one thing a reset (rst) does not clear (registers_scaled.v: the chip has no reset for them): after the cold start's
    // release 0 is written to $D000-$D02E through the posted-write port (fw_*), one per clock, as a power-on leaves them. The chip ignores writes while
    // its own reset is up (registers_no_eeprom.vi: 31 clocks after the release of rst_dot4x), so the first write waits 64 clocks.
    reg        vi_run = 1'b0;
    reg  [5:0] vi_a = 6'd0;
    reg  [6:0] vi_dly = 7'd0;
    always @(posedge clk) begin
        if (rst) vi_run <= 1'b0;
        else if (cold_rst && cold_cnt == 9'd0
                 && ph_cnt == FRAME - 1'b1
                ) begin vi_run <= 1'b1; vi_a <= 6'd0; vi_dly <= 7'd64; end
        else if (vi_run && ce) begin
            if (vi_dly != 7'd0) vi_dly <= vi_dly - 1'b1;
            else if (vi_a == 6'h2E) vi_run <= 1'b0;
            else vi_a <= vi_a + 1'b1;
        end
    end
    wire       dma_in = dma_req || dma_hold;
    always @(posedge clk) begin
        ce_d     <= ce;
        dma_hold <= dma_in && !ce;
        rv_hold  <= (rv_hold || reu_rdata_valid) && !ce;
        if (!ce) shut <= shut >= FRAME - 1 ? 20'd0 : shut + 1'b1;
        if (dma_in && (!run || halted_r)) held <= 1'b1;
        if (ce) begin
            // stop with this edge, the phi2 falling one, unless a DMA needs more
            if ((!run || halted_r) && xc_phi2_n && !dma_busy && !dma_in && !turbo_on) begin
                ce       <= 1'b0;
                halted_r <= 1'b1;
            end
        end else if (dma_in) begin
            ce <= 1'b1;                     // the DMA's cycle (and the CPU's way to a read)
        end else if (run && shut == res_at) begin
            ce       <= 1'b1;               // whole frames left out: go on
            halted_r <= 1'b0;
            held     <= 1'b0;
            shut     <= 20'd0;              // the frame starts here
        end
        if (rsti) begin
            ce <= 1'b1; halted_r <= 1'b0; held <= 1'b0; shut <= 20'd0;
        end
    end

    // the UCI's bus events: the edge that ends a CPU half (ce: gclk takes it)
    assign bus_addr = xc_addr;
    always @(posedge clk) begin
        io_ev <= ce && xc_phi2_n;
        d7ff_ev <= ce && xc_phi2_n && x_dbg_we;
        if (ce && xc_phi2_n && x_dbg_we) d7ff_data <= xc_din;
        if (ce && xc_phi2_n) begin
            io_ev_addr <= xc_addr;
            io_ev_data <= xc_din;
            io_ev_we   <= x_io_we;
            io_ev_io   <= x_io_sel;
        end
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
        if (rsti) begin vm <= vmode; trst <= 6'd0; end
    end
    wire vic_trst      = trst > 6'd55;      // kawari reset_req: 8 gclk edges
    wire vic_trst_mask = trst != 6'd0;      // its reset (31 clocks after) kept from the CPU and the other chips

    assign gate_ce = ce;
    assign halted  = halted_r;
    assign gate_shut = halted_r && !ce && !ce_d;
    assign dma_done = g_dma_done && ce_d;
    assign reu_cmd_valid = g_reu_valid && ce;

    // T113: no sampler (Q2): its byte window answers nothing. The C128's extra keys and switches come in with the application's registers
    // (c128_regs.v, cpu_clk): quasi-static, two flip-flops into this clock.
    wire        smp_oe = 1'b0, smp_irq = 1'b0;
    wire  [7:0] smp_dout = 8'd0;
    wire signed [22:0] smp_l = 23'sd0, smp_r = 23'sd0;
    (* ASYNC_REG = "TRUE" *) reg [26:0] xk1 = 27'd0, xk = 27'd0;
    always @(posedge clk) begin
        xk1 <= {x_boot64, x_d4080, x_capslk, x_keys};
        xk  <= xk1;
    end
    assign smp_a_rs_tgl = 1'b0;
    assign smp_a_rdata  = 8'd0;
    assign smp_f_req_tgl = 1'b0;
    assign smp_f_addr    = 27'd0;

`ifdef ROM_CACHE
    (* ASYNC_REG = "TRUE" *) reg [2:0] rf_s = 3'b000;
    always @(posedge clk) rf_s <= {rf_s[1:0], rom_flush_tgl};
    wire rom_flush_p = rf_s[2] != rf_s[1];
`endif
    c64_top_c128 #(
        .C1541_EXTERNAL_ROM(0),
        .C1541(0),                          // the drives are outside (c64u_drives.v, iec_bus_c64u.v)
        .TURBO_HOLD(TURBO_HOLD),
        .SYS_CLK_FREQUENCY(31_527_778),
        .TMDS_CLK_FREQUENCY(27_586_806)
    ) c64 (
        .clk(gclk), .rst(rsti),
        .vic_reset_req(1'b0), .cpu_reset_req(cpu_rst_in), .cpu_pause_req(cpu_pause_req || held || dma_busy),
        .gate_ce(ce), .turbo_inhibit(turbo_inh),
        .tcfg_en(turbo_en), .tcfg_prefer(turbo_prefer), .tcfg_upd_tgl(turbo_upd_tgl),
        .tb_addr(tb_addr), .tb_raddr(tb_raddr), .tb_we(tb_we), .tb_wdata(tb_wdata),
        .tb_ram_lo(tb_ram_lo), .tb_ram_hi(tb_ram_hi), .tb_rom_lo(tb_rom_lo), .tb_rom_hi(tb_rom_hi), .tb_took(tb_took),
        .turbo_stat(turbo_stat), .turbo_w1(turbo_w1), .turbo_w2(turbo_w2), .turbo_pack(),
        .turbo_ld(1'b0), .turbo_ld_pack(32'd0),
        .turbo_ran_clr(turbo_ran_clr), .turbo_on(turbo_on),
        .vmode(vm), .vic_trst(vic_trst), .vic_trst_mask(vic_trst_mask), .init_we(vi_run && ce && vi_dly == 7'd0), .init_a(vi_a),
        .rom_miss(mem_rom_miss), .rom_fill_busy(mem_fill_busy), .ram_hold(mem_ram_hold), .mem_vic_tag(mem_vic_tag), .mem_cmd_vic(mem_cmd_vic), .chr_wclk(chr_wclk), .chr_we(chr_we), .chr_a(chr_a), .chr_d(chr_d), .chr_q(chr_q),

        .mem_cmd_addr(mem_cmd_addr), .mem_cmd_we(mem_cmd_we), .mem_cmd_valid(mem_cmd_valid),
        .mem_cmd_ready(mem_cmd_ready), .mem_wdata(mem_wdata), .mem_wdata_we(mem_wdata_we),
        .mem_wdata_ready(mem_wdata_ready), .mem_rdata(mem_rdata), .mem_rdata_valid(mem_rdata_valid),

        .reu_mem_cmd_addr(reu_cmd_addr), .reu_mem_cmd_we(reu_cmd_we),
        .reu_mem_cmd_valid(g_reu_valid), .reu_mem_cmd_ready(reu_cmd_ready),
        .reu_mem_wdata(reu_wdata), .reu_mem_wdata_we(reu_wdata_we), .reu_mem_wdata_ready(1'b1),
        .reu_mem_rdata(reu_rdata), .reu_mem_rdata_valid((reu_rdata_valid || rv_hold) && ce),

        .leds(leds),
        // kawari's own 576p DVI output is not used (the native pixels go to pix_layer's capture and c128_avout); this clock times the
        // SID's sample side.
        .tmds_clk(snd_clk), .tmds_rst(snd_rst),
        .tmds_0(), .tmds_1(), .tmds_2(),

        .usb_clk(gclk), .usb_rst(1'b1),
        .usb_dp_0(usb_dp_0), .usb_dn_0(usb_dn_0), .usb_pullup_dp_0(), .usb_pullup_dn_0(),
        .usb_dp_1(usb_dp_1), .usb_dn_1(usb_dn_1), .usb_pullup_dp_1(), .usb_pullup_dn_1(),

        .flags(flags), .reu_size(reu_size),

        .iec_data_out(iec_data_o), .iec_clk_out(iec_clk_o), .iec_atn_out(iec_atn_o),
        .iec_data_in(iec_data_i), .iec_clk_in(iec_clk_i), .iec_atn_in(iec_atn_i),

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
        .sid_cfg_digi(sid_cfg_digi), .sid_cfg_rs8(sid_cfg_rs8), .sid_cv_clk(sid_cv_clk), .sid_cv_we(sid_cv_we),
        .sid_cv_addr(sid_cv_addr), .sid_cv_wdata(sid_cv_wdata),
        .sid_lv_addr(sid_lv_addr), .sid_lv_data(sid_lv_data), .sid_mix_gains(sid_mix_gains),
        .sid_mix_gains2(sid_mix_gains2), .smp_l(smp_l), .smp_r(smp_r),
        .sock_base(sk_c[15:0]), .sock_mask(sk_c[31:16]), .sock_en(sk_c[33:32]), .sock_hit(sock_hit), .sock_dout(s_dout),
        .ext_io2_wr(), .ext_io_addr(), .ext_io_data(),
        .ext_dma_req(dma_in && ce), .ext_dma_addr(dma_addr), .ext_dma_we(dma_we),
        .ext_dma_wdata(dma_wdata), .ext_dma_done(g_dma_done), .ext_dma_rdata(dma_rdata),
        .ext_ultimax(ultimax), .ext_dma_force(2'b00),
        .ext_io_sel(x_io_sel), .ext_io_we(x_io_we), .ext_dbg_we(x_dbg_we), .ext_io_oe(io_oe | smp_oe), .ext_io_dout(smp_oe ? smp_dout : io_dout),
        .ext_io_irq(io_irq | smp_irq),
        .ext_cart_en(1'b1),
        .ext_cart_addr(xc_addr), .ext_cart_din(xc_din), .ext_cart_we(xc_we),
        .ext_cart_roml(xc_roml), .ext_cart_romh(xc_romh), .ext_cart_io1_cen(xc_io1),
        .ext_cart_io2_cen(xc_io2), .ext_cart_phi2_p(xc_phi2_p), .ext_cart_phi2_h(xc_phi2_h),
        .ext_cart_phi2_n(xc_phi2_n), .ext_cart_reset(xc_reset),
        .ext_cart_vic(xc_vic), .ext_cart_vaddr(xc_vaddr), .ext_cart_ba(xc_ba),
        .ext_cart_exrom(s_rom ? s_exrom : xc_exrom), .ext_cart_game(s_rom ? s_game : xc_game),
        .ext_cart_dout(port_rd ? s_dout : xc_dout),
        .ext_cart_nmi(xc_nmi || (s_irq_ext && s_nmi)), .ext_cart_irq(xc_irq || (s_irq_ext && s_irq)),
        .ext_cart_io2_we_valid(xc_io2_we_valid || s_io2_ext),
        .ext_cart_wait(xc_wait), .ext_dma_busy(dma_busy),
        // no state bus in this core: its shadows, staging and loads are tied off
        .act_q(act_q), .sb_vic(), .sb_cia1(), .sb_cia2(), .sb_cia1_port(), .sb_cia2_port(), .sb_cpu_regs(), .sb_cpu_next(),
        .sb_cpu_pc(), .sb_cpu_sync(), .sb_cpu_intg(), .sb_cpu_rdy(), .sb_cpu_bus(),
        .sb_ld_cpu(1'b0), .sb_stg_cpu(209'd0), .sb_cur_cpu(),
        .sb_ld_cia1(1'b0), .sb_stg_cia1(363'd0), .sb_cur_cia1(),
        .sb_ld_cia2(1'b0), .sb_stg_cia2(363'd0), .sb_cur_cia2(),
        .sb_vic_snap_ok(), .sb_vic_access(), .sb_reu_busy(),
        .sb_cpu_port(), .sb_cart(),
        .pk_reu_addr(4'd0), .pk_reu(), .pk_reu_dout(),
        .cram_clk(clk), .cram_addr(cram_addr), .cram_we(cram_we), .cram_wdata(cram_wdata),
        .cram_rdata(cram_rdata),
        .cram_tap_we(), .cram_tap_addr(), .cram_tap_din(), .cram_tap_old(),
        .xkeys(xk[23:0]), .x_capslk(xk[24]), .x_d4080(xk[25]), .x_boot64(xk[26]),
        .vdc_rgbi(vdc_rgbi), .vdc_ce(vdc_ce), .vdc_hb(vdc_hb), .vdc_vb(vdc_vb), .c128_fast(c128_fast), .c128_stat(c128_stat),
        .vd_clk(vd_clk), .vd_addr(vd_addr), .vd_we(vd_we), .vd_wdata(vd_wdata), .vd_rdata(vd_rdata)
    );

    cart_slot_c64u cart (
        .clk(gclk),
        .cart_type(cart_type), .desc(cart_desc), .cart_kill_tgl(cart_kill_tgl), .cart_force_tgl(cart_force_tgl),
        .cart_active(cart_active),
        .addr(xc_addr), .din(xc_din), .we(xc_we), .roml_n(xc_roml), .romh_n(xc_romh),
        .io1_n(xc_io1), .io2_n(xc_io2), .phi2_p(xc_phi2_p), .phi2_h(xc_phi2_h), .phi2_n(xc_phi2_n),
        .reset(xc_reset), .exrom(xc_exrom), .game(xc_game), .dout(xc_dout), .nmi(xc_nmi),
        .irq(xc_irq), .io2_we_valid(xc_io2_we_valid), .wait_cpu(xc_wait),
        .req_tgl(cart_req_tgl), .req_we(cart_req_we), .req_addr(cart_req_addr),
        .req_data(cart_req_data), .req_slot(cart_req_slot), .req_cached(cart_req_cached),
        .ack_tgl(cart_ack_tgl),
        .fill_clk(cart_wclk), .fill_be(cart_fill_be), .fill_addr(cart_fill_addr),
        .fill_data(cart_fill_data), .st_bank(l_bank),
        .fl_enable(cart_fl_enable), .fl_magic1(cart_fl_magic1), .fl_magic2(cart_fl_magic2),
        .fl_mask(cart_fl_mask), .fl_id(cart_fl_id), .evt_tgl(cart_evt_tgl), .evt_word(cart_evt_word),
        .fl_ack_tgl(cart_fl_ack_tgl), .inval_tgl(cart_inval_tgl), .ee_tgl(cart_ee_tgl),
        .ee_clk(cart_ee_clk), .ee_addr(cart_ee_addr), .ee_we(cart_ee_we), .ee_wdata(cart_ee_wdata),
        .ee_rdata(cart_ee_rdata), .freeze(freeze_btn),
        .wtap(), .wtap_addr(), .wtap_data(),
        .pk_addr(16'd0), .pk_cls(2'd0), .pk_hit(c_pk_hit), .pk_mem(c_pk_mem), .pk_maddr(c_pk_maddr),
        .pk_data(c_pk_data)
    );

    // the real expansion port: ROML/ROMH/IO1/IO2 reads it serves take its data
    assign port_rd = ((!xc_roml || !xc_romh) && s_rom) || (!xc_io1 && s_io1_rd) || (!xc_io2 && s_io2_rd);
    slot_port_c64u #(.EN(SLOT_EN)) slot (
        .clk(clk), .rst(rsti), .ce(ce), .ce_d(ce_d), .halted(halted_r),
        .bus_int(slot_int), .bus_ext(slot_ext), .cart_int(cart_type[4:0] != 5'd0),
        .rom_wr_en(slot_wr_en), .ultimax(ultimax), .own_reset(cpu_reset_req || rsti),
        .phi2_p(xc_phi2_p), .cpu_reset(xc_reset), .vic(xc_vic), .vaddr(xc_vaddr), .ba(xc_ba),
        .addr(xc_addr), .din(xc_din), .we(xc_we),
        .roml_n(xc_roml), .romh_n(xc_romh), .io1_n(xc_io1), .io2_n(xc_io2),
        .sock_hit(sock_hit), .sock_en(sk_c[33:32]), .sock_split(sk_c[36:34]),
        .rom_port(s_rom), .io1_rd(s_io1_rd), .io2_rd(s_io2_rd), .io2_ext(s_io2_ext),
        .irq_ext(s_irq_ext), .present(s_present), .game(s_game), .exrom(s_exrom),
        .irq(s_irq), .nmi(s_nmi), .dout(s_dout), .reset_req(s_rst_req),
        .status(slot_status), .cnt0(slot_cnt0), .cnt1(slot_cnt1), .last(slot_last),
        .p_addr(slot_a), .p_ahi_t(slot_ahi_t), .p_rw(slot_rw), .p_d_o(slot_d_o), .p_d_t(slot_d_t),
        .p_d_i(slot_d_i), .p_adir(slot_adir), .p_aoen(slot_aoen), .p_ddir(slot_ddir), .p_doen(slot_doen),
        .p_roml(slot_roml), .p_romh(slot_romh), .p_io1(slot_io1), .p_io2(slot_io2),
        .p_phi2(slot_phi2), .p_dotclk(slot_dotclk), .p_ba(slot_ba), .p_rst_o(slot_rst_o),
        .p_sid1(slot_sid1), .p_sid2(slot_sid2), .p_sid_a(slot_sid_addr),
        .p_game(slot_game), .p_exrom(slot_exrom), .p_dma(slot_dma), .p_irq(slot_irq),
        .p_nmi(slot_nmi), .p_rst_i(slot_rst_i)
    );

`ifdef RAM_DDR2
    c64_mem_c128d mem (
        .clk(gclk), .dbg_clk(clk), .clk2x(clk2x), .dbg_req(dbg_req),
        .tb_addr(tb_addr), .tb_raddr(tb_raddr), .tb_we(tb_we), .tb_wdata(tb_wdata),
        .tb_ram_lo(tb_ram_lo), .tb_ram_hi(tb_ram_hi), .tb_rom_lo(tb_rom_lo), .tb_rom_hi(tb_rom_hi), .tb_took(tb_took),
        .cmd_addr(mem_cmd_addr), .cmd_we(mem_cmd_we), .cmd_valid(mem_cmd_valid),
        .cmd_ready(mem_cmd_ready), .wdata(mem_wdata), .wdata_we(mem_wdata_we),
        .wdata_ready(mem_wdata_ready), .rdata(mem_rdata), .rdata_valid(mem_rdata_valid),
        .dbg_addr(dbg_addr), .dbg_rom(dbg_rom), .dbg_we(dbg_we), .dbg_wdata(dbg_wdata),
        .dbg_rdata(dbg_rdata), .dbg_ack(dbg_ack), .cmd_vic(mem_cmd_vic), .vic_tag(mem_vic_tag), .ram_hold(mem_ram_hold),
        .rom_flush(rsti || rom_flush_p), .rom_req_tgl(rom_req_tgl), .rom_req_line(rom_req_line), .rom_done_tgl(rom_done_tgl), .rom_fill(rom_fill),
        .rom_n_hit(rom_n_hit), .rom_n_miss(rom_n_miss), .rom_n_wait(rom_n_wait), .rom_fill_max(rom_fill_max), .rom_busy(rom_busy),
        .rom_miss(mem_rom_miss), .rom_fill_busy(mem_fill_busy),
        .ram_m_req_tgl(ram_m_req_tgl), .ram_m_req_line(ram_m_req_line), .ram_m_done_tgl(ram_m_done_tgl),
        .ram_w_req_tgl(ram_w_req_tgl), .ram_w_req_addr(ram_w_req_addr), .ram_w_req_data(ram_w_req_data), .ram_w_done_tgl(ram_w_done_tgl),
        .ram_n_vic_miss(ram_n_vic_miss), .ram_n_cpu_wait(ram_n_cpu_wait), .ram_n_fetch(ram_n_fetch), .ram_n_wlost(ram_n_wlost), .ram_n_vic_blk1(ram_n_vic_blk1)
    );
`else
    c64_mem_c128 mem (
        .clk(gclk), .dbg_clk(clk), .clk2x(clk2x), .dbg_req(dbg_req),
        .tb_addr(tb_addr), .tb_raddr(tb_raddr), .tb_we(tb_we), .tb_wdata(tb_wdata),
        .tb_ram_lo(tb_ram_lo), .tb_ram_hi(tb_ram_hi), .tb_rom_lo(tb_rom_lo), .tb_rom_hi(tb_rom_hi), .tb_took(tb_took),
        .cmd_addr(mem_cmd_addr), .cmd_we(mem_cmd_we), .cmd_valid(mem_cmd_valid),
        .cmd_ready(mem_cmd_ready), .wdata(mem_wdata), .wdata_we(mem_wdata_we),
        .wdata_ready(mem_wdata_ready), .rdata(mem_rdata), .rdata_valid(mem_rdata_valid),
        .dbg_addr(dbg_addr), .dbg_rom(dbg_rom), .dbg_we(dbg_we), .dbg_wdata(dbg_wdata),
        .dbg_rdata(dbg_rdata)
`ifdef ROM_CACHE
        , .rom_flush(rsti || rom_flush_p), .rom_req_tgl(rom_req_tgl), .rom_req_line(rom_req_line), .rom_done_tgl(rom_done_tgl), .rom_fill(rom_fill),
        .rom_n_hit(rom_n_hit), .rom_n_miss(rom_n_miss), .rom_n_wait(rom_n_wait), .rom_fill_max(rom_fill_max), .rom_busy(rom_busy),
        .rom_miss(mem_rom_miss), .rom_fill_busy(mem_fill_busy)
`endif
    );
`endif
`ifndef RAM_DDR2
    assign mem_ram_hold = 1'b0;
`endif
`ifndef ROM_CACHE
    assign mem_rom_miss  = 1'b0;
    assign mem_fill_busy = 1'b0;
`endif

    // T29 (spec §12.2 D7): a stop, a held CPU or DMA, a VIC restart: the engine falls back to normal speed first
    assign turbo_inh = halt_req || cpu_rst_in || halted_r || held || dma_in || trst != 6'd0 || rsti;

`ifdef SID_MULTI
    assign sid_resid8 = 1'b1;
`else
    assign sid_resid8 = 1'b0;
`endif

endmodule
