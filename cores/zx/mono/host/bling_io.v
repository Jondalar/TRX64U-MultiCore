// The app's keyboard-board registers (ultimate_sys hands over IO
// 0x10100600-0x101009FF, bl_addr = address bits 11:0):
//   0x800-0x803  Bling RX (software/system/u64.h BLING_RX_*): the bytes the
//                keyboard board sends on BLING_KEYB (bling_keyboard, sys_clk)
//                in a 16-byte FIFO
//                +0 DATA  oldest byte
//                +1 GET   write: drop it
//                +2 FLAGS b7 data available, b2 board installed, b1 SHIFT
//                         LOCK down, b0 shift lock off (write: the menu's
//                         scan sees no SHIFT from it)
//                +3 IRQEN b0: ITU high IRQ 4 while data is available
//   everything else (the app's LED registers 0x600-0x6FF and 0x900-0x9FF):
//                writes are ignored, reads give 0 (OPT-bling, A04: the LED
//                effects are out of the bitstream)
// BLING_LED carries a fixed WS2812 frame, refreshed all the time: 70 LEDs
// dark, the 71st is the C64U's power lamp (T159 D3/D4/D5, every core):
//   lamp_en = 0: dark, amber while SHIFT LOCK is locked
//   lamp_en = 1: red = the core runs (a real C64's power LED); SHIFT LOCK locked = yellow, steady, over everything;
//                else hd_act = blue/off flicker, else drive_act = red/off flicker (the red goes off); ~9 Hz, 50 %, from one free-running
//                phase; each activity is held for one full flicker period so a short access still shows. Levels 0x80 (the old 0x24 was
//                hardly visible on the board).
// WS2812: 800 kHz, 0 = 0.35 us high, 1 = 0.8 us high, then 310 us low.
module bling_io #(
    parameter integer CLK_HZ = 100000000,
    parameter integer FLICK_HALF = CLK_HZ / 18      // clocks per half period of the flicker: ~9 Hz
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        we,
    input  wire [11:0] addr,
    input  wire  [7:0] wdata,
    output reg   [7:0] rdata,               // one clock after addr
    output wire        irq,
    // from bling_keyboard (sys_clk): rx_tgl toggles with each byte in rx_byte
    input  wire        rx_tgl,
    input  wire  [7:0] rx_byte,
    input  wire        locked,
    input  wire        drive_act,           // a drive motor runs or the hard disk is accessed (cpu_clk); the floppy flicker
    input  wire        hd_act,              // the hard disk is accessed (cpu_clk): the blue flicker, wins over drive_act
    input  wire        lamp_en,             // the app switched "LED Load Indicator" on (cpu_clk)
    output reg         lock_off = 1'b0,
    output reg         bling_led = 1'b0
);
    wire sel_rx = addr[11:8] == 4'h8;

    // ---- Bling RX ----
    (* ASYNC_REG = "TRUE" *) reg [1:0] tgl_s = 2'b00, lock_s = 2'b00;
    reg        tgl_d = 1'b0;
    reg  [7:0] fifo [0:15];
    reg  [4:0] wp = 5'd0, rp = 5'd0;
    reg        irqen = 1'b0;
    wire       avail = wp != rp;
    wire       full  = (wp ^ rp) == 5'b10000;
    always @(posedge clk) begin
        tgl_s  <= {tgl_s[0], rx_tgl};
        lock_s <= {lock_s[0], locked};
        tgl_d  <= tgl_s[1];
        if (tgl_d != tgl_s[1] && !full) begin   // rx_byte has been still for 2 clocks
            fifo[wp[3:0]] <= rx_byte;
            wp <= wp + 5'd1;
        end
        if (we && sel_rx) case (addr[1:0])
            2'd1: if (avail) rp <= rp + 5'd1;
            2'd2: lock_off <= wdata[0];
            2'd3: irqen <= wdata[0];
            default: ;
        endcase
        if (rst) begin
            wp <= 5'd0; rp <= 5'd0; irqen <= 1'b0; lock_off <= 1'b0;
        end
    end
    assign irq = irqen & avail;

    reg        rsel = 1'b0;
    reg  [7:0] rx_rdata = 8'h00;
    always @(posedge clk) begin
        rsel <= sel_rx;
        case (addr[1:0])
        2'd0: rx_rdata <= fifo[rp[3:0]];
        2'd2: rx_rdata <= {avail, 4'd0, 1'b1, lock_s[1], lock_off};
        2'd3: rx_rdata <= {7'd0, irqen};
        default: rx_rdata <= 8'h00;
        endcase
    end
    always @(*) rdata = rsel ? rx_rdata : 8'h00;

    // ---- the keyboard's 71st LED (the power lamp), G R B ----
    localparam integer T_BIT = CLK_HZ / 800000;             // 1.25 us
    localparam integer T_0H  = CLK_HZ / 1000 * 35 / 100000; // 0.35 us
    localparam integer T_1H  = CLK_HZ / 1000 * 80 / 100000; // 0.8 us
    localparam integer T_RES = CLK_HZ / 1000 * 310 / 1000;  // 310 us
    localparam integer N_DARK = 70 * 24;                    // bits before the last LED
    localparam [23:0]  AMBER = 24'h1E2400;                  // SHIFT LOCK with the indicator off
    localparam [23:0]  YELLOW = 24'h808000;
    localparam [23:0]  RED   = 24'h008000;
    localparam [23:0]  BLUE  = 24'h000080;
    reg [14:0] t = 15'd0;                   // clocks in the bit, or in the reset gap
    reg [10:0] n = 11'd0;                   // bit of the frame
    reg        gap = 1'b1;
    reg [23:0] col = 24'h000000;            // the 71st LED's colour of this frame
    (* ASYNC_REG = "TRUE" *) reg [1:0] act_s = 2'b00, hd_s = 2'b00, en_s = 2'b00;
    always @(posedge clk) begin act_s <= {act_s[0], drive_act}; hd_s <= {hd_s[0], hd_act}; en_s <= {en_s[0], lamp_en}; end
    reg [23:0] fl_cnt = 24'd0, act_hold = 24'd0, hd_hold = 24'd0;
    reg        ph = 1'b1;                   // flicker phase: 1 = lit
    always @(posedge clk) begin
        if (fl_cnt == FLICK_HALF - 1) begin fl_cnt <= 24'd0; ph <= ~ph; end else fl_cnt <= fl_cnt + 24'd1;
        if (act_s[1]) act_hold <= 2 * FLICK_HALF; else if (act_hold != 24'd0) act_hold <= act_hold - 24'd1;
        if (hd_s[1])  hd_hold  <= 2 * FLICK_HALF; else if (hd_hold  != 24'd0) hd_hold  <= hd_hold  - 24'd1;
    end
    wire        hd_on  = hd_s[1]  || hd_hold  != 24'd0;
    wire        act_on = act_s[1] || act_hold != 24'd0;
    wire [23:0] col_next = en_s[1] ? (lock_s[1] ? YELLOW :
                                      hd_on     ? (ph ? BLUE : 24'h000000) :
                                      act_on    ? (ph ? RED  : 24'h000000) : RED)
                                   : (lock_s[1] ? AMBER : 24'h000000);
    wire [4:0] j    = n[4:0] - N_DARK[4:0]; // bit of the last LED (N_DARK mod 32 = 16)
    wire       dbit = n >= N_DARK && col[5'd23 - j];
    always @(posedge clk) begin
        if (gap) begin
            bling_led <= 1'b0;
            t <= t + 15'd1;
            if (t == T_RES) begin gap <= 1'b0; t <= 15'd0; end
        end else begin
            if (n == 11'd0 && t == 15'd0) col <= col_next;
            bling_led <= t < (dbit ? T_1H : T_0H);
            if (t == T_BIT - 1) begin
                t <= 15'd0;
                if (n == N_DARK + 23) begin n <= 11'd0; gap <= 1'b1; end
                else n <= n + 11'd1;
            end else
                t <= t + 15'd1;
        end
        if (rst) begin gap <= 1'b1; t <= 15'd0; n <= 11'd0; bling_led <= 1'b0; end
    end
endmodule
