// T134 S9: the drive without the PET. A behavioural IEEE-488 controller (the part of the PET ROM) talks to pet_ieee: the MiSTer 4040 / 8250 drive, the
// block service pet_dblk and a DDR2 model that holds the disk image behind the host's drive port. The Commodore DOS and controller ROMs are not in this
// repository: they come from files named on the command line (+dos=, +ctl=, +dosoff=0x1000 for a 12 KB 4040 DOS), the disk from sim/gen_d64.py.
//   +type=4040|8250  +dos=<file> +ctl=<file> +dosoff=<hex> +disk=<file> +prg=<file>
// Proof: LOAD "$" returns a directory (disk name, file name, BLOCKS FREE), LOAD "TESTFILE" returns the PRG bytes of the image.
`timescale 1ns / 1ps
module tb_pet_disk;
    reg sys_clk = 0, cpu_clk = 0;
    always #15.87 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    reg rst = 1;

    // ---- the bus: levels, 1 = released; the controller's outputs and the drive's
    reg  [7:0] c_data = 8'hFF;
    reg        c_atn = 1, c_eoi = 1, c_dav = 1, c_ndac = 1, c_nrfd = 1;
    wire [7:0] d_data;
    wire       d_atn, d_eoi, d_dav, d_ndac, d_nrfd;
    wire [7:0] b_data = c_data & d_data;
    wire       b_eoi = c_eoi & d_eoi, b_dav = c_dav & d_dav, b_ndac = c_ndac & d_ndac, b_nrfd = c_nrfd & d_nrfd;

    // ---- registers
    reg        r_en = 0, r_4040 = 1;
    reg [31:0] r_size0 = 0, r_size1 = 0, r_base0 = 32'h02200000, r_base1 = 32'h02400000;
    reg [1:0]  r_ro = 0, r_mnt = 0, r_clr = 0;
    reg        rom_wr = 0, rom_sel = 1;
    reg [14:0] rom_addr = 0;
    reg [7:0]  rom_data = 0;
    wire       drv_tgl, drv_we;
    wire [25:0] drv_addr;
    wire [31:0] drv_wdata;
    wire [3:0] drv_be;
    reg        drv_done = 0;
    reg [127:0] drv_rdata = 0;
    wire [1:0] s_dirty, s_led;
    wire       s_led_err;

    pet_ieee dut (
        .sys_clk(sys_clk), .cpu_clk(cpu_clk), .rst(rst),
        .p_data_o(c_data), .p_atn_o(c_atn), .p_ndac_o(c_ndac), .p_nrfd_o(c_nrfd), .p_dav_o(c_dav), .p_eoi_o(c_eoi),
        .p_data_i(d_data), .p_atn_i(d_atn), .p_ndac_i(d_ndac), .p_nrfd_i(d_nrfd), .p_dav_i(d_dav), .p_eoi_i(d_eoi),
        .r_en(r_en), .r_4040(r_4040), .r_size0(r_size0), .r_size1(r_size1), .r_ro(r_ro), .r_mnt_tgl(r_mnt),
        .r_base0(r_base0), .r_base1(r_base1), .r_clr_dirty_tgl(r_clr),
        .s_dirty(s_dirty), .s_led(s_led), .s_led_err(s_led_err),
        .rom_wr(rom_wr), .rom_sel(rom_sel), .rom_addr(rom_addr), .rom_data(rom_data),
        .drv_tgl(drv_tgl), .drv_addr(drv_addr), .drv_we(drv_we), .drv_wdata(drv_wdata), .drv_be(drv_be),
        .drv_done_tgl(drv_done), .drv_rdata(drv_rdata)
    );

    // ---- DDR2 model: 2 MB at r_base0
    reg [7:0] mem [0:2097151];
    integer nreads = 0, nwrites = 0;
    reg tg_q = 0;
    wire [26:0] byte_a = {drv_addr, 1'b0};
    wire [20:0] mo = byte_a[20:0];
    integer i;
    always @(posedge cpu_clk) begin
        tg_q <= drv_tgl;
        if (drv_tgl != tg_q) begin
            if (drv_we) begin
                for (i = 0; i < 4; i = i + 1)
                    if (drv_be[i]) mem[{mo[20:2], 2'b00} + i] <= drv_wdata[i*8 +: 8];
                nwrites = nwrites + 1;
            end else begin
                for (i = 0; i < 16; i = i + 1) drv_rdata[i*8 +: 8] <= mem[{mo[20:4], 4'd0} + i];
                nreads = nreads + 1;
            end
            drv_done <= ~drv_done;
        end
    end

    // ---- the controller
    task tick(input integer n); repeat (n) @(posedge sys_clk); endtask
    integer tmo;
    // talker: one byte with the handshake (ATN as it is now); eoi_l = last byte of a file
    task put(input [7:0] b, input eoi_l);
        begin
            tmo = 0;
            c_dav = 1; c_eoi = eoi_l ? 0 : 1;
            c_data = ~b;
            tick(4);
            while (b_nrfd == 0 && tmo < 200000000) begin tick(1); tmo = tmo + 1; end   // wait: all listeners ready
            if (tmo >= 200000000) begin $display("FAIL timeout NRFD high"); $finish; end
            tick(2);
            c_dav = 0;
            tmo = 0;
            while (b_ndac == 0 && tmo < 200000000) begin tick(1); tmo = tmo + 1; end   // wait: data accepted
            if (tmo >= 200000000) begin $display("FAIL timeout NDAC high (byte %02x)", b); $finish; end
            tick(2);
            c_dav = 1; c_data = 8'hFF; c_eoi = 1;
            tick(4);
        end
    endtask
    // listener: one byte; eoi_o = the talker marked the last byte
    reg [7:0] got; reg got_eoi; reg got_to;
    task get;
        begin
            tmo = 0; got_to = 0;
            c_nrfd = 1;                                                              // ready
            while (b_dav == 1 && tmo < 200000000) begin tick(1); tmo = tmo + 1; end
            if (tmo >= 200000000) begin got_to = 1; end
            else begin
                c_nrfd = 0;
                tick(2);
                got = ~b_data; got_eoi = ~b_eoi;
                c_ndac = 1;                                                          // accepted
                tmo = 0;
                while (b_dav == 0 && tmo < 200000000) begin tick(1); tmo = tmo + 1; end
                tick(1);
                c_ndac = 0;
            end
        end
    endtask

    // ---- files
    reg [7:0] dosb [0:16383];
    reg [7:0] ctlb [0:2047];
    reg [7:0] prgb [0:4095];
    reg [8*200-1:0] f_dos, f_ctl, f_disk, f_prg, ty;
    integer fd, n, dosoff, prglen, k;
    reg [7:0] dir [0:4095];
    reg [7:0] fil [0:4095];
    integer dlen, flen;
    reg ok;
    // an OPEN + read of one name on channel 0 ("$" or a file), result in buf[]
    task load_name(input [8*8-1:0] nm, input integer nlen, input integer is_dir);
        integer q;
        begin
            c_atn = 0; c_nrfd = 1; c_ndac = 1;
            tick(100);
            put(8'h28, 0);                // LISTEN 8
            put(8'hF0, 0);                // OPEN channel 0
            c_atn = 1;
            tick(100);
            for (q = 0; q < nlen; q = q + 1) put(nm[(nlen-1-q)*8 +: 8], q == nlen - 1);
            c_atn = 0; tick(100);
            put(8'h3F, 0);                // UNLISTEN
            c_atn = 1; tick(100);
            c_atn = 0; tick(100);
            put(8'h48, 0);                // TALK 8
            put(8'h60, 0);                // SECOND 0
            c_ndac = 0; c_nrfd = 1;       // the controller turns listener
            c_atn = 1;
            n = 0;
            got_eoi = 0; got_to = 0;
            while (!got_eoi && !got_to && n < 4000) begin
                get;
                if (!got_to) begin
                    if (is_dir) dir[n] = got; else fil[n] = got;
                    n = n + 1;
                end
            end
            if (is_dir) dlen = n; else flen = n;
            c_nrfd = 0; c_ndac = 0;
            c_atn = 0; tick(100);
            put(8'h5F, 0);                // UNTALK
            c_atn = 1; c_nrfd = 1; c_ndac = 1; tick(200);
        end
    endtask

    integer a;
    reg nr_q = 0; always @(posedge sys_clk) begin nr_q <= d_nrfd; if (d_nrfd != nr_q) $display("t=%0t drive NRFD %b ATN(c)=%b DAV=%b reads=%0d led=%b", $time, d_nrfd, c_atn, d_dav, nreads, s_led); end
    initial begin
        for (a = 0; a < 2097152; a = a + 1) mem[a] = 8'h00;
        ty = "4040";
        if ($value$plusargs("type=%s", ty)) ;
        r_4040 = (ty == "4040");
        if ($test$plusargs("synth")) begin : synth
            // T134 S9 smoke: no Commodore ROM. A synthetic DOS image (4040 layout) for the drive's DOS CPU: it sets RIOT2 ($0280/$0281: the I/O half of the RIOT is A9 = 1) DDRA, then loops: ATN in (PA7) -> DAV low, else DAV high.
            // The controller ROM stays $FF. Proves: the T65 runs from the DOS ROM window, RIOT2 outputs reach the PET's bus lines, ATN from the PET reaches the RIOT.
            reg [7:0] pg [0:24];
            integer e1, e2, e3, e4;
            pg[0]=8'h78; pg[1]=8'hA9; pg[2]=8'h1F; pg[3]=8'h8D; pg[4]=8'h81; pg[5]=8'h02;
            pg[6]=8'hAD; pg[7]=8'h80; pg[8]=8'h02; pg[9]=8'h30; pg[10]=8'h08;
            pg[11]=8'hA9; pg[12]=8'hFC; pg[13]=8'h8D; pg[14]=8'h80; pg[15]=8'h02; pg[16]=8'h4C; pg[17]=8'h06; pg[18]=8'hF0;
            pg[19]=8'hA9; pg[20]=8'hEC; pg[21]=8'h8D; pg[22]=8'h80; pg[23]=8'h02; pg[24]=8'h4C;
            for (a = 0; a < 16384; a = a + 1) dosb[a] = 8'hFF;
            for (a = 0; a < 25; a = a + 1) dosb[14'h3000 + a] = pg[a];
            dosb[14'h3000+25] = 8'h06; dosb[14'h3000+26] = 8'hF0;
            dosb[14'h3FFC] = 8'h00; dosb[14'h3FFD] = 8'hF0;
            for (a = 0; a < 2048; a = a + 1) ctlb[a] = 8'hFF;
            rom_sel = 1;
            tick(20);
            for (a = 0; a < 16384; a = a + 1) begin @(posedge sys_clk); rom_wr <= 1; rom_addr <= a[14:0]; rom_data <= dosb[a]; end
            for (a = 0; a < 2048; a = a + 1) begin @(posedge sys_clk); rom_wr <= 1; rom_addr <= 15'h4000 + a[14:0]; rom_data <= ctlb[a]; end
            @(posedge sys_clk); rom_wr <= 0;
            r_en = 1; tick(20); rst = 0;
            tick(36000000);   // the drive holds its own reset for 2^24 ce ticks (16 MHz): 1.05 s after enable / ROM write
            e1 = {31'd0, d_dav} + 2*d_ndac + 4*d_nrfd + 8*d_eoi;        // ATN released: DAV, NDAC, NRFD, EOI all released
            c_atn = 0; tick(20000);
            e2 = {31'd0, d_dav} + 2*d_ndac + 4*d_nrfd + 8*d_eoi;        // ATN asserted: DAV low (software), NDAC and NRFD low (RIOT logic), EOI released
            c_atn = 1; tick(20000);
            e3 = {31'd0, d_dav} + 2*d_ndac + 4*d_nrfd + 8*d_eoi;
            $display("synth: idle=%b atn=%b released=%b (bits EOI,NRFD,NDAC,DAV)", e1[3:0], e2[3:0], e3[3:0]);
            if (e1 == 15 && e2 == 8 && e3 == 15) $display("PASS synth: the drive's DOS CPU runs and answers ATN on the PET's bus lines");
            else $display("FAIL synth");
            $finish; #1;
        end
        if (!$value$plusargs("dos=%s", f_dos) || !$value$plusargs("ctl=%s", f_ctl) || !$value$plusargs("disk=%s", f_disk) || !$value$plusargs("prg=%s", f_prg)) begin
            $display("FAIL need +dos= +ctl= +disk= +prg="); $finish;
        end
        dosoff = 0; if ($value$plusargs("dosoff=%h", dosoff)) ;
        for (a = 0; a < 16384; a = a + 1) dosb[a] = 8'hFF;
        fd = $fopen(f_dos, "rb"); n = $fread(dosb, fd, dosoff, 16384 - dosoff); $fclose(fd);
        fd = $fopen(f_ctl, "rb"); n = $fread(ctlb, fd); $fclose(fd);
        fd = $fopen(f_prg, "rb"); prglen = $fread(prgb, fd); $fclose(fd);
        fd = $fopen(f_disk, "rb"); n = $fread(mem, fd); $fclose(fd);   // the image at the start of the model = r_base0
        $display("disk %0d bytes, prg %0d bytes, type %0s", n, prglen, ty);
        rom_sel = r_4040;
        tick(20);
        // the ROMs through the window's path (one byte per clock)
        for (a = 0; a < 16384; a = a + 1) begin
            @(posedge sys_clk); rom_wr <= 1; rom_addr <= a[14:0]; rom_data <= dosb[a];
        end
        for (a = 0; a < 2048; a = a + 1) begin
            @(posedge sys_clk); rom_wr <= 1; rom_addr <= 15'h4000 + a[14:0]; rom_data <= ctlb[a];
        end
        @(posedge sys_clk); rom_wr <= 0;
        tick(10);
        // register setup as the app does it: size, base, enable, then the mount strobe of drive 0
        r_size0 = n;
        r_en = 1;
        tick(20);
        r_mnt = 2'b01;
        tick(20);
        rst = 0;
        // the DOS needs a moment (self test, init) before it listens
        tick(20000);
        load_name("$", 1, 1);
        $write("dir %0d bytes:", dlen);
        for (a = 0; a < dlen; a = a + 1) $write(" %02x", dir[a]);
        $write("\n");
        ok = (dlen > 30);
        // the directory program: load address, then lines; look for TESTDISK, TESTFILE, "BLOCKS FREE"
        begin : chk
            integer j, f1, f2, f3;
            f1 = 0; f2 = 0; f3 = 0;
            for (j = 0; j + 8 <= dlen; j = j + 1) begin
                if (dir[j] == "T" && dir[j+1] == "E" && dir[j+2] == "S" && dir[j+3] == "T" && dir[j+4] == "D" && dir[j+5] == "I" && dir[j+6] == "S" && dir[j+7] == "K") f1 = 1;
                if (dir[j] == "T" && dir[j+1] == "E" && dir[j+2] == "S" && dir[j+3] == "T" && dir[j+4] == "F" && dir[j+5] == "I" && dir[j+6] == "L" && dir[j+7] == "E") f2 = 1;
            end
            for (j = 0; j + 6 <= dlen; j = j + 1)
                if (dir[j] == "B" && dir[j+1] == "L" && dir[j+2] == "O" && dir[j+3] == "C" && dir[j+4] == "K" && dir[j+5] == "S") f3 = 1;
            $display("directory: disk name %0d, file name %0d, BLOCKS FREE %0d", f1, f2, f3);
            ok = ok && f1 && f2 && f3;
        end
        $display("%0s dir", ok ? "PASS" : "FAIL");
        load_name("TESTFILE", 8, 0);
        $display("file %0d bytes (expected %0d), DDR2 reads %0d", flen, prglen, nreads);
        begin : cmp
            integer j, bad;
            bad = 0;
            for (j = 0; j < prglen; j = j + 1) if (fil[j] !== prgb[j]) bad = bad + 1;
            if (flen != prglen) bad = bad + 1000;
            $display("%0s file: %0d mismatches", bad == 0 ? "PASS" : "FAIL", bad);
            ok = ok && (bad == 0);
        end
        $display("%0s tb_pet_disk %0s", ok ? "PASS" : "FAIL", ty);
        $finish;
    end
    initial begin
        #6000000000;     // 6 s of simulated time
        $display("FAIL tb_pet_disk watchdog"); $finish;
    end
endmodule
