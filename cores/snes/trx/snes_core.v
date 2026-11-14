// TRX SNES core (T142): the Super Nintendo of SNES_MiSTer (cores/snes/gen: the S-CPU, S-PPU, S-SMP, S-DSP and the plain cartridge mapper as GHDL made them
// from the VHDL, srg320 and others, GPL v3, see rtl/NOTICE) behind the host's layer-2 socket of a LARGE core (host/top.v, unchanged). This entity is
// what cores/snes/layer2_snes.v wraps; it has no C64 part in it.
//
// Clock: the machine was written for a 21.477 MHz master clock (21.281 MHz PAL). It runs on cpu_clk, the host's 100 MHz, with that master clock as an
// enable: an NCO (ticks 4 or 5 clocks apart, so every path between two flip-flops of the machine has at least four clocks - cores/snes/core_timing.tcl
// relaxes them accordingly). tools/snes_gen.py put the enable on every clocked process: X_cep is a rising edge of the machine's clock, X_cen the falling one
// (only the S-DSP's and the mapper's clock-enable generators have one); here cen is the tick delayed by two clocks. `sys_clk` only carries the app's reset
// and pause requests in and the dummy raster out; `mclk` carries the sound.
// Memory: the cartridge ROM, the work RAM and the battery RAM are in the DDR2 behind line caches (snes_mem.v); a miss holds the machine's clock enable. The
// video RAM (2 x 32 KiB), the audio RAM (64 KiB) and the PPU's and DSP's small memories are block RAM (here and cores/snes/ip/snes_ip.vhd). All of them
// are clocked by the machine's enable, so they behave as in SNES_MiSTer's top (SNES.sv): one master clock per access, the read of the address of the clock.
// Freeze: the machine's clock enable stops (a request is honoured at the next tick; the machine and everything on the enable stand). `halted` answers when
// the enable is held and the picture writer has nothing on its way to the DDR2.
// Reset: the app holds it (CTRL.b0) while it writes the cartridge image, the header registers and the save into the DDR2; the caches are flushed at both
// edges of that request and the machine stays in reset until the flush is done.
// Picture: the PPU's 512 pixels of each line become RGB565 and are written into a DDR2 frame buffer for the host's pixel layer (snes_pixw); the host's
// VIC-II raster input gets a dummy raster (the scaler locks to it; it never stops), as in the NES and C16 cores.
// Controller: snes_pad drives both ports from the host's joysticks and the C64 keyboard, and (T156) the application's USB pads (snes_regs PAD1 / PAD2), OR-ed.
`timescale 1ns / 1ps
module snes_core #(
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
    output wire        running,                     // out of reset (case LED 1), sys_clk

    input  wire [4:0]  c64_joy1,                    // the controller ports (snes_pad), sys_clk
    input  wire [4:0]  c64_joy2,
    input  wire [63:0] c64_keys,

    output wire [3:0]  pix_color,
    output wire [9:0]  pix_x,
    output wire [8:0]  pix_y,
    input  wire        audio_sample_en,             // mclk: the host's audio frame
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

    // the host's real-time DDR2 port c_* (ui_clk): the cartridge image, work RAM and battery RAM (snes_mem)
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
    wire [7:0]  ctrl, sys_cfg, rom_type;
    wire [23:0] rom_mask, ram_mask;
    wire [64:0] rtc;
    wire [31:0] cart_base;
    wire [31:0] fb_base, fb_stride;
    wire        ipl_wr;
    wire [5:0]  ipl_wa;
    wire [7:0]  ipl_wd;
    wire        fw_wr, fw_sel;                 // the DSP-n firmware port (T218, snes_regs.v 0x140)
    wire [10:0] fw_a;
    wire [23:0] fw_d;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rq_s = 2'b00, hl_s = 2'b00;
    always @(posedge cpu_clk) begin
        rq_s <= {rq_s[0], reset_req | sys_rst};
        hl_s <= {hl_s[0], halt_req};
    end

    // ---------------------------------------------------------------- reset and pause
    // rst_m: the request (the app's hold, the host's reset). rst_r: the machine's reset, the request stretched by 640 clocks so that the cache flush
    // (512 clocks, snes_mem) that both edges of the request start is done before the machine runs. The machine's RST_N is a plain clocked reset (every
    // process of tools/snes_gen.py's machine resets on cpu_clk and not on the enable), so the machine is in reset whether it ticks or not.
    wire       rst_m = rq_s[1] | ctrl[0];
    reg        rst_r = 1'b1;
    reg [9:0]  rcnt = 10'd0;
    wire       fl_busy;
    always @(posedge cpu_clk) begin
        if (rst_m) begin rcnt <= 10'd0; rst_r <= 1'b1; end
        else if (rcnt[9] && !fl_busy) rst_r <= 1'b0;
        else if (!rcnt[9]) rcnt <= rcnt + 10'd1;
    end
    wire       hold_req = hl_s[1] | ctrl[1];

    // ---------------------------------------------------------------- the master clock as an enable
    // 21.477272 MHz / 100 MHz in 32 bits. `cap` is the clock before a tick: everything that samples the machine's combinational outputs (snes_mem, the picture,
    // the sound) does it there, on values that have had at least three clocks to settle.
    localparam [31:0] NCO_NTSC = 32'd922441840;     // 21.477272 MHz
    localparam [31:0] NCO_PAL  = 32'd914027864;     // 21.2813696 MHz
    wire [31:0] NCO_STEP = sys_cfg[0] ? NCO_PAL : NCO_NTSC;
    reg [31:0] nco = 32'd0;
    reg        cap_r = 1'b0;
    (* MAX_FANOUT = 200 *) reg ce_r = 1'b0;          // the tools replicate it
    reg        ce_d1 = 1'b0;
    (* MAX_FANOUT = 200 *) reg cen_r = 1'b0;         // the falling edge of the machine's clock: two clocks after the rising one
    reg        adv = 1'b1;                           // a tick happened since the last cap
    wire [32:0] nco_n = {1'b0, nco} + {1'b0, NCO_STEP};
    wire        stall;
    wire        hold = hold_req & ~rst_r & ~rst_m;
    always @(posedge cpu_clk) begin
        nco <= nco_n[31:0];
        cap_r <= nco_n[32];
        ce_r <= cap_r & ~stall & ~hold;             // one registered net to the machine's thousands of flip-flops
        ce_d1 <= ce_r;
        cen_r <= ce_d1;
        if (cap_r) adv <= 1'b0;
        if (ce_r) adv <= 1'b1;
    end
    wire ce = ce_r;

    // What the machine sees of the rest of the core changes only on a tick, like its own flip-flops: every path into it then has the four clocks that
    // cores/snes/core_timing.tcl gives the machine.
    reg         pal_q = 1'b0, blend_q = 1'b1, dspf_q = 1'b0;
    reg  [7:0]  type_q = 8'd0;
    reg  [23:0] rmask_q = 24'd0, bmask_q = 24'd0;
    reg  [64:0] rtc_q = 65'd0;
    always @(posedge cpu_clk) if (ce) begin
        pal_q <= sys_cfg[0]; blend_q <= ~sys_cfg[1]; dspf_q <= sys_cfg[2];
        type_q <= rom_type; rmask_q <= rom_mask; bmask_q <= ram_mask; rtc_q <= rtc;
    end
    reg         mrst_q = 1'b1;                       // the machine's reset input: rst_r, or the cache flush
    always @(posedge cpu_clk) mrst_q <= rst_r | fl_busy;

    // ---------------------------------------------------------------- the machine
    wire [23:0] rom_addr;
    wire        rom_ce_n, rom_oe_n;
    wire [19:0] bs_addr;
    wire [7:0]  bs_d;
    wire        bs_ce_n, bs_oe_n, bs_we_n;
    wire [16:0] wram_addr;
    wire [7:0]  wram_d;
    wire        wram_ce_n, wram_oe_n, wram_we_n;
    wire [15:0] vram_aa, vram_ab;
    wire [7:0]  vram_dao, vram_dbo;
    wire        vram_wra_n, vram_wrb_n, vram_rd_n;
    wire [15:0] aram_addr;
    wire [7:0]  aram_d;
    wire        aram_ce_n, aram_oe_n, aram_we_n;
    wire        sysclkf, sysclkr, refresh;
    wire        high_res, field_out, interlace, v224, dotclk, hde, vde, hsync, vsync, atzero;
    wire [23:0] rgb_out;
    wire        joy_strb, joy1_clk, joy2_clk;
    wire [1:0]  joy1_di, joy2_di;
    wire signed [15:0] a_l, a_r;
    wire [7:0]  rom_q8, wram_q8, bs_q8;
    wire [7:0]  vram_dai, vram_dbi, aram_q;

    snes_sys m (
        .fclk(cpu_clk), .MCLK_cep(ce), .MCLK_cen(cen_r), .DSPCLK_cep(ce), .DSPCLK_cen(cen_r),
        .RST_N(~mrst_q), .PAL(pal_q), .BLEND(blend_q), .DSP_FREQ(dspf_q),
        .ROM_TYPE(type_q), .ROM_MASK(rmask_q), .RAM_MASK(bmask_q), .EXT_RTC(rtc_q),
        .ROM_ADDR(rom_addr), .ROM_Q({8'd0, rom_q8}), .ROM_CE_N(rom_ce_n), .ROM_OE_N(rom_oe_n),
        .BSRAM_ADDR(bs_addr), .BSRAM_D(bs_d), .BSRAM_Q(bs_q8), .BSRAM_CE_N(bs_ce_n), .BSRAM_OE_N(bs_oe_n), .BSRAM_WE_N(bs_we_n),
        .WRAM_ADDR(wram_addr), .WRAM_D(wram_d), .WRAM_Q(wram_q8), .WRAM_CE_N(wram_ce_n), .WRAM_OE_N(wram_oe_n), .WRAM_WE_N(wram_we_n),
        .VRAM_ADDRA(vram_aa), .VRAM_ADDRB(vram_ab), .VRAM_DAI(vram_dai), .VRAM_DBI(vram_dbi), .VRAM_DAO(vram_dao), .VRAM_DBO(vram_dbo),
        .VRAM_WRA_N(vram_wra_n), .VRAM_WRB_N(vram_wrb_n), .VRAM_RD_N(vram_rd_n),
        .ARAM_ADDR(aram_addr), .ARAM_D(aram_d), .ARAM_Q(aram_q), .ARAM_CE_N(aram_ce_n), .ARAM_OE_N(aram_oe_n), .ARAM_WE_N(aram_we_n),
        .SYSCLKF_CE(sysclkf), .SYSCLKR_CE(sysclkr), .REFRESH(refresh),
        .HIGH_RES(high_res), .FIELD_OUT(field_out), .INTERLACE(interlace), .V224_MODE(v224), .DOTCLK(dotclk),
        .RGB_OUT(rgb_out), .HDE(hde), .VDE(vde), .HSYNC(hsync), .VSYNC(vsync), .HVCNT_ATZERO(atzero),
        .JOY1_DI(joy1_di), .JOY2_DI(joy2_di), .JOY_STRB(joy_strb), .JOY1_CLK(joy1_clk), .JOY2_CLK(joy2_clk),
        .AUDIO_L(a_l), .AUDIO_R(a_r), .IPL_WR(ipl_wr), .IPL_WA(ipl_wa), .IPL_WD(ipl_wd),
        .FW_WR(fw_wr), .FW_SEL(fw_sel), .FW_A(fw_a), .FW_D(fw_d));

    // ---------------------------------------------------------------- the machine's own block RAM (SNES.sv: dpram vram1, vram2, aram)
    // all of them read and write on the machine's enable; the read of an address is registered (a block RAM), a write is read back as written
    (* ram_style = "block" *) reg [7:0] vram1 [0:32767] /*verilator public_flat_rw*/;
    (* ram_style = "block" *) reg [7:0] vram2 [0:32767] /*verilator public_flat_rw*/;
    (* ram_style = "block" *) reg [7:0] aram  [0:65535] /*verilator public_flat_rw*/;
    reg [7:0] vram1_q = 8'd0, vram2_q = 8'd0, aram_qr = 8'd0;
    reg       vrd_d = 1'b1;
    always @(posedge cpu_clk) if (ce) begin
        if (!vram_wra_n) begin vram1[vram_aa[14:0]] <= vram_dao; vram1_q <= vram_dao; end else vram1_q <= vram1[vram_aa[14:0]];
    end
    always @(posedge cpu_clk) if (ce) begin
        if (!vram_wrb_n) begin vram2[vram_ab[14:0]] <= vram_dbo; vram2_q <= vram_dbo; end else vram2_q <= vram2[vram_ab[14:0]];
    end
    always @(posedge cpu_clk) if (ce) begin
        if (!aram_ce_n && !aram_we_n) begin aram[aram_addr] <= aram_d; aram_qr <= aram_d; end else aram_qr <= aram[aram_addr];
    end
    always @(posedge cpu_clk) if (ce) vrd_d <= vram_rd_n;
    // SNES.sv: the PPU reads 0xFF when it did not ask for the byte in this clock and the one before
    assign vram_dai = (!vram_rd_n && !vrd_d) ? vram1_q : 8'hFF;
    assign vram_dbi = (!vram_rd_n && !vrd_d) ? vram2_q : 8'hFF;
    assign aram_q = aram_qr;

    // ---------------------------------------------------------------- the cartridge memory
    // fresh: the mapper's ROM address register has been loaded in this bus cycle (SYSCLKR_CE loads it, SYSCLKF_CE ends the cycle): only then is ROM_ADDR
    // the address of the cycle and not of the one before (DSP_LHRomMap.vhd)
    reg         fresh = 1'b0;
    always @(posedge cpu_clk) if (ce) begin
        if (sysclkr) fresh <= 1'b1;
        else if (sysclkf) fresh <= 1'b0;
    end
    wire        running_i = ~mrst_q;
    wire        rom_rd = running_i & fresh & ~rom_ce_n;
    wire        wram_rd = running_i & ~wram_ce_n & ~wram_oe_n;
    // a write commits at the tick on which SYSCLKF_CE is high (SNES.sv: wr1 = ... & SNES_SYSCLKF_CE): the one-shot per bus cycle
    wire        wram_wr = running_i & ~wram_ce_n & ~wram_we_n & sysclkf;
    wire        bs_rd = running_i & ~bs_ce_n & ~bs_oe_n;
    wire        bs_wr = running_i & ~bs_ce_n & ~bs_we_n & sysclkf;
    wire [31:0] st_miss_rom, st_miss_wram, st_miss_bs, st_stall, st_wr;
`ifndef SNES_MEM_MOD                                      // the benches build a reference with a memory that is never late (sim/snes_mem_ref.v)
`define SNES_MEM_MOD snes_mem
`endif
    `SNES_MEM_MOD mem (
        .clk(cpu_clk), .rst(rst_m), .busy(fl_busy), .cap(cap_r), .adv(adv), .base(cart_base),
        .rom_a(rom_addr[22:0]), .rom_rd(rom_rd), .rom_q(rom_q8),
        .wram_a(wram_addr), .wram_rd(wram_rd), .wram_wr(wram_wr), .wram_d(wram_d), .wram_q(wram_q8),
        .bs_a(bs_addr[17:0]), .bs_rd(bs_rd), .bs_wr(bs_wr), .bs_d(bs_d), .bs_q(bs_q8),
        .stall(stall),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
        .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .st_miss_rom(st_miss_rom), .st_miss_wram(st_miss_wram), .st_miss_bs(st_miss_bs), .st_stall(st_stall), .st_wr(st_wr));

    // ---------------------------------------------------------------- controller
    wire [15:0] app_pad1, app_pad2;                 // the application's USB pads (T156), cpu_clk like the register block they come from
    snes_pad pad (.clk(cpu_clk), .rst(mrst_q), .joy1(c64_joy1), .joy2(c64_joy2), .keys(c64_keys), .app1(app_pad1), .app2(app_pad2),
                  .strobe(joy_strb), .clk1(joy1_clk), .clk2(joy2_clk), .di1(joy1_di), .di2(joy2_di));

    // ---------------------------------------------------------------- picture
    // One event for each half of a dot (DOTCLK changes, the PPU's COLOR_OUT is the sub screen pixel while DOTCLK is high and the main one while it is
    // low): a high resolution frame keeps both pixels, a low resolution one the main pixel twice (SNES.sv's ce_pix does the same with its counter).
    // Sampled at cap on the registers the machine left. The events go on even in the blanking, the picture writer follows the blanking from them.
    reg         dclk_q = 1'b0, ev_p = 1'b0, ev2_p = 1'b0, ev3_p = 1'b0, dbl_p = 1'b0;
    reg  [15:0] pxd = 16'd0;
    reg         pxb_h = 1'b1, pxb_v = 1'b1, pxb_il = 1'b0, pxb_f = 1'b0, pxb_224 = 1'b1;
    wire [15:0] rgb565 = {rgb_out[7:3], rgb_out[15:10], rgb_out[23:19]};
    wire        ev_edge = cap_r && (dotclk != dclk_q);
    always @(posedge cpu_clk) begin
        ev_p <= 1'b0; ev2_p <= ev_p; ev3_p <= ev2_p;
        if (cap_r) dclk_q <= dotclk;
        if (ev_edge && (high_res || !dotclk)) begin     // hires: both halves; low resolution: the main pixel (the falling edge)
            ev_p <= 1'b1;
            dbl_p <= ~high_res;
            pxd <= rgb565;
            pxb_h <= ~hde; pxb_v <= ~vde; pxb_il <= interlace; pxb_f <= field_out; pxb_224 <= v224;
        end
    end
    // low resolution: the strobe twice, two clocks apart
    wire        px_s = ev_p | (ev3_p & dbl_p);
    wire        pw_quiet, frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    snes_pixw pixw (
        .sys_clk(cpu_clk), .sys_rst(rst_r), .px(px_s), .rgb565(pxd), .hblank(pxb_h), .vblank(pxb_v), .interlace(pxb_il), .field(pxb_f), .v224(pxb_224),
        .quiet(pw_quiet), .frame_tgl(frame_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
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

    // ---------------------------------------------------------------- sound
    wire corepaused = hold;
    wire        pk_clr;
    wire [15:0] pk_dsp, pk_out, hands;
    snes_audio audio (.clk(cpu_clk), .l_in(a_l), .r_in(a_r), .cap(cap_r), .ce(ce), .mute(corepaused | mrst_q), .mclk(mclk), .asen(audio_sample_en), .audio_l(audio_l), .audio_r(audio_r),
                      .pk_clr(pk_clr), .pk_dsp(pk_dsp), .pk_out(pk_out), .hands(hands));

    // ---------------------------------------------------------------- registers
    reg [15:0] frames = 16'd0;
    reg        ft_q = 1'b0, vde_q = 1'b0;
    reg [15:0] rom_a_s = 16'd0;
    always @(posedge cpu_clk) begin
        ft_q <= frame_tgl;
        if (cap_r) begin
            vde_q <= vde;
            if (vde_q & ~vde) frames <= frames + 16'd1;
            rom_a_s <= rom_addr[15:0];
        end
    end
    wire ev_frame = ft_q ^ frame_tgl;
    wire [7:0] status = {6'd0, corepaused, mrst_q};
    snes_regs #(.ID_TRX2(ID_TRX2)) regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(ctrl), .sys_cfg(sys_cfg), .rom_type(rom_type), .rom_mask(rom_mask), .ram_mask(ram_mask), .rtc(rtc), .cart_base(cart_base), .pad1(app_pad1), .pad2(app_pad2), .ipl_wr(ipl_wr), .ipl_wa(ipl_wa), .ipl_wd(ipl_wd), .fw_wr(fw_wr), .fw_sel(fw_sel), .fw_a(fw_a), .fw_d(fw_d),
        .st_miss_rom(st_miss_rom), .st_miss_wram(st_miss_wram), .st_miss_bs(st_miss_bs), .st_stall(st_stall), .st_wr(st_wr),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .status(status), .frames(frames), .romaddr(rom_a_s),
        .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over),
        .pk_clr(pk_clr), .pk_dsp(pk_dsp), .pk_out(pk_out), .hands(hands)
    );

    // ---------------------------------------------------------------- the answers back in sys_clk
    // halted: the machine is held and the frame writer has nothing on its way to the DDR2
    (* ASYNC_REG = "TRUE" *) reg [1:0] ho_s = 2'b00, ru_s = 2'b00;
    always @(posedge sys_clk) begin
        ho_s <= {ho_s[0], corepaused & pw_quiet};
        ru_s <= {ru_s[0], ~mrst_q};
    end
    assign halted = ho_s[1];
    assign running = ru_s[1];

    // ---------------------------------------------------------------- JTAG proof of life (trxdbg "trx 0 ..."), words:
    //  0 "SNES"  1 status: b0 in reset, b1 held, b2 reset_req, b3 ctrl reset hold, b4 sys_rst, b5 halt_req, b6 ctrl halt, b7 stalled
    //  2 frames   3 last cartridge address (low 16 bits)   4 machine ticks counted (low 16)   5 stall clocks (low 16)   6 rom_type   7 write-throughs queued
    reg [15:0] tick_cnt = 16'd0;
    always @(posedge cpu_clk) if (ce) tick_cnt <= tick_cnt + 16'd1;
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h53454E53;
            4'h1: jd = {24'd0, stall, ctrl[1], halt_req, sys_rst, ctrl[0], reset_req, corepaused, mrst_q};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, rom_a_s};
            4'h4: jd = {16'd0, tick_cnt};
            4'h5: jd = {16'd0, st_stall[15:0]};
            4'h6: jd = {24'd0, rom_type};
            4'h7: jd = st_wr;
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
