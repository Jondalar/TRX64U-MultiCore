// The c64u core's window for the application (T103): the cw_* port of the contract, decoded here. Register map:
// c64u_cw.vh (and software/io/c64u/c64u_cw.h of the application). Derived from trx64/trx_io.v (T103 Q5: a copy,
// the original is not touched) with the pages of the ring, the engine, the state bus and the debugger gone, and the
// pages of the stock C64U set added (capability word, level interrupts, the UDP streams' template RAM, the UCI, the
// tape streamer and recorder).
//
// 0x101A0000-0x101AFFFF in 4 KB pages (cw_addr[16] = 0), plus Gideon's GMod2 EEPROM window 0x1004C000 (cw_addr[16] = 1).
// In the app's clock (cpu_clk); pages 0 from 0x100, 5 and 7 live in the C64's clock (c64u_sys.v): their byte accesses go
// there over a toggle handshake and ack when the answer is back (some 10 clocks of this one). Page 8 (the sampler's byte
// window, also 0x10048000) the same over its own toggle pair. The UCI and the two tape windows are Gideon's blocks on
// his io bus: the request is a one-clock pulse, the answer comes with their own ack. The rest ack one clock after the
// address. Absent addresses read 0.
//
// Cartridge write-back, minimum hardware: cart_slot_c64u's flash responder stops at a program or erase and reports it
// here; the app applies it to the image in DDR2 with its own stores (AND for a program, $FF for an erase, its own
// sector map), then writes CMD = INVAL | ACK. Image byte of a chip address A (the app's layout, 16 KB per bank):
// EasyFlash {A[20:13], chip, A[12:0]}, one-chip carts (GMod2, MegaByter, C64MegaCart) {A[20:13], 0, A[12:0]}.
//
// Byte-wide IO bus, read data with ack; multi-byte registers little endian:
//   page 0  0x101A0000
//     000-003  ID         R    "C64U"
//     004      CAPS       R    0xD0: b4 page 4 (cartridge), b6 page 6 (video), b7 the pixel layer is RGB565
//     005      CAPS2      R    0x42: b1 BUILD is there, b6 the turbo engine (T29): TURBO_STAT and its registers from 0x140
//     006      CAPS3      R    b0 the SID is the 8-SID reDIP with the reSID8 filter control below (T25.5), b1 the c64u
//                              map: pages 9-D (streams, UCI, tape), LEVEL_IRQ, CORE_CAPS, PIN_CTRL
//     008-00B  BUILD      R    the bitstream's build stamp (USR_ACCESSE2)
//     010      IRQ_EN     RW   cause bits below
//     014      IRQ_CAUSE  R/W1C b4 flash operation pending, b5 EEPROM written, b6 video frame start (vsync), b7 page flip done
//     018      LEVEL_IRQ  R    live levels, not masked: b0 UCI, b1 drive A (WD177x), b2 0 (no drive B), b3 tape recorder
//     019      LEVEL_IRQ_EN RW  reset 0. The interrupt line (ITU high 2) = (IRQ_EN & IRQ_CAUSE) != 0 | (LEVEL_IRQ_EN & LEVEL_IRQ) != 0
//     020-027  DDR_CAL0/1 R    as the reference core (ddr2_ctrl.v)
//     040-04D  RS8        RW   reSID8 filter control (T25.5), as the reference core
//     070-073  CORE_CAPS  R    the capability word: host_caps | this core's bits (c64u_cw.vh)
//     078      PIN_CTRL   RW   reset 0 = every pin released and ignored. b0 drive MOTOR with the machine's motor bit, b1 drive WRITE with
//                              the machine's write line, b2 drive READ with the streamer's line, b3 SENSE pulled low while the streamer or the
//                              recorder ask for it, b4 the SENSE and READ pins feed the machine (an external datasette), b5 drive PADDLE_DIS with
//                              the SID's discharge phase. The direction of the C2N and PADDLE pins is not known from any source (T103 §5): nothing
//                              is driven until a board session sets these bits.
//     088-094  SOCK_*     RW   T37, the SID sockets, at Gideon's C64_IO_BASE offsets + 0x80 (system/u64.h): 088/089 SID1/2_BASE
//                              (A11..A4, reset 0x01 = unmapped), 08C/08D SID1/2_MASK (reset 0xFE), 091/092 SID1/2_EN (b0), 094
//                              STEREO_ADDRSEL (0 off, 1-5: A5-A9 is the sockets' SID_ADDR). Quasi-static; the C64 side takes
//                              them through two flip-flops
//     0A4-0A7  SOCK_GAIN  RW   T37, U64_AUDIO_MIXER bytes 4-7 (socket 1 R, L, socket 2 R, L; 0x80 = 0 dB, reset 0): the host
//                              keeps those bytes, so the application writes them here as well
//     100-     (c64u_sys.v)
//   page 4  0x101A4000   cartridge, as the reference core
//     000 FL_ENABLE W, 004/005 FL_MAGIC1 W, 008/009 FL_MAGIC2 W, 00C/00D FL_MASK W, 010-012 FL_ID W, 020-023 EVT R (reading byte 0
//     takes bytes 1-3 and 024), 024 EVT_HI R, 028 CMD W (b1 drop the cached lines, b0 ACK), 02C/02D WR_COUNT R, 030 ROM_SEL RW
//     (b1:0 where cart_fill_c64u reads the ROM image: 0 DDR2 0x3C00000, 1 the expansion-port block 0x1000000, 2 the built-in slot
//     0xF00000), 038 XPORT_CAPS R (0x03), 040-06A DESC W (the cartridge's mapping descriptor, TREX-PROP-002)
//   page 5  0x101A5028 TX_VIEW (c64u_sys.v); page 6  0x101A6000 the video layer (the host's pix_layer, its own registers)
//   page 7  0x101A7000 expansion port (c64u_sys.v)
//   0x1004C000 (addr b16 = 1), Gideon's window (CAPAB_EEPROM): 000 dirty R (!= 0: the EEPROM changed; any write clears it),
//     800-FFF array RW (the 93C86's 2 KB, byte 2n = high half of word n)
//   page 8  0x101A8000 and 0x10048000 (SAMPLER_BASE), T25.6: 00-FF the Ultimate Audio sampler's byte window (trx_sampler_c64u.v in the
//     C64's clock, 8 voices x 32 bytes), a byte access takes the toggle handshake (some 10 clocks)
//   page 9  0x101A9000   the UDP streams (udp_stream_c64u / udp_audio_c64u on the packet port)
//     000-0FF  template RAM   W    the headers of the two streams, as the host's r_udp region took them (reads 0)
//     100      STREAM_ENA     RW   b0 video stream, b1 audio stream
//     101      STREAM_MUTE    RW   b0 mutes the audio of the streams (set with C64_STOP; the core mutes it too while the machine is paused)
//   page A,B 0x101AA000  UCI (the 8 KB range of Gideon's command_interface, as the host's old 0x10044000)
//   page C  0x101AC000   the tape streamer (c2n_playback_io, 4 KB: 000 control/status, 800-FFF FIFO)
//   page D  0x101AD000   the tape recorder (c2n_record, 4 KB)
module c64u_io (
    input  wire        clk,
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,            // b16: the EEPROM window, else 0x101A0000 + addr
    input  wire  [7:0] wdata,
    output wire  [7:0] rdata,
    output reg         ack = 1'b0,      // rdata valid / write done
    output wire        irq,

    // c64u_sys.v (C64 clock): one toggle per byte access, the answer toggles back
    output reg         rq_tgl = 1'b0,
    output reg  [15:0] rq_addr = 16'd0,
    output reg         rq_we = 1'b0,
    output reg   [7:0] rq_wdata = 8'd0,
    input  wire        rs_tgl,
    input  wire  [7:0] rs_rdata,

    // page 8: trx_sampler_c64u.v in the C64's clock: one toggle per byte access, address and data held, the answer toggles back
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

    // cart_slot_c64u (C64 clock)
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
    output reg   [1:0] rom_sel = 2'd0,  // ROM_SEL (sys clk: the layer takes it into ui_clk)
    input  wire        ee_tgl,
    // the EEPROM's second port, read data one clock later
    output wire [10:0] ee_addr,
    output wire        ee_we,
    output wire  [7:0] ee_wdata,
    input  wire  [7:0] ee_rdata,
    input  wire [31:0] build,           // USR_ACCESSE2 DATA, constant after configuration
    input  wire [63:0] ddr_cal,         // DDR2 calibration result, ui_clk, static once done (DDR_CAL0/1)

    // T25.5: CAPS3 b0 and the reSID8 filter control (0x040-0x04B)
    input  wire        sid_resid8,
    output wire [49:0] rs8_cfg,         // {bypass 2, offset 2 x 11, base 2 x 9, auto 2 x 2, filter source 2 x 2}

    // the capability word (page 0 0x070), the level interrupts, the pins
    input  wire [31:0] core_caps,
    input  wire  [3:0] level_irq,       // cpu_clk levels: UCI, drive A, 0 (no drive B), tape recorder
    output reg   [7:0] pin_ctrl = 8'd0,

    // T37: the SID sockets' addressing and the mixer's socket gains (quasi-static, this clock)
    output reg  [15:0] sock_base = 16'h0101,    // {socket 2, socket 1} A11..A4
    output reg  [15:0] sock_mask = 16'hFEFE,
    output reg   [1:0] sock_en = 2'b00,
    output reg   [2:0] sock_split = 3'd0,
    output reg  [31:0] sock_gain = 32'd0,       // mixer bytes 7..4

    // page 9: the streams
    output wire        udp_we,
    output wire  [7:0] udp_addr,
    output wire  [7:0] udp_wdata,
    output reg   [7:0] stream_ena = 8'd0,
    output reg         stream_mute = 1'b0,

    // pages A/B (UCI) and C/D (tape) on Gideon's io bus: a pulse, the answer with its ack
    output wire        uci_re,
    output wire        uci_we,
    output wire [12:0] uci_addr,
    input  wire  [7:0] uci_rdata,
    input  wire        uci_ack,
    output wire        pb_re,
    output wire        pb_we,
    input  wire  [7:0] pb_rdata,
    input  wire        pb_ack,
    output wire        rc_re,
    output wire        rc_we,
    input  wire  [7:0] rc_rdata,
    input  wire        rc_ack
);
    // reset values = the original reDIP: filter by the chip model, resonance and digis Auto, base 245, offset -780
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
    (* ASYNC_REG = "TRUE" *) reg [2:0] rs_s = 3'b000;
    always @(posedge clk) begin
        evt_s  <= {evt_s[1:0], evt_tgl};
        ee_s   <= {ee_s[1:0], ee_tgl};
        rs_s   <= {rs_s[1:0], rs_tgl};
    end
    wire evt_ev  = evt_s[2] != evt_s[1];
    wire ee_ev   = ee_s[2] != ee_s[1];
    wire rs_ev   = rs_s[2] != rs_s[1];

    (* ASYNC_REG = "TRUE" *) reg [2:0] sm_s = 3'b000;
    always @(posedge clk) sm_s <= {sm_s[1:0], sm_rs_tgl};
    wire sm_ev = sm_s[2] != sm_s[1];
    wire        win  = addr[16];
    wire  [3:0] page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    // the C64-clock pages
    wire        far  = !win && (page == 4'd5 || page == 4'd7 || (page == 4'd0 && off[11:8] != 4'd0));
    wire        w0   = we && !win && page == 4'd0 && !far;
    wire        w4   = we && !win && page == 4'd4;
    reg         cd_we = 1'b0;                   // the staged descriptor write (below)
    reg   [5:0] cd_i = 6'd0;
    reg   [7:0] cd_wd = 8'd0;
    wire        smp  = !win && page == 4'd8;
    wire        p9   = !win && page == 4'd9;
    wire        puci = !win && (page == 4'd10 || page == 4'd11);
    wire        ppb  = !win && page == 4'd12;
    wire        prc  = !win && page == 4'd13;
    wire        dev  = puci || ppb || prc;
    reg         smp_busy = 1'b0, q_sm = 1'b0;
    reg   [7:0] sm_q = 8'd0;
    assign p6_we = we && !win && page == 4'd6;
    assign p6_re = re && !win && page == 4'd6;

    assign udp_we    = we && p9 && !off[8];
    assign udp_addr  = off[7:0];
    assign udp_wdata = wdata;
    assign uci_re   = re && puci;
    assign uci_we   = we && puci;
    assign uci_addr = addr[12:0];
    assign pb_re    = re && ppb;
    assign pb_we    = we && ppb;
    assign rc_re    = re && prc;
    assign rc_we    = we && prc;

    reg  [33:0] evt = 34'd0, evt_q = 34'd0;
    reg  [15:0] wr_count = 16'd0;
    reg   [7:0] wr_count_q = 8'd0;
    reg         ee_gid = 1'b0;
    reg   [7:0] irq_en = 8'd0, cause = 8'd0;    // the register bits
    reg   [3:0] lvl_en = 4'd0;
    wire  [7:0] ev = {ev_flip, ev_vsync, ee_ev, evt_ev, 4'b0000};
    reg   [7:0] q = 8'd0;
    reg         q_ee = 1'b0, q_p6 = 1'b0;
    reg         far_busy = 1'b0;
    reg   [7:0] q_dev = 8'd0;
    reg         use_dev = 1'b0;
    wire        dev_ack = uci_ack || pb_ack || rc_ack;
    reg         dev_busy = 1'b0;

    always @(posedge clk) begin
        wr_count <= wr_count + evt_ev + ee_ev;
        if (evt_ev) evt <= evt_word;
        cause <= cause | ev;
        if (ee_ev) ee_gid <= 1'b1;

        if (w0 && off == 12'h010) irq_en <= wdata;
        if (w0 && off == 12'h014)
            cause <= (cause & ~wdata) | ev;
        if (w0 && off == 12'h019) lvl_en <= wdata[3:0];
        if (w0 && off == 12'h078) pin_ctrl <= wdata;
        if (w0)
            case (off)
            12'h088: sock_base[7:0]   <= wdata;
            12'h089: sock_base[15:8]  <= wdata;
            12'h08C: sock_mask[7:0]   <= wdata;
            12'h08D: sock_mask[15:8]  <= wdata;
            12'h091: sock_en[0]       <= wdata[0];
            12'h092: sock_en[1]       <= wdata[0];
            12'h094: sock_split       <= wdata[2:0];
            12'h0A4: sock_gain[7:0]   <= wdata;
            12'h0A5: sock_gain[15:8]  <= wdata;
            12'h0A6: sock_gain[23:16] <= wdata;
            12'h0A7: sock_gain[31:24] <= wdata;
            default: ;
            endcase
        if (we && p9 && off == 12'h100) stream_ena <= wdata;
        if (we && p9 && off == 12'h101) stream_mute <= wdata[0];

        // ack: one clock after the address here, when c64u_sys answers there, with the device's ack for the pages A-D
        ack <= 1'b0;
        if ((re || we) && far) begin
            rq_addr  <= addr[15:0];
            rq_we    <= we;
            rq_wdata <= wdata;
            rq_tgl   <= ~rq_tgl;
            far_busy <= 1'b1;
        end else if ((re || we) && !smp && !dev)
            ack <= 1'b1;
        if ((re || we) && dev) begin
            dev_busy <= 1'b1;
            use_dev  <= 1'b1;
        end else if ((re || we) && !dev)
            use_dev <= 1'b0;
        if (dev_busy && dev_ack) begin
            dev_busy <= 1'b0;
            ack      <= 1'b1;
            q_dev    <= uci_ack ? uci_rdata : pb_ack ? pb_rdata : rc_rdata;
        end
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
            default: ;
            endcase
        // the descriptor's byte is written one clock later: 344 flip-flops behind a decode of the host's address
        // were the slowest path across the partition edge (T103 §5); nothing here reads cart_desc back
        cd_we <= w4 && off >= 12'h040 && off <= 12'h06A;
        cd_i  <= off[5:0];                       // 0x040-0x06A: bytes 0-42
        cd_wd <= wdata;
        if (cd_we) cart_desc[8*cd_i +: 8] <= cd_wd;
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
            12'h000: q <= "C";
            12'h001: q <= "6";
            12'h002: q <= "4";
            12'h003: q <= "U";
            12'h004: q <= 8'hD0;
            12'h005: q <= 8'h42;
            12'h006: q <= {6'd0, 1'b1, sid_resid8};
            12'h008: q <= build[7:0];
            12'h009: q <= build[15:8];
            12'h00A: q <= build[23:16];
            12'h00B: q <= build[31:24];
            12'h018: q <= {4'd0, level_irq};
            12'h019: q <= {4'd0, lvl_en};
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
            12'h070: q <= core_caps[7:0];
            12'h071: q <= core_caps[15:8];
            12'h072: q <= core_caps[23:16];
            12'h073: q <= core_caps[31:24];
            12'h078: q <= pin_ctrl;
            12'h088: q <= sock_base[7:0];
            12'h089: q <= sock_base[15:8];
            12'h08C: q <= sock_mask[7:0];
            12'h08D: q <= sock_mask[15:8];
            12'h091: q <= {7'd0, sock_en[0]};
            12'h092: q <= {7'd0, sock_en[1]};
            12'h094: q <= {5'd0, sock_split};
            12'h0A4: q <= sock_gain[7:0];
            12'h0A5: q <= sock_gain[15:8];
            12'h0A6: q <= sock_gain[23:16];
            12'h0A7: q <= sock_gain[31:24];
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
        else if (page == 4'd9)
            case (off)
            12'h100: q <= stream_ena;
            12'h101: q <= {7'd0, stream_mute};
            default: ;
            endcase
    end
    assign rdata = use_dev ? q_dev : q_sm ? sm_q : q_ee ? ee_rdata : q_p6 ? p6_rdata : q;
    assign irq   = ((irq_en & cause) != 8'd0) || ((lvl_en & level_irq) != 4'd0);

    assign ee_addr  = off[10:0];
    assign ee_we    = we && win && off[11];
    assign ee_wdata = wdata;
endmodule
