// T85: amiga_sdc (sector service) + amiga_eport on the real mig_arb and the DDR2 model, driven the way
// paula_floppy.v and nanomig.v's IDE drive the MCU's sd_card.v: floppy read and write, hard disk read and write
// through the mailbox, mount announcements, the dirty log.
`timescale 1ns/1ps
module tb_sdc;
    // T102 D1: the size register in bytes (HDFSECT 0, the Amiga core) or in sectors (1, the A1200 core); what the machine must be told
    parameter HDFSECT = 0;
    parameter [31:0] HSIZE_REG = 32'd4194304;
    parameter [63:0] HSIZE_BYTES = 64'd4194304;
    parameter [31:0] HLBA_LAST = 32'd8191;          // the last sector the image has
    reg sys_clk = 0, ui_clk = 0;
    always #15.86 sys_clk = ~sys_clk;
    always #6.667 ui_clk = ~ui_clk;
    reg sys_rst = 1;
    integer cyc = 0;
    always @(posedge sys_clk) begin cyc <= cyc + 1; if (cyc == 30) sys_rst <= 0; end
    // ce28: 9 of 10
    reg ce28 = 1; reg [3:0] n10 = 0; reg ce_hold = 0;      // ce_hold: the machine held (Freeze)
    always @(posedge sys_clk) begin n10 <= (n10 == 9) ? 0 : n10 + 1; ce28 <= (n10 != 3) && !ce_hold; end

    wire ui_rst_m, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_valid;
    mig_ui_model #(.MEM_WORDS(1<<20), .RD_LAT(8), .MISS(0), .STALL_PCT(5), .CALIB(100)) ddr (
        .ui_clk(ui_clk), .ui_rst(ui_rst_m), .calib_done(calib), .app_addr(app_addr), .app_cmd(app_cmd),
        .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_valid));
    wire [26:0] e_addr; wire [2:0] e_cmd; wire e_en, e_rdy, e_valid; wire [127:0] e_wdf_data, c_rd; wire [15:0] e_wdf_mask; wire e_wdf_wren;
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst_m), .calib_done(calib),
        .req_tgl(3'b000), .addr(78'd0), .we(3'b000), .wdata(96'd0), .be(12'd0), .done_tgl(), .rdata(), .rdata_line(),
        .c_addr(27'd0), .c_cmd(3'b001), .c_en(1'b0), .c_rdy(), .c_wdf_data(128'd0), .c_wdf_mask(16'd0), .c_wdf_wren(1'b0),
        .c_wdf_rdy(), .c_rd_data(c_rd), .c_rd_data_valid(),
        .v_addr(27'd0), .v_cmd(3'b001), .v_en(1'b0), .v_rdy(), .v_wdf_data(128'd0), .v_wdf_mask(16'd0), .v_wdf_wren(1'b0),
        .v_wdf_rdy(), .v_rd_data_valid(), .v_urgent(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(e_wdf_wren), .e_rd_data_valid(e_valid),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data),
        .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data),
        .app_rd_data_valid(app_rd_valid));

    wire b_valid, b_we, b_ready; wire [26:0] b_addr; wire [127:0] b_data, b_rdata;
    amiga_eport eport (.ui_clk(ui_clk), .ui_rst(ui_rst_m | ~calib), .a_valid(1'b0), .a_addr(27'd0), .a_data(128'd0), .a_ready(),
        .b_valid(b_valid), .b_we(b_we), .b_addr(b_addr), .b_data(b_data), .b_ready(b_ready), .b_rdata(b_rdata),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(e_wdf_wren), .e_rd_valid(e_valid), .rd_data(c_rd));

    reg flush = 0;
    wire [7:0] mounted; wire [63:0] img_size; reg [7:0] sdc_rd = 0, sdc_wr = 0; reg [31:0] sdc_sector = 0;
    wire busy, done, bstrobe; wire [8:0] baddr; wire [7:0] bin; reg [7:0] bout = 0;
    reg [3:0] adf_tgl = 0, adf_pres = 0; reg [1:0] hdf_tgl = 0, hdf_pres = 0;
    wire dirty_wr; wire [9:0] dirty_val; wire mb_tgl, mb_we, mb_drv; wire [31:0] mb_lba; reg mb_done = 0;
    amiga_sdc #(.WIN_W(27'd0), .HDF_SECT(HDFSECT)) sdc (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .ce28(ce28), .flush(flush),
        .sdc_img_mounted(mounted), .sdc_img_size(img_size), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr), .sdc_sector(sdc_sector),
        .sdc_busy(busy), .sdc_done(done), .sdc_byte_in_strobe(bstrobe), .sdc_byte_addr(baddr), .sdc_byte_in_data(bin),
        .sdc_byte_out_data(bout),
        .adf_mount_tgl(adf_tgl), .adf_present(adf_pres), .hdf_mount_tgl(hdf_tgl), .hdf_present(hdf_pres),
        .hdf_size0(HSIZE_REG), .hdf_size1(32'd0), .dirty_wr(dirty_wr), .dirty_val(dirty_val),
        .mb_req_tgl(mb_tgl), .mb_req_we(mb_we), .mb_req_drv(mb_drv), .mb_req_lba(mb_lba), .mb_done_tgl(mb_done),
        .busy_o(),
        .ui_clk(ui_clk), .ui_rst(ui_rst_m | ~calib), .b_valid(b_valid), .b_we(b_we), .b_addr(b_addr), .b_data(b_data),
        .b_ready(b_ready), .b_rdata(b_rdata));

    // byte access to the DDR2 window (offsets in bytes; the model index is the burst number, window = 16 MB)
    function [7:0] mem8(input [23:0] off);
        mem8 = ddr.mem[off >> 4][8*(off & 15) +: 8];
    endfunction
    task poke8(input [23:0] off, input [7:0] v);
        begin ddr.mem[off >> 4][8*(off & 15) +: 8] = v; end
    endtask
    function [7:0] pat(input integer drv, input integer sec, input integer i);
        pat = (drv * 37 + sec * 11 + i * 3 + (i >> 4)) & 8'hFF;
    endfunction

    integer errors = 0;
    task check(input cond, input [8*64-1:0] what);
        begin if (!cond) begin errors = errors + 1; $display("FAIL: %0s", what); end else $display("ok  : %0s", what); end
    endtask

    // the Minimig's side of a read: request, wait for busy, collect 512 bytes
    reg [7:0] got [0:511]; integer ngot;
    reg seen_done;
    always @(posedge sys_clk) if (ce28) begin
        if (bstrobe) begin got[baddr] <= bin; ngot <= ngot + 1; end
        if (done) seen_done <= 1;
    end
    task do_read(input integer idx, input integer sec);
        begin
            ngot = 0; seen_done = 0;
            @(posedge sys_clk); sdc_sector <= sec; sdc_rd[idx] <= 1;
            while (!busy) @(posedge sys_clk);
            sdc_rd <= 0;
            while (!seen_done) @(posedge sys_clk);
            repeat (4) @(posedge sys_clk);
        end
    endtask
    // a write: the Minimig answers byte requests: data for baddr appears 1 tick after the address changed
    reg [8:0] wlast = 0;
    always @(posedge sys_clk) if (ce28) bout <= (baddr * 5 + 9) & 8'hFF;
    task do_write(input integer idx, input integer sec);
        begin
            seen_done = 0;
            @(posedge sys_clk); sdc_sector <= sec; sdc_wr[idx] <= 1;
            while (!busy) @(posedge sys_clk);
            sdc_wr <= 0;
            while (!seen_done) @(posedge sys_clk);
            repeat (4) @(posedge sys_clk);
        end
    endtask

    integer i, bad, nmount = 0, ndirty = 0; reg [9:0] dv;
    always @(posedge sys_clk) begin if (|mounted) nmount <= nmount + 1; end
    // events captured
    reg [63:0] last_size;
    always @(posedge sys_clk) if (|mounted) last_size <= img_size;
    // announcements counted by their rising edge, per drive bit
    reg [7:0] mounted_d = 0; integer nev1 = 0, nev4 = 0;
    always @(posedge sys_clk) begin mounted_d <= mounted; if (mounted[1] && !mounted_d[1]) nev1 <= nev1 + 1; if (mounted[4] && !mounted_d[4]) nev4 <= nev4 + 1; end
    always @(posedge sys_clk) if (dirty_wr) begin ndirty <= ndirty + 1; dv <= dirty_val; end

    initial begin
        // ADF contents: drive d sector s byte i
        for (i = 0; i < 4 * 20; i = i + 1) begin : fill end
        #20000;
        begin : fill_all
            integer d, s, k;
            for (d = 0; d < 4; d = d + 1) for (s = 0; s < 40; s = s + 1) for (k = 0; k < 512; k = k + 1)
                poke8(24'h900000 + d * 24'hE0000 + s * 512 + k, pat(d, s, k));
        end
        #50000;
        // mount announcement
        adf_pres[0] = 1; adf_tgl[0] = ~adf_tgl[0];
        #3000; check(nmount > 0 && last_size == 64'd901120, "mount of DF0 announced with 901120 bytes");
        // T85 disk change: the application mounts from the Freeze menu, the machine is held (ce28 low): MOUNT 0, fill, MOUNT 1 are two toggles.
        // Held, nothing is announced; running again, the drive is announced once, with the last state (present, 901120 bytes). Same for a hard disk.
        ce_hold = 1; #2000;
        adf_pres[1] = 0; adf_tgl[1] = ~adf_tgl[1]; #3000;
        adf_pres[1] = 1; adf_tgl[1] = ~adf_tgl[1]; #3000;
        hdf_pres[0] = 0; hdf_tgl[0] = ~hdf_tgl[0]; #3000;
        hdf_pres[0] = 1; hdf_tgl[0] = ~hdf_tgl[0]; #3000;
        check(nev1 == 0 && nev4 == 0, "held: nothing is announced while ce28 stands");
        ce_hold = 0; #5000;
        check(nev1 == 1, "Freeze: DF1 eject + mount in one hold is announced once");
        check(nev4 == 1, "Freeze: HD0 eject + mount in one hold is announced once");
        check(adf_pres[1] === 1'b1 && mounted === 8'd0, "the announcement is a pulse, not a level");
        // a reset in the middle of a sector: the next sector must not be assembled from what the aborted one left behind
        ngot = 0; seen_done = 0;
        @(posedge sys_clk); sdc_sector <= 3; sdc_rd[2] <= 1;
        while (!busy) @(posedge sys_clk);
        sdc_rd <= 0;
        while (ngot < 100) @(posedge sys_clk);
        flush = 1; repeat (30) @(posedge sys_clk); flush = 0;
        #20000;
        do_read(2, 9);
        bad = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== pat(2, 9, i)) bad = bad + 1;
        check(ngot == 512 && bad == 0, "after a reset in mid-sector the next sector is right (nothing left over)");
        // floppy read: drive 2, sector 17
        do_read(2, 17);
        bad = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== pat(2, 17, i)) bad = bad + 1;
        check(ngot == 512 && bad == 0, "floppy read DF2 sector 17: 512 bytes, all right");
        do_read(0, 0);
        bad = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== pat(0, 0, i)) bad = bad + 1;
        check(ngot == 512 && bad == 0, "floppy read DF0 sector 0");
        // floppy write: drive 1, sector 25 (track 2)
        do_write(1, 25);
        #20000;
        bad = 0; for (i = 0; i < 512; i = i + 1) if (mem8(24'h900000 + 1 * 24'hE0000 + 25 * 512 + i) !== ((i * 5 + 9) & 8'hFF)) bad = bad + 1;
        check(bad == 0, "floppy write DF1 sector 25 landed in the DDR2 image");
        check(ndirty == 1 && dv == {2'd1, 8'd2}, "dirty log: drive 1 track 2");
        // an unaffected neighbour sector
        check(mem8(24'h900000 + 1 * 24'hE0000 + 26 * 512 + 5) === pat(1, 26, 5), "neighbour sector untouched");
        // hard disk: mount, read sector 7 through the mailbox
        hdf_pres[0] = 1; hdf_tgl[0] = ~hdf_tgl[0];
        #3000; check(last_size == HSIZE_BYTES, "mount of HD0 announced with its size in bytes");
        fork
            do_read(4, 7);
            begin : app_side
                @(mb_tgl);
                #1000;
                check(mb_we == 0 && mb_drv == 0 && mb_lba == 7, "mailbox: read request drive 0 lba 7");
                for (i = 0; i < 512; i = i + 1) poke8(24'hC80000 + i, 8'hA0 ^ i);
                mb_done = ~mb_done;
            end
        join
        bad = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== (8'hA0 ^ i[7:0])) bad = bad + 1;
        check(ngot == 512 && bad == 0, "HDF read through the mailbox: 512 bytes from the staging sector");
        // hard disk write, drive 1 sector 99
        fork
            do_write(5, 99);
            begin : app_side2
                @(mb_tgl);
                #1000;
                check(mb_we == 1 && mb_drv == 1 && mb_lba == 99, "mailbox: write request drive 1 lba 99");
                bad = 0; for (i = 0; i < 512; i = i + 1) if (mem8(24'hC80000 + i) !== ((i * 5 + 9) & 8'hFF)) bad = bad + 1;
                check(bad == 0, "the staging sector holds the written data when the request is posted");
                mb_done = ~mb_done;
            end
        join
        // the last sector of the image: the LBA that reaches the application is the sector number, 32 bit, whatever the size
        fork
            do_read(4, HLBA_LAST);
            begin : app_side3
                @(mb_tgl);
                #1000;
                check(mb_we == 0 && mb_drv == 0 && mb_lba == HLBA_LAST, "mailbox: read request for the last sector of the image");
                for (i = 0; i < 512; i = i + 1) poke8(24'hC80000 + i, 8'h5A ^ i);
                mb_done = ~mb_done;
            end
        join
        bad = 0; for (i = 0; i < 512; i = i + 1) if (got[i] !== (8'h5A ^ i[7:0])) bad = bad + 1;
        check(ngot == 512 && bad == 0, "HDF read of the last sector: 512 bytes from the staging sector");
        #5000;
        $display("PASS tb_sdc errors=%0d", errors);
        $finish;
    end
    initial begin #300000000; $display("FAIL timeout"); $finish; end
endmodule
