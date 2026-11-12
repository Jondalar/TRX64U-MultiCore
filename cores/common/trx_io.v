// Our IO block for the app (docs/specs/ARCH-lean-state-access.md §10):
// 0x101A0000-0x101AFFFF in 4 KB pages, plus Gideon's GMod2 EEPROM window
// 0x1004C000. Pages built so far: 0 (ID, interrupt; from offset 0x100 the
// gate and capture control), 1 (state bus), 2 (engine, trx_engine.v), 4
// (cartridge, ARCH §7.4), 3 (debug, trx_dbg.v / drv_dbg.v), 5
// (lens unit, text layer view), 6 (video, ARCH §7.5: pix_layer.v, which has the page's
// map) and 7 (expansion port, T16 §13.7, trx_sys.v); the others read 0. In
// the app's clock; pages 1, 2, 3, 5 and 7 and
// page 0 from 0x100 live in the C64's clock (trx_sys.v, which has their map):
// their byte accesses go there over a toggle handshake and ack when the
// answer is back (some 10 clocks of this one), the others ack one clock after
// the address.
//
// Cartridge write-back, minimum hardware: cart_slot's flash responder stops
// at a program or erase and reports it here; the app applies it to the image
// in DDR2 with its own stores (AND for a program, $FF for an erase, its own
// sector map), then writes CMD = INVAL | ACK. Image byte of a chip address A
// (the app's layout, 16 KB per bank): EasyFlash {A[20:13], chip, A[12:0]},
// one-chip carts (GMod2, MegaByter, C64MegaCart) {A[20:13], 0, A[12:0]}.
// The dirty map, the quiet time and the write-back itself are the app's.
//
// Byte-wide IO bus, read data with ack; multi-byte registers little endian
// (offsets inside the page are ours, ARCH leaves them open; page 8 is the sampler's, below):
//   page 0  0x101A0000
//     000-003  ID         R    "TRX2"
//     004      CAPS       R    b0 page 0 gate/capture control, b1 page 1 (state
//                              bus), b2 page 2 (engine, frame ring), b3 page 3
//                              (debug, trx_dbg.v / drv_dbg.v), b4 page 4
//                              (cartridge), b5 page 5 (lens unit, TX_VIEW; the
//                              freeze session of T12 is gone, SLIM), b6 page 6 (video), b7 the pixel layer
//                              is RGB565 (PX_CTRL format 1, PX_KEY)
//     005      CAPS2      R    b0 the drain writes stream 2's F4 only after a
//                              frame's F3 marker (older bitstreams read 0: an
//                              F4 also after a block record with $F3 on top),
//                              b1 BUILD is there; b2 the CPU write path (T21 §5.1.1: state bus
//                              chip 1 w4/w5 the registers after the pending update, page 3 IPC_SET);
//                              b3 read 0 (was: the freeze session with commit list, SLIM D1;
//                              TX_CTRL b31:24 = 2 says the text layer takes TX_VIEW instead);
//                              b4 the lens unit (page 5 LK_REQ/LK_RES,
//                              T21 §3.4.1); b5 the engine's APPLY (page 2 AP_*, T20 §17.3);
//                              b6 the turbo engine (T29): TURBO_STAT and its registers on
//                              page 0 from 0x140;
//                              b7 the trace former (page 2 LOG_CTRL b4/b9, LOG_CNT[3], T21 §4.11.1)
//     006      CAPS3      R    b0 the SID is the 8-SID reDIP with the reSID8 filter control below
//                              (T25.5; bitstreams without it read 0, Gideon's too)
//     008-00B  BUILD      R    the bitstream's build stamp (USR_ACCESSE2, Vivado
//                              BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP); the boot
//                              ROM compares it with the AXSS write of the
//                              bitstream in flash (T15 §4.2)
//     020-023  DDR_CAL0   R    the DDR2 calibration (ddr2_ctrl.v, OPT-ddr2 §9): [4:0] IDELAY tap
//                              lane 0, [12:8] lane 1 (78 ps), [18:16] read delay, [27:24]
//                              bitslip position, [28] init done, [29] calibration done, [30]
//                              calibration ok, [31] controller up; older bitstreams read 0
//     024-027  DDR_CAL1   R    [5:0] window width lane 0, [13:8] lane 1 (taps), [16] / [17] window
//                              cut by the scan range, [23:20] positions that had a window.
//                              Final once DDR_CAL0 bit 29 is set (the boot ROM has waited for
//                              DDR2 before any app code runs); read the bytes of one word in a row
//     010      IRQ_EN     RW   cause bits below
//     014      IRQ_CAUSE  R/W1C b0 the machine stopped, b1 an engine job is done,
//                              b2 the log lost records, b3 a capture loaded the
//                              shadows (frame or stop), b4 flash operation
//                              pending, b5 EEPROM written, b6 video frame
//                              start (vsync), b7 page flip done
//     040-04B  RS8       RW   reSID8 filter control (T25.5, 8-SID reDIP only), engine 1 at 040/042/044-047, engine 2 at
//                              041/043/048-04B: 040/041 filter source b1:0 (0 the chip model's own, 1 own 6581 curve,
//                              2 own 8580, 3 reserved: acts as 2; no loadable table), 042/043 b0 resonance Auto, b1 digis Auto,
//                              044/045 + 048/049 6581 curve base (9 bit), 046/047 + 04A/04B offset (11 bit signed).
//                              04C/04D b0 filter bypass (reSID enable_filter(false)), engine 1 / 2.
//                              Reset: 0, 3, 245, -780, no bypass = the original reDIP
//     100-     (trx_sys.v)
//   page 4  0x101A4000
//     000      FL_ENABLE  W    b0 the flash responder answers (with the cartridge on)
//     004-005  FL_MAGIC1  W    chip-relative first unlock address ($555, $5555, $AAA)
//     008-009  FL_MAGIC2  W    second unlock address
//     00C-00D  FL_MASK    W    address bits compared ($7FF, $7FFF, $FFF)
//     010-012  FL_ID      W    manufacturer, device, device ID address (1 or 2)
//     020-023  EVT        R    the pending operation: [31:30] 1 program, 2 sector
//                              erase, 3 chip erase (0 none), [29] chip (EasyFlash
//                              ROMH = 1), [28:8] chip address (21 bit; bits 22:21 of an
//                              8 MiB part are in EVT+024 bits 1:0, T51), [7:0] data;
//                              reading byte 0 takes bytes 1-3 and 024
//     024      EVT_HI     R    b1:0 chip address bits 22:21 (taken with byte 0 of EVT)
//     038      XPORT_CAPS R    b0 the cartridge image window is 8 MiB (24-bit request address), b1 the
//                              EasyFlash 3 banking (descriptor registers R2, 9-bit bank, chip order, T51); older bitstreams read 0
//     028      CMD        W    b1 drop the cartridge's cached lines (after the
//                              app's stores, its D-cache written back), b0 ACK
//     02C-02D  WR_COUNT   R    +1 per flash operation and per EEPROM write (the
//                              EEPROM copy's seqlock); reading byte 0 takes byte 1
//     030      ROM_SEL    RW   b1:0 where cart_fill reads the cartridge ROM image (T60):
//                              0 DDR2 0x3C00000 (Gideon's layout; the reset value, what his
//                              firmware expects), 1 the expansion-port block 0x1000000 (shared
//                              with the REU, the app's CRT image), 2 the built-in cart slot
//                              0xF00000 (boot cart, SID/MOD player: never on top of REU data).
//                              b7:2 write ignored, read 0: a bitstream without the register
//                              reads back 0 after a write of 1, which is how the app knows
//     040-06A  DESC       W    the cartridge's mapping descriptor (T33,
//                              TREX-PROP-002), byte 0x40 + n = desc[8n +: 8];
//                              the field map is in vendor/icepi/c64_cartridge.v.
//                              cart_slot copies it at every CPU reset and
//                              cartridge force: the app writes it with the C64
//                              held in reset, before it lets go
//   0x1004C000 (addr b16 = 1), Gideon's window (CAPAB_EEPROM)
//     000      dirty      R    != 0: the EEPROM changed; any write clears it
//     800-FFF  array      RW   the 93C86's 2 KB (byte 2n = high half of word n)
//   page 8  0x101A8000 and 0x10048000 (SAMPLER_BASE, ultimate_sys.vhd decodes both here), T25.6
//     00-FF    the Ultimate Audio sampler's byte window (trx_sampler.v in the C64's clock, 8 voices x 32 bytes, spec
//              T25.6 3): a byte access takes the toggle handshake (some 10 clocks)
// The interrupt is ITU high interrupt 2 (drive B's WD177x in ultimate_logic_32;
// we have no drive B). irq_in(4) is the UCI's (T28, uci_unit.vhd).
module trx_io (
    input  wire        clk,
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,            // b16: the EEPROM window, else 0x101A0000 + addr
    input  wire  [7:0] wdata,
    output wire  [7:0] rdata,
    output reg         ack = 1'b0,      // rdata valid / write done
    output wire        irq,

    // trx_sys.v (C64 clock): one toggle per byte access, the answer toggles back
    output reg         rq_tgl = 1'b0,
    output reg  [15:0] rq_addr = 16'd0,
    output reg         rq_we = 1'b0,
    output reg   [7:0] rq_wdata = 8'd0,
    input  wire        rs_tgl,
    input  wire  [7:0] rs_rdata,
    input  wire        stop_tgl,        // the machine stopped (C64 clock)
    input  wire        cap_tgl,         // a capture (C64 clock)
    input  wire        eng_tgl,         // an engine job is done (C64 clock)
    input  wire        gap_tgl,         // the log lost records (C64 clock)

    // page 8 (0x10048000-0x100480FF, the sampler's bytes, T25.6; also 0x101A8000): trx_sampler.v in the C64's
    // clock: one toggle per byte access, address and data held, the answer toggles back with the read data
    output reg         sm_tgl = 1'b0,
    output reg   [7:0] sm_addr = 8'd0,
    output reg         sm_we = 1'b0,
    output reg   [7:0] sm_wdata = 8'd0,
    input  wire        sm_rs_tgl,
    input  wire  [7:0] sm_rdata,

    // page 6: pix_layer's registers (its read data one clock after the address)
    output wire        p6_we,
    output wire        p6_re,
    input  wire  [7:0] p6_rdata,
    input  wire        ev_vsync,        // one-clock pulses
    input  wire        ev_flip,

    // cart_slot (C64 clock)
    output reg         fl_enable = 1'b0,
    output reg  [15:0] fl_magic1 = 16'd0,
    output reg  [15:0] fl_magic2 = 16'd0,
    output reg  [15:0] fl_mask = 16'd0,
    output reg  [23:0] fl_id = 24'd0,
    output reg [343:0] cart_desc = 344'd0,
    input  wire        evt_tgl,         // data stable from the toggle until the ACK
    input  wire [33:0] evt_word,
    output reg         fl_ack_tgl = 1'b0,
    output reg         inval_tgl = 1'b0,
    output reg   [1:0] rom_sel = 2'd0,  // ROM_SEL (sys clk: top.v takes it into ui_clk)
    input  wire        ee_tgl,
    // the EEPROM's second port, read data one clock later
    output wire [10:0] ee_addr,
    output wire        ee_we,
    output wire  [7:0] ee_wdata,
    input  wire  [7:0] ee_rdata,
    input  wire [31:0] build,           // USR_ACCESSE2 DATA, constant after configuration
    input  wire [63:0] ddr_cal,         // DDR2 calibration result, ui_clk, static once done (DDR_CAL0/1)

    // T25.5: CAPS3 (0x006) and the reSID8 filter control (0x040-0x04B), docs/specs/T25.5-resid8-menus.md
    input  wire  [7:0] caps3,
    output wire [49:0] rs8_cfg          // {bypass 2, offset 2 x 11, base 2 x 9, auto 2 x 2, filter source 2 x 2}
);
    // reset values = the original reDIP: filter by the chip model, resonance and digis Auto,
    // base 245, offset -780
    reg  [1:0] rs8_fcsel [0:1];
    reg  [1:0] rs8_auto  [0:1];
    reg  [8:0] rs8_base  [0:1];
    reg [10:0] rs8_off   [0:1];
    reg  [1:0] rs8_byp = 2'b00;     // 1 = the engine's filter is bypassed (reSID enable_filter(false))
    initial begin
        rs8_fcsel[0] = 2'd0; rs8_fcsel[1] = 2'd0;
        rs8_auto[0]  = 2'd3; rs8_auto[1]  = 2'd3;
        rs8_base[0]  = 9'd245; rs8_base[1] = 9'd245;
        rs8_off[0]   = 11'h4F4; rs8_off[1] = 11'h4F4;      // -780
    end
    assign rs8_cfg = {rs8_byp, rs8_off[1], rs8_off[0], rs8_base[1], rs8_base[0],
                      rs8_auto[1], rs8_auto[0], rs8_fcsel[1], rs8_fcsel[0]};

    (* ASYNC_REG = "TRUE" *) reg [2:0] evt_s = 3'b000, ee_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rs_s = 3'b000, stop_s = 3'b000, cap_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [2:0] eng_s = 3'b000, gap_s = 3'b000;
    always @(posedge clk) begin
        evt_s  <= {evt_s[1:0], evt_tgl};
        ee_s   <= {ee_s[1:0], ee_tgl};
        rs_s   <= {rs_s[1:0], rs_tgl};
        stop_s <= {stop_s[1:0], stop_tgl};
        cap_s  <= {cap_s[1:0], cap_tgl};
        eng_s  <= {eng_s[1:0], eng_tgl};
        gap_s  <= {gap_s[1:0], gap_tgl};
    end
    wire evt_ev  = evt_s[2] != evt_s[1];
    wire ee_ev   = ee_s[2] != ee_s[1];
    wire rs_ev   = rs_s[2] != rs_s[1];
    wire stop_ev = stop_s[2] != stop_s[1];
    wire cap_ev  = cap_s[2] != cap_s[1];

    (* ASYNC_REG = "TRUE" *) reg [2:0] sm_s = 3'b000;
    always @(posedge clk) sm_s <= {sm_s[1:0], sm_rs_tgl};
    wire sm_ev = sm_s[2] != sm_s[1];
    wire        win  = addr[16];
    wire  [3:0] page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    // the C64-clock pages
    wire        far  = !win && (page == 4'd1 || page == 4'd2 || page == 4'd3 || page == 4'd5 ||
                                page == 4'd7 || (page == 4'd0 && off[11:8] != 4'd0));
    wire        w0   = we && !win && page == 4'd0 && !far;
    wire        w4   = we && !win && page == 4'd4;
    wire        smp  = !win && page == 4'd8;
    reg         smp_busy = 1'b0, q_sm = 1'b0;
    reg   [7:0] sm_q = 8'd0;
    assign p6_we = we && !win && page == 4'd6;
    assign p6_re = re && !win && page == 4'd6;

    reg  [33:0] evt = 34'd0, evt_q = 34'd0;
    reg  [15:0] wr_count = 16'd0;
    reg   [7:0] wr_count_q = 8'd0;
    reg         ee_gid = 1'b0;
    wire        eng_ev = eng_s[2] != eng_s[1];
    wire        gap_ev = gap_s[2] != gap_s[1];
    reg   [7:0] irq_en = 8'd0, cause = 8'd0;    // the register bits
    wire  [7:0] ev = {ev_flip, ev_vsync, ee_ev, evt_ev, cap_ev, gap_ev, eng_ev, stop_ev};
    reg   [7:0] q = 8'd0;
    reg         q_ee = 1'b0, q_p6 = 1'b0;
    reg         far_busy = 1'b0;

    always @(posedge clk) begin
        wr_count <= wr_count + evt_ev + ee_ev;
        if (evt_ev) evt <= evt_word;
        cause <= cause | ev;
        if (ee_ev) ee_gid <= 1'b1;

        if (w0 && off == 12'h010) irq_en <= wdata;
        if (w0 && off == 12'h014)
            cause <= (cause & ~wdata) | ev;

        // ack: one clock after the address here, when trx_sys answers there
        ack <= 1'b0;
        if ((re || we) && far) begin
            rq_addr  <= addr[15:0];
            rq_we    <= we;
            rq_wdata <= wdata;
            rq_tgl   <= ~rq_tgl;
            far_busy <= 1'b1;
        end else if ((re || we) && !smp)
            ack <= 1'b1;
        if ((re || we) && smp) begin
            sm_addr  <= off[7:0];
            sm_we    <= we;
            sm_wdata <= wdata;
            sm_tgl   <= ~sm_tgl;
            smp_busy <= 1'b1;
        end
        if (smp_busy && sm_ev) begin
            smp_busy <= 1'b0;
            ack      <= 1'b1;
            sm_q     <= sm_rdata;
        end
        q_sm <= smp;
        if (far_busy && rs_ev) begin
            far_busy <= 1'b0;
            ack <= 1'b1;
        end
        if (w0)
            case (off)
            12'h040: rs8_fcsel[0]    <= wdata[1:0];
            12'h041: rs8_fcsel[1]    <= wdata[1:0];
            12'h042: rs8_auto[0]     <= wdata[1:0];
            12'h043: rs8_auto[1]     <= wdata[1:0];
            12'h044: rs8_base[0][7:0]  <= wdata;
            12'h045: rs8_base[0][8]    <= wdata[0];
            12'h046: rs8_off[0][7:0]   <= wdata;
            12'h047: rs8_off[0][10:8]  <= wdata[2:0];
            12'h048: rs8_base[1][7:0]  <= wdata;
            12'h049: rs8_base[1][8]    <= wdata[0];
            12'h04A: rs8_off[1][7:0]   <= wdata;
            12'h04B: rs8_off[1][10:8]  <= wdata[2:0];
            12'h04C: rs8_byp[0]        <= wdata[0];
            12'h04D: rs8_byp[1]        <= wdata[0];
            default: ;
            endcase
        if (w4)
            case (off)
            12'h000: fl_enable        <= wdata[0];
            12'h004: fl_magic1[7:0]   <= wdata;
            12'h005: fl_magic1[15:8]  <= wdata;
            12'h008: fl_magic2[7:0]   <= wdata;
            12'h009: fl_magic2[15:8]  <= wdata;
            12'h00C: fl_mask[7:0]     <= wdata;
            12'h00D: fl_mask[15:8]    <= wdata;
            12'h010: fl_id[7:0]       <= wdata;
            12'h011: fl_id[15:8]      <= wdata;
            12'h012: fl_id[23:16]     <= wdata;
            12'h030: rom_sel          <= wdata[1:0];
            12'h028: begin
                if (wdata[1]) inval_tgl <= ~inval_tgl;
                if (wdata[0]) begin
                    fl_ack_tgl     <= ~fl_ack_tgl;
                    evt[31:30]     <= 2'd0;
                end
            end
            default:
                if (off >= 12'h040 && off <= 12'h06A)
                    cart_desc[8*(off - 12'h040) +: 8] <= wdata;
            endcase
        if (we && win && off == 12'h000) ee_gid <= ee_ev;
        if (re && !win && page == 4'd4 && off == 12'h020) evt_q <= evt;
        if (re && !win && page == 4'd4 && off == 12'h02C) wr_count_q <= wr_count[15:8];

        q_ee <= win && off[11] && !far_busy && !far;
        q_p6 <= !win && page == 4'd6 && !far_busy;
        if (!far_busy) q <= 8'd0;
        if (far_busy) begin
            if (rs_ev) q <= rs_rdata;
        end else if (win) begin
            if (off == 12'h000) q <= {7'd0, ee_gid};
        end else if (page == 4'd0)
            case (off)
            12'h000: q <= "T";
            12'h001: q <= "R";
            12'h002: q <= "X";
            12'h003: q <= "2";
            12'h004: q <= 8'hFF;
            12'h005: q <= 8'hF7;
            12'h006: q <= caps3;
            12'h008: q <= build[7:0];
            12'h009: q <= build[15:8];
            12'h00A: q <= build[23:16];
            12'h00B: q <= build[31:24];
            12'h020: q <= ddr_cal[7:0];
            12'h021: q <= ddr_cal[15:8];
            12'h022: q <= ddr_cal[23:16];
            12'h023: q <= ddr_cal[31:24];
            12'h024: q <= ddr_cal[39:32];
            12'h025: q <= ddr_cal[47:40];
            12'h026: q <= ddr_cal[55:48];
            12'h027: q <= ddr_cal[63:56];
            12'h010: q <= irq_en;
            12'h014: q <= cause;
            12'h040: q <= {6'd0, rs8_fcsel[0]};
            12'h041: q <= {6'd0, rs8_fcsel[1]};
            12'h042: q <= {6'd0, rs8_auto[0]};
            12'h043: q <= {6'd0, rs8_auto[1]};
            12'h044: q <= rs8_base[0][7:0];
            12'h045: q <= {7'd0, rs8_base[0][8]};
            12'h046: q <= rs8_off[0][7:0];
            12'h047: q <= {5'd0, rs8_off[0][10:8]};
            12'h048: q <= rs8_base[1][7:0];
            12'h049: q <= {7'd0, rs8_base[1][8]};
            12'h04A: q <= rs8_off[1][7:0];
            12'h04B: q <= {5'd0, rs8_off[1][10:8]};
            12'h04C: q <= {7'd0, rs8_byp[0]};
            12'h04D: q <= {7'd0, rs8_byp[1]};
            default: ;
            endcase
        else if (page == 4'd4)
            case (off)
            12'h020: q <= evt[7:0];
            12'h021: q <= evt_q[15:8];
            12'h022: q <= evt_q[23:16];
            12'h023: q <= evt_q[31:24];
            12'h024: q <= {6'd0, evt_q[33:32]};
            12'h02C: q <= wr_count[7:0];
            12'h02D: q <= wr_count_q;
            12'h030: q <= {6'd0, rom_sel};
            12'h038: q <= 8'h03;                    // XPORT_CAPS (T51)
            default: ;
            endcase
    end
    assign rdata = q_sm ? sm_q : q_ee ? ee_rdata : q_p6 ? p6_rdata : q;
    assign irq   = (irq_en & cause) != 8'd0;

    assign ee_addr  = off[10:0];
    assign ee_we    = we && win && off[11];
    assign ee_wdata = wdata;
endmodule
