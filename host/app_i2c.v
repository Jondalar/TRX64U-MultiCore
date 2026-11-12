// The Ultimate app's hardware I2C master (0x10100700, t_hw_i2c in
// software/io/i2c/hw_i2c.h), on the three buses our board logic also uses.
//   +0 W send a byte (the first one after idle sends a START first), R the
//      byte received;  +1 R status: b7 busy, b4 last byte NACKed, b3 missed
//      write, b2 NACK (b2/b3 cleared by the read), b0 started;
//   +2 W repeated START;  +3 W STOP;  +4 W receive a byte and NACK it,
//   +5 W receive and ACK;  +6 channel (0 HDMI, 1 1.8 V, 2 3.3 V);
//   +7 W b0 soft reset;  +8 W b0 scan_enable, b1 CDCE open (T115 S9).
// 100 kHz, four quarter-bit phases per bit, clock stretching honoured.
//
// Sharing: our keyboard scanner (c64u_keyboard) has the buses while the app
// leaves them alone. The app claims them with its first command while
// scan_enable is 0 (Gideon's driver clears it before every locked access);
// the master stays busy until the scanner has stopped between two
// transactions (kb_parked), then drives the pins (own). scan_enable = 1
// gives the buses back once the master is idle and stopped. A command while
// scan_enable is 1 runs on a disconnected bus and reads NACK. Before the
// synthesizers are programmed (i2c_done) the app never gets the buses.
//
// T40: hold (from mode_ctl, any clock) parks the master between two
// transactions while the synthesizers are reprogrammed: no new claim, and a
// command waits (busy, lines released) until hold is gone; parked says the
// buses are free of the app (not owned, or owned and parked with no
// transaction open).
//
// The CDCE synthesizers (7-bit 0x64) are not the app's: a write addressed to
// 0x64 runs disconnected from the pins with every byte ACKed, until the STOP
// or a repeated START. Exception (T115 S9, D8): with CDCE open (+8 b1 = 1) a
// write to 0x64 goes out like any other; the host app sets it only right
// before the hand-over to a full firmware, which then owns the synthesizers.
// Every write to +8 sets it (Gideon's driver writes b1 = 0), so does soft reset and rst.
//
// Pins: index 0 = 1.8 V, 1 = HDMI, 2 = 3.3 V (top.v's order), 1 = pull low.
module app_i2c #(
    parameter DIV = 250                         // clk / 400 kHz
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       we,
    input  wire       re,
    input  wire [3:0] addr,
    input  wire [7:0] wdata,
    output reg  [7:0] rdata,                    // the clock after re
    output reg        scan_en,

    input  wire       i2c_done,                 // any clock
    output reg        kb_pause,
    input  wire       kb_parked,                // any clock
    output reg        own,
    output reg  [2:0] scl_lo = 3'b000,       // registered: no decode glitches on the pins
    output reg  [2:0] sda_lo = 3'b000,
    input  wire [2:0] scl_in,                   // pin levels (async)
    input  wire [2:0] sda_in,
    input  wire       hold,                     // any clock
    output wire       parked
);
    localparam S_IDLE = 3'd0, S_START = 3'd1, S_TX = 3'd2, S_ACK = 3'd3, S_RX = 3'd4,
               S_RXACK = 3'd5, S_STOP = 3'd6, S_RSTART = 3'd7;

    (* ASYNC_REG = "TRUE" *) reg [1:0] done_s = 2'b00, park_s = 2'b00, hold_s = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [2:0] scl_s1 = 3'b111, scl_s2 = 3'b111, sda_s1 = 3'b111, sda_s2 = 3'b111;
    always @(posedge clk) begin
        done_s <= {done_s[0], i2c_done};
        park_s <= {park_s[0], kb_parked};
        hold_s <= {hold_s[0], hold};
        scl_s1 <= scl_in; scl_s2 <= scl_s1;
        sda_s1 <= sda_in; sda_s2 <= sda_s1;
    end

    reg  [2:0] state;
    reg  [1:0] chan;
    reg  [7:0] dout;
    reg  [2:0] bitc;
    reg        started, err, temp_err, missed, do_ack, blk, want, first;
    reg        cdce_open = 1'b0;
    reg  [4:0] park_cnt;
    reg        held = 1'b0;                     // parked by hold
    assign parked = !own || held;

    // the app's channel on top.v's bus index
    wire [1:0] bus = (chan == 2'd0) ? 2'd1 : (chan == 2'd1) ? 2'd0 : (chan == 2'd2) ? 2'd2 : 2'd3;
    wire       conn = own && !blk && bus != 2'd3;
    reg        scl_o = 1'b1, sda_o = 1'b1;      // 1 = released
    wire       scl_line = conn ? scl_s2[bus] : scl_o;
    wire       sda_line = conn ? sda_s2[bus] : sda_o;
    always @(posedge clk) begin
        scl_lo <= (conn && !scl_o) ? (3'b001 << bus) : 3'b000;
        sda_lo <= (conn && !sda_o) ? (3'b001 << bus) : 3'b000;
    end

    // ---- four-phase sequencer ----------------------------------------------------
    reg  [3:0] q_scl, q_sda;                    // phase 3 first
    reg        go, sbusy, sdone, sampled;
    reg  [1:0] cnt;
    reg  [8:0] div;
    wire       run = (own && !held) || !want;   // stalled while waiting for the buses, or held
    wire       stretch = (state == S_IDLE) || !run || (scl_o && !scl_line);
    wire       tick = !stretch && div == 9'd0;
    always @(posedge clk) begin
        if (stretch || div == 9'd0) div <= DIV - 1;
        else div <= div - 9'd1;
        sdone <= 1'b0;
        if (!sbusy) begin
            cnt <= 2'd3;
            if (go) sbusy <= 1'b1;
        end else begin
            scl_o <= q_scl[cnt];
            sda_o <= q_sda[cnt];
            if (tick) begin
                if (cnt == 2'd0) begin sbusy <= 1'b0; sdone <= 1'b1; end
                else cnt <= cnt - 2'd1;
                if (cnt == 2'd2) sampled <= sda_line;
            end
        end
        if (rst || (we && addr == 4'd7 && wdata[0])) begin
            sbusy <= 1'b0; sampled <= 1'b0; scl_o <= 1'b1; sda_o <= 1'b1;
        end
    end

    // ---- registers and byte level ------------------------------------------------
    wire cmd = we && (addr == 4'd0 || (addr >= 4'd2 && addr <= 4'd6));
    always @(posedge clk) begin
        go <= 1'b0;
        if (sdone) temp_err <= 1'b0;
        // a read clears the error bits; one set in the same clock stays
        if (re) begin
            case (addr)
                4'd0, 4'd4, 4'd5: rdata <= dout;
                4'd1: begin
                    rdata <= {state != S_IDLE, 2'b00, temp_err, missed, err, 1'b0, started};
                    err <= 1'b0; missed <= 1'b0;
                end
                4'd6: rdata <= {6'd0, chan};
                default: rdata <= 8'h00;
            endcase
        end
        case (state)
            S_IDLE: if (we) case (addr)
                4'd0: begin
                    bitc <= 3'd7; dout <= wdata; do_ack <= 1'b0;
                    if ((!started || first) && wdata == 8'hC8 && !cdce_open) blk <= 1'b1;
                    first <= 1'b0;
                    if (started) state <= S_TX;
                    else begin
                        q_scl <= 4'b1110; q_sda <= 4'b1100; go <= 1'b1;
                        state <= S_START;
                    end
                end
                4'd2: begin state <= S_RSTART; blk <= 1'b0; first <= 1'b1; end
                4'd3: state <= S_STOP;
                4'd4: begin bitc <= 3'd7; do_ack <= 1'b0; state <= S_RX; end
                4'd5: begin bitc <= 3'd7; do_ack <= 1'b1; state <= S_RX; end
                4'd6: chan <= wdata[1:0];
                default: ;
            endcase
            S_START: if (sdone) begin started <= 1'b1; state <= S_TX; end
            S_TX:
                if (sdone) begin
                    if (bitc == 3'd0) state <= S_ACK;
                    else bitc <= bitc - 3'd1;
                end else if (!sbusy && !go) begin
                    q_scl <= 4'b0110; q_sda <= {4{dout[7]}};
                    dout <= {dout[6:0], 1'b1};
                    go <= 1'b1;
                end
            S_ACK:
                if (sdone) begin
                    state <= S_IDLE;
                    if (sampled && !blk) begin err <= 1'b1; temp_err <= 1'b1; end
                end else if (!sbusy && !go) begin
                    q_scl <= 4'b0110; q_sda <= 4'b1111; go <= 1'b1;
                end
            S_RX: begin
                if (!sbusy && !go) begin
                    q_scl <= 4'b0110; q_sda <= 4'b1111; go <= 1'b1;
                end
                if (sdone) begin
                    dout <= {dout[6:0], sampled};
                    if (bitc == 3'd0) begin
                        state <= S_RXACK;
                        q_scl <= 4'b0110; q_sda <= {4{~do_ack}}; go <= 1'b1;
                    end else bitc <= bitc - 3'd1;
                end
            end
            S_RXACK: if (sdone) state <= S_IDLE;
            S_STOP:
                if (sdone) begin started <= 1'b0; blk <= 1'b0; state <= S_IDLE; end
                else if (!sbusy && !go) begin
                    q_scl <= 4'b0111; q_sda <= 4'b0011; go <= 1'b1;
                end
            default: // S_RSTART
                if (sdone) state <= S_IDLE;
                else if (!sbusy && !go) begin
                    q_scl <= 4'b0110; q_sda <= 4'b1100; go <= 1'b1;
                end
        endcase

        if (cmd && state != S_IDLE) missed <= 1'b1;
        if (we && addr == 4'd8) begin scan_en <= wdata[0]; cdce_open <= wdata[1]; end

        // bus ownership
        if (cmd && !scan_en) want <= 1'b1;
        kb_pause <= want;
        if (!(kb_pause && want && park_s[1] && done_s[1] && !hold_s[1])) park_cnt <= 5'd0;
        else if (park_cnt != 5'd31) park_cnt <= park_cnt + 5'd1;
        if (!own && park_cnt == 5'd31) own <= 1'b1;
        if (scan_en && state == S_IDLE && !started) begin
            want <= 1'b0; own <= 1'b0; kb_pause <= 1'b0;
        end
        if (!hold_s[1]) held <= 1'b0;
        else if (state == S_IDLE && !started && !sbusy && !go) held <= 1'b1;

        if (rst || (we && addr == 4'd7 && wdata[0])) begin
            state <= S_IDLE; started <= 1'b0; do_ack <= 1'b0; missed <= 1'b0; err <= 1'b0;
            temp_err <= 1'b0; chan <= 2'd0; scan_en <= 1'b0; blk <= 1'b0; first <= 1'b0; cdce_open <= 1'b0;
            want <= 1'b0; own <= 1'b0; kb_pause <= 1'b0; park_cnt <= 5'd0; held <= 1'b0;
        end
    end
endmodule
