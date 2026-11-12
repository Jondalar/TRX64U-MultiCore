// T113 VDC variant 2: only what the CPU can see of the 8563/8568 VDC, in gateware. Written for this project from the
// register description (C128 Programmer's Reference Guide), not from the MiSTer RTL; the MiSTer VDC was read for the corner
// cases of the update-address/data-register pipeline (R18/R19/R31) and of block copy/fill.
//
//  In gateware: the address register ($D600) and the data port ($D601); the register file R0-R37 with the field widths and
//  the read-back values of the real chip; the status byte (bit 7 ready, bit 5 vertical blank, bits 1:0 version); the VRAM
//  (16 KiB or 64 KiB, block RAM) behind R18/R19 (update address) and R31 (data) with the auto-increment; block fill/copy
//  (R24 bit 7, R30 word count, R32/R33 source); a raster counter just good enough for the status bit 5 and for a frame number
//  (R0, R4, R5, R6, R9, R22 high nibble set the geometry); a dirty flag per 256 VRAM bytes.
//  NOT in gateware (the application): everything that draws - screen/attribute/character fetch, attribute decoding (colour,
//  reverse, underline, blink, alternate set), cursor, smooth scroll, double width, interlace, bitmap mode, the RGBI palette.
//  The application reads VRAM, the register file, the frame number and the dirty flags through the window vd_* (vd_clk):
//    0x0000-0x3FFF (0xFFFF with 64 KiB) VRAM, read; 0x4000+n R_n (n < 38); 0x4040 frame number (low 8 bits),
//    0x4041 b0 = in vertical blank; 0x4042 b0 = a dirty snapshot is ready, write = take one (snapshot+clear, toggles);
//    0x4080-0x40BF dirty snapshot (64 bits, 8 bytes at 0x4080-0x4087).
//  Light pen: not implemented (R16/R17 read 0, status bit 6 stays 0).
`timescale 1ns / 1ps
module vdc_cpu #(
    parameter RAM_BITS = 14                      // 14: 16 KiB, 16: 64 KiB
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        bus_en,                   // one pulse per CPU cycle: the access takes effect
    input  wire        cs, rs, we,
    input  wire  [7:0] db_in,
    output reg   [7:0] db_out,
    // the application's window (vd_clk)
    input  wire        vd_clk,
    input  wire [15:0] vd_addr,
    input  wire        vd_we,
    input  wire  [7:0] vd_wdata,
    output reg   [7:0] vd_rdata
);
    // ------------------------------------------------------------- register file
    reg  [7:0] r_ht, r_hd, r_hp, r_vt, r_vd, r_vp, r_ai, r_wc, r_da, r_deb, r_dee;
    reg  [3:0] r_vw, r_hw, r_cth, r_cdh, r_hss, r_fg, r_bg, r_drr;
    reg  [4:0] r_va, r_ctv, r_cs, r_ce, r_cdv, r_vss, r_ul;
    reg  [1:0] r_im, r_cm;
    reg [15:0] r_ds, r_cp, r_ua, r_aa, r_ba;
    reg        r_copy, r_rvs, r_cbr, r_text, r_atr, r_semi, r_dbl, r_ram;
    reg  [2:0] r_cb;
    reg        r_hspol, r_vspol;
    reg  [5:0] rsel;

    // ------------------------------------------------------------- VRAM, one write port, one read port per side
    (* ram_style = "block" *) reg [7:0] vram [0:(1<<RAM_BITS)-1];
    reg  [RAM_BITS-1:0] va;
    reg                 vwe;
    reg           [7:0] vwd;
    reg           [7:0] vrd;
    always @(posedge clk) begin
        if (vwe) vram[va] <= vwd;
        vrd <= vram[va];
    end
    reg [7:0] vrd_b;
    always @(posedge vd_clk) vrd_b <= vram[vd_addr[RAM_BITS-1:0]];

    // ------------------------------------------------------------- the CPU-visible FSM (UA/DA pipeline, fill, copy)
    localparam IDLE = 4'd0, RD0 = 4'd1, RD1 = 4'd2, RD2 = 4'd8, WR = 4'd3, FILL = 4'd4, CP0 = 4'd5, CP1 = 4'd6, CP2 = 4'd7;
    reg  [3:0] st;
    reg  [7:0] wc, cda;
    wire       busy = st != IDLE || rst;

    function [RAM_BITS-1:0] fold(input [15:0] a);
        fold = a[RAM_BITS-1:0];
    endfunction

    // dirty flags: one per 256 bytes of the 16 bit space
    reg [63:0] dirty, snap;
    reg        snap_rdy, take_tgl_s;
    (* ASYNC_REG = "TRUE" *) reg [2:0] take_s;
    reg        take_tgl = 1'b0;                  // vd_clk
    reg        snap_tgl = 1'b0;                  // clk: a snapshot was taken
    (* ASYNC_REG = "TRUE" *) reg [1:0] snap_s;

    wire acc   = cs && bus_en;
    wire accw  = acc && we;
    wire accr  = acc && !we;

    always @(posedge clk) begin
        vwe <= 1'b0;
        if (rst) begin
            st <= IDLE; rsel <= 6'd0;
            r_ht <= 0; r_hd <= 0; r_hp <= 0; r_vt <= 0; r_vd <= 0; r_vp <= 0; r_ai <= 0; r_wc <= 0; r_da <= 0;
            r_deb <= 0; r_dee <= 0; r_vw <= 0; r_hw <= 0; r_cth <= 0; r_cdh <= 0; r_hss <= 0; r_fg <= 0; r_bg <= 0;
            r_drr <= 0; r_va <= 0; r_ctv <= 0; r_cs <= 0; r_ce <= 0; r_cdv <= 0; r_vss <= 0; r_ul <= 0; r_im <= 0;
            r_cm <= 0; r_ds <= 0; r_cp <= 0; r_ua <= 0; r_aa <= 0; r_ba <= 0; r_copy <= 0; r_rvs <= 0; r_cbr <= 0;
            r_text <= 0; r_atr <= 0; r_semi <= 0; r_dbl <= 0; r_ram <= 0; r_cb <= 0; r_hspol <= 0; r_vspol <= 0;
            wc <= 0; cda <= 0; dirty <= 64'd0; snap <= 64'd0; snap_tgl <= 1'b0;
        end else begin
            // ---- the bus
            if (accw) begin
                if (!rs) rsel <= db_in[5:0];
                else case (rsel)
                    0: r_ht <= db_in;      1: r_hd <= db_in;       2: r_hp <= db_in;
                    3: begin r_vw <= db_in[7:4]; r_hw <= db_in[3:0]; end
                    4: r_vt <= db_in;      5: r_va <= db_in[4:0];  6: r_vd <= db_in;      7: r_vp <= db_in;
                    8: r_im <= db_in[1:0]; 9: r_ctv <= db_in[4:0];
                    10: begin r_cm <= db_in[6:5]; r_cs <= db_in[4:0]; end
                    11: r_ce <= db_in[4:0];
                    12: r_ds[15:8] <= db_in; 13: r_ds[7:0] <= db_in;
                    14: r_cp[15:8] <= db_in; 15: r_cp[7:0] <= db_in;
                    18: begin r_ua[15:8] <= db_in; st <= RD0; end
                    19: begin r_ua[7:0]  <= db_in; st <= RD0; end
                    20: r_aa[15:8] <= db_in; 21: r_aa[7:0] <= db_in;
                    22: begin r_cth <= db_in[7:4]; r_cdh <= db_in[3:0]; end
                    23: r_cdv <= db_in[4:0];
                    24: begin r_copy <= db_in[7]; r_rvs <= db_in[6]; r_cbr <= db_in[5]; r_vss <= db_in[4:0]; end
                    25: begin r_text <= db_in[7]; r_atr <= db_in[6]; r_semi <= db_in[5]; r_dbl <= db_in[4]; r_hss <= db_in[3:0]; end
                    26: begin r_fg <= db_in[7:4]; r_bg <= db_in[3:0]; end
                    27: r_ai <= db_in;
                    28: begin r_cb <= db_in[7:5]; r_ram <= db_in[4]; end
                    29: r_ul <= db_in[4:0];
                    30: begin r_wc <= db_in; wc <= db_in; st <= r_copy ? CP0 : FILL; end
                    31: begin cda <= db_in; st <= WR; end
                    32: r_ba[15:8] <= db_in; 33: r_ba[7:0] <= db_in;
                    34: r_deb <= db_in; 35: r_dee <= db_in; 36: r_drr <= db_in[3:0];
                    37: begin r_hspol <= db_in[7]; r_vspol <= db_in[6]; end
                    default: ;
                endcase
            end
            if (accr && rs && rsel == 31) begin r_ua <= r_ua + 16'd1; st <= RD0; end

            // ---- the RAM side
            case (st)
                RD0:  begin va <= fold(r_ua); st <= RD1; end
                RD1:  st <= RD2;                                                // the block RAM's read register is two clocks behind va
                RD2:  begin st <= IDLE; r_da <= vrd; end
                WR:   begin va <= fold(r_ua); vwd <= cda; vwe <= 1'b1; dirty[r_ua[15:8]] <= 1'b1;
                            r_ua <= r_ua + 16'd1; st <= RD0; end
                FILL: begin va <= fold(r_ua); vwd <= cda; vwe <= 1'b1; dirty[r_ua[15:8]] <= 1'b1;
                            r_ua <= r_ua + 16'd1; wc <= wc - 8'd1; if (wc == 8'd1) st <= IDLE; end
                CP0:  begin va <= fold(r_ba); st <= CP1; end
                CP1:  begin r_ba <= r_ba + 16'd1; st <= CP2; end
                CP2:  begin va <= fold(r_ua); vwd <= vrd; vwe <= 1'b1; dirty[r_ua[15:8]] <= 1'b1;
                            r_ua <= r_ua + 16'd1; wc <= wc - 8'd1; st <= (wc == 8'd1) ? IDLE : CP0; end
                default: ;
            endcase

            // ---- the application's snapshot request
            take_s <= {take_s[1:0], take_tgl};
            if (take_s[2] != take_s[1]) begin snap <= dirty; dirty <= 64'd0; snap_tgl <= ~snap_tgl; end
        end
    end
    // FILL uses cda as its data (R31's last written byte) as on the real chip (R31 holds the fill value)

    // ------------------------------------------------------------- the raster, just for status bit 5 and the frame number
    reg  [7:0] hc;      // character in the line
    reg  [3:0] pc;      // pixel in the character
    reg  [4:0] sl;      // scan line in the row
    reg  [7:0] row;
    reg        in_adj;
    reg  [4:0] adj;
    reg        pce;
    reg  [7:0] frame;
    reg        vblank;
    always @(posedge clk) begin
        pce <= rst ? 1'b0 : ~pce;
        if (rst) begin hc <= 0; pc <= 0; sl <= 0; row <= 0; in_adj <= 0; adj <= 0; frame <= 0; vblank <= 1'b1; end
        else if (pce) begin
            if (pc == r_cth) begin
                pc <= 0;
                if (hc == r_ht) begin
                    hc <= 0;
                    if (in_adj) begin
                        if (adj == r_va) begin in_adj <= 0; adj <= 0; row <= 0; sl <= 0; frame <= frame + 8'd1; vblank <= 1'b0; end
                        else adj <= adj + 5'd1;
                    end else if (sl == r_ctv) begin
                        sl <= 0;
                        if (row == r_vd - 8'd1) vblank <= 1'b1;
                        if (row == r_vt) begin
                            if (r_va != 5'd0) in_adj <= 1'b1;
                            else begin row <= 0; frame <= frame + 8'd1; vblank <= 1'b0; end
                        end else row <= row + 8'd1;
                    end else sl <= sl + 5'd1;
                end else hc <= hc + 8'd1;
            end else pc <= pc + 4'd1;
        end
    end

    // ------------------------------------------------------------- read-back
    wire [7:0] status = {~busy, 1'b0, vblank, 3'b000, 1'b0, 1'b1};
    function [7:0] rd_reg(input [5:0] n);
        case (n)
            0: rd_reg = r_ht;  1: rd_reg = r_hd;  2: rd_reg = r_hp;  3: rd_reg = {r_vw, r_hw};
            4: rd_reg = r_vt;  5: rd_reg = {3'b111, r_va}; 6: rd_reg = r_vd; 7: rd_reg = r_vp;
            8: rd_reg = {6'b111111, r_im}; 9: rd_reg = {3'b111, r_ctv};
            10: rd_reg = {1'b1, r_cm, r_cs}; 11: rd_reg = {3'b111, r_ce};
            12: rd_reg = r_ds[15:8]; 13: rd_reg = r_ds[7:0]; 14: rd_reg = r_cp[15:8]; 15: rd_reg = r_cp[7:0];
            16: rd_reg = 8'd0; 17: rd_reg = 8'd0;
            18: rd_reg = r_ua[15:8]; 19: rd_reg = r_ua[7:0]; 20: rd_reg = r_aa[15:8]; 21: rd_reg = r_aa[7:0];
            22: rd_reg = {r_cth, r_cdh}; 23: rd_reg = {3'b111, r_cdv};
            24: rd_reg = {r_copy, r_rvs, r_cbr, r_vss}; 25: rd_reg = {r_text, r_atr, r_semi, r_dbl, r_hss};
            26: rd_reg = {r_fg, r_bg}; 27: rd_reg = r_ai; 28: rd_reg = {r_cb, r_ram, 4'b1111}; 29: rd_reg = {3'b111, r_ul};
            30: rd_reg = r_wc; 31: rd_reg = r_da; 32: rd_reg = r_ba[15:8]; 33: rd_reg = r_ba[7:0];
            34: rd_reg = r_deb; 35: rd_reg = r_dee; 36: rd_reg = {4'b1111, r_drr}; 37: rd_reg = {r_hspol, r_vspol, 6'b111111};
            default: rd_reg = 8'hFF;
        endcase
    endfunction
    always @(posedge clk) db_out <= !rs ? status : rd_reg(rsel);

    // ------------------------------------------------------------- the application's window (vd_clk)
    (* ASYNC_REG = "TRUE" *) reg [1:0] snap_s1;
    reg  snap_seen = 1'b0;
    reg  [7:0] f_s, f_s2;                       // frame number and blank, 2 flops (the app reads them as quasi-static)
    reg        b_s, b_s2;
    reg [63:0] snap_b;
    always @(posedge vd_clk) begin
        snap_s1 <= {snap_s1[0], snap_tgl};
        f_s <= frame; f_s2 <= f_s; b_s <= vblank; b_s2 <= b_s;
        if (vd_we && vd_addr == 16'h4042) take_tgl <= ~take_tgl;
        if (snap_s1[1] != snap_seen) begin snap_seen <= snap_s1[1]; snap_b <= snap; end
        case (vd_addr[15:14])
            2'b00: vd_rdata <= vrd_b;
            2'b01: case (vd_addr[7:6])
                2'b00: vd_rdata <= vd_addr[5:0] < 6'd38 ? rd_reg(vd_addr[5:0]) : 8'hFF;
                2'b01: vd_rdata <= vd_addr[1:0] == 2'd0 ? f_s2 : vd_addr[1:0] == 2'd1 ? {7'd0, b_s2} : {7'd0, snap_s1[1] == snap_seen};
                2'b10: vd_rdata <= snap_b[vd_addr[2:0]*8 +: 8];
                default: vd_rdata <= 8'hFF;
            endcase
            default: vd_rdata <= 8'hFF;
        endcase
    end
endmodule
