// TRX CPC core (T92): the Amstrad CPC 464, 664 and 6128 behind the host's layer-2 socket. The machine is MiSTer-devel/Amstrad_MiSTer's Gate
// Array, CRTC, PPI, MMU, FDC and ink table (cores/cpc/rtl) with the Spectrum core's Z80 and AY (cores/zx, shared), wired by cpc_board.v the way
// Amstrad_motherboard.v wires them, without the framework: the memories are block RAM (cpc_mem), the keyboard is the host's C64 matrix through the
// shared translator (cpc_keys), the tape is the shared pulse engine (zxcpc_tape), the disks are DDR2 blocks that the FDC reads and writes
// (cpc_fdc), the picture is RGB565 in DDR2 frame buffers for the host's pixel layer (cpc_pixw). This entity is what cores/cpc/layer2_cpc.v
// wraps; it has no C64 part in it.
//
// Clock (D4): everything runs on sys_clk (the host's C64 clock, 31.527778 MHz PAL) with clock enables from one accumulator (zxcpc_nco): the Gate
// Array's 16 MHz is sys_clk x 32/63, so a standard CPC frame (312 lines of 1,024 Gate Array clocks) is exactly the scaler's frame of 628,992
// sys_clk. The machine runs +0.089 % fast. The Gate Array makes the CPU's 4 MHz, the CRTC's and the AY's 1 MHz from it; the FDC's 8 MHz and
// the tape's 3.5 MHz unit are taken from the same enable.
// Freeze (D12): every enable stops at once (a clock edge); the host's black raster for the scaler keeps running, so no lock concern. A reset
// takes the hold away.
// Picture (D6): the Gate Array's ink outputs go through color_mix to RGB565 into three frame buffers (720 x 540, every CPC line twice); the
// edge's pix_color/pix_x/pix_y carry only a black raster of the scaler's size, so that the scaler locks and the output never stops.
`timescale 1ns / 1ps
module cpc_core #(
    parameter integer ID_TRX2 = 0,
    parameter integer PIX_X0 = 168,                // 720 - 640 = 80 border columns, 40 each side (Alex 2026-10-19: centred; it was 152 = 56 left, 24 right)
    parameter integer PIX_Y0 = 31
) (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        cpu_clk,
    input  wire        mclk,
    input  wire        ui_clk,
    input  wire        ui_rst,
    input  wire        reset_req,
    input  wire        halt_req,
    output wire        halted,
    output wire        running,
    output wire        drv_led,                        // T159 D-all: the disk drive's motor runs (port $FA7E bit 0, 664 and 6128; sys_clk)

    input  wire [63:0] keys,
    input  wire [4:0]  joy1,
    input  wire [4:0]  joy2,

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
    // the host's real-time DDR2 port c_* (ui_clk): the tape's pulse stream and the disk images
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
    // ================================================================ the app's settings (cpu_clk) into sys_clk
    wire [7:0]  r_ctrl;
    wire [1:0]  r_model;
    wire        r_joyswap, r_crtc, r_tplay, r_tseek;
    wire [2:0]  r_winpage;
    wire [31:0] r_tbase, r_fbbase, r_fbstride, r_d0b, r_d0s, r_d1b, r_d1s;
    wire [24:0] r_tpos;
    wire [79:0] r_inject;
    wire [1:0]  r_mount, r_eject, r_wp;
    wire        r_clr_tgl, r_clr_d;
    wire [2:0]  r_clr_i;
    wire [7:0]  r_clr_a;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ctrl_s1 = 8'd0, ctrl_s2 = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [7:0] set_s1 = 8'd0, set_s2 = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] sk_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [2:0] mt0_s = 3'b000, mt1_s = 3'b000, ej0_s = 3'b000, ej1_s = 3'b000;
    reg [79:0] inj_l = 80'd0;
    reg [31:0] tbase_l = 32'd0, d0b_l = 32'd0, d0s_l = 32'd0, d1b_l = 32'd0, d1s_l = 32'd0;
    reg [24:0] tpos_l = 25'd0;
    reg [1:0]  wp_l = 2'b00;
    reg        clr_s = 1'b0;
    always @(posedge sys_clk) begin
        ctrl_s1 <= r_ctrl;
        ctrl_s2 <= ctrl_s1;
        set_s1 <= {r_tplay, r_winpage, r_crtc, r_joyswap, r_model[1:0]};
        set_s2 <= set_s1;
        sk_s <= {sk_s[1:0], r_tseek};
        mt0_s <= {mt0_s[1:0], r_mount[0]};
        mt1_s <= {mt1_s[1:0], r_mount[1]};
        ej0_s <= {ej0_s[1:0], r_eject[0]};
        ej1_s <= {ej1_s[1:0], r_eject[1]};
        inj_l <= r_inject;
        tbase_l <= r_tbase; tpos_l <= r_tpos;
        d0b_l <= r_d0b; d0s_l <= r_d0s; d1b_l <= r_d1b; d1s_l <= r_d1s; wp_l <= r_wp;
    end
    wire [1:0] model_s   = set_s2[1:0];
    wire       joyswap_s = set_s2[2];
    wire       crtc_s    = set_s2[3];
    wire [2:0] winpage_s = set_s2[6:4];
    wire       tplay_s   = set_s2[7];
    wire       mount0_ev = mt0_s[2] ^ mt0_s[1], mount1_ev = mt1_s[2] ^ mt1_s[1];
    wire       eject0_ev = ej0_s[2] ^ ej0_s[1], eject1_ev = ej1_s[2] ^ ej1_s[1];

    // ================================================================ reset and pause
    wire       rst_core = sys_rst | reset_req | ctrl_s2[0];
    reg        rst_q = 1'b1;
    reg [7:0]  rcnt = 8'd0;
    reg [1:0]  model_l = 2'd2;
    reg        hold_q = 1'b0;
    wire       hold_req = halt_req | ctrl_s2[1];
    always @(posedge sys_clk) begin
        rst_q <= rst_core;
        if (rst_core) begin rcnt <= 8'd0; model_l <= (model_s == 2'd3) ? 2'd2 : model_s; end
        else if (rcnt != 8'hFF) rcnt <= rcnt + 8'd1;
        hold_q <= hold_req & ~rst_core;                // a reset takes the hold away (the menu's Reset works while frozen)
    end
    wire       board_reset = rst_q | (rcnt != 8'hFF);
    wire       ram64k = (model_l != 2'd2);
    wire       has_amsdos = (model_l != 2'd0);

    // ================================================================ the clock enables
    wire ce_16;
    zxcpc_nco #(.W(7)) nco (.clk(sys_clk), .hold(hold_q), .restart(1'b0), .num(7'd32), .den(7'd63), .en(ce_16));
    reg        ce16_d = 1'b0, fdiv = 1'b0;
    always @(posedge sys_clk) begin
        ce16_d <= ce_16;
        if (ce_16) fdiv <= ~fdiv;
    end
    wire ce_fdc = ce_16 & fdiv;                        // 8 MHz
    wire tape_tick;
    zxcpc_nco #(.W(6)) nco_tape (.clk(sys_clk), .hold(~ce_16), .restart(1'b0), .num(6'd7), .den(6'd32), .en(tape_tick));   // 3.5 MHz unit

    // ================================================================ the machine
    wire [79:0] cpc_mat;
    cpc_keys keyx (.keys(keys), .cpc_mat(cpc_mat));
    wire [4:0] stick0 = joyswap_s ? joy1 : joy2;       // port 2 -> joystick 0 (matrix line 9), port 1 -> joystick 1 (line 6)
    wire [4:0] stick1 = joyswap_s ? joy2 : joy1;
    wire [79:0] mat_all;
    assign mat_all = cpc_mat | inj_l | ({75'd0, stick0} << 72) | ({75'd0, stick1} << 48);

    wire [22:0] mem_addr;
    wire        mem_rd, mem_wr, romen, phi_en_n, phi_en_p, cclk_en_n, cclk_en_p, iorq, io_rd, io_wr, ga_ready, cpu_m1;
    wire [15:0] cpu_addr, cpu_pc;
    wire [7:0]  cpu_dout, mem_q;
    wire [14:0] vram_addr;
    wire [15:0] vram_q;
    wire [7:0]  ay_a, ay_b, ay_c;
    wire [1:0]  mode, red, green, blue;
    wire        hsync, vsync, crtc_de;
    wire        tape_out, tape_motor, tape_level;
    wire        tape_in_w = tape_level;
    wire [7:0]  fdc_dout;

    wire [7:0] cpu_din = (mem_rd ? mem_q : 8'hFF) & fdc_dout;
    cpc_board board (
        .reset(board_reset), .clk(sys_clk), .ce_16(ce_16),
        .mat(mat_all), .crtc_type(crtc_s), .ppi_jumpers(4'b1111),
        .tape_in(tape_in_w), .tape_out(tape_out), .tape_motor(tape_motor),
        .ay_a(ay_a), .ay_b(ay_b), .ay_c(ay_c),
        .mode(mode), .red(red), .green(green), .blue(blue), .hsync(hsync), .vsync(vsync), .crtc_de(crtc_de),
        .vram_din(vram_q), .vram_addr(vram_addr),
        .has_amsdos(has_amsdos), .ram64k(ram64k), .mem_addr(mem_addr), .mem_rd(mem_rd), .mem_wr(mem_wr), .romen(romen),
        .phi_en_n(phi_en_n), .phi_en_p(phi_en_p), .cpu_addr(cpu_addr), .cpu_dout(cpu_dout), .cpu_din(cpu_din),
        .iorq(iorq), .io_rd(io_rd), .io_wr(io_wr), .cclk_en_n(cclk_en_n), .cclk_en_p(cclk_en_p), .ga_ready(ga_ready),
        .cpu_pc_dbg(cpu_pc), .cpu_m1(cpu_m1)
    );

    // ---- memory and the app's window
    wire        w_start, w_we, w_rom, w_done, mem_busy;
    wire [16:0] w_a;
    wire [7:0]  w_di, w_do;
    cpc_mem mem (
        .clk(sys_clk), .mem_addr(mem_addr), .mem_rd(mem_rd), .mem_wr(mem_wr), .romen(romen), .cpu_d(cpu_dout), .mem_q(mem_q),
        .vram_addr(vram_addr), .vram_q(vram_q),
        .w_start(w_start), .w_we(w_we), .w_rom(w_rom), .w_a(w_a), .w_di(w_di), .w_done(w_done), .w_do(w_do), .busy(mem_busy)
    );
    cpc_win win (
        .clk(sys_clk),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(dbg_busy), .page(winpage_s),
        .port_use(win_port_use), .port_addr(win_port_addr), .port_rom(win_port_rom), .port_we(win_port_we),
        .port_wdata(win_port_wdata), .p_req(1'b0),
        .w_start(w_start), .w_we(w_we), .w_rom(w_rom), .w_a(w_a), .w_di(w_di), .w_done(w_done), .w_do(w_do)
    );

    // ---- the FDC (664 and 6128; the 464 has none): ports $FA7E (motor), $FB7E / $FB7F (u765)
    wire [3:0] fdc_sel = {cpu_addr[10], cpu_addr[8], cpu_addr[7], cpu_addr[0]};
    wire       u765_sel = (fdc_sel[3:1] == 3'b010) & has_amsdos;
    reg        motor = 1'b0, io_wr_d = 1'b0;
    assign drv_led = motor & has_amsdos;
    always @(posedge sys_clk) begin
        io_wr_d <= io_wr;
        if (board_reset) motor <= 1'b0;
        else if (~io_wr_d && io_wr && (fdc_sel[3:1] == 3'b000)) motor <= cpu_dout[0];
    end
    wire [7:0] u765_dout;
    assign fdc_dout = (u765_sel & io_rd) ? u765_dout : 8'hFF;
    reg  [1:0] u765_ready = 2'b00;
    reg  [1:0] img_mounted = 2'b00;
    reg [31:0] img_size = 32'd0;
    reg        img_wp = 1'b0;
    always @(posedge sys_clk) begin
        img_mounted <= 2'b00;
        if (mount0_ev | eject0_ev) begin
            img_mounted[0] <= 1'b1; img_size <= eject0_ev ? 32'd0 : d0s_l; img_wp <= wp_l[0];
            u765_ready[0] <= ~eject0_ev & (|d0s_l);
        end else if (mount1_ev | eject1_ev) begin
            img_mounted[1] <= 1'b1; img_size <= eject1_ev ? 32'd0 : d1s_l; img_wp <= wp_l[1];
            u765_ready[1] <= ~eject1_ev & (|d1s_l);
        end
    end
    wire [31:0] sd_lba;
    wire [1:0]  sd_rd, sd_wr;
    wire        sd_ack, sd_buff_wr, fdc_busy, fdc_oob, fdc_ui_busy;
    wire [8:0]  sd_buff_addr;
    wire [7:0]  sd_buff_dout, sd_buff_din;
    u765 fdc (
        .reset(board_reset), .clk_sys(sys_clk), .ce(ce_fdc), .ready(u765_ready), .motor({motor, motor}), .available(2'b11), .fast(1'b0),
        .a0(fdc_sel[0]), .nRD(~(u765_sel & io_rd)), .nWR(~(u765_sel & io_wr)), .din(cpu_dout), .dout(u765_dout),
        .img_mounted(img_mounted), .img_wp(img_wp), .img_size(img_size),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack),
        .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout), .sd_buff_din(sd_buff_din), .sd_buff_wr(sd_buff_wr)
    );
    wire [63:0] dirty0, dirty1;
    wire [26:0] f_addr;
    wire [2:0]  f_cmd;
    wire        f_en;
    wire [127:0] f_wdf_data;
    wire        f_wdf_wren;
    wire        f_rdy, f_valid;
    cpc_fdc fdca (
        .clk(sys_clk), .rst(board_reset), .hold(1'b0),
        .sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack), .sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
        .sd_buff_wr(sd_buff_wr), .sd_buff_din(sd_buff_din),
        .d0_base(d0b_l), .d0_size(d0s_l), .d1_base(d1b_l), .d1_size(d1s_l), .dirty0(dirty0), .dirty1(dirty1),
        .clr_tgl(r_clr_tgl), .clr_d(r_clr_d), .clr_i(r_clr_i), .clr_a(r_clr_a), .busy(fdc_busy), .oob(fdc_oob),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(f_addr), .c_cmd(f_cmd), .c_en(f_en), .c_rdy(f_rdy),
        .c_wdf_data(f_wdf_data), .c_wdf_mask(), .c_wdf_wren(f_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(f_valid), .ui_busy(fdc_ui_busy)
    );

    // ---- tape (the PPI's motor bit gates the stream)
    wire        tape_playing, tape_eof, tape_stop, tape_err, t_busy, t_rdy, t_valid;
    wire [24:0] tape_pos;
    wire [7:0]  tape_under;
    wire [26:0] t_addr;
    wire [2:0]  t_cmd;
    wire        t_en;
    zxcpc_tape tape (
        .clk(sys_clk), .rst(board_reset), .hold(hold_q), .tick(tape_tick), .play(tplay_s & tape_motor), .base(tbase_l),
        .seek_tgl(sk_s[2]), .seek_pos(tpos_l), .level(tape_level), .playing(tape_playing), .eof(tape_eof), .stopmark(tape_stop),
        .pos_o(tape_pos), .underrun(tape_under), .ddr_err(tape_err),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(t_addr), .c_cmd(t_cmd), .c_en(t_en), .c_rdy(t_rdy),
        .c_rd_data(c_rd_data), .c_rd_valid(t_valid), .ui_busy(t_busy)
    );
    cpc_ddr_mux ddrmux (
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .a_busy(t_busy), .a_addr(t_addr), .a_cmd(t_cmd), .a_en(t_en),
        .b_busy(fdc_ui_busy), .b_addr(f_addr), .b_cmd(f_cmd), .b_en(f_en), .b_wdf_data(f_wdf_data), .b_wdf_wren(f_wdf_wren),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rdy(c_rdy), .c_rd_valid(c_rd_valid), .a_rdy(t_rdy), .a_valid(t_valid), .b_rdy(f_rdy), .b_valid(f_valid)
    );

    // ================================================================ picture
    wire [7:0] cm_r, cm_g, cm_b;
    wire       cm_hb, cm_vb, cm_hs, cm_vs;
    color_mix colormix (
        .clk_vid(sys_clk), .ce_pix(ce_16), .mix(3'd0), .R_in(red), .G_in(green), .B_in(blue),
        .HSync_in(hsync), .VSync_in(vsync), .HBlank_in(hsync), .VBlank_in(vsync),
        .R_out(cm_r), .G_out(cm_g), .B_out(cm_b), .HSync_out(cm_hs), .VSync_out(cm_vs), .HBlank_out(cm_hb), .VBlank_out(cm_vb)
    );
    wire [15:0] rgb565 = {cm_r[7:3], cm_g[7:2], cm_b[7:3]};
    wire        pw_quiet, frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    cpc_pixw #(.X0(PIX_X0), .Y0(PIX_Y0)) pixw (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .dotst(ce16_d), .rgb565(rgb565), .hblank(cm_hb), .vblank(cm_vb),
        .quiet(pw_quiet), .frame_tgl(frame_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over),
        .fb_base(r_fbbase), .fb_stride(r_fbstride),
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

    // ================================================================ sound: the AY's three channels, mono
    wire [15:0] ay_sum = {8'd0, ay_a} + {8'd0, ay_b} + {8'd0, ay_c};
    reg signed [15:0] mix_q = 16'sd0;
    always @(posedge sys_clk) mix_q <= {1'b0, ay_sum[9:0], 5'd0};      // x 32: three full channels = 24,480
    zxcpc_audio audio (.clk(sys_clk), .a_in(mix_q), .mute(hold_q), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // ================================================================ status
    assign halted = hold_q & pw_quiet & ~fdc_busy & ~fdc_ui_busy & ~t_busy & ~mem_busy;
    assign running = ~board_reset;
    reg [15:0] frames = 16'd0;
    reg        vs_d = 1'b0;
    reg        frame_cpc_tgl = 1'b0;
    always @(posedge sys_clk) begin
        vs_d <= vsync;
        if (vsync & ~vs_d) begin frames <= frames + 16'd1; frame_cpc_tgl <= ~frame_cpc_tgl; end
    end
    (* ASYNC_REG = "TRUE" *) reg [2:0] ft_s = 3'b000;
    always @(posedge cpu_clk) ft_s <= {ft_s[1:0], frame_tgl};
    wire ev_frame = ft_s[2] ^ ft_s[1];
    wire [7:0] status = {4'd0, model_l, hold_q, board_reset};
    (* ASYNC_REG = "TRUE" *) reg [3:0] ts1 = 4'd0, ts2 = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ds1 = 2'd0, ds2 = 2'd0;
    always @(posedge cpu_clk) begin
        ts1 <= {tape_err, tape_stop, tape_eof, tape_playing}; ts2 <= ts1;
        ds1 <= {fdc_oob, fdc_busy}; ds2 <= ds1;
    end
    cpc_regs #(.ID_TRX2(ID_TRX2)) regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(r_ctrl), .model(r_model), .joy_swap(r_joyswap), .crtc_type(r_crtc), .win_page(r_winpage),
        .fb_base(r_fbbase), .fb_stride(r_fbstride),
        .tape_play(r_tplay), .tape_base(r_tbase), .tape_seek_pos(r_tpos), .tape_seek_tgl(r_tseek), .inject(r_inject),
        .d0_base(r_d0b), .d0_size(r_d0s), .d1_base(r_d1b), .d1_size(r_d1s), .mount_tgl(r_mount), .eject_tgl(r_eject), .disk_wp(r_wp),
        .dirty0(dirty0), .dirty1(dirty1), .dirty_clr_a(r_clr_a), .dirty_clr_d(r_clr_d), .dirty_clr_tgl(r_clr_tgl), .dirty_clr_i(r_clr_i),
        .status(status), .frames(frames), .cpuaddr(cpu_addr), .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over),
        .tape_pos(tape_pos), .tape_stat(ts2), .tape_under(tape_under), .disk_stat(ds2)
    );

    // ---- JTAG proof of life (trxdbg "trx 0 ..."), words: 0 "CPC_"  1 status  2 frames  3 CPU address  4 PC of the Z80
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h5F435043;
            4'h1: jd = {24'd0, status};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, cpu_addr};
            4'h4: jd = {16'd0, cpu_pc};
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
