// TRX NES core (T95 D12): the NES of NES_MiSTer (cores/nes/gen: nes.v, ppu.sv, apu.sv, cart.sv, the mappers, the 6502 as GHDL made it
// from T65; Ludvig Strigeus and others, GPL v3, see rtl/NOTICE) behind the host's layer-2 socket. This entity is what
// cores/nes/layer2_nes.v wraps; it has no C64 part in it.
//
// Clock: the machine was written for a 21.477 MHz master clock with its own dividers (CPU /12, PPU /4). It runs on cpu_clk, the
// host's 100 MHz, with that master clock as an enable: an NCO (22.1 % duty, ticks 4 or 5 clocks apart, so every path between two
// flip-flops of the machine has at least four clocks - cores/nes/nes.xdc relaxes them accordingly). tools/nes_gen.py put the enable
// `ce` on every clocked process. `sys_clk` only carries the app's reset and pause requests in and the dummy raster out; `mclk`
// carries the sound.
// Freeze: the machine's own pause handshake (nes.v `pausecore` / `corepaused`, the MiSTer OSD pause): the request is held until
// the machine is in vblank on an instruction boundary, then every counter stops and the picture stays; releasing resumes at the
// same dot. The NCO keeps running (the handshake counts on it). `halted` is "paused and the frame writer has nothing on its way
// to the DDR2". A reset takes the pause away.
// Picture: the PPU's 6-bit colour index of each dot of the visible window (256 x 224: the PPU's 8 overscan lines top and bottom
// left out) becomes RGB565 (nes_pal) and is written into a DDR2 frame buffer for the host's pixel layer (nes_pixw); the host's
// VIC-II raster input gets a dummy raster (the scaler locks to it; it never stops), as in the C16 core.
// Controller (T95 5.18): nes_pad drives both ports from the host's joysticks and the C64 keyboard - directions and A from
// c64_joy1/2, B = space / right shift, Select = F3 / F7, Start = F1 / F5. Since T156 the application's USB pads (nes_regs PAD1 / PAD2) are OR-ed in.
`timescale 1ns / 1ps
module nes_core #(
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

    input  wire [4:0]  c64_joy1,                   // the controller ports (nes_pad), sys_clk
    input  wire [4:0]  c64_joy2,
    input  wire [63:0] c64_keys,

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

    // the host's real-time DDR2 port c_* (ui_clk): the cartridge image (nes_mem)
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
    wire [127:0] hdr;
    wire [1:0]  sys_cfg;
    wire [31:0] cart_base;
    wire [4:0]  win_page;
    wire [31:0] fb_base, fb_stride;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rq_s = 2'b00, hl_s = 2'b00;
    always @(posedge cpu_clk) begin
        rq_s <= {rq_s[0], reset_req | sys_rst};
        hl_s <= {hl_s[0], halt_req};
    end

    // ---------------------------------------------------------------- reset and pause
    wire       rst_m = rq_s[1] | ctrl[0];
    reg        rst_r = 1'b1;                        // the machine's reset: the request, stretched by 256 clocks
    reg [8:0]  rcnt = 9'd0;
    always @(posedge cpu_clk) begin
        if (rst_m) begin rcnt <= 9'd0; rst_r <= 1'b1; end
        else if (rcnt[8]) rst_r <= 1'b0;
        else rcnt <= rcnt + 9'd1;
    end
    wire       hold_req = hl_s[1] | ctrl[1];

    // ---------------------------------------------------------------- the master clock as an enable
    // 21.477272 MHz / 100 MHz in 32 bits. `cap` is the clock before a tick: everything that samples the machine's combinational
    // outputs (nes_mem) does it there, on values that have had at least three clocks to settle.
    localparam [31:0] NCO_NTSC = 32'd922441840;     // 21.477272 MHz (NTSC and Dendy)
    localparam [31:0] NCO_PAL  = 32'd914027864;     // 21.2813696 MHz
    wire [31:0] NCO_STEP = (sys_cfg == 2'd1) ? NCO_PAL : NCO_NTSC;
    reg [31:0] nco = 32'd0;
    reg        cap_r = 1'b0;
    (* MAX_FANOUT = 200 *) reg ce_r = 1'b0;          // the tools replicate it
    wire [32:0] nco_n = {1'b0, nco} + {1'b0, NCO_STEP};
    wire        stall;
    always @(posedge cpu_clk) begin
        nco <= nco_n[31:0];
        cap_r <= nco_n[32];
        ce_r <= cap_r & ~stall;                     // one registered net to the machine's thousands of flip-flops
    end
    wire ce = ce_r;

    // What the machine sees of the rest of the core changes only on a tick, like its own flip-flops: every path into it then has
    // the four clocks that cores/nes/nes.xdc gives the machine (a request that changed one clock before a tick would meet logic
    // that was timed for four).
    reg         pausecore = 1'b0, rst_m_q = 1'b1;
    reg  [1:0]  sys_q = 2'd0;
    reg  [63:0] flags_q = 64'd0;
    reg  [9:0]  pmask_q = 10'd0, cmask_q = 10'd0;
    wire [63:0] mapper_flags;
    wire [9:0]  prg_mask, chr_mask;
    always @(posedge cpu_clk) if (ce) begin
        pausecore <= hold_req & ~rst_r & ~rst_m;
        rst_m_q <= rst_r;
        sys_q <= sys_cfg;
        flags_q <= mapper_flags; pmask_q <= prg_mask; cmask_q <= chr_mask;
    end

    // ---------------------------------------------------------------- the machine
    wire [21:0] prg_bytes, chr_bytes;
    nes_flags flags (.hdr(hdr), .mapper_flags(mapper_flags), .prg_mask(prg_mask), .chr_mask(chr_mask), .prg_bytes(prg_bytes), .chr_bytes(chr_bytes));

    wire [24:0] cpumem_addr;
    wire        cpumem_read, cpumem_write;
    wire [7:0]  cpumem_dout, cpumem_din;
    wire [21:0] ppumem_addr;
    wire        ppumem_read, ppumem_write;
    wire [7:0]  ppumem_dout, ppumem_din;
    wire [15:0] sample;
    wire [5:0]  color;
    wire [8:0]  cycle, scanline;
    wire        px_ce_o, corepaused, apu_ce, nes_hblank, nes_vblank;
    wire [1:0]  joypad_clock;
    wire [2:0]  joypad_out;
    wire [4:0]  pad_data1, pad_data2;
    wire joy_swap;
    wire [7:0]  app_pad1, app_pad2;                 // the application's USB pads (T156), cpu_clk like the register block they come from
    nes_pad pad (.clk(cpu_clk), .rst(rst_m_q), .joy1(c64_joy1), .joy2(c64_joy2), .joy_swap(joy_swap), .keys(c64_keys), .app1(app_pad1), .app2(app_pad2),
                 .strobe(joypad_out[0]), .rdclk(joypad_clock), .data1(pad_data1), .data2(pad_data2));
    NES m (
        .ce_in(ce), .clk(cpu_clk), .reset_nes(rst_m_q), .ppu_rst_behavior(1'b0), .cold_reset(rst_m_q),
        .pausecore(pausecore), .corepaused(corepaused), .sys_type(sys_q), .vs_dip_switches(8'd0), .nes_div(),
        .mapper_flags(flags_q), .sample(sample), .color(color),
        .joypad_clock(joypad_clock), .joypad_out(joypad_out), .joypad1_data(pad_data1), .joypad2_data(pad_data2),
        .fds_busy(1'b0), .fds_eject(1'b0), .fds_auto_eject(1'b0), .max_diskside(2'd0), .fds_fast(1'b0), .diskside(),
        .audio_channels(5'b11111), .ex_sprites(1'b0), .mask(2'b00), .dejitter_timing(1'b1),
        .cpumem_addr(cpumem_addr), .cpumem_read(cpumem_read), .cpumem_write(cpumem_write), .cpumem_dout(cpumem_dout), .cpumem_din(cpumem_din),
        .ppumem_addr(ppumem_addr), .ppumem_read(ppumem_read), .ppumem_write(ppumem_write), .ppumem_dout(ppumem_dout), .ppumem_din(ppumem_din),
        .refresh(),
        .prg_mask({11'd0, pmask_q}), .chr_mask({10'd0, cmask_q}),
        .bram_addr(), .bram_din(8'd0), .bram_dout(), .bram_write(), .bram_override(),
        .cycle(cycle), .scanline(scanline), .int_audio(1'b1), .ext_audio(1'b0), .apu_ce(apu_ce),
        .gg(1'b1), .gg_code(129'd0), .gg_avail(), .gg_reset(1'b0), .emphasis(), .save_written(), .mapper_has_flashsaves(),
        .debug_dots(1'b0),
        .mapper_has_savestate(), .increaseSSHeaderCount(1'b0), .save_state(1'b0), .load_state(1'b0), .savestate_number(2'd0),
        .sleep_savestate(), .state_loaded(),
        .hsync(), .hblank(nes_hblank), .vsync(), .vblank(nes_vblank), .composite_a(), .composite_b(),
        .Savestate_SDRAMAddr(), .Savestate_SDRAMRdEn(), .Savestate_SDRAMWrEn(), .Savestate_SDRAMWriteData(), .Savestate_SDRAMReadData(8'd0),
        .SaveStateExt_Din(), .SaveStateExt_Adr(), .SaveStateExt_wren(), .SaveStateExt_rst(), .SaveStateExt_Dout(64'd0), .SaveStateExt_load(),
        .SAVE_out_Din(), .SAVE_out_Dout(64'd0), .SAVE_out_Adr(), .SAVE_out_rnw(), .SAVE_out_ena(), .SAVE_out_be(), .SAVE_out_done(1'b0),
        .px_ce_o(px_ce_o)
    );

    // ---------------------------------------------------------------- the cartridge memory and the app's window
    wire        w_start, w_we, w_chr, w_done;
    wire [20:0] w_a;
    wire [7:0]  w_di, w_do;
    wire [31:0] st_miss_prg, st_miss_chr, st_stall;
    nes_mem mem (
        .clk(cpu_clk), .rst(rst_r), .cap(cap_r), .chr_ram(flags_q[15]), .cart_base(cart_base),
        .cpu_addr(cpumem_addr), .cpu_rd(cpumem_read), .cpu_wr(cpumem_write), .cpu_dout(cpumem_dout), .cpu_din(cpumem_din),
        .ppu_addr(ppumem_addr), .ppu_rd(ppumem_read), .ppu_wr(ppumem_write), .ppu_dout(ppumem_dout), .ppu_din(ppumem_din),
        .stall(stall),
        .w_start(w_start), .w_we(w_we), .w_chr(w_chr), .w_a(w_a), .w_di(w_di), .w_done(w_done), .w_do(w_do),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
        .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .st_miss_prg(st_miss_prg), .st_miss_chr(st_miss_chr), .st_stall(st_stall)
    );    nes_win win (
        .clk(cpu_clk),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(dbg_busy),
        .port_use(win_port_use), .port_addr(win_port_addr), .port_rom(win_port_rom), .port_we(win_port_we),
        .port_wdata(win_port_wdata),
        .page(win_page), .p_req(port_req), .p_addr(port_addr), .p_rom(port_rom), .p_we(port_we), .p_wdata(port_wdata),
        .w_start(w_start), .w_we(w_we), .w_chr(w_chr), .w_a(w_a), .w_di(w_di), .w_done(w_done), .w_do(w_do)
    );
    // the host's JTAG peek of RAM (ram_rdata) gets the byte of the last window read; the cartridge is in the DDR2, whose answer
    // comes when it comes, so the peek does not wait for it
    reg [7:0] rd_hold = 8'd0;
    always @(posedge cpu_clk) if (w_done) rd_hold <= w_do;
    assign ram_rdata = rd_hold;
    // ---------------------------------------------------------------- picture
    // a PPU dot is produced by a tick; `color` and the position are the registers it left
    // three clocks later, when everything the dot's path through the colour table has settled, the frame writer takes it (the
    // machine -> frame writer paths are timed with three clocks, cores/nes/nes.xdc)
    reg px_d1 = 1'b0, px_d2 = 1'b0, px_q = 1'b0;
    always @(posedge cpu_clk) begin px_d1 <= px_ce_o & ce; px_d2 <= px_d1; px_q <= px_d2; end
    wire [15:0] rgb565;
    nes_pal pal (.idx(color), .q(rgb565));
    localparam [8:0] X0 = 9'd2;                      // the dot (PPU `cycle`) of the window's first pixel
    wire vis_x = (cycle >= X0) && (cycle < X0 + 9'd256);
    wire vis_y = (scanline >= 9'd8) && (scanline < 9'd232);
    wire hblank = ~vis_x;
    wire vblank_w = ~vis_y;

    wire        pw_quiet, frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    nes_pixw pixw (
        .sys_clk(cpu_clk), .sys_rst(rst_r), .dotst(px_q), .rgb565(rgb565), .hblank(hblank), .vblank(vblank_w),
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
    nes_audio audio (.clk(cpu_clk), .a_in(sample), .cap(cap_r), .mute(corepaused), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // ---------------------------------------------------------------- registers
    reg [15:0] frames = 16'd0;
    reg        vb_s = 1'b1, ft_q = 1'b0;
    always @(posedge cpu_clk) begin
        ft_q <= frame_tgl;
        if (px_q) begin
            vb_s <= nes_vblank;
            if (vb_s & ~nes_vblank) frames <= frames + 16'd1;
        end
    end
    wire ev_frame = ft_q ^ frame_tgl;
    wire [7:0] status = {6'd0, corepaused, rst_r};
    reg [15:0] cpu_a_s = 16'd0;
    always @(posedge cpu_clk) if (cap_r) cpu_a_s <= cpumem_addr[15:0];
    nes_regs #(.ID_TRX2(ID_TRX2)) regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(ctrl), .hdr(hdr), .sys_type(sys_cfg), .joy_swap(joy_swap), .pad1(app_pad1), .pad2(app_pad2), .cart_base(cart_base), .st_miss_prg(st_miss_prg), .st_miss_chr(st_miss_chr), .st_stall(st_stall), .win_page(win_page), .fb_base(fb_base), .fb_stride(fb_stride),
        .status(status), .frames(frames), .cpuaddr(cpu_a_s),
        .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over)
    );

    // ---------------------------------------------------------------- the answers back in sys_clk
    // halted: the machine is paused and the frame writer has nothing on its way to the DDR2
    (* ASYNC_REG = "TRUE" *) reg [1:0] ho_s = 2'b00, ru_s = 2'b00;
    always @(posedge sys_clk) begin
        ho_s <= {ho_s[0], corepaused & pw_quiet};
        ru_s <= {ru_s[0], ~rst_r};
    end
    assign halted = ho_s[1];
    assign running = ru_s[1];

    // ---------------------------------------------------------------- JTAG proof of life (trxdbg "trx 0 ..."), words:
    //  0 "NES_"  1 status: b0 in reset, b1 paused, b2 reset_req, b3 ctrl reset hold, b4 sys_rst, b5 halt_req, b6 ctrl halt
    //  2 NES frames   3 last CPU-side address (linear, low 16 bits)   4 CPU cycles counted   5 dots counted (low 16)   6 mapper
    reg [15:0] cyc_cnt = 16'd0, dot_cnt = 16'd0;
    always @(posedge cpu_clk) begin
        if (apu_ce & ce) cyc_cnt <= cyc_cnt + 16'd1;
        if (px_q) dot_cnt <= dot_cnt + 16'd1;
    end
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h5F53454E;
            4'h1: jd = {25'd0, ctrl[1], halt_req, sys_rst, ctrl[0], reset_req, corepaused, rst_r};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, cpu_a_s};
            4'h4: jd = {16'd0, cyc_cnt};
            4'h5: jd = {16'd0, dot_cnt};
            4'h6: jd = {24'd0, mapper_flags[7:0]};
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
