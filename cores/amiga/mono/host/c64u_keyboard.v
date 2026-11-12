// C64U keyboard: the matrix sits behind a 16-bit I2C port expander, PCA9555
// register map, 7-bit address 0x21 (Gideon's software/u64/u64ii_test.cc calls
// it 0x42: his driver takes 8-bit addresses, as PLL1 0xC8 = 0x64 shows; port 0
// drives the columns, port 1 reads the rows). This scans it without a
// CPU: configure port 0 as outputs and port 1 as inputs once, then for each
// column pull that column low (register 2), read the rows (register 1).
// 100 kHz, about 5.4 ms for all eight columns.
//
// Before the scan it sets up the NAU88C22 audio codec (7-bit 0x1A, Gideon's
// 0x34) the way Gideon's software/io/audio/nau8822.cc does: slave, I2S 24 bit,
// MCLK = 256 fs, DACs to the AUX outputs. Its registers are 9 bits wide:
// the second byte carries the register number and data bit 8.
//
// The scan runs in both directions in turn: port 0 driven and port 1 read,
// then port 1 driven and port 0 read. Gideon's code (U64-II, classic keyboard
// header) only does the first; the C64U keyboard has N-key rollover, i.e.
// diodes in its matrix, which pass current one way only, so we do not assume
// which side they face. A key seen in either direction counts.
//
// keys[col*8 + row] = 1 while the key at that column/row is down
// (port 0 = columns = CIA 1 port A, port 1 = rows = CIA 1 port B).
//
// pause (any clock) stops it before the next START with both lines released;
// parked says it stands there (the app's I2C master then has the buses).
module c64u_keyboard (
    input  wire        clk,             // 50 MHz
    input  wire        rst,
    input  wire        sda_in,
    output reg         scl_low,         // 1 = pull SCL low
    output reg         sda_low,         // 1 = pull SDA low
    output reg  [63:0] keys,
    output reg  [15:0] status,          // {nack count, last rows read}
    input  wire        pause,
    output reg         parked
);
    localparam [7:0] ADDR_W = 8'h42, ADDR_R = 8'h43;   // 0x21
    localparam [7:0] NAU_W  = 8'h34;                   // 0x1A
    localparam OP_S = 4'd0, OP_RS = 4'd1, OP_P = 4'd2, OP_W = 4'd3, OP_COL = 4'd4,
               OP_R = 4'd5, OP_END = 4'd6, OP_WAIT = 4'd7,
               OP_DW = 4'd8;            // write a byte that depends on the direction (arg = which)
    localparam NAU_OPS = 7'd56;         // codec: 11 writes of 5 ops and a wait after the reset
    localparam LOOP = NAU_OPS;

    reg [2:0] col;
    // codec writes: {register, 9-bit value}
    function [15:0] nau(input [3:0] i);
        case (i)
            0:  nau = {7'h00, 9'h000};      // reset (then wait)
            1:  nau = {7'h01, 9'h0CF};      // AUX1, AUX2, bias, IO buffers, 3k reference
            2:  nau = {7'h02, 9'h03F};      // ADCs, PGAs, input mixers
            3:  nau = {7'h03, 9'h18F};      // AUX outputs, main mixers, DACs
            4:  nau = {7'h04, 9'h050};      // I2S, 24 bit
            5:  nau = {7'h06, 9'h000};      // clocks from MCLK / 1 (256 fs), slave
            6:  nau = {7'h07, 9'h000};      // 48 kHz
            7:  nau = {7'h0A, 9'h008};      // DAC 128x oversampling
            8:  nau = {7'h0E, 9'h108};      // ADC 128x, high-pass
            9:  nau = {7'h2F, 9'h005};      // LAUXIN to left ADC
            default: nau = {7'h30, 9'h005}; // RAUXIN to right ADC
        endcase
    endfunction

    // {op, byte} for one pc. Only ever evaluated at elaboration (ROM below).
    function [11:0] prog_at(input [6:0] pc);
        reg [3:0] w;
        reg [2:0] k;
        reg [15:0] v;
        begin
            if (pc < NAU_OPS) begin
                // codec: write 0 (reset) is ops 0-4 and a wait (5); writes 1-10
                // take 5 ops each from op 6: start, address, two bytes, stop
                if (pc < 7'd6) begin w = 4'd0; k = pc[2:0]; end
                else begin w = (pc - 7'd6) / 5 + 4'd1; k = (pc - 7'd6) % 5; end
                v = nau(w);
                case (k)
                    0: prog_at = {OP_S, 8'h00};
                    1: prog_at = {OP_W, NAU_W};
                    2: prog_at = {OP_W, v[15:9], v[8]};
                    3: prog_at = {OP_W, v[7:0]};
                    4: prog_at = {OP_P, 8'h00};
                    default: prog_at = {OP_WAIT, 8'h00};
                endcase
            end else case (pc - NAU_OPS)
            // per line: set the port directions for this scan direction,
            // drive the line low, read the other port
            0:  prog_at = {OP_S,  8'h00};
            1:  prog_at = {OP_W,  ADDR_W};
            2:  prog_at = {OP_W,  8'h06};
            3:  prog_at = {OP_DW, 8'd0};       // config port 0
            4:  prog_at = {OP_DW, 8'd1};       // config port 1
            5:  prog_at = {OP_P,  8'h00};
            6:  prog_at = {OP_S,  8'h00};
            7:  prog_at = {OP_W,  ADDR_W};
            8:  prog_at = {OP_DW, 8'd2};       // output register of the driven port
            9:  prog_at = {OP_COL, 8'h00};
            10: prog_at = {OP_P,  8'h00};
            11: prog_at = {OP_S,  8'h00};
            12: prog_at = {OP_W,  ADDR_W};
            13: prog_at = {OP_DW, 8'd3};       // input register of the read port
            14: prog_at = {OP_RS, 8'h00};
            15: prog_at = {OP_W,  ADDR_R};
            16: prog_at = {OP_R,  8'h00};
            17: prog_at = {OP_P,  8'h00};
            default: prog_at = {OP_END, 8'h00};
            endcase
        end
    endfunction

    // The whole sequence folded into a constant: the divide/modulo above runs in
    // the elaborator, the hardware sees a 128 x 12 table, a pure 7-input lookup.
    function [128*12-1:0] build_rom(input dummy);
        integer i;
        begin
            for (i = 0; i < 128; i = i + 1) build_rom[i*12 +: 12] = prog_at(i[6:0]);
        end
    endfunction
    localparam [128*12-1:0] ROM = build_rom(1'b0);

    // quarter-bit tick: 50 MHz / 125 = 400 kHz -> 100 kHz SCL
    reg  [6:0] div;
    wire tick = (div == 7'd124);
    reg  [6:0] pc;
    reg  [3:0] bitn;                    // 0-7 data, 8 = acknowledge slot
    reg  [1:0] ph;
    reg  [7:0] rx;
    reg [12:0] settle;
    reg [10:0] wait_cnt;                // OP_WAIT: 1600 ticks = 4 ms
    // registered: pc only moves on a tick, and the next tick is 125 clocks away
    reg  [11:0] ins = 12'd0;
    always @(posedge clk) ins <= ROM[pc*12 +: 12];
    wire [3:0] op  = ins[11:8];
    wire [7:0] arg = ins[7:0];
    reg        dir;                     // 0: drive port 0, read port 1; 1: the other way
    reg [63:0] keys_a, keys_b;
    wire [7:0] dbyte = (arg == 8'd0) ? (dir ? 8'hFF : 8'h00) :      // config port 0
                       (arg == 8'd1) ? (dir ? 8'h00 : 8'hFF) :      // config port 1
                       (arg == 8'd2) ? (dir ? 8'h03 : 8'h02) :      // output port register
                                       (dir ? 8'h00 : 8'h01);       // input port register
    wire [7:0] tx  = (op == OP_COL) ? ~(8'h01 << col) : (op == OP_DW) ? dbyte : arg;

    (* ASYNC_REG = "TRUE" *) reg [1:0] sda_s;
    always @(posedge clk) sda_s <= {sda_s[0], sda_in};
    (* ASYNC_REG = "TRUE" *) reg [1:0] pause_s = 2'b00;
    always @(posedge clk) pause_s <= {pause_s[0], pause};
    wire park = pause_s[1] && op == OP_S && ph == 2'd0;

    // OP_END (the only op left that the sequence holds besides those handled below)
    // fires once per scan line; one clock-enable per byte of keys_a / per column of keys_b.
    wire fire = tick && settle[12] && !park && op == OP_END;
    genvar g, h;
    generate for (g = 0; g < 8; g = g + 1) begin : kcol
        wire hit = fire && col == g;
        always @(posedge clk)                       // drove column g: the rows read are byte g
            if (rst) keys_a[g*8 +: 8] <= 8'd0;
            else if (hit && !dir) keys_a[g*8 +: 8] <= ~rx;
        for (h = 0; h < 8; h = h + 1) begin : krow
            always @(posedge clk)                   // drove row g: the columns read are bit g of each byte
                if (rst) keys_b[h*8 + g] <= 1'b0;
                else if (hit && dir) keys_b[h*8 + g] <= ~rx[h];
        end
    end endgenerate
    always @(posedge clk) keys <= keys_a | keys_b;
    always @(posedge clk) begin
        if (rst) begin
            div <= 0; pc <= 0; bitn <= 0; ph <= 0; col <= 0; settle <= 0; wait_cnt <= 0;
            dir <= 0;
            scl_low <= 0; sda_low <= 0; status <= 16'd0; parked <= 0;
        end else begin
            div <= tick ? 7'd0 : div + 7'd1;
            parked <= park;
            if (tick && !settle[12]) settle <= settle + 13'd1;   // 20 ms idle bus first
            else if (tick && !park) begin
                ph <= ph + 2'd1;
                case (op)
                    OP_S, OP_RS: case (ph)                  // (repeated) start
                        0: begin sda_low <= 0; scl_low <= (op == OP_RS); end
                        1: scl_low <= 0;
                        2: sda_low <= 1;                    // SDA falls, SCL high
                        3: begin scl_low <= 1; pc <= pc + 7'd1; end
                    endcase
                    OP_P: case (ph)                         // stop
                        0: sda_low <= 1;
                        1: scl_low <= 0;
                        2: sda_low <= 0;                    // SDA rises, SCL high
                        3: pc <= pc + 7'd1;
                    endcase
                    OP_W, OP_COL, OP_R, OP_DW: case (ph)
                        0: sda_low <= (bitn == 4'd8) ? 1'b0             // ACK: release
                                     : (op == OP_R) ? 1'b0              // read: release
                                     : ~tx[3'd7 - bitn[2:0]];
                        1: scl_low <= 0;
                        2: if (op == OP_R && bitn != 4'd8) rx <= {rx[6:0], sda_s[1]};
                           else if (op != OP_R && bitn == 4'd8 && sda_s[1] && status[15:8] != 8'hFF)
                               status[15:8] <= status[15:8] + 8'd1;   // no acknowledge
                        3: begin
                            scl_low <= 1;
                            if (bitn == 4'd8) begin
                                bitn <= 0;
                                pc   <= pc + 5'd1;
                            end else
                                bitn <= bitn + 4'd1;
                        end
                    endcase
                    OP_WAIT: begin
                        ph <= 0;
                        wait_cnt <= wait_cnt + 11'd1;
                        if (wait_cnt == 11'd1600) begin wait_cnt <= 0; pc <= pc + 7'd1; end
                    end
                    default: begin                          // OP_END: store rows, next column
                        ph <= 0;
                        status[7:0] <= rx;
                        col <= col + 3'd1;
                        if (col == 3'd7) dir <= ~dir;
                        pc  <= LOOP;
                    end
                endcase
            end
        end
    end
endmodule
