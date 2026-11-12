// ---------------------------------------------------------------------------
// T29 turbo engine (docs/specs/T29-turbo.md §12): the CPU faster than PHI2.
//
// The 6502N core (cpu6510.v) runs on clk2x, 2 x the C64's dot4x, with clock
// enables. At normal speed the enable is the PHI2 phase of today; in turbo
// it is a slot of a 64-slot grid per PHI2 cycle (a DDA: N slots, the last one
// always on the PHI2 falling edge, so the CPU is between cycles at every
// edge a stop can come on). Memory accesses of the turbo CPU are
//   RAM, ROM        c64_mem's port B (clocked on the falling edge of clk2x,
//                   half a CPU cycle after the address; data in the same cycle)
//   $00/$01         inside cpu6510
//   turbo registers here, no bus cycle
//   $D011/$D012 R   the VIC's raster, no bus cycle
//   VIC writes      posted into kawari's register file (fw_*)
//   VIC reads       kawari's register vector through fr_q (not $D01E/$D01F)
//   colour RAM      the block RAM's port A in the clocks the VIC leaves free (cq_*)
//   SID             writes into the SID's op FIFO (ps_*), reads from its read-back registers (fr_q)
//   CIA 1/2         reads from the CIA's register vector through fr_q; the access itself is posted: the
//                   stage holds it, c64_top drives the CIA from the stage until the fall (pk_g); not the ICR
//   everything else a bus cycle: the CPU holds, the arbiter's stage (c64_top)
//                   takes the address in a window of the first gclk edges after the
//                   PHI2 fall, the access runs in the next half as at 1 MHz and
//                   ends at the falling edge with din_r valid.
// Stops leave turbo: inhibit makes the engine fall back to normal speed at
// the next falling edge (once the SID's FIFO is empty); c64_core takes the stop only while !tmode.
//
// Clock domains: everything here is clk2x unless a name says gclk. gclk's
// edges pass only when the machine runs; g_edge is the clk2x edge that
// coincides with one that passes, run2 any clk2x edge that passes (c64_core's
// ce, the BUFGCE's enable, is a clk register, "the CE for the next gclk edge").
//
// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module c64_turbo #(
  // Windows: the last gclk edge after the PHI2 fall (gph, the value before the edge) at which a slow access may load the
  // stage. The stage is visible one edge later; an access whose stage is visible by the edge before the fall completes at
  // that fall, else at the next one. 31 is the latest (the stage is seen during the last clock before the fall).
  parameter [4:0] WIN  = 5'd12,      // anything with RAS/CAS or strobe timing (the VIC's address latch, colour RAM, cartridge ROM)
  parameter [4:0] WINC = 5'd30,      // CIA 1/2 reads and writes (they take the access in the whole high phase; sim/turbo phase, icr)
  parameter [4:0] WINS = 5'd25,      // SID
  parameter [4:0] WINX = 5'd14,      // IO1/IO2 ($DE00-$DFFF: the UCI, the REU, the cartridge)
  parameter [6:0] IEC_WIN = 7'd100,  // PHI2 cycles at the 1 MHz cadence after a change of a serial bus line the CIA 2 drives
  parameter [22:0] HOLD = 23'h400000 // PHI2 cycles at 1 MHz after a reset: 2^22 (measured on Gideon's core)
) (
  input  wire        clk,            // gclk
  input  wire        clk2x,
  input  wire        rst,            // clk-side reset
  // the gate
  input  wire        gate_ce,        // c64_core's ce (clk of the system, ungated)
  output wire        run2,
  output wire        g_edge,
  // the machine (gclk side; stable at the clk2x edges they are read on)
  input  wire        phi2_l,         // one clk wide pulses (kawari)
  input  wire        phi2_n,
  input  wire        vic_ba,
  input  wire        cpu_rdy,        // vic_ba && !paused && !reu dma && !cart wait
  input  wire        reu_dma_active,
  input  wire        cpu_reset,
  input  wire        inhibit,        // something stops the machine or needs it exact
  input  wire        game,
  input  wire        exrom,
  input  wire  [2:0] pout3,          // CPU port bits 2:0 as driven: {charen, hiram, loram}
  input  wire  [7:0] vic_d011,       // the VIC's $D011/$D012 as a read returns them
  input  wire  [7:0] vic_d012,
  input  wire [383:0] vic_regs,      // $D000-$D02F as a read returns them, register 0 in bits 7:0 (kawari's sb_regs)
  input  wire [127:0] cia1_regs,     // the CIAs' 16 registers as a read returns them, register 0 in bits 127:120
  input  wire [127:0] cia2_regs,
  // the CPU (clk2x)
  input  wire [15:0] cpu_addr,
  input  wire        cpu_we,
  input  wire  [7:0] cpu_dout,
  input  wire  [7:0] cpu_dout_early,   // cpu_dout without the late sources (proc_registers data_early)
  input  wire        cpu_dout_late,    // this cycle's byte is a late one (the shifter's, the SHA family's)
  output wire        t_mode,         // tmode: the engine drives the core
  output wire        t_ce,
  output wire        t_sel,
  output wire  [7:0] t_din,
  input  wire  [7:0] din_r,          // cpu6510's: the bus byte of the last PHI2 fall
  output wire        skip_pl,
  // the arbiter's stage (c64_top, gclk): follows the CPU at normal speed; in
  // turbo st_ld loads it, st_park parks it (address 0, read) unless held
  output wire        st_ld,
  output wire        st_park,
  output wire        st_post,        // with st_ld: the access is a posted CIA access (c64_top drives the CIA from the stage until the fall)
  // the SID's op FIFO (trx_sid.v): writes are pushed with a two clk2x clock pulse, reads take the read-back registers
  output wire        ps_v,
  output wire  [9:0] ps_a,
  output wire  [7:0] ps_d,
  input  wire        sid_fhit,       // gclk side, a function of cpu_addr: the SID decode hits
  input  wire  [7:0] sid_frd,
  input  wire        sid_full,
  input  wire        sid_empty,
  input  wire        sid_quiet,
  // colour RAM (c64_top: the block RAM's port A in the clocks the VIC and the bus leave free): one request at a time, toggle each way
  output wire        cq_tgl,
  output wire        cq_we,
  output wire  [9:0] cq_a,
  output wire  [3:0] cq_d,
  input  wire        cq_ack,
  input  wire  [3:0] cq_q,
  input  wire        pk_ok,          // no DMA holds the bus: a posted access completes at the fall
  input  wire [15:0] addr_a,         // the stage
  input  wire        we_a,
  input  wire  [7:0] dout_a,
  input  wire        nw_sel,         // the arbiter selects the VIC's page for a CPU write in this half
  output wire        tr_oe,          // a read of a turbo register at normal speed
  output wire  [7:0] tr_dout,
  // c64_mem port B
  output wire [15:0] tb_addr,
  output wire [14:0] tb_raddr,
  output wire        tb_we,
  output wire  [7:0] tb_wdata,
  input  wire  [7:0] tb_ram_lo, tb_ram_hi, tb_rom_lo, tb_rom_hi,
  input  wire        tb_took,        // an outside access took the last falling edge
  // kawari's posted register write
  output wire        fw_we,
  output wire  [5:0] fw_a,
  output wire  [7:0] fw_d,
  // the app: the enable word, the preferred speed, the strobe (any clock,
  // quasi-static; the toggle changes after the bytes)
  input  wire  [7:0] cfg_en,
  input  wire  [7:0] cfg_prefer,
  input  wire        cfg_upd_tgl,
  // status and state (clk2x registers; read from the clk side)
  output wire [31:0] stat,
  output wire [31:0] st_w1,          // live: {$D031, $D030 bit, enable word, PREFER}
  output wire [31:0] st_w2,          // live: {0, hold}
  output wire [31:0] st_pack,        // the state bus word (chip 0 word 1): everything in 32 bits
  input  wire        ld_en,          // sb_ldarm[0] on the load edge
  input  wire [31:0] ld_pack,
  input  wire        ran_clr,        // clk: clears turbo_ran (a clk-side pulse)
  input  wire  [2:0] iec_lines       // the CIA 2's pulls on ATN, CLK, DATA (gclk side; a change opens the IEC window)
);
    reg        tmode = 1'b0;                 // the engine drives the core (adopted at a PHI2 fall)
    reg  [5:0] n_cur = 6'd1;                 // CPU cycles per PHI2 cycle in force
    reg        bl_cur = 1'b1;                // badline timing in force
    reg        skip_r = 1'b0;
    reg        ran = 1'b0;                   // sticky: the engine has run since the last clear
    reg  [1:0] sl_st = 2'd0;                 // slow access: 0 idle, 2 stage loaded, wait for the fall
    reg        slow_done = 1'b0;
    reg        ack_s = 1'b0, crd_req = 1'b0;     // crd_req: a read request is out or answered, not yet taken by a step
    reg  [3:0] col_q = 4'd0;
    reg        rq_tgl = 1'b0, rq_we = 1'b0;
    reg  [9:0] rq_a = 10'd0;
    reg  [3:0] rq_d = 4'd0;
    assign cq_tgl = rq_tgl; assign cq_we = rq_we; assign cq_a = rq_a; assign cq_d = rq_d;
    wire       cq_busy = rq_tgl != ack_s;
    reg        age_ok = 1'b0, age2 = 1'b0, sid_hit_q = 1'b0, sfull_s = 1'b0, sq_ok_s = 1'b0, sempty_s = 1'b0;
    reg  [2:0] sidw_age = 3'd0;
    reg        pv1 = 1'b0, pv2 = 1'b0, sqs = 1'b0;
    reg  [9:0] pa_r = 10'd0;
    reg  [7:0] pd_r = 8'd0;
                 // the CPU's address has been stable for one clk2x edge
    reg        sl_post = 1'b0;                // the access in the stage is a posted one
    wire       done_ok = sl_post ? pk_ok : cpu_rdy;   // the fall completes it
    reg        ld_done = 1'b0;                // the stage holds this CPU cycle's posted access
    wire       ran_clr_s;

    // ---- the gate's two strobes ----------------------------------------------
    reg  ce_s = 1'b1, tg_s = 1'b0;
    reg  tg = 1'b0;                          // gclk: toggles on every edge that passes
    always @(posedge clk) tg <= ~tg;
    always @(posedge clk2x) begin
        ce_s <= gate_ce;
        tg_s <= tg;
    end
    assign run2   = gate_ce && ce_s;
    assign g_edge = run2 && (tg_s == tg);
    wire pn = g_edge && phi2_n;
    wire pl = g_edge && phi2_l;

    // ---- the app's setting, taken at the strobe ----------------------------------
    (* ASYNC_REG = "TRUE" *) reg [16:0] cfg_s1 = 17'd0, cfg_s2 = 17'd0;
    reg        upd_q = 1'b0;
    always @(posedge clk2x) begin
        cfg_s1 <= {cfg_upd_tgl, cfg_prefer, cfg_en};
        cfg_s2 <= cfg_s1;
        upd_q  <= cfg_s2[16];
    end
    wire upd = cfg_s2[16] != upd_q;

    reg  [2:0] regs_en = 3'd0;
    reg  [7:0] prefer  = 8'h80;
    reg  [7:0] d031    = 8'h00;
    reg        d030_b0 = 1'b0;
    reg [22:0] hold    = 23'd0;
    wire [7:0] menu = {~prefer[7], prefer[6:0]};

    // ---- the turbo registers' events: a write at normal speed is seen on the
    // bus (the arbiter selected the VIC page for a CPU write, snooped at the
    // falling edge that ends the half), in turbo the step that ends the write
    wire [15:0] ta = tmode ? cpu_addr : addr_a;
    wire        t_pg0 = ta[11:8] == 4'h0;
    wire        m_d030 = ta[5:0] == 6'h30, m_d031 = ta[5:0] == 6'h31;
    wire        m_d07a = ta[6] && ta[5:0] == 6'h3A, m_d07b = ta[6] && ta[5:0] == 6'h3B;
    wire        m_d0bc = ta[5:0] == 6'h3C;
    // the registers (reads below: $D0BD-$D0BF read $FF like the VIC)
    wire        a_treg = m_d030 || m_d031 || m_d07a || m_d07b || m_d0bc;
    wire        tw_n   = pn && !tmode && nw_sel && we_a;     // normal speed, snooped
    // (turbo: cls_treg below)
    wire        tw_t;
    wire        tw_v = tw_n || tw_t;
    wire  [7:0] tw_d = tmode ? cpu_dout : dout_a;
    wire        tr_any = regs_en[0] || regs_en[2];

    always @(posedge clk2x) begin
        if (upd) begin
            regs_en <= cfg_s2[2:0];
            prefer  <= cfg_s2[15:8];
            d031    <= {~cfg_s2[15], cfg_s2[14:8]};
            d030_b0 <= 1'b1;
        end else if (tw_v && t_pg0) begin
            if (m_d031 && regs_en[0]) d031 <= tw_d;
            if (m_d030 && regs_en[2]) begin
                if (tw_d[0] && !d030_b0) begin d030_b0 <= 1'b1; d031 <= menu; end
                else if (!tw_d[0]) begin d030_b0 <= 1'b0; d031 <= 8'h00; end
            end
            if (m_d07a && tr_any) d031 <= 8'h00;
            if (m_d07b && tr_any) d031 <= menu;
        end
        if (pn && hold != 23'd0) hold <= hold - 23'd1;
        if (cpu_reset) begin
            d031 <= 8'h00; d030_b0 <= 1'b0; hold <= HOLD;
        end
        if (ld_en && g_edge) begin           // a restore (state bus chip 0 word 1)
            prefer  <= {ld_pack[4], 3'd0, ld_pack[3:0]};
            regs_en <= ld_pack[7:5];
            d030_b0 <= ld_pack[8];
            d031    <= ld_pack[16:9];
            hold    <= {ld_pack[31:17], 8'd0};
        end
    end
    assign st_w1 = {d031, 12'd0, d030_b0, regs_en, prefer};
    assign st_w2 = {9'd0, hold};
    // the state bus word: PREFER's speed index and badline bit (the other bits are not used by
    // anything), the enable word, $D030 bit 0, $D031, the hold in units of 256 PHI2 cycles
    assign st_pack = {hold[22:8], d031, d030_b0, regs_en, prefer[7], prefer[3:0]};

    // reads
    reg [7:0] treg_rd;
    always @(*) begin
        treg_rd = 8'hFF;
        if (m_d031) treg_rd = regs_en[0] ? d031 : 8'hFF;
        else if (m_d030) treg_rd = regs_en[2] ? {7'h7F, d030_b0} : 8'hFF;
        else if (m_d0bc) treg_rd = regs_en[1] ? 8'h01 : 8'hFF;
    end
    assign tr_oe   = t_pg0 && a_treg && !we_a && !tmode;
    assign tr_dout = treg_rd;

    // ---- the speed in force (§12.1) ------------------------------------------------
    reg  [3:0] idx;
    reg        bl;
    always @(*) begin
        if (hold != 23'd0)                      begin idx = 4'd0;       bl = 1'b1; end
        else if (!regs_en[0])                   begin idx = prefer[3:0]; bl = prefer[7]; end
        else if (regs_en[2] && !d030_b0)        begin idx = 4'd0;       bl = 1'b1; end
        else                                    begin idx = d031[3:0];   bl = !d031[7]; end
    end
    reg [5:0] n_idx;
    always @(*)
        case (idx)
            4'd0:  n_idx = 6'd1;   4'd1:  n_idx = 6'd2;   4'd2:  n_idx = 6'd3;   4'd3:  n_idx = 6'd4;
            4'd4:  n_idx = 6'd6;   4'd5:  n_idx = 6'd8;   4'd6:  n_idx = 6'd10;  4'd7:  n_idx = 6'd12;
            4'd8:  n_idx = 6'd14;  4'd9:  n_idx = 6'd16;  4'd10: n_idx = 6'd20;  4'd11: n_idx = 6'd24;
            4'd12: n_idx = 6'd32;  4'd13: n_idx = 6'd40;  4'd14: n_idx = 6'd47;  default: n_idx = 6'd63;
        endcase
    wire eng_want = n_idx != 6'd1 || !bl;

    // ---- the engine's mode, adopted at a PHI2 falling edge ---------------------------
    wire       want      = eng_want && !inhibit && !cpu_reset;
    wire       completing = (sl_st == 2'd2 && done_ok) || slow_done;   // a bus cycle ends with this edge
    wire       leave_ok  = !(tmode && completing) && sempty_s && sidw_age == 3'd0;   // the SID's op FIFO is empty (its ops are not the bus's)
    wire       tm_next   = want ? 1'b1 : (tmode && !leave_ok);
    always @(posedge clk2x) begin
        if (pn) begin
            tmode  <= tm_next;
            n_cur  <= n_idx;
            bl_cur <= bl;
            if (tmode && !tm_next) skip_r <= 1'b1;
            if (tm_next) ran <= 1'b1;
        end
        if (pl) skip_r <= 1'b0;
        if (cpu_reset) begin tmode <= 1'b0; skip_r <= 1'b0; end
        if (ran_clr_s) ran <= 1'b0;
    end
    assign t_mode  = tmode;
    assign skip_pl = skip_r;
    // the clk-side pulse into clk2x (one clk wide: two clk2x edges)
    (* ASYNC_REG = "TRUE" *) reg ran_clr_q = 1'b0;
    always @(posedge clk2x) ran_clr_q <= ran_clr;
    assign ran_clr_s = ran_clr_q;

    // ---- the slot grid ---------------------------------------------------------------
    reg  [5:0] acc = 6'd0;
    wire [6:0] sum = {1'b0, acc} + {1'b0, n_cur};
    always @(posedge clk2x)
        if (run2) acc <= pn ? 6'd0 : sum[5:0];
    // ---- the IEC window -------------------------------------------------------------
    // The KERNAL times the serial bus with CPU loops between writes to $DD00 (a CLK pulse is a handful of
    // instructions), and the drive samples at 1 MHz. A turbo CPU shrinks those gaps below what the drive
    // sees. For IEC_WIN PHI2 cycles after any change of a line the CIA 2 drives, the CPU gets one step per
    // PHI2 cycle (the 1 MHz cadence), so every gap between two writes to the bus is that of a 1 MHz C64.
    // A posted write to the CIA 2's port A or its direction register (the serial bus lines) opens the window when the
    // stage takes it, not when the line changes at the fall: the CPU must not run ahead of the write's effect.
    wire iec_open;
    reg  [2:0] iec_s1 = 3'b111, iec_s2 = 3'b111, iec_q = 3'b111;
    reg  [6:0] iec_cnt = 7'd0;
    always @(posedge clk2x) begin
        iec_s1 <= iec_lines; iec_s2 <= iec_s1; iec_q <= iec_s2;
        if (iec_s2 != iec_q || iec_open) iec_cnt <= IEC_WIN;
        else if (pn && iec_cnt != 7'd0) iec_cnt <= iec_cnt - 7'd1;
    end
    wire iec_win = iec_cnt != 7'd0;
    wire slot = tmode && run2 && ((sum[6] && !iec_win) || pn);

    // ---- the PLA on the CPU's address (BA high: BA acts on the CPU's stalls) -----------
    wire p_io, p_ram, p_basic, p_kernal, p_chr, p_roml, p_romh;
    // GAME/EXROM are the cartridge's (and the app's Ultimax): they change rarely, and through
    // combinational paths of the cartridge model, so the PLA sees a clk2x copy
    reg game_q = 1'b1, exrom_q = 1'b1;
    always @(posedge clk2x) begin game_q <= game; exrom_q <= exrom; end
    c64_pla pla (
        .game(game_q), .exrom(exrom_q), .charen(pout3[2]), .hiram(pout3[1]), .loram(pout3[0]),
        .msn(cpu_addr[15:12]), .we(cpu_we), .ba(1'b1),
        .io(p_io), .ram(p_ram), .basic(p_basic), .kernal(p_kernal), .chr(p_chr),
        .roml(p_roml), .romh(p_romh)
    );
    wire a_pg0   = p_io && cpu_addr[11:8] == 4'h0;                 // $D0xx
    wire a_vic   = p_io && cpu_addr[11:10] == 2'b00;               // $D000-$D3FF
    wire c_treg  = a_pg0 && ((cpu_addr[5:0] == 6'h30) || (cpu_addr[5:0] == 6'h31) ||
                             (cpu_addr[5:0] == 6'h3C) ||
                             (cpu_addr[6] && (cpu_addr[5:0] == 6'h3A || cpu_addr[5:0] == 6'h3B)));
    wire c_vras  = a_vic && !cpu_we && !c_treg &&
                   (cpu_addr[5:0] == 6'h11 || cpu_addr[5:0] == 6'h12);
    wire c_vicw  = a_vic && cpu_we;
    wire ff00w   = cpu_we && cpu_addr == 16'hFF00;                 // the REU's trigger needs the bus
    wire c_ram   = p_ram && !ff00w;
    wire c_rom   = p_basic || p_kernal || p_chr;
    // VIC registers other than $D011/$D012 (fast already) and the collision registers $D01E/$D01F, whose read clears
    // them: a read returns the register vector kawari makes its bus output of. Like every fast I/O read the byte is
    // taken one clk2x edge after the address (age_ok), so at 63 cycles per PHI2 it costs one CPU cycle more than RAM.
    wire c_vicr  = a_vic && !cpu_we && !c_treg && !c_vras && cpu_addr[5:1] != 5'b01111;
    // Colour RAM ($D800-$DBFF): a write is posted into the request register (one at a time), a read posts a request
    // once the address has been stable for an edge and takes the nibble when the block RAM has answered; the byte's
    // upper nibble reads 0. Gideon: 0.065 PHI2 per write, 0.082 per read at 63 cycles per PHI2.
    wire c_col   = p_io && cpu_addr[11:10] == 2'b10;
    wire c_colr  = c_col && !cpu_we;
    wire c_colw  = c_col && cpu_we;
    // SID ($D400-$D7FF where a SID answers): a write is pushed into the SID's op FIFO and the CPU goes on, a read takes
    // the SID's read-back registers once the FIFO is empty and the last op has been in the engine for two C64 cycles.
    // The decode (sid_hit_q) is a register: the class is known one edge after the address is stable (age2).
    wire c_sid   = p_io && cpu_addr[11:10] == 2'b01;
    wire c_sidf  = c_sid && sid_hit_q;
    wire c_slow  = !(c_ram || c_rom || c_treg || c_vras || c_vicw || c_vicr || c_col || c_sidf);
    // CIA 1/2 except the ICR ($xD): a read answers at once with the register as a read returns it (it is the same
    // vector the CIA's bus output is made of), a write and the read's own bus cycle (the TOD latch) are posted: the stage holds
    // the access and it completes at the next fall as before, the CPU goes on as soon as the stage is loaded. The next
    // slow access waits for the stage. The ICR stays a held access: its flags and IRQ output change at the fall
    // and the CPU must not see the old line.
    wire c_ciaa  = p_io && cpu_addr[11:9] == 3'b110;
    // Not the port registers' reads ($xx0, $xx1): the CIA returns the pins as it sampled them at PHI2's rising edge, so a
    // read right after a write to the port (the keyboard scan) must wait for the next rising edge, as it does at 1 MHz.
    wire c_cp    = c_ciaa && cpu_addr[3:0] != 4'hD && !(!cpu_we && cpu_addr[3:1] == 3'b000);
    assign t_sel = tmode;                  // every turbo cycle's data is t_din (a slow one's from slow_data)
    assign tw_t  = t_ce && cpu_we && c_treg;

    wire [7:0] vras_rd = cpu_addr[0] ? vic_d011 : vic_d012;       // $11 or $12
    // a slow access's byte: din_r holds it only until the next PHI2 fall, so it is
    // copied on the first clk2x edge after the fall that ended the access; the CPU
    // may wait (BA) for many cycles before it takes it
    reg  [7:0] slow_data = 8'd0;
    reg        cap_pend = 1'b0;
    always @(posedge clk2x) begin
        cap_pend <= pn && ((sl_st == 2'd2 && cpu_rdy) || (!tmode && tm_next));   // or the pending cycle was the bus's at normal speed
        if (cap_pend) slow_data <= din_r;
    end
    // the block RAM's bytes come last: one AND-OR of the four lanes with the early selects
    // (decode of the CPU's address), then everything else that is not from a block RAM
    wire sel_rl = c_ram && !cpu_addr[0], sel_rh = c_ram && cpu_addr[0];
    wire sel_ol = c_rom && !cpu_addr[0], sel_oh = c_rom && cpu_addr[0];
    wire [127:0] cia_sel = cpu_addr[8] ? cia2_regs : cia1_regs;
    reg  [7:0] fr_q = 8'd0;                // the fast I/O read register: the byte for the address, one clk2x edge old
    wire [5:0] vi = cpu_addr[5:0];
    wire [7:0] vic_byte = vi < 6'd48 ? vic_regs[vi * 8 +: 8] : 8'hFF;
    always @(posedge clk2x) fr_q <= cpu_addr[11] ? cia_sel[(4'd15 - cpu_addr[3:0]) * 8 +: 8] : cpu_addr[10] ? sid_frd : vic_byte;
    wire [7:0] t_other = c_treg ? treg_rd :
                         c_vras ? vras_rd :
                         (c_cp || c_vicr || (c_sidf && !cpu_we)) ? fr_q :
                         c_colr ? {4'h0, col_q} :
                         (c_ram || c_rom) ? 8'h00 : (cap_pend ? din_r : slow_data);
    assign t_din = ({8{sel_rl}} & tb_ram_lo) | ({8{sel_rh}} & tb_ram_hi) |
                   ({8{sel_ol}} & tb_rom_lo) | ({8{sel_oh}} & tb_rom_hi) | t_other;

    reg        late_done = 1'b0;               // the holding slot of a late byte has passed
    reg  [7:0] wd_late = 8'd0;
    wire       late_hold = cpu_we && c_ram && cpu_dout_late && !late_done;

    // ---- go: does the CPU step on this slot? ------------------------------------------
    wire stall_ba   = bl_cur && !cpu_we && !vic_ba;
    wire slow_wait  = c_slow && !c_cp && !slow_done;
    wire post_wait  = c_cp && !ld_done;
    wire fr_wait    = c_vicr && !age_ok;
    wire sid_hold   = c_sid && !age_ok;
    wire sid_wait   = c_sidf && (!want || (cpu_we ? sfull_s : !(sq_ok_s && sidw_age == 3'd0)));
    wire col_wait   = (c_colr && !(crd_req && !cq_busy)) || (c_colw && cq_busy);
    wire go         = !(stall_ba || tb_took || reu_dma_active || slow_wait || post_wait || fr_wait || col_wait || sid_hold || sid_wait || late_hold);
    assign t_ce     = slot && go;

    // ---- a bus cycle for a slow access --------------------------------------------------
    reg  [4:0] gph = 5'd31;                   // gclk edges since the PHI2 fall
    always @(posedge clk2x)
        if (g_edge) gph <= phi2_n ? 5'd0 : (gph == 5'd31 ? gph : gph + 5'd1);
    // The decode of the CPU's address is registered (sq...) one clk2x edge after a step and used once the address has
    // been stable for that edge (age_ok): the stage then loads at the gclk edge that follows, in the same clock the
    // issue is seen (the stage is visible one edge later).
    wire c_cia = p_io && (cpu_addr[11:8] == 4'hC || cpu_addr[11:8] == 4'hD);
    wire c_io12 = p_io && cpu_addr[11:9] == 3'b111;
    reg        sq = 1'b0, sqp = 1'b0, sqi = 1'b0;
    reg  [4:0] winq = 5'd0;
    always @(posedge clk2x) begin
        age_ok <= !t_ce;
        sq     <= c_slow;
        sqp    <= c_cp;
        sqs    <= c_sid;
        sqi    <= c_cp && cpu_we && cpu_addr[8] && (cpu_addr[3:0] == 4'h0 || cpu_addr[3:0] == 4'h2);
        winq   <= c_cia ? WINC : c_sid ? WINS : c_io12 ? WINX : WIN;
    end
    wire issue = tmode && sq && age_ok && (!sqs || age2) && !slow_done && sl_st == 2'd0 && gph <= winq;
    always @(posedge clk2x) begin
        if (g_edge) begin
            case (sl_st)
                2'd0: if (issue) begin sl_st <= 2'd2; sl_post <= sqp; if (sqp) ld_done <= 1'b1; end
                default: if (phi2_n && done_ok) sl_st <= 2'd0;
            endcase
            if (sl_st == 2'd2 && phi2_n && cpu_rdy && !sl_post) slow_done <= 1'b1;
        end
        if (t_ce) begin slow_done <= 1'b0; ld_done <= 1'b0; end
        if (pn && tmode && !tm_next) begin sl_st <= 2'd0; slow_done <= 1'b0; ld_done <= 1'b0; end
        if (pn && !tmode) begin sl_st <= 2'd0; slow_done <= 1'b0; ld_done <= 1'b0; end
        if (cpu_reset) begin sl_st <= 2'd0; slow_done <= 1'b0; ld_done <= 1'b0; end
    end
    // ---- SID fast path ----------------------------------------------------------------------
    always @(posedge clk2x) begin
        age2      <= age_ok;
        sid_hit_q <= sid_fhit;
        sfull_s   <= sid_full; sq_ok_s <= sid_quiet; sempty_s <= sid_empty;
        pv2       <= pv1;
        pv1       <= t_ce && cpu_we && c_sidf;
        if (t_ce && cpu_we && c_sidf) begin pa_r <= cpu_addr[9:0]; pd_r <= cpu_dout; sidw_age <= 3'd6; end
        else if (sidw_age != 3'd0) sidw_age <= sidw_age - 3'd1;
    end
    assign ps_v = pv1 || pv2;
    assign ps_a = pa_r;
    assign ps_d = pd_r;

    // ---- colour RAM requests -------------------------------------------------------------
    always @(posedge clk2x) begin
        ack_s <= cq_ack;
        if (cq_busy) col_q <= cq_q;           // the last edge before the answer is seen holds the block RAM's output
        if (t_ce && c_colw) begin
            rq_tgl <= ~rq_tgl; rq_we <= 1'b1; rq_a <= cpu_addr[9:0]; rq_d <= cpu_dout[3:0];
        end else if (c_colr && age_ok && !cq_busy && !crd_req && tmode) begin
            rq_tgl <= ~rq_tgl; rq_we <= 1'b0; rq_a <= cpu_addr[9:0]; crd_req <= 1'b1;
        end
        if (t_ce || !tmode || cpu_reset) crd_req <= 1'b0;
    end
    assign st_ld   = issue;
    assign iec_open = g_edge && sl_st == 2'd0 && issue && sqi;
    assign st_post = sqp;
    assign st_park = sl_st == 2'd0;

    // ---- port B and the posted VIC write ---------------------------------------------
    assign tb_addr  = cpu_addr;
    assign tb_raddr = cpu_addr[15:13] == 3'b111 ? {2'b01, cpu_addr[12:0]} :
                      cpu_addr[15:13] == 3'b101 ? {2'b10, cpu_addr[12:0]} : {3'b000, cpu_addr[11:0]};
    // A byte the core computes late in its cycle (a read-modify-write's result, the SHA family) is
    // not ready half a cycle in, when port B writes: it is copied on the next clk2x edge (wd_late) and
    // written half a cycle after that. If that edge is a slot, the CPU holds it (late_hold: one
    // CPU cycle more, only above 32 cycles per PHI2, where slots can be adjacent). Every other byte
    // comes from cpu_dout_early, a few LUTs behind registers, and is written half a cycle in.
    always @(posedge clk2x) begin
        wd_late <= cpu_dout;
        if (t_ce) late_done <= 1'b0;
        else if (late_hold) late_done <= 1'b1;
        if (cpu_reset) late_done <= 1'b0;
    end
    assign tb_wdata = cpu_dout_late ? wd_late : cpu_dout_early;
    // Port B's write enable is the half-cycle path from the core's address registers to the block RAM's
    // WEBWE pins (CI 497, WNS -0.033: adl_reg -> the $FF00 compare -> c_ram -> tb_we, 7 levels), so it does
    // not wait for ff00w: a write to $FF00 (which c_ram sends to the bus, the REU's trigger) also lands in
    // RAM at the turbo step, as every other RAM write does; the bus write that follows stores the same
    // byte. A late byte is not written early there: late_done is only ever set through late_hold, which
    // still needs c_ram.
    assign tb_we    = tmode && cpu_we && p_ram && (!cpu_dout_late || late_done);

    reg        fw_v = 1'b0, fw_v2 = 1'b0;
    reg  [5:0] fw_a_r = 6'd0;
    reg  [7:0] fw_d_r = 8'd0;
    always @(posedge clk2x) begin
        fw_v  <= t_ce && cpu_we && c_vicw;
        fw_v2 <= fw_v;
        if (t_ce && cpu_we && c_vicw) begin fw_a_r <= cpu_addr[5:0]; fw_d_r <= cpu_dout; end
    end
    assign fw_we = fw_v || fw_v2;
    assign fw_a  = fw_a_r;
    assign fw_d  = fw_d_r;

    // ---- status ---------------------------------------------------------------------
    assign stat = {hold != 23'd0, tmode, bl_cur, ran, inhibit, 4'd0, 1'b0, n_cur, 5'd0, regs_en,
                   d030_b0, 3'd0, idx} ;
endmodule
