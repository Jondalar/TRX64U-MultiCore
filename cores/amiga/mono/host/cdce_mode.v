// T164 WP7 (the host, all cores; from the A1200 large core, T107 D7): host/cdce_i2c.v with the PLL registers per HDMI mode instead of the two System Mode blobs. The same
// sequence (block write of 8 bytes from register 0x14, sent twice); bytes 0x18..0x1B come from hdmi_modes.vh (tools/hdmi_modes.py host-gen: N, Q, R, P
// and the VCO range bits for the TMDS clock of the mode, out of our 37.89 MHz reference). Mode 2 (1080p50) is byte for byte the old
// 55/14 blob. `lock` = 1 (a C64-type core, whose frame must equal the VIC frame) takes hm_cdce_lock, `lock` = 0 (a frame-buffer core) hm_cdce.
// `mode` and `lock` have to be steady from rst on; rst starts the sequence again (a mode change).
module cdce_mode (
    input  wire clk,            // 50 MHz
    input  wire rst,
    output reg  scl_low,        // 1 = pull SCL low
    output reg  sda_low,        // 1 = pull SDA low
    output reg  done,
    input  wire [3:0] mode,
    input  wire       lock
);
`include "hdmi_modes_amiga.vh"   // T234: a name of its own - Vivado runs in host/, where host/hdmi_modes.vh (no rows 12..14) would be found first
    wire [31:0] pll = lock ? hm_cdce_lock(mode) : hm_cdce(mode);
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
            7: byte_at = pll[31:24];   // 0x18: N[11:4]
            8: byte_at = pll[23:16];   // 0x19: N[3:0], R[8:5]
            9: byte_at = pll[15:8];    // 0x1A: R[4:0], Q[5:3]
            default: byte_at = pll[7:0]; // 0x1B: Q[2:0], P, VCO range
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
