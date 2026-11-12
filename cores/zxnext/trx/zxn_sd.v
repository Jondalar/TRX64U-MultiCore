// T116 D4 / P4: the SD card the Next's SPI master talks to (o_SPI_SS_SD0_n, o_SPI_SCK, o_SPI_MOSI, i_SPI_SD_MISO; SPI mode 0, spi_master.vhd), as a
// model in gateware: an SDHC card in SPI mode whose blocks are the app's. The card answers CMD0, 1, 8, 9, 10, 12, 13, 16, 17, 18, 24, 25, 55,
// 58, 59 and ACMD41 (22, 23 and 51 get an R1); anything else gets R1 = 0x04 (illegal command). A block read raises the mailbox (mb_pend,
// mb_wr = 0, mb_lba, and a new mb_seq: every request has a tag, and DONE counts only with the tag of the request that is pending - the answer of an application
// that was slow with a request the host gave up (CMD12, deselect, a new command) is not the answer to the next one) and answers 0xFF bytes until the app
// has put the 512 bytes into the staging sector of the DDR2 and written DONE (mb_ack);
// then the card runs the staging sector into its 512 byte buffer (zxn_mem moves the 32 lines) and sends token 0xFE, the block and its CRC16.
// A block write takes the token 0xFE (0xFC for CMD25) and 512 bytes into the buffer, answers 0x05 and holds MISO low while the block goes to
// the staging sector and the app writes it into the file (mb_wr = 1); the host's CRC16 is not checked. With `present` = 0 there is no card:
// MISO stays high and the boot ROM prints "Error initializing SD card!" (T116 S10).
// Timing (spi_master.vhd): SCK = state_r(0), changes at a CPU clock edge; the master samples MISO just before SCK falls (state_r0_d), so the
// card changes MISO after the falling edge, to the bit that follows. The byte decision is made after the eighth rising edge, a CPU clock
// before the fall. One 512 byte buffer in block RAM serves the SPI side and the engine's lines (zxn_mem moves 128 bit lines; the 16 bytes
// go through the same port one by one, which the DDR2 latency hides). Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zxn_sd (
    input  wire         clk,
    input  wire         rst,
    input  wire         sck,
    input  wire         mosi,
    input  wire         ss_n,
    output wire         miso,
    input  wire         present,
    input  wire [31:0]  sectors,
    output reg          mb_pend = 1'b0,
    output reg          mb_wr = 1'b0,
    output reg  [31:0]  mb_lba = 32'd0,
    output reg  [3:0]   mb_seq = 4'd0,           // counts every request posted: the application answers the one it read, not the one that is pending by then
    input  wire         mb_ack,
    output reg          sd_start = 1'b0,
    output reg          sd_to_ddr = 1'b0,
    input  wire         sd_done,
    input  wire [4:0]   sd_k,
    input  wire         sd_we,
    input  wire [127:0] sd_wdata,
    output reg  [127:0] sd_rdata = 128'd0,
    output wire         busy,
    output wire         hold,                       // the machine stands while the application has not answered (see below)
    output reg  [31:0]  st_reads = 32'd0,
    output reg  [31:0]  st_writes = 32'd0,
    output reg  [7:0]   st_last_cmd = 8'd0
);
    // ---------------------------------------------------------------- the buffer, one port
    (* ram_style = "block" *) reg [7:0] sdbuf [0:511];
    reg [8:0]  pa = 9'd0;
    reg [7:0]  pd = 8'd0;
    reg        pwe = 1'b0;
    reg [7:0]  pq = 8'd0;
    always @(posedge clk) begin
        if (pwe) sdbuf[pa] <= pd;
        pq <= sdbuf[pa];
    end

    // ---------------------------------------------------------------- the engine's side of the buffer
    reg [4:0]  rkq = 5'd0;                           // the line read in last
    reg        rkv = 1'b0;
    reg [4:0]  rs = 5'd31;                           // 0..17 a line is being read in; 31 idle
    reg [4:0]  wk = 5'd0;
    reg [127:0] wd = 128'd0;
    reg [4:0]  ws = 5'd31;                           // 0..15 a line is being written; 31 idle
    reg        jobrun = 1'b0;
    reg        job_dir = 1'b0;
    reg        done_w = 1'b0;                        // the engine is done, waiting for the buffer writes to end

    // ---------------------------------------------------------------- the card
    localparam [4:0] ST_IDLE = 5'd0, ST_ARG = 5'd1, ST_GAP = 5'd2, ST_R1 = 5'd3, ST_EX = 5'd4, ST_RDWAIT = 5'd5, ST_DATA = 5'd6,
                     ST_CRC1 = 5'd7, ST_CRC2 = 5'd8, ST_WTOKEN = 5'd9, ST_WDATA = 5'd10, ST_WCRC1 = 5'd11, ST_WCRC2 = 5'd12,
                     ST_WBUSY = 5'd13, ST_NEXT = 5'd14;
    localparam [2:0] A_NONE = 3'd0, A_READ = 3'd1, A_READM = 3'd2, A_REG = 3'd3, A_WRITE = 3'd4, A_WRITEM = 3'd5;
    reg [4:0]  st = ST_IDLE;
    reg [2:0]  after = A_NONE;
    reg [5:0]  cmd = 6'd0;
    reg [31:0] arg = 32'd0;
    reg [2:0]  fcnt = 3'd0;
    reg        acmd = 1'b0, idle = 1'b1;
    reg [7:0]  r1 = 8'h00;
    reg [31:0] ex = 32'd0;
    reg [2:0]  ex_n = 3'd0;
    reg [8:0]  bi = 9'd0;
    reg [4:0]  ri = 5'd0;
    reg [15:0] crc = 16'd0;
    reg        src_reg = 1'b0, src_cid = 1'b0;
    reg        blk_ready = 1'b0, skip_ack = 1'b0, wr_complete = 1'b0;
    reg [1:0]  busy_min = 2'd0;                      // a card is busy for a while after a block (the data response, then 0x00 bytes): at least this many
    reg        multi = 1'b0;
    reg [31:0] cur_lba = 32'd0;

    // SPI bit level
    reg        sck_q = 1'b0, ss_q = 1'b1;
    wire       rise = sck & ~sck_q & ~ss_n & present;
    wire       fall = ~sck & sck_q & ~ss_n & present;
    reg [2:0]  bc = 3'd0;                            // rising edges of the current byte
    reg [7:0]  rx = 8'hFF;
    reg [7:0]  din = 8'hFF;
    reg        byte_ev = 1'b0;
    reg [7:0]  txn = 8'hFF;                          // the byte decided for the next slot
    reg [7:0]  txc = 8'hFF;                          // the byte on the line now
    reg        miso_r = 1'b1;
    assign miso = present ? miso_r : 1'b1;
    assign busy = (st != ST_IDLE) | mb_pend | jobrun | (ws != 5'd31) | (rs != 5'd31);

    // The application takes milliseconds for a block (a seek in a big image on the stick walks its FAT chain), the machine's own driver gives up after a
    // fixed number of polls (the Next's firmware: 5,000 INs of the data port, tens of milliseconds at 28 MHz). A real card is as fast as its owner expects;
    // this one is as slow as its application, so the machine's time stands still for as long as the application takes: the master clock is held (zxn_clk)
    // from the moment the card has asked for a block (or a written block) until the block is in the card's buffer (or the application has taken the
    // written one), and the driver finds the token at its first poll after. Nothing the machine does is missed; its sound, picture and timers wait the
    // same time (the cost is the one of a cache miss, longer). A application that does not come back lets go after 2^28 clocks (2.7 s): the card then
    // behaves as before, and the driver's own timeout reports it. The hold needs no byte of the host: the answer reaches the card by the mailbox.
    // A read holds only once the host has polled for the token (`polls`: eight bytes in the wait): the block a multi-block read asks for after the one just
    // delivered is a guess, the host's next bytes are very likely CMD12, and a machine that stood for that guess would let the application answer it before the
    // host could stop (the card would then start the block under the command). A host that polls is waiting for the block.
    reg [3:0]   polls = 4'd0;
    wire        waiting = (st == ST_RDWAIT && !blk_ready && polls[3] && (mb_pend || jobrun || done_w)) ||
                          (st == ST_WBUSY && busy_min == 2'd0 && !wr_complete);
    reg [27:0]  wcnt = 28'd0;
    reg         gave_up = 1'b0;
    always @(posedge clk) begin
        if (!waiting || rst) begin wcnt <= 28'd0; gave_up <= 1'b0; end
        else if (!gave_up) begin wcnt <= wcnt + 28'd1; if (&wcnt) gave_up <= 1'b1; end
    end
    assign hold = waiting & ~gave_up & present;

    function [15:0] crc16(input [15:0] c, input [7:0] d);
        integer i;
        reg [15:0] x;
        begin
            x = c ^ {d, 8'h00};
            for (i = 0; i < 8; i = i + 1) x = x[15] ? ((x << 1) ^ 16'h1021) : (x << 1);
            crc16 = x;
        end
    endfunction
    // CSD of an SDHC card (version 2.0) for `sectors` blocks: C_SIZE = sectors / 1024 - 1
    wire [21:0] c_size = (sectors[31:10] == 22'd0) ? 22'd0 : (sectors[31:10] - 22'd1);
    function [7:0] csd_byte(input [3:0] i, input [21:0] cs);
        case (i)
        4'd0: csd_byte = 8'h40;  4'd1: csd_byte = 8'h0E;  4'd2: csd_byte = 8'h00;  4'd3: csd_byte = 8'h32;
        4'd4: csd_byte = 8'h5B;  4'd5: csd_byte = 8'h59;  4'd6: csd_byte = 8'h00;  4'd7: csd_byte = {2'b00, cs[21:16]};
        4'd8: csd_byte = cs[15:8]; 4'd9: csd_byte = cs[7:0]; 4'd10: csd_byte = 8'h7F; 4'd11: csd_byte = 8'h80;
        4'd12: csd_byte = 8'h0A; 4'd13: csd_byte = 8'h40; 4'd14: csd_byte = 8'h00; default: csd_byte = 8'h01;   // CRC7 not computed: bit 0 set
        endcase
    endfunction
    function [7:0] cid_byte(input [3:0] i);          // manufacturer 0xFE, application "TR", product "TRX64", revision 1.0, serial 0x00116000
        case (i)
        4'd0: cid_byte = 8'hFE; 4'd1: cid_byte = 8'h54; 4'd2: cid_byte = 8'h52; 4'd3: cid_byte = 8'h54; 4'd4: cid_byte = 8'h52;
        4'd5: cid_byte = 8'h58; 4'd6: cid_byte = 8'h36; 4'd7: cid_byte = 8'h34; 4'd8: cid_byte = 8'h10; 4'd9: cid_byte = 8'h00;
        4'd10: cid_byte = 8'h11; 4'd11: cid_byte = 8'h60; 4'd12: cid_byte = 8'h00; 4'd13: cid_byte = 8'h01; 4'd14: cid_byte = 8'h23; default: cid_byte = 8'h01;
        endcase
    endfunction
    wire [7:0] reg_byte = src_cid ? cid_byte(ri[3:0]) : csd_byte(ri[3:0], c_size);
    wire [7:0] data_byte = src_reg ? reg_byte : pq;

    // what follows a response
    task dispatch;
        begin
            ex_n <= 3'd0;
            txn <= 8'hFF;
            case (after)
            A_READ, A_READM: begin
                st <= ST_RDWAIT; mb_pend <= 1'b1; mb_wr <= 1'b0; mb_lba <= arg; cur_lba <= arg; blk_ready <= 1'b0; skip_ack <= 1'b0; mb_seq <= mb_seq + 4'd1;
                multi <= (after == A_READM); st_reads <= st_reads + 32'd1;
            end
            A_REG: begin st <= ST_DATA; txn <= 8'hFE; src_reg <= 1'b1; ri <= 5'd0; crc <= 16'd0; end
            A_WRITE, A_WRITEM: begin st <= ST_WTOKEN; multi <= (after == A_WRITEM); cur_lba <= arg; end
            default: st <= ST_IDLE;
            endcase
        end
    endtask

    always @(posedge clk) begin
        sck_q <= sck; ss_q <= ss_n;
        sd_start <= 1'b0; byte_ev <= 1'b0; pwe <= 1'b0;
        if (rst || !present) begin
            st <= ST_IDLE; mb_pend <= 1'b0; bc <= 3'd0; acmd <= 1'b0; idle <= 1'b1; miso_r <= 1'b1; txn <= 8'hFF; txc <= 8'hFF;
            blk_ready <= 1'b0; skip_ack <= 1'b0; multi <= 1'b0; wr_complete <= 1'b0;
        end
        if (st != ST_RDWAIT) polls <= 4'd0;
        // ---------------- SPI bit level
        if (ss_n && !ss_q && present) begin             // deselected: a half command is dropped, a read in progress is given up
            bc <= 3'd0;
            if (st != ST_WBUSY && st != ST_WDATA && st != ST_WCRC1 && st != ST_WCRC2) begin
                if (st == ST_RDWAIT && mb_pend) skip_ack <= 1'b1;
                st <= ST_IDLE; txn <= 8'hFF; multi <= 1'b0; blk_ready <= 1'b0;
            end
            miso_r <= 1'b1; txc <= 8'hFF;
        end
        if (!ss_n && ss_q && present) begin bc <= 3'd0; txc <= txn; miso_r <= txn[7]; end
        if (rise) begin
            rx <= {rx[6:0], mosi};
            bc <= bc + 3'd1;
            if (bc == 3'd7) begin din <= {rx[6:0], mosi}; byte_ev <= 1'b1; end
        end
        if (fall) begin
            if (bc == 3'd0) begin txc <= txn; miso_r <= txn[7]; end      // the eighth clock has ended: the next byte starts
            else miso_r <= txc[3'd7 - bc];
        end
        // ---------------- the byte decision (din is a complete byte; txn is what goes out in the next slot)
        if (byte_ev) begin
            txn <= 8'hFF;
            case (st)
            ST_IDLE: if (din[7:6] == 2'b01) begin cmd <= din[5:0]; fcnt <= 3'd1; st <= ST_ARG; end
            ST_ARG: begin
                if (fcnt < 3'd5) begin arg <= {arg[23:0], din}; fcnt <= fcnt + 3'd1; end
                else begin
                    // din is the command's CRC byte: decide the answer
                    st_last_cmd <= {acmd, 1'b0, cmd};
                    st <= ST_GAP; after <= A_NONE; ex_n <= 3'd0; ex <= 32'd0; r1 <= idle ? 8'h01 : 8'h00; acmd <= 1'b0;
                    case ({acmd, cmd})
                    7'd0:  begin idle <= 1'b1; r1 <= 8'h01; end
                    7'd1:  begin idle <= 1'b0; r1 <= 8'h00; end
                    7'd8:  begin r1 <= 8'h01; ex <= {20'd0, arg[11:0]}; ex_n <= 3'd4; end
                    7'd9:  begin after <= A_REG; src_cid <= 1'b0; r1 <= 8'h00; end
                    7'd10: begin after <= A_REG; src_cid <= 1'b1; r1 <= 8'h00; end
                    7'd12: begin r1 <= 8'h00; multi <= 1'b0; blk_ready <= 1'b0; if (mb_pend) skip_ack <= 1'b1; end
                    7'd13: begin ex_n <= 3'd1; ex <= 32'd0; end
                    7'd16: r1 <= 8'h00;
                    7'd17: begin after <= A_READ; r1 <= 8'h00; end
                    7'd18: begin after <= A_READM; r1 <= 8'h00; end
                    7'd24: begin after <= A_WRITE; r1 <= 8'h00; end
                    7'd25: begin after <= A_WRITEM; r1 <= 8'h00; end
                    7'd55: begin acmd <= 1'b1; end
                    7'd58: begin ex <= 32'hC0FF8000; ex_n <= 3'd4; end
                    7'd59: r1 <= 8'h00;
                    7'd105: begin idle <= 1'b0; r1 <= 8'h00; end            // ACMD41
                    7'd87, 7'd115, 7'd86: r1 <= 8'h00;                      // ACMD23, ACMD51, ACMD22
                    default: r1 <= 8'h04;
                    endcase
                end
            end
            ST_GAP: begin txn <= r1; st <= ST_R1; end
            ST_R1, ST_EX: begin
                if (ex_n != 3'd0) begin txn <= ex[31:24]; ex <= {ex[23:0], 8'h00}; ex_n <= ex_n - 3'd1; st <= ST_EX; end
                else dispatch;
            end
            ST_RDWAIT: begin
                if (multi && din[7:6] == 2'b01) begin cmd <= din[5:0]; fcnt <= 3'd1; st <= ST_ARG; if (mb_pend) skip_ack <= 1'b1; end
                else if (blk_ready) begin
                    txn <= 8'hFE; st <= ST_DATA; src_reg <= 1'b0; bi <= 9'd0; crc <= 16'd0; pa <= 9'd0; blk_ready <= 1'b0;
                end else if (!polls[3]) polls <= polls + 4'd1;
            end
            ST_DATA: begin
                txn <= data_byte; crc <= crc16(crc, data_byte);
                if (src_reg) begin
                    ri <= ri + 5'd1;
                    if (ri == 5'd15) st <= ST_CRC1;
                end else begin
                    bi <= bi + 9'd1; pa <= bi + 9'd1;
                    if (bi == 9'd511) st <= ST_CRC1;
                end
            end
            ST_CRC1: begin txn <= crc[15:8]; st <= ST_CRC2; end
            ST_CRC2: begin txn <= crc[7:0]; st <= ST_NEXT; end
            ST_NEXT: begin
                if (!src_reg && multi) begin
                    cur_lba <= cur_lba + 32'd1; mb_pend <= 1'b1; mb_wr <= 1'b0; mb_lba <= cur_lba + 32'd1; blk_ready <= 1'b0; skip_ack <= 1'b0; mb_seq <= mb_seq + 4'd1;
                    st <= ST_RDWAIT; st_reads <= st_reads + 32'd1;
                end else st <= ST_IDLE;
            end
            ST_WTOKEN: begin
                if (din == (multi ? 8'hFC : 8'hFE)) begin st <= ST_WDATA; bi <= 9'd0; end
                else if (multi && din == 8'hFD) st <= ST_IDLE;
            end
            ST_WDATA: begin
                pa <= bi; pd <= din; pwe <= 1'b1; bi <= bi + 9'd1;
                if (bi == 9'd511) st <= ST_WCRC1;
            end
            ST_WCRC1: st <= ST_WCRC2;
            ST_WCRC2: begin
                txn <= 8'h05; st <= ST_WBUSY; wr_complete <= 1'b0; busy_min <= 2'd3;
                sd_start <= 1'b1; sd_to_ddr <= 1'b1; jobrun <= 1'b1; job_dir <= 1'b1; rkv <= 1'b0; st_writes <= st_writes + 32'd1;
            end
            ST_WBUSY: begin
                if (busy_min != 2'd0) begin busy_min <= busy_min - 2'd1; txn <= 8'h00; end
                else if (wr_complete) begin
                    wr_complete <= 1'b0;
                    if (multi) begin st <= ST_WTOKEN; cur_lba <= cur_lba + 32'd1; end else st <= ST_IDLE;
                end else txn <= 8'h00;
            end
            default: st <= ST_IDLE;
            endcase
        end
        // ---------------- the app's answer to the mailbox
        if (mb_ack && mb_pend) begin
            mb_pend <= 1'b0;
            if (mb_wr) wr_complete <= 1'b1;
            else if (skip_ack) skip_ack <= 1'b0;
            else begin sd_start <= 1'b1; sd_to_ddr <= 1'b0; jobrun <= 1'b1; job_dir <= 1'b0; end
        end
        // ---------------- the engine's job: lines between the buffer and the DDR2's staging sector
        if (jobrun && job_dir && rs == 5'd31 && ws == 5'd31 && !done_w && (!rkv || sd_k != rkq)) begin rs <= 5'd0; rkq <= sd_k; rkv <= 1'b1; end
        if (rs != 5'd31) begin
            if (rs < 5'd16) pa <= {rkq, rs[3:0]};
            if (rs >= 5'd2) sd_rdata[8 * (rs - 5'd2) +: 8] <= pq;
            rs <= (rs == 5'd17) ? 5'd31 : rs + 5'd1;
        end
        if (sd_we && !job_dir) begin wk <= sd_k; wd <= sd_wdata; ws <= 5'd0; end
        if (ws != 5'd31) begin
            pa <= {wk, ws[3:0]}; pd <= wd[8 * ws[3:0] +: 8]; pwe <= 1'b1;
            ws <= (ws == 5'd15) ? 5'd31 : ws + 5'd1;
        end
        if (sd_done && jobrun) done_w <= 1'b1;
        if (done_w && ws == 5'd31 && rs == 5'd31 && !sd_we) begin
            done_w <= 1'b0; jobrun <= 1'b0; rkv <= 1'b0;
            if (job_dir) begin mb_pend <= 1'b1; mb_wr <= 1'b1; mb_lba <= cur_lba; mb_seq <= mb_seq + 4'd1; end        // the block is in the staging sector: the app writes it
            else blk_ready <= 1'b1;
        end
    end
endmodule
