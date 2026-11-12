// Programs a CDCE(L)913/CDCEL824-type clock synthesizer at I2C address 0x64
// the way the U64-II bootloader does (block write of 8 bytes from register
// 0x14), with PLL1 at 55/14 of its input: 148.87 MHz (PAL; NTSC-50 149.14 MHz)
// out of the 37.89 MHz we feed it (125/104 of dot4x), so that the 2640 x 1125
// HDMI frame (CEA 1080p50 blanking) is exactly one VIC frame of 63 x 312
// cycles (pixel clock = 6875/1456 of dot4x). N 55, M 14, P 1 (register 3),
// TI: N' = N x 2^(4-P) = 440 = 14 x 31 + 6, so Q 31, R 6 (P 2 would give
// Q 15 < 16), VCO 148.9-149.1 MHz in the range 125-150 MHz.
// Pdiv2/Pdiv3 = 1 as in the bootloader.
// Drives both buses the bootloader uses; ACKs are not checked. done goes high
// when the sequence has been sent twice.
// v60 (T40, the 60 Hz modes NTSC and PAL-60; static while rst is low): N/M =
// 990/263 instead, so that the 2200 x 1125 HDMI frame is exactly one VIC frame
// of 65 x 263 cycles (pixel clock = 61875/13676 of dot4x, 148.06 MHz at NTSC):
// N 990, P 3, Q 30, R 30 (TI: N' = N x 2^P = 7920 = 263 x 30 + 30), VCO range
// 125-150 MHz.
module cdce_i2c (
    input  wire clk,            // 50 MHz
    input  wire rst,
    output reg  scl_low,        // 1 = pull SCL low
    output reg  sda_low,        // 1 = pull SDA low
    output reg  done,
    input  wire v60
);
    localparam NBYTES = 11;
    function [7:0] byte_at; input [3:0] i;
        case (i)
            0: byte_at = 8'hC8;     // address 0x64, write
            1: byte_at = 8'h14;     // block write from register 0x14
            2: byte_at = 8'h08;     // eight bytes
            3: byte_at = 8'h57;     // 0x14: as the bootloader
            4: byte_at = 8'h00;     // 0x15
            5: byte_at = 8'h01;     // 0x16: Pdiv2 = 1
            6: byte_at = 8'h01;     // 0x17: Pdiv3 = 1
            7: byte_at = v60 ? 8'h3D : 8'h03;   // 0x18: N[11:4]
            8: byte_at = v60 ? 8'hE0 : 8'h70;   // 0x19: N[3:0], R[8:5]
            9: byte_at = v60 ? 8'hF3 : 8'h33;   // 0x1A: R[4:0], Q[5:3]
            default: byte_at = v60 ? 8'hCD : 8'hED; // 0x1B: Q[2:0], P, VCO range
        endcase
    endfunction

    // quarter-bit tick: 50 MHz / 125 = 400 kHz -> 100 kHz SCL
    reg [6:0]  div;
    wire tick = (div == 124);
    reg [3:0]  bidx;
    reg [3:0]  bitn;         // 8 = ACK slot
    reg [1:0]  ph;
    reg [2:0]  st;
    reg [19:0] wait_cnt;
    reg        pass;
    wire [7:0] cur = byte_at(bidx);
    localparam S_IDLE = 0, S_START = 1, S_BITS = 2, S_STOP = 3, S_GAP = 4, S_DONE = 5;

    always @(posedge clk) begin
        if (rst) begin
            div <= 0; st <= S_IDLE; scl_low <= 0; sda_low <= 0; done <= 0;
            bidx <= 0; bitn <= 0; ph <= 0; wait_cnt <= 0; pass <= 0;
        end else begin
            div <= tick ? 0 : div + 1;
            if (tick) case (st)
                S_IDLE: begin                         // let the synthesizer and buses settle
                    wait_cnt <= wait_cnt + 1;
                    if (wait_cnt == 20'd4000) begin st <= S_START; ph <= 0; end
                end
                S_START: begin
                    case (ph)
                        0: begin scl_low <= 0; sda_low <= 0; end
                        1: sda_low <= 1;               // SDA falls while SCL high
                        2: scl_low <= 1;
                        3: begin bidx <= 0; bitn <= 0; st <= S_BITS; end
                    endcase
                    ph <= ph + 1;
                end
                S_BITS: begin
                    case (ph)
                        0: sda_low <= (bitn == 8) ? 1'b0 : ~cur[7 - bitn];
                        1: scl_low <= 0;
                        2: ;
                        3: begin
                            scl_low <= 1;
                            if (bitn == 8) begin
                                bitn <= 0;
                                if (bidx == NBYTES - 1) begin st <= S_STOP; end
                                else bidx <= bidx + 1;
                            end else
                                bitn <= bitn + 1;
                        end
                    endcase
                    ph <= ph + 1;
                end
                S_STOP: begin
                    case (ph)
                        0: sda_low <= 1;
                        1: scl_low <= 0;
                        2: sda_low <= 0;               // SDA rises while SCL high
                        3: begin st <= S_GAP; wait_cnt <= 0; end
                    endcase
                    ph <= ph + 1;
                end
                S_GAP: begin
                    wait_cnt <= wait_cnt + 1;
                    if (wait_cnt == 20'd400) begin
                        if (pass) st <= S_DONE; else begin pass <= 1; st <= S_START; ph <= 0; end
                    end
                end
                S_DONE: done <= 1;
            endcase
        end
    end
endmodule
