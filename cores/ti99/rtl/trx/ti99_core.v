// TI-99/4A core: Mega99's mainboard (zeldin/Mega99, LGPL-3.0; TMS9900, TMS9901, TMS9918, TMS9919, GROMs, console ROM, 32K RAM card, see rtl/NOTICE) behind the
// host's layer-2 socket. This entity is what cores/ti99/layer2_ti99.v wraps. Written for this project.
//   ti99_win   the app's DMA window as a wishbone master (ROMs, GROMs, cartridge, DSRs)
//   ti99_keys  the C64 keyboard and joysticks into the TI matrix
//   ti99_pixw  the TMS9918 picture into the host's frame buffer in the DDR2
//   ti99_regs  the register block at the core window
// Everything runs in sys_clk (the host's System Mode clock, 31.5-32.8 MHz) unless noted; the machine's clock enables come from clkgen_nco.v (the
// patched mainboard's clkgen), which follows the System Mode (vmode_sys) and stops the CPU side while the machine is held.
`timescale 1ns / 1ps
module ti99_core (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        cpu_clk,                 // the app's bus (100 MHz)
    input  wire        mclk,                    // audio
    input  wire [1:0]  vmode,                   // the host's System Mode
    input  wire        reset_req,               // the app's machine reset (sys_clk)
    input  wire        halt_req,                // the app's pause (sys_clk)
    output wire        halted,
    output wire        running,

    input  wire [63:0] keys,
    input  wire [4:0]  joy1,
    input  wire [4:0]  joy2,
    input  wire        restore,

    input  wire        dma_req_tgl,
    input  wire [15:0] dma_addr,
    input  wire        dma_we,
    input  wire [7:0]  dma_wdata,
    output wire        dma_done_tgl,
    output wire [7:0]  dma_rdata,

    output wire [3:0]  pix_color,               // the idle raster the host's scaler locks to
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

    input  wire        ui_clk,
    input  wire        ui_rst,
    output wire [26:0] e_addr,
    output wire [2:0]  e_cmd,
    output wire        e_en,
    input  wire        e_rdy,
    output wire [127:0] e_wdf_data,
    output wire [15:0] e_wdf_mask,
    output wire        e_wdf_wren
);
    // ---------------------------------------------------------------- the app's settings into sys_clk
    wire [7:0] ctrl, cfg, wbhi, wbadd;
    wire [31:0] fb_base, fb_stride;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ctrl_s1 = 8'h00, ctrl_s2 = 8'h00, cfg_s1 = 8'h04, cfg_s2 = 8'h04;
    always @(posedge sys_clk) begin
        ctrl_s1 <= ctrl; ctrl_s2 <= ctrl_s1;
        cfg_s1  <= cfg;  cfg_s2  <= cfg_s1;
    end

    reg rst_q = 1'b1;
    always @(posedge sys_clk) rst_q <= sys_rst | reset_req | ctrl_s2[0];
    wire hold = halt_req | ctrl_s2[1];
    reg  held_q = 1'b0;
    always @(posedge sys_clk) held_q <= hold;
    assign halted  = held_q;
    assign running = ~rst_q;

    // ---------------------------------------------------------------- the app's window: the mainboard's wishbone slave
    wire [23:0] m_adr; wire [7:0] m_dat; wire m_we, m_stb, m_cyc, w_busy;
    wire [7:0] s_dat; wire s_ack;
    ti99_win win (
        .clk(sys_clk), .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .wbhi(wbhi), .wbadd(wbadd),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata),
        .m_adr(m_adr), .m_dat(m_dat), .m_we(m_we), .m_stb(m_stb), .m_cyc(m_cyc), .s_dat(s_dat), .s_ack(s_ack), .busy(w_busy)
    );

    // ---------------------------------------------------------------- keyboard and joysticks
    wire [0:47] key_state;
    wire [0:4]  tj1, tj2;
    ti99_keys kb (.keys(keys), .restore(restore), .joy1(joy1), .joy2(joy2), .key_state(key_state), .tj1(tj1), .tj2(tj2));

    // ---------------------------------------------------------------- the machine
    wire        vdp_clk_en, vdp_hsync, vdp_vsync, vdp_cburst, vdp_color_en, vdp_extvideo;
    wire [0:3]  vdp_color;
    wire [0:15] audio_out;
    wire        sys_reset_o, vga_clk_en, overlay_clk_en, clk_3mhz_en;
    wire        cs1, cs2, audio_gate, mag_out;
    wire [1:3]  drive_activity;
    wire        tipi_reset, tipi_din;
    wire [0:15] dbg_pc, dbg_st, dbg_wp, dbg_ir, dbg_ga;
    wire [0:13] dbg_va;
    wire [0:20] dma_adr_o; wire [0:31] dma_dat_o; wire dma_we_o, dma_stb_o, dma_cyc_o; wire [0:3] dma_sel_o;
    wire [0:7]  wb_dat_o_w; wire wb_ack_o_w;

    mainboard #(
        .vdp_clk_multiplier(10), .cpu_clk_multiplier(36),          // accepted and ignored by clkgen_nco.v
        .INCLUDE_PCODE(0), .USE_GROM_DMA(0), .CROM_BANKS(3), .INCLUDE_VSP(0), .INCLUDE_TIPI(0), .NUM_GROMS(6)
    ) tib (
        .clk(sys_clk), .ext_reset(rst_q), .clk_mode(vmode), .clk_hold(hold), .sys_reset(sys_reset_o),
        .reset_9900(1'b0), .reset_9901(1'b0), .reset_9918(1'b0), .reset_9919(1'b0), .reset_5200(1'b0), .reset_peb(1'b0), .reset_grom(1'b0),
        .cpu_turbo(cfg_s2[0]),
        .vdp_clk_en(vdp_clk_en), .vga_clk_en(vga_clk_en), .overlay_clk_en(overlay_clk_en), .clk_3mhz_en(clk_3mhz_en),
        .enable_ram32k(cfg_s2[2]), .enable_fdc(cfg_s2[3]), .enable_vsp(1'b0), .enable_tipi(1'b0), .enable_pcode(1'b0),
        .enable_1kscratch(cfg_s2[4]), .swap_joysticks(cfg_s2[1]), .tipi_crubase(4'd0),
        .vdp_hsync(vdp_hsync), .vdp_vsync(vdp_vsync), .vdp_cburst(vdp_cburst), .vdp_color(vdp_color), .vdp_color_en(vdp_color_en),
        .vdp_extvideo(vdp_extvideo),
        .audio_in(16'd0), .audio_out(audio_out),
        .key_state(key_state), .alpha_state(1'b0), .joy1(tj1), .joy2(tj2),
        .synth_key_state(48'd0), .synth_keys_enabled(1'b0),
        .cs1_cntrl(cs1), .cs2_cntrl(cs2), .audio_gate(audio_gate), .mag_out(mag_out), .drive_activity(drive_activity),
        .tipi_clk(1'b0), .tipi_rt(1'b0), .tipi_le(1'b0), .tipi_reset(tipi_reset), .tipi_dout(1'b0), .tipi_din(tipi_din), .tipi_dc(1'b0),
        .debug_pc(dbg_pc), .debug_st(dbg_st), .debug_wp(dbg_wp), .debug_ir(dbg_ir), .debug_vdp_addr(dbg_va), .debug_grom_addr(dbg_ga),
        .wb_adr_i(m_adr), .wb_dat_i(m_dat), .wb_dat_o(wb_dat_o_w), .wb_we_i(m_we), .wb_sel_i(1'b1), .wb_stb_i(m_stb),
        .wb_ack_o(wb_ack_o_w), .wb_cyc_i(m_cyc),
        .dma_adr_o(dma_adr_o), .dma_dat_o(dma_dat_o), .dma_dat_i(32'd0), .dma_we_o(dma_we_o), .dma_sel_o(dma_sel_o),
        .dma_stb_o(dma_stb_o), .dma_ack_i(1'b0), .dma_cyc_o(dma_cyc_o)
    );
    assign s_dat = wb_dat_o_w;
    assign s_ack = wb_ack_o_w;

    // diagnostic trap: the first time the CPU's PC leaves the console ROM (>= 0x2000) after a reset; the state at that moment stays latched
    reg [15:0] pcp = 16'd0, irq_p = 16'd0, ir_pp = 16'd0;
    reg        trapped = 1'b0;
    reg [15:0] t_ppc = 16'd0, t_pc = 16'd0, t_ir = 16'd0, t_irp = 16'd0, t_st = 16'd0, t_ga = 16'd0;
    always @(posedge sys_clk) begin
        pcp <= dbg_pc;
        if (dbg_pc != pcp) begin irq_p <= dbg_ir; ir_pp <= irq_p; end
        if (rst_q) trapped <= 1'b0;
        else if (!trapped && dbg_pc != pcp && dbg_pc >= 16'h2000) begin
            trapped <= 1'b1;
            t_ppc <= pcp; t_pc <= dbg_pc; t_ir <= dbg_ir; t_irp <= ir_pp; t_st <= dbg_st; t_ga <= dbg_ga;
        end
    end

    // ---------------------------------------------------------------- the picture
    // hblank is "no colour enable"; vblank starts with the VDP's vertical sync and ends with the first line that has colour (either sync polarity works)
    wire [3:0] colour = vdp_color;
    reg        sv_d = 1'b0, vb_reg = 1'b1;
    always @(posedge sys_clk) if (vdp_clk_en) begin
        sv_d <= vdp_vsync;
        if (vdp_vsync && !sv_d) vb_reg <= 1'b1;
        else if (vdp_color_en)  vb_reg <= 1'b0;
    end
    wire hblank = ~vdp_color_en;
    wire vblank = vb_reg & ~vdp_color_en;

    wire        frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    ti99_pixw #(.OVERLAY(0)) pixw (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .ce_pix(vdp_clk_en), .colour(colour), .hblank(hblank), .vblank(vblank),
        .dbgw({dbg_pc, t_ppc, t_pc, t_ir, t_irp, t_st, t_ga, trapped, 5'b00000, held_q, rst_q, frames[7:0]}),
        .frame_tgl(frame_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over),
        .fb_base(fb_base), .fb_stride(fb_stride), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );

    // the frame-done toggle into cpu_clk for the interrupt cause; the frame counter
    (* ASYNC_REG = "TRUE" *) reg [2:0] ft_s = 3'b000;
    always @(posedge cpu_clk) ft_s <= {ft_s[1:0], frame_tgl};
    wire ev_frame = ft_s[2] ^ ft_s[1];
    reg  ft_d = 1'b0; reg [15:0] frames = 16'd0;
    always @(posedge sys_clk) begin ft_d <= frame_tgl; if (ft_d != frame_tgl) frames <= frames + 16'd1; end

    // the idle raster the host's scaler locks to: 504 dots of 4 clocks, 312 lines, colour 0, free running (as vc20_video / c264_core)
    reg [1:0] dph = 2'd0; reg [8:0] dx = 9'd0, dy = 9'd0;
    always @(posedge sys_clk) begin
        dph <= dph + 2'd1;
        if (dph == 2'd3) begin
            if (dx == 9'd503) begin dx <= 9'd0; dy <= (dy == 9'd311) ? 9'd0 : dy + 9'd1; end
            else dx <= dx + 9'd1;
        end
    end
    assign pix_color = 4'h0;
    assign pix_x = {1'b0, dx};
    assign pix_y = dy;

    // ---------------------------------------------------------------- audio: the machine's 16 bit sample averaged over 656 sys_clk (48 kHz), handed to mclk by a toggle
    reg signed [25:0] aacc = 26'sd0, aavg = 26'sd0;
    reg [9:0] acnt = 10'd0; reg atgl = 1'b0;
    always @(posedge sys_clk) begin
        if (acnt == 10'd655) begin
            acnt <= 10'd0;
            aavg <= hold ? 26'sd0 : aacc + $signed(audio_out);
            aacc <= 26'sd0;
            atgl <= ~atgl;
        end else begin
            acnt <= acnt + 10'd1;
            aacc <= aacc + $signed(audio_out);
        end
    end
    (* ASYNC_REG = "TRUE" *) reg [2:0] at_s = 3'b000;
    reg signed [23:0] smp = 24'sd0;
    always @(posedge mclk) begin
        at_s <= {at_s[1:0], atgl};
        if (at_s[2] != at_s[1]) smp <= aavg >>> 2;                  // +-32768 * 656 / 4 = +-5.4 M of +-8.4 M
    end
    assign audio_l = smp;
    assign audio_r = smp;

    // ---------------------------------------------------------------- registers and the JTAG words
    wire [7:0] status = {6'b000000, held_q, rst_q};
    ti99_regs regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(ctrl), .cfg(cfg), .wbhi(wbhi), .wbadd(wbadd), .fb_base(fb_base), .fb_stride(fb_stride),
        .status(status), .frames(frames), .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over),
        .dbg_pc(dbg_pc), .dbg_st(dbg_st), .dbg_wp(dbg_wp), .dbg_ga(dbg_ga), .dbg_va({2'b00, dbg_va})
    );

    // words (trxdbg trx 0 / 4 / 8 ...): 0 "TI99"  1 status  2 frames  3 CPU program counter  4 CPU status register  5 workspace pointer
    reg [31:0] jd;
    always @* case (dbg_addr[3:0])
        4'h0: jd = 32'h39395449;
        4'h1: jd = {24'd0, 1'b0, w_busy, halt_req, sys_rst, ctrl_s2[0], reset_req, held_q, rst_q};
        4'h2: jd = {16'd0, frames};
        4'h3: jd = {16'd0, dbg_pc};
        4'h4: jd = {16'd0, dbg_st};
        4'h5: jd = {16'd0, dbg_wp};
        default: jd = 32'd0;
    endcase
    assign j_rdata  = jd;
    assign j_rvalid = 1'b1;
endmodule
