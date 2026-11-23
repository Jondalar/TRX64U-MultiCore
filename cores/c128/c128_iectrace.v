// The bus trace of the serial port (T170 D2): every edge of ATN / CLK / DATA / SRQ with its time and the fast serial direction, in a ring of 1024
// events, so a tester's failed load with a real drive can be read from the bug report (T162) without the drive. Written for this project (TREX).
// Generic page (the application reads it for any core that has it): at cw_addr[16:12] = 4'hF of the core window, in cpu_clk. Absent offsets read 0.
//   000-003 ID       "IECT"
//   004 VERSION      0x01
//   005 DEPTH_LOG2   10 (the ring holds 1024 events)
//   008-00B TICK_HZ  R 32 bit: the clock the times count in (the machine's clock of the System Mode)
//   010 CTRL         W b0 = 1: clear the ring and the counters; W b1 = 1: take a snapshot of the registers below; R b0: a snapshot is being taken
//   014-017 EV_TOTAL R 32 bit (snapshot): events written since the clear (the newest is number EV_TOTAL-1; slot = number mod 1024)
//   018-019 LIVE     R 16 bit (snapshot): the state now, bits as an event's low 10 bits
//   020-023 CNT_ATN, 024-027 CNT_CLK, 028-02B CNT_DATA, 02C-02F CNT_SRQ   R 32 bit (snapshot): every edge of the bus line as the core hears it, since the
//                    clear - also those the ring leaves out while a fast serial burst runs
//   030-031 RD_IDX   RW slot number (b9:0)
//   034-037 RD_DATA  R 32 bit: the event in slot RD_IDX
// An event: [31:16] ticks since the previous event (saturates at 65535; the first event after the clear counts from the clear),
//   [9] C128 mode, [8] FSDIR (1 = the machine sends), [7:4] the board port's lines, [3:0] the bus as the core hears it; line bits {SRQ, DATA, CLK, ATN}, 1 = high.
// Rule of the ring: a change of ATN, CLK, FSDIR or the mode is an event. A change of DATA or SRQ is one too, except while SRQ is moving (a burst: SRQ changed
// less than 256 ticks ago, 8 us) - the burst's first edge is an event, and so is the state after its last (when SRQ has been quiet for 256 ticks).
// The registers are read after a snapshot (CTRL b1, wait for CTRL b0 = 0): the counters of the sys_clk side are then still while the bytes are read.
`timescale 1ns / 1ps
module c128_iectrace (
    input  wire        sys_clk,
    input  wire  [3:0] bus,             // sys_clk: the bus as the core hears it {SRQ, DATA, CLK, ATN}, 1 = high
    input  wire  [3:0] port,            // the board port's lines as they reach the core
    input  wire        fs_dir,
    input  wire        c128_mode,       // 1 = C128 mode
    input  wire [31:0] tick_hz,         // read back as TICK_HZ (stable)
    input  wire        clk,             // cpu_clk: the register interface
    input  wire        we, re,
    input  wire [16:0] addr,
    input  wire  [7:0] wdata,
    output wire  [7:0] rdata,
    output wire        ack
);
    localparam [9:0] FAST = 10'b00_1100_1100;    // the DATA and SRQ bits of the bus and of the port
    localparam [8:0] BURST = 9'd256;             // 8 us: the bit clock of a fast serial byte is faster than that, the gap between two bytes is not

    // ---- sys_clk: the recorder ------------------------------------------------------------------------------------------
    wire [9:0] cur = {c128_mode, fs_dir, port, bus};
    reg  [9:0] prev = 10'h3FF, rec = 10'h3FF;
    reg  [15:0] dt = 16'hFFFF;
    reg   [8:0] sage = 9'h1FF;
    reg  [9:0] wp = 10'd0;
    reg [31:0] total = 32'd0, c_atn = 32'd0, c_clk = 32'd0, c_dat = 32'd0, c_srq = 32'd0;
    (* ram_style = "block" *) reg [31:0] ring [0:1023];

    wire [9:0] diff = cur ^ rec;
    wire       burst = sage < BURST;
    wire       ev = ((diff & ~FAST) != 10'd0) || (((diff & FAST) != 10'd0) && !burst);
    wire       srq_moved = (cur[3] != prev[3]) || (cur[7] != prev[7]);

    // requests from the register interface: a toggle each, seen here after two flip-flops
    reg        clr_tgl = 1'b0, snap_tgl = 1'b0;                  // cpu_clk
    (* ASYNC_REG = "TRUE" *) reg [2:0] clr_s = 3'd0, snap_s = 3'd0;
    wire       clr_ev  = clr_s[2]  != clr_s[1];
    wire       snap_ev = snap_s[2] != snap_s[1];
    reg        done_tgl = 1'b0;                                  // sys_clk: the snapshot is taken
    reg [31:0] s_total = 32'd0, s_atn = 32'd0, s_clk = 32'd0, s_dat = 32'd0, s_srq = 32'd0;
    reg  [9:0] s_live = 10'd0;

    always @(posedge sys_clk) begin
        clr_s  <= {clr_s[1:0], clr_tgl};
        snap_s <= {snap_s[1:0], snap_tgl};
        prev <= cur;
        if (dt != 16'hFFFF) dt <= dt + 16'd1;
        if (srq_moved) sage <= 9'd0; else if (sage != 9'h1FF) sage <= sage + 9'd1;
        if (cur[0] != prev[0]) c_atn <= c_atn + 32'd1;
        if (cur[1] != prev[1]) c_clk <= c_clk + 32'd1;
        if (cur[2] != prev[2]) c_dat <= c_dat + 32'd1;
        if (cur[3] != prev[3]) c_srq <= c_srq + 32'd1;
        if (ev) begin
            ring[wp] <= {dt, 6'd0, cur};
            wp    <= wp + 10'd1;
            total <= total + 32'd1;
            rec   <= cur;
            dt    <= 16'd0;
        end
        if (snap_ev) begin
            s_total <= total; s_atn <= c_atn; s_clk <= c_clk; s_dat <= c_dat; s_srq <= c_srq; s_live <= cur;
            done_tgl <= ~done_tgl;
        end
        if (clr_ev) begin
            wp <= 10'd0; total <= 32'd0; c_atn <= 32'd0; c_clk <= 32'd0; c_dat <= 32'd0; c_srq <= 32'd0;
            dt <= 16'hFFFF;
        end
    end

    // ---- cpu_clk: the registers -----------------------------------------------------------------------------------------
    wire        sel = !addr[16] && addr[15:12] == 4'hF;
    wire [11:0] off = addr[11:0];
    reg   [9:0] idx = 10'd0;
    reg   [7:0] q = 8'd0;
    reg         ack_r = 1'b0;
    reg  [31:0] rd_q = 32'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] done_s = 2'd0;
    wire        busy = (snap_tgl != done_s[1]);

    always @(posedge clk) rd_q <= ring[idx];

    always @(posedge clk) begin
        done_s <= {done_s[0], done_tgl};
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        if (we && sel) begin
            case (off)
                12'h010: begin
                    if (wdata[0]) clr_tgl <= ~clr_tgl;
                    if (wdata[1] && !busy) snap_tgl <= ~snap_tgl;
                end
                12'h030: idx[7:0] <= wdata;
                12'h031: idx[9:8] <= wdata[1:0];
                default: ;
            endcase
        end
        q <= 8'h00;
        if (sel) begin
            case (off)
                12'h000: q <= "I";
                12'h001: q <= "E";
                12'h002: q <= "C";
                12'h003: q <= "T";
                12'h004: q <= 8'h01;
                12'h005: q <= 8'd10;
                12'h008: q <= tick_hz[7:0];
                12'h009: q <= tick_hz[15:8];
                12'h00A: q <= tick_hz[23:16];
                12'h00B: q <= tick_hz[31:24];
                12'h010: q <= {7'd0, busy};
                12'h014: q <= s_total[7:0];
                12'h015: q <= s_total[15:8];
                12'h016: q <= s_total[23:16];
                12'h017: q <= s_total[31:24];
                12'h018: q <= s_live[7:0];
                12'h019: q <= {6'd0, s_live[9:8]};
                12'h020: q <= s_atn[7:0];   12'h021: q <= s_atn[15:8];   12'h022: q <= s_atn[23:16];   12'h023: q <= s_atn[31:24];
                12'h024: q <= s_clk[7:0];   12'h025: q <= s_clk[15:8];   12'h026: q <= s_clk[23:16];   12'h027: q <= s_clk[31:24];
                12'h028: q <= s_dat[7:0];   12'h029: q <= s_dat[15:8];   12'h02A: q <= s_dat[23:16];   12'h02B: q <= s_dat[31:24];
                12'h02C: q <= s_srq[7:0];   12'h02D: q <= s_srq[15:8];   12'h02E: q <= s_srq[23:16];   12'h02F: q <= s_srq[31:24];
                12'h030: q <= idx[7:0];
                12'h031: q <= {6'd0, idx[9:8]};
                12'h034: q <= rd_q[7:0];
                12'h035: q <= rd_q[15:8];
                12'h036: q <= rd_q[23:16];
                12'h037: q <= rd_q[31:24];
                default: ;
            endcase
        end
    end
    assign rdata = q;
    assign ack   = ack_r;
endmodule
