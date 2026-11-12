// T85: the sector service of the Amiga core: what the MCU and the SD card were for NanoMig. Floppy images (ADF) lie
// in the DDR2 window, the core reads and writes their sectors itself; hard disk sectors (HDF) are fetched by the
// application from the card into a staging sector in the window (a mailbox, with an interrupt). Written for
// this project.
//
// Towards the Minimig the interface is the one the MCU's sd_card.v had (nanomig.v sdc_*): the requests sdc_rd /
// sdc_wr (drives 0-3 floppy, 4-5 IDE) with sdc_sector are held until sdc_busy, the sector comes as 512 strobed
// bytes (sdc_byte_in_*) or is fetched byte by byte by sdc_byte_addr / sdc_byte_out_data, sdc_done ends it.
// Everything on this side moves on ce28 like the chipset.
//
// DDR2 window (byte offsets, WIN_W = word address of the window start):
//   ADF  drive d: 0x900000 + d * 0xE0000   (901,120 byte image, 512 bytes per sector, sector = track * 11 + n)
//   HDF staging : 0xC80000 (one sector, written/read by the application for the mailbox request)
`timescale 1ns / 1ps
module amiga_sdc #(
    parameter [26:0] WIN_W = 27'h0800000,
    parameter [26:0] ADF_W = 27'h0480000,           // 0x900000 / 2
    parameter [26:0] ADF_STRIDE_W = 27'h070000,     // 0xE0000 / 2
    parameter [26:0] STAGE_W = 27'h0640000,         // 0xC80000 / 2
    // T102 D1: hdf_size0/1 are 512-byte sectors (1) instead of bytes (0). Sectors reach 2 TiB through the same 32 bits, and
    // total_sectors in nanomig.v takes sdc_img_size[40:9] already; the request LBA (mb_req_lba) was always in sectors.
    parameter integer HDF_SECT = 0
) (
    input  wire         sys_clk,
    input  wire         sys_rst,
    input  wire         ce28,
    input  wire         flush,                      // the machine is in reset: drop the transfer in progress and what it left on the DDR2 side
    // towards nanomig
    output reg  [7:0]   sdc_img_mounted = 8'd0,
    output reg  [63:0]  sdc_img_size = 64'd0,
    input  wire [7:0]   sdc_rd,
    input  wire [7:0]   sdc_wr,
    input  wire [31:0]  sdc_sector,
    output reg          sdc_busy = 1'b0,
    output reg          sdc_done = 1'b0,
    output reg          sdc_byte_in_strobe = 1'b0,
    output reg  [8:0]   sdc_byte_addr = 9'd0,
    output reg  [7:0]   sdc_byte_in_data = 8'd0,
    input  wire [7:0]   sdc_byte_out_data,
    // the application's mount registers (quasi static, via amiga_regs)
    input  wire [3:0]   adf_mount_tgl,              // toggles: re-announce drive d (present = adf_present[d])
    input  wire [3:0]   adf_present,
    input  wire [1:0]   hdf_mount_tgl,
    input  wire [1:0]   hdf_present,
    input  wire [31:0]  hdf_size0,                  // image bytes
    input  wire [31:0]  hdf_size1,
    // dirty log of floppy sector writes: {drive[1:0], track[7:0]}
    output reg          dirty_wr = 1'b0,
    output reg  [9:0]   dirty_val = 10'd0,
    // HDF mailbox
    output reg          mb_req_tgl = 1'b0,          // a request is posted
    output reg          mb_req_we = 1'b0,
    output reg          mb_req_drv = 1'b0,
    output reg  [31:0]  mb_req_lba = 32'd0,
    input  wire         mb_done_tgl,                // the application finished it (staging holds / has taken the sector)
    output wire         busy_o,
    output wire         quiet,                      // sys_clk: no command in the queue or on the DDR2 port (Freeze)
    // ui_clk: client B of amiga_eport
    input  wire         ui_clk,
    input  wire         ui_rst,
    output wire         b_valid,
    output wire         b_we,
    output wire [26:0]  b_addr,
    output wire [127:0] b_data,
    input  wire         b_ready,
    input  wire [127:0] b_rdata
);
    // ---- sys -> ui: read/write bursts; ui -> sys: read data
    reg          cq_wr = 1'b0;
    reg  [155:0] cq_wdata = 156'd0;                 // {we, addr[26:0], data[127:0]}
    wire         cq_full, cq_empty;
    wire [155:0] cq_rdata;
    reg          cq_rd = 1'b0;
    trx_afifo_rr #(.W(156), .AW(2)) cq (.wclk(sys_clk), .wrst(sys_rst), .wr(cq_wr), .wdata(cq_wdata), .full(cq_full),
        .rclk(ui_clk), .rrst(ui_rst), .rd(cq_rd), .rdata(cq_rdata), .empty(cq_empty));
    reg          rq_rd = 1'b0;
    wire         rq_full, rq_empty;
    wire [127:0] rq_rdata;
    reg          rq_wr = 1'b0;
    reg  [127:0] rq_wdata = 128'd0;
    trx_afifo_rr #(.W(128), .AW(2)) rq (.wclk(ui_clk), .wrst(ui_rst), .wr(rq_wr), .wdata(rq_wdata), .full(rq_full),
        .rclk(sys_clk), .rrst(sys_rst), .rd(rq_rd), .rdata(rq_rdata), .empty(rq_empty));

    // ui side: pop a command, run it on the e_ port, push read data
    reg  [1:0]  us = 2'd0;
    reg         u_we = 1'b0;
    reg  [26:0] u_addr = 27'd0;
    reg  [127:0] u_data = 128'd0;
    assign b_valid = (us == 2'd1);
    assign b_we = u_we;
    assign b_addr = u_addr;
    assign b_data = u_data;
    reg         ui_idle = 1'b1;
    always @(posedge ui_clk) begin
        cq_rd <= 1'b0; rq_wr <= 1'b0;
        if (ui_rst) us <= 2'd0;
        else case (us)
        2'd0: if (!cq_empty && !cq_rd) begin
            {u_we, u_addr, u_data} <= cq_rdata; cq_rd <= 1'b1; us <= 2'd1; ui_idle <= 1'b0;
        end else ui_idle <= cq_empty;
        2'd1: if (b_ready) begin
            if (!u_we) begin rq_wdata <= b_rdata; rq_wr <= 1'b1; end
            us <= 2'd0;
        end
        default: us <= 2'd0;
        endcase
    end
    (* ASYNC_REG = "TRUE" *) reg [1:0] idle_s = 2'b11;
    always @(posedge sys_clk) idle_s <= {idle_s[0], ui_idle};
    assign quiet = idle_s[1] & ~cq_wr;

    // ---- sys side
    (* ASYNC_REG = "TRUE" *) reg [2:0] md_s = 3'b000;      // mb_done_tgl
    always @(posedge sys_clk) md_s <= {md_s[1:0], mb_done_tgl};
    wire md_ev = md_s[2] ^ md_s[1];

    reg [3:0] adf_tgl_seen = 4'd0;
    reg [1:0] hdf_tgl_seen = 2'd0;
    reg [5:0] mnt_pend = 6'd0;                          // MOUNT writes seen, not announced yet: ADF 3..0, HDF 5..4
    reg md_seen = 1'b0;
    reg md_clear = 1'b0;
    always @(posedge sys_clk) if (md_ev) md_seen <= 1'b1; else if (md_clear) md_seen <= 1'b0;

    localparam S_IDLE = 4'd0, S_ACC = 4'd1, S_RD_ISSUE = 4'd2, S_RD_WORD = 4'd3, S_WR_COLLECT = 4'd5,
               S_WR_BYTE = 4'd6, S_WR_PUSH = 4'd7, S_MBOX_WAIT = 4'd8, S_WR_DRAIN = 4'd9, S_FIN = 4'd10;
    reg [3:0]   s = S_IDLE;
    reg         drn = 1'b0;                         // draining after a reset
    reg [5:0]   drc = 6'd0;
    reg [2:0]   drv = 3'd0;
    reg         is_wr = 1'b0;
    reg [31:0]  lba = 32'd0;
    reg [5:0]   issued = 6'd0, got = 6'd0;
    reg [127:0] word = 128'd0;
    reg [4:0]   bytei = 5'd0;
    reg [8:0]   bcnt = 9'd0;
    reg [3:0]   wait_c = 4'd0;
    reg [127:0] wacc = 128'd0;
    reg [5:0]   wburst = 6'd0;
    assign busy_o = (s != S_IDLE);

    function [2:0] first(input [5:0] v);
        first = v[0] ? 3'd0 : v[1] ? 3'd1 : v[2] ? 3'd2 : v[3] ? 3'd3 : v[4] ? 3'd4 : 3'd5;
    endfunction
    wire [26:0] sec_base_w = WIN_W + (drv[2] ? STAGE_W :
                             (ADF_W + ({24'd0, drv} * ADF_STRIDE_W) + {lba[18:0], 8'd0}));
    wire        any_rd = |sdc_rd[5:0];
    wire        any_wr = |sdc_wr[5:0];

    always @(posedge sys_clk) begin
        cq_wr <= 1'b0; rq_rd <= 1'b0; dirty_wr <= 1'b0; md_clear <= 1'b0;
        if (ce28) begin
            sdc_img_mounted <= 8'd0;
            sdc_done <= 1'b0;
            sdc_byte_in_strobe <= 1'b0;
            // T85 disk change: one pending announcement per ce28 (the size register is shared), lowest drive first; what the application wrote
            // while ce28 stood (Freeze: MOUNT 0 then MOUNT 1) is announced once, with the last state, as soon as the machine runs
            begin : mnt
                integer i;
                reg found;
                found = 1'b0;
                for (i = 0; i < 6; i = i + 1)
                    if (mnt_pend[i] && !found) begin
                        found = 1'b1;
                        mnt_pend[i] <= 1'b0;
                        sdc_img_mounted[i] <= 1'b1;
                        if (i < 4) sdc_img_size <= adf_present[i] ? 64'd901120 : 64'd0;
                        else sdc_img_size <= !hdf_present[i - 4] ? 64'd0 : HDF_SECT != 0 ? {23'd0, (i == 4) ? hdf_size0 : hdf_size1, 9'd0}
                                                                         : {32'd0, (i == 4) ? hdf_size0 : hdf_size1};
                    end
            end
            case (s)
            S_IDLE: if ((any_rd || any_wr) && !drn) begin
                is_wr <= ~any_rd;
                drv <= any_rd ? first(sdc_rd[5:0]) : first(sdc_wr[5:0]);
                lba <= sdc_sector;
                sdc_busy <= 1'b1; wait_c <= 4'd0; s <= S_ACC;
            end
            S_ACC: begin
                // the Minimig sees busy, drops its request, and is ready for strobes two ticks later
                wait_c <= wait_c + 4'd1;
                if (wait_c == 4'd3) begin
                    issued <= 6'd0; got <= 6'd0; bcnt <= 9'd0; wburst <= 6'd0; bytei <= 5'd0; wait_c <= 4'd0;
                    if (is_wr) s <= S_WR_COLLECT;
                    else if (drv[2]) begin
                        // hard disk read: the application puts the sector into the staging area first
                        mb_req_we <= 1'b0; mb_req_drv <= drv[0]; mb_req_lba <= lba; mb_req_tgl <= ~mb_req_tgl;
                        md_clear <= 1'b1; s <= S_MBOX_WAIT;
                    end else s <= S_RD_ISSUE;
                end
            end
            S_MBOX_WAIT: if (md_seen) begin
                md_clear <= 1'b1;
                if (is_wr) s <= S_FIN; else s <= S_RD_ISSUE;
            end
            S_RD_ISSUE, S_RD_WORD: begin
                // up to 32 bursts of 16 bytes; read commands go out as the queue takes them
                // at most 4 words issued but not streamed yet: the answer FIFO holds 4 and would drop the fifth
                if (issued != 6'd32 && (issued - got) < 6'd4 && !cq_full && !cq_wr) begin
                    cq_wr <= 1'b1; cq_wdata <= {1'b0, sec_base_w + {18'd0, issued, 3'd0}, 128'd0};
                    issued <= issued + 6'd1;
                end
                if (s == S_RD_ISSUE) begin
                    if (!rq_empty && !rq_rd) begin word <= rq_rdata; rq_rd <= 1'b1; bytei <= 5'd0; s <= S_RD_WORD; end
                end else begin
                    // stream the 16 bytes of the burst, one per tick; byte 0 is the lowest address
                    sdc_byte_in_strobe <= 1'b1; sdc_byte_addr <= bcnt; sdc_byte_in_data <= word[8 * bytei +: 8];
                    bcnt <= bcnt + 9'd1;
                    if (bytei == 5'd15) begin
                        got <= got + 6'd1;
                        if (got == 6'd31) s <= S_FIN; else s <= S_RD_ISSUE;
                    end else bytei <= bytei + 5'd1;
                end
            end
            S_WR_COLLECT: begin
                // fetch the sector byte by byte from the Minimig: the address now, the data some ticks later
                sdc_byte_addr <= bcnt; wait_c <= 4'd0; s <= S_WR_BYTE;
            end
            S_WR_BYTE: begin
                wait_c <= wait_c + 4'd1;
                if (wait_c == 4'd3) begin
                    wacc[8 * bytei +: 8] <= sdc_byte_out_data;
                    bcnt <= bcnt + 9'd1;
                    if (bytei == 5'd15) begin bytei <= 5'd0; s <= S_WR_PUSH; end
                    else begin bytei <= bytei + 5'd1; s <= S_WR_COLLECT; end
                end
            end
            S_WR_PUSH: if (!cq_full && !cq_wr) begin
                cq_wr <= 1'b1; cq_wdata <= {1'b1, sec_base_w + {18'd0, wburst, 3'd0}, wacc};
                wburst <= wburst + 6'd1;
                if (wburst == 6'd31) begin s <= S_WR_DRAIN; wait_c <= 4'd0; end else s <= S_WR_COLLECT;
            end
            S_WR_DRAIN: begin
                // wait until the ui side has issued everything (it needs a few clocks to see the queue)
                if (wait_c != 4'd15) wait_c <= wait_c + 4'd1;
                else if (idle_s[1] && !cq_wr) begin
                    if (drv[2]) begin
                        mb_req_we <= 1'b1; mb_req_drv <= drv[0]; mb_req_lba <= lba; mb_req_tgl <= ~mb_req_tgl;
                        md_clear <= 1'b1; s <= S_MBOX_WAIT;
                    end else begin
                        dirty_wr <= 1'b1; dirty_val <= {drv[1:0], 8'(lba / 32'd11)};
                        s <= S_FIN;
                    end
                end
            end
            S_FIN: begin
                sdc_done <= 1'b1; sdc_busy <= 1'b0; s <= S_IDLE;
            end
            default: s <= S_IDLE;
            endcase
        end
        if (sys_rst || flush) begin s <= S_IDLE; sdc_busy <= 1'b0; sdc_byte_in_strobe <= 1'b0; sdc_done <= 1'b0; end
        // a reset in the middle of a sector leaves read commands in the queue and burst answers in the answer FIFO: the next
        // sector would be assembled from them (shifted data). Wait until the ui side is idle, throw the answers away.
        if (sys_rst || flush) begin drn <= 1'b1; drc <= 6'd0; end
        else if (drn) begin
            if (!rq_empty && !rq_rd) begin rq_rd <= 1'b1; drc <= 6'd0; end
            else if (drc != 6'd40) drc <= drc + 6'd1;
            else if (idle_s[1] && rq_empty) drn <= 1'b0;
            else drc <= 6'd0;
        end
        // the MOUNT toggles are looked at on every sys_clk, not on ce28 only: two toggles while the machine is held (Freeze) are two writes, not none
        // (last in the block: a toggle seen in the cycle an announcement is taken keeps its flag)
        begin : mdet
            integer j;
            for (j = 0; j < 4; j = j + 1)
                if (adf_mount_tgl[j] != adf_tgl_seen[j]) begin adf_tgl_seen[j] <= adf_mount_tgl[j]; mnt_pend[j] <= 1'b1; end
            for (j = 0; j < 2; j = j + 1)
                if (hdf_mount_tgl[j] != hdf_tgl_seen[j]) begin hdf_tgl_seen[j] <= hdf_mount_tgl[j]; mnt_pend[4 + j] <= 1'b1; end
        end
    end
endmodule
