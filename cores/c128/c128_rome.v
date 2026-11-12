// T113 §5.4: the DDR2 side of the ROM line cache (c128_romc.v). One 128 bit read at a time on the host's engine client (e_*, ui_clk; the
// read data comes on c_rd_data with e_rd_valid, as mig_arb says); the command and the read data are the protocol of cores/nes/trx/nes_mem.v's
// c_* client (toggle request in, toggle done out, one command open).
//
// `owns` is high from the request until the command is taken: layer2_c128.v puts this block's command on e_* instead of the VDC frame writer's
// (which waits, because its e_rdy is held low) for that time. The image is at byte address ROM_BASE (the application fills it through the host's DDR2
// access, like the cartridge and the REU images), line n at ROM_BASE + 16 n; e_addr counts 16 bit words.
`timescale 1ns / 1ps
module c128_rome #(
    parameter [26:0] ROM_BASE_W = 27'h0E00000        // byte 0x01C00000 / 2
) (
    input  wire         ui_clk,
    input  wire         ui_rst,
    input  wire         req_tgl,                    // from c128_romc (its clock)
    input  wire [12:0]  req_line,
    output reg          done_tgl = 1'b0,
    output reg  [127:0] fill = 128'd0,

    output reg          owns = 1'b0,
    output reg          en = 1'b0,
    output reg  [26:0]  addr = 27'd0,
    input  wire         rdy,
    input  wire         rd_valid,
    input  wire [127:0] rd_data
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] req_s = 3'b000;
    wire req_ev = req_s[2] != req_s[1];
    localparam S_IDLE = 2'd0, S_ISSUE = 2'd1, S_WAIT = 2'd2;
    reg [1:0] st = S_IDLE;
    always @(posedge ui_clk) begin
        req_s <= {req_s[1:0], req_tgl};
        if (ui_rst) begin
            st <= S_IDLE; en <= 1'b0; owns <= 1'b0;
            req_s <= {3{req_tgl}};
        end else case (st)
            S_IDLE: if (req_ev) begin
                addr  <= ROM_BASE_W + {11'd0, req_line, 3'b000};
                en    <= 1'b1;
                owns  <= 1'b1;
                st    <= S_ISSUE;
            end
            S_ISSUE: if (rdy) begin                  // en && rdy: the command is taken in this clock
                en   <= 1'b0;
                owns <= 1'b0;
                st   <= S_WAIT;
            end
            S_WAIT: if (rd_valid) begin
                fill     <= rd_data;
                done_tgl <= ~done_tgl;
                st       <= S_IDLE;
            end
            default: st <= S_IDLE;
        endcase
    end
endmodule
