// TRX Neo Geo core (T143): MiSTer's NeoGeo core (cores/neogeo/rtl, Sean 'Furrtek' Gonsalves and others, GPL v2 or later / v3, see rtl/NOTICE) behind the host's layer-2 socket.
// This entity is what cores/neogeo/layer2_neogeo.v wraps; it has no C64 part in it.
//
// Clocks: the chip models want one 48 MHz clock with 24 MHz enables (neogeo.sv). neo_clk48 makes it from cpu_clk with an MMCM; the machine and the picture writer's
// first half run on it. The register block and the controls are cpu_clk (the app's bus), the memory and the picture writer's second half ui_clk (the host's DDR2 user clock).
// Everything between two clocks is a toggle with stable data or a level that is static while the machine runs (written while the reset is held).
// Memory (D3, D5): neo_mem. The ROM spaces and the RAM images are in the DDR2 where the application put them (five base registers); a late sprite or fix word holds the
// machine (neo_mux.v stall). Picture (D6): neo_pixw, RGB565 into a four buffer ring. Sound: neo_audio. Controls (D7): neo_pad. Freeze (C10): the hold stops the 24 MHz enables
// where they are. The reset: the register block's CTRL.b0 and the application's machine reset; the machine has its own power-on counter after it (neo_core).
`timescale 1ns / 1ps
module neo_top (
    input  wire        sys_clk,                     // the host's C64 clock: only the answers go there
    input  wire        sys_rst,
    input  wire        cpu_clk,                     // the app's bus and the register block (100 MHz)
    input  wire        mclk,                        // audio
    input  wire        ui_clk,                      // the host's DDR2 user clock
    input  wire        ui_rst,
    input  wire        reset_req,                   // the app's machine reset (sys_clk)
    input  wire        halt_req,                    // the app's pause (sys_clk)
    output wire        halted,                      // sys_clk
    output wire        running,                     // out of reset, sys_clk

    input  wire        dma_req_tgl,                 // the host's window into a machine's ROM space: not used here, answered at once with nothing
    output wire        dma_done_tgl,
    output wire [7:0]  dma_rdata,
    output wire        win_port_use,
    output wire [15:0] win_port_addr,
    output wire        win_port_rom,
    output wire        win_port_we,
    output wire [7:0]  win_port_wdata,
    output wire [3:0]  pix_color,
    output wire [9:0]  pix_x,
    output wire [8:0]  pix_y,

    input  wire [4:0]  c64_joy1,
    input  wire [4:0]  c64_joy2,
    input  wire [63:0] c64_keys,

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

    // the host's general DDR2 port e_* (ui_clk)
    output wire [26:0] e_addr,
    output wire [2:0]  e_cmd,
    output wire        e_en,
    input  wire        e_rdy,
    output wire [127:0] e_wdf_data,
    output wire [15:0] e_wdf_mask,
    output wire        e_wdf_wren,
    input  wire        e_rd_valid,
    // the host's real-time DDR2 port c_* (ui_clk)
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
    // ---------------------------------------------------------------- the clock
    wire clk48, locked48;
    neo_clk48 clkgen (.cpu_clk(cpu_clk), .clk48(clk48), .locked(locked48));

    // ---------------------------------------------------------------- registers (cpu_clk)
    wire [7:0]  ctrl, system, mv_done;
    wire [2:0]  dip;
    wire [3:0]  mv_cmd;
    wire [31:0] cp_src, cp_dst, cp_len;
    wire        pad_swap, mv_go_tgl, save_ack_tgl;
    wire [31:0] cfg, fb_base, fb_stride, rom_base, v1_base, v2_base, m1_base, img_base;
    wire [55:0] rtc;
    wire [26:0] p2_mask, c_mask, v1_mask, v2_mask, m_mask;
    wire [6:0]  c_start;
    wire [7:0]  c_dl, s_dl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over, seen_cols, seen_lines;
    wire        ch_hint_tgl, sh_hint_tgl;
    wire [26:3] ch_hint_addr;
    wire [26:1] sh_hint_addr;
    wire [31:0] pf_c_hit, pf_c_pend, pf_c_miss, pf_c_hint, pf_s_hit, pf_s_pend, pf_s_miss, pf_s_hint, pf_p_hit, pf_p_pend, pf_p_miss;
    wire [31:0] st_stall_clk, st_c_late, st_s_late, st_p_reads, st_c_cmds, st_e_cmds, st_va_miss, st_mz_miss;
    wire [7:0]  st_c_lat_max, st_c_max, st_s_max, pck_age;
    wire        mv_done_tgl, mv_busy_u, frame_tgl, sv_dirty, nreset_core, halted48, pw_quiet;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rq_s = 2'b00, hl_s = 2'b00;
    always @(posedge cpu_clk) begin
        rq_s <= {rq_s[0], reset_req | sys_rst};
        hl_s <= {hl_s[0], halt_req};
    end
    wire rst_m = rq_s[1] | ctrl[0];
    reg  hold_q = 1'b0;
    always @(posedge cpu_clk) hold_q <= (hl_s[1] | ctrl[1]) & ~rst_m;       // a reset takes the hold away (the menu's Reset works while frozen)

    // answers from the other clocks, in cpu_clk
    (* ASYNC_REG = "TRUE" *) reg [2:0] md_s = 3'b000, fr_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] mb_s = 2'b00, dt_s = 2'b00, nr_s = 2'b00, h48_s = 2'b00, pq_s = 2'b00, lk_s = 2'b00;
    reg [7:0] done_cnt = 8'd0;
    always @(posedge cpu_clk) begin
        md_s <= {md_s[1:0], mv_done_tgl};
        fr_s <= {fr_s[1:0], frame_tgl};
        mb_s <= {mb_s[0], mv_busy_u};
        dt_s <= {dt_s[0], sv_dirty};
        nr_s <= {nr_s[0], nreset_core};
        h48_s <= {h48_s[0], halted48};
        pq_s <= {pq_s[0], pw_quiet};
        lk_s <= {lk_s[0], locked48};
        if (md_s[2] ^ md_s[1]) done_cnt <= done_cnt + 8'd1;
    end
    wire [7:0] status = {2'b00, lk_s[1], mb_s[1], dt_s[1], nr_s[1], hold_q & h48_s[1], rst_m | ~nr_s[1]};

    wire [15:0] app_pad1, app_pad2;                 // the application's USB pads (T156), cpu_clk
    neo_regs regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal),
        .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(fr_s[2] ^ fr_s[1]), .ev_mover(md_s[2] ^ md_s[1]),
        .ctrl(ctrl), .system(system), .dip(dip), .pad_swap(pad_swap), .pad1(app_pad1), .pad2(app_pad2), .cfg(cfg),
        .fb_base(fb_base), .fb_stride(fb_stride), .rom_base(rom_base), .v1_base(v1_base), .v2_base(v2_base), .m1_base(m1_base), .img_base(img_base),
        .mv_cmd(mv_cmd), .cp_src(cp_src), .cp_dst(cp_dst), .cp_len(cp_len), .mv_go_tgl(mv_go_tgl), .mv_done(done_cnt), .rtc(rtc),
        .p2_mask(p2_mask), .c_mask(c_mask), .v1_mask(v1_mask), .v2_mask(v2_mask), .m_mask(m_mask), .c_start(c_start), .c_dl(c_dl), .s_dl(s_dl), .save_ack_tgl(save_ack_tgl),
        .status(status), .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over), .seen_cols({6'd0, seen_cols[9:0]}), .seen_lines({6'd0, seen_lines[9:0]}),
        .st_stall_clk(st_stall_clk), .st_c_late(st_c_late), .st_s_late(st_s_late), .st_p_reads(st_p_reads),
        .st_c_cmds(st_c_cmds), .st_e_cmds(st_e_cmds), .st_va_miss(st_va_miss), .st_mz_miss(st_mz_miss),
        .st_pf({pf_p_miss, pf_p_pend, pf_p_hit, pf_s_hint, pf_s_miss, pf_s_pend, pf_s_hit, pf_c_hint, pf_c_miss, pf_c_pend, pf_c_hit}),
        .st_c_lat_max(st_c_lat_max), .st_c_max(st_c_max), .st_s_max(st_s_max), .pck_age(pck_age)
    );

    // ---------------------------------------------------------------- into the machine's clock
    (* ASYNC_REG = "TRUE" *) reg [1:0] rs48 = 2'b11, hd48 = 2'b00;
    always @(posedge clk48) begin
        rs48 <= {rs48[0], rst_m | ~locked48};
        hd48 <= {hd48[0], hold_q};
    end
    // MiSTer RTC layout (neogeo.sv hps_io RTC): seconds, minutes, hours (24 h), weekday, day, month, year in BCD fields
    wire [64:0] rtc65 = {1'b0, 8'h40, 5'd0, rtc[26:24], rtc[55:48], 3'd0, rtc[44:40], 2'd0, rtc[37:32], 2'd0, rtc[21:16], 1'b0, rtc[14:8], 1'b0, rtc[6:0]};
    // rtc bytes: 0 seconds, 1 minutes, 2 hours, 3 weekday, 4 day, 5 month, 6 year

    // pad, sound, picture
    wire [15:0] pad0, pad1;
    neo_pad pad (.clk(clk48), .joy1(c64_joy1), .joy2(c64_joy2), .keys(c64_keys), .swap(pad_swap), .app1(app_pad1), .app2(app_pad2), .pad0(pad0), .pad1(pad1));

    wire        ce_pix, hblank, vblank, hsync, vsync;
    wire [7:0]  pix_r, pix_g, pix_b;
    wire signed [15:0] snd_l, snd_r;

    wire        pm_req_tgl, pm_ack_tgl, s_req_tgl, s_ack_tgl, c_req_tgl, c_ack_tgl, va_req_tgl, va_ack_tgl, vb_req_tgl, vb_ack_tgl, mz_req_tgl, mz_ack_tgl;
    wire [26:1] pm_addr, s_addr;
    wire [26:3] c_addr_m;
    wire [24:0] va_addr, vb_addr, mz_addr;
    wire [15:0] pm_data, s_data;
    wire [63:0] c_data;
    wire [7:0]  va_data, vb_data, mz_data;
    wire        bk_req_tgl, bk_req_wr, bk_ack_tgl;
    wire [15:1] bk_req_addr;
    wire [15:0] bk_req_wdata;
    wire [1:0]  bk_req_be;
    wire [127:0] bk_ack_line;
    wire [1:0]  img_sel;
    wire [15:0] img_addr, img_wdata, img_rdata;
    wire        img_we;
    wire [31:0] st_stall_clk_w, st_c_late_w, st_s_late_w, st_p_reads_w;
    wire [7:0]  st_c_max_w, st_s_max_w, pck_age_w;

    neo_core core (
        .CLK_48M(clk48), .reset_in(rs48[1]), .hold_in(hd48[1]),
        .cfg(cfg), .sys_mvs_in(system[0]), .vmode_pal_in(system[1]), .card_absent_in(system[2]), .dip_in(dip),
        .P2ROM_MASK_in(p2_mask), .CROM_MASK_in(c_mask), .V1ROM_MASK_in(v1_mask), .V2ROM_MASK_in(v2_mask), .MROM_MASK_in(m_mask), .CROM_START_in(c_start), .c_deadline_in(c_dl), .s_deadline_in(s_dl),
        .joystick_0(pad0), .joystick_1(pad1), .rtc(rtc65),
        .ce_pix(ce_pix), .pix_r(pix_r), .pix_g(pix_g), .pix_b(pix_b), .hblank(hblank), .vblank(vblank), .hsync(hsync), .vsync(vsync),
        .snd_l(snd_l), .snd_r(snd_r),
        .pm_req_tgl(pm_req_tgl), .pm_addr(pm_addr), .pm_ack_tgl(pm_ack_tgl), .pm_data(pm_data),
        .s_req_tgl(s_req_tgl), .s_addr(s_addr), .s_ack_tgl(s_ack_tgl), .s_data(s_data),
        .c_req_tgl(c_req_tgl), .c_addr(c_addr_m), .c_ack_tgl(c_ack_tgl), .c_data(c_data),
        .va_req_tgl(va_req_tgl), .va_addr(va_addr), .va_ack_tgl(va_ack_tgl), .va_data(va_data),
        .vb_req_tgl(vb_req_tgl), .vb_addr(vb_addr), .vb_ack_tgl(vb_ack_tgl), .vb_data(vb_data),
        .mz_req_tgl(mz_req_tgl), .mz_addr(mz_addr), .mz_ack_tgl(mz_ack_tgl), .mz_data(mz_data),
        .ch_hint_tgl(ch_hint_tgl), .ch_hint_addr(ch_hint_addr), .sh_hint_tgl(sh_hint_tgl), .sh_hint_addr(sh_hint_addr),
        .st_stall_clk(st_stall_clk_w), .st_c_late(st_c_late_w), .st_s_late(st_s_late_w), .st_p_reads(st_p_reads_w), .st_c_max(st_c_max_w), .st_s_max(st_s_max_w),
        .bk_req_tgl(bk_req_tgl), .bk_req_wr(bk_req_wr), .bk_req_addr(bk_req_addr), .bk_req_wdata(bk_req_wdata), .bk_req_be(bk_req_be),
        .bk_ack_tgl(bk_ack_tgl), .bk_ack_line(bk_ack_line), .halted_o(halted48),
        .img_clk(ui_clk), .img_sel(img_sel), .img_addr(img_addr), .img_wdata(img_wdata), .img_we(img_we), .img_rdata(img_rdata),
        .sv_dirty(sv_dirty), .sv_dirty_clr_tgl(save_ack_tgl),
        .running(nreset_core), .dbg_pck_age(pck_age_w)
    );
    assign st_stall_clk = st_stall_clk_w; assign st_c_late = st_c_late_w; assign st_s_late = st_s_late_w; assign st_p_reads = st_p_reads_w;
    assign st_c_max = st_c_max_w; assign st_s_max = st_s_max_w; assign pck_age = pck_age_w;

    // picture
    wire        pw_en, pw_rdy;
    wire [26:0] pw_addr;
    wire [127:0] pw_data;
    wire [15:0] fb_over_w;
    neo_pixw pixw (
        .sys_clk(clk48), .sys_rst(rs48[1]), .dotst(ce_pix), .rgb565({pix_r[7:3], pix_g[7:2], pix_b[7:3]}), .hblank(hblank), .vblank(vblank),
        .quiet(pw_quiet), .frame_tgl(frame_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over_w), .seen_cols(seen_cols[9:0]), .seen_lines(seen_lines[9:0]),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .pw_en(pw_en), .pw_addr(pw_addr), .pw_data(pw_data), .pw_rdy(pw_rdy)
    );
    assign fb_over = fb_over_w;
    assign seen_cols[15:10] = 6'd0;
    assign seen_lines[15:10] = 6'd0;

    neo_audio aud (.clk(clk48), .l_in(snd_l), .r_in(snd_r), .mute(hd48[1] | rs48[1]), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // memory
    neo_mem mem (
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .rom_base(rom_base), .v1_base(v1_base), .v2_base(v2_base), .m1_base(m1_base), .img_base(img_base),
        .mv_go_tgl(mv_go_tgl), .mv_cmd(mv_cmd), .cp_src(cp_src), .cp_dst(cp_dst), .cp_len(cp_len), .mv_done_tgl(mv_done_tgl), .mv_busy(mv_busy_u),
        .pm_req_tgl(pm_req_tgl), .pm_addr(pm_addr), .pm_ack_tgl(pm_ack_tgl), .pm_data(pm_data),
        .s_req_tgl(s_req_tgl), .s_addr(s_addr), .s_ack_tgl(s_ack_tgl), .s_data(s_data),
        .c_req_tgl(c_req_tgl), .c_addr(c_addr_m), .c_ack_tgl(c_ack_tgl), .c_data(c_data),
        .va_req_tgl(va_req_tgl), .va_addr(va_addr), .va_ack_tgl(va_ack_tgl), .va_data(va_data),
        .vb_req_tgl(vb_req_tgl), .vb_addr(vb_addr), .vb_ack_tgl(vb_ack_tgl), .vb_data(vb_data),
        .mz_req_tgl(mz_req_tgl), .mz_addr(mz_addr), .mz_ack_tgl(mz_ack_tgl), .mz_data(mz_data),
        .img_sel(img_sel), .img_addr(img_addr), .img_wdata(img_wdata), .img_we(img_we), .img_rdata(img_rdata),
        .bk_req_tgl(bk_req_tgl), .bk_wr(bk_req_wr), .bk_addr(bk_req_addr), .bk_wdata(bk_req_wdata), .bk_be(bk_req_be), .bk_ack_tgl(bk_ack_tgl), .bk_line(bk_ack_line),
        .pw_en(pw_en), .pw_addr(pw_addr), .pw_data(pw_data), .pw_rdy(pw_rdy),
        .c_addr_o(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren), .e_rd_valid(e_rd_valid),
        .st_c_cmds(st_c_cmds), .st_e_cmds(st_e_cmds), .st_va_miss(st_va_miss), .st_mz_miss(st_mz_miss), .st_c_lat_max(st_c_lat_max),
        .ch_hint_tgl(ch_hint_tgl), .ch_hint_addr(ch_hint_addr), .sh_hint_tgl(sh_hint_tgl), .sh_hint_addr(sh_hint_addr), .inval(rst_m | hold_q),
        .st_c_hit(pf_c_hit), .st_c_pend(pf_c_pend), .st_c_miss(pf_c_miss), .st_c_hint(pf_c_hint), .st_s_hit(pf_s_hit), .st_s_pend(pf_s_pend), .st_s_miss(pf_s_miss), .st_s_hint(pf_s_hint),
        .st_p_hit(pf_p_hit), .st_p_pend(pf_p_pend), .st_p_miss(pf_p_miss)
    );

    // ---------------------------------------------------------------- the answers back in sys_clk
    // halted: the machine is held (Freeze), the picture writer has nothing on its way to the DDR2 and no image move is open
    (* ASYNC_REG = "TRUE" *) reg [1:0] ho_s = 2'b00, ru_s = 2'b00;
    always @(posedge sys_clk) begin
        ho_s <= {ho_s[0], hold_q & h48_s[1] & pq_s[1] & ~mb_s[1]};
        ru_s <= {ru_s[0], nr_s[1] & ~rst_m};
    end
    assign halted = ho_s[1];
    assign running = ru_s[1];

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

    // ---------------------------------------------------------------- JTAG proof of life (trxdbg "trx 0 ..."), words:
    //  0 "NEO_"  1 status  2 frames (the picture writer's sequence)  3 stall clocks  4 late sprite words  5 late fix words  6 68k reads  7 {c_lat_max, c_max, s_max, pck_age}
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h5F4F454E;
            4'h1: jd = {24'd0, status};
            4'h2: jd = {16'd0, 8'd0, fb_seq};
            4'h3: jd = st_stall_clk;
            4'h4: jd = st_c_late;
            4'h5: jd = st_s_late;
            4'h6: jd = st_p_reads;
            4'h7: jd = {st_c_lat_max, st_c_max, st_s_max, pck_age};
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
