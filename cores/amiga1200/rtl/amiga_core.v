// T85: the Amiga core (Minimig OCS/ECS with the 68000 of fx68k, from NanoMig: cores/amiga/rtl/src,
// see NOTICE) behind the host's layer-2 socket. This module is what cores/amiga/layer2_amiga.v wraps; it has
// no C64 part in it. Written for this project.
//
// Clocks: the chipset runs on sys_clk (the host's C64 clock, 31.5278 MHz PAL) with the clock enable ce28
// (9 of 10 clocks = 28.37516 MHz exactly, minus the elastic stall of the memory), the memory interfaces in
// ui_clk (the DDR2 ports c_* and e_*), the application's registers in cpu_clk, audio in mclk.
// Memory (DDR2 window, byte offsets from the window base): see amiga_mem.v, amiga_fb.v, amiga_sdc.v.
`timescale 1ns / 1ps
module amiga_core #(
    parameter integer FB_BUFS = 3,
    // What the machine is before the application has said anything. The application overwrites all
    // of it at its first commit (cfg_tgl), so these only decide what a core does with no app
    // settings - which is how the A1200 core (cores/amiga1200) differs from this one: same RTL,
    // AGA on and 2 MB chip RAM from power-up. chipset_config bit 4 is AGA (minimig.v), bit 1 ntsc,
    // bit 2 turbo chipram; memory_config[1:0] is the chip RAM size (0 = 512 KB .. 3 = 2 MB) and
    // [3:2] the slow RAM (nanomig.v).
    parameter integer CHS_INIT = 0,
    parameter integer MEM_INIT = 0,
    // the HDF size registers carry 512-byte sectors (T102 D1): the A1200 core, whose application is built for it
    parameter integer HDF_SECT = 0
) (
    input  wire         sys_clk,
    input  wire         sys_rst,
    input  wire         cpu_clk,
    input  wire         mclk,
    input  wire         ui_clk,
    input  wire         ui_rst,
    input  wire         reset_req,                  // the application's machine reset (sys_clk)
    output wire         running,                    // out of reset (case LED 1)
    input  wire         pause_req,                  // Freeze: hold the whole machine (sys_clk)
    output reg          paused = 1'b0,              // the machine is held and every DDR2 access it began is done (sys_clk)

    input  wire [63:0]  keys,
    input  wire [4:0]   joy1,
    input  wire [4:0]   joy2,
    input  wire         restore,

    input  wire         dma_req_tgl,
    output reg          dma_done_tgl = 1'b0,
    output wire [7:0]   dma_rdata,

    // the DDR2 ports (ui_clk): c_* real time, e_* general
    output wire [26:0]  c_addr,
    output wire [2:0]   c_cmd,
    output wire         c_en,
    input  wire         c_rdy,
    output wire [127:0] c_wdf_data,
    output wire [15:0]  c_wdf_mask,
    output wire         c_wdf_wren,
    input  wire         c_wdf_rdy,
    input  wire [127:0] c_rd_data,
    input  wire         c_rd_valid,
    output wire [26:0]  e_addr,
    output wire [2:0]   e_cmd,
    output wire         e_en,
    input  wire         e_rdy,
    output wire [127:0] e_wdf_data,
    output wire [15:0]  e_wdf_mask,
    output wire         e_wdf_wren,
    input  wire         e_rd_valid,

    // the picture the scaler locks to (a dummy VIC-II raster, colour 0)
    output reg  [3:0]   pix_color = 4'd0,
    output wire [9:0]   pix_x,
    output wire [8:0]   pix_y,
    input  wire         audio_sample_en,            // mclk, once per host audio frame (48 kHz)
    output wire signed [23:0] audio_l,
    output wire signed [23:0] audio_r,

    input  wire         cw_we,
    input  wire         cw_re,
    input  wire [16:0]  cw_addr,
    input  wire [7:0]   cw_wdata,
    output wire [7:0]   cw_rdata,
    output wire         cw_ack,
    output wire         cw_irq,
    output wire         px_we,
    output wire         px_re,
    input  wire [7:0]   px_rdata,
    input  wire         px_ev_vsync,
    input  wire         px_ev_flip,
    input  wire [31:0]  build,
    input  wire [63:0]  ddr_cal,
    output wire         drv_led,                    // T159 D2: a drive motor runs, or the hard disk is accessed (sys_clk)
    output wire         drv_hd,                     // T159 D4: the hard disk access alone (sys_clk)
    output wire         frame_done_o                // T188 D3: a toggle (sys_clk) when a frame is complete in the DDR2 (amiga_fb frame_done_tgl); the host's output follows it
);
    parameter [26:0] BASE = 27'h0800000;           // DDR2 byte 0x1000000 (16-bit word address)

    // ---------------------------------------------------------------- registers (cpu_clk) and their sync
    wire [7:0] r_ctrl, r_mem, r_fast, r_cpu, r_chipset, r_floppy, r_ide, r_joy;
    wire r_rtc_en; wire [8:0] r_rtc_ld; wire rtc_loaded_c;
    wire [3:0] r_adf_present, r_adf_tgl; wire [1:0] r_hdf_present, r_hdf_tgl;
    wire [31:0] r_hdf0, r_hdf1;
    wire [7:0] r_mdx, r_mdy; wire [2:0] r_mbtn; wire r_mtgl, r_mbdone, r_ovfclr;
    wire [1:0] r_wb;
    wire dlog_empty; wire [9:0] dlog_head; wire dlog_pop;
    wire fs_tgl, fd_tgl; wire [15:0] fb_frames, fb_over;
    wire [7:0] status;
    wire [31:0] s_reads, s_miss, s_writes, s_pf, s_hold, s_age, s_info, s_gap, s_debt;
    wire [3:0] r_statsel; wire [31:0] stat_val; wire r_cfgtgl;
    wire mb_tgl, mb_we, mb_drv; wire [31:0] mb_lba; wire dlog_ovf;
    wire [10:0] r_padw; wire [1:0] r_padm; wire cd32p2;   // T225: the USB pad for game port 2
    wire [7:0] r_rkd; wire r_rkt, rk_room;            // T141: raw key events of the application into amiga_input
    wire [39:0] diw; wire [32:0] r_fbwin;           // T97 5.10: the display window out of Agnus, the crop window the application writes

    amiga_regs #(.FB_BUFS(FB_BUFS), .FB_BYTE0(32'h04000000), .FB_SHIFT(21), .HDF_SECT(HDF_SECT)) regs (   // T136 D3
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack),
        .irq(cw_irq), .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip),
        .ctrl(r_ctrl), .mem_cfg(r_mem), .fast_cfg(r_fast), .cpu_cfg(r_cpu), .chipset_cfg(r_chipset),
        .floppy_cfg(r_floppy), .ide_cfg(r_ide), .joy_cfg(r_joy), .rtc_en(r_rtc_en), .rtc_ld(r_rtc_ld), .rtc_loaded(rtc_loaded_c), .adf_present(r_adf_present), .adf_mount_tgl(r_adf_tgl),
        .hdf_present(r_hdf_present), .hdf_mount_tgl(r_hdf_tgl), .hdf_size0(r_hdf0), .hdf_size1(r_hdf1),
        .mouse_dx(r_mdx), .mouse_dy(r_mdy), .mouse_btn(r_mbtn), .mouse_tgl(r_mtgl), .rk_data(r_rkd), .rk_tgl(r_rkt), .rk_room(rk_room), .pad_word(r_padw), .pad_mode(r_padm), .mb_done_tgl(r_mbdone),
        .dlog_ovf_clr_tgl(r_ovfclr),
        .status(status), .frame_start_tgl(fs_tgl), .frame_done_tgl(fd_tgl), .frames(fb_frames), .overruns(fb_over),
        .stat_sel(r_statsel), .stat_val(stat_val), .cfg_tgl(r_cfgtgl),
        .mb_req_tgl(mb_tgl), .mb_req_we(mb_we), .mb_req_drv(mb_drv), .mb_req_lba(mb_lba),
        .dlog_empty(dlog_empty), .dlog_head(dlog_head), .dlog_ovf(dlog_ovf), .dlog_pop(dlog_pop), .wb_idx(r_wb),
        .diw(diw), .fbwin(r_fbwin));

    // configuration into sys_clk: one copy of everything at the commit toggle (cfg_tgl), taken after the toggle has crossed;
    // the values were written before the toggle, so they are stable (no wide ASYNC_REG vectors: those pack into slices)
    (* ASYNC_REG = "TRUE" *) reg [2:0] cfg_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] wb_a = 0, wb_b = 0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] mbd_s = 0, ovf_s = 0;
    reg [7:0] ctrl_b = 8'h01, mem_b = MEM_INIT[7:0], fast_b = 0, cpu_b = 0, chs_b = CHS_INIT[7:0], flp_b = 0, ide_b = 0;
    reg [3:0] adfp_b = 0, adft_b = 0;
    reg [1:0] hdfp_b = 0, hdft_b = 0;
    reg [31:0] h0_b = 0, h1_b = 0;
    reg [32:0] win_b = {9'd270, 7'd90, 8'd10, 9'd36};   // the crop window (amiga_fb's fixed one until the application writes)
    always @(posedge sys_clk) begin
        cfg_s <= {cfg_s[1:0], r_cfgtgl};
        if (cfg_s[2] ^ cfg_s[1]) begin
            ctrl_b <= r_ctrl; mem_b <= r_mem; fast_b <= r_fast; cpu_b <= r_cpu; chs_b <= r_chipset; flp_b <= r_floppy;
            ide_b <= r_ide; adfp_b <= r_adf_present; adft_b <= r_adf_tgl; hdfp_b <= r_hdf_present; hdft_b <= r_hdf_tgl;
            h0_b <= r_hdf0; h1_b <= r_hdf1; win_b <= r_fbwin;
        end
        wb_a <= r_wb; wb_b <= wb_a;
        mbd_s <= {mbd_s[1:0], r_mbdone}; ovf_s <= {ovf_s[1:0], r_ovfclr};
    end
    // the counters of the memory, one of them chosen by the application (its select is quasi static)
    // the audio KPIs live in mclk: the chosen one is taken twice in sys_clk and accepted when both agree (the counters change at
    // most once per 512 mclk; a torn read of the fill disagrees with its neighbour and is dropped)
    //   8: {st_over, st_under}   9: {st_fmax, st_fmin} (11 bit each, fill pairs while playing since HOLD)   10: st_fill
    wire [15:0] au_under, au_over; wire [10:0] au_fill, au_fmin, au_fmax;
    reg [31:0] av_a = 32'd0, av_b = 32'd0, av_c = 32'd0;
    always @(posedge sys_clk) begin
        av_a <= (r_statsel == 4'd8) ? {au_over, au_under} : (r_statsel == 4'd9) ? {5'd0, au_fmax, 5'd0, au_fmin} : {21'd0, au_fill};
        av_b <= av_a; if (av_a == av_b) av_c <= av_b;
    end
    reg [31:0] sv_r = 32'd0;
    always @(posedge sys_clk) begin
        case (r_statsel)
        4'd0: sv_r <= s_reads;
        4'd1: sv_r <= s_miss;
        4'd2: sv_r <= s_writes;
        4'd3: sv_r <= s_pf;
        4'd4: sv_r <= s_hold;
        4'd5: sv_r <= s_info;
        4'd6: sv_r <= s_age;
        4'd8: sv_r <= av_c;
        4'd9: sv_r <= av_c;
        4'd10: sv_r <= av_c;
        4'd11: sv_r <= s_debt;
        default: sv_r <= s_gap;
        endcase
    end
    assign stat_val = sv_r;
    // T157: the joystick swap bit of the register block (cpu_clk, quasi static), two flip-flops into sys_clk
    (* ASYNC_REG = "TRUE" *) reg [1:0] joy_swap_s = 2'b00;
    always @(posedge sys_clk) joy_swap_s <= {joy_swap_s[0], r_joy[0]};
    // T191: the clock's setting and time load (cpu_clk values, quasi static: the data bits are written before the toggle) into sys_clk, its loaded flag back
    (* ASYNC_REG = "TRUE" *) reg [10:0] rtc_s1 = 11'd0, rtc_s2 = 11'd0;
    always @(posedge sys_clk) begin rtc_s1 <= {r_rtc_en, r_rtc_ld, 1'b0}; rtc_s2 <= rtc_s1; end
    wire rtc_loaded_s;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rtc_lc = 2'b00;
    always @(posedge cpu_clk) rtc_lc <= {rtc_lc[0], rtc_loaded_s};
    assign rtc_loaded_c = rtc_lc[1];
    wire hold = ctrl_b[0] | reset_req;
    wire video_off = ctrl_b[1];

    // ---------------------------------------------------------------- the chipset
    wire ce28, clk7_en, clk7n_en; wire [1:0] phase;
    wire cpu_nrst, pwr_led, fdd_led, hdd_led, motor_led, motor_hd;
    wire hs, vs; wire [7:0] vr, vg, vb; wire hbl_o, vbl_o, lace_o, field1_o; wire [1:0] hires_o;
    wire [14:0] au_l, au_r;
    wire [15:0] ram_data; wire [23:1] ram_address; wire n_bhe, n_ble, n_we, n_oe, refresh;
    wire fastram_sel; wire [25:1] fastram_addr;   // T138: [25] = the Zorro III board (ramaddr[28] of the CPU wrapper)
    wire fastram_lds, fastram_uds, fastram_wr; wire [15:0] fastram_din;
    wire [15:0] m_dout, p2_dout; wire [47:0] m_dout48, p2_dout48; wire p2_ready; wire m_pend;
    wire [7:0] sdc_mounted, sdc_rd, sdc_wr; wire [63:0] sdc_size; wire [31:0] sdc_sector;
    wire sdc_busy, sdc_done, sdc_bstrobe; wire [8:0] sdc_baddr; wire [7:0] sdc_bin, sdc_bout;
    wire kml; wire [1:0] kmt; wire [7:0] kmd; wire [2:0] mbtn; wire [10:0] joy0; wire [7:0] joyb; wire reset_kbd;
    wire uart_tx;

    // Freeze. The request holds ce28 (amiga_ce) unless the machine is in reset: a reset needs ticks to go through. The
    // units that talk to the DDR2 (amiga_mem, amiga_fb, amiga_sdc) are not tied to ce28 and finish what they began; `paused`
    // rises when the clock enable has been held and all three have been quiet for 32 clocks in a row (the FIFOs' pointers
    // need a few clocks to show on the other side), and falls with the request.
    wire pause_eff = pause_req & ~hold;
    wire q_mem, q_fb, q_sdc;
    reg [4:0] qcnt = 5'd0;
    always @(posedge sys_clk) begin
        if (!pause_eff || ce28 || !(q_mem && q_fb && q_sdc)) qcnt <= 5'd0;
        else if (qcnt != 5'd31) qcnt <= qcnt + 5'd1;
        paused <= pause_eff && (qcnt == 5'd31);
    end

    amiga_ce ce (.clk(sys_clk), .phase(phase), .pend(m_pend), .pause(pause_eff), .ce28(ce28), .st_hold(s_hold), .st_gap_max(s_gap), .st_debt(s_debt));

    // T97 5.9 (one of the changes against ../../amiga/rtl/trx/amiga_core.v, recorded in amiga_core.diff; the others are the display window out of nanomig and the crop window into amiga_fb, 5.10): FAST_CFG 3 (5.5 MB) takes 1.5 MB
    // "from the slow RAM" ($600000..$77FFFF are the fast cards 2 and 3; nanomig.v clears slowram_config for the CPU's own port). The
    // chipset (gary, minimig_sram_bridge) still got the raw memory_config and served the slow RAM at $C00000..$D7FFFF from the very
    // same DDR2 bytes, so the OS saw them twice, as slow and as fast RAM. The slow bits are cleared for the chipset as well then.
    wire [7:0] mem_nm = {mem_b[7:4], mem_b[3:2] & {2{~&fast_b[1:0]}}, mem_b[1:0]};

    nanomig nm (
        .rtc_ld(rtc_s2[9:1]), .rtc_en(rtc_s2[10]), .rtc_loaded(rtc_loaded_s),
        .ce28(ce28), .clk_sys(sys_clk), .reset(hold | reset_kbd), .por(sys_rst),
        .clk7_en(clk7_en), .clk7n_en(clk7n_en), .clk7_phase(phase), .cpu_nrst_out(cpu_nrst),
        .pwr_led(pwr_led), .fdd_led(fdd_led), .hdd_led(hdd_led), .motor_led(motor_led), .motor_hd(motor_hd),
        .hs(hs), .vs(vs), .r(vr), .g(vg), .b(vb),
        .diw_o(diw), .hblank_o(hbl_o), .vblank_o(vbl_o), .lace_o(lace_o), .field1_o(field1_o), .hires_o(hires_o),
        .memory_config(mem_nm), .fastram_config(fast_b[2:0]), .cpu_config(cpu_b[1:0]), .chipset_config(chs_b[5:0]),
        .floppy_config(flp_b[3:0]), .video_config(4'b0000), .turbo_config(3'b000), .ide_config(ide_b[5:0]),
        .audio_left(au_l), .audio_right(au_r),
        .mouse_buttons(mbtn), .kbd_mouse_level(kml), .kbd_mouse_type(kmt), .kbd_mouse_data(kmd),
        .joystick0(joy0), .cd32pad2_en(cd32p2), .joystick1(joyb), .uart_tx(uart_tx), .uart_rx(1'b1),
        .sdc_img_mounted(sdc_mounted), .sdc_img_size(sdc_size), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr),
        .sdc_sector(sdc_sector), .sdc_busy(sdc_busy), .sdc_done(sdc_done), .sdc_byte_in_strobe(sdc_bstrobe),
        .sdc_byte_addr(sdc_baddr), .sdc_byte_in_data(sdc_bin), .sdc_byte_out_data(sdc_bout),
        .ram_data(ram_data), .ramdata_in(m_dout), .chip48(m_dout48), .refresh(refresh), .ram_address(ram_address),
        ._ram_bhe(n_bhe), ._ram_ble(n_ble), ._ram_we(n_we), ._ram_oe(n_oe),
        .fastram_sel(fastram_sel), .fastram_addr(fastram_addr), .fastram_lds(fastram_lds), .fastram_uds(fastram_uds),
        .fastram_dout(p2_dout), .fastram_dout48(p2_dout48), .fastram_din(fastram_din), .fastram_wr(fastram_wr),
        .fastram_ready(p2_ready));

    amiga_mem #(.BASE(BASE)) mem (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .flush(hold), .ce28(ce28), .clk7_en(clk7_en),
        .cs(~n_oe | ~n_we), .we(~n_we), .addr(ram_address[22:1]), .be(~{n_bhe, n_ble}), .din(ram_data),
        .dout(m_dout), .dout48(m_dout48), .pend(m_pend), .quiet(q_mem),
        .p2_sel(fastram_sel), .p2_wr(fastram_wr), .p2_addr(fastram_addr[25:1]), .p2_be(~{fastram_uds, fastram_lds}),
        .p2_din(fastram_din), .p2_dout(p2_dout), .p2_dout48(p2_dout48), .p2_ready(p2_ready),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(c_wdf_rdy),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .st_reads(s_reads), .st_miss(s_miss), .st_writes(s_writes), .st_pf(s_pf), .st_pf_used(), .st_cycles(), .st_age_max(s_age), .st_stall_info(s_info));

    // ---------------------------------------------------------------- the general port: picture and sectors
    wire a_valid, a_ready, b_valid, b_we, b_ready; wire [26:0] a_addr, b_addr; wire [127:0] a_data, b_data, b_rdata;
    amiga_eport eport (
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .a_valid(a_valid), .a_addr(a_addr), .a_data(a_data), .a_ready(a_ready),
        .b_valid(b_valid), .b_we(b_we), .b_addr(b_addr), .b_data(b_data), .b_ready(b_ready), .b_rdata(b_rdata),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(e_wdf_wren), .e_rd_valid(e_rd_valid), .rd_data(c_rd_data));

    // T136 D3: 32 bits per pixel, 3 x 720 x 540 x 4 bytes do not fit the window's 3 MB at +0xD00000: the buffers are at DDR2
    // 0x4000000, 2 MB apart (outside the window, which ends at 0x1FFFFFF; a power of two, amiga_regs takes the buffer from the address bits)
    amiga_fb #(.FB_BASE_W(27'h2000000), .BUF_STRIDE_W(27'h100000)) fb (
        .sys_clk(sys_clk), .sys_rst(sys_rst | hold), .ce28(ce28), .r(vr), .g(vg), .b(vb),
        .hblank(hbl_o), .vblank(vbl_o), .lace(lace_o), .field1(field1_o), .enable(~video_off), .quiet(q_fb), .wb_idx(wb_b),
        .skip_x(win_b[8:0]), .skip_y(win_b[16:9]), .cols_w(win_b[23:17]), .lines(win_b[32:24]),
        .frame_start_tgl(fs_tgl), .frame_done_tgl(fd_tgl), .frames(fb_frames), .overruns(fb_over),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .a_valid(a_valid), .a_addr(a_addr), .a_data(a_data), .a_ready(a_ready));

    // dirty log of floppy sector writes to the application
    wire dirty_wr; wire [9:0] dirty_val; wire dl_full;
    reg dlog_ovf_r = 1'b0;
    always @(posedge sys_clk) begin
        if (dirty_wr && dl_full) dlog_ovf_r <= 1'b1;
        if (ovf_s[2] ^ ovf_s[1]) dlog_ovf_r <= 1'b0;
    end
    assign dlog_ovf = dlog_ovf_r;
    trx_afifo #(.W(10), .AW(4)) dlog (.wclk(sys_clk), .wrst(sys_rst), .wr(dirty_wr), .wdata(dirty_val), .full(dl_full),
        .rclk(cpu_clk), .rrst(1'b0), .rd(dlog_pop), .rdata(dlog_head), .empty(dlog_empty));

    wire [7:0] unused_busy;
    wire sdc_busy_o;
    amiga_sdc #(.WIN_W(BASE), .HDF_SECT(HDF_SECT)) sdc (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .ce28(ce28), .flush(hold | reset_kbd),
        .sdc_img_mounted(sdc_mounted), .sdc_img_size(sdc_size), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr), .sdc_sector(sdc_sector),
        .sdc_busy(sdc_busy), .sdc_done(sdc_done), .sdc_byte_in_strobe(sdc_bstrobe), .sdc_byte_addr(sdc_baddr),
        .sdc_byte_in_data(sdc_bin), .sdc_byte_out_data(sdc_bout),
        .adf_mount_tgl(adft_b), .adf_present(adfp_b), .hdf_mount_tgl(hdft_b), .hdf_present(hdfp_b),
        .hdf_size0(h0_b), .hdf_size1(h1_b), .dirty_wr(dirty_wr), .dirty_val(dirty_val),
        .mb_req_tgl(mb_tgl), .mb_req_we(mb_we), .mb_req_drv(mb_drv), .mb_req_lba(mb_lba), .mb_done_tgl(mbd_s[2]),
        .busy_o(sdc_busy_o), .quiet(q_sdc),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .b_valid(b_valid), .b_we(b_we), .b_addr(b_addr), .b_data(b_data),
        .b_ready(b_ready), .b_rdata(b_rdata));

    // ---------------------------------------------------------------- input, audio
    amiga_input inp (
        .sys_clk(sys_clk), .sys_rst(sys_rst | hold), .ce28(ce28), .keys(keys), .restore(restore), .joy1(joy1), .joy2(joy2), .joy_swap(joy_swap_s[1]),
        .mouse_dx(r_mdx), .mouse_dy(r_mdy), .mouse_btn(r_mbtn), .mouse_tgl(r_mtgl), .rk_data(r_rkd), .rk_tgl(r_rkt), .rk_room(rk_room), .pad_word(r_padw), .pad_mode(r_padm),
        .kbd_mouse_level(kml), .kbd_mouse_type(kmt), .kbd_mouse_data(kmd), .mouse_buttons(mbtn),
        .joystick0(joy0), .cd32pad2_en(cd32p2), .joystick1(joyb), .reset_kbd(reset_kbd));

    amiga_audio aud (.sys_clk(sys_clk), .ce28(ce28), .mclk(mclk), .sample_en(audio_sample_en), .left(au_l), .right(au_r), .mute(pause_eff), .clr(hold),
                     .audio_l(audio_l), .audio_r(audio_r), .st_under(au_under), .st_over(au_over), .st_fill(au_fill), .st_fmin(au_fmin), .st_fmax(au_fmax));

    // ---------------------------------------------------------------- the dummy VIC-II raster for the host's scaler
    // 504 dots of 4 clocks per line, 312 lines: the frame the scaler locks to (vic_scaler.v), colour 0
    reg [1:0] dph = 2'd0; reg [8:0] dx = 9'd0, dy = 9'd0;
    always @(posedge sys_clk) begin
        dph <= dph + 2'd1;
        if (dph == 2'd3) begin
            if (dx == 9'd503) begin dx <= 9'd0; dy <= (dy == 9'd311) ? 9'd0 : dy + 9'd1; end
            else dx <= dx + 9'd1;
        end
        pix_color <= 4'd0;
    end
    assign pix_x = {1'b0, dx};
    assign pix_y = dy;

    // ---------------------------------------------------------------- the app's DMA window: not served (answers at once)
    (* ASYNC_REG = "TRUE" *) reg [2:0] dq = 3'b000;
    always @(posedge sys_clk) begin dq <= {dq[1:0], dma_req_tgl}; if (dq[2] != dq[1]) dma_done_tgl <= ~dma_done_tgl; end
    assign dma_rdata = 8'h00;

    assign status = {1'b0, paused, ~cpu_nrst, hdd_led, fdd_led, pwr_led, |phase | ce28, hold};
    assign running = ~hold;
    assign drv_led = motor_led;
    assign drv_hd = motor_hd;
    assign frame_done_o = fd_tgl;
endmodule
