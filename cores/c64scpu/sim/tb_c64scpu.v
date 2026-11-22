// T182 step 1: the whole C64 machine (c64_core_c64scpu, the 6510 still in) in a bench - a boot bench. Cut from cores/c128/sim/tb_c128.v. The system ROM is a
// synthetic image of our own (tools/gen_c64scpu_test.py) that build_machine.sh preloads (mem/c64scpu_rom.mem); no ROM byte of any real machine is involved,
// unless run_real.sh (a local bench, never in CI) puts the image of real C64 ROMs there.
// Plusargs:  +MS=<n>          run n milliseconds of simulated time (default 100)
//            +EXPECT=a:v,..   RAM bytes (hex) that must hold at the end: aaaa:vv
//            +KEYS=<hex64>    c64 key matrix bits held down from the start (64 bit)
//            +SCREEN          at the end print the 40-column screen ($0400) as text ("scr|" lines)
//            +SHOT_AT=<us>    the same at that time ("evt| screen at ...")
//            +PIXDUMP=<n>     write every pixel of the n-th VIC frame ("y x colour") into pixdump.txt: the picture as the VIC made it
//            +SCRCODES        at the end of the +PIXDUMP frame write the 1000 screen codes of $0400 (hex, one per line) into scrcodes.txt
//            +PRG_AT=<us>     T217 (run_prgrun.sh): at that time replay mem/prgscript.txt: what the application does to start a program (tools/cbm_prg_sim.cc): 1 ddr byte
//                             writes into the DDR2 (the SRAM), 2 addr byte writes into the C64 RAM window, 3 1 the CPU cache flush, 4 ms hex64 the key matrix
//            +TRACE           print every CPU bus cycle
//            (always)         "mark|n|cycle" for every write to $0B00+n (n = 0..3): the PHI2 cycle counter at that bus cycle (T182 step 2)
`timescale 1ns / 1ps
module tb_c64scpu;
`include "tb_c64scpu_ports.vh"
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

    // the serial bus: the core's three outputs are line levels (1 = released); one device on it, as a drive is: it pulls DATA low while ATN is asserted
    always @(*) begin
        iec_atn_i  = iec_atn_o;
        iec_clk_i  = iec_clk_o;
        iec_data_i = iec_data_o & iec_atn_o;
    end

    // the DDR2 side (75 MHz ui_clk): c64scpu_ddrx serves the ROM line fetch, the RAM line fetch and the RAM byte write; the DDR2 model holds the ROM image at
    // 0x01C00000 and the RAM at 0x01C20000
    reg ui_clk_r = 1'b0;
    always #6.667 ui_clk_r = ~ui_clk_r;
    wire        x_r_done, x_m_done, x_w_done, x_s_done, x_sw_done, x_owns, x_en, x_rdy, x_rdv;
    wire  [2:0] x_cmd;
    wire [26:0] x_addr;
    wire [127:0] x_fill, x_wdata, x_rd;
    wire [15:0] x_wmask;
    always @(*) begin rom_done_tgl = x_r_done; rom_fill = x_fill; ram_m_done_tgl = x_m_done; ram_w_done_tgl = x_w_done; scpu_s_done_tgl = x_s_done; scpu_sw_done_tgl = x_sw_done; end
    c64scpu_ddrx ddrx (.ui_clk(ui_clk_r), .ui_rst(1'b0),
        .r_req_tgl(rom_req_tgl), .r_req_line(rom_req_line), .r_done_tgl(x_r_done),
        .m_req_tgl(ram_m_req_tgl), .m_req_line(ram_m_req_line), .m_done_tgl(x_m_done),
        .w_req_tgl(ram_w_req_tgl), .w_req_addr(ram_w_req_addr), .w_req_data(ram_w_req_data), .w_done_tgl(x_w_done),
        .s_req_tgl(scpu_s_req_tgl), .s_req_line(scpu_s_req_line), .s_done_tgl(x_s_done),
        .sw_req_tgl(scpu_sw_req_tgl), .sw_req_addr(scpu_sw_req_addr), .sw_req_data(scpu_sw_req_data), .sw_done_tgl(x_sw_done),
        .fill(x_fill), .owns(x_owns), .en(x_en), .cmd(x_cmd), .addr(x_addr), .wdata(x_wdata), .wmask(x_wmask), .rdy(x_rdy), .rd_valid(x_rdv), .rd_data(x_rd));
    ddr2c_model #(.BASE(32'h01C00000), .LAT_MIN(22), .LAT_RND(30), .DEPTH_LOG2(21)) ddr (.ui_clk(ui_clk_r), .c_addr(x_addr), .c_cmd(x_cmd), .c_en(x_en), .c_rdy(x_rdy),
        .c_wdf_data(x_wdata), .c_wdf_mask(x_wmask), .c_wdf_wren(x_en && x_cmd == 3'b000), .c_rd_data(x_rd), .c_rd_valid(x_rdv));
    // the system ROM image (20 KiB: C64 BASIC, KERNAL, character ROM) into the DDR2 model at the ROM base
    reg [15:0] rimg [0:10239];
    integer rn, rk;
    initial begin
        $readmemh("mem/c64scpu_rom.mem", rimg);
        for (rn = 0; rn < 1280; rn = rn + 1)
            for (rk = 0; rk < 8; rk = rk + 1) ddr.mem[rn][16 * rk +: 16] = rimg[rn * 8 + rk];
    end
    // T182 step 3d: the DDR2 model covers 0x01C00000-0x03BFFFFF (32 MiB): the C64 ROM and RAM as before, SuperRAM at 0x02000000, the SRAM at 0x03000000, the SuperCPU ROM at
    // 0x03080000. The SuperCPU ROM image (mem/scpu_rom.mem, one byte per line, 128 KiB, tools/gen_c64scpu_map.py) goes in when the file exists. The application's registers
    // are driven here: ROM_VALID (+ROMVALID=0 holds the CPU), SROM_LOG2 = 17 unless +SROMLOG2 says 16 (64 KiB) or 17 (128 KiB).
    integer sfd, sn, sk, rvp;
    reg [7:0] simg [0:131071];
    initial begin
        sfd = $fopen("mem/scpu_rom.mem", "r");
        if (sfd != 0) begin
            $fclose(sfd);
            $readmemh("mem/scpu_rom.mem", simg);
            for (sn = 0; sn < 8192; sn = sn + 1)
                for (sk = 0; sk < 16; sk = sk + 1) ddr.mem[32'h148000 + sn][8 * sk +: 8] = simg[sn * 16 + sk];
        end
        scpu_srom_log2 = 5'd17;
        if ($value$plusargs("SROMLOG2=%d", rvp)) scpu_srom_log2 = rvp[4:0];     // run_realscpu.sh: 16 for a 64 KiB file, 17 for 128 KiB
        rvp = 1;
        if ($value$plusargs("ROMVALID=%d", rvp)) ;
        scpu_rom_valid = rvp != 0;
        scpu_sw = 2'b01;                        // T10 reset value: 20 MHz, no JiffyDOS
        if ($value$plusargs("SCPUSW=%d", rvp)) scpu_sw = rvp[1:0];
    end
    function [7:0] ddr_byte(input [31:0] a);
        reg [31:0] o;
        begin o = a - 32'h01C00000; ddr_byte = ddr.mem[o >> 4][8 * o[3:0] +: 8]; end
    endfunction
    // every ROM answer of the line cache against the image (the cache must never return a wrong word)
    integer rc_bad = 0;
    reg [15:0] rc_word;
    reg        rc_pend = 1'b0;
    always @(posedge gclk) begin
        if (dut.mem.romc.rvalid && rc_pend) begin
            if (dut.mem.romc.rdata !== rimg[rc_word]) begin
                rc_bad = rc_bad + 1;
                if (rc_bad < 8) $display("ROMBAD %t word %h got %h want %h", $time, rc_word, dut.mem.romc.rdata, rimg[rc_word]);
            end
            rc_pend <= 1'b0;
        end
        if (dut.mem.romc.cmd_valid && dut.mem.romc.cmd_ready) begin rc_word <= dut.mem.romc.cmd_word; rc_pend <= 1'b1; end
    end

    c64_core_c64scpu dut (
`include "tb_c64scpu_inst.vh"
    );

    // ---- what the bench looks at ------------------------------------------------------------------------------------------
    wire        cpu_end = dut.c64.vic_phi2_n && !dut.c64.vic_stall;
    wire [15:0] b_addr  = dut.c64.cpu_addr;
    wire  [7:0] b_din   = dut.c64.cpu_din;
    wire  [7:0] b_dout  = dut.c64.cpu_dout;
    wire        b_we    = dut.c64.cpu_we;
    integer cyc = 0;
    reg [31:0] dmp_a;
    function [7:0] ram_byte(input [15:0] a);
        begin
            ram_byte = ddr.mem[8192 + a[15:4]][8 * a[3:0] +: 8];          // the DDR2 holds the truth once the write FIFO has drained
        end
    endfunction
    // the 40 x 25 screen at $0400 as text (screen codes: 1-26 letters, 32-63 as ASCII)
    task dump_screen;
        integer r, c; reg [7:0] b; reg [8*40-1:0] ln;
        begin
            for (r = 0; r < 25; r = r + 1) begin
                ln = 0;
                for (c = 0; c < 40; c = c + 1) begin
                    b = ram_byte(16'h0400 + r * 40 + c);
                    b = b & 8'h7F;
                    if (b >= 1 && b <= 26) b = b + 8'd64; else if (b == 0) b = 8'd64; else if (b < 32) b = 8'd46;
                    ln = {ln[8*39-1:0], b};
                end
                $display("scr|%s", ln);
            end
        end
    endtask
    integer shot_us = -1;
    initial if ($value$plusargs("SHOT_AT=%d", shot_us)) begin #(shot_us * 1000.0); $display("evt| screen at %0t", $time); dump_screen(); end
    // the VIC's frames: the raster line falling back to a small value starts a new one
    integer fr_n = 0, fr_py_q = 0;
    always @(posedge clk_r) begin
        if (dut.c64.vic_pix_y + 100 < fr_py_q) fr_n = fr_n + 1;
        fr_py_q = dut.c64.vic_pix_y;
    end
    // +PIXDUMP=<n>: every pixel of frame n as "y x colour", one line per change of the position
    integer pd_frame = -1, pd_fd = 0;
    reg [18:0] pd_pos_q = 19'h7FFFF;
    initial if (!$value$plusargs("PIXDUMP=%d", pd_frame)) pd_frame = -1;
    initial if (pd_frame >= 0) pd_fd = $fopen("pixdump.txt", "w");
    always @(posedge clk_r) if (pd_frame >= 0 && fr_n == pd_frame && {pix_y, pix_x} != pd_pos_q) begin
        $fwrite(pd_fd, "%0d %0d %0d\n", pix_y, pix_x, pix_color);
        pd_pos_q = {pix_y, pix_x};
    end
    integer sc_fd = 0, sc_i;
    reg sc_done = 1'b0;
    always @(posedge clk_r) if ($test$plusargs("SCRCODES") && pd_frame >= 0 && fr_n == pd_frame + 1 && !sc_done) begin
        sc_done <= 1'b1;
        sc_fd = $fopen("scrcodes.txt", "w");
        for (sc_i = 0; sc_i < 1000; sc_i = sc_i + 1) $fwrite(sc_fd, "%02x\n", ram_byte(16'h0400 + sc_i));
        $fclose(sc_fd);
    end
    reg trace = 1'b0;
`ifndef C64SCPU_CPU6510
    // T182 step 4: the markers of the speed benches (run_speed.sh): a completed 65816 write to 24-bit $00:0B00-$00:0B03 (SRAM, never on the C64 bus) prints the PHI2 cycle it completed in
    always @(posedge clk2x_r) if (!rst && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc && dut.c64.cpu_0.wr_cyc && dut.c64.cpu_0.a_out[23:2] == 22'h0002C0)
        $display("mark|%0d|%0d|frame %0d", dut.c64.cpu_0.a_out[1:0], cyc, fr_n);
    // T182 step 6 (run_irq.sh, S9): with +RESTOREHOOK a completed 65816 write to $00:0B08 pulses the RESTORE input (ext_restore, 8 us at the 1 MHz clock enable's rate: well over two PHI2 cycles)
    integer rst_cnt = 0;
    always @(posedge clk2x_r) begin
        if ($test$plusargs("RESTOREHOOK") && !rst && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc && dut.c64.cpu_0.wr_cyc && dut.c64.cpu_0.a_out == 24'h000B08) rst_cnt <= 400;
        else if (rst_cnt != 0) rst_cnt <= rst_cnt - 1;
        restore <= rst_cnt != 0;
    end
    // +NMIDBG: the sources of the C64's NMI line at its first rising edge ("nmi|time|cia2_irq|kbd_restore_toggle|cart_nmi|ext_restore")
    reg nmi_dbg_q = 1'b0;
    always @(posedge clk2x_r) if ($test$plusargs("NMIDBG")) begin
        nmi_dbg_q <= dut.c64.cpu_nmi;
        if (dut.c64.cpu_nmi && !nmi_dbg_q) $display("nmi|%0t|%b|%b|%b|%b", $time, dut.c64.cia2_irq, dut.c64.kbd_restore_toggle, dut.c64.cart_nmi, dut.c64.ext_restore);
    end
    // +TRACE816: every bus cycle the 65816 completes (its own address, 24 bit): "c816|n|aaaaaa|R/W|data"
    integer c816 = 0;
    always @(posedge clk2x_r) if ($test$plusargs("TRACE816") && !rst && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc) begin
        c816 = c816 + 1;
        $display("c816|%0d|%06x|%s|%02x", c816, dut.c64.cpu_0.a_out, dut.c64.cpu_0.wr_cyc ? "W" : "R", dut.c64.cpu_0.wr_cyc ? dut.c64.cpu_0.d_out : dut.c64.cpu_0.d_in);
    end
`endif
`ifndef C64SCPU_CPU6510
    // +PCHIST: how often the 65816 fetched an opcode at each 24 bit address (printed at the end, "pch|addr|count"), and the last 48 fetches ("pcl|addr|op|data after") of the run, newest last.
    // Local debugging of the real ROM (run_realscpu.sh), not a check.
`ifdef C64SCPU_PCHIST
    reg [31:0] pc_hist [0:16777215]; // 64 MiB: only in the real-ROM build (run_realscpu.sh defines C64SCPU_PCHIST)
`endif
    integer pc_ring_i = 0, pc_k;
    reg [23:0] pc_ring_a [0:47];
    reg [7:0]  pc_ring_d [0:47];
    reg        pc_hist_on;
    initial pc_hist_on = $test$plusargs("PCHIST");
    always @(posedge clk2x_r) if (pc_hist_on && !rst && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.vpa && dut.c64.cpu_0.vda) begin
`ifdef C64SCPU_PCHIST
        pc_hist[dut.c64.cpu_0.a_out] = pc_hist[dut.c64.cpu_0.a_out] + 1;
`endif
        pc_ring_a[pc_ring_i] = dut.c64.cpu_0.a_out; pc_ring_d[pc_ring_i] = dut.c64.cpu_0.d_in;
        pc_ring_i = (pc_ring_i + 1) % 48;
    end
    // +STKTRACE=<aaaaaa>: at the first opcode fetch at that 24 bit address, the last 6 stack-page writes of the 65816 before it ("stk|addr|byte"): what an interrupt pushed
    reg [23:0] stk_from; reg stk_arm, stk_done = 1'b0; reg [23:0] stk_a [0:5]; reg [7:0] stk_d [0:5]; integer stk_i = 0, stk_k;
    initial stk_arm = $value$plusargs("STKTRACE=%h", stk_from);
    always @(posedge clk2x_r) if (stk_arm && !stk_done && !rst && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc) begin
        if (dut.c64.cpu_0.vpa && dut.c64.cpu_0.vda && dut.c64.cpu_0.a_out == stk_from) begin
            stk_done = 1'b1;
            for (stk_k = 0; stk_k < 6; stk_k = stk_k + 1) $display("stk|%06x|%02x", stk_a[(stk_i + stk_k) % 6], stk_d[(stk_i + stk_k) % 6]);
        end
        if (dut.c64.cpu_0.wr_cyc && dut.c64.cpu_0.a_out[23:8] == 16'h0001) begin
            stk_a[stk_i] = dut.c64.cpu_0.a_out; stk_d[stk_i] = dut.c64.cpu_0.d_out; stk_i = (stk_i + 1) % 6;
        end
    end
    // +SRAMDUMP: the 65816's SRAM bank 0 $0000-$0FFF at the end of the run ("sram|addr|16 bytes", the DDR2 holds the truth once the write cache is flushed: bytes still in the cache are not seen)
    task sram_dump;
        integer q, w; reg [127:0] ln;
        begin
            if ($test$plusargs("SRAMDUMP")) for (q = 0; q < 256; q = q + 1) begin
                for (w = 0; w < 16; w = w + 1) ln[8 * w +: 8] = ddr_byte(32'h03000000 + 16 * q + w);
                $display("sram|%04x|%h", 16 * q, {ln[7:0],ln[15:8],ln[23:16],ln[31:24],ln[39:32],ln[47:40],ln[55:48],ln[63:56],ln[71:64],ln[79:72],ln[87:80],ln[95:88],ln[103:96],ln[111:104],ln[119:112],ln[127:120]});
            end
        end
    endtask
    task pc_report;
        integer ha;
        begin
            if (pc_hist_on) begin
`ifdef C64SCPU_PCHIST
                for (ha = 0; ha < 16777216; ha = ha + 1) if (pc_hist[ha] != 0) $display("pch|%06x|%0d", ha, pc_hist[ha]);
`endif
                for (pc_k = 0; pc_k < 48; pc_k = pc_k + 1) $display("pcl|%06x|%02x", pc_ring_a[(pc_ring_i + pc_k) % 48], pc_ring_d[(pc_ring_i + pc_k) % 48]);
            end
        end
    endtask
`else
    task pc_report; begin end endtask
    task sram_dump; begin end endtask
`endif
    always @(posedge gclk) if (cpu_end && !rst) begin
        cyc = cyc + 1;
        if (trace) $display("cy %0d a=%04x %s d=%02x", cyc, b_addr, b_we ? "W" : "R", b_we ? b_dout : b_din);
        // T182 step 2: the markers of the cycle bench (run_emu.sh): a write to $0B00-$0B03 prints the PHI2 cycle it happened in
        if (b_we && b_addr[15:2] == 14'h2C0) $display("mark|%0d|%0d", b_addr[1:0], cyc);
    end

    // T182 step 4 (spec 5.6, D8): the C64 bus of an I/O cycle at 20 MHz is a 6510's: address and R/W are the same at the PHI2 rise and at the PHI2 fall of the cycle they land in (they are on the
    // bus before the chips look at the rise, and stay), and a write shows its byte at the fall: "bw|addr|data" for every write to $D020 (check_speed.py compares the sequence with the program's).
    // Sampled at the machine's clk, where the chips sample the bus.
`ifndef C64SCPU_CPU6510
    reg [15:0] bs_a = 16'd0;  reg bs_w = 1'b0;
    integer bs_bad = 0, bs_n = 0;
    always @(posedge clk_r) if (!rst) begin
        if (dut.c64.vic_phi2_p) begin bs_a = b_addr; bs_w = b_we; end
        if (dut.c64.vic_phi2_n) begin
            bs_n = bs_n + 1;
            if (b_addr !== bs_a || b_we !== bs_w) begin
                bs_bad = bs_bad + 1;
                if (bs_bad < 4) $display("FAIL bus: the address / R/W changed inside the PHI2-high phase: %04x %0d at the rise, %04x %0d at the fall (%0t)", bs_a, bs_w, b_addr, b_we, $time);
            end
            if (b_we && b_addr == 16'hD020) $display("bw|%04x|%02x", b_addr, b_dout);
        end
    end
`endif

    // T182 step 5 (spec 5.7): the mirror queue's writes on the C64 bus and the ones the VIC did not leave the bus for (aec low at the PHI2 fall: driven again in the next cycle)
`ifndef C64SCPU_CPU6510
    integer mir_n = 0, mir_f = 0, mir_ba = 0, mir_ae = 0, mir_q = 0, mir_ok = 0;
    always @(posedge clk2x_r) if (!rst) begin   // every write the queue accepted must land on the C64 bus once, with the VIC off it (aec high at the PHI2 fall)
        if (dut.c64.cpu_0.smem.core_ce && dut.c64.cpu_0.smem.mir_fast) mir_q = mir_q + 1;
        if (dut.c64.cpu_0.smem.ma_drv && dut.c64.cpu_0.smem.n_d[1] && !dut.c64.cpu_0.smem.ma_fail) mir_ok = mir_ok + 1;
    end
    always @(posedge clk2x_r) if (!rst && dut.c64.cpu_0.smem.ma_drv && dut.c64.cpu_0.smem.n_d[1]) begin
        mir_n = mir_n + 1;
        if (dut.c64.cpu_0.smem.ma_fail) mir_f = mir_f + 1;
    end
    integer v_n = 0, v_ba = 0, v_ae = 0, v_ba_cpu = 0;
    always @(posedge clk2x_r) if (!rst && dut.c64.cpu_0.smem.n_ev) begin   // all PHI2 falls: how often the VIC has the bus, and how often the CPU's write is queued meanwhile
        v_n = v_n + 1;
        if (!dut.c64.vic_ba) v_ba = v_ba + 1;
        if (!dut.c64.vic_aec) v_ae = v_ae + 1;
        if (!dut.c64.vic_aec && dut.c64.cpu_0.smem.mir_busy) v_ba_cpu = v_ba_cpu + 1;
    end
    always @(posedge clk2x_r) if (!rst && dut.c64.cpu_0.smem.ma_drv && dut.c64.cpu_0.smem.n_ev) begin
        if (!dut.c64.vic_ba) mir_ba = mir_ba + 1;
        if (!dut.c64.vic_aec) mir_ae = mir_ae + 1;
    end
`endif

    // T182 step 2: the CPU's clock enable (core_ce of cpu65816_c64scpu) is never closer than 3 clk2x cycles to the one before. The timing constraints of the flat build
    // (cores/c64scpu/mono/build_mono.tcl: set_multicycle_path -setup 3 / -hold 2 for the paths between the registers of cpu_0/cpu) are only true while this holds.
    // STEP 4's 20 MHz enable (3.15 clk2x cycles per CPU cycle) MUST KEEP IT: a gap of 2 or less makes the constraint wrong, not just the bench red.
`ifndef C64SCPU_CPU6510
    integer ce_gap = 1000, ce_min = 1000, ce_n = 0, ce_bad = 0;
    always @(posedge clk2x_r) begin
        if (rst) begin ce_gap = 1000; end
        else begin
            if (dut.c64.cpu_0.core_ce) begin
                ce_n = ce_n + 1;
                if (ce_gap < ce_min) ce_min = ce_gap;
                if (ce_gap < 3) begin
                    ce_bad = ce_bad + 1;
                    if (ce_bad < 4) $display("FAIL ce gap: two CPU enables %0d clk2x cycle(s) apart at %0t (minimum 3)", ce_gap, $time);
                end
                ce_gap = 1;
            end else if (ce_gap < 1000) ce_gap = ce_gap + 1;
        end
    end
`endif

`ifndef C64SCPU_CPU6510
    // T241 S2 (run_irqmir.sh): +IRQMIR, the probe of the mirror-everything bench (phases, IRQ edges against vector pulls, BA runs, steps that break the RDY rule)
`include "tb_irqmir.vh"
`endif
    reg [8*8192-1:0] expect_s;
    reg [8*4096-1:0] dexp_s;
    reg [31:0] dacc;
    integer ms = 100, nbad = 0, p;
`ifndef C64SCPU_CPU6510
    // ---- T208 (run_mdust.sh): the Metal Dust benches -------------------------------------------------------------------------------------------------
    // +IOW: "irq|cyc|..." at every rising edge of the VIC's IRQ line, "vec|..." at the first vector pull after it, "iow|addr|data|..." for every completed 65816 write to $D000-$D03F / $DD00 with the
    // PHI2 cycles (`dirq`) and clk2x cycles (`dclk`) since that edge and the pixel position the capture shows.
    // +MIRCHK: a shadow of every mirrored write the 65816 completed (smem.mir_fast at its enable), compared with the VIC's own copy (the ramd slot, block RAM) once the write is older than 400 clk2x
    // and the queue has had time; mismatches are printed ("mm|...") and counted; at the end the DDR2 truth is compared too and ram_n_wlost printed.
    integer c2x = 0;
    always @(posedge clk2x_r) c2x = c2x + 1;
    reg irq_q = 1'b0, vec_arm = 1'b0;
    integer irq_c = 0, irq_c2 = 0;
    always @(posedge clk2x_r) if (!rst) begin
        irq_q <= dut.c64.vic_irq;
        if (dut.c64.vic_irq && !irq_q) begin
            irq_c = cyc; irq_c2 = c2x; vec_arm = 1'b1;
            if ($test$plusargs("IOW")) $display("irq|%0d|y %0d|x %0d|fr %0d", cyc, pix_y, pix_x, fr_n);
        end
        if (vec_arm && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc && !dut.c64.cpu_0.vpb) begin
            vec_arm = 1'b0;
            if ($test$plusargs("IOW")) $display("vec|%04x|dirq %0d|dclk %0d", dut.c64.cpu_0.a_out[15:0], cyc - irq_c, c2x - irq_c2);
        end
        if ($test$plusargs("IOW") && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc && dut.c64.cpu_0.wr_cyc &&
            (dut.c64.cpu_0.a_out[23:6] == 18'h00340 || dut.c64.cpu_0.a_out == 24'h00DD00))
            $display("iow|%04x|%02x|dirq %0d|dclk %0d|cyc %0d|y %0d|x %0d", dut.c64.cpu_0.a_out[15:0], dut.c64.cpu_0.d_out, cyc - irq_c, c2x - irq_c2, cyc, pix_y, pix_x);
        if ($test$plusargs("IOW") && dut.c64.cpu_0.core_ce && dut.c64.cpu_0.bus_cyc && !dut.c64.cpu_0.wr_cyc && dut.c64.cpu_0.a_out[23:0] >= 24'h00D011 && dut.c64.cpu_0.a_out[23:0] <= 24'h00D019)
            $display("ior|%04x|%02x|dirq %0d|dclk %0d|cyc %0d|y %0d|x %0d", dut.c64.cpu_0.a_out[15:0], dut.c64.cpu_0.d_in, cyc - irq_c, c2x - irq_c2, cyc, pix_y, pix_x);
    end
    // +ARMDBG (T208 S2): the PHI2 position (clk2x cycles since the start of the cycle) of every address arming of a synchronous 65816 cycle and of every completed synchronous cycle
    always @(posedge clk2x_r) if (!rst && $test$plusargs("ARMDBG")) begin
        if (dut.c64.cpu_0.smem.arm_go) $display("arm|%0d|ph %0d|skip %0d|past_n %0d|a %04x", c2x, dut.c64.cpu_0.smem.ph, dut.c64.cpu_0.smem.skip_now, dut.c64.cpu_0.smem.past_n, dut.c64.cpu_0.a_out[15:0]);
        if (dut.c64.cpu_0.core_ce) $display("sce|%0d|ph %0d|a %04x|%s|bus %0d|sync %0d|acc %0d|wb %0d", c2x, dut.c64.cpu_0.smem.ph, dut.c64.cpu_0.a_out[15:0], dut.c64.cpu_0.wr_cyc ? "W" : "R", dut.c64.cpu_0.bus_cyc, dut.c64.cpu_0.smem.sync_cyc, dut.c64.cpu_0.smem.acc, dut.c64.cpu_0.smem.wb_st);
    end
    // +PFDBG (T208 S5): a CPU write (wr_go) to line stream + 1 in the very clock a next-line prefetch would start (the condition of scpu_cache.v pgo without its !wr_go): printed as pfcoin|.
    // No bench program reached it (0 in the smc bench; the CPU enables are at least 3 clk2x apart); tb_scpu_cache.v drives that race on the module's ports
    always @(posedge clk2x_r) if (!rst && $test$plusargs("PFDBG")) begin
        if (dut.c64.cpu_0.smem.cache.pf_pend && !dut.c64.cpu_0.smem.cache.f_go && !dut.c64.cpu_0.smem.cache.st_wait && !dut.c64.cpu_0.smem.cache.flushing && !dut.c64.cpu_0.smem.cache.wb
            && dut.c64.cpu_0.smem.cache.quiet == 3'd0 && dut.c64.cpu_0.smem.cache.wr_go && dut.c64.cpu_0.smem.cache.w_p[24:4] == dut.c64.cpu_0.smem.cache.stream_nx)
            $display("pfcoin|%0d|stream %0x", c2x, dut.c64.cpu_0.smem.cache.stream);
    end
    reg [7:0] sh_d [0:65535];
    reg       sh_in [0:65535];
    integer   sh_t [0:65535];
    reg [15:0] dl_a [0:65535];
    integer nd = 0, mm_n = 0, mm_chk = 0, mm_w = 0, di, dj, mm_ddr = 0;
    initial for (di = 0; di < 65536; di = di + 1) sh_in[di] = 1'b0;
    function [7:0] slot_b(input [15:0] a);
        reg [12:0] wi;
        begin
            wi = a[13:1];
            case (a[15:14])
                2'd0: slot_b = a[0] ? dut.mem.ramd.gb[0].hi[wi] : dut.mem.ramd.gb[0].lo[wi];
                2'd1: slot_b = a[0] ? dut.mem.ramd.gb[1].hi[wi] : dut.mem.ramd.gb[1].lo[wi];
                2'd2: slot_b = a[0] ? dut.mem.ramd.gb[2].hi[wi] : dut.mem.ramd.gb[2].lo[wi];
                default: slot_b = a[0] ? dut.mem.ramd.gb[3].hi[wi] : dut.mem.ramd.gb[3].lo[wi];
            endcase
        end
    endfunction
    reg mc_on;
    initial mc_on = $test$plusargs("MIRCHK");
    always @(posedge clk2x_r) if (mc_on && !rst) begin
        if (dut.c64.cpu_0.smem.core_ce && dut.c64.cpu_0.smem.mir_fast) begin
            mm_w = mm_w + 1;
            sh_d[dut.c64.cpu_0.smem.a16] = dut.c64.cpu_0.smem.b_d;
            sh_t[dut.c64.cpu_0.smem.a16] = c2x;
            if (!sh_in[dut.c64.cpu_0.smem.a16]) begin sh_in[dut.c64.cpu_0.smem.a16] = 1'b1; dl_a[nd] = dut.c64.cpu_0.smem.a16; nd = nd + 1; end
        end
        if (c2x[9:0] == 10'd0) begin
            dj = 0;
            for (di = 0; di < nd; di = di + 1) begin
                if (c2x - sh_t[dl_a[di]] > 400 && !dut.c64.cpu_0.smem.mir_busy) begin
                    mm_chk = mm_chk + 1;
                    if (slot_b(dl_a[di]) !== sh_d[dl_a[di]]) begin
                        mm_n = mm_n + 1;
                        if (mm_n <= 16) $display("mm|cyc %0d|fr %0d|%04x|cpu %02x|vic slot %02x|ddr %02x", cyc, fr_n, dl_a[di], sh_d[dl_a[di]], slot_b(dl_a[di]), ram_byte(dl_a[di]));
                    end
                    sh_in[dl_a[di]] = 1'b0;
                end else begin dl_a[dj] = dl_a[di]; dj = dj + 1; end
            end
            nd = dj;
        end
    end
    task mir_report;
        integer ai;
        begin
            if (mc_on) begin
                for (ai = 0; ai < 65536; ai = ai + 1)
                    if (sh_in[ai] === 1'b0 && sh_t[ai] != 0 && ram_byte(ai[15:0]) !== sh_d[ai]) mm_ddr = mm_ddr + 1;
                $display("mchk|%0d mirrored writes seen, %0d addresses checked against the VIC slot, %0d wrong, %0d wrong in the DDR2 at the end, n_wlost %0d", mm_w, mm_chk, mm_n, mm_ddr, ram_n_wlost);
                if (mm_n != 0 || mm_ddr != 0 || ram_n_wlost != 0) nbad = nbad + 1;
            end
        end
    endtask
`endif


    // ---- T217: the application's program start, replayed (run_prgrun.sh). The script is written by tools/cbm_prg_sim.cc from the application's own library code.
    integer pg_us, pg_fd, pg_n, pg_ms, pg_prev, pg_nw = 0, pg_nr = 0, pg_nk = 0, pg_nf = 0;
    reg [8*80-1:0] pg_line;
    integer pg_t;
    reg [31:0] pg_a, pg_off;
    reg [7:0]  pg_b;
    reg [63:0] pg_k;
    initial if ($value$plusargs("PRG_AT=%d", pg_us)) begin
        #(pg_us * 1000.0);
        pg_fd = $fopen("mem/prgscript.txt", "r");
        pg_prev = 0;
        while (pg_fd != 0 && !$feof(pg_fd)) begin
            pg_n = $fgets(pg_line, pg_fd);
            if (pg_n != 0) begin
                pg_n = $sscanf(pg_line, "%d", pg_t);
                if (pg_t == 1 || pg_t == 2) pg_n = $sscanf(pg_line, "%d %h %h", pg_t, pg_a, pg_b);
                if (pg_t == 1) begin
                    pg_off = pg_a - 32'h01C00000;
                    ddr.mem[pg_off >> 4][8 * pg_off[3:0] +: 8] = pg_b;
                    pg_nw = pg_nw + 1;
                end else if (pg_t == 2) begin
                    ddr.mem[8192 + pg_a[15:4]][8 * pg_a[3:0] +: 8] = pg_b;
                    pg_nr = pg_nr + 1;
                end else if (pg_t == 3) begin
                    rom_flush_tgl = ~rom_flush_tgl;
                    pg_nf = pg_nf + 1;
                    #200000.0;
                end else if (pg_t == 4) begin
                    pg_n = $sscanf(pg_line, "%d %d %h", pg_t, pg_ms, pg_k);
                    #((pg_ms - pg_prev) * 1000000.0);
                    pg_prev = pg_ms;
                    keys = pg_k;
                    pg_nk = pg_nk + 1;
                end
            end
        end
        $display("prg|%0d DDR2 bytes, %0d RAM-window bytes, %0d flushes, %0d key states replayed", pg_nw, pg_nr, pg_nf, pg_nk);
    end

    reg [63:0] keyv = 64'd0;
    reg [15:0] acc;
    reg  [7:0] vacc;
    integer c, st;
    initial begin
        if ($test$plusargs("TRACE")) trace = 1'b1;
        if (!$value$plusargs("MS=%d", ms)) ms = 100;
        if ($value$plusargs("KEYS=%h", keyv)) ;
        keys = keyv;
        #(ms * 1000000.0);
        $display("cycles: %0d, frames: %0d", cyc, fr_n);
        if ($value$plusargs("EXPECT=%s", expect_s)) begin
            // "aaaa:vv,aaaa:vv"
            if (expect_s[8191*8 +: 8] != 0) begin $display("FAIL: +EXPECT fills the %0d-char buffer, it may be truncated", 8192); nbad = nbad + 1; end
            acc = 0; vacc = 0; st = 0;
            for (p = 8191; p >= 0; p = p - 1) begin
                c = expect_s[p*8 +: 8];
                if (c != 0) begin
                    if (c == ":") begin st = 1; end
                    else if (c == ",") begin
                        if (ram_byte(acc) !== vacc) begin $display("FAIL: ram[%04x] = %02x, expected %02x", acc, ram_byte(acc), vacc); nbad = nbad + 1; end
                        else $display("ok   ram[%04x] = %02x", acc, vacc);
                        acc = 0; vacc = 0; st = 0;
                    end else begin
                        c = (c >= "a") ? c - "a" + 10 : (c >= "A") ? c - "A" + 10 : c - "0";
                        if (st == 0) acc = {acc[11:0], c[3:0]}; else vacc = {vacc[3:0], c[3:0]};
                    end
                end
            end
            if (st == 1) begin
                if (ram_byte(acc) !== vacc) begin $display("FAIL: ram[%04x] = %02x, expected %02x", acc, ram_byte(acc), vacc); nbad = nbad + 1; end
                else $display("ok   ram[%04x] = %02x", acc, vacc);
            end
        end
        if ($value$plusargs("DEXPECT=%s", dexp_s)) begin
            // "aaaaaaaa:vv,aaaaaaaa:vv": absolute DDR2 byte addresses (hex), the DDR2 holds the truth once the write jobs have drained
            if (dexp_s[4095*8 +: 8] != 0) begin $display("FAIL: +DEXPECT fills the %0d-char buffer, it may be truncated", 4096); nbad = nbad + 1; end
            dacc = 0; vacc = 0; st = 0;
            for (p = 4095; p >= 0; p = p - 1) begin
                c = dexp_s[p*8 +: 8];
                if (c != 0) begin
                    if (c == ":") st = 1;
                    else if (c == ",") begin
                        if (ddr_byte(dacc) !== vacc) begin $display("FAIL: ddr[%08x] = %02x, expected %02x", dacc, ddr_byte(dacc), vacc); nbad = nbad + 1; end
                        else $display("ok   ddr[%08x] = %02x", dacc, vacc);
                        dacc = 0; vacc = 0; st = 0;
                    end else begin
                        c = (c >= "a") ? c - "a" + 10 : (c >= "A") ? c - "A" + 10 : c - "0";
                        if (st == 0) dacc = {dacc[27:0], c[3:0]}; else vacc = {vacc[3:0], c[3:0]};
                    end
                end
            end
            if (st == 1) begin
                if (ddr_byte(dacc) !== vacc) begin $display("FAIL: ddr[%08x] = %02x, expected %02x", dacc, ddr_byte(dacc), vacc); nbad = nbad + 1; end
                else $display("ok   ddr[%08x] = %02x", dacc, vacc);
            end
        end
        if ($value$plusargs("DUMP=%h", dmp_a)) begin   // +DUMP=aaaa: sixteen C64 RAM bytes, printed (the raw counts of a bench: run_irq.sh dumps $0330-$033F)
            $display("dump|%04x|%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", dmp_a[15:0], ram_byte(dmp_a[15:0]), ram_byte(dmp_a[15:0] + 1), ram_byte(dmp_a[15:0] + 2), ram_byte(dmp_a[15:0] + 3),
                     ram_byte(dmp_a[15:0] + 4), ram_byte(dmp_a[15:0] + 5), ram_byte(dmp_a[15:0] + 6), ram_byte(dmp_a[15:0] + 7),
                     ram_byte(dmp_a[15:0] + 8), ram_byte(dmp_a[15:0] + 9), ram_byte(dmp_a[15:0] + 10), ram_byte(dmp_a[15:0] + 11),
                     ram_byte(dmp_a[15:0] + 12), ram_byte(dmp_a[15:0] + 13), ram_byte(dmp_a[15:0] + 14), ram_byte(dmp_a[15:0] + 15));
        end
        $display("scpu|hits %0d|misses %0d|dev_cyc %0d|boot %0d|hw %0d|dos %0d", scpu_n_hit, scpu_n_miss, scpu_dev_cyc, scpu_bootmap, scpu_hwenable, scpu_dosext);
        $display("romc|%0d|%0d|%0d|%0d|bad %0d", dut.mem.romc.n_hit, dut.mem.romc.n_miss, dut.mem.romc.n_wait, dut.mem.romc.fill_max, rc_bad);
        if (rc_bad != 0) nbad = nbad + 1;
`ifndef C64SCPU_CPU6510
        mir_report;
`endif
`ifndef C64SCPU_CPU6510
        $display("bus|%0d PHI2 cycles looked at, %0d with the address or R/W changing inside the high phase", bs_n, bs_bad);
        if (bs_bad != 0 && $test$plusargs("BUSCHK")) nbad = nbad + 1;      // +BUSCHK: run_speed.sh (the 20 MHz bench); the 1 MHz benches do not look
        $display("mir|%0d mirror writes driven on the C64 bus, %0d of them held off by the VIC (repeated)", mir_n, mir_f);
        $display("mird|at the PHI2 fall of a driven write: ba low %0d, aec low %0d", mir_ba, mir_ae);
        $display("mirv|%0d PHI2 falls, ba low %0d, aec low %0d, aec low with the queue busy %0d", v_n, v_ba, v_ae, v_ba_cpu);
        $display("mirq|%0d mirror writes accepted by the queue, %0d landed on the C64 bus with the VIC off it", mir_q, mir_ok);
        $display("ce|%0d CPU enables, smallest gap %0d clk2x cycles (must be >= 3), %0d too close", ce_n, ce_min, ce_bad);
        if (ce_bad != 0 || (ce_n == 0 && rvp != 0)) nbad = nbad + 1;       // +ROMVALID=0 holds the CPU: no enable is the point
`endif
        if ($test$plusargs("SCREEN")) dump_screen();
        pc_report();
        sram_dump();
        if (nbad == 0) $display("PASS tb_c64scpu"); else $display("FAIL tb_c64scpu: %0d wrong", nbad);
        $finish;
    end
endmodule
