// T182 step 3 (T4, T5, T10; D9 P-A): the SuperCPU memory map of the 65C816. Written for this project; the oracle is VICE scpu64mem.c / scpu64meminit.c.
// Runs on clk2x (the CPU's clock) and sits beside the CPU (cpu65816_c64scpu.v). For every bus cycle the CPU is about to make it
//   1. settles (the CPU's registers change only at its enable; the paths from them are multicycle, spec 5.4), then decides with the generated table (scpu_cfgtab.v, index
//      {boot, dos, hw, !game, !exrom, loram, hiram, charen}, 10 page areas) and the bank which of these the cycle is:
//        MEM  the CPU's own memory in the DDR2 behind scpu_cache.v: SRAM (banks 0/1), SuperRAM (banks $02-$F5, 16 MiB fixed, D3), the SIMM window $F6/$F7 (= SuperRAM bytes
//             0..$1FFFF, writes only with hwenable, scpu64mem.c:529-542), the ROM ($F8-$FF and the F8 class over $8000-$FFFF of bank 0; the file image repeated:
//             file[k & (2^SROM_LOG2 - 1)], writes ignored, scpu64mem.c:544-551)
//        BUS  the C64 side (I/O, colour RAM, cartridge, character ROM, the internal C64 RAM RC): the cycle goes to the C64 bus as before (PHI2 synchronous); the C64 PLA is
//             told the class by forcing the CPU port lines it sees (RC: 000, CR: 011, the rest 111)
//        INT  the SuperCPU hardware registers $D071-$D07F / $D0B0-$D0BF (scpu64mem.c:634-821): answered here, never shown to the C64 chips
//   2. holds the CPU (its enable is withheld) until the data of a MEM read is in the cache and the write buffer is free for a MEM write; every enable that was withheld
//      counts in dev_cyc (the deviation from the original, which has no wait there; D8). Step 4 builds the original's wait-state model on top.
// Registers implemented here: hwenable ($D07E / $D07D / $D07F / $D0B2 bit 7), bootmap ($D0B6 / $D0B7, hw needed; reset 1), dosext ($D0BC-$D0BF); reads of $D0B0-$D0BF. Not here
// (step 4/5): the speed registers (the reads show the switch), the optimisation / mirroring registers ($D074-$D077, $D0B3/$D0B4: constant reset value).
// `ifdef SCPU_FLATMAP: the C64's flat map of steps 1/2 (every cycle goes to the C64 bus, no hold) - only the old benches (run_base.sh, run_emu.sh) build it.
// `ifdef SCPU_MUT_NOBOOTMAP (S1 control): bootmap resets to 0.  `ifdef SCPU_MUT_NOALIAS (S6 control): $F6/$F7 are two more SuperRAM banks, writes not gated.
// `ifdef SCPU_MUT_ADDR23 (S6 control): bit 23 of a SuperRAM address is dropped on the way to the DDR2.
`timescale 1ns / 1ps
module scpu_mem (
    input  wire         clk2x,
    input  wire         rst,
    input  wire         ce_raw,                 // g_edge && phi2_l
    input  wire         l_ev,                   // g_edge && phi2_l: the start of a PHI2 cycle (the 1 MHz enable)
    input  wire         n_ev,                   // g_edge && phi2_n: the PHI2 fall, din_r has the byte of the cycle that ends
    output wire         core_ce,
    input  wire [23:0]  a_out,
    input  wire         bus_cyc,
    input  wire         wr_cyc,
    input  wire  [7:0]  d_out,
    input  wire         e_flag,
    input  wire         vpb,                    // 0 = the cycle is a vector pull ($FFE4-$FFFE of bank 0)
    input  wire         vpa,                    // the cycle fetches the program stream (opcode or operand byte)
    input  wire  [2:0]  pport,
    input  wire         exrom_pin,              // the cartridge lines as pins: 1 = not pulled
    input  wire         game_pin,
    // the application (cpu_clk, quasi static) and the ROM_CTRL flush (a pulse)
    input  wire         rom_valid_i,
    input  wire  [4:0]  srom_log2_i,
    input  wire  [1:0]  scpu_sw_i,              // b0 = 20 MHz switch, b1 = JiffyDOS switch
    input  wire         flush,
    input  wire         aec,                    // the VIC leaves the C64 bus to the CPU (high in the PHI2 high phase, low in the badline's c-accesses)
    output wire         rom_valid_o,
    output wire         fast_o,                 // 20 MHz in effect
    output wire         mem_free,               // fast mode and the cycle is not a PHI2-synchronous one: BA does not stop it
    // to the CPU wrapper
    output wire         fwd,                    // the cycle goes to the C64 bus
    output wire  [2:0]  pla_pport,
    output wire         rd_mem,
    output wire         rd_int,
    output wire  [7:0]  mem_q,
    output wire  [7:0]  int_q,
    output wire         sync_cyc,               // the cycle is a PHI2-synchronous one (I/O, cartridge, C64 RAM, registers, the CPU port)
    output wire         mir_drv,                // S5 (spec 5.7): the queued mirror write is on the C64 bus (address, data, PLA lines 000)
    output wire [15:0]  mir_a,
    output wire  [7:0]  mir_d,
    // the DDR2 jobs (c64scpu_ddrx.v)
    output wire         s_req_tgl,
    output wire [20:0]  s_req_line,
    input  wire         s_done_tgl,
    input  wire [127:0] fill,
    output wire         sw_req_tgl,
    output wire [24:0]  sw_req_addr,
    output wire  [7:0]  sw_req_data,
    input  wire         sw_done_tgl,
    // status
    output reg  [31:0]  dev_cyc = 32'd0,
    output wire [31:0]  n_hit,
    output wire [31:0]  n_miss,
    output wire         bootmap_o,
    output wire         hwenable_o,
    output wire         dosext_o
);
    // ---- the application's registers, into clk2x ----------------------------------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [1:0] rv_s = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [4:0] lg_s0 = 5'd16, lg_s1 = 5'd16;
    (* ASYNC_REG = "TRUE" *) reg [1:0] sw_s0 = 2'b01, sw_s1 = 2'b01;
    always @(posedge clk2x) begin rv_s <= {rv_s[0], rom_valid_i}; lg_s0 <= srom_log2_i; lg_s1 <= lg_s0; sw_s0 <= scpu_sw_i; sw_s1 <= sw_s0; end
`ifdef SCPU_MUT_NOVALID
    wire        rom_valid = 1'b1;           // negative control of S1: ROM_VALID is ignored
`else
    wire        rom_valid = rv_s[1];
`endif
    assign rom_valid_o = rom_valid;
    wire [18:0] rom_mask = lg_s1 >= 5'd19 ? 19'h7FFFF : ((19'd1 << lg_s1) - 19'd1);

    // ---- the registers and the mode ------------------------------------------------------------------------------------------------------
`ifdef SCPU_MUT_NOBOOTMAP
    localparam BOOT_RST = 1'b0;
`else
    localparam BOOT_RST = 1'b1;
`endif
    reg boot = BOOT_RST, dos = 1'b0, hw = 1'b0;
    reg sys1 = 1'b0, soft1 = 1'b0;                      // $D072/$D073/$D0B2 and $D07A/$D07B/$D0B8 (scpu64mem.c:689-790)
    reg [7:0] optim = 8'hC7;                            // optimisation / mirroring mode (reset 0xC7, scpu64mem.c:1518-1532); the mirror (S5) is step 5
    reg [7:0] simm = 8'd4;                              // $D078 (reset 4); stored, the row size stays 8 KiB
    assign bootmap_o = boot; assign hwenable_o = hw; assign dosext_o = dos;
`ifdef SCPU_MUT_NOHWGATE
    wire hwg = 1'b1;                                    // S11 control: the registers that need hwenable ignore it
`else
    wire hwg = hw;
`endif
    wire sw1 = ~sw_s1[0];                               // the cartridge's speed switch: 1 = 1 MHz (T10: SCPU_SW b0 = 1 is 20 MHz)
`ifdef SCPU_MUT_NOFAST
    wire fast = 1'b0;                                   // S4 control: the CPU's enable is always PHI2
`else
    wire fast = !(sys1 | soft1 | (sw1 & ~hw));           // scpu64mem.c:702, :760
`endif
    assign fast_o = fast;

    // ---- every clock: the CPU's address, the class table and the cache line (the CPU's registers change only at its enable; the paths from them are multicycle 2, spec 5.6) ----
    reg [23:0] b_a = 24'd0;  reg b_bus = 1'b0,  b_wr = 1'b0;  reg [7:0] b_d = 8'd0;
    wire [3:0] area;
    wire [3:0] cls;
    scpu_area ar (.page(a_out[15:8]), .area(area));
    wire [7:0] cfg_idx = {boot, dos, hw, ~game_pin, ~exrom_pin, pport};
    scpu_cfgtab tab (.clk(clk2x), .a({area, cfg_idx}), .q(cls));
    reg b_vpb = 1'b1, b_vpa = 1'b0;
    always @(posedge clk2x) begin b_vpb <= vpb; b_vpa <= vpa; b_a <= a_out; b_bus <= bus_cyc; b_wr <= wr_cyc; b_d <= d_out; end
    reg [1:0] age = 2'd0;                               // clocks since the CPU's enable (saturates at 2); the lookup shows the new cycle from 2 on
    wire settled = age[1];
    always @(posedge clk2x) if (core_ce || rst) age <= 2'd0; else if (!age[1]) age <= age + 2'd1;

    localparam C_R0 = 0, C_R1 = 1, C_RC = 2, C_UM = 3, C_RL = 4, C_RH = 5, C_IO = 6, C_CR = 7, C_F8 = 8, C_KS = 9, C_KT = 10, C_CO = 11, C_OP = 12;
    localparam RK_NONE = 2'd0, RK_BUS = 2'd1, RK_MEM = 2'd2, RK_INT = 2'd3;
    localparam WK_NONE = 3'd0, WK_BUS = 3'd1, WK_MEM = 3'd2, WK_BOTH = 3'd3, WK_INT = 3'd4;

    // ---- the decision (combinational from the registered b_*, cls) ----------------------------------------------------------------------
    wire [7:0]  bank = b_a[23:16];
    wire [15:0] a16  = b_a[15:0];
    wire [7:0]  pg   = a16[15:8];
    function [24:0] sram(input [16:0] s); sram = 25'h1000000 | {8'd0, s}; endfunction
    function [24:0] romp(input [18:0] k); romp = 25'h1080000 | {6'd0, k}; endfunction
    // ---- the interrupt vector reroute (S9, spec 5.8; scpu64_interrupt_reroute() scpu64mem.c:1741-1744 and LOAD_INT_ADDR scpu64cpu.c:409-417): a vector pull whose page $FF reads the kernal shadow or bank 1
    // (classes KS, R1, KT) reads the ROM at $F8:vector instead when the CPU is in native mode or hwenable / system 1 MHz / dosext is set (RAMlink: not in this core)
    wire vec_pull = b_bus && !b_vpb && bank == 8'h00 && a16[15:5] == 11'h7FF;
`ifdef SCPU_MUT_NOREROUTE
    wire rr = 1'b0;                                       // S9 control: the vectors always come from the SRAM
`else
    wire rr = vec_pull && (cls == C_KS || cls == C_R1 || cls == C_KT) && (!e_flag || hw || sys1 || dos);
`endif
    reg [1:0]  d_rk;
    reg [2:0]  d_wk;
    reg [24:0] d_rp, d_wp;
    reg [2:0]  d_pla;
    reg [7:0]  d_int;
    // ---- mirroring (S5, spec 5.7): the pages of bank 0 whose writes also go to the C64 RAM (scpu64mem.c:81-86, index :1474-1475)
    wire [3:0] mir_ix = {optim[7], optim[6], optim[2], optim[0]};
    reg  [15:0] mir_tab;
    always @(*) case (mir_ix)
        4'd0, 4'd1:   mir_tab = 16'h80bf;
        4'd2:         mir_tab = 16'h003f;
        4'd3:         mir_tab = 16'h023f;
        4'd4, 4'd5:   mir_tab = 16'h407f;
        4'd6, 4'd7:   mir_tab = 16'hc0ff;
        4'd8, 4'd9:   mir_tab = 16'h0407;
        4'd10, 4'd11: mir_tab = 16'h0000;
        4'd12, 4'd14: mir_tab = 16'h00ff;
        default:      mir_tab = 16'h02ff;
    endcase
`ifdef SCPU_MUT_NOMIRROR
    wire mir_range = 1'b0;                                                                   // S5 control: nothing is mirrored
`elsif SCPU_MUT_MIRRWIN
    wire mir_range = mir_tab != 16'd0 && {1'b0, pg} >= {1'b0, mir_tab[15:8]} + 9'd1 && {1'b0, pg} <= {1'b0, mir_tab[7:0]} + 9'd1;   // S5 control: the window is one page too high
`elsif SCPU_FLATMAP
    wire mir_range = 1'b0;
`else
    wire mir_range = mir_tab != 16'd0 && pg >= mir_tab[15:8] && pg <= mir_tab[7:0];
`endif
    reg d_mir;
    wire is_hwr = pg == 8'hD0 && (a16[7:4] == 4'hB || (a16[7:4] == 4'h7 && a16[3:0] != 4'h0));      // $D071-$D07F, $D0B0-$D0BF
    wire is_hwb = pg == 8'hD0 && a16[7:4] == 4'hB;                                                // the readable ones
    wire is_iorm = pg == 8'hD2 || pg == 8'hD3;                                                    // IO RAM of the v2 board
    always @(*) begin
        d_rk = RK_NONE; d_wk = WK_NONE; d_rp = 25'd0; d_wp = 25'd0; d_pla = 3'd7;
        d_int = 8'hFF;
        if (b_bus) begin
            if (bank == 8'h00 && !rr) begin
                d_wk = WK_MEM; d_wp = sram({1'b0, a16});                                  // default: "write hook preset, ram" = SRAM[addr] of bank 0
                case (cls)
                    C_R0: begin d_rk = RK_MEM; d_rp = sram({1'b0, a16}); end
                    C_R1, C_KT: begin d_rk = RK_MEM; d_rp = sram({1'b1, a16}); end
                    C_KS: begin d_rk = RK_MEM; d_rp = sram(17'h08000 + {1'b0, a16}); end
                    C_F8: begin d_rk = RK_MEM; d_rp = romp({3'd0, a16} & rom_mask); end
                    C_RC: begin d_rk = RK_BUS; d_wk = WK_BUS; d_pla = 3'b000; end
                    C_UM, C_OP: begin d_rk = RK_BUS; d_wk = WK_BUS; end
                    C_RL, C_RH: begin d_rk = RK_BUS; end
                    C_CR: begin d_rk = RK_BUS; d_pla = 3'b011; end
                    C_CO: begin d_rk = RK_MEM; d_rp = sram({1'b1, a16}); d_wk = WK_BOTH; d_wp = sram({1'b1, a16}); end
                    C_IO: begin
                        d_rk = RK_BUS; d_wk = WK_BUS;
                        if (is_hwr) begin d_wk = WK_INT; if (is_hwb) d_rk = RK_INT; end
                        else if (is_iorm) begin d_rk = RK_MEM; d_rp = sram({1'b1, a16}); d_wk = WK_MEM; d_wp = sram({1'b1, a16}); end
                    end
                    default: ;
                endcase
                if (a16[15:1] == 15'd0) d_rk = RK_NONE;                                   // $0000/$0001: the CPU port (cpu65816_c64scpu.v)
            end else if (bank == 8'h01) begin
                d_rk = RK_MEM; d_rp = sram({1'b1, a16}); d_wk = WK_MEM; d_wp = sram({1'b1, a16});
            end else if (bank >= 8'hF8 || rr) begin
                d_rk = RK_MEM; d_rp = romp(b_a[18:0] & rom_mask); d_wk = WK_NONE;
            end else if (
`ifdef SCPU_MUT_NOALIAS
                1'b0
`else
                bank[7:1] == 7'b1111011
`endif
            ) begin                                                                       // $F6/$F7: SuperRAM bytes 0..$1FFFF, writes only with hwenable
                d_rk = RK_MEM; d_rp = {8'd0, b_a[16:0]}; d_wk = hw ? WK_MEM : WK_NONE; d_wp = {8'd0, b_a[16:0]};
            end else begin                                                                // $02-$F5: SuperRAM
`ifdef SCPU_MUT_ADDR23
                d_rk = RK_MEM; d_rp = {1'b0, 1'b0, bank[6:0], a16}; d_wk = WK_MEM; d_wp = {1'b0, 1'b0, bank[6:0], a16};
`else
                d_rk = RK_MEM; d_rp = {1'b0, bank, a16}; d_wk = WK_MEM; d_wp = {1'b0, bank, a16};
`endif
            end
        end
        // a write of bank 0 into RAM (the SRAM store) in the mirror range also goes to the C64 RAM: at 20 MHz through the queue below (the CPU's cycle stays a MEM write),
        // at 1 MHz as a cycle of both (the bus write in the CPU's own PHI2 cycle, PLA lines 000: RAM whatever the class)
        d_mir = b_bus && b_wr && bank == 8'h00 && d_wk == WK_MEM && a16[15:1] != 15'd0 && mir_range;
        if (d_mir && !fast) begin d_wk = WK_BOTH; d_pla = 3'b000; end
        // the hardware registers' read value ($D0B0-$D0BF): scpu64mem.c:634-690; every read ORs in the optimisation mode's low bits (reset 0xC7 -> 7)
        case (a16[3:0])
            4'h0: d_int = 8'h40;
            4'h1: d_int = 8'h00;
            4'h2: d_int = {hw, sys1, 6'd0};
            4'h3, 4'h4: d_int = {optim[7:6], 6'd0};
            4'h5: d_int = {sw_s1[1], sw1, 6'd0};
            4'h6: d_int = {e_flag, 7'd0};
            4'h7, 4'hA, 4'hB: d_int = 8'h00;
            4'h8, 4'h9: d_int = {soft1, soft1 | (sw1 & ~hw) | sys1, 6'd0};
            default: d_int = {dos, 7'd0};                                                 // $D0BC-$D0BF
        endcase
        d_int = d_int | {5'd0, optim[2:0]};
    end


    // ---- the cycle's classes, from the decision ----------------------------------------------------------------------------------------------
    wire port_acc = b_bus && bank == 8'h00 && a16[15:1] == 15'd0;                              // $00/$01: the CPU port (scpu_pport answers at the PHI2 fall)
    wire sync_w   = b_bus && (port_acc || (b_wr ? (d_wk == WK_BUS || d_wk == WK_BOTH || d_wk == WK_INT) : (d_rk == RK_BUS || d_rk == RK_INT)));
`ifdef SCPU_MUT_NOSYNC
    assign sync_cyc = 1'b0;                             // S10 control: the I/O cycle does not wait for PHI2 (the data would be wrong, the count is the check)
`else
    assign sync_cyc = sync_w;
`endif
    wire need_rd = settled && b_bus && !b_wr && d_rk == RK_MEM;
    wire need_wr = settled && b_bus && b_wr && (d_wk == WK_MEM || d_wk == WK_BOTH);
    wire mir_fast = d_mir && fast;                      // a mirrored write at 20 MHz: it sets the write buffer (scpu64_clock_write_stretch; unmirrored stores do not, scpu64mem.c:311-320)
`ifdef SCPU_MUT_WBALL
    wire wr_sram0 = b_bus && b_wr && bank == 8'h00 && (d_wk == WK_MEM || d_wk == WK_BOTH);   // S5 control: the step-4 rule, every bank-0 SRAM write sets the buffer
`else
    wire wr_sram0 = mir_fast;
`endif
    // the mirror queue: mp = the write waiting, ma = the write on the bus (or waiting to be repeated)
    reg        mp_v = 1'b0, ma_v = 1'b0, ma_drv = 1'b0, ma_fail = 1'b0;
    reg [15:0] mp_a = 16'd0, ma_a = 16'd0;
    reg  [7:0] mp_d = 8'd0,  ma_d = 8'd0;
    wire       mir_busy = mp_v || ma_v;
    wire       need_mir = settled && mir_fast;
    reg        aec_q = 1'b1;
    always @(posedge clk2x) aec_q <= aec;
    assign mir_drv = ma_drv; assign mir_a = ma_a; assign mir_d = ma_d;

    // ---- the cache -----------------------------------------------------------------------------------------------------------------------
    wire         wbusy, flushing;
    wire [140:0] cq;
    reg          wr_go = 1'b0, rd_done = 1'b0, rd_if = 1'b0;
    reg  [20:0]  rd_ln = 21'd0;
    reg  [24:0]  w_p = 25'd0;
    reg   [7:0]  w_d = 8'd0;
    wire         hit = cq[140] && cq[139:128] == d_rp[24:13] && !flushing;
    wire         f_go = need_rd && !hit;
    scpu_cache cache (
        .clk(clk2x), .flush(flush), .ix(a_out[12:4]), .cq_o(cq), .f_go(f_go), .f_if(b_vpa), .f_line(d_rp[24:4]), .rd_done(rd_done), .rd_if(rd_if), .rd_ln(rd_ln),
        .wr_go(wr_go), .w_p(w_p), .wr_d(w_d), .wbusy(wbusy), .flushing_o(flushing),
        .s_req_tgl(s_req_tgl), .s_req_line(s_req_line), .s_done_tgl(s_done_tgl), .fill(fill),
        .sw_req_tgl(sw_req_tgl), .sw_req_addr(sw_req_addr), .sw_req_data(sw_req_data), .sw_done_tgl(sw_done_tgl),
        .n_hit(n_hit), .n_miss(n_miss));
    assign mem_q = cq[8 * b_a[3:0] +: 8];
`ifdef SCPU_FLATMAP
    assign rd_mem = 1'b0;                               // the flat map of steps 1/2: every read is the C64 bus's
    assign rd_int = 1'b0;
    assign int_q = 8'd0;
`else
    assign rd_mem = d_rk == RK_MEM;
    assign rd_int = d_rk == RK_INT;
    assign int_q = d_int;
`endif

    // ---- the wait-state model (T5; transcription of scpu64cpu.c:129-325 for the classes the program sees) -------------------------------------
    // acc counts the 20 MHz enable: +203 per clk2x cycle, an enable at >= 640 (203/640 = 0.3172 per clk2x: 20 MHz of the 63.0556 MHz clk2x), so the spacing is 3 or 4 and never less;
    // a wait of w half-cycles is taken off the accumulator at the enable (acc -= 640 + w * 320): the enable comes later, never earlier. The accumulator does not run ahead
    // while an enable is withheld (no catching up with back-to-back enables).
    reg signed [13:0] acc = 14'sd0;
    wire pend = acc >= 14'sd640;
    reg  [6:0]  ph = 7'd0;                              // clk2x cycles since the start of this PHI2 cycle
    always @(posedge clk2x) if (l_ev) ph <= 7'd0; else if (ph != 7'd127) ph <= ph + 7'd1;
    // the SuperRAM cell / row model
    reg  [23:0] simm_cell = 24'd0;
    wire [23:0] cell_a   = bank[7:1] == 7'b1111011 ? {7'd0, b_a[16:0]} : b_a;
    wire        same_cell = simm_cell[23:2] == cell_a[23:2];
    wire        row_diff  = simm_cell[23:13] != cell_a[23:13];
    wire [23:0] cell_nx   = simm_cell + 24'd4;
    wire        next_cell = cell_nx[23:2] == cell_a[23:2];
    wire        is_rom = b_bus && (bank >= 8'hF8 || rr || (bank == 8'h00 && !b_wr && cls == C_F8));
    wire        is_sup = b_bus && bank >= 8'h02 && bank < 8'hF8;
    reg  [2:0]  w_h;                                    // wait in half 20 MHz cycles
    always @(*) begin
        w_h = 3'd0;
        if (is_rom) w_h = 3'd6;                                                           // EPROM +3 (read and write)
        else if (is_sup) begin
            if (!b_wr) w_h = same_cell ? 3'd0 : row_diff ? 3'd5 : next_cell ? 3'd0 : 3'd2;   // read: same cell 0, other row 2.5, next cell 0, else 1
            else       w_h = row_diff ? 3'd4 : 3'd2;                                         // write: other row 2, same row 1
        end
    end
    wire [10:0] w_acc = {w_h, 8'd0} + {w_h, 6'd0};                                          // w_h * 320
    // the write buffer: after a bank-0 SRAM write the next such write or I/O access waits until the next PHI2 boundary + 0.475 of a cycle (scpu64cpu.c:138-149, :244-254)
    reg  [1:0]  wb_st = 2'd0;
    reg  [5:0]  wb_t = 6'd0;
    wire        wb_busy = wb_st != 2'd0;
    always @(posedge clk2x) begin
        if (rst || !fast) wb_st <= 2'd0;
        else if (core_ce && wr_sram0) wb_st <= (ph >= 7'd57) ? 2'd2 : 2'd1;
        else if (l_ev && wb_st == 2'd2) wb_st <= 2'd1;
        else if (l_ev && wb_st == 2'd1) begin wb_st <= 2'd3; wb_t <= 6'd30; end
        else if (wb_st == 2'd3) begin if (wb_t == 6'd0) wb_st <= 2'd0; else wb_t <= wb_t - 6'd1; end
    end
    // the PHI2 synchronisation of an I/O class cycle: it needs its address on the bus when the chips look at the PHI2 rise (xc_phi2_p is at ph 12, measured): either in the PHI2 cycle,
    // when arm_go is seen at ph <= 12 (the address is on the bus in that same clock: fwd below does not wait for the register), or in the low phase after this cycle's fall (past_n: the
    // address of the cycle that ends is done), else it takes the next PHI2 cycle (a_skip); the data is the byte of the PHI2 fall (n_ev), and the CPU goes on in the clock after
    // n_d[1] (din_r reaches the CPU's registers two periods after n_ev + 1: multicycle 2, build_mono.tcl).
    reg armed = 1'b0, a_skip = 1'b0, bus_ok = 1'b0;
    reg [1:0] n_d = 2'b00;                              // n_ev delayed by two clocks
    always @(posedge clk2x) n_d <= {n_d[0], n_ev};
    // the mirror queue's drive: the write goes onto the C64 bus at the start of the next PHI2 cycle (or still in this one before ph 8: the address is on the bus before the PHI2 rise at ph 12),
    // the bus window ends two clocks after the PHI2 fall (n_d[1], as an I/O write); a write the VIC did not leave the bus for (aec low at the fall) is driven again in the next PHI2 cycle
    wire go_win = l_ev || ph <= 7'd7;
    always @(posedge clk2x) begin
        if (rst) begin mp_v <= 1'b0; ma_v <= 1'b0; ma_drv <= 1'b0; ma_fail <= 1'b0; end
        else begin
            if (core_ce && mir_fast) begin mp_v <= 1'b1; mp_a <= a16; mp_d <= b_d; end
            if (!ma_drv && go_win) begin
                if (ma_v) ma_drv <= 1'b1;
                else if (mp_v) begin ma_v <= 1'b1; ma_drv <= 1'b1; ma_a <= mp_a; ma_d <= mp_d; mp_v <= 1'b0; end
            end
            if (ma_drv && n_ev) ma_fail <= !aec_q;
            if (ma_drv && n_d[1]) begin
`ifdef SCPU_MUT_NORETRY
                ma_v <= 1'b0;
`else
                ma_v <= ma_fail;
`endif
                ma_drv <= 1'b0;
            end
        end
    end
    reg past_n = 1'b0;                                  // this PHI2 cycle's fall and the 2 clocks of its data are over: the next rise is the next cycle's
    always @(posedge clk2x) if (l_ev) past_n <= 1'b0; else if (n_d[1]) past_n <= 1'b1;
    wire arm_go = sync_cyc && settled && !armed && !bus_ok && !(fast && wb_busy) && !mir_busy;
`ifdef SCPU_MUT_LATEADDR
    wire skip_now = 1'b0;                               // S4 bus control: the address goes out whenever the class is known, also after the PHI2 rise
`else
    wire skip_now = ph > 7'd12 && !past_n && !n_d[1];
`endif
    always @(posedge clk2x) begin
        if (core_ce || rst) begin armed <= 1'b0; a_skip <= 1'b0; bus_ok <= 1'b0; end
        else begin
            if (arm_go) begin armed <= 1'b1; a_skip <= skip_now; end
            else if (armed && l_ev && a_skip) a_skip <= 1'b0;
            else if (armed && n_d[1] && !a_skip) bus_ok <= 1'b1;
        end
    end
    wire bus_up = (armed && !a_skip) || (arm_go && !skip_now);      // the address is on the C64 bus (this clock)
    wire bus_go = !sync_cyc || bus_ok || (armed && n_d[1] && !a_skip);

    wire mem_ok = (!need_rd || hit) && (!need_wr || !wbusy) && (!need_mir || !mp_v);
`ifdef SCPU_FLATMAP
    wire ok = 1'b1;
    wire tick = ce_raw;
    assign core_ce = ce_raw;
    assign fwd = bus_cyc;
    assign pla_pport = pport;
    assign mem_free = 1'b0;
`else
    wire wbuf_ok = !(fast && wb_busy && wr_sram0);
    wire ok = settled && rom_valid && !flushing && mem_ok && wbuf_ok && bus_go;
    wire tick = fast ? pend : ce_raw;
    assign core_ce = tick && ok;
    assign fwd = bus_up && b_bus && (b_wr ? (d_wk == WK_BUS || d_wk == WK_BOTH) : d_rk == RK_BUS);
    assign pla_pport = ma_drv ? 3'b000 : fwd ? d_pla : pport;
`ifdef SCPU_MUT_BASTALL
    assign mem_free = 1'b0;                              // S10 control: BA stops the CPU in 20 MHz mode as in 1 MHz mode
`else
    assign mem_free = fast && !sync_cyc;
`endif
`endif

    always @(posedge clk2x) begin
        if (rst || !fast) acc <= 14'sd0;
        else if (core_ce) acc <= acc - 14'sd437 - $signed({3'd0, w_acc});     // -640 and this clock's +203
        else if (!pend) acc <= acc + 14'sd203;
        if (core_ce && fast && is_sup) simm_cell <= cell_a;
        wr_go   <= core_ce && need_wr;
        rd_done <= core_ce && need_rd;
        rd_if   <= core_ce && need_rd && b_vpa;
        if (core_ce) rd_ln <= d_rp[24:4];
        if (core_ce) begin w_p <= d_wp; w_d <= b_d; end
        if (rst) begin
            boot <= BOOT_RST; dos <= 1'b0; hw <= 1'b0; sys1 <= 1'b0; soft1 <= 1'b0; optim <= 8'hC7; simm <= 8'd4;
        end else if (core_ce && b_bus && b_wr && d_wk == WK_INT) begin
            case (a16[7:0])
                8'h72: sys1 <= 1'b1;
                8'h73: sys1 <= 1'b0;
                8'h74, 8'h75, 8'h76, 8'h77: if (hw) optim <= {a16[1:0], 6'd0};
                8'h78: if (hw) simm <= b_d;
                8'h7A: soft1 <= 1'b1;
                8'h79, 8'h7B: soft1 <= 1'b0;
                8'h7E: hw <= 1'b1;
                8'h7D, 8'h7F: hw <= 1'b0;
                8'hB2: if (hw) begin sys1 <= b_d[6]; if (!b_d[7]) hw <= 1'b0; end
                8'hB3: if (hwg) optim <= (optim & 8'h38) | (b_d & 8'hC7);
                8'hB4: if (hwg) optim <= (optim & 8'h3F) | (b_d & 8'hC0);
                8'hB6: if (hw) boot <= 1'b0;
                8'hB7: if (hw) boot <= 1'b1;
                8'hB8: if (hwg) soft1 <= b_d[7];
                8'hBC: if (hwg) dos <= b_d[7];
                8'hBE: if (hw) dos <= 1'b1;
                8'hBD, 8'hBF: dos <= 1'b0;
                default: ;
            endcase
        end
        if (tick && !ok && rom_valid && !rst && settled && !mem_ok && dev_cyc != 32'hFFFFFFFF) dev_cyc <= dev_cyc + 32'd1;
    end
endmodule
