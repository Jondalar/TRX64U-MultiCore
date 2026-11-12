// TRX ZX Spectrum core (T91): the 48K and 128K Spectrum behind the host's layer-2 socket. The machine is MiSTer-devel/ZX-Spectrum_MiSTer's
// ULA (cores/zx/rtl/ula.sv, GPL v3), Z80 (T80pa, GHDL-converted to cores/zx/gen/t80pa.v) and AY (ym2149.sv), wired the way
// ZX-Spectrum.sv wires them but without the framework: the memories are block RAM (zx_mem), the keyboard is the host's C64 matrix
// through the shared translator (zx_keys), the tape is the shared pulse engine (zxcpc_tape), the picture is the 4-bit index of the
// host's palette path. This entity is what cores/zx/layer2_zx.v wraps; it has no C64 part in it.
//
// Clock (D4): everything runs on sys_clk (the host's C64 clock, 31.527778 MHz PAL) with clock enables from one accumulator
// (zxcpc_nco). ce_7mn is the pixel clock (7 MHz class), ce_7mp the same enable two clocks later (the ULA wants both phases of its 7 MHz
// clock; its CPU-clock enables come from it with the contention). The rate is fitted so that one Spectrum frame is exactly the
// scaler's frame of 628,992 sys_clk: 48K pixel x 2/9 (139,776 pixel clocks), 128K x 5909/26208 (141,816). The machine runs
// +0.089 % (48K) and +0.207 % (128K) fast; its video, the AY and the tape follow the same enable.
// Freeze (D12): the request is taken at the next frame boundary (the ULA's VSync rising edge); all enables then stop together. The
// resume waits for the frame counter `fc` to be at the value it had at the stop, so the machine restarts on the very clock a
// never-paused run would have had it, and the scaler's lock holds.
// Picture (T119): the ULA's I,G,R,B of every pixel enable (ce_7mp) as RGB565 into a DDR2 frame buffer of the host's pixel layer through
// the general port e_* (zx_pixw: 344 x 280, the ULA's visible raster, four buffers). The host's VIC-II raster input (pix_*) gets a dummy
// raster of the scaler's size (504 dots of 4 clocks, 312 lines, colour 0, never stops: the scaler locks to it, as for the C264).
`timescale 1ns / 1ps
module zx_core #(
    parameter integer ID_TRX2 = 0
) (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        cpu_clk,                     // the app's bus (100 MHz)
    input  wire        mclk,                        // audio
    input  wire        ui_clk,                      // the host's DDR2 user clock
    input  wire        ui_rst,
    input  wire        reset_req,                   // the app's machine reset (sys_clk)
    input  wire        halt_req,                    // the app's pause (sys_clk)
    output wire        halted,
    output wire        running,

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

    // the host's real-time DDR2 port c_* (ui_clk): the tape's pulse stream
    output wire [26:0] c_addr,
    output wire [2:0]  c_cmd,
    output wire        c_en,
    input  wire        c_rdy,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,

    // the host's general DDR2 port e_* (ui_clk): the frame buffer of the pixel layer
    output wire [26:0] e_addr,
    output wire [2:0]  e_cmd,
    output wire        e_en,
    input  wire        e_rdy,
    output wire [127:0] e_wdf_data,
    output wire [15:0] e_wdf_mask,
    output wire        e_wdf_wren
);
    // ================================================================ the app's settings (cpu_clk) into sys_clk
    wire [7:0]  r_ctrl;
    wire        r_model, r_snapgo, r_tplay, r_tseek;
    wire [2:0]  r_winpage;
    wire [2:0]  r_j1, r_j2, r_border;
    wire [215:0] r_dir;
    wire [7:0]  r_7ffd;
    wire [127:0] r_ay;
    wire [3:0]  r_aysel;
    wire [31:0] r_tbase;
    wire [24:0] r_tpos;
    wire [39:0] r_inject;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ctrl_s1 = 8'd0, ctrl_s2 = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg [10:0] set_s1 = 11'd0, set_s2 = 11'd0;     // {tape play, win page and ROM bank, joy2 mode, joy1 mode, model}
    (* ASYNC_REG = "TRUE" *) reg [2:0] go_s = 3'b000, sk_s = 3'b000;
    reg [39:0] inj_l = 40'd0;
    always @(posedge sys_clk) begin
        ctrl_s1 <= r_ctrl;
        ctrl_s2 <= ctrl_s1;
        set_s1 <= {r_tplay, r_winpage, r_j2, r_j1, r_model};
        set_s2 <= set_s1;
        go_s <= {go_s[1:0], r_snapgo};
        sk_s <= {sk_s[1:0], r_tseek};
        inj_l <= r_inject;                          // quasi static: the app sets it for a key press that lasts frames
    end
    wire       model_s  = set_s2[0];
    wire [2:0] joy1_mode = set_s2[3:1];
    wire [2:0] joy2_mode = set_s2[6:4];
    wire [2:0] winpage_s = set_s2[9:7];
    wire       tplay_s   = set_s2[10];
    wire       go_ev     = go_s[2] ^ go_s[1];

    // ================================================================ reset
    reg        snap_mask = 1'b0;                    // a snapshot load took the machine out of the app's reset hold
    wire       rst_core = sys_rst | reset_req | (ctrl_s2[0] & ~snap_mask);
    reg        rst_q = 1'b1;
    reg [7:0]  rcnt = 8'd0;
    reg        m128_l = 1'b0;
    always @(posedge sys_clk) begin
        rst_q <= rst_core;
        if (rst_core) begin rcnt <= 8'd0; m128_l <= model_s; end
        else if (rcnt != 8'hFF) rcnt <= snap_mask ? 8'hFF : rcnt + 8'd1;   // a snapshot load ends the stretch at once (the CPU's reset is released by its FSM)
    end
    reg        snap_cpu_hold = 1'b0, snap_blk = 1'b0, snap_dirset = 1'b0, snap_busy = 1'b0;
    wire       cpu_reset = rst_q | snap_cpu_hold | (rcnt != 8'hFF && !snap_mask);

    // ================================================================ the clock enables
    reg        hold_q = 1'b0;
    wire       ce_pix;
    zxcpc_nco #(.W(15)) nco (
        .clk(sys_clk), .hold(hold_q), .restart(1'b0),
        .num(m128_l ? 15'd5909 : 15'd2), .den(m128_l ? 15'd26208 : 15'd9), .en(ce_pix)
    );
    wire       ce_7mn = ce_pix;
    reg [1:0]  mp_d = 2'b00;
    always @(posedge sys_clk) mp_d <= {mp_d[0], ce_pix};
    wire       ce_7mp = mp_d[1];
    reg [2:0]  ph = 3'd7;
    always @(posedge sys_clk) begin
        if (ce_pix) ph <= 3'd0;
        else if (ph != 3'd7) ph <= ph + 3'd1;
    end
    reg [1:0]  aydiv = 2'd0;
    reg        tdiv = 1'b0;
    always @(posedge sys_clk) if (ce_pix) begin aydiv <= aydiv + 2'd1; tdiv <= ~tdiv; end
    wire       ce_ay = ce_pix & (aydiv == 2'd3);
    wire       ce_tape = ce_pix & tdiv;             // one per T-state (the pixel clock is twice the CPU clock)

    // ================================================================ the machine
    wire        ce_cpu_sp, ce_cpu_sn;
    wire [15:0] cpu_a;
    wire [7:0]  cpu_do, cpu_di;
    wire        nM1, nMREQ, nIORQ, nRD, nWR, nRFSH, nINT;
    wire        io_wr = ~nIORQ & ~nWR & nM1;
    wire        io_rd = ~nIORQ & ~nRD & nM1;
    wire [211:0] cpu_reg;
    wire [211:0] snap_regs;

    T80pa cpu (
        .RESET_n(~cpu_reset), .CLK(sys_clk), .CEN_p(ce_cpu_sp & ~snap_blk), .CEN_n(ce_cpu_sn & ~snap_blk),
        .WAIT_n(1'b1), .INT_n(nINT), .NMI_n(1'b1), .BUSRQ_n(1'b1), .OUT0(1'b0),
        .DI(cpu_di), .R800_mode(1'b0), .DIRSet(snap_dirset), .DIR(snap_regs),
        .M1_n(nM1), .MREQ_n(nMREQ), .IORQ_n(nIORQ), .RD_n(nRD), .WR_n(nWR), .RFSH_n(nRFSH), .HALT_n(), .BUSAK_n(),
        .A(cpu_a), .DO(cpu_do), .REG(cpu_reg)
    );

    // ---- paging (the 128K port $7FFD) and the ULA port $FE
    reg [7:0]  page_reg = 8'd0;
    reg [2:0]  border_color = 3'd0;
    reg        ear_out = 1'b0, mic_out = 1'b0;
    reg        old_wr = 1'b0;
    wire       page_disable = ~m128_l | page_reg[5];
    wire       page_write = ~cpu_a[15] & ~cpu_a[1] & ~page_disable;
    reg        snap_setregs = 1'b0;
    reg [7:0]  snap_p7 = 8'd0;
    reg [2:0]  snap_bd = 3'd0;
    always @(posedge sys_clk) begin
        old_wr <= io_wr;
        if (rst_q) begin
            page_reg <= 8'd0; ear_out <= 1'b0; mic_out <= 1'b0;
        end else begin
            if (io_wr & ~old_wr) begin
                if (page_write) page_reg <= cpu_do;
                if (~cpu_a[0]) begin border_color <= cpu_do[2:0]; ear_out <= cpu_do[4]; mic_out <= cpu_do[3]; end
            end
            if (snap_setregs) begin page_reg <= snap_p7; border_color <= snap_bd; end
        end
    end
    wire [2:0] page_ram = page_reg[2:0];
    wire       page_scr = page_reg[3];

    // ---- the ULA
    wire        HSync, VSync, HBlank, VBlank, I, R, G, B;
    wire [14:0] vram_addr;
    wire [7:0]  vram_dout, port_ff;
    wire nMREQ_u = nMREQ;
    ULA ula (
        .reset(rst_q), .clk_sys(sys_clk), .ce_7mp(ce_7mp), .ce_7mn(ce_7mn), .ce_cpu_sp(ce_cpu_sp), .ce_cpu_sn(ce_cpu_sn),
        .addr(cpu_a), .din(cpu_do), .nMREQ(nMREQ_u), .nIORQ(nIORQ), .nRFSH(nRFSH), .nRD(nRD), .nWR(nWR), .nINT(nINT),
        .vram_addr(vram_addr), .vram_dout(vram_dout), .port_ff(port_ff),
        .ulap_avail(1'b0), .ulap_sel(), .ulap_dout(), .ulap_ena(), .ulap_mono(), .ulap_color(),
        .tmx_avail(1'b0), .mode512(),
        .snow_ena(1'b0), .mZX(1'b1), .m128(m128_l), .scorp_tim(1'b0), .page_scr(page_scr), .page_ram(page_ram),
        .border_color(border_color), .wide(2'b00), .overscan(2'b00),
        .HSync(HSync), .VSync(VSync), .HBlank(HBlank), .VBlank(VBlank), .I(I), .R(R), .G(G), .B(B)
    );

    // ---- memory
    wire [7:0] ram_q, rom_q;
    wire       w_start, w_we, w_rom, w_done, mem_pend;
    wire [16:0] w_a;
    wire [7:0] w_di, w_do;
    wire [2:0] bank = (cpu_a[15:14] == 2'b01) ? 3'd5 : (cpu_a[15:14] == 2'b10) ? 3'd2 : (m128_l ? page_ram : 3'd0);
    wire       rom_page = m128_l & page_reg[4];
    zx_mem mem (
        .clk(sys_clk), .ph(ph),
        .cpu_ra({bank, cpu_a[13:0]}), .cpu_we(~nMREQ & ~nWR & (cpu_a[15] | cpu_a[14])), .cpu_d(cpu_do), .ram_q(ram_q),
        .rom_a({rom_page, cpu_a[13:0]}), .rom_q(rom_q),
        .ula_a({vram_addr[14] ? 3'd7 : 3'd5, vram_addr[13:0]}), .ula_q(vram_dout),
        .w_start(w_start), .w_we(w_we), .w_rom(w_rom), .w_a(w_a), .w_di(w_di), .busy(mem_pend), .w_done(w_done), .w_do(w_do)
    );
    zx_win win (
        .clk(sys_clk),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(dbg_busy), .page(winpage_s),
        .port_use(win_port_use), .port_addr(win_port_addr), .port_rom(win_port_rom), .port_we(win_port_we),
        .port_wdata(win_port_wdata), .p_req(1'b0),
        .w_start(w_start), .w_we(w_we), .w_rom(w_rom), .w_a(w_a), .w_di(w_di), .w_done(w_done), .w_do(w_do)
    );

    // ---- keyboard and joysticks
    wire [39:0] zx_mat;
    zx_keys keyx (.keys(keys), .zx_mat(zx_mat));
    function [39:0] joymat(input [4:0] j, input [2:0] mode);   // j: 0 up, 1 down, 2 left, 3 right, 4 fire (the host's order)
        begin
            joymat = 40'd0;
            case (mode)
                3'd2: begin joymat[24] = j[2]; joymat[23] = j[3]; joymat[22] = j[1]; joymat[21] = j[0]; joymat[20] = j[4]; end   // Sinclair 1: 6 7 8 9 0
                3'd3: begin joymat[15] = j[2]; joymat[16] = j[3]; joymat[17] = j[1]; joymat[18] = j[0]; joymat[19] = j[4]; end   // Sinclair 2: 1 2 3 4 5
                3'd4: begin joymat[19] = j[2]; joymat[24] = j[1]; joymat[23] = j[0]; joymat[22] = j[3]; joymat[20] = j[4]; end   // Cursor: 5 6 7 8 0
                default: ;
            endcase
        end
    endfunction
    wire [39:0] mat = zx_mat | inj_l | joymat(joy1, joy1_mode) | joymat(joy2, joy2_mode);
    reg  [4:0]  kbd = 5'h1F;
    integer kr;
    always @* begin
        kbd = 5'h1F;
        for (kr = 0; kr < 8; kr = kr + 1)
            if (!cpu_a[8 + kr]) kbd = kbd & ~mat[5 * kr +: 5];
    end
    wire [4:0] kj1 = (joy1_mode == 3'd1) ? joy1 : 5'd0;
    wire [4:0] kj2 = (joy2_mode == 3'd1) ? joy2 : 5'd0;
    wire [4:0] kj  = kj1 | kj2;
    wire [7:0] kemp = {3'b000, kj[4], kj[0], kj[1], kj[2], kj[3]};   // Kempston: bit 0 right, 1 left, 2 down, 3 up, 4 fire
    wire       kemp_sel = (cpu_a[7:0] == 8'h1F);

    // ---- AY (128K only): ports $FFFD (select, read) and $BFFD (write)
    wire        psg_sel = m128_l & cpu_a[15] & ~cpu_a[1];
    wire        psg_we  = psg_sel & io_wr;
    wire        psg_rd  = psg_sel & cpu_a[14];
    wire [7:0]  psg_do;
    wire [7:0]  ay_a, ay_b, ay_c;
    reg         sn_bdir = 1'b0, sn_bc = 1'b0, sn_ay_act = 1'b0;
    reg  [7:0]  sn_di = 8'd0;
    YM2149 ay (
        .CLK(sys_clk), .CE(ce_ay), .RESET(rst_q),
        .BDIR(sn_ay_act ? sn_bdir : psg_we), .BC(sn_ay_act ? sn_bc : cpu_a[14]), .DI(sn_ay_act ? sn_di : cpu_do), .DO(psg_do),
        .CHANNEL_A(ay_a), .CHANNEL_B(ay_b), .CHANNEL_C(ay_c), .SEL(1'b0), .MODE(1'b0), .ACTIVE(),
        .IOA_in(8'hFF), .IOA_out(), .IOB_in(8'hFF), .IOB_out()
    );

    // ---- tape
    wire        tape_level, tape_playing, tape_eof, tape_stop, tape_err;
    wire [24:0] tape_pos;
    wire [7:0]  tape_under;
    reg  [31:0] tbase_l = 32'd0;
    reg  [24:0] tpos_l = 25'd0;
    always @(posedge sys_clk) begin tbase_l <= r_tbase; tpos_l <= r_tpos; end
    wire [26:0] t_addr;
    wire [2:0]  t_cmd;
    zxcpc_tape tape (
        .clk(sys_clk), .rst(rst_q), .hold(hold_q), .tick(ce_tape), .play(tplay_s), .base(tbase_l),
        .seek_tgl(sk_s[2]), .seek_pos(tpos_l), .level(tape_level), .playing(tape_playing), .eof(tape_eof), .stopmark(tape_stop),
        .pos_o(tape_pos), .underrun(tape_under), .ddr_err(tape_err),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .c_addr(t_addr), .c_cmd(t_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid)
    );
    assign c_addr = t_addr;
    assign c_cmd = t_cmd;

    // ---- the CPU's data bus
    wire [7:0] mem_d = (cpu_a[15:14] == 2'b00) ? rom_q : ram_q;
    wire [7:0] io_d  = kemp_sel ? kemp : psg_rd ? psg_do : ~cpu_a[0] ? {1'b1, tape_level, 1'b1, kbd} : port_ff;
    assign cpu_di = ~nMREQ ? mem_d : ~io_rd ? port_ff : io_d;

    // ================================================================ Freeze (D12)
    reg        vs_d = 1'b0;
    reg [19:0] fc = 20'd0, fv = 20'd0;
    wire       vs_rise = VSync & ~vs_d;
    wire       hold_req = halt_req | ctrl_s2[1];
    always @(posedge sys_clk) begin
        vs_d <= VSync;
        fc <= (fc == 20'd628991) ? 20'd0 : fc + 20'd1;
        if (vs_rise) fv <= fc;
        if (rst_core) hold_q <= 1'b0;                // a reset takes the hold away (the menu's Reset works while frozen)
        else if (!hold_q) begin if (hold_req && vs_rise) hold_q <= 1'b1; end
        else if (!hold_req && fc == fv) hold_q <= 1'b0;
    end
    wire pw_quiet;
    wire quiet = ~mem_pend & ~w_start & pw_quiet;
    assign halted = hold_q & quiet;
    assign running = ~cpu_reset;

    // ================================================================ the snapshot load (D10)
    // While the app holds the reset (CTRL.b0) a write to SNAP_GO takes the peripherals out of it, keeps the CPU in reset and without
    // clock, writes the AY's registers and the paging and border ports, then releases the CPU in the clock where T80pa loads its
    // register image (DIRSet). The app clears CTRL.b0 afterwards (the mask stays until it does).
    reg [215:0] dir_l = 216'd0;
    reg [127:0] ay_l = 128'd0;
    reg [4:0]   sst = 5'd0;
    reg [3:0]   sidx = 4'd0;
    reg [3:0]   aysel_l = 4'd0;
    assign snap_regs = {dir_l[211:0]};
    always @(posedge sys_clk) begin
        snap_setregs <= 1'b0;
        snap_dirset <= 1'b0;
        if (!ctrl_s2[0]) snap_mask <= 1'b0;
        case (sst)
        5'd0: if (go_ev && ctrl_s2[0] && !snap_busy) begin
            dir_l <= r_dir; ay_l <= r_ay; snap_p7 <= r_7ffd; snap_bd <= r_border; aysel_l <= r_aysel;
            snap_mask <= 1'b1; snap_cpu_hold <= 1'b1; snap_blk <= 1'b1; snap_busy <= 1'b1; sst <= 5'd1; sidx <= 4'd0;
        end
        5'd1: begin sn_ay_act <= 1'b1; sst <= 5'd2; end                       // rst_q has fallen by now (two clocks)
        5'd2: begin sn_ay_act <= 1'b1; sn_bdir <= 1'b0; sn_bc <= 1'b0; sst <= 5'd3; end
        5'd3: begin sn_bdir <= 1'b1; sn_bc <= 1'b1; sn_di <= {4'd0, sidx}; sst <= 5'd4; end      // select register sidx
        5'd4: begin sn_bdir <= 1'b0; sst <= 5'd5; end
        5'd5: begin sn_bdir <= 1'b1; sn_bc <= 1'b0; sn_di <= ay_l[8 * sidx +: 8]; sst <= 5'd6; end   // write it
        5'd6: begin
            sn_bdir <= 1'b0;
            if (sidx == 4'd15) sst <= 5'd7; else begin sidx <= sidx + 4'd1; sst <= 5'd3; end
        end
        5'd7: begin sn_bdir <= 1'b1; sn_bc <= 1'b1; sn_di <= {4'd0, aysel_l}; sst <= 5'd8; end     // leave the selected register as it was
        5'd8: begin sn_bdir <= 1'b0; sn_ay_act <= 1'b0; snap_setregs <= 1'b1; sst <= 5'd9; end
        5'd9: begin snap_cpu_hold <= 1'b0; sst <= 5'd10; end                    // RESET_n high
        5'd10: begin snap_dirset <= 1'b1; sst <= 5'd11; end                     // DIRSet for one clock
        5'd11: begin sst <= 5'd12; end
        5'd12: begin snap_blk <= 1'b0; snap_busy <= 1'b0; sst <= 5'd0; end
        default: sst <= 5'd0;
        endcase
        if (sys_rst) begin sst <= 5'd0; snap_cpu_hold <= 1'b0; snap_blk <= 1'b0; snap_busy <= 1'b0; sn_ay_act <= 1'b0; end
    end

    // ================================================================ picture
    // The pixel of every ce_7mp, with the blanking of the same clock: the frame buffer window starts at the first pixel after HBlank
    // (zx_pixw). y0 is the number of lines between the end of VBlank and the first row: the 128K has a line less in the frame, so the
    // window starts a line earlier and the paper sits at row 44 in both models (tb_zx pixw).
    wire       HBlank_s = HBlank;
    reg        hb_p = 1'b1;
    wire       line_start = hb_p & ~HBlank_s;
    wire       vis = ~HBlank_s & ~VBlank;
    always @(posedge sys_clk) if (ce_7mp) hb_p <= HBlank_s;
    // {I,G,R,B} as the Spectrum's colours: a set colour bit is 215 of 255, with the bright bit 255 (bright black stays black)
    wire [4:0] lv5 = I ? 5'd31 : 5'd26;
    wire [5:0] lv6 = I ? 6'd63 : 6'd53;
    wire [15:0] rgb565 = {R ? lv5 : 5'd0, G ? lv6 : 6'd0, B ? lv5 : 5'd0};
    reg  [15:0] rgb_d = 16'd0;
    always @(posedge sys_clk) if (ce_7mp) rgb_d <= rgb565;
    wire [15:0] rgb_w = rgb565;
    wire [7:0] fb_latest, fb_seq;
    wire [15:0] fb_over;
    wire       fb_tgl;
    wire [31:0] fb_base, fb_stride;
    wire [15:0] fb_lines, fb_acc, fb_uidbg;
    zx_pixw pixw (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .dotst(ce_7mp), .y0(m128_l ? 10'd2 : 10'd3), .rgb565(rgb_w), .hblank(HBlank_s), .vblank(VBlank),
        .quiet(pw_quiet), .frame_tgl(fb_tgl), .latest(fb_latest), .seq(fb_seq), .overruns(fb_over),
        .lines_out(fb_lines), .e_acc(fb_acc), .ui_dbg(fb_uidbg),
        .fb_base(fb_base), .fb_stride(fb_stride),
        .ui_clk(ui_clk), .ui_rst(ui_rst),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );

    // the raster the host's scaler locks to: 504 dots of 4 clocks, 312 lines, colour 0; not tied to the ULA, never stops
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
    // the picture path as the board can read it (JTAG words 5-7), free running 16 bit counters
    reg [15:0] d_ls = 16'd0, d_vis = 16'd0;
    always @(posedge sys_clk) if (ce_7mp) begin
        if (line_start) d_ls <= d_ls + 16'd1;
        if (vis) d_vis <= d_vis + 16'd1;
    end

    // T119 5.10 (board 2026-10-24, 48K): the same edge seen at the boundary of the ULA, to tell a ULA whose HBlank does not move (rises
    // missing) from a line start that is lost after it (rises present): pixel enables, HBlank rises, dots with HBlank high (JTAG words 10, 11)
    reg [15:0] d_ce = 16'd0, d_hbr = 16'd0, d_hbh = 16'd0;
    always @(posedge sys_clk) begin
        if (ce_pix) d_ce <= d_ce + 16'd1;
        if (ce_7mp) begin
            if (~hb_p & HBlank_s) d_hbr <= d_hbr + 16'd1;
            if (HBlank_s) d_hbh <= d_hbh + 16'd1;
        end
    end

    // T119 5.14 (board 2026-11-02, 48K: HBlank never rises while the ULA's frames, INT and CPU run): the ULA's other edges, counted from the outside
    // at ce_pix, to tell a line that is not 448 / 456 dots long (hc compare) from a line of the right length whose HBlank is dead (JTAG words 12, 13):
    // HSync rises, dots between the last two HSync rises, dots HSync was high in the last line, HSync rises between the last two VSync rises
    reg        hs_d = 1'b0, vs2_d = 1'b0;
    reg [15:0] hs_n = 16'd0, hs_per = 16'd0, hs_cnt = 16'd0, hs_len = 16'd0, hs_run = 16'd0, ln_pf = 16'd0, ln_cnt = 16'd0;
    always @(posedge sys_clk) if (ce_pix) begin
        hs_d <= HSync; vs2_d <= VSync;
        hs_cnt <= hs_cnt + 16'd1;
        if (HSync) hs_run <= hs_run + 16'd1;
        if (~HSync & hs_d) begin hs_len <= hs_run; hs_run <= 16'd0; end
        if (HSync & ~hs_d) begin hs_n <= hs_n + 16'd1; hs_per <= hs_cnt + 16'd1; hs_cnt <= 16'd0; ln_cnt <= ln_cnt + 16'd1; end
        if (VSync & ~vs2_d) begin ln_pf <= ln_cnt + {15'd0, HSync & ~hs_d}; ln_cnt <= 16'd0; end
    end

    // ================================================================ sound
    wire [15:0] ay_sum = {8'd0, ay_a} + {8'd0, ay_b} + {8'd0, ay_c};
    wire [15:0] mix = (ear_out ? 16'd6000 : 16'd0) + (mic_out ? 16'd600 : 16'd0)
                    + ((ctrl_s2[2] & tape_level) ? 16'd3000 : 16'd0) + (m128_l ? {ay_sum[11:0], 4'd0} : 16'd0);
    reg  signed [15:0] mix_q = 16'sd0;
    always @(posedge sys_clk) mix_q <= mix;
    zxcpc_audio audio (.clk(sys_clk), .a_in(mix_q), .mute(hold_q), .mclk(mclk), .audio_l(audio_l), .audio_r(audio_r));

    // ================================================================ registers
    reg [15:0] frames = 16'd0;
    reg        frame_tgl = 1'b0;
    always @(posedge sys_clk) if (vs_rise) begin frames <= frames + 16'd1; frame_tgl <= ~frame_tgl; end
    (* ASYNC_REG = "TRUE" *) reg [2:0] ft_s = 3'b000;
    always @(posedge cpu_clk) ft_s <= {ft_s[1:0], frame_tgl};
    wire ev_frame = ft_s[2] ^ ft_s[1];
    wire [7:0] status = {4'd0, m128_l, snap_busy, hold_q, rst_q | (rcnt != 8'hFF)};
    // the tape's status crosses as a few quasi static bits
    (* ASYNC_REG = "TRUE" *) reg [3:0] ts1 = 4'd0, ts2 = 4'd0;
    always @(posedge cpu_clk) begin ts1 <= {tape_err, tape_stop, tape_eof, tape_playing}; ts2 <= ts1; end
    zx_regs #(.ID_TRX2(ID_TRX2)) regs (
        .clk(cpu_clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(build), .ddr_cal(ddr_cal), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata),
        .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip), .ev_frame(ev_frame),
        .ctrl(r_ctrl), .model(r_model), .joy1_mode(r_j1), .joy2_mode(r_j2), .win_page(r_winpage), .snap_go_tgl(r_snapgo),
        .snap_dir(r_dir), .snap_7ffd(r_7ffd), .snap_border(r_border), .snap_ay(r_ay), .snap_aysel(r_aysel),
        .tape_play(r_tplay), .tape_base(r_tbase), .tape_seek_pos(r_tpos), .tape_seek_tgl(r_tseek), .inject(r_inject),
        .status(status), .frames(frames), .cpuaddr(cpu_a),
        .fb_latest(fb_latest), .fb_seq(fb_seq), .fb_over(fb_over), .fb_base(fb_base), .fb_stride(fb_stride), .tape_pos(tape_pos), .tape_stat(ts2), .tape_under(tape_under)
    );

    // ---- JTAG proof of life (trxdbg "trx 0 ..."), words: 0 ID "ZXSP"  1 status  2 frames  3 CPU address  4 PC of the Z80  5-13 picture path (below)
    reg [31:0] jd;
    always @* begin
        case (dbg_addr[3:0])
            4'h0: jd = 32'h5053585A;
            4'h1: jd = {24'd0, status};
            4'h2: jd = {16'd0, frames};
            4'h3: jd = {16'd0, cpu_a};
            4'h4: jd = {16'd0, cpu_reg[79:64]};
            4'h5: jd = {fb_over, d_ls};                                                    // frame buffer lines late, lines started
            4'h6: jd = {d_vis, fb_latest, fb_seq};                                         // pixels shown, newest buffer (b7 valid, b1:0), frames written
            4'h7: jd = {dy, 1'b0, dx, HBlank_s, VBlank, VSync, hb_p, 2'b00};                 // 9 + 10 + 6 bits: dummy raster line and dot, flags
            4'h8: jd = {fb_acc, fb_lines};                                                 // e_* commands the host took, lines handed to the ui side
            4'h9: jd = {16'd0, fb_uidbg};                                                  // ui_rst, writer state, buffer, half, word of the row
            4'hA: jd = {d_hbr, d_hbh};                                                     // HBlank rises, dots with HBlank high (ce_7mp)
            4'hB: jd = {d_ce, d_ls};                                                       // pixel enables, line starts
            4'hC: jd = {hs_per, hs_n};                                                     // dots between the last two HSync rises, HSync rises
            4'hD: jd = {hs_len, ln_pf};                                                    // dots HSync high in the last line, HSync rises per frame
            default: jd = 32'd0;
        endcase
    end
    assign j_rdata = jd;
    assign j_rvalid = 1'b1;
endmodule
