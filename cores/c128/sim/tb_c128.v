// T113: the whole C128 machine (c64_core_c128) in a bench. The system ROM is a synthetic image of our own (tools/gen_c128_test.py) that
// build_machine.sh preloads (mem/c128_rom.mem); no ROM byte of any real machine is involved.
// Plusargs:  +MS=<n>        run n milliseconds of simulated time (default 20)
//            +EXPECT=a:v,..  RAM bytes (hex) that must hold at the end: aaaa:vv (block 0), 1aaaa:vv (block 1)
//            +TRACE          print every CPU bus cycle (end of the CPU half) with the active CPU
//            +KEYS=<hex64>   c64 key matrix bits held down from the start (64 bit)
//            +XK=<hex>       the extra matrix columns K0-K2 (24 bit)
//            +T0=<ms>        the first <t0> ms the keys are not held
//            +KEYOFF=<us>    the keys are released at that time (us of simulated time)
//            +SHOT_AT=<us>   print the 40-column screen ($0400) at that time ("scr|" lines) as well as at the end
//            +VSHOT_AT=<us>  print the VDC's 80 x 25 text screen ("vdc|" lines) at that time
//            +RESET_AT=<us>  the machine reset (the core's reset request, what the application's Reset pulses) goes high at that time, +RESET_US=<us> long (default 100)
//            +COLD_AT=<us>   the cold-start line (register page COLD_START) is pulsed at that time (T113 5.6)
//            +TRFROM=<us>    +TRACE prints from that time on
//            +DUMP=<hex>     at the end print 16 x DUMPN (default 6) bytes of RAM block 0 from that address (\"ram|addr|bytes\")
//            +PIXHASH=<n>    print a hash of the VIC's pixel stream (colour, x, y) over the n-th frame ("pixhash|n|hash")
//            +FRAMES         print the period of every VIC frame ("frame|n|clocks|reset") and the raster position at which the reset came
//            +VKREAD_AT=<us> +VDCFIELDS=<us> +VRAMALL   the VDC read-back with the machine running and stopped, four fields of the VDC's picture, the whole VDC RAM
//                            (T257, run_ruler.sh; at their definitions below)
`timescale 1ns / 1ps
module tb_c128;
`include "tb_c128_ports.vh"
    // the clocks: sys clk 31.5278 MHz (PAL), clk2x twice that and edge-aligned, the SID's MCLK 12.288 MHz
    reg clk2x_r = 1'b0, clk_r = 1'b0, mclk_r = 1'b0;
    always #7.9195 clk2x_r = ~clk2x_r;
    always @(posedge clk2x_r) clk_r <= ~clk_r;
    always #40.69 mclk_r = ~mclk_r;
    // the machine's gate: gclk is clk through a BUFGCE whose CE (gate_ce) is latched while clk is low
    reg ce_l = 1'b1;
    always @(*) if (!clk_r) ce_l = dut.gate_ce;
    always @(*) begin clk = clk_r; gclk = clk_r & ce_l; clk2x = clk2x_r; snd_clk = mclk_r; sid_cv_clk = clk_r; cart_wclk = clk_r; cart_ee_clk = clk_r; end

    initial begin
        snd_rst = 1'b1;
        slot_game = 1'b1; slot_exrom = 1'b1; slot_dma = 1'b1; slot_irq = 1'b1; slot_nmi = 1'b1; slot_rst_i = 1'b1;
        tape_read = 1'b1; tape_sense_n = 1'b1;
        sock_mask = 16'hFEFE; sock_base = 16'h0101;
        sid_cfg_base = 16'h0000; sid_cfg_mask = 16'hFFFF;
        cart_ack_tgl = 1'b0;
        reu_cmd_ready = 1'b1;
        rst = 1'b1;
        #1000 snd_rst = 1'b0;
        #3000 rst = 1'b0;
    end

    // 48 kHz sample strobe for the SID's audio side
    integer sdiv = 0;
    always @(posedge snd_clk) begin
        audio_sample_en <= 1'b0;
        sdiv <= sdiv + 1;
        if (sdiv == 255) begin sdiv <= 0; audio_sample_en <= 1'b1; end
    end

    // the serial bus: the core's three outputs are line levels (1 = released); one device on it, as a drive is: it pulls DATA low while ATN is
    // asserted (+NODEV: no device on the bus, so the KERNAL's boot attempt ends; run_robots.sh uses it)
    reg nodev = 1'b0;
    reg dv_on = 1'b0, dv_clk = 1'b1, dv_data = 1'b1;       // +DEV8: the bus-level device below instead of the stub; its line drivers (1 = released)
    wire drv_clk_o, drv_data_o;
    reg  drv_on = 1'b0;                                     // -DSIM_DRIVE: the real 1541 below is on the bus instead of the stub (+DRIVE)
    always @(*) begin
        iec_atn_i  = iec_atn_o;
        iec_srq_i  = iec_srq_o;                         // T170: SRQ is the core's own line here (no device pulls it)
        iec_clk_i  = iec_clk_o & dv_clk & drv_clk_o;
        iec_data_i = iec_data_o & dv_data & drv_data_o & (dv_on | nodev | drv_on | iec_atn_o);
    end
`ifdef SIM_DRIVE
    // T140 5.19 (issue #67): a real 1541 (vendor/icepi c64_c1541: its 6502, VIAs and GCR head) with the 1541 ROM (mem/d1541.mem, 32 KiB, 16 KiB mirrored)
    // and the disk image as raw .d64 bytes (mem/disk.mem, tools/c128_mem_from_bin.py); the track "SD" side is this memory: block_lba is the first sector of
    // the track (start_sectors.mem), buff_addr = {sector, byte}. Read only. +DRIVE puts it on the bus.
    reg [7:0] d_rom [0:32767];
    reg [7:0] d_img [0:262143];
    wire [14:0] drv_ra; wire drv_re;
    wire [31:0] drv_blba; wire [5:0] drv_bcnt; wire drv_brd, drv_bwr;
    wire [12:0] drv_ba; wire [7:0] drv_bdin; wire drv_bwe, drv_ben, drv_led, drv_busy, drv_mtr;
    reg  [7:0] drv_rq = 8'h00, drv_bq = 8'h00;
    reg        drv_ack = 1'b0, drv_mnt = 1'b0;
    reg  [7:0] drv_ackc = 8'd0;
    reg  [9:0] drv_lba = 10'd0;
    initial begin
        $readmemh("mem/d1541.mem", d_rom);
        $readmemh("mem/disk.mem", d_img);
        if ($test$plusargs("DRIVE")) drv_on = 1'b1;
        #3000000 drv_mnt = 1'b1; #200 drv_mnt = 1'b0;
    end
    always @(posedge clk_r) begin
        if (drv_re) drv_rq <= d_rom[drv_ra];
        drv_bq <= d_img[{drv_lba, 8'd0} + {5'd0, drv_ba}];
        drv_ack <= 1'b0;
        if (drv_brd || drv_bwr) begin
            if (drv_ackc == 8'd40) begin drv_ack <= 1'b1; drv_lba <= drv_blba[9:0]; drv_ackc <= 8'd0; end
            else drv_ackc <= drv_ackc + 8'd1;
        end else drv_ackc <= 8'd0;
    end
    wire drv_clk_w, drv_data_w;
    assign drv_clk_o  = drv_on ? drv_clk_w  : 1'b1;
    assign drv_data_o = drv_on ? drv_data_w : 1'b1;
    c64_c1541 #(.NTSC(0), .DRIVE_NUM(0), .EXTERNAL_ROM(1)) drive (
        .clk(clk_r), .reset(rst || !drv_on), .stall(1'b0),
        .img_id(16'h0000), .img_readonly(1'b1), .img_present(1'b1), .img_mounted(drv_mnt),
        .led(drv_led), .busy(drv_busy), .mtr(drv_mtr),
        .iec_atn_i(iec_atn_i), .iec_data_i(iec_data_i), .iec_clk_i(iec_clk_i), .iec_data_o(drv_data_w), .iec_clk_o(drv_clk_w),
        .par_data_i(8'hFF), .par_stb_i(1'b1), .par_data_o(), .par_stb_o(), .ext_en(1'b0),
        .block_lba(drv_blba), .block_cnt(drv_bcnt), .block_rd(drv_brd), .block_wr(drv_bwr), .block_ack(drv_ack),
        .buff_addr(drv_ba), .buff_dout(drv_bq), .buff_din(drv_bdin), .buff_we(drv_bwe), .buff_en(drv_ben),
        .ext_rom_en(drv_re), .ext_rom_addr(drv_ra), .ext_rom_dout(drv_rq));
`else
    assign drv_clk_o = 1'b1; assign drv_data_o = 1'b1;
`endif

    // +DEV8 (T140 D31, bench only): device 8 on the serial bus, written from the C128 KERNAL's routines (ROM 318020-05: LISTEN/CIOUT $E343-$E412, TKSA $E4E0, ACPTR $E43E,
    // UNLSN $E515). It answers ATN with DATA low, takes the command bytes (LISTEN $28, SECOND, TALK $48, TKSA, UNLISTEN, UNTALK) and the name, and serves ONE file (mem/devfile.mem,
    // +DEVLEN bytes, whatever the name) to a TALK with the KERNAL's handshake: CLK released = ready to send, DATA released by the host = go, EOI = CLK kept high until the host's
    // DATA pulse, 8 bits with data valid 70 us before CLK high and CLK high 70 us (a badline holds the host's CPU for up to ~43 us, 30 us pulses were lost), frame acknowledge = the host pulls DATA low.
    reg  [7:0] dv_file [0:65535];
    integer    dv_len = 0, dv_pos = 0, dv_nrx = 0, dv_t, dv_i;
    reg  [7:0] dv_rx, dv_cmd, dv_sec = 8'h00;
    reg        dv_eoi, dv_ab, dv_chk, dv_lis = 1'b0, dv_tlk = 1'b0;
    reg  [8*16-1:0] dv_name;
    task dv_wclk(input lvl);                             // wait for CLK = lvl (aborts when ATN falls and dv_chk is set)
        begin
            if (!dv_ab) begin
                wait (iec_clk_i == lvl || (dv_chk && !iec_atn_i));
                if (dv_chk && !iec_atn_i) dv_ab = 1'b1;
            end
        end
    endtask
    task dv_rxbyte;                                      // listener, the host talks (precondition: we hold DATA low)
        begin
            dv_rx = 8'h00; dv_eoi = 1'b0;
            dv_wclk(1'b1);                               // host: ready to send
            if (!dv_ab) begin
                #3000 dv_data = 1'b1;                    // ready for data
                dv_t = 0;
                while (iec_clk_i && dv_t < 150) begin #1000 dv_t = dv_t + 1; end
                if (iec_clk_i) begin                     // EOI: CLK stayed high for 150 us
                    dv_eoi = 1'b1; #5000 dv_data = 1'b0; #60000 dv_data = 1'b1;
                end
                for (dv_i = 0; dv_i < 8 && !dv_ab; dv_i = dv_i + 1) begin
                    dv_wclk(1'b0);
                    dv_wclk(1'b1);
                    if (!dv_ab) begin #1000 dv_rx = {iec_data_i, dv_rx[7:1]}; end
                end
                dv_wclk(1'b0);
                #8000 dv_data = 1'b0;                    // frame acknowledge
            end
        end
    endtask
    reg [3:0] dv_st = 4'd0;                              // where the talker is (for the +DEVDBG lines and the end line)
    always @(dv_st) if ($test$plusargs("DEVDBG") && dv_pos < 4) $display("devdbg|%0.1f us|byte %0d state %0d|atn %b clk %b data %b", $realtime / 1000.0, dv_pos, dv_st, iec_atn_i, iec_clk_i, iec_data_i);
    final if (dv_on) $display("devend|pos %0d of %0d|state %0d|bit %0d|ab %b|lis %b tlk %b|atn %b clk %b (dev %b host %b) data %b (dev %b host %b)", dv_pos, dv_len, dv_st, dv_i, dv_ab, dv_lis, dv_tlk,
        iec_atn_i, iec_clk_i, dv_clk, iec_clk_o, iec_data_i, dv_data, iec_data_o);
    task dv_txbyte(input [7:0] b, input last);           // talker (precondition: CLK held low by us, DATA low by the host)
        begin
            dv_st = 4'd1;
            #50000 dv_clk = 1'b1;                        // ready to send
            dv_st = 4'd2;
            wait (iec_data_i == 1'b1 || !iec_atn_i);     // the host released DATA = go
            dv_st = 4'd3;
            if (!iec_atn_i) dv_ab = 1'b1;
            if (!dv_ab && last) begin
                dv_st = 4'd7;
                wait (iec_data_i == 1'b0 || !iec_atn_i); wait (iec_data_i == 1'b1 || !iec_atn_i);   // EOI: the host's DATA pulse
                if (!iec_atn_i) dv_ab = 1'b1;
            end
            if (!dv_ab) begin
                #40000 dv_clk = 1'b0;
                dv_st = 4'd4;
                for (dv_i = 0; dv_i < 8; dv_i = dv_i + 1) begin
                    dv_data = b[dv_i]; #70000 dv_clk = 1'b1; #70000 dv_clk = 1'b0;
                end
                dv_data = 1'b1; #2000;
                dv_st = 4'd5;
                wait (iec_data_i == 1'b0 || !iec_atn_i);  // frame acknowledge
                dv_st = 4'd6;
                if (!iec_atn_i) dv_ab = 1'b1;
            end
        end
    endtask
    initial if ($test$plusargs("DEV8")) begin
        nodev = 1'b1;                                    // no device until +DEVAT (us): the boot attempt of the KERNAL (a disk read at ~1.5 s) must end as in the +NODEV runs
        if (!$value$plusargs("DEVLEN=%d", dv_len)) dv_len = 0;
        if ($value$plusargs("DEVAT=%d", dv_t)) #(dv_t * 1000.0);
        dv_on = 1'b1;
        $readmemh("mem/devfile.mem", dv_file);
        wait (iec_atn_i == 1'b1);                        // ATN reads low until the KERNAL has initialised the port
        forever begin
            wait (iec_atn_i == 1'b0);
            dv_ab = 1'b0; dv_chk = 1'b0; dv_clk = 1'b1; dv_data = 1'b0;      // present: DATA low
            $display("dev|%0.1f us|ATN asserted|lis %b tlk %b", $realtime / 1000.0, dv_lis, dv_tlk);
            wait (iec_clk_i == 1'b0 || iec_atn_i);
            while (!iec_atn_i) begin
                dv_rxbyte;
                dv_cmd = dv_rx;
                $display("dev|%0.1f us|command %02x", $realtime / 1000.0, dv_cmd);
                if (dv_cmd == 8'h28) begin dv_lis = 1'b1; dv_tlk = 1'b0; dv_nrx = 0; dv_name = 0; end
                else if (dv_cmd == 8'h48) begin dv_tlk = 1'b1; dv_lis = 1'b0; dv_pos = 0; end
                else if (dv_cmd == 8'h3F) dv_lis = 1'b0;
                else if (dv_cmd == 8'h5F) dv_tlk = 1'b0;
                else dv_sec = dv_cmd;
                wait (iec_atn_i || iec_clk_i);
            end
            #5000;
            if (dv_lis) begin                                    // data bytes (the name) until ATN falls again
                dv_chk = 1'b1; dv_ab = 1'b0;
                while (!dv_ab) begin
                    dv_rxbyte;
                    if (!dv_ab) begin
                        dv_name = {dv_name[8*15-1:0], dv_rx}; dv_nrx = dv_nrx + 1;
                        if (dv_eoi) $display("dev|%0.1f us|name (%0d bytes, last 16) %0s", $realtime / 1000.0, dv_nrx, dv_name);
                    end
                end
            end else if (dv_tlk) begin                           // turnaround and the file
                dv_data = 1'b1; #20000 dv_clk = 1'b0;
                $display("dev|%0.1f us|TALK, sec %02x, sending %0d bytes", $realtime / 1000.0, dv_sec, dv_len);
                dv_chk = 1'b1; dv_ab = 1'b0;
                while (!dv_ab && dv_pos < dv_len) begin
                    dv_txbyte(dv_file[dv_pos], dv_pos == dv_len - 1);
                    if (!dv_ab) begin
                        dv_pos = dv_pos + 1;
                        if (dv_pos % 256 == 0 || dv_pos == dv_len) $display("dev|%0.1f us|sent %0d of %0d", $realtime / 1000.0, dv_pos, dv_len);
                    end
                end
                if (dv_ab) $display("dev|%0.1f us|talk aborted by ATN after %0d bytes", $realtime / 1000.0, dv_pos);
                else wait (iec_atn_i == 1'b0);
            end else begin dv_clk = 1'b1; dv_data = 1'b1; end
        end
    end

`ifdef ROM_CACHE
    // the ROM image in the DDR2 behind the line cache (5.4): c128_rome on the host's client port, the NES bench's DDR2 model with the image in it
    reg ui_clk_r = 1'b0;
    always #6.667 ui_clk_r = ~ui_clk_r;                  // 75 MHz
`ifdef RAM_DDR2
    // T140 D25: c128_ddrx serves the ROM line fetch, the RAM line fetch and the RAM byte write; the DDR2 model holds the ROM image at 0x01C00000 and the RAM at 0x01C20000
    wire        x_r_done, x_m_done, x_w_done, x_owns, x_en, x_rdy, x_rdv;
    wire  [2:0] x_cmd;
    wire [26:0] x_addr;
    wire [127:0] x_fill, x_wdata, x_rd;
    wire [15:0] x_wmask;
    always @(*) begin rom_done_tgl = x_r_done; rom_fill = x_fill; ram_m_done_tgl = x_m_done; ram_w_done_tgl = x_w_done; end
    // the host's `ui_rst || !ddr_calib` (layer2_c128.v): the DDR2 controller is not calibrated for the first milliseconds after configuration while the RAM's
    // sweep (c128_ramd, ungated clock) starts with the first clock. +UIRST_US=<n>: the reset is high for n us from time 0 (default: never, as before)
    reg ui_rst_r = 1'b0;
    integer uirst_us;
    initial if ($value$plusargs("UIRST_US=%d", uirst_us) && uirst_us > 0) begin ui_rst_r = 1'b1; #(uirst_us * 1000.0) ui_rst_r = 1'b0; end
    c128_ddrx ddrx (.ui_clk(ui_clk_r), .ui_rst(ui_rst_r),
        .r_req_tgl(rom_req_tgl), .r_req_line(rom_req_line), .r_done_tgl(x_r_done),
        .m_req_tgl(ram_m_req_tgl), .m_req_line(ram_m_req_line), .m_done_tgl(x_m_done),
        .w_req_tgl(ram_w_req_tgl), .w_req_addr(ram_w_req_addr), .w_req_data(ram_w_req_data), .w_done_tgl(x_w_done),
        .fill(x_fill), .owns(x_owns), .en(x_en), .cmd(x_cmd), .addr(x_addr), .wdata(x_wdata), .wmask(x_wmask), .rdy(x_rdy), .rd_valid(x_rdv), .rd_data(x_rd));
`ifndef DDR_LATMIN
 `define DDR_LATMIN 22
`endif
`ifndef DDR_LATRND
 `define DDR_LATRND 30
`endif
    ddr2c_model #(.BASE(32'h01C00000), .LAT_MIN(`DDR_LATMIN), .LAT_RND(`DDR_LATRND)) ddr (.ui_clk(ui_clk_r), .c_addr(x_addr), .c_cmd(x_cmd), .c_en(x_en), .c_rdy(x_rdy),
        .c_wdf_data(x_wdata), .c_wdf_mask(x_wmask), .c_wdf_wren(x_en && x_cmd == 3'b000), .c_rd_data(x_rd), .c_rd_valid(x_rdv));
`else
    wire        rome_done, rome_owns, rome_en, rome_rdy, rome_rdv;
    wire [127:0] rome_fill, rome_rd;
    wire [26:0] rome_addr;
    always @(*) begin rom_done_tgl = rome_done; rom_fill = rome_fill; end
    c128_rome #(.ROM_BASE_W(27'h0E00000)) rome (.ui_clk(ui_clk_r), .ui_rst(1'b0), .req_tgl(rom_req_tgl), .req_line(rom_req_line),
        .done_tgl(rome_done), .fill(rome_fill), .owns(rome_owns), .en(rome_en), .addr(rome_addr), .rdy(rome_rdy), .rd_valid(rome_rdv), .rd_data(rome_rd));
    ddr2c_model #(.BASE(32'h01C00000)) ddr (.ui_clk(ui_clk_r), .c_addr(rome_addr), .c_cmd(3'b001), .c_en(rome_en), .c_rdy(rome_rdy),
        .c_wdf_data(128'd0), .c_wdf_mask(16'hFFFF), .c_wdf_wren(1'b0), .c_rd_data(rome_rd), .c_rd_valid(rome_rdv));
`endif
    reg [15:0] rimg [0:36863];
    integer rn, rk;
    initial begin
        $readmemh("mem/c128_rom.mem", rimg);
        for (rn = 0; rn < 4608; rn = rn + 1)
            for (rk = 0; rk < 8; rk = rk + 1) ddr.mem[rn][16 * rk +: 16] = rimg[rn * 8 + rk];
    end
    final $display("rom cache: %0d hits, %0d misses, %0d wrong answers", rom_n_hit, rom_n_miss, rc_bad);
    // every ROM answer against the image (the cache must never return a wrong word)
    integer rc_bad = 0;
    reg [15:0] rc_word;
    reg        rc_pend = 1'b0;
    always @(posedge gclk) begin
        if (dut.mem.romc.rvalid && rc_pend) begin
            if (dut.mem.romc.rdata !== rimg[rc_word]) begin
                rc_bad = rc_bad + 1;
                if (rc_bad < 8) $display("ROMBAD %t word %h got %h want %h (hit %0d miss %0d)", $time, rc_word, dut.mem.romc.rdata, rimg[rc_word], rom_n_hit, rom_n_miss);
            end
            rc_pend <= 1'b0;
        end
        if (dut.mem.romc.cmd_valid && dut.mem.romc.cmd_ready) begin rc_word <= dut.mem.romc.cmd_word; rc_pend <= 1'b1; end
    end
`endif

    c64_core_c128 dut (
`include "tb_c128_inst.vh"
`ifdef ROM_CACHE
        ,
`include "tb_c128_inst_cache.vh"
`endif
`ifdef RAM_DDR2
        ,
`include "tb_c128_inst_ramd.vh"
`endif
    );

    // ---- what the bench looks at ------------------------------------------------------------------------------------------
    wire        cpu_end = dut.c64.vic_phi2_n && !dut.c64.vic_stall;
    wire [15:0] b_addr  = dut.c64.cpu_addr;
    wire  [7:0] b_din   = dut.c64.cpu_din;
    wire  [7:0] b_dout  = dut.c64.cpu_dout;
    wire        b_we    = dut.c64.cpu_we;
    wire        z80_off = !dut.c64.z80_act;          // 1: the 8502 owns the bus
    integer n_z80 = 0, n_8502 = 0, first_8502 = -1, last_z80 = -1, cyc = 0, held_cyc = 0, pause_cyc = -1, snap_cyc = -1, pause_ns = 0;
    realtime pause_t = 0;
    // the hold ends after pause_ns of simulated time (the gate opens at a frame boundary: the hold is a whole number of frames)
    always @(posedge clk_r) if (halt_req && ($realtime - pause_t) > pause_ns) halt_req = 1'b0;
    // the 40 x 25 screen at $0400 of RAM block 0 as text (screen codes: 1-26 letters, 32-63 as ASCII)
    task dump_screen;
        integer r, c; reg [7:0] b; reg [8*40-1:0] ln;
        begin
            for (r = 0; r < 25; r = r + 1) begin
                ln = 0;
                for (c = 0; c < 40; c = c + 1) begin
                    b = ram_byte({1'b0, 16'h0400 + r * 40 + c});
                    b = b & 8'h7F;
                    if (b >= 1 && b <= 26) b = b + 8'd64; else if (b == 0) b = 8'd64; else if (b < 32) b = 8'd46;
                    ln = {ln[8*39-1:0], b};
                end
                $display("scr|%s", ln);
            end
        end
    endtask
    // the VDC's 80 x 25 text screen (screen RAM at 0 of the VDC's RAM, R12/R13 = 0 as the KERNAL leaves it)
    task dump_vdc;
        integer r, c; reg [7:0] b; reg [8*80-1:0] ln; reg [15:0] la, pa;
        begin
            for (r = 0; r < 25; r = r + 1) begin
                ln = 0;
                for (c = 0; c < 80; c = c + 1) begin
                    la = r * 80 + c;
                    pa = dut.c64.x.vdc.reg_ram ? la[15:0] : {la[15], la[13:8], la[8], la[7:0]};     // the VDC's own address shuffle (vdc_ramiface.sv shuffleAddr)
                    b = dut.c64.x.vdc.ram.ram.m[pa] & 8'h7F;
                    if (b >= 1 && b <= 26) b = b + 8'd64; else if (b == 0) b = 8'd64; else if (b < 32) b = 8'd46;
                    ln = {ln[8*79-1:0], b};
                end
                $display("vdc|%s", ln);
            end
        end
    endtask
    // +VDCPROBE (T140 5.19): every CPU write into the VDC (cw), what the VDC's RAM interface did with it (rw = a RAM write, DROP = a pending CPU action was
    // replaced before the RAM saw it), and the Z80's own writes to $D6xx at the latch (zw), to count Z80 writes against VDC strobes. act: 0 idle 1 read 2 write 3 fill 4/5 copy.
    reg vprobe = 1'b0;
    initial vprobe = $test$plusargs("VDCPROBE");
    reg zl_q = 1'b0; reg rw_q = 1'b0;
    integer vp_n = 0;
    time vp_from = 0; integer vp_fromus;
    initial if ($value$plusargs("VPFROM=%d", vp_fromus)) vp_from = vp_fromus * 1000;     // +VPFROM=<us>: no rw lines before that time (the 64 KiB power-up erase)
    reg [15:0] ua_q = 16'd0; reg [2:0] ca_q = 3'd0; reg [2:0] ra_q = 3'd0;
    always @(posedge clk_r) if (vprobe) begin
        ua_q <= dut.c64.x.vdc.reg_ua; ca_q <= vp_act; ra_q <= dut.c64.x.vdc.ram.ramAction;
        if (dut.c64.x.vdc.reg_ua != ua_q || vp_act != ca_q) $display("vp|%0t|st|ua%04x|ca%0d>%0d|ra%0d|col%0d", $time, dut.c64.x.vdc.reg_ua, ca_q, vp_act, dut.c64.x.vdc.ram.ramAction, dut.c64.x.vdc.ram.col);
        if (dut.c64.x.vdc.enableBus && dut.c64.x.vdc.cs && !dut.c64.x.vdc.we && dut.c64.x.vdc.rs) $display("vp|%0t|cr|r%0d", $time, dut.c64.x.vdc.regSel);
    end
    always #100000000 if (vprobe) begin $display("hb|%0t|vdc busy %b ram_addr %04x act %0d rst %b status %02x", $time, dut.c64.x.vdc.ram.busy, dut.c64.x.vdc.ram.ram_addr, dut.c64.x.vdc.ram.cpuAction, dut.c64.x.vdc.reset, dut.c64.x.vdc.db_out); $fflush; end
    wire [2:0] vp_act = dut.c64.x.vdc.ram.cpuAction;
    wire       vp_cw  = dut.c64.x.vdc.enableBus && dut.c64.x.vdc.cs && dut.c64.x.vdc.we;
    wire       vp_set = vp_cw && (dut.c64.x.vdc.rs ? (dut.c64.x.vdc.regSel == 6'd18 || dut.c64.x.vdc.regSel == 6'd19 || dut.c64.x.vdc.regSel == 6'd30 || dut.c64.x.vdc.regSel == 6'd31) : 1'b0);
    always @(posedge clk_r) if (vprobe) begin
        if (vp_cw) begin
            $display("vp|%0t|cw|rs%b|r%0d|d%02x|act%0d%s|ua%04x|z80 %b", $time, dut.c64.x.vdc.rs, dut.c64.x.vdc.regSel, dut.c64.x.vdc.db_in, vp_act,
                (vp_set && vp_act != 3'd0) ? " DROP-PENDING" : "", dut.c64.x.vdc.reg_ua, dut.c64.z80_act);
            vp_n = vp_n + 1;
        end
        rw_q <= dut.c64.x.vdc.ram.ram_we;
        if (dut.c64.x.vdc.ram.ram_we && !rw_q && $time > vp_from) $display("vp|%0t|rw|a%04x|d%02x", $time, dut.c64.x.vdc.ram.ram_addr, dut.c64.x.vdc.ram.ram_di);
        zl_q <= dut.c64.z80_lat;
        if (dut.c64.z80_lat && dut.c64.z80_we && dut.c64.z80_act && dut.c64.z80_addr[15:8] == 8'hD6) $display("vp|%0t|zw|a%04x|d%02x", $time, dut.c64.z80_addr, dut.c64.z80_dout);
    end
    task snap;
        integer a; reg [31:0] h;
        begin
            h = 32'h811C9DC5;
            for (a = 16'h0200; a < 16'h0400; a = a + 1) h = (h ^ ram_byte({1'b0, a[15:0]})) * 32'h01000193;
            $display("snap cycle %0d hash %08x time %0t", cyc, h, $time);
        end
    endtask
    // ---- timed events and the frame period (T113 5.6) ---------------------------------------------------------------------
    // the VIC's fetches during the machine reset (cpu_reset_req up to 3 us after it): clocks in the window and clocks in which the arbiter selects RAM or
    // colour RAM for the VIC's half-cycle
    reg reset_win = 1'b0;
    integer rs_clk = 0, rs_vsel = 0;
    always @(posedge clk_r) if (reset_win) begin
        rs_clk = rs_clk + 1;
        if (dut.c64.vic_write_ab && (dut.c64.ram_select || dut.c64.ram_color_select || dut.c64.rom_char_select)) rs_vsel = rs_vsel + 1;
    end
    integer keyoff_us = -1, reset_at_us = -1, reset_us = 100, cold_at_us = -1;
    initial begin
        if (!$value$plusargs("KEYOFF=%d", keyoff_us)) keyoff_us = -1;
        if (!$value$plusargs("RESET_AT=%d", reset_at_us)) reset_at_us = -1;
        if (!$value$plusargs("RESET_US=%d", reset_us)) reset_us = 100;
        if (!$value$plusargs("COLD_AT=%d", cold_at_us)) cold_at_us = -1;
    end
    integer shot_us = -1;
    integer vshot_us = -1;
    initial if ($value$plusargs("VSHOT_AT=%d", vshot_us)) begin #(vshot_us * 1000.0); $display("evt| vdc screen at %0t", $time); dump_vdc(); end
    initial if ($value$plusargs("SHOT_AT=%d", shot_us)) begin #(shot_us * 1000.0); $display("evt| screen at %0t", $time); dump_screen(); end
    // T257 (run_ruler.sh). +VKREAD_AT=<us>: at that time the VDC's registers (0-37, 38 status, 39 selected register) are read through the read-back port vk_req /
    // vk_ans, the handshake of c128_regs.v in steps of 10 ns (its 100 MHz clock; no free clock here, it would slow the whole run), a read without an answer in
    // 1023 steps giving $FF as there: first with the machine running
    // ("vk|run|n|byte"), then stopped by halt_req as the menu's C64_STOP stops it ("vk|stop|n|byte", with the 16 VRAM bytes at $4000 and at $8B00 through the
    // same port beside the RAM's own bytes, "vkram|addr|port bytes|RAM bytes"), the machine clock counted while stopped ("vk|stopped, ... machine clocks").
    // A FAIL line for a read without an answer, a register other than the status byte that differs between the two, or a port byte unlike the RAM's.
    integer vk_us = -1, vk_i, vk_t, vk_g = 0, vk_g0 = 0, vk_nbad = 0;              // vk_nbad: the FAIL lines of this part ("vk|n failures")
    reg [7:0] vk_run [0:39];
    reg [7:0] vk_v;
    reg vk_tg, vk_ok;
    reg [8*48-1:0] vk_pb, vk_mb;
    always @(posedge gclk) vk_g <= vk_g + 1;
    task vk_read(input isram, input [15:0] a);
        begin
            #10;
            vk_tg = vk_ans[8];
            vk_req = {~vk_req[17], isram, a};
            vk_t = 0;
            while (vk_ans[8] == vk_tg && vk_t < 1023) begin #10; vk_t = vk_t + 1; end
            vk_ok = vk_ans[8] != vk_tg;
            vk_v = vk_ok ? vk_ans[7:0] : 8'hFF;
            if (!vk_ok) begin $display("FAIL: vk read of %s %04x: no answer in 1023 clocks", isram ? "VRAM" : "register", a); vk_nbad = vk_nbad + 1; end
        end
    endtask
    // the RAM byte a CPU address reaches: vdc_ramiface.sv shuffleAddr with 64 KiB (VDC64), R28 b4 = 0 drops address bit 14 and repeats bit 8
    function [15:0] vk_phys(input [15:0] a);
        vk_phys = dut.c64.x.vdc.reg_ram ? a : {a[15], a[13:8], a[8], a[7:0]};
    endfunction
    task vk_ram16(input [15:0] a);
        integer j;
        begin
            vk_pb = 0; vk_mb = 0;
            for (j = 0; j < 16; j = j + 1) begin
                vk_read(1'b1, a + j[15:0]);
                vk_pb = {vk_pb[8*47-1:0], vk_v};
                vk_mb = {vk_mb[8*47-1:0], dut.c64.x.vdc.ram.ram.m[vk_phys(a + j[15:0])]};
                if (vk_v !== dut.c64.x.vdc.ram.ram.m[vk_phys(a + j[15:0])]) begin $display("FAIL: vk VRAM %04x = %02x, the RAM holds %02x", a + j[15:0], vk_v, dut.c64.x.vdc.ram.ram.m[vk_phys(a + j[15:0])]); vk_nbad = vk_nbad + 1; end
            end
            $display("vkram|%04x|%032x|%032x", a, vk_pb[127:0], vk_mb[127:0]);
        end
    endtask
    initial if ($value$plusargs("VKREAD_AT=%d", vk_us)) begin
        #(vk_us * 1000.0);
        for (vk_i = 0; vk_i < 40; vk_i = vk_i + 1) begin
            vk_read(1'b0, vk_i[15:0]); vk_run[vk_i] = vk_v;
            $display("vk|run|%0d|%02x", vk_i, vk_v);
        end
        pause_t = $realtime; pause_ns = 100000000;                     // the hold: 100 ms (the release is the always block at pause_t)
        halt_req = 1'b1;
        wait (halted);
        #1000;
        vk_g0 = vk_g;
        $display("evt| machine stopped at %0t", $time);
        for (vk_i = 0; vk_i < 40; vk_i = vk_i + 1) begin
            vk_read(1'b0, vk_i[15:0]);
            $display("vk|stop|%0d|%02x", vk_i, vk_v);
            if (vk_i != 38 && vk_v !== vk_run[vk_i]) begin $display("FAIL: R%0d read %02x stopped, %02x running", vk_i, vk_v, vk_run[vk_i]); vk_nbad = vk_nbad + 1; end
        end
        vk_ram16(16'h4000);
        vk_ram16(16'h8B00);
        $display("vk|stopped, %0d machine clocks while the page was read", vk_g - vk_g0);
        if (vk_g != vk_g0) begin $display("FAIL: the machine clock ran while stopped"); vk_nbad = vk_nbad + 1; end
        $display("vk|%0d failures", vk_nbad);
    end
    // +VDCREGLOG: every CPU write into a VDC register other than the data path R18, R19, R31 ("vreg|us|register|byte"), and at the end ("vdcw|...") the
    // CPU's R31 writes and the writes into R18, R19, R30 or R31 that came while a CPU action of the VDC's RAM was still pending (the VDC replaces it: a
    // program that waits for the ready bit before each access never does that)
    integer vw_n = 0, vw_rep = 0;
    always @(posedge gclk) if (dut.c64.x.vdc.enableBus && dut.c64.x.vdc.cs && dut.c64.x.vdc.we && dut.c64.x.vdc.rs) begin
        if (dut.c64.x.vdc.regSel == 6'd31) vw_n = vw_n + 1;
        if ((dut.c64.x.vdc.regSel == 6'd18 || dut.c64.x.vdc.regSel == 6'd19 || dut.c64.x.vdc.regSel == 6'd30 || dut.c64.x.vdc.regSel == 6'd31)
            && dut.c64.x.vdc.ram.cpuAction != 3'd0) vw_rep = vw_rep + 1;
        if ($test$plusargs("VDCREGLOG") && dut.c64.x.vdc.regSel != 6'd18 && dut.c64.x.vdc.regSel != 6'd19 && dut.c64.x.vdc.regSel != 6'd31)
            $display("vreg|%0d|%0d|%02x", $time / 1000, dut.c64.x.vdc.regSel, dut.c64.x.vdc.db_in);
    end
    // +VDCFIELDS=<us>: the next four fields of the VDC's picture after that time, one hex digit (rgbi) per dot of the active area, into vdcfield<k>.txt
    // (first line "field <k> il <vdc_il> lines <n> width <w>"); vdcil_model.py fields renders them. il is taken where the field's visible rows start, the
    // edge at which c128_pixw takes il and field too (c128_pixw.v: fs = vb_d & ~vblank). The VDC flips its field at the frame wrap, in the blank after the
    // visible rows (vdc_signals_v.sv cfield, vVisible), so at the blank's start it still names the field just shown (T257 fault 4: taken there, every
    // field carried the flag of the one before it).
    integer vf_us = -1, vf_k = 0, vf_x = 0, vf_y = 0, vf_w = 0, vf_fd = 0, vf_i, vf_j;
    reg [1:0] vf_st = 2'd0;                                            // 0 off, 1 waiting for a field start, 2 capturing
    reg vf_hb_q = 1'b1, vf_vb_q = 1'b1;
    reg [1:0] vf_il = 2'd0;
    reg [3:0] vf_px [0:299][0:799];
    reg [8*16-1:0] vf_name;
    initial if ($value$plusargs("VDCFIELDS=%d", vf_us)) begin #(vf_us * 1000.0); vf_st = 2'd1; end
    always @(posedge gclk) begin
        vf_hb_q <= vdc_hb; vf_vb_q <= vdc_vb;
        if (vf_st == 2'd2 && vdc_ce && !vdc_hb && !vdc_vb && vf_y < 300 && vf_x < 800) begin vf_px[vf_y][vf_x] = vdc_rgbi; vf_x = vf_x + 1; end
        if (vdc_hb && !vf_hb_q) begin if (vf_x > 0) begin if (vf_x > vf_w) vf_w = vf_x; vf_y = vf_y + 1; end vf_x = 0; end
        if (!vdc_vb && vf_vb_q) vf_il = vdc_il;
        if (vdc_vb && !vf_vb_q) begin
            if (vf_st == 2'd2 && vf_y > 0) begin
                $sformat(vf_name, "vdcfield%0d.txt", vf_k);
                vf_fd = $fopen(vf_name, "w");
                $fwrite(vf_fd, "field %0d il %0d lines %0d width %0d\n", vf_k, vf_il, vf_y, vf_w);
                for (vf_i = 0; vf_i < vf_y; vf_i = vf_i + 1) begin
                    for (vf_j = 0; vf_j < vf_w; vf_j = vf_j + 1) $fwrite(vf_fd, "%h", vf_px[vf_i][vf_j]);
                    $fwrite(vf_fd, "\n");
                end
                $fclose(vf_fd);
                $display("vdcfield|%0d|il %0d|%0d lines|%0d dots", vf_k, vf_il, vf_y, vf_w);
                vf_k = vf_k + 1;
                if (vf_k == 4) vf_st = 2'd0;
            end
            if (vf_st == 2'd1) vf_st = 2'd2;
            vf_y = 0; vf_x = 0; vf_w = 0;
        end
    end
    // +INJECT_AT=<us>: at that time the RAM image mem/inject.mem (byte per line, address = {block, 16 bit}) is written wherever mem/inject.msk is 1: into the DDR2 model (the
    // truth), into the pinned slots of block 0 and the CPU's line cache is emptied (c128-vicglitch: a game as its loader leaves it, tools see sim/vicglitch/mk_inject.py)
    reg [7:0] inj_img [0:131071];
    reg [7:0] inj_msk [0:131071];
    integer inj_us = -1, inj_i, inj_n;
    reg [16:0] inj_a;
    initial if ($value$plusargs("INJECT_AT=%d", inj_us)) begin
        $readmemh("mem/inject.mem", inj_img);
        $readmemh("mem/inject.msk", inj_msk);
        #(inj_us * 1000.0);
        inj_n = 0;
        for (inj_i = 0; inj_i < 131072; inj_i = inj_i + 1) if (inj_msk[inj_i] == 8'd1) begin
            inj_a = inj_i[16:0];
`ifdef RAM_DDR2
            ddr.mem[8192 + inj_a[16:4]][8 * inj_a[3:0] +: 8] = inj_img[inj_i];
            if (!inj_a[16]) begin
                if (inj_a[15:14] == 2'd0) begin if (inj_a[0]) dut.mem.ramd.gb[0].hi[inj_a[13:1]] = inj_img[inj_i]; else dut.mem.ramd.gb[0].lo[inj_a[13:1]] = inj_img[inj_i]; end
                if (inj_a[15:14] == 2'd1) begin if (inj_a[0]) dut.mem.ramd.gb[1].hi[inj_a[13:1]] = inj_img[inj_i]; else dut.mem.ramd.gb[1].lo[inj_a[13:1]] = inj_img[inj_i]; end
                if (inj_a[15:14] == 2'd2) begin if (inj_a[0]) dut.mem.ramd.gb[2].hi[inj_a[13:1]] = inj_img[inj_i]; else dut.mem.ramd.gb[2].lo[inj_a[13:1]] = inj_img[inj_i]; end
                if (inj_a[15:14] == 2'd3) begin if (inj_a[0]) dut.mem.ramd.gb[3].hi[inj_a[13:1]] = inj_img[inj_i]; else dut.mem.ramd.gb[3].lo[inj_a[13:1]] = inj_img[inj_i]; end
            end
`else
            if (inj_a[0]) dut.mem.ram_hi[{inj_a[16], inj_a[15:1]}] = inj_img[inj_i]; else dut.mem.ram_lo[{inj_a[16], inj_a[15:1]}] = inj_img[inj_i];
`endif
            inj_n = inj_n + 1;
        end
`ifdef RAM_DDR2
        for (inj_i = 0; inj_i < 512; inj_i = inj_i + 1) dut.mem.ramd.cm[inj_i][132] = 1'b0;
`endif
        $display("evt| injected %0d bytes at %0t", inj_n, $time);
    end
    // +PRG_AT=<us> (T217, run_prgrun.sh): at that time mem/prgscript.txt is replayed - what the application does to start a program (tools/cbm_prg_sim.cc): 2 addr byte
    // writes into RAM block 0 (the RAM window; the same places as +INJECT_AT: the DDR2 model, the pinned slots, the CPU's line cache emptied), 4 ms hex64 the key matrix
    // (T217 D4, the C128's start of a C64 program: 5 ms = wait, 6 n hex = wait until the 40-column screen holds those screen codes, 7 addr value = wait until that RAM byte holds the value)
    integer pg_us, pg_fd, pg_n, pg_ms, pg_prev, pg_nr = 0, pg_nk = 0;
    reg [8*80-1:0] pg_line;
    integer pg_t;
    reg [31:0] pg_a;
    reg [7:0]  pg_b;
    reg [63:0] pg_k;
    reg [16:0] pg_x;
    reg [127:0] pg_w;
    integer pg_len, pg_pos, pg_j, pg_waited;
    reg pg_hit;
`ifdef RAM_DDR2
    function [7:0] pg_rd(input [15:0] a);                 // RAM block 0 as the CPU sees it (the pinned slots)
        case (a[15:14])
            2'd0: pg_rd = a[0] ? dut.mem.ramd.gb[0].hi[a[13:1]] : dut.mem.ramd.gb[0].lo[a[13:1]];
            2'd1: pg_rd = a[0] ? dut.mem.ramd.gb[1].hi[a[13:1]] : dut.mem.ramd.gb[1].lo[a[13:1]];
            2'd2: pg_rd = a[0] ? dut.mem.ramd.gb[2].hi[a[13:1]] : dut.mem.ramd.gb[2].lo[a[13:1]];
            default: pg_rd = a[0] ? dut.mem.ramd.gb[3].hi[a[13:1]] : dut.mem.ramd.gb[3].lo[a[13:1]];
        endcase
    endfunction
`endif
    initial if ($value$plusargs("PRG_AT=%d", pg_us)) begin
        #(pg_us * 1000.0);
        pg_fd = $fopen("mem/prgscript.txt", "r");
        pg_prev = 0;
        while (pg_fd != 0 && !$feof(pg_fd)) begin
            pg_n = $fgets(pg_line, pg_fd);
            if (pg_n != 0) begin
                pg_n = $sscanf(pg_line, "%d", pg_t);
                if (pg_t == 1 || pg_t == 2) pg_n = $sscanf(pg_line, "%d %h %h", pg_t, pg_a, pg_b);
                if (pg_t == 2) begin
                    pg_x = {1'b0, pg_a[15:0]};
`ifdef RAM_DDR2
                    ddr.mem[8192 + pg_x[16:4]][8 * pg_x[3:0] +: 8] = pg_b;
                    if (pg_x[15:14] == 2'd0) begin if (pg_x[0]) dut.mem.ramd.gb[0].hi[pg_x[13:1]] = pg_b; else dut.mem.ramd.gb[0].lo[pg_x[13:1]] = pg_b; end
                    if (pg_x[15:14] == 2'd1) begin if (pg_x[0]) dut.mem.ramd.gb[1].hi[pg_x[13:1]] = pg_b; else dut.mem.ramd.gb[1].lo[pg_x[13:1]] = pg_b; end
                    if (pg_x[15:14] == 2'd2) begin if (pg_x[0]) dut.mem.ramd.gb[2].hi[pg_x[13:1]] = pg_b; else dut.mem.ramd.gb[2].lo[pg_x[13:1]] = pg_b; end
                    if (pg_x[15:14] == 2'd3) begin if (pg_x[0]) dut.mem.ramd.gb[3].hi[pg_x[13:1]] = pg_b; else dut.mem.ramd.gb[3].lo[pg_x[13:1]] = pg_b; end
`else
                    if (pg_x[0]) dut.mem.ram_hi[{pg_x[16], pg_x[15:1]}] = pg_b; else dut.mem.ram_lo[{pg_x[16], pg_x[15:1]}] = pg_b;
`endif
                    pg_nr = pg_nr + 1;
                end else if (pg_t == 5) begin
                    pg_n = $sscanf(pg_line, "%d %d", pg_t, pg_ms);       // 5 ms: wait that many ms from here (the next writes are a later step of the application)
`ifdef RAM_DDR2
                    for (pg_n = 0; pg_n < 512; pg_n = pg_n + 1) dut.mem.ramd.cm[pg_n][132] = 1'b0;
`endif
                    #(pg_ms * 1000000.0);
                end else if (pg_t == 6) begin
`ifdef RAM_DDR2
                    // 6 <n> <hex of n bytes>: wait until the 40-column screen ($0400-$07E7) holds those screen codes (the application waits for the same text on the board), 20 s at most
                    pg_n = $sscanf(pg_line, "%d %d %h", pg_t, pg_len, pg_w);
                    pg_hit = 1'b0; pg_waited = 0;
                    while (!pg_hit && pg_waited < 4000) begin
                        for (pg_pos = 0; pg_pos <= 1000 - pg_len; pg_pos = pg_pos + 1) begin
                            pg_n = 1;
                            for (pg_j = 0; pg_j < pg_len; pg_j = pg_j + 1) if (pg_rd(16'h0400 + pg_pos + pg_j) != pg_w[8 * (pg_len - 1 - pg_j) +: 8]) pg_n = 0;
                            if (pg_n) pg_hit = 1'b1;
                        end
                        if (!pg_hit) begin #5000000.0; pg_waited = pg_waited + 1; end
                    end
                    $display("prg|screen wait (%0d bytes): %s after %0d ms of polling", pg_len, pg_hit ? "found" : "TIMEOUT", pg_waited * 5);
`endif
                end else if (pg_t == 7) begin
`ifdef RAM_DDR2
                    // 7 <addr> <value>: wait until that RAM byte holds the value (what the application polls: TXTTAB, the keyboard count), 20 s at most
                    pg_n = $sscanf(pg_line, "%d %h %h", pg_t, pg_a, pg_b);
                    pg_waited = 0;
                    while (pg_rd(pg_a[15:0]) !== pg_b && pg_waited < 4000) begin #5000000.0; pg_waited = pg_waited + 1; end
                    $display("prg|byte wait $%04h = %02h: %s after %0d ms of polling", pg_a[15:0], pg_b, pg_rd(pg_a[15:0]) === pg_b ? "found" : "TIMEOUT", pg_waited * 5);
`endif
                end else if (pg_t == 4) begin
                    pg_n = $sscanf(pg_line, "%d %d %h", pg_t, pg_ms, pg_k);
                    #((pg_ms - pg_prev) * 1000000.0);
                    pg_prev = pg_ms;
                    keys = pg_k;
                    pg_nk = pg_nk + 1;
                end
            end
        end
`ifdef RAM_DDR2
        for (pg_n = 0; pg_n < 512; pg_n = pg_n + 1) dut.mem.ramd.cm[pg_n][132] = 1'b0;
`endif
        $display("prg|%0d RAM-window bytes, %0d key states replayed", pg_nr, pg_nk);
    end
    initial if (keyoff_us >= 0) begin #(keyoff_us * 1000.0); keys = 64'd0; $display("evt| keys released at %0t", $time); end
    initial if (reset_at_us >= 0) begin
        #(reset_at_us * 1000.0); cpu_reset_req = 1'b1; reset_win = 1'b1; $display("evt| reset on at %0t, raster line %0d x %0d", $time, dut.c64.vic_pix_y, dut.c64.vic_pix_x);
        #(reset_us * 1000.0); cpu_reset_req = 1'b0; $display("evt| reset off at %0t", $time);
        #3000.0; reset_win = 1'b0;
    end
    // the VIC's own frame: the raster line (vic_pix_y) falling back to a small value; fr_clk counts the free clock, so a machine held for the ROM
    // (vic_stall) or by the gate lengthens the frame in real time. The last column is the frame start modulo the nominal frame, counted from the
    // third frame (the phase the host scaler would have locked to).
    initial if (cold_at_us >= 0) begin
        #(cold_at_us * 1000.0); cold_req_tgl = ~cold_req_tgl; $display("evt| cold start requested at %0t, raster line %0d x %0d", $time, dut.c64.vic_pix_y, dut.c64.vic_pix_x);
        #20000.0; while (cold_busy) #1000.0;
        $display("evt| cold start done at %0t, raster line %0d x %0d", $time, dut.c64.vic_pix_y, dut.c64.vic_pix_x);
    end
    // the character ROM store against the image (T113 5.7): whenever the arbiter has selected the character ROM with the same address for 3 clocks, the
    // byte the store gives is the image's byte (0x10000 + {set, address}); a count of the checks and of the wrong bytes
    reg [15:0] cimg_tb [0:36863];
    initial $readmemh("mem/c128_rom.mem", cimg_tb);
    reg [12:0] ck_a1 = 0, ck_a2 = 0, ck_a0;
    reg        ck_s1 = 0, ck_s2 = 0;
    integer    ck_n = 0, ck_bad = 0;
    reg  [7:0] ck_exp;
    reg [16:0] ck_ix;
    always @(posedge clk_r) begin
        ck_a0 = {!dut.c64.c128_n, dut.c64.rom_char_addr};
        if (ck_s1 && ck_s2 && dut.c64.rom_char_select && ck_a0 == ck_a1 && ck_a1 == ck_a2) begin
            ck_ix  = 17'h10000 + ck_a0;
            ck_exp = ck_ix[0] ? cimg_tb[ck_ix[16:1]][15:8] : cimg_tb[ck_ix[16:1]][7:0];
            ck_n = ck_n + 1;
            if (dut.c64.rom_char_dout !== ck_exp) begin
                ck_bad = ck_bad + 1;
                if (ck_bad < 6) $display("CHRBAD %t set/addr %h got %h want %h", $time, ck_a0, dut.c64.rom_char_dout, ck_exp);
            end
        end
        ck_s2 <= ck_s1; ck_s1 <= dut.c64.rom_char_select;
        ck_a2 <= ck_a1; ck_a1 <= ck_a0;
    end
    // a hash of the VIC's pixel stream over one frame
    integer ph_frame = -1;
    reg [31:0] ph_h = 32'h811C9DC5;
    initial if (!$value$plusargs("PIXHASH=%d", ph_frame)) ph_frame = -1;
    reg [31:0] ph_m;
    // +PIXSKIP_LO/_HI and +PIXSKIP2_LO/_HI: lines lo..hi-1 are left out of the hash (a poll loop's bank switch lands on a cycle of its line that depends on how many cycles
    // the CPU lost before); +PIXLINES: also one hash per line ("pixline|y|hash"), to see where two runs differ
    integer ps_lo = 0, ps_hi = 0, ps_lo2 = 0, ps_hi2 = 0, ph_lines = 0;
    initial if (!$value$plusargs("PIXSKIP_LO=%d", ps_lo)) ps_lo = 0;
    initial if (!$value$plusargs("PIXSKIP_HI=%d", ps_hi)) ps_hi = 0;
    initial if (!$value$plusargs("PIXSKIP2_LO=%d", ps_lo2)) ps_lo2 = 0;
    initial if (!$value$plusargs("PIXSKIP2_HI=%d", ps_hi2)) ps_hi2 = 0;
    initial if (!$value$plusargs("PIXLINES=%d", ph_lines)) ph_lines = 0;
    reg [31:0] pl_h [0:511];
    integer pli;
    initial for (pli = 0; pli < 512; pli = pli + 1) pl_h[pli] = 32'h811C9DC5;
    wire ph_skip = (pix_y >= ps_lo && pix_y < ps_hi) || (pix_y >= ps_lo2 && pix_y < ps_hi2);
    reg [31:0] pl_m;
    always @(posedge clk_r) if (ph_frame >= 0 && fr_n == ph_frame) begin
        ph_m = (ph_h ^ {pix_color, pix_y[8:0], pix_x[9:0], 9'd0}) * 32'h01000193;
        if (!ph_skip) ph_h <= ph_m ^ (ph_m >> 13);      // the multiplication only carries upwards: fold the top back down
        pl_m = (pl_h[pix_y[8:0]] ^ {pix_color, pix_x[9:0], 9'd0}) * 32'h01000193;
        pl_h[pix_y[8:0]] <= pl_m ^ (pl_m >> 13);
    end
    integer fr_n = 0, fr_clk = 0, fr_tot = 0, fr_t0 = 0;
    // +PIXDUMP=<n>: every pixel of frames n..n+PIXDUMPN-1 as "frame y x colour" into pixdump.txt (c128-vicglitch: render and compare frames)
    integer pd_frame = -1, pd_fd = 0, pd_n = 1;
    initial if (!$value$plusargs("PIXDUMPN=%d", pd_n)) pd_n = 1;
    initial if (!$value$plusargs("PIXDUMP=%d", pd_frame)) pd_frame = -1;
    initial if (pd_frame >= 0) pd_fd = $fopen("pixdump.txt", "w");
    always @(posedge clk_r) if (pd_frame >= 0 && fr_n >= pd_frame && fr_n < pd_frame + pd_n) $fwrite(pd_fd, "%0d %0d %0d %0d\n", fr_n, pix_y, pix_x, pix_color);
`ifdef RAM_DDR2
    // T140 D25: the VIC's misses at the end of the hashed frame and at the end of the run (no miss may come in between: the banks are resident)
    reg vm_snap = 1'b0;
    integer vm_at = 0;
    always @(posedge clk_r) if (ph_frame >= 0 && fr_n == ph_frame + 1 && !vm_snap) begin vm_snap <= 1'b1; vm_at <= dut.mem.ramd.n_vic_miss; end
`endif
`ifdef RAM_DDR2
    // +VICCHK: every VIC read answered by c128_ramd against the byte pair the DDR2 model holds for that address (the truth). A wrong answer prints
    // frame, raster line, x, RAM block, word address, got, want and the VIC's BA/AEC ("vicbad|..."); the first 12, then counts only ("vicchk|reads|wrong|blk1 reads|blk1 wrong")
    integer vc_n = 0, vc_bad = 0, vc_b1 = 0, vc_b1bad = 0;
    realtime vc_t_b1 = -1.0, vc_t_bad0 = -1.0, vc_t_bad1 = -1.0, vc_t_alloc = -1.0;   // first block-1 read, first and last wrong one, slot taken (T140 D28: the residual window)
    always @(posedge clk_r) if ($test$plusargs("VICCHK") && vc_t_alloc < 0.0 && |(dut.mem.ramd.s_act & ~dut.mem.ramd.PINMASK)) vc_t_alloc = $realtime;
    reg [15:0] vc_want;
    always @(posedge clk_r) if ($test$plusargs("VICCHK") && dut.mem.ramd.r_rd && dut.mem.ramd.r_vic && !dut.mem.ramd.r_port) begin
        vc_want = {ram_byte({dut.mem.ramd.r_wa[15:0], 1'b1}), ram_byte({dut.mem.ramd.r_wa[15:0], 1'b0})};
        vc_n = vc_n + 1;
        if (dut.mem.ramd.r_wa[15]) begin vc_b1 = vc_b1 + 1; if (vc_t_b1 < 0.0) vc_t_b1 = $realtime; end
        if (dut.mem.ramd.rd_word !== vc_want) begin
            vc_bad = vc_bad + 1;
            if (dut.mem.ramd.r_wa[15]) begin vc_b1bad = vc_b1bad + 1; if (vc_t_bad0 < 0.0) vc_t_bad0 = $realtime; vc_t_bad1 = $realtime; end
            if (vc_bad <= 6 || (dut.mem.ramd.r_wa[15] && vc_b1bad <= 8)) $display("vicbad|frame %0d|line %0d x %0d|blk %0d word %04x|got %04x want %04x|s_hit %b valid %b|ba %b aec %b", fr_n, dut.c64.vic_pix_y, dut.c64.vic_pix_x,
                dut.mem.ramd.r_wa[15], dut.mem.ramd.r_wa[14:0], dut.mem.ramd.rd_word, vc_want, dut.mem.ramd.s_hit, dut.mem.ramd.rd_v, dut.c64.vic_ba, dut.c64.vic_aec);
        end
    end
    final begin
        $display("vicchk|%0d|%0d|%0d|%0d", vc_n, vc_bad, vc_b1, vc_b1bad);
        // times in us of simulated time: slot 4 taken, first block-1 VIC read, first and last wrong block-1 read, the wrong reads' window
        $display("vicwin|slot taken %0.1f|first blk1 read %0.1f|first wrong %0.1f|last wrong %0.1f|window %0.1f us", vc_t_alloc / 1000.0, vc_t_b1 / 1000.0, vc_t_bad0 / 1000.0, vc_t_bad1 / 1000.0, (vc_t_bad0 < 0.0) ? 0.0 : (vc_t_bad1 - vc_t_bad0) / 1000.0);
    end

    // +VICEVT (T140 D30, bench only): the events of the dynamic slot 4. "vtag" = the VIC's bank selection changed (stable 4 clocks), "take" = slot 4 re-taken (tag old->new),
    // "full" = its sweep is complete (time since the take), "evsum" = per take: the wrong VIC reads until the next take, by class (c-access = cycle types HRC/HGC/HGI/HRX,
    // g-access = LG, other), the badline flag of the wrong reads, the lines and the span of the wrong reads, "evbad" = the first wrong reads of an event.
    reg        ve_on = 1'b0;
    initial ve_on = $test$plusargs("VICEVT");
    integer    ve_n = 0, ve_w = 0, ve_wc = 0, ve_wg = 0, ve_wo = 0, ve_wbl = 0, ve_wcbl = 0, ve_l0 = -1, ve_l1 = -1, ve_f0 = -1, ve_f1 = -1, ve_pr = 0, ve_wb0 = 0, ve_nl = 0, ve_lastl = -1, ve_lastf = -1, ve_rd = 0;
    realtime   ve_t_take = -1.0, ve_t_w0 = -1.0, ve_t_w1 = -1.0;
    reg        ve_full_done = 1'b1, ve_pact = 1'b0;
    reg  [2:0] ve_ptag = 3'd0, ve_vt = 3'd0;
    reg [63:0] ve_ppv = 64'd0;
    realtime   ve_t_prev_take = -1.0;
    wire [3:0] ve_ct = dut.c64.video_kawari_0.vic_inst.cycle_type;
    wire       ve_bl = dut.c64.video_kawari_0.vic_inst.badline;
    wire       ve_cacc = ve_ct == 4'd10 || ve_ct == 4'd11 || ve_ct == 4'd12 || ve_ct == 4'd15;
    task ve_flush;
        begin
            if (ve_n > 0) $display("evsum|take %0d|tag %b|%0.1f us after previous take|vic reads in event %0d|wrong blk1 %0d (c %0d g %0d other %0d)|on badline %0d (c-access %0d)|wrong blk0 %0d|distinct lines %0d|frames %0d..%0d|lines %0d..%0d|first %0.1f last %0.1f us (span %0.1f us)",
                ve_n, ve_ptag, (ve_t_prev_take < 0.0) ? 0.0 : (ve_t_take - ve_t_prev_take) / 1000.0, ve_rd, ve_w, ve_wc, ve_wg, ve_wo, ve_wbl, ve_wcbl, ve_wb0, ve_nl, ve_f0, ve_f1, ve_l0, ve_l1,
                ve_t_w0 / 1000.0, ve_t_w1 / 1000.0, (ve_t_w0 < 0.0) ? 0.0 : (ve_t_w1 - ve_t_w0) / 1000.0);
            ve_w = 0; ve_wc = 0; ve_wg = 0; ve_wo = 0; ve_wbl = 0; ve_wcbl = 0; ve_wb0 = 0; ve_nl = 0; ve_l0 = -1; ve_l1 = -1; ve_f0 = -1; ve_f1 = -1; ve_pr = 0; ve_lastl = -1; ve_lastf = -1; ve_rd = 0;
            ve_t_w0 = -1.0; ve_t_w1 = -1.0;
        end
    endtask
    always @(posedge clk_r) if (ve_on) begin
        if (dut.mem.ramd.vt_stable && dut.mem_vic_tag != ve_vt) begin
            $display("vtag|%0.1f us|frame %0d line %0d|%b -> %b|slot4 act %b tag %b", $realtime / 1000.0, fr_n, dut.c64.vic_pix_y, ve_vt, dut.mem_vic_tag, dut.mem.ramd.s_act[4], dut.mem.ramd.s_tag[4]);
            ve_vt = dut.mem_vic_tag;
        end
        if ((dut.mem.ramd.s_act[4] && !ve_pact) || (dut.mem.ramd.s_tag[4] != ve_ptag) || (dut.mem.ramd.s_pv[4] == 64'd0 && ve_ppv != 64'd0)) begin
            ve_flush;
            ve_t_prev_take = ve_t_take; ve_t_take = $realtime; ve_n = ve_n + 1; ve_full_done = 1'b0;
            $display("take|%0d|%0.1f us|frame %0d line %0d x %0d|slot4 tag %b -> %b|vic tag %b", ve_n, $realtime / 1000.0, fr_n, dut.c64.vic_pix_y, dut.c64.vic_pix_x, ve_ptag, dut.mem.ramd.s_tag[4], dut.mem_vic_tag);
        end
        ve_pact = dut.mem.ramd.s_act[4]; ve_ptag = dut.mem.ramd.s_tag[4]; ve_ppv = dut.mem.ramd.s_pv[4];
        if (!ve_full_done && dut.mem.ramd.s_act[4] && (&dut.mem.ramd.s_pv[4])) begin
            ve_full_done = 1'b1;
            $display("full|take %0d|sweep complete %0.1f us after the take (%0.2f frames)", ve_n, ($realtime - ve_t_take) / 1000.0, ($realtime - ve_t_take) / 19656000.0);
        end
        if (dut.mem.ramd.r_rd && dut.mem.ramd.r_vic && !dut.mem.ramd.r_port) begin
            if (dut.mem.ramd.r_wa[15]) ve_rd = ve_rd + 1;
            if (dut.mem.ramd.rd_word !== {ram_byte({dut.mem.ramd.r_wa[15:0], 1'b1}), ram_byte({dut.mem.ramd.r_wa[15:0], 1'b0})}) begin
                if (!dut.mem.ramd.r_wa[15]) ve_wb0 = ve_wb0 + 1;
                else begin
                    ve_w = ve_w + 1;
                    if (ve_cacc) ve_wc = ve_wc + 1; else if (ve_ct == 4'd4) ve_wg = ve_wg + 1; else ve_wo = ve_wo + 1;
                    if (ve_bl) begin ve_wbl = ve_wbl + 1; if (ve_cacc) ve_wcbl = ve_wcbl + 1; end
                    if (ve_l0 < 0 || dut.c64.vic_pix_y < ve_l0) ve_l0 = dut.c64.vic_pix_y;
                    if (dut.c64.vic_pix_y > ve_l1) ve_l1 = dut.c64.vic_pix_y;
                    if (ve_f0 < 0) ve_f0 = fr_n; ve_f1 = fr_n;
                    if (ve_lastl != dut.c64.vic_pix_y || ve_lastf != fr_n) begin ve_nl = ve_nl + 1; ve_lastl = dut.c64.vic_pix_y; ve_lastf = fr_n; end
                    if (ve_t_w0 < 0.0) ve_t_w0 = $realtime; ve_t_w1 = $realtime;
                    if (ve_pr < 6) begin ve_pr = ve_pr + 1; $display("evbad|take %0d|%0.1f us|frame %0d line %0d x %0d|word %04x|ctype %0d badline %b", ve_n, $realtime / 1000.0, fr_n, dut.c64.vic_pix_y, dut.c64.vic_pix_x, dut.mem.ramd.r_wa[14:0], ve_ct, ve_bl); end
                end
            end
        end
    end
    final if (ve_on) begin ve_flush; $display("vicevt|takes %0d|end %0.1f us|frame %0d", ve_n, $realtime / 1000.0, fr_n); end
`endif
    // +FRAMEHASH: one hash per VIC frame over the pixels (colour, x, y), taken when the pixel position changes - a held machine (stalls) repeats a pixel for more clocks
    // but must not change the picture ("fh|frame|hash")
    // +PROBE=<ms>: every <ms> of simulated time the CPU's bus address and the bank the VIC addresses ({RAM block, VA15, VA14}) ("probe|time|addr|vic_tag"), and the last 24 CPU
    // bus cycles at the end ("last|...")
    integer probe_ms = 0, lc_i = 0, lc_k;
    reg [25:0] lc_ring [0:23];
    initial if ($value$plusargs("PROBE=%d", probe_ms) && probe_ms > 0) forever begin #(probe_ms * 1000000.0); $display("probe|%0t|%04x|%b", $time, b_addr, dut.mem_vic_tag); end
    always @(posedge gclk) if (cpu_end && !rst && probe_ms > 0) begin lc_ring[lc_i] <= {z80_off, b_we, b_addr, (b_we ? b_dout : b_din)}; lc_i <= (lc_i == 23) ? 0 : lc_i + 1; end
    final if (probe_ms > 0) for (lc_k = 0; lc_k < 24; lc_k = lc_k + 1) $display("last|%s a=%04x %s d=%02x", lc_ring[(lc_i + lc_k) % 24][25] ? "8502" : "Z80 ", lc_ring[(lc_i + lc_k) % 24][23:8],
        lc_ring[(lc_i + lc_k) % 24][24] ? "W" : "R", lc_ring[(lc_i + lc_k) % 24][7:0]);
    integer fh_last = 0;
    reg [31:0] fh_h = 32'h811C9DC5, fh_m;
    reg [18:0] fh_pos_q = 19'h7FFFF;
    always @(posedge clk_r) if ($test$plusargs("FRAMEHASH")) begin
        if (fr_n != fh_last) begin $display("fh|%0d|%08x", fh_last, fh_h); fh_h = 32'h811C9DC5; fh_last = fr_n; end
        if ({pix_y, pix_x} != fh_pos_q) begin
            fh_m = (fh_h ^ {pix_color, pix_y[8:0], pix_x[9:0], 9'd0}) * 32'h01000193;
            fh_h = fh_m ^ (fh_m >> 13);
            fh_pos_q = {pix_y, pix_x};
        end
    end
    // +VICBUS (issue #19): every byte the VIC latches from the bus (bus_access: phi_phase_start[DATA_DAV = 0], AEC low; c-, g-, p- and s-accesses, not refresh or
    // idle) of a RAM access, against a reference memory the bench keeps from every RAM write command (mem_cmd_*, byte lanes): bytes never written are not checked.
    // For a c-access also the colour nibble against the colour RAM. Unlike +VICCHK (the word c128_ramd answered) this sees what the arbiter's data bus mux gives the
    // VIC. "vicbus|bad|..." for the first 12 wrong ones (frame, line, x, cycle type, RAM block and address, got, want, the CPU's bus address), "vicbus|checked|wrong|wrong colour".
    reg  [7:0] vb_mem [0:131071];
    reg        vb_known [0:131071];
    integer    vb_i, vb_n = 0, vb_bad = 0, vb_cbad = 0;
    initial for (vb_i = 0; vb_i < 131072; vb_i = vb_i + 1) vb_known[vb_i] = 1'b0;
    wire [20:0] vb_wa = dut.c64.mem_cmd_addr[20:0];
    always @(posedge gclk) if ($test$plusargs("VICBUS") && dut.c64.mem_cmd_valid && dut.c64.mem_cmd_we && vb_wa[20:16] == 5'd0) begin
        if (dut.c64.mem_wdata_we[0]) begin vb_mem[{vb_wa[15:0], 1'b0}] <= dut.c64.mem_wdata[7:0];  vb_known[{vb_wa[15:0], 1'b0}] <= 1'b1; end
        if (dut.c64.mem_wdata_we[1]) begin vb_mem[{vb_wa[15:0], 1'b1}] <= dut.c64.mem_wdata[15:8]; vb_known[{vb_wa[15:0], 1'b1}] <= 1'b1; end
    end
    wire  [3:0] vb_ct  = dut.c64.video_kawari_0.vic_inst.cycle_type;
    wire        vb_lat = vb_ct == 4'd0 || vb_ct == 4'd2 || vb_ct == 4'd4 || vb_ct == 4'd5 || vb_ct == 4'd8 || vb_ct == 4'd10 || vb_ct == 4'd11;
    wire        vb_cac = vb_ct == 4'd10 || vb_ct == 4'd11;
    wire [16:0] vb_a   = {dut.c64.c64_bus_arbiter_0.ram_bank, dut.c64.c64_bus_arbiter_0.vic_ram_addr};
    wire  [7:0] vb_got = dut.c64.c64_bus_arbiter_0.vic_dbi;
    wire  [3:0] vb_gotc = dut.c64.c64_bus_arbiter_0.vic_dbh;
    wire  [3:0] vb_wantc = dut.c64.color_mem[{dut.c64.c64_bus_arbiter_0.ram_color_a10, dut.c64.c64_bus_arbiter_0.ram_color_addr}];
    always @(posedge clk_r) if ($test$plusargs("VICBUS") && !rst && dut.c64.video_kawari_0.vic_inst.phi_phase_start[0] && !dut.c64.video_kawari_0.vic_inst.aec && vb_lat
                                && dut.c64.c64_bus_arbiter_0.ram_vic && dut.c64.c64_bus_arbiter_0.ram_select && vb_known[vb_a]) begin
        vb_n = vb_n + 1;
        if (vb_got !== vb_mem[vb_a] || (vb_cac && vb_gotc !== vb_wantc)) begin
            if (vb_got !== vb_mem[vb_a]) vb_bad = vb_bad + 1; else vb_cbad = vb_cbad + 1;
            if (vb_bad + vb_cbad <= 12) $display("vicbus|bad|frame %0d|line %0d x %0d|ctype %0d|blk %0d addr %04x|got %02x want %02x|colour got %x want %x|cpu addr %04x %s", fr_n,
                dut.c64.vic_pix_y, dut.c64.vic_pix_x, vb_ct, vb_a[16], vb_a[15:0], vb_got, vb_mem[vb_a], vb_gotc, vb_wantc, dut.c64.c64_bus_arbiter_0.bus_addr,
                dut.c64.c64_bus_arbiter_0.bus_we ? "W" : "R");
        end
    end
    final if ($test$plusargs("VICBUS")) $display("vicbus|%0d|%0d|%0d", vb_n, vb_bad, vb_cbad);
    integer fr_py_q = 0;
    reg rst_seen = 1'b0;
    always @(posedge clk_r) begin
        fr_clk = fr_clk + 1;
        fr_tot = fr_tot + 1;
        if (cpu_reset_req) rst_seen = 1'b1;
        if (dut.c64.vic_pix_y + 100 < fr_py_q) begin
            if ($test$plusargs("FRAMES")) $display("frame|%0d|%0d|%0d|%0d", fr_n, fr_clk, rst_seen, (fr_tot - fr_t0) % 628992);
            if (fr_n == 3) fr_t0 = fr_tot;
            fr_n = fr_n + 1; fr_clk = 0; rst_seen = 1'b0;
        end
        fr_py_q = dut.c64.vic_pix_y;
    end
    reg     trace = 1'b0;
    realtime tr_from = 0;
    integer tr_from_i;
    // +CTRACE (T257 fault 4): every bus cycle of the 8502 itself, at its core's clock enable (clk2x), so the 2 MHz mode's two cycles per 1 MHz cycle are both
    // there (+TRACE prints one per 1 MHz cycle), from +TRFROM on: "ct|us|address|R/W|byte|ph|fast_hok fast_lcyc|rdy_r and the RDY the core takes|
    // ram miss" (a read's byte is what the core takes; the miss: c128_ramd's n_cpu_wait, the CPU reads held so far)
    reg ctrace = 1'b0;
    initial ctrace = $test$plusargs("CTRACE");
    always @(posedge clk2x_r) if (ctrace && $time >= tr_from && dut.c64.cpu6510_0.core_ce && !dut.c64.z80_act)
`ifdef RAM_DDR2
        $display("ct|%0d|%04x|%s|%02x|ph %0d|hok %b lc %b|rr %b rdy %b|miss %0d", $time / 1000, dut.c64.c8502_addr, dut.c64.c8502_we ? "W" : "R",
            dut.c64.c8502_we ? dut.c64.c8502_dout : dut.c64.cpu6510_0.din_i, dut.c64.ph, dut.c64.fast_hok, dut.c64.fast_lcyc,
            dut.c64.cpu6510_0.rdy_r, dut.c64.cpu6510_0.rdy_latched, dut.mem.ramd.n_cpu_wait);
`else
        $display("ct|%0d|%04x|%s|%02x|ph %0d|hok %b lc %b|rr %b rdy %b", $time / 1000, dut.c64.c8502_addr, dut.c64.c8502_we ? "W" : "R",
            dut.c64.c8502_we ? dut.c64.c8502_dout : dut.c64.cpu6510_0.din_i, dut.c64.ph, dut.c64.fast_hok, dut.c64.fast_lcyc,
            dut.c64.cpu6510_0.rdy_r, dut.c64.cpu6510_0.rdy_latched);
`endif
    initial if ($value$plusargs("TRFROM=%d", tr_from_i)) tr_from = tr_from_i * 1000.0;
    // +KTRACE=<n> (T257 fault 4): n machine clocks from +TRFROM on, one line each, taken in the middle of the clock (the values the next edge takes):
    // the 2 MHz sequencer (ph, fast_*), the 8502's address and the arbiter's copy, who owns the bus (vic_write_ab, BA, AEC), the RAM command and its
    // answer, RDY and the data the 8502 captures (cpu_din, din_r)
    integer ktn = 0;
    initial if (!$value$plusargs("KTRACE=%d", ktn)) ktn = 0;
    always @(negedge clk_r) if (ktn > 0 && $time >= tr_from) begin
        ktn = ktn - 1;
        $write("k|%0d|ph %0d|ca %04x %s|aa %04x|io %b fe %b hok %b lc %b cap %b cex %b|wab %b ba %b aec %b|sp %b rs %b re %b cv %b %06x %s|rh %b rdy %b rr %b|din %02x dr %02x",
            $time, dut.c64.ph, dut.c64.c8502_addr, dut.c64.c8502_we ? "W" : "R", dut.c64.addr_a,
            dut.c64.io_class, dut.c64.fast_en, dut.c64.fast_hok, dut.c64.fast_lcyc, dut.c64.fast_cap, dut.c64.cpu_ce_x,
            dut.c64.vic_write_ab, dut.c64.vic_ba, dut.c64.vic_aec,
            dut.c64.sdram_pending, dut.c64.ram_select, dut.c64.ram_enable, dut.c64.mem_cmd_valid, dut.c64.mem_cmd_addr, dut.c64.mem_cmd_we ? "W" : "R",
            dut.c64.rom_hold, dut.c64.cpu_rdy, dut.c64.cpu6510_0.rdy_r, dut.c64.cpu_din, dut.c64.cpu6510_0.din_r);
`ifdef RAM_DDR2
        $write("|ev %b v %b %04x miss %b", dut.mem.ram_ev, dut.mem.ramd_v, dut.mem.ramd_word, dut.mem.ramd_miss);
`endif
        $display("");
    end
    always @(posedge gclk) if (cpu_end && !rst) begin
        cyc = cyc + 1;
        if (dut.halted) held_cyc = held_cyc + 1;
        if (cyc == pause_cyc) begin halt_req = 1'b1; pause_t = $time; end
        if (cyc == snap_cyc) snap();
        if (cyc % 4000 == 0 && $test$plusargs("DBGR")) $display("dbgr cyc %0d line %0d x %0d stalls %0d stallclk %0d ph %0d", cyc, dut.c64.vic_pix_y, dut.c64.vic_pix_x, st_ep, st_clk, dut.c64.ph);
        if (z80_off) begin n_8502 = n_8502 + 1; if (first_8502 < 0) first_8502 = cyc; end
        else begin n_z80 = n_z80 + 1; last_z80 = cyc; end
        if (trace && $time >= tr_from) $display("cy %0d %s a=%04x %s d=%02x", cyc, z80_off ? "8502" : "Z80 ", b_addr, b_we ? "W" : "R", b_we ? b_dout : b_din);
    end

    // debug: the extra key lines
    always @(dut.c64.x.d02f or dut.c64.ext_pb_pull_x) if ($test$plusargs("DBGX")) $display("dbg %t d02f=%b pull_x=%b xkeys=%h", $time, dut.c64.x.d02f, dut.c64.ext_pb_pull_x, dut.c64.xkeys);
    always @(posedge clk) if ($test$plusargs("DBGV") && !dut.c64.vicie_cen) $display("dbgv %t we=%b a0=%b en=%b p2n=%b stall=%b waddr=%h wab=%b", $time, dut.c64.vicie_we_a, dut.c64.vicie_a0, dut.c64.x.bus_en, dut.c64.vic_phi2_n, dut.c64.vic_stall, dut.c64.addr_a, dut.c64.vic_write_ab);
    integer ph = 0;
    always @(posedge clk) begin
        if (dut.c64.vic_phi2_p) ph <= 0; else ph <= ph + 1;
        if ($test$plusargs("DBGP") && $time > 100000 && $time < 101200)
            $display("ph=%0d pend=%b wab=%b aec=%b ba=%b p2n=%b p2l=%b p2h=%b ras=%b cas=%b ramen=%b", ph, dut.c64.sdram_pending, dut.c64.vic_write_ab, dut.c64.vic_aec, dut.c64.vic_ba, dut.c64.vic_phi2_n, dut.c64.vic_phi2_l, dut.c64.vic_phi2_h, dut.c64.vic_ras, dut.c64.vic_cas, dut.c64.ram_enable);
    end
    always @(posedge gclk) if ($test$plusargs("DBGW") && dut.mem.cmd_valid && dut.mem.cmd_we) $display("dbgw %t addr=%h wdata=%h we=%b z80=%b", $time, dut.mem.cmd_addr, dut.mem.wdata, dut.mem.wdata_we, dut.c64.z80_act);
    // debug window: +DBGFROM=<cpu cycle> +DBGN=<clocks>
    integer dbg_from = -1, dbg_n = 0, dbg_left = 0;
    initial begin if (!$value$plusargs("DBGFROM=%d", dbg_from)) dbg_from = -1; if (!$value$plusargs("DBGN=%d", dbg_n)) dbg_n = 0; end
    always @(posedge gclk) begin
        if (cyc == dbg_from && dbg_left == 0 && dbg_n > 0) dbg_left = dbg_n;
        if (dbg_left > 0) begin
            dbg_left = dbg_left - 1;
            $display("dbgc ph=%0d ba=%b rdy=%b a=%04x din=%02x dinr=%02x p2n=%b stall=%b sel=%b%b%b%b pend=%b cmdv=%b cmdr=%b rv=%b maddr=%h mrd=%h lsb=%b kaddr=%h",
                dut.c64.ph, dut.c64.vic_ba, dut.c64.cpu_rdy, dut.c64.addr_a, dut.c64.cpu_din, dut.c64.din_r_o, dut.c64.vic_phi2_n, dut.c64.vic_stall,
                dut.c64.ram_select, dut.c64.rom_kernal_select, dut.c64.rom_char_select, dut.c64.rom_basic_select, dut.c64.sdram_pending,
                dut.c64.mem_cmd_valid, dut.c64.mem_cmd_ready, dut.c64.mem_rdata_valid, dut.c64.mem_cmd_addr, dut.c64.mem_rdata, dut.c64.mem_raddr_lsb, dut.c64.rom_kernal_addr);
        end
    end
    // +VDCLOG: every write into the VDC's RAM (address, byte)
    always @(posedge clk_r) if ($test$plusargs("VDCLOG") && dut.c64.x.vdc.ram.ram.we) $display("vlog %h %h", dut.c64.x.vdc.ram.ram.addr, dut.c64.x.vdc.ram.ram.dai);
    integer st_ep = 0, st_clk = 0;
    reg stq = 1'b0;
    always @(posedge clk_r) begin
        if (dut.c64.vic_stall) st_clk = st_clk + 1;
        if (dut.c64.vic_stall && !stq) st_ep = st_ep + 1;
        stq <= dut.c64.vic_stall;
    end
    integer dbgs_left = 0;
    always @(posedge clk_r) begin
        if ($test$plusargs("DBGS") && dut.c64.vic_stall && !stq && st_ep > 5 && st_ep < 8) dbgs_left = 40;
        if (dbgs_left > 0) begin
            dbgs_left = dbgs_left - 1;
            $display("dbgs x=%0d y=%0d dot=%b stall=%b ph=%0d p2n=%b p2p=%b rv=%b", dut.c64.vic_pix_x, dut.c64.vic_pix_y, dut.c64.video_kawari_0.vic_inst.dot_rising, dut.c64.vic_stall, dut.c64.ph, dut.c64.vic_phi2_n, dut.c64.vic_phi2_p, dut.c64.mem_rdata_valid);
        end
    end
    function [7:0] ram_byte(input [16:0] a);   // a[16] = block
        reg [15:0] w;
        begin
            w = {a[16], a[15:1]};
`ifdef RAM_DDR2
            ram_byte = ddr.mem[8192 + a[16:4]][8 * a[3:0] +: 8];          // the DDR2 holds the truth once the write FIFO has drained
`else
            ram_byte = a[0] ? dut.mem.ram_hi[w] : dut.mem.ram_lo[w];
`endif
        end
    endfunction

    reg [8*400-1:0] expect_s;
    integer ms = 20, t0 = 0, nbad = 0, k, p;
    reg [63:0] keyv = 64'd0;
    reg [23:0] xkv = 24'd0;
    reg [16:0] ea;
    reg [16:0] dump_a;
    integer dump_n = 6;
    reg  [7:0] ev;
    reg [8*400-1:0] tmp;
    integer pos, len, c, st;
    reg [16:0] acc;
    reg [7:0]  vacc;
    reg        inval;
    initial begin
        if ($test$plusargs("TRACE")) trace = 1'b1;
        if ($test$plusargs("NODEV")) nodev = 1'b1;
        if ($test$plusargs("D80")) x_d4080 = 1'b1;
        if (!$value$plusargs("PAUSE_CYC=%d", pause_cyc)) pause_cyc = -1;
        if (!$value$plusargs("PAUSE_NS=%d", pause_ns)) pause_ns = 0;
        if (!$value$plusargs("SNAPCYC=%d", snap_cyc)) snap_cyc = -1;
        if (!$value$plusargs("MS=%d", ms)) ms = 20;
        if (!$value$plusargs("T0=%d", t0)) t0 = 0;
        if ($value$plusargs("KEYS=%h", keyv)) ;
        if ($value$plusargs("XK=%h", xkv)) ;
        #(t0 * 1000000.0);
        keys = keyv;
        x_keys = xkv;
        #((ms - t0) * 1000000.0);
        $display("cycles: %0d (Z80 %0d, 8502 %0d), first 8502 cycle %0d, last Z80 cycle %0d", cyc, n_z80, n_8502, first_8502, last_z80);
        if (pause_cyc >= 0) $display("held: %0d CPU cycles while the machine was halted", held_cyc);
        if ($value$plusargs("EXPECT=%s", expect_s)) begin
            // "aaaa:vv,aaaa:vv"
            acc = 0; vacc = 0; st = 0; inval = 0;
            for (p = 399; p >= 0; p = p - 1) begin
                c = expect_s[p*8 +: 8];
                if (c != 0) begin
                    if (c == ":") begin st = 1; end
                    else if (c == ",") begin
                        if (ram_byte(acc) !== vacc) begin $display("FAIL: ram[%05x] = %02x, expected %02x", acc, ram_byte(acc), vacc); nbad = nbad + 1; end
                        else $display("ok   ram[%05x] = %02x", acc, vacc);
                        acc = 0; vacc = 0; st = 0;
                    end else begin
                        c = (c >= "a") ? c - "a" + 10 : (c >= "A") ? c - "A" + 10 : c - "0";
                        if (st == 0) acc = {acc[12:0], c[3:0]}; else vacc = {vacc[3:0], c[3:0]};
                    end
                end
            end
            if (st == 1) begin
                if (ram_byte(acc) !== vacc) begin $display("FAIL: ram[%05x] = %02x, expected %02x", acc, ram_byte(acc), vacc); nbad = nbad + 1; end
                else $display("ok   ram[%05x] = %02x", acc, vacc);
            end
        end
        if (!$value$plusargs("DUMPN=%d", dump_n)) dump_n = 6;
        if ($value$plusargs("DUMP=%h", dump_a)) begin
            for (k = 0; k < 16 * dump_n; k = k + 16) begin
                $display("ram|%04x|%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", dump_a + k,
                    ram_byte(dump_a + k), ram_byte(dump_a + k + 1), ram_byte(dump_a + k + 2), ram_byte(dump_a + k + 3), ram_byte(dump_a + k + 4), ram_byte(dump_a + k + 5),
                    ram_byte(dump_a + k + 6), ram_byte(dump_a + k + 7), ram_byte(dump_a + k + 8), ram_byte(dump_a + k + 9), ram_byte(dump_a + k + 10), ram_byte(dump_a + k + 11),
                    ram_byte(dump_a + k + 12), ram_byte(dump_a + k + 13), ram_byte(dump_a + k + 14), ram_byte(dump_a + k + 15));
            end
        end
        if (reset_at_us >= 0) $display("resetwin|%0d|%0d", rs_clk, rs_vsel);
        $display("chrchk|%0d|%0d", ck_n, ck_bad);
        $display("frmeter|%0d|%0d", fr_min, fr_max);
`ifdef RAM_DDR2
        $display("ramd|%0d|%0d|%0d|%0d|%0d|%0d", dut.mem.ramd.n_vic_miss, dut.mem.ramd.n_cpu_wait, dut.mem.ramd.n_fetch, dut.mem.ramd.n_wlost, vm_at, dut.mem.ramd.n_vic_blk1);
`endif
`ifdef ROM_CACHE
        $display("romc|%0d|%0d|%0d|%0d", dut.mem.romc.n_hit, dut.mem.romc.n_miss, dut.mem.romc.n_wait, dut.mem.romc.fill_max);
`endif
        if (ph_frame >= 0) $display("pixhash|%0d|%08x", ph_frame, ph_h);
        if (ph_frame >= 0 && ph_lines) for (pli = 0; pli < 512; pli = pli + 1) $display("pixline|%0d|%08x", pli, pl_h[pli]);
        if ($test$plusargs("SCREEN")) dump_screen();
        if ($test$plusargs("VDCSCREEN")) dump_vdc();
        if ($test$plusargs("VDCREGLOG")) $display("vdcw|%0d R31 writes|%0d writes while an action was pending", vw_n, vw_rep);
        if ($test$plusargs("VRAMALL")) begin : vram_all         // +VRAMALL (T257, run_ruler.sh): the whole VDC RAM, 64 bytes a line ("vramall|addr|bytes")
            integer va, vj;
            for (va = 0; va < 65536; va = va + 64) begin
                $write("vramall|%04x|", va);
                for (vj = 0; vj < 64; vj = vj + 1) $write("%02x", dut.c64.x.vdc.ram.ram.m[va + vj]);
                $write("\n");
            end
        end
        if ($test$plusargs("VRAM")) begin : vram_dump          // +VRAM: 64 bytes of the VDC's RAM at $4000 / $4100 / $4200 ("vram|addr|bytes"), z80vdc
            integer vb, vi;
            for (vb = 16'h4000; vb <= 16'h4300; vb = vb + 16'h100) begin
                $write("vram|%04x|", vb);
                for (vi = 0; vi < 64; vi = vi + 1) $write("%02x", dut.c64.x.vdc.ram.ram.m[vb + vi]);
                $write("\n");
            end
        end
        if (nbad == 0) $display("PASS tb_c128"); else $display("FAIL tb_c128: %0d bytes wrong", nbad);
        $finish;
    end

    // +ACTDBG: per VIC frame the latched c128_act values beside what the bench counted itself (T140 section 18): "actdbg|t ms|seq|vic cram vdc den|raw strobe rising edges vic cram vdc|
    // strobe clocks vic cram vdc|CPU writes seen by the bench at $D000-$D3FF, $D800-$DBFF, $D601". The raw columns are of the frame that has just ended.
    reg        ad_on = 1'b0;
    initial ad_on = $test$plusargs("ACTDBG");
    reg  [2:0] ad_q = 3'd0;
    integer    ad_re [0:2], ad_hi [0:2], ad_gt [0:2], ad_sre [0:2], ad_shi [0:2], ad_sgt [0:2], ad_k;
    reg  [7:0] ad_seq = 8'd0;
    reg        ad_pr = 1'b0;
    initial for (ad_k = 0; ad_k < 3; ad_k = ad_k + 1) begin ad_re[ad_k] = 0; ad_hi[ad_k] = 0; ad_gt[ad_k] = 0; end
    always @(posedge clk_r) if (ad_on) begin
        ad_q <= {dut.c64.act.vdc_wr, dut.c64.act.cram_wr, dut.c64.act.vic_wr};
        if (dut.c64.act.vic_wr)  begin ad_hi[0] = ad_hi[0] + 1; if (!ad_q[0]) ad_re[0] = ad_re[0] + 1; end
        if (dut.c64.act.cram_wr) begin ad_hi[1] = ad_hi[1] + 1; if (!ad_q[1]) ad_re[1] = ad_re[1] + 1; end
        if (dut.c64.act.vdc_wr)  begin ad_hi[2] = ad_hi[2] + 1; if (!ad_q[2]) ad_re[2] = ad_re[2] + 1; end
        if (dut.c64.act.e_fr) begin
            for (ad_k = 0; ad_k < 3; ad_k = ad_k + 1) begin ad_sre[ad_k] = ad_re[ad_k]; ad_shi[ad_k] = ad_hi[ad_k]; ad_sgt[ad_k] = ad_gt[ad_k]; ad_re[ad_k] = 0; ad_hi[ad_k] = 0; ad_gt[ad_k] = 0; end
            ad_pr <= 1'b1;
        end else if (ad_pr) begin
            ad_pr <= 1'b0;
            $display("actdbg|%0.1f ms|seq %0d|vic %0d cram %0d vdc %0d den %0d|edges %0d %0d %0d|clks %0d %0d %0d|cpu %0d %0d %0d", $realtime / 1.0e6, dut.c64.act.l_seq,
                dut.c64.act.l_vic, dut.c64.act.l_cram, dut.c64.act.l_vdc, dut.c64.act.l_den, ad_sre[0], ad_sre[1], ad_sre[2], ad_shi[0], ad_shi[1], ad_shi[2], ad_sgt[0], ad_sgt[1], ad_sgt[2]);
        end
    end
    always @(posedge gclk) if (ad_on && cpu_end && !rst && b_we) begin
        if (b_addr >= 16'hD000 && b_addr <= 16'hD3FF) ad_gt[0] = ad_gt[0] + 1;
        if (b_addr >= 16'hD800 && b_addr <= 16'hDBFF) ad_gt[1] = ad_gt[1] + 1;
        if (b_addr == 16'hD601) ad_gt[2] = ad_gt[2] + 1;
    end
    // +IOLOG (T140 D31, bench only): the CPU's writes to the registers that move the VIC's bank or the slot 4 take: CIA 2 $DD00 (VIC bank = bits 1..0, the other bits only counted),
    // MMU RCR $D506, $D011, $D018, $D030, $D015. "io|time|frame line|reg|old -> new" for the first 24 changes of each, the counts at the end; every change of the VIC bank/RCR/$D011/$D018
    // is logged without limit while the serial device is transferring (dv_on) up to 400 lines.
    reg [7:0] io_last [0:5];
    integer   io_n [0:5], io_w [0:5], io_k, io_pr = 0;
    initial for (io_k = 0; io_k < 6; io_k = io_k + 1) begin io_last[io_k] = 8'hxx; io_n[io_k] = 0; io_w[io_k] = 0; end
    reg [2:0] io_i;
    reg       io_hit, io_on = 1'b0;
    initial io_on = $test$plusargs("IOLOG");
    always @(posedge gclk) if (cpu_end && !rst && b_we && io_on) begin
        io_hit = 1'b1;
        case (b_addr)
            16'hDD00: io_i = 3'd0;
            16'hD506: io_i = 3'd1;
            16'hD011: io_i = 3'd2;
            16'hD018: io_i = 3'd3;
            16'hD030: io_i = 3'd4;
            16'hD015: io_i = 3'd5;
            default: io_hit = 1'b0;
        endcase
        if (io_hit) begin
            io_w[io_i] = io_w[io_i] + 1;
            if ((io_i == 3'd0 ? (io_last[io_i][1:0] !== b_dout[1:0]) : (io_last[io_i] !== b_dout))) begin
                io_n[io_i] = io_n[io_i] + 1;
                if (io_n[io_i] <= 24 || (io_i <= 3'd3 && dv_on && io_pr < 400)) begin
                    if (io_n[io_i] > 24) io_pr = io_pr + 1;
                    $display("io|%0.1f us|frame %0d line %0d x %0d|%s|%02x -> %02x", $realtime / 1000.0, fr_n, dut.c64.vic_pix_y, dut.c64.vic_pix_x,
                        io_i == 3'd0 ? "DD00" : io_i == 3'd1 ? "D506" : io_i == 3'd2 ? "D011" : io_i == 3'd3 ? "D018" : io_i == 3'd4 ? "D030" : "D015", io_last[io_i], b_dout);
                end
            end
            io_last[io_i] = b_dout;
        end
    end
    final if (io_on) $display("iosum|writes DD00 %0d D506 %0d D011 %0d D018 %0d D030 %0d D015 %0d|changes (DD00: bank bits) %0d %0d %0d %0d %0d %0d",
        io_w[0], io_w[1], io_w[2], io_w[3], io_w[4], io_w[5], io_n[0], io_n[1], io_n[2], io_n[3], io_n[4], io_n[5]);

endmodule
