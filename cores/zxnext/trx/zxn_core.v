// TRX ZX Spectrum Next core (T116): the Next's FPGA source (cores/zxnext/gen/zxnext.v: Z80N, DMA, Copper, ULA, Layer 2, sprites, tilemap, three AY,
// DACs, UART, SPI, CTC, DivMMC, Multiface; Alvin Albrecht, Victor Trucco, Fabio Belavenuto and others, GPL v3 or later, see rtl/NOTICE) behind the
// host's layer-2 socket. This entity is what cores/zxnext/layer2_zxnext.v wraps; it has no C64 part in it.
//
// Clock (P8): the machine was written for five related clocks (28 MHz, its inverse, 14, 7 and a CPU clock that is gated for contention); it runs
// on cpu_clk, the host's 100 MHz, with those clocks as enables (zxn_clk): tools/zxnext_gen.py gave every clocked process of the machine the pair
// X_cep / X_cen. The 28 MHz is an NCO of 100 MHz, run 0.09 % (48K timing, 50 Hz) to 2.7 % (Pentagon) fast so that the machine's 50 Hz frame is the
// host's frame (T116 Q6, as T91 D4); 60 Hz timings run at 28.000 MHz and the pixel layer repeats and drops frames.
// Freeze (C10): every master cycle stops at the next r28 (the machine stands exactly where it was, and resumes there); the machine's own reset is
// not held by Freeze (a reset takes the hold away). A cache miss holds the machine the same way (zxn_mem stall).
// Memory (P2, P3): the machine's 2 MB address space in the DDR2 behind two caches (zxn_mem). Picture (P5): the 9 bit RGB at each 14 MHz dot into
// RGB565 and a DDR2 frame buffer (zxn_pixw); the host's VIC-II raster input gets a dummy raster. Sound (P6): zxn_audio. Input (P7): zxn_keys, the
// mouse counters of the register block. SD card (P4): zxn_sd, answered from the app's image through a mailbox and the DDR2 staging sector.
// RTC (Q10): zxn_i2c, a DS1307 whose time the app writes. Everything in section 4 of the spec is tied off.
`timescale 1ns / 1ps
module zxn_core #(
    parameter integer ID_TRX2 = 0
) (
    input  wire        sys_clk,                     // the host's C64 clock: requests in, dummy raster out
    input  wire        sys_rst,
    input  wire        cpu_clk,                     // the machine and the app's bus (100 MHz)
    input  wire        mclk,                        // audio
    input  wire        ui_clk,                      // the host's DDR2 user clock
    input  wire        ui_rst,
    input  wire        reset_req,                   // the app's machine reset (sys_clk)
    input  wire        halt_req,                    // the app's pause (sys_clk)
    output wire        halted,                      // sys_clk
    output wire        running,                     // out of reset, sys_clk

    input  wire        dma_req_tgl,
    input  wire [15:0] dma_addr,
    input  wire        dma_we,
    input  wire [7:0]  dma_wdata,
    input  wire        dma_rom,
    output wire        dma_done_tgl,
    output wire [7:0]  dma_rdata,
    input  wire        dbg_busy,
    output wire        win_port_use,
    output wire [15:0] win_port_addr,
    output wire        win_port_rom,
    output wire        win_port_we,
    output wire [7:0]  win_port_wdata,

    input  wire [4:0]  c64_joy1,
    input  wire [4:0]  c64_joy2,
    input  wire [63:0] c64_keys,
    input  wire        c64_restore,                 // the DivMMC "drive" button: NextZXOS' NMI menu

    output wire [3:0]  pix_color,
    output wire [9:0]  pix_x,
    output wire [8:0]  pix_y,
    output wire signed [23:0] audio_l,
    output wire signed [23:0] audio_r,

    input  wire        cw_we,
    input  wire        cw_re,
    input  wire [16:0] cw_addr,
    input  wire [7:0]  cw_wdata,
    output wire [7:0]  cw_rdata,
    output wire        cw_ack,
    output wire        cw_irq,
    output wire        px_we,
    output wire        px_re,
    input  wire [7:0]  px_rdata,
    input  wire        px_ev_vsync,
    input  wire        px_ev_flip,
    input  wire [31:0] build,
    input  wire [63:0] ddr_cal,
    input  wire        dbg_re,
    input  wire [11:0] dbg_addr,
    output wire [31:0] j_rdata,
    output wire        j_rvalid,

    // the host's general DDR2 port e_* (ui_clk): the frame writer
    output wire [26:0] e_addr,
    output wire [2:0]  e_cmd,
    output wire        e_en,
    input  wire        e_rdy,
    output wire [127:0] e_wdf_data,
    output wire [15:0] e_wdf_mask,
    output wire        e_wdf_wren,

    // the host's real-time DDR2 port c_* (ui_clk): the machine's memory and the SD staging sector (zxn_mem)
    output wire [26:0] c_addr,
    output wire [2:0]  c_cmd,
    output wire        c_en,
    input  wire        c_rdy,
    output wire [127:0] c_wdf_data,
    output wire [15:0] c_wdf_mask,
    output wire        c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid
);
    // ---------------------------------------------------------------- the app's requests (sys_clk) into the machine clock
    wire [7:0]  ctrl;
    wire        joy_swap, lock_off;
    wire [3:0]  board_id;
    wire [31:0] fb_base, fb_stride, ram_base, stage_base;
    wire [55:0] rtc;
    wire [9:0]  fkeys;
    wire        mf_btn;
    wire        sd_present;
    wire [31:0] sd_sectors;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rq_s = 2'b00, hl_s = 2'b00, rs_s = 2'b00;
    always @(posedge cpu_clk) begin
        rq_s <= {rq_s[0], reset_req | sys_rst};
        hl_s <= {hl_s[0], halt_req};
    end

    // ---------------------------------------------------------------- reset and pause
    // rst_r: the app's reset, the request stretched by 256 clocks; it flushes the caches. The machine's own reset input is rst_r and, after it,
    // 1024 master cycles (the original's top holds its reset counter for as long); a soft reset the machine asks for (nextreg 2) has the same.
    wire       rst_m = rq_s[1] | ctrl[0];
    reg        rst_r = 1'b1;
    reg [8:0]  rcnt = 9'd0;
    always @(posedge cpu_clk) begin
        if (rst_m) begin rcnt <= 9'd0; rst_r <= 1'b1; end
        else if (rcnt[8]) rst_r <= 1'b0;
        else rcnt <= rcnt + 9'd1;
    end
    reg        hold_q = 1'b0;
    wire       hold_req = hl_s[1] | ctrl[1];
    always @(posedge cpu_clk) begin
        hold_q <= hold_req & ~rst_m & ~rst_r;                // a reset takes the hold away (the menu's Reset works while frozen)
    end

    // ---------------------------------------------------------------- the clock enables
    wire        stall, sd_hold;
    wire [1:0]  cpu_speed;
    wire        cpu_lsb, cpu_contend;
    wire [2:0]  mach_timing;
    wire        v5060;
    wire [31:0] step = lock_off ? 32'd1202590843 :
                       v5060 ? 32'd1202590843 :
                       (mach_timing[2]) ? 32'd1234521144 :
                       (mach_timing[1]) ? 32'd1221225241 : 32'd1203658116;
    wire        r28, f28, r14, f14, r7, f7, cpu_r, cpu_f, psg_en, clk_idle;
    zxn_clk zclk (
        .clk(cpu_clk), .rst(1'b0), .hold(hold_q | stall | sd_hold), .step(step), .cpu_speed(cpu_speed), .cpu_lsb(cpu_lsb), .cpu_contend(cpu_contend),
        .r28(r28), .f28(f28), .r14(r14), .f14(f14), .r7(r7), .f7(f7), .cpu_r(cpu_r), .cpu_f(cpu_f), .psg_en(psg_en), .idle(clk_idle)
    );

    // ---------------------------------------------------------------- the machine
    wire        rst_soft, rst_hard;
    reg [10:0]  rcount = 11'd0;
    reg         rsoft_q = 1'b0;
    always @(posedge cpu_clk) begin
        rsoft_q <= rst_soft | rst_hard;
        if (rst_r) rcount <= 11'd1024;
        else if ((rst_soft | rst_hard) && !rsoft_q && rcount == 11'd0) rcount <= 11'd1024;
        else if (r28 && rcount != 11'd0) rcount <= rcount - 11'd1;
    end
    wire        zx_reset = rst_r | (rcount != 11'd0);

    wire [7:0]  kbd_row;
    wire [4:0]  kbd_col;
    wire [15:0] kbd_ext;
    wire [11:0] joy_l, joy_r;
    wire [7:0]  mouse_x, mouse_y;
    wire [2:0]  mouse_btn;
    zxn_keys zkeys (.keys(c64_keys), .row(kbd_row), .col(kbd_col), .ext(kbd_ext), .joy1(c64_joy1), .joy2(c64_joy2), .joy_swap(joy_swap),
                   .joy_left(joy_l), .joy_right(joy_r));

    wire        spi_sck, spi_mosi, spi_ss_sd0_n, spi_ss_sd1_n, spi_ss_fl_n;
    wire        sd_miso;
    reg         sd_miso_q = 1'b1;
    always @(posedge cpu_clk) if (cpu_r) sd_miso_q <= sd_miso;      // the original's top registers SCK and MOSI at the CPU clock and not MISO (zxnext_top_issue5.vhd:2062-2074);
                                                                    // here SCK and MOSI reach the model unregistered, so MISO takes the one CPU clock (benches sd, sdfast: 3.5 and 28 MHz)

    wire        scl_n_o, sda_n_o, sda_pull;
    wire        sda_bus = sda_n_o & ~sda_pull;

    wire [20:0] ram_a_addr, ram_b_addr;
    wire [7:0]  ram_a_do, ram_a_di, ram_b_di;
    wire        ram_a_req, ram_a_rd_n, ram_b_req_t, ram_a_addr_21;
    wire [8:0]  rgb;
    wire        rgb_vs_n, rgb_hs_n, rgb_bk_n;
    wire [12:0] aud_l, aud_r;
    zxnext zxn (
        .fclk(cpu_clk),
        .i_CLK_28_cep(r28), .i_CLK_28_cen(f28), .i_CLK_28_n_cep(f28), .i_CLK_28_n_cen(r28),
        .i_CLK_14_cep(r14), .i_CLK_14_cen(f14), .i_CLK_7_cep(r7), .i_CLK_7_cen(f7), .i_CLK_CPU_cep(cpu_r), .i_CLK_CPU_cen(cpu_f),
        .i_CLK_PSG_EN(psg_en),
        .i_BOARD_ID(board_id),
        .i_RESET(zx_reset),
        .i_SPKEY_FUNCTION(fkeys), .i_SPKEY_BUTTONS({c64_restore, mf_btn}),
        .i_KBD_COL(kbd_col), .i_KBD_EXTENDED_KEYS(kbd_ext),
        .i_JOY_LEFT(joy_l), .i_JOY_RIGHT(joy_r),
        .i_MOUSE_X(mouse_x), .i_MOUSE_Y(mouse_y), .i_MOUSE_BUTTON(mouse_btn), .i_MOUSE_WHEEL(4'd0),
        .i_I2C_SCL_n(scl_n_o), .i_I2C_SDA_n(sda_bus),
        .i_SPI_SD_MISO(sd_miso_q), .i_SPI_FLASH_MISO(1'b1),
        .i_UART0_RX(1'b1), .i_UART0_CTS_n(1'b1),
        .i_AUDIO_EAR(1'b0),
        .i_RAM_A_DI(ram_a_di), .i_RAM_B_DI(ram_b_di),
        .i_BUS_DI(8'hFF), .i_BUS_WAIT_n(1'b1), .i_BUS_NMI_n(1'b1), .i_BUS_INT_n(1'b1), .i_BUS_BUSREQ_n(1'b1), .i_BUS_ROMCS_n(1'b1), .i_BUS_IORQULA_n(1'b1),
        .i_ESP_GPIO_20(3'd0), .i_GPIO(28'd0), .i_XDNA_DO(1'b0), .i_XADC_DRDY(1'b0), .i_XADC_DO(16'd0), .i_XADC_BUSY(1'b0), .i_XADC_EOC(1'b0), .i_XADC_EOS(1'b0),
        .o_CPU_SPEED(cpu_speed), .o_CPU_CONTEND(cpu_contend), .o_CPU_CLK_LSB(cpu_lsb),
        .o_RESET_SOFT(rst_soft), .o_RESET_HARD(rst_hard), .o_RESET_PERIPHERAL(), .o_FLASH_BOOT(), .o_CORE_ID(),
        .o_KBD_CANCEL(), .o_KBD_ROW(kbd_row), .o_KEYMAP_ADDR(), .o_KEYMAP_DATA(), .o_KEYMAP_WE(), .o_JOYMAP_WE(),
        .o_JOY_IO_MODE_EN(), .o_JOY_IO_MODE_PIN_7(), .o_JOY_LEFT_TYPE(), .o_JOY_RIGHT_TYPE(), .o_PS2_MODE(), .o_MOUSE_CONTROL(),
        .o_I2C_SCL_n(scl_n_o), .o_I2C_SDA_n(sda_n_o),
        .o_SPI_SS_FLASH_n(spi_ss_fl_n), .o_SPI_SS_SD1_n(spi_ss_sd1_n), .o_SPI_SS_SD0_n(spi_ss_sd0_n), .o_SPI_SCK(spi_sck), .o_SPI_MOSI(spi_mosi),
        .o_UART0_TX(), .o_UART0_RTR_n(),
        .o_RGB(rgb), .o_RGB_VS_n(rgb_vs_n), .o_RGB_HS_n(rgb_hs_n), .o_RGB_BK_n(rgb_bk_n),
        .o_VIDEO_50_60(v5060), .o_VIDEO_SCANLINES(), .o_VIDEO_SCANDOUBLE(), .o_VIDEO_MODE(), .o_MACHINE_TIMING(mach_timing),
        .o_HDMI_RESET(), .o_HDMI_PIXEL(), .o_HDMI_LOCK(),
        .o_AUDIO_HDMI_AUDIO_EN(), .o_AUDIO_SPEAKER_EN(), .o_AUDIO_SPEAKER_EXCL(), .o_AUDIO_PI_FE_EAR(), .o_AUDIO_ISSUE2_FE_MIC(),
        .o_AUDIO_MIC(), .o_AUDIO_EAR(), .o_AUDIO_L(aud_l), .o_AUDIO_R(aud_r),
        .o_RAM_A_ADDR_21(ram_a_addr_21), .o_RAM_A_ADDR(ram_a_addr), .o_RAM_A_REQ(ram_a_req), .o_RAM_A_RD_n(ram_a_rd_n), .o_RAM_A_DO(ram_a_do),
        .o_RAM_B_ADDR(ram_b_addr), .o_RAM_B_REQ_T(ram_b_req_t),
        .o_BUS_ADDR(), .o_BUS_DO(), .o_BUS_MREQ_n(), .o_BUS_IORQ_n(), .o_BUS_RD_n(), .o_BUS_WR_n(), .o_BUS_M1_n(), .o_BUS_INT_n(), .o_BUS_BUSAK_n(),
        .o_BUS_HALT_n(), .o_BUS_RFSH_n(), .o_BUS_IEO(), .o_BUS_P3_MTR_n(), .o_BUS_P3_DRD_n(), .o_BUS_P3_DWR_n(), .o_BUS_EN(), .o_BUS_CLKEN(),
        .o_BUS_NMI_DEBOUNCE_DISABLE(),
        .o_ESP_GPIO_0(), .o_ESP_GPIO_0_EN(), .o_GPIO(), .o_GPIO_EN(),
        .o_XDNA_LOAD(), .o_XDNA_SHIFT(), .o_XADC_RESET(), .o_XADC_DEN(), .o_XADC_DADDR(), .o_XADC_DWE(), .o_XADC_DI(), .o_XADC_CONVST(), .o_XADC_CONTROL()
    );

    // ---------------------------------------------------------------- the machine's memory (P2, P3) and the SD card (P4)
    wire        sd_start, sd_to_ddr, sd_done, sd_we, mem_busy;
    wire [4:0]  sd_k;
    wire [127:0] sd_wdata, sd_rdata;
    wire [31:0] st_miss_a, st_miss_b, st_pref, st_stall, st_late;
    zxn_mem mem (
        .clk(cpu_clk), .rst(rst_r), .ram_base(ram_base),
        .a_addr(ram_a_addr), .a_req_t(ram_a_req), .a_rd_n(ram_a_rd_n), .a_do(ram_a_do), .a_di(ram_a_di),
        .b_addr(ram_b_addr), .b_req_t(ram_b_req_t), .b_di(ram_b_di), .stall(stall),
        .sd_start(sd_start), .sd_to_ddr(sd_to_ddr), .sd_stage(stage_base), .sd_done(sd_done), .sd_k(sd_k), .sd_we(sd_we), .sd_wdata(sd_wdata), .sd_rdata(sd_rdata),
        .busy(mem_busy),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .st_miss_a(st_miss_a), .st_miss_b(st_miss_b), .st_pref(st_pref), .st_stall(st_stall), .st_late(st_late)
    );
    wire        mb_pend, mb_wr, mb_ack, sd_busy;
    wire [31:0] mb_lba, st_reads, st_writes;
    wire [3:0]  mb_seq;
    wire [7:0]  st_last_cmd;
    zxn_sd sd (
        .clk(cpu_clk), .rst(rst_r), .sck(spi_sck), .mosi(spi_mosi), .ss_n(spi_ss_sd0_n), .miso(sd_miso), .present(sd_present), .sectors(sd_sectors),
        .mb_pend(mb_pend), .mb_wr(mb_wr), .mb_lba(mb_lba), .mb_seq(mb_seq), .mb_ack(mb_ack),
        .sd_start(sd_start), .sd_to_ddr(sd_to_ddr), .sd_done(sd_done), .sd_k(sd_k), .sd_we(sd_we), .sd_wdata(sd_wdata), .sd_rdata(sd_rdata), .busy(sd_busy), .hold(sd_hold),
        .st_reads(st_reads), .st_writes(st_writes), .st_last_cmd(st_last_cmd)
    );
    zxn_i2c rtc_i2c (.clk(cpu_clk), .rst(rst_r), .scl_n(scl_n_o), .sda_n(sda_bus), .sda_pull(sda_pull), .time_bcd(rtc));

    // the host's window into a machine's ROM space is not used: a request is answered at once with nothing
    reg dn_q = 1'b0;
    always @(posedge cpu_clk) dn_q <= dma_req_tgl;
    assign dma_done_tgl = dn_q;
    assign dma_rdata = 8'd0;
    assign win_port_use = 1'b0;
    assign win_port_addr = 16'd0;
    assign win_port_rom = 1'b0;
    assign win_port_we = 1'b0;
    assign win_port_wdata = 8'd0;

    // ---------------------------------------------------------------- picture (P5)
    reg  [2:0] px_p = 3'd0;
    always @(posedge cpu_clk) px_p <= {px_p[1:0], r14};
    wire        px_q = px_p[1];                              // two clocks after the CLK_14 edge that produced the dot
    wire [15:0] rgb565 = {rgb[8:6], rgb[8:7], rgb[5:3], rgb[5:3], rgb[2:0], rgb[2:1]};
    reg  [9:0]  x0;
    reg  [8:0]  y0;
    always @* begin
        if (mach_timing[2]) begin x0 = 10'd64; y0 = 9'd32; end                          // Pentagon
        else if (mach_timing[1]) begin x0 = 10'd16; y0 = v5060 ? 9'd0 : 9'd24; end      // 128K, +3
        else begin x0 = 10'd0; y0 = v5060 ? 9'd0 : 9'd24; end                           // 48K
    end
    wire        pw_quiet, frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    zxn_pixw pixw (
        .sys_clk(cpu_clk), .sys_rst(rst_r), .dotst(px_q), .rgb565(rgb565), .hblank(~rgb_bk_n), .vblank(~rgb_vs_n), .x0(x0), .y0(y0),
        .quiet(pw_quiet), .frame_tgl(frame_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );

    // the raster the host's scaler locks to: 504 dots of 4 clocks, 312 lines, colour 0; not tied to the machine, never stops
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

    // ---------------------------------------------------------------- sound (P6)
    wire [12:0] aud_lm = aud_l, aud_rm = aud_r;
    zxn_audio audio (.clk(cpu_clk), .l_in(aud_lm), .r_in(aud_rm), .mute(hold_q), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // ---------------------------------------------------------------- registers
    reg [15:0] frames = 16'd0;
    reg        vs_q = 1'b1, ft_q = 1'b0, mb_q = 1'b0;
    always @(posedge cpu_clk) begin
        ft_q <= frame_tgl;
        mb_q <= mb_pend;
        if (px_q) begin
            vs_q <= rgb_vs_n;
            if (vs_q & ~rgb_vs_n) frames <= frames + 16'd1;
        end
    end
    wire ev_frame = ft_q ^ frame_tgl;
    wire ev_block = mb_pend & ~mb_q;
    wire [7:0] status = {4'd0, sd_busy | mem_busy, sd_present, hold_q & clk_idle, zx_reset};
    wire [7:0] machine = {2'd0, cpu_speed, v5060, mach_timing};
    zxn_regs #(.ID_TRX2(ID_TRX2)) regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame), .ev_block(ev_block),
        .ctrl(ctrl), .board_id(board_id), .joy_swap(joy_swap), .lock_off(lock_off), .fb_base(fb_base), .fb_stride(fb_stride), .ram_base(ram_base), .stage_base(stage_base),
        .rtc(rtc), .fkeys(fkeys), .mf_btn(mf_btn), .sd_present(sd_present), .sd_sectors(sd_sectors),
        .mb_pend(mb_pend), .mb_wr(mb_wr), .mb_lba(mb_lba), .mb_seq(mb_seq), .mb_ack(mb_ack), .mouse_x(mouse_x), .mouse_y(mouse_y), .mouse_btn(mouse_btn),
        .status(status), .machine(machine), .frames(frames), .st_miss_a(st_miss_a), .st_miss_b(st_late), .st_stall(st_stall), .st_pref(st_pref),
        .st_reads(st_reads), .st_writes(st_writes), .st_last_cmd(st_last_cmd),
        .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over)
    );

    // ---------------------------------------------------------------- the answers back in sys_clk
    // halted: the machine is held (Freeze), no master cycle is half done, the memory engine and the SD card have nothing on their way, and the
    // frame writer has nothing on its way to the DDR2
    (* ASYNC_REG = "TRUE" *) reg [1:0] ho_s = 2'b00, ru_s = 2'b00;
    always @(posedge sys_clk) begin
        ho_s <= {ho_s[0], hold_q & clk_idle & pw_quiet & ~mem_busy & ~sd_busy};
        ru_s <= {ru_s[0], ~zx_reset};
    end
    assign halted = ho_s[1];
    assign running = ru_s[1];

    // ---------------------------------------------------------------- JTAG proof of life (trxdbg "trx 0 ..."), words:
    //  0 "ZXN_"  1 status: b0 in reset, b1 held, b2 stall, b3 SD present  2 Next frames  3 CPU address (a sample)  4 memory misses  5 stall clocks  6 machine
    reg [15:0] cpu_a_s = 16'd0;
    always @(posedge cpu_clk) if (ram_a_req) cpu_a_s <= ram_a_addr[15:0];
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h5F4E585A;
            4'h1: jd = {28'd0, sd_present, stall, hold_q, zx_reset};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, cpu_a_s};
            4'h4: jd = st_miss_a;
            4'h5: jd = st_stall;
            4'h6: jd = {24'd0, machine};
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
