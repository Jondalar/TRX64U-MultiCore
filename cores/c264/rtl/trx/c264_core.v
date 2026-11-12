// TRX C264 core (T90): the Commodore 264 family (C16, C116, Plus/4) of C16Nano (cores/c264/rtl/src: ted.v, colors_to_rgb.v, the T65
// 6502, Hegedus et al., GPL-3.0-or-later, see NOTICE) behind the host's layer-2 socket. This entity is what
// cores/c264/layer2_c264.v wraps; it has no C64 part in it. The machine follows c16.v of C16Nano (the bus, the reset, the
// joysticks on the keyboard lines, the IEC lines on the CPU port) with every process on one clock enable.
//
// Clock: sys_clk is the host's C64 clock (31.527778 MHz PAL). The TED clock is sys_clk x 9/10 exactly (c264_nco): ted_en is that
// enable. Every process of the machine runs on ted_en (ted.v takes it as clk_en), so the pause is one gate: no ted_en, nothing moves.
// Memory: RAM and the six ROM arrays are c264_mem; the machine's bus selects them as the Nano's top does (cs0/cs1 of the TED,
// the ROM bank register at $FDD0-$FDDF in the Plus/4 model, KERNAL always at $FC00-$FCFF). The memories answer one clock after
// the address on the ungated sys_clk, which is the Nano's block RAM timing (address changes only at an enabled edge).
// Picture: the TED's 121 colours are converted to RGB565 and written into a DDR2 frame buffer for the host's pixel layer
// (c264_pixw); the host's VIC-II raster input gets a dummy raster of the same size (the scaler locks to it; it never stops).
`timescale 1ns / 1ps
module c264_core #(
    parameter integer ID_TRX2 = 0
) (
    input  wire        sys_clk,                     // the machine (the host's C64 clock)
    input  wire        sys_rst,
    input  wire        cpu_clk,                     // the app's bus (100 MHz)
    input  wire        mclk,                        // audio
    input  wire        ui_clk,                      // the host's DDR2 user clock
    input  wire        ui_rst,
    input  wire        reset_req,                   // the app's machine reset (sys_clk)
    input  wire        halt_req,                    // the app's pause (sys_clk)
    output wire        halted,
    output wire        running,                     // out of reset (case LED 1)

    input  wire [63:0] keys,
    input  wire [4:0]  joy1,
    input  wire [4:0]  joy2,
    input  wire        restore,

    input  wire        iec_atn_i,                   // the bus levels, 1 = high
    input  wire        iec_clk_i,
    input  wire        iec_data_i,
    output wire        iec_atn_o,                   // 0 = the machine pulls the line low
    output wire        iec_clk_o,
    output wire        iec_data_o,

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
    input  wire        port_req,
    input  wire [15:0] port_addr,
    input  wire        port_rom,
    input  wire        port_we,
    input  wire [7:0]  port_wdata,
    output wire [7:0]  ram_rdata,

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
    output wire        e_wdf_wren
);
    // ---------------------------------------------------------------- the app's settings (cpu_clk) into sys_clk
    wire [7:0]  ctrl;
    wire        r_model, r_ram64, r_swap;
    wire [2:0]  r_sel;
    wire [1:0]  r_cart;
    wire [31:0] fb_base, fb_stride;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ctrl_s1 = 8'd0, ctrl_s2 = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [7:0] set_s1 = 8'd0, set_s2 = 8'd0;
    always @(posedge sys_clk) begin
        ctrl_s1 <= ctrl;
        ctrl_s2 <= ctrl_s1;
        set_s1 <= {r_cart, r_sel, r_swap, r_ram64, r_model};
        set_s2 <= set_s1;
    end
    wire       model_s   = set_s2[0];
    wire       ram64_s   = set_s2[1];
    wire       swap_s    = set_s2[2];
    wire [2:0] rom_sel_s = set_s2[5:3];
    wire [1:0] cart_s    = set_s2[7:6];

    // ---------------------------------------------------------------- reset and pause
    wire       rst_m = sys_rst | reset_req | ctrl_s2[0];
    reg        rst_q = 1'b1;
    reg        sreset = 1'b1;                       // the machine's reset: RESET stretched by 1,000,000 TED clocks (c16.v)
    reg [19:0] resetcounter = 20'd0;
    reg        model_l = 1'b0, size64_l = 1'b0;     // MODEL and RAM_SIZE are taken in reset
    wire       hold_req = halt_req | ctrl_s2[1];
    wire       hold_eff = hold_req & ~sreset & ~rst_q;
    wire       ted_en;
    reg        held = 1'b0;
    c264_nco nco (.clk(sys_clk), .hold(hold_eff), .en(ted_en));

    always @(posedge sys_clk) begin
        rst_q <= rst_m;
        held <= hold_eff;
        if (rst_m) begin
            model_l <= model_s;
            size64_l <= ram64_s;
        end
        if (rst_m) begin
            resetcounter <= 20'd0;
            sreset <= 1'b1;
        end else if (ted_en) begin
            if (resetcounter == 20'd1000000) sreset <= 1'b0;
            else begin
                resetcounter <= resetcounter + 20'd1;
                sreset <= 1'b1;
            end
        end
    end
    assign halted = held;
    assign running = ~sreset;

    // ---------------------------------------------------------------- the TED machine (c16.v)
    wire [15:0] c16_addr, ted_addr, cpu_addr;
    wire [7:0]  c16_data, ted_data, cpu_data, port_in, port_out, keyport_data;
    wire [6:0]  c16_color;
    wire        cpuenable, mux, ras, cas, cs0, cs1, aec, rdy, rnw, irq_n;
    wire        hblank, vblank, ted_pal;
    wire signed [15:0] digi_sound;
    reg  [7:0]  c16_datalatch = 8'hFF;
    reg  [15:0] c16_addrlatch = 16'hFFFF;
    wire [7:0]  kbus, kbus_kbd, c16_din;
    wire [63:0] k264;

    // joysticks on the keyboard lines (c16.v): a port is read when the TED's last write to $FF08 selects it
    wire [4:0] jA = swap_s ? joy2 : joy1;
    wire [4:0] jB = swap_s ? joy1 : joy2;
    wire [4:0] joy0_sel = (!c16_data[2]) ? ~{jA[4], jA[3], jA[2], jA[1], jA[0]} : 5'h1F;   // up, down, left, right on k[0..3]
    wire [4:0] joy1_sel = (!c16_data[1]) ? ~{jB[4], jB[3], jB[2], jB[1], jB[0]} : 5'h1F;
    assign kbus[3:0] = kbus_kbd[3:0] & joy0_sel[3:0] & joy1_sel[3:0];
    assign kbus[5:4] = kbus_kbd[5:4];
    assign kbus[6]   = kbus_kbd[6] & joy0_sel[4];
    assign kbus[7]   = kbus_kbd[7] & joy1_sel[4];

    c264_cpu cpu (
        .clk(sys_clk), .reset(sreset), .enable(cpuenable & ted_en), .irq_n(irq_n),
        .data_in(c16_data), .data_out(cpu_data), .address(cpu_addr), .rw(rnw),
        .port_in(port_in), .port_out(port_out), .rdy(rdy), .aec(aec)
    );

    ted mos8360 (
        .clk(sys_clk), .clk_en(ted_en), .reset(sreset),
        .addr_in(c16_addr), .addr_out(ted_addr), .data_in(c16_data), .data_out(ted_data),
        .cpuclk(), .rw(rnw), .color(c16_color), .csync(), .hsync(), .vsync(),
        .hblank(hblank), .vblank(vblank), .irq(irq_n), .ba(rdy), .mux(mux), .ras(ras), .cas(cas),
        .cs0(cs0), .cs1(cs1), .aec(aec), .k(kbus), .snd(), .digi_sound(digi_sound), .pal(ted_pal),
        .cpuenable(cpuenable), .burst(), .even(), .data_oe(), .refresh_o()
    );

    // the keyboard: the host's C64 matrix, translated; the 6529 at $FD3x holds the row select (reads give back what was written)
    c264_keys keyx (.keys(keys), .k264(k264));
    reg [7:0] keyrow = 8'd0;
    wire keyboardio = (c16_addr[15:4] == 12'hFD3);
    assign keyport_data = (keyboardio & rnw) ? keyrow : 8'hFF;
    always @(posedge sys_clk) begin
        if (ted_en & keyboardio) begin
            if (rnw) keyrow <= keyrow;
            else keyrow <= c16_data;
        end
    end
    genvar gc;
    generate
        for (gc = 0; gc < 8; gc = gc + 1) begin : kcol
            // column gc reads low when a key of it is down in a row whose select bit is low
            assign kbus_kbd[gc] = ~|(k264[gc * 8 + 7 -: 8] & ~keyrow);
        end
    endgenerate

    // the bus: wired-AND of everybody, with the TED's address/data latch for the multiplexed phases
    wire openbus_sel = (cpu_addr[15:5] == {8'hFD, 3'b111});          // $FDE0-$FDFF
    wire [7:0] openbus_data = openbus_sel ? c16_datalatch : 8'hFF;
    assign c16_addr = (~mux) ? c16_addrlatch : (cpu_addr & ted_addr);
    assign c16_data = (mux) ? c16_datalatch : (cpu_data & ted_data & c16_din & keyport_data & openbus_data);
    always @(posedge sys_clk) begin
        if (ted_en) begin
            c16_datalatch <= c16_data;
            c16_addrlatch <= c16_addr;
        end
    end

    // the 6510-style port: IEC on bits 0-2 (out, inverted) and 6/7 (in), the rest feeds back
    reg [1:0] ic_s = 2'b11, id_s = 2'b11;
    always @(posedge sys_clk) begin ic_s <= {ic_s[0], iec_clk_i}; id_s <= {id_s[0], iec_data_i}; end
    assign {port_in[5], port_in[3:0]} = {port_out[5], port_out[3:0]};
    assign port_in[4] = 1'b1;                                         // no cassette sense
    assign port_in[6] = ~port_out[1] & ic_s[1];
    assign port_in[7] = ~port_out[0] & id_s[1];
    assign iec_data_o = ~port_out[0];
    assign iec_clk_o  = ~port_out[1];
    assign iec_atn_o  = ~port_out[2];

    // ---------------------------------------------------------------- memory
    reg  [1:0] roml = 2'd0, romh = 2'd0;
    reg        old_mux = 1'b1, old_cas = 1'b1, ram_we = 1'b0;
    always @(posedge sys_clk) begin
        if (rst_q) begin
            romh <= 2'd0; roml <= 2'd0;
        end else if (ted_en) begin
            if (model_l && old_mux && !mux && !rnw && c16_addr[15:4] == 12'hFDD) begin
                romh <= c16_addr[3:2];
                roml <= c16_addr[1:0];
            end
        end
        if (ted_en) begin
            old_mux <= mux;
            old_cas <= cas;
            ram_we <= old_cas & ~cas & ~rnw;
        end
    end

    wire [7:0] ram_q, kern_q, basic_q, flo_q, fhi_q, clo_q, chi_q;
    wire       w_wr, w_hit, w_rom;
    wire [2:0] w_sel;
    wire [15:0] w_ai;
    wire [7:0] w_di, w_do;
    c264_mem mem (
        .clk(sys_clk), .m_addr(c16_addr), .m_wdata(cpu_data), .m_we(ram_we & ted_en), .size64(size64_l),
        .ram_q(ram_q), .kern_q(kern_q), .basic_q(basic_q), .flo_q(flo_q), .fhi_q(fhi_q), .clo_q(clo_q), .chi_q(chi_q),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di), .w_do(w_do)
    );

    wire kern = (c16_addr[15:8] == 8'hFC);
    wire cs_ram = cas | ~cs0 | ~cs1;
    wire [7:0] ram_dout    = cs_ram ? 8'hFF : ram_q;
    wire [7:0] kernal_dout = (!cs1 && (romh == 2'd0 || kern)) ? kern_q : 8'hFF;
    wire [7:0] basic_dout  = (!cs0 && roml == 2'd0) ? basic_q : 8'hFF;
    wire [7:0] fl_dout     = (!cs0 && roml == 2'd2) ? flo_q : 8'hFF;
    wire [7:0] fh_dout     = (!cs1 && romh == 2'd2 && !kern) ? fhi_q : 8'hFF;
    wire [7:0] cartl_dout  = (!cs0 && cart_s[0] && roml == 2'd1) ? clo_q : 8'hFF;
    wire [7:0] carth_dout  = (!cs1 && cart_s[1] && romh == 2'd1 && !kern) ? chi_q : 8'hFF;
    assign c16_din = ram_dout & kernal_dout & basic_dout & fl_dout & fh_dout & cartl_dout & carth_dout;

    // the app's window and the JTAG port
    wire [7:0] conf_do = w_do;
    c264_win win (
        .clk(sys_clk),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(dbg_busy), .rom_sel(rom_sel_s),
        .port_use(win_port_use), .port_addr(win_port_addr), .port_rom(win_port_rom), .port_we(win_port_we),
        .port_wdata(win_port_wdata), .port_rdata(conf_do),
        .p_req(port_req), .p_addr(port_addr), .p_rom(port_rom), .p_we(port_we), .p_wdata(port_wdata),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di)
    );
    // the read data is valid in the clock after the request and stays until the next request's
    reg       req_q = 1'b0;
    reg [7:0] rd_hold = 8'd0;
    always @(posedge sys_clk) begin
        req_q <= port_req;
        if (req_q) rd_hold <= conf_do;
    end
    assign ram_rdata = req_q ? conf_do : rd_hold;

    // ---------------------------------------------------------------- picture
    wire [3:0] cr, cg, cb;
    colors_to_rgb colordecode (.clk(sys_clk), .color(c16_color), .red(cr), .green(cg), .blue(cb));
    wire [15:0] rgb565 = {cr, cr[3], cg, cg[3:2], cb, cb[3]};

    // the dot phase: the same count as the TED's phicounter[1:0] (both start at 0, both move on ted_en or reset), so that the
    // dot strobe sits at a fixed place of every dot, two TED clocks after the TED has latched the colour
    reg [1:0] ph = 2'd0;
    always @(posedge sys_clk) if (ted_en | sreset) ph <= ph + 2'd1;
    wire dotst = ted_en & (ph == 2'd2);

    wire        pw_quiet, frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    c264_pixw pixw (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .dotst(dotst), .rgb565(rgb565), .hblank(hblank), .vblank(vblank),
        .quiet(pw_quiet), .frame_tgl(frame_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );

    // the raster the host's scaler locks to: 504 dots of 4 clocks, 312 lines, colour 0; not tied to the TED, never stops
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

    // ---------------------------------------------------------------- sound
    c264_audio audio (.clk(sys_clk), .a_in(digi_sound), .mute(held), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // ---------------------------------------------------------------- registers
    reg [15:0] frames = 16'd0;
    reg        vb_s = 1'b1;
    always @(posedge sys_clk) if (ted_en) begin
        vb_s <= vblank;
        if (vb_s & ~vblank) frames <= frames + 16'd1;
    end
    // the frame-done toggle into cpu_clk for the interrupt cause
    (* ASYNC_REG = "TRUE" *) reg [2:0] ft_s = 3'b000;
    always @(posedge cpu_clk) ft_s <= {ft_s[1:0], frame_tgl};
    wire ev_frame = ft_s[2] ^ ft_s[1];
    wire [7:0] status = {5'd0, ted_pal, held, rst_q | sreset};
    c264_regs #(.ID_TRX2(ID_TRX2)) regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(ctrl), .model(r_model), .ram64(r_ram64), .joy_swap(r_swap), .rom_sel(r_sel), .cart(r_cart),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .status(status), .frames(frames), .cpuaddr(c16_addr),
        .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over)
    );

    // ---------------------------------------------------------------- JTAG proof of life (trxdbg "trx 0 ..."), words:
    //  0 "C264"   1 status: b0 in reset, b1 held, b2 reset_req, b3 ctrl reset hold, b4 sys_rst, b5 halt_req, b6 ctrl halt, b7 left
    //  reset once   2 TED frames   3 bus address now   4 TED clocks counted   5 lines counted   6 address changes counted
    //  7 {last CPU write to $FF11, writes to $FF0E-$FF12}   8 peak digi_sound   9 peak abs(audio_l) at the layer-2 port
    reg [15:0] ce_cnt = 16'd0, ln_cnt = 16'd0, ad_cnt = 16'd0;
    reg [15:0] caddr_q = 16'd0;
    reg        hb_q = 1'b1, hb_d = 1'b1, seen_run = 1'b0;
    always @(posedge sys_clk) begin
        if (ted_en) begin
            ce_cnt <= ce_cnt + 16'd1;
            hb_q <= hblank; hb_d <= hb_q;
            if (hb_q == 1'b0 && hb_d == 1'b1) ln_cnt <= ln_cnt + 16'd1;
        end
        caddr_q <= c16_addr;
        if (caddr_q != c16_addr) ad_cnt <= ad_cnt + 16'd1;
        if (!sreset) seen_run <= 1'b1;
    end
    // T90 sound probe: what the board's CPU writes to the TED's sound control, the TED's level and the samples at the layer-2 port
    reg [7:0]  snd_wr11 = 8'd0;                     // last CPU write to $FF11 (volume, voice enables)
    reg [15:0] snd_wr_cnt = 16'd0;                  // TED clocks with a CPU write to $FF0E..$FF12
    reg [15:0] snd_dpeak = 16'd0;                   // peak of digi_sound since reset
    reg [23:0] snd_apeak = 24'd0;                   // peak |audio_l| at the port (mclk; read asynchronously, the value only grows)
    always @(posedge sys_clk) if (ted_en) begin
        if (!rnw && c16_addr[15:0] == 16'hFF11) snd_wr11 <= c16_data;
        if (!rnw && c16_addr >= 16'hFF0E && c16_addr <= 16'hFF12 && snd_wr_cnt != 16'hFFFF) snd_wr_cnt <= snd_wr_cnt + 16'd1;
        if (digi_sound > $signed(snd_dpeak)) snd_dpeak <= digi_sound;
    end
    wire signed [23:0] aud_abs = audio_l[23] ? -audio_l : audio_l;
    always @(posedge mclk) if (aud_abs > $signed(snd_apeak)) snd_apeak <= aud_abs;
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h34363243;
            4'h1: jd = {24'd0, seen_run, ctrl_s2[1], halt_req, sys_rst, ctrl_s2[0], reset_req, held, rst_q | sreset};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, c16_addr};
            4'h4: jd = {16'd0, ce_cnt};
            4'h5: jd = {16'd0, ln_cnt};
            4'h6: jd = {16'd0, ad_cnt};
            4'h7: jd = {8'd0, snd_wr11, snd_wr_cnt};
            4'h8: jd = {16'd0, snd_dpeak};
            4'h9: jd = {8'd0, snd_apeak};
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
