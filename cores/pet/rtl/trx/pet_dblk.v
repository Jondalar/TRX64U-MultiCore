// T134: the block service of the IEEE drives. The MiSTer drive (ieee_drive, GPL-2) asks for 256 byte blocks the way hps_io serves them
// (sd_rd / sd_wr with sd_lba and sd_blk_cnt; the answer is sd_ack held high while the bytes pass on sd_buff_addr / sd_buff_dout / sd_buff_wr;
// a write reads sd_buff_din at sd_buff_addr). Here the image sits in DDR2, put there by the app; this module moves the bytes between the drive
// (sys_clk) and the host's drive DDR2 port (cpu_clk, the toggle handshake cores/common/drive_mem.vhd drives: drv_addr = byte address [25:1],
// a read returns the aligned 16 byte line, a write is a 32 bit word with byte enables).
//   unit u reads the byte (base[u] + lba * 256 + n); a DDR2 line is 16 bytes, so a sector is 16 line transactions.
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module pet_dblk #(
    parameter NU = 2
) (
    input  wire             sys_clk,
    input  wire             cpu_clk,
    // the drive's side (sys_clk)
    input  wire [NU*32-1:0] sd_lba,
    input  wire [NU*6-1:0]  sd_blk_cnt,
    input  wire [NU-1:0]    sd_rd,
    input  wire [NU-1:0]    sd_wr,
    output reg  [NU-1:0]    sd_ack = {NU{1'b0}},
    output reg  [12:0]      sd_buff_addr = 13'd0,
    output reg  [7:0]       sd_buff_dout = 8'd0,
    output reg              sd_buff_wr = 1'b0,
    input  wire [NU*8-1:0]  sd_buff_din,
    input  wire [NU*32-1:0] base,                   // DDR2 byte address of each unit's image (quasi static)
    output reg  [NU-1:0]    dirty = {NU{1'b0}},     // a write reached the image; cleared by clr_dirty
    input  wire [NU-1:0]    clr_dirty,
    output reg              ev_tgl = 1'b0,          // sys_clk: a block of the image was written; ev_* are stable before it toggles (the dirty log of the app's window media)
    output reg  [7:0]       ev_chunk = 8'd0,        // which 8 KiB of the image
    output reg              ev_unit = 1'b0,
    // the host's drive DDR2 port (cpu_clk)
    output reg              drv_tgl = 1'b0,
    output reg  [25:0]      drv_addr = 26'd0,
    output reg              drv_we = 1'b0,
    output reg  [31:0]      drv_wdata = 32'd0,
    output reg  [3:0]       drv_be = 4'd0,
    input  wire             drv_done_tgl,
    input  wire [127:0]     drv_rdata
);
    // ------------------------------------------------------------ cpu_clk side: one line transaction per request toggle
    reg         rq_tgl = 1'b0;          // sys_clk: a request is there (rq_* are stable before it toggles)
    reg         rq_we = 1'b0;
    reg [25:0]  rq_addr = 26'd0;        // byte address of the line, 16 byte aligned
    reg [127:0] rq_data = 128'd0;
    reg         rs_tgl = 1'b0;          // cpu_clk: the answer is there
    reg [127:0] rs_data = 128'd0;

    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [2:0] dn_s = 3'b000;
    reg [2:0]   cs = 3'd0;
    reg [1:0]   wcnt = 2'd0;
    localparam C_IDLE = 3'd0, C_RDW = 3'd1, C_WR = 3'd2, C_WRW = 3'd3;
    always @(posedge cpu_clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        dn_s <= {dn_s[1:0], drv_done_tgl};
        case (cs)
            C_IDLE: if (rq_s[2] != rq_s[1]) begin
                if (rq_we) begin
                    wcnt <= 2'd0;
                    cs <= C_WR;
                end else begin
                    drv_addr <= {1'b0, rq_addr[25:1]};
                    drv_we <= 1'b0;
                    drv_be <= 4'b1111;
                    drv_tgl <= ~drv_tgl;
                    cs <= C_RDW;
                end
            end
            C_RDW: if (dn_s[2] != dn_s[1]) begin
                rs_data <= drv_rdata;
                rs_tgl <= ~rs_tgl;
                cs <= C_IDLE;
            end
            C_WR: begin
                drv_addr <= {1'b0, rq_addr[25:4], wcnt, 1'b0};
                drv_we <= 1'b1;
                drv_be <= 4'b1111;
                case (wcnt)
                    2'd0: drv_wdata <= rq_data[31:0];
                    2'd1: drv_wdata <= rq_data[63:32];
                    2'd2: drv_wdata <= rq_data[95:64];
                    default: drv_wdata <= rq_data[127:96];
                endcase
                drv_tgl <= ~drv_tgl;
                cs <= C_WRW;
            end
            C_WRW: if (dn_s[2] != dn_s[1]) begin
                wcnt <= wcnt + 2'd1;
                if (wcnt == 2'd3) begin
                    rs_tgl <= ~rs_tgl;
                    cs <= C_IDLE;
                end else cs <= C_WR;
            end
            default: cs <= C_IDLE;
        endcase
    end

    // ------------------------------------------------------------ sys_clk side: the block device
    (* ASYNC_REG = "TRUE" *) reg [2:0] rs_s = 3'b000;
    localparam S_IDLE = 3'd0, S_RQ = 3'd1, S_WAIT = 3'd2, S_PUSH = 3'd3, S_GET = 3'd4, S_SEND = 3'd5, S_END = 3'd6;
    reg [2:0]   ss = S_IDLE;
    integer     k;
    integer     u = 0;
    reg         is_wr = 1'b0;
    reg [31:0]  img_base = 32'd0;
    reg [23:0]  lba_l = 24'd0;
    reg [10:0]  line = 11'd0, lines = 11'd0;        // line index in the transfer, number of lines (16 per block)
    reg [4:0]   bi = 5'd0;                          // byte in the line
    reg [127:0] sh = 128'd0;
    wire [7:0]  din_u = sd_buff_din[u*8 +: 8];
    wire        last = (line + 11'd1 == lines);
    always @(posedge sys_clk) begin
        rs_s <= {rs_s[1:0], rs_tgl};
        sd_buff_wr <= 1'b0;
        dirty <= dirty & ~clr_dirty;
        case (ss)
            S_IDLE: begin
                for (k = NU - 1; k >= 0; k = k - 1)
                    if (sd_rd[k] || sd_wr[k]) begin
                        u <= k;
                        is_wr <= sd_wr[k];
                        img_base <= base[k*32 +: 32] + {sd_lba[k*32 +: 24], 8'd0};
                        lba_l <= sd_lba[k*32 +: 24];
                        lines <= ({5'd0, sd_blk_cnt[k*6 +: 6]} + 11'd1) << 4;
                        line <= 11'd0;
                        sd_ack[k] <= 1'b1;
                        ss <= S_RQ;
                    end
            end
            S_RQ: begin
                bi <= 5'd0;
                if (is_wr) ss <= S_GET;
                else begin
                    rq_addr <= img_base[25:0] + {line, 4'd0};
                    rq_we <= 1'b0;
                    rq_tgl <= ~rq_tgl;
                    ss <= S_WAIT;
                end
            end
            S_WAIT: if (rs_s[2] != rs_s[1]) begin
                if (is_wr) begin
                    line <= line + 11'd1;
                    ss <= last ? S_END : S_RQ;
                end else begin
                    sh <= rs_data;
                    ss <= S_PUSH;
                end
            end
            S_PUSH: begin
                sd_buff_addr <= {line[8:0], bi[3:0]};
                sd_buff_dout <= sh[7:0];
                sd_buff_wr <= 1'b1;
                sh <= {8'd0, sh[127:8]};
                bi <= bi + 5'd1;
                if (bi == 5'd15) begin
                    line <= line + 11'd1;
                    ss <= last ? S_END : S_RQ;
                end
            end
            // write: the buffer answers two clocks after the address (registered read), so byte n is taken at bi = n + 2
            S_GET: begin
                if (bi < 5'd16) sd_buff_addr <= {line[8:0], bi[3:0]};
                if (bi >= 5'd2) sh <= {din_u, sh[127:8]};
                bi <= bi + 5'd1;
                if (bi == 5'd17) ss <= S_SEND;
            end
            S_SEND: begin
                rq_data <= sh;
                rq_addr <= img_base[25:0] + {line, 4'd0};
                rq_we <= 1'b1;
                rq_tgl <= ~rq_tgl;
                dirty[u] <= 1'b1;
                if (line[3:0] == 4'd0) begin
                    ev_chunk <= ({lba_l, 8'd0} + {line, 4'd0}) >> 13;
                    ev_unit <= (u != 0);
                    ev_tgl <= ~ev_tgl;
                end
                ss <= S_WAIT;
            end
            S_END: begin
                sd_ack[u] <= 1'b0;
                ss <= S_IDLE;
            end
            default: ss <= S_IDLE;
        endcase
    end
endmodule
