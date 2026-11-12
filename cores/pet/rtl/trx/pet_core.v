// TRX PET core (T134): the Commodore PET family (2001, 3032, 4032, 8032, 8096) behind the host's layer-2 socket. The CPU, PIA and VIA are Thomas
// Skibo's (cores/pet/rtl/src, BSD, see NOTICE); the bus, the memories, the CRTC register file, the picture, the keyboard and the registers are
// written for this project. The machine follows pet2001hw.v of Pet2001_Arty (the bus, the I/O block pet2001io.v wiring) with a CRTC at $E880.
//
// Clock: sys_clk is the host's C64 clock (31.527778 MHz PAL). The CPU runs on a ready strobe `rdy` (one sys_clk wide), as Skibo's does: a 16 bit
// phase accumulator fires at 2079/65536 of sys_clk = 1.0002 MHz times the speed setting (x1..x8); the VIA timers get the x1 strobe `slow` always.
// A pause (halt) stops both. Every process of the machine otherwise runs on sys_clk; the memories answer one clock after the address and the
// CPU holds an address for at least three clocks.
// Picture: pet_vid (a raster of its own, 800 x 657 / 788 dots, one dot per sys_clk) into pet_pixw (the DDR2 frame buffer for the pixel layer);
// the host's VIC-II raster input gets a dummy raster of its own (the scaler locks to it; it never stops).
`timescale 1ns / 1ps
module pet_core (
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
    output wire        drv_tgl,                     // the drives' DDR2 port (cpu_clk)
    output wire [25:0] drv_addr,
    output wire        drv_we,
    output wire [31:0] drv_wdata,
    output wire [3:0]  drv_be,
    input  wire        drv_done_tgl,
    input  wire [127:0] drv_rdata,
    output wire [127:0] e_wdf_data,
    output wire [15:0] e_wdf_mask,
    output wire        e_wdf_wren,
    // the host's real-time DDR2 port c_* (ui_clk): the tape's pulse stream
    output wire [26:0] c_addr,
    output wire [2:0]  c_cmd,
    output wire        c_en,
    input  wire        c_rdy,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,
    // the cassette port pins (sys_clk side; layer2 registers them onto the c2n_* pins)
    input  wire        tp_read_pin,
    input  wire        tp_sense_pin,
    output wire        tp_motor_on,
    output wire        tp_write,
    output wire        tp_sense_pull,
    output wire [7:0]  tp_pin
);
    // ---------------------------------------------------------------- the app's settings (cpu_clk) into sys_clk
    wire [7:0]  ctrl;
    wire [2:0]  r_model, r_cpux;
    wire [3:0]  r_sel;
    wire [1:0]  r_spcpu;
    wire        r_diag, r_hz;
    wire [1:0]  r_phos, r_ramsz;
    wire [31:0] fb_base, fb_stride;
    wire        r_tplay, r_tseek;
    wire        tape_level, tape_playing, tape_eof, tape_stop, tape_err;
    wire [24:0] tape_pos;
    wire [7:0]  tape_under;
    wire [31:0] r_tbase;
    wire [24:0] r_tpos;
    wire [7:0]  r_tpin;
    (* ASYNC_REG = "TRUE" *) reg [2:0] tpl_s = 3'b000, tsk_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [7:0] tpn_s1 = 8'd0, tpn_s2 = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rdp_s = 2'b11, snp_s = 2'b11;
    reg [31:0] tbase_l = 32'd0;
    reg [24:0] tpos_l = 25'd0;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ctrl_s1 = 8'd0, ctrl_s2 = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [17:0] set_s1 = 18'd0, set_s2 = 18'd0;
    always @(posedge sys_clk) begin
        ctrl_s1 <= ctrl;
        ctrl_s2 <= ctrl_s1;
        set_s1 <= {r_spcpu, r_ramsz, r_phos, r_hz, r_diag, r_sel, r_cpux, r_model};
        set_s2 <= set_s1;
        tpl_s <= {tpl_s[1:0], r_tplay};
        tsk_s <= {tsk_s[1:0], r_tseek};
        tpn_s1 <= r_tpin;
        tpn_s2 <= tpn_s1;
        tbase_l <= r_tbase;
        tpos_l <= r_tpos;
        rdp_s <= {rdp_s[0], tp_read_pin};
        snp_s <= {snp_s[0], tp_sense_pin};
    end
    wire [2:0] model_s   = set_s2[2:0];
    wire [2:0] cpux_s    = set_s2[5:3];
    wire [3:0] rom_sel_s = set_s2[9:6];
    wire       diag_s    = set_s2[10];
    wire       hz50_s    = set_s2[11];
    wire [1:0] phos_s    = set_s2[13:12];
    wire [1:0] ramsz_s   = set_s2[15:14];
    wire [1:0] spcpu_s   = set_s2[17:16];

    // ---------------------------------------------------------------- reset and pause
    wire       rst_m = sys_rst | reset_req | ctrl_s2[0];
    reg        rst_q = 1'b1;
    reg        sreset = 1'b1;                       // the machine's reset: held until 32 CPU strobes have passed
    reg [5:0]  rcnt = 6'd0;
    reg [2:0]  model_l = 3'd0;                      // the model is taken in reset
    reg [1:0]  ramsz_l = 2'd2;
    reg [1:0]  spcpu_l = 2'd0;                      // the SuperPET's CPU switch is taken in reset too
    wire       hold_req = halt_req | ctrl_s2[1];
    wire       hold_eff = hold_req & ~sreset & ~rst_q;
    reg        held = 1'b0;
    reg [15:0] cacc = 16'd0, sacc = 16'd0;
    wire [15:0] cstep = 16'd2079 * ({13'd0, cpux_s} + 16'd1);
    wire [16:0] cnext = {1'b0, cacc} + {1'b0, cstep};
    wire [16:0] snext = {1'b0, sacc} + 17'd2079;
    wire       rdy  = cnext[16] & ~hold_eff;
    wire       slow = snext[16] & ~hold_eff;
    always @(posedge sys_clk) begin
        rst_q <= rst_m;
        held <= hold_eff;
        if (!hold_eff) begin
            cacc <= cnext[15:0];
            sacc <= snext[15:0];
        end
        if (rst_m) begin
            model_l <= model_s;
            ramsz_l <= ramsz_s;
            spcpu_l <= spcpu_s;
            rcnt <= 6'd0;
            sreset <= 1'b1;
        end else if (rdy) begin
            if (rcnt == 6'd32) sreset <= 1'b0;
            else rcnt <= rcnt + 6'd1;
        end
    end
    assign halted = held;
    assign running = ~sreset;

    wire m_crtc = (model_l >= 3'd3);

    // ---------------------------------------------------------------- the CPU and the bus (pet2001hw.v)
    // The SuperPET (Q13) has two CPUs on one bus: the 6502 and a 6809 (Greg Miller's mc6809is, cores/pet/rtl/src/mc6809); the one that is not selected is held in
    // reset and its bus is ignored. The 6809 steps on the same strobe `rdy` (one bus cycle each): Q falls on rdy, E one clock later, and E is what latches the data
    // and strobes the I/O (the 6502's strobe is rdy itself).
    wire [15:0] a, a65, a09;
    wire [7:0]  cpu_do, cpu_di, do65, do09;
    wire        rw, rw65, rw09, sync_o, irq;
    wire        act809;
    reg         e09 = 1'b0;
    always @(posedge sys_clk) e09 <= rdy;
    wire        bus_st = act809 ? e09 : rdy;               // one bus cycle's strobe, whichever CPU runs
    cpu6502 cpu (.A(a65), .RW(rw65), .DO(do65), .DI(cpu_di), .RDY(rdy), .SYNC(sync_o), .IRQ_(~irq), .NMI_(1'b1), .RES_(~(sreset | act809)), .PHI(sys_clk));
    mc6809is cpu09 (.CLK(sys_clk), .fallE_en(e09), .fallQ_en(rdy), .D(cpu_di), .DOut(do09), .ADDR(a09), .RnW(rw09), .BS(), .BA(),
                    .nIRQ(~irq), .nFIRQ(1'b1), .nNMI(1'b1), .AVMA(), .BUSY(), .LIC(), .nHALT(1'b1), .nRESET(~(sreset | ~act809)), .nDMABREQ(1'b1), .RegData());
    assign a = act809 ? a09 : a65;
    assign cpu_do = act809 ? do09 : do65;
    assign rw = act809 ? rw09 : rw65;
    wire we = ~rw;

    // 8096: the control register at $FFF0 (write only, b7 extra RAM on, b6 I/O peek-through, b5 screen peek-through, b3 / b2 pick the 16 KiB block of the extra RAM behind
    // $C000-$FFFF / $8000-$BFFF, b1 / b0 write protect that upper / lower block; VICE petmem.c FFF0_* and the table above store_8x96); 64 KiB
    wire dec_vram = (a[15:12] == 4'h8);
    wire dec_io   = (a[15:11] == 5'b11101);
    reg  [7:0] xreg = 8'd0;
    wire m_8096 = (model_l == 3'd4);
    wire x_hit  = m_8096 & xreg[7] & a[15] & ~(xreg[6] & dec_io) & ~(xreg[5] & dec_vram);
    wire x_wp   = a[14] ? xreg[1] : xreg[0];
    // the SuperPET's 64 KiB bank RAM (4 KiB behind $9000-$9FFF) lives in the same 64 KiB array: the models are exclusive
    wire m_spet = (model_l == 3'd5);
    wire [7:0] sp_q;
    wire       sp_hit, sp_ramen, sp_ramwp, sp_diag;
    wire [3:0] sp_bank;
    wire       s9_hit = m_spet & (a[15:12] == 4'h9) & sp_ramen;
    wire       xsel   = x_hit | s9_hit;
    wire [7:0] x_q;
    pet_ram64 xram (.clk(sys_clk), .addr(m_spet ? {sp_bank, a[11:0]} : {a[14], a[14] ? xreg[3] : xreg[2], a[13:0]}),
                    .we(we & (m_spet ? (s9_hit & ~sp_ramwp) : (x_hit & ~x_wp))), .d(cpu_do), .q(x_q));
    always @(posedge sys_clk) begin
        if (sreset) xreg <= 8'd0;
        else if (bus_st & we & m_8096 & (a == 16'hFFF0)) xreg <= cpu_do;
    end
    pet_spet spet (.clk(sys_clk), .rst(sreset), .en(m_spet), .cpu_set(spcpu_l), .st(bus_st), .addr(a), .we(we), .wdata(cpu_do), .hit(sp_hit), .q(sp_q),
                   .ramen(sp_ramen), .bank(sp_bank), .ramwp(sp_ramwp), .diag(sp_diag), .act809(act809));
    wire ram_ok = m_crtc | ramsz_l[1] | (ramsz_l[0] ? ~a[14] : (a[14:13] == 2'b00));
    wire sel_ram  = ~a[15];
    wire sel_vram = dec_vram & ~x_hit;
    wire sel_io   = dec_io & ~x_hit & ~sp_hit;
    wire [7:0] ram_q, rom_q, vc_q, vv_q, ch_q, w_do;
    wire [10:0] v_a, c_a;
    wire        w_wr, w_hit, w_rom;
    wire [3:0]  w_sel;
    wire [15:0] w_ai;
    wire [7:0]  w_di;
    wire [7:0] rom9_q;
    pet_mem mem (
        .clk(sys_clk), .m_addr(a[14:0]), .m_wdata(cpu_do), .m_we(we & sel_ram & ram_ok), .ram_q(ram_q), .r_addr(a[14:0]), .rom_q(rom_q), .r9_addr(a[14:0] - 15'h2000), .rom9_q(rom9_q),
        .vc_addr(m_crtc ? a[10:0] : {1'b0, a[9:0]}), .vc_we(we & sel_vram), .vc_q(vc_q), .vv_addr(v_a), .vv_q(vv_q), .ch_addr(c_a), .ch_q(ch_q),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di), .w_do(w_do)
    );

    // the I/O block: PIA1 $E810, PIA2 $E820, VIA $E840 (pet2001io.v, the CRTC at $E880 is here)
    wire [3:0] keyrow;
    wire [7:0] keyin, io_q;
    wire       v_blank, v_gfx, cb2_audio;
    wire [7:0] ieee_do, ieee_di;
    wire       ieee_atn_i, ieee_ndac_i, ieee_nrfd_i, ieee_dav_i, ieee_eoi_i;
    wire       ieee_atn_o, ieee_ndac_o, ieee_nrfd_o, ieee_dav_o, ieee_eoi_o, cass_motor_n, cass_write;
    wire       video_on;
    wire       sense_low = tpl_s[2] | (tpn_s2[4] & ~snp_s[1]);
    wire       cass_rd = tpn_s2[4] ? rdp_s[1] : tape_level;
    pet2001io io (
        .data_out(io_q), .data_in(cpu_do), .addr(a[7] ? 7'd0 : a[6:0]), .rdy(bus_st & sel_io), .we(we & sel_io), .irq(irq),
        .keyrow(keyrow), .keyin(keyin), .video_blank(v_blank), .video_gfx(v_gfx), .video_sync(video_on),
        .cass_motor_n(cass_motor_n), .cass_write(cass_write), .cass_sense_n(~sense_low), .cass_read(cass_rd),
        .ieee_do(ieee_do), .ieee_di(ieee_di),
        .ieee_atn_o(ieee_atn_o), .ieee_atn_i(ieee_atn_i), .ieee_ndac_o(ieee_ndac_o), .ieee_ndac_i(ieee_ndac_i),
        .ieee_nrfd_o(ieee_nrfd_o), .ieee_nrfd_i(ieee_nrfd_i), .ieee_dav_o(ieee_dav_o), .ieee_dav_i(ieee_dav_i),
        .ieee_srq_i(1'b1), .ieee_eoi_o(ieee_eoi_o), .ieee_eoi_i(ieee_eoi_i),
        .audio(cb2_audio), .diag_l(~(diag_s | sp_diag)), .slow_clock(slow), .clk(sys_clk), .reset(sreset)
    );
    // cassette 1: the app's .tap as a pulse stream in the DDR2 (zxcpc_tape.v, one tick per microsecond of the PET's 1 MHz clock, none while held);
    // the motor (PIA1 CB2 low) gates the play bit, so the tape stands still while the PET stops the motor
    wire        motor_on = ~cass_motor_n;
    pet_tape tape (
        .clk(sys_clk), .rst(rst_q), .hold(held), .tick(slow), .play(tpl_s[2] & motor_on), .base(tbase_l),
        .seek_tgl(tsk_s[2]), .seek_pos(tpos_l), .level(tape_level), .playing(tape_playing), .eof(tape_eof), .stopmark(tape_stop),
        .pos_o(tape_pos), .underrun(tape_under), .ddr_err(tape_err),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid)
    );
    assign tp_motor_on = motor_on;
    assign tp_write = cass_write;
    assign tp_sense_pull = tpl_s[2];
    assign tp_pin = tpn_s2;
    pet_keys keyx (.keys(keys), .bus(m_crtc), .keyrow(keyrow), .keyin(keyin));

    // the CRTC register file (6545 / 6845): index at $E880, data at $E881
    reg [4:0] crtc_ar = 5'd0;
    reg [7:0] r1 = 8'd80, r10 = 8'h20, r11 = 8'd0, r12 = 8'd0, r13 = 8'd0, r14 = 8'd0, r15 = 8'd0;
    wire      sel_crtc = sel_io & a[7] & m_crtc & (a[6:1] == 6'd0);
    wire      vsync_stat;
    always @(posedge sys_clk) begin
        if (sreset) begin
            crtc_ar <= 5'd0;
            r1 <= 8'd80; r10 <= 8'h20; r11 <= 8'd0; r12 <= 8'd0; r13 <= 8'd0; r14 <= 8'd0; r15 <= 8'd0;
        end else if (bus_st & we & sel_crtc) begin
            if (!a[0]) crtc_ar <= cpu_do[4:0];
            else case (crtc_ar)
                5'd1:  r1 <= cpu_do;
                5'd10: r10 <= cpu_do;
                5'd11: r11 <= cpu_do;
                5'd12: r12 <= cpu_do;
                5'd13: r13 <= cpu_do;
                5'd14: r14 <= cpu_do;
                5'd15: r15 <= cpu_do;
                default: ;
            endcase
        end
    end
    wire [7:0] crtc_q = !a[0] ? {1'b1, 1'b0, ~video_on, 5'd0} : (crtc_ar == 5'd14) ? {2'b00, r14[5:0]} : (crtc_ar == 5'd15) ? r15 : 8'h00;

    wire [7:0] io_data = a[7] ? (sel_crtc ? crtc_q : 8'hFF) : (a[6:4] == 3'd0) ? 8'hFF : io_q;
    assign cpu_di = xsel ? x_q : sel_ram ? (ram_ok ? ram_q : 8'hFF) : sel_vram ? vc_q : sel_io ? io_data : sp_hit ? sp_q : (act809 & a[15] & (a[14] | a[13])) ? rom9_q : rom_q;

    // ---------------------------------------------------------------- the app's window and the JTAG port
    pet_win win (
        .clk(sys_clk),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(dbg_busy), .rom_sel(rom_sel_s),
        .port_use(win_port_use), .port_addr(win_port_addr), .port_rom(win_port_rom), .port_we(win_port_we),
        .port_wdata(win_port_wdata), .port_rdata(w_do),
        .p_req(port_req), .p_addr(port_addr), .p_rom(port_rom), .p_we(port_we), .p_wdata(port_wdata),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di)
    );
    // the read data is valid in the clock after the request and stays until the next request's
    reg       req_q = 1'b0;
    reg [7:0] rd_hold = 8'd0;
    always @(posedge sys_clk) begin
        req_q <= port_req;
        if (req_q) rd_hold <= w_do;
    end
    assign ram_rdata = req_q ? w_do : rd_hold;

    // ---------------------------------------------------------------- picture
    wire [15:0] rgb565;
    wire        hblank, vblank, dotst, frame_end;
    pet_vid vid (
        .clk(sys_clk), .hz50(hz50_s), .phos(phos_s), .crtc_m(m_crtc), .r1(r1), .start({r12[3:0], r13}), .cur({r14[5:0], r15}), .r10(r10), .r11(r11[4:0]),
        .gfx(v_gfx), .blank(v_blank), .v_a(v_a), .v_q(vv_q), .c_a(c_a), .c_q(ch_q),
        .hblank(hblank), .vblank(vblank), .video_on(video_on), .dotst(dotst), .rgb565(rgb565), .frame_end(frame_end)
    );
    wire        pw_quiet, frame_tgl;
    wire [7:0]  fb_latest, fb_seq;
    wire [15:0] fb_over;
    pet_pixw pixw (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .dotst(dotst), .rgb565(rgb565), .hblank(hblank), .vblank(vblank),
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

    // ---------------------------------------------------------------- sound: CB2 of the VIA
    pet_audio audio (.clk(sys_clk), .a_in(cb2_audio ? 16'sd3000 : -16'sd3000), .mute(held), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // ---------------------------------------------------------------- registers
    reg [15:0] frames = 16'd0;
    always @(posedge sys_clk) if (frame_end) frames <= frames + 16'd1;
    (* ASYNC_REG = "TRUE" *) reg [2:0] ft_s = 3'b000;
    always @(posedge cpu_clk) ft_s <= {ft_s[1:0], frame_tgl};
    wire ev_frame = ft_s[2] ^ ft_s[1];
    wire col80 = m_crtc && (r1 >= 8'd60);
    // the drives' registers and flags (used by pet_regs below and pet_ieee)
    wire       d_en, d_4040, d_led_e;
    wire [1:0] d_ro, d_mnt, d_clr, d_dirty, d_led;
    wire [31:0] d_sz0, d_sz1, d_bs0, d_bs1;
    wire       d_ev_tgl, d_ev_unit;
    wire [7:0] d_ev_chunk;
    wire [7:0] status = {5'd0, col80, held, rst_q | sreset};
    pet_regs regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(ctrl), .model(r_model), .cpu_x(r_cpux), .diag(r_diag), .hz50(r_hz), .phos(r_phos), .ram_sz(r_ramsz), .rom_sel(r_sel), .sp_cpu(r_spcpu),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .status(status), .frames(frames), .cpuaddr(a),
        .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over),
        .d_en(d_en), .d_4040(d_4040), .d_ro(d_ro), .d_mnt_tgl(d_mnt), .d_clr_tgl(d_clr), .d_size0(d_sz0), .d_size1(d_sz1), .d_base0(d_bs0), .d_base1(d_bs1),
        .d_status({d_led, d_dirty}), .d_ev_tgl(d_ev_tgl), .d_ev_chunk(d_ev_chunk), .d_ev_unit(d_ev_unit),
        .t_play(r_tplay), .t_base(r_tbase), .t_seek_pos(r_tpos), .t_seek_tgl(r_tseek), .t_pin(r_tpin),
        .t_pos(tape_pos), .t_stat({tape_err, tape_stop, tape_eof, tape_playing}), .t_under(tape_under),
        .t_lines({cass_write, cass_rd, sense_low, motor_on})
    );

    // ---------------------------------------------------------------- the disk drives: the MiSTer 4040 / 8250 dual unit as device 8 (pet_ieee.sv)
    // the app puts the drive ROMs through the window (rom_sel 4-7, write only) and the disk images into DDR2 (pet_regs 0D0-0E7)
    wire       dr_wr = w_wr & w_hit & w_rom & (w_sel[3:2] == 2'b01);
    pet_ieee ieee (
        .sys_clk(sys_clk), .cpu_clk(cpu_clk), .rst(sreset),
        .p_data_o(ieee_do), .p_atn_o(ieee_atn_o), .p_ndac_o(ieee_ndac_o), .p_nrfd_o(ieee_nrfd_o), .p_dav_o(ieee_dav_o), .p_eoi_o(ieee_eoi_o),
        .p_data_i(ieee_di), .p_atn_i(ieee_atn_i), .p_ndac_i(ieee_ndac_i), .p_nrfd_i(ieee_nrfd_i), .p_dav_i(ieee_dav_i), .p_eoi_i(ieee_eoi_i),
        .r_en(d_en), .r_4040(d_4040), .r_size0(d_sz0), .r_size1(d_sz1), .r_ro(d_ro), .r_mnt_tgl(d_mnt), .r_base0(d_bs0), .r_base1(d_bs1), .r_clr_dirty_tgl(d_clr),
        .s_dirty(d_dirty), .s_led(d_led), .s_led_err(d_led_e), .ev_tgl(d_ev_tgl), .ev_chunk(d_ev_chunk), .ev_unit(d_ev_unit),
        .rom_wr(dr_wr), .rom_sel(~w_sel[1]), .rom_addr({w_sel[0], w_ai[13:0]}), .rom_data(w_di),
        .drv_tgl(drv_tgl), .drv_addr(drv_addr), .drv_we(drv_we), .drv_wdata(drv_wdata), .drv_be(drv_be), .drv_done_tgl(drv_done_tgl), .drv_rdata(drv_rdata)
    );


    // ---------------------------------------------------------------- JTAG proof of life (trxdbg "trx 0 ..."), words:
    //  0 "PET1"  1 status: b0 in reset, b1 held, b2 reset_req, b3 ctrl reset hold, b4 sys_rst, b5 halt_req, b6 ctrl halt, b7 left reset once
    //  2 frames  3 CPU address now  4 CPU strobes counted  5 PIA/VIA IRQ seen count  6 model in use
    reg [15:0] rdy_cnt = 16'd0, irq_cnt = 16'd0;
    reg        irq_d = 1'b0, seen_run = 1'b0;
    always @(posedge sys_clk) begin
        if (rdy) rdy_cnt <= rdy_cnt + 16'd1;
        irq_d <= irq;
        if (irq & ~irq_d) irq_cnt <= irq_cnt + 16'd1;
        if (!sreset) seen_run <= 1'b1;
    end
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h31544550;
            4'h1: jd = {24'd0, seen_run, ctrl_s2[1], halt_req, sys_rst, ctrl_s2[0], reset_req, held, rst_q | sreset};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, a};
            4'h4: jd = {16'd0, rdy_cnt};
            4'h5: jd = {16'd0, irq_cnt};
            4'h6: jd = {29'd0, model_l};
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
    wire _unused = &{1'b0, d_led_e, sync_o, ieee_atn_o, dbg_re, pw_quiet, ieee_ndac_o, ieee_nrfd_o, ieee_dav_o, ieee_eoi_o, vsync_stat, 1'b0};
endmodule
