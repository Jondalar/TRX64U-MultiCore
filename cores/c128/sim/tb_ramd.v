// T140 D25: c128_ramd + c128_ddrx against the NES bench's DDR2 model (ui_clk 75 MHz, reads 22..52 clocks, c_rdy down a quarter of the clocks), with a reference
// memory. The "machine" issues one command every 16 core clocks, alternately the VIC's (a read in its bank, no wait) and the CPU's (a read that is repeated
// while it is held, or a write). Checks, per scenario (the script is the second initial block):
//   every CPU read returns the reference byte; every VIC read that the module answers from a slot (dut.s_hit) returns the reference word, every other one $FFFF;
//   once the VIC's bank is a full slot the VIC never misses again, whatever the CPU writes (S1, S2, S3);
//   a second bank is resident as well (alternating banks never miss after the first sweeps);
//   a command with a bank that is no slot does not allocate (S4); a burst of writes followed by a read of its line gives the new bytes (S5);
//   the port job reads and writes what the CPU sees (S6); nothing is lost in the write FIFO (n_wlost = 0).
// -DLATMIN=n -DLATRND=n set the DDR2 latency (ui clocks) for the worst-case run.
`timescale 1ns / 1ps
module tb_ramd;
`ifdef DYN
    parameter NS = 5, PINP = 1, ND = 1;      // T140 D28: four pinned banks of block 0 and one allocatable slot (one bank of block 1)
`elsif PIN
    parameter NS = 4, PINP = 1, ND = 0;
`else
    parameter NS = 2, PINP = 0, ND = 0;
`endif
    reg clk = 1'b0, ui = 1'b0;
    always #15.875 clk = ~clk;            // 31.5 MHz
    always #6.667 ui = ~ui;               // 75 MHz

    reg         cmd_valid = 1'b0, cmd_we = 1'b0, cmd_vic = 1'b0;
    reg  [15:0] cmd_wa = 16'd0, cmd_wdata = 16'd0;
    reg   [1:0] cmd_wlane = 2'd0;
    reg   [2:0] vic_tag = 3'd0;
    wire        rd_v, rd_miss, hold;
    wire [15:0] rd_word;
    reg         pj_req = 1'b0, pj_we = 1'b0;
    reg  [15:0] pj_addr = 16'd0;
    reg   [7:0] pj_wdata = 8'd0;
    wire        pj_ack;
    wire  [7:0] pj_rdata;
    wire        m_req_tgl, w_req_tgl, m_done_tgl, w_done_tgl;
    wire [12:0] m_req_line;
    wire [16:0] w_req_addr;
    wire  [7:0] w_req_data;
    wire [127:0] fill;
    wire [15:0] n_vic_miss, n_cpu_wait, n_fetch, n_wlost, n_vic_blk1;
    wire [NS*3-1:0] slot_tags;
    wire [NS-1:0]   slot_act, slot_full;

    c128_ramd #(.NSLOT(NS), .PIN(PINP), .NDYN(ND)) dut (
        .clk(clk), .cmd_valid(cmd_valid), .cmd_we(cmd_we), .cmd_wa(cmd_wa), .cmd_wdata(cmd_wdata), .cmd_wlane(cmd_wlane), .cmd_vic(cmd_vic),
        .vic_tag(vic_tag), .rd_v(rd_v), .rd_word(rd_word), .rd_miss(rd_miss), .hold(hold),
        .pj_req(pj_req), .pj_addr(pj_addr), .pj_we(pj_we), .pj_wdata(pj_wdata), .pj_rom(1'b0), .pj_ack(pj_ack), .pj_rdata(pj_rdata),
        .m_req_tgl(m_req_tgl), .m_req_line(m_req_line), .m_done_tgl(m_done_tgl), .fill(fill),
        .w_req_tgl(w_req_tgl), .w_req_addr(w_req_addr), .w_req_data(w_req_data), .w_done_tgl(w_done_tgl),
        .n_vic_miss(n_vic_miss), .n_cpu_wait(n_cpu_wait), .n_fetch(n_fetch), .n_wlost(n_wlost), .n_vic_blk1(n_vic_blk1),
        .slot_tags(slot_tags), .slot_act(slot_act), .slot_full(slot_full));

    wire        owns, en, rdy, rd_valid;
    wire  [2:0] cmd;
    wire [26:0] addr;
    wire [127:0] wdata, rd_data;
    wire [15:0] wmask;
    c128_ddrx ddrx (
        .ui_clk(ui), .ui_rst(1'b0),
        .r_req_tgl(1'b0), .r_req_line(13'd0), .r_done_tgl(),
        .m_req_tgl(m_req_tgl), .m_req_line(m_req_line), .m_done_tgl(m_done_tgl),
        .w_req_tgl(w_req_tgl), .w_req_addr(w_req_addr), .w_req_data(w_req_data), .w_done_tgl(w_done_tgl),
        .fill(fill), .owns(owns), .en(en), .cmd(cmd), .addr(addr), .wdata(wdata), .wmask(wmask), .rdy(rdy), .rd_valid(rd_valid), .rd_data(rd_data));
`ifndef LATMIN
 `define LATMIN 22
`endif
`ifndef LATRND
 `define LATRND 30
`endif
    ddr2c_model #(.BASE(32'h01C00000), .LAT_MIN(`LATMIN), .LAT_RND(`LATRND)) ddr (
        .ui_clk(ui), .c_addr(addr), .c_cmd(cmd), .c_en(en), .c_rdy(rdy), .c_wdf_data(wdata), .c_wdf_mask(wmask),
        .c_wdf_wren(en && cmd == 3'b000), .c_rd_data(rd_data), .c_rd_valid(rd_valid));

    // ---- the reference ------------------------------------------------------------------------------------------------------------------
    reg [7:0] refm [0:131071];
    integer i, k;
    initial begin
        for (i = 0; i < 131072; i = i + 1) refm[i] = (i * 7 + (i >> 8) * 13 + 5) & 255;
        for (i = 0; i < 8192; i = i + 1)
            for (k = 0; k < 16; k = k + 1) ddr.mem[8192 + i][8 * k +: 8] = refm[16 * i + k];
    end
    function [15:0] refw(input [15:0] wa);
        refw = {refm[{wa, 1'b1}], refm[{wa, 1'b0}]};
    endfunction

    // ---- the machine ---------------------------------------------------------------------------------------------------------------------
    // scenario controls (set by the script)
    reg         run = 1'b0;
    reg   [2:0] vic_bank = 3'd0;                 // the bank the VIC reads
    reg         vic_on = 1'b1;
    reg  [15:0] cpu_base = 16'd0;                // CPU region: word address base ...
    integer     cpu_span = 256;                  // ... and span in words
    integer     cpu_wr = 30;                     // percent of CPU operations that are writes
    reg         cpu_burst = 1'b0;
    reg         cpu_inflight = 1'b0;           // CPU writes go to the slot line that is being fetched or copied (white box: dut.f_line)
    reg         glitch = 1'b0;                   // the VIC's commands carry another bank (every 5th)
    integer     seed = 7;
    integer     errors = 0, cpu_reads = 0, cpu_holds = 0, cpu_wrs = 0, vic_reads = 0, vic_missed = 0, vic_wrong = 0, hold_clks = 0;
    reg         cpu_rep = 1'b0;
    reg  [15:0] cpu_rep_wa = 16'd0;
    reg  [15:0] w;
    reg   [7:0] bsel;
    integer     phase = 0, nvic = 0;
    reg         vmiss_flag;

    task issue(input vic, input we, input [15:0] wa, input [7:0] data, input lane);
        begin
            cmd_valid <= 1'b1; cmd_vic <= vic; cmd_we <= we; cmd_wa <= wa; cmd_wdata <= {data, data}; cmd_wlane <= lane ? 2'b10 : 2'b01;
            @(posedge clk);
            cmd_valid <= 1'b0;
        end
    endtask

    // one VIC read: the answer is checked in the clock after the command
    task vic_read;
        reg [15:0] wa;
        reg        hit;
        begin
            nvic = nvic + 1;
            wa = {glitch && (nvic % 5 == 0) ? (PINP ? vic_bank ^ 3'd3 : ~vic_bank) : vic_bank, 13'd0} | ($random(seed) & 16'h1FFF);
            issue(1'b1, 1'b0, wa, 8'd0, 1'b0);
            @(posedge clk);                       // the clock after: sample
            hit = dut.s_hit;
            vic_reads = vic_reads + 1;
            if (!rd_v) begin errors = errors + 1; $display("FAIL VIC read %h: no answer", wa); end
            else if (hit) begin
                if (rd_word !== refw(wa)) begin vic_wrong = vic_wrong + 1; errors = errors + 1; if (vic_wrong < 6) $display("FAIL VIC read %h got %h want %h (t=%0t)", wa, rd_word, refw(wa), $time); end
            end else begin
                vic_missed = vic_missed + 1;
                if (rd_word !== 16'hFFFF) begin errors = errors + 1; $display("FAIL VIC miss %h answered %h", wa, rd_word); end
            end
        end
    endtask

    task cpu_op;
        reg [15:0] wa;
        reg we;
        reg [7:0] d;
        reg lane;
        integer burst;
        begin
            if (hold) begin cpu_holds = cpu_holds + 1; end
            else begin
                if (cpu_rep) wa = cpu_rep_wa;
                else wa = cpu_base + ($random(seed) & 32'h7FFFFFFF) % cpu_span;
                we = !cpu_rep && (($random(seed) & 32'h7FFFFFFF) % 100 < cpu_wr);
                if (cpu_inflight && !cpu_rep && dut.fs != 2'd0 && !dut.f_cache && (($random(seed) & 32'h7FFFFFFF) % 4 == 0)) begin wa = {dut.f_line, 3'd0} | ($random(seed) & 7); we = 1'b1; end
                if (we) begin
                    d = $random(seed); lane = $random(seed);
                    refm[{wa, lane}] = d;
                    cpu_wrs = cpu_wrs + 1;
                    issue(1'b0, 1'b1, wa, d, lane);
                    @(posedge clk);
                end else begin
                    issue(1'b0, 1'b0, wa, 8'd0, 1'b0);
                    @(posedge clk);
                    cpu_reads = cpu_reads + 1;
                    if (rd_miss) begin cpu_rep = 1'b1; cpu_rep_wa = wa; end
                    else if (rd_v) begin
                        cpu_rep = 1'b0;
                        if (rd_word !== refw(wa)) begin errors = errors + 1; $display("FAIL CPU read %h got %h want %h (t=%0t)", wa, rd_word, refw(wa), $time); end
                    end else begin errors = errors + 1; $display("FAIL CPU read %h: neither word nor miss", wa); end
                end
            end
        end
    endtask

    // a burst of writes to one fresh line, then a read of that line (the FIFO must be empty before the fetch starts)
    task burst_test(input [15:0] line_wa);
        integer n;
        reg [7:0] d;
        reg lane;
        begin
            for (n = 0; n < 5; n = n + 1) begin
                d = $random(seed); lane = n[0];
                refm[{line_wa + n, lane}] = d;
                issue(1'b0, 1'b1, line_wa + n, d, lane);
            end
            // the read of the line: held until the line is in
            cpu_rep = 1'b0;
            for (n = 0; n < 40; n = n + 1) begin
                repeat (14) @(posedge clk);
                issue(1'b0, 1'b0, line_wa + 2, 8'd0, 1'b0);
                @(posedge clk);
                if (rd_miss) ;
                else if (rd_v) begin
                    if (rd_word !== refw(line_wa + 2)) begin errors = errors + 1; $display("FAIL burst read %h got %h want %h", line_wa + 2, rd_word, refw(line_wa + 2)); end
                    n = 99;
                end
            end
            if (n != 100) begin errors = errors + 1; $display("FAIL burst read never answered"); end
        end
    endtask

    integer slot_i;
    reg  [15:0] hv;
    integer cyc_n = 0;
    always @(posedge clk) hold_clks <= hold_clks + (hold ? 1 : 0);
    initial begin
        wait (run);
        forever begin
            if (!run) @(posedge clk);
            else begin
                if (cyc_n % 2 == 0) begin if (vic_on) vic_read; else repeat (3) @(posedge clk); end
                else cpu_op;
                cyc_n = cyc_n + 1;
                // 16 clocks per half cycle: issue + sample took 4 or so
                repeat (12) @(posedge clk);
            end
        end
    end
    always @(*) if (run) vic_tag = vic_bank;

    // ---- the script ------------------------------------------------------------------------------------------------------------------------
    integer t, vm0, bad0, tries;
    task settle_full(input integer which, input integer maxclk);   // wait until slot `which` is full
        integer c;
        begin
            c = 0;
            while (!slot_full[which] && c < maxclk) begin @(posedge clk); c = c + 1; end
            if (!slot_full[which]) begin errors = errors + 1; $display("FAIL slot %0d never became full (waited %0d clocks, fetches %0d, vic misses %0d)", which, maxclk, n_fetch, n_vic_miss); end
            else $display("   slot %0d full after %0d clocks (%0d fetches, %0d VIC misses so far)", which, c, n_fetch, n_vic_miss);
        end
    endtask
    task no_more_misses(input integer clocks, input [255:0] what);
        integer m0;
        begin
            m0 = vic_missed;
            repeat (clocks) @(posedge clk);
            if (vic_missed != m0) begin errors = errors + 1; $display("FAIL %0s: %0d VIC misses in %0d clocks after the sweep", what, vic_missed - m0, clocks); end
            else $display("   %0s: no VIC miss in %0d clocks (%0d VIC reads so far)", what, clocks, vic_reads);
        end
    endtask

`ifdef DYN
`define PIN
`endif
`ifdef PIN
    // P: NSLOT = 4, PIN = 1: the four banks of block 0 are slots from the start (tags 0..3), swept once, then the VIC never misses in any of them, in any order;
    //    a VIC read of block 1 is answered $FF and counted; commands and bank bits never allocate
    initial begin
        repeat (20) @(posedge clk);
        $display("== P1 pinned slots: four banks of block 0, rotated");
        if (slot_tags[11:0] !== 12'b011_010_001_000 || slot_act[3:0] !== 4'b1111) begin errors = errors + 1; $display("FAIL P1: slots not pinned to banks 0..3 (tags %b act %b)", slot_tags, slot_act); end
        vic_bank = 3'd0; cpu_base = 16'h0000; cpu_span = 16384; cpu_wr = 40; run = 1'b1;
        for (t = 0; t < 4; t = t + 1) begin vic_bank = t[2:0]; repeat (200) @(posedge clk); end
        vic_bank = 3'd0; settle_full(0, 600000); settle_full(1, 600000); settle_full(2, 600000); settle_full(3, 600000);
        vm0 = vic_missed;
        for (t = 0; t < 120; t = t + 1) begin vic_bank = (t * 7 + (t >> 2)) & 3; repeat (150) @(posedge clk); end
        if (vic_missed != vm0) begin errors = errors + 1; $display("FAIL P1: %0d VIC misses with all four banks resident", vic_missed - vm0); end
        else $display("   P1: no VIC miss in 120 switches among the four banks of block 0 (%0d VIC reads so far)", vic_reads);
        $display("== P2 CAS glitch and CPU writes into every bank");
        vic_bank = 3'd1; glitch = 1'b1; cpu_base = 16'h0000; cpu_span = 32768; cpu_wr = 70; vm0 = vic_missed; repeat (40000) @(posedge clk); glitch = 1'b0;
        if (vic_missed != vm0) begin errors = errors + 1; $display("FAIL P2: %0d VIC misses (glitch) in block 0", vic_missed - vm0); end
        else $display("   P2: no VIC miss with the glitch, CPU writing all four banks");
        if (slot_tags[11:0] !== 12'b011_010_001_000) begin errors = errors + 1; $display("FAIL P2: tags moved (%b)", slot_tags); end
`ifdef DYN
        // D28: slot 4 is free until the VIC selects a bank of block 1; the selection alone (no read: the VIC is off in this part) takes it, the sweep fills it,
        // the VIC reads it without a miss, CPU writes into it stay current, the glitch (another bank of block 1) does not take it, block 0 stays resident
        $display("== P3 slot 4: one bank of block 1 on demand");
        if (slot_act[4] !== 1'b0) begin errors = errors + 1; $display("FAIL P3: slot 4 taken before block 1 was selected"); end
        run = 1'b0; repeat (100) @(posedge clk);
        vic_on = 1'b0; cpu_base = 16'hA000; cpu_span = 8192; cpu_wr = 70; run = 1'b1;
        vic_bank = 3'd5; repeat (20) @(posedge clk);
        if (slot_act[4] !== 1'b1 || slot_tags[14:12] !== 3'd5) begin errors = errors + 1; $display("FAIL P3: selecting block 1 bank 1 did not take slot 4 (act %b tag %0d)", slot_act, slot_tags[14:12]); end
        else $display("   P3: slot 4 taken 20 clocks after the selection (tag %0d), no VIC read yet", slot_tags[14:12]);
        settle_full(4, 600000);
        vic_on = 1'b1; vm0 = vic_missed; t = n_vic_blk1; bad0 = vic_wrong;
        repeat (60000) @(posedge clk);
        if (vic_missed != vm0 || vic_wrong != bad0 || vic_reads == 0) begin errors = errors + 1; $display("FAIL P3: %0d VIC misses, %0d wrong in block 1 after the sweep", vic_missed - vm0, vic_wrong - bad0); end
        else $display("   P3: no VIC miss and no wrong word in 60000 clocks of block 1 reads with CPU writes into the bank");
        glitch = 1'b1; repeat (40000) @(posedge clk); glitch = 1'b0;
        if (slot_tags[14:12] !== 3'd5 || slot_act[4] !== 1'b1) begin errors = errors + 1; $display("FAIL P3: the glitch moved slot 4 (tag %0d)", slot_tags[14:12]); end
        $display("== P4 another bank of block 1, and back");
        vic_bank = 3'd7; cpu_base = 16'hE000; repeat (20) @(posedge clk);
        if (slot_tags[14:12] !== 3'd7) begin errors = errors + 1; $display("FAIL P4: slot 4 did not follow the selection (tag %0d)", slot_tags[14:12]); end
        vm0 = vic_missed; t = n_vic_blk1; repeat (20000) @(posedge clk);
        $display("   P4: %0d VIC reads answered $FF (counted %0d) while the new bank is swept; wrong words %0d", vic_missed - vm0, n_vic_blk1 - t, vic_wrong - bad0);
        if (n_vic_blk1 - t != vic_missed - vm0) begin errors = errors + 1; $display("FAIL P4: misses of block 1 not counted"); end
        settle_full(4, 600000);
        vm0 = vic_missed; repeat (40000) @(posedge clk);
        if (vic_missed != vm0 || vic_wrong != bad0) begin errors = errors + 1; $display("FAIL P4: misses or wrong words after the sweep"); end
        vic_bank = 3'd2; repeat (200) @(posedge clk);
`else
        $display("== P3 VIC reads block 1");
        vm0 = vic_missed; t = n_vic_blk1;
        vic_bank = 3'd5; repeat (4000) @(posedge clk); vic_bank = 3'd0; repeat (200) @(posedge clk);
        if (vic_missed == vm0 || n_vic_blk1 == t) begin errors = errors + 1; $display("FAIL P3: block 1 reads were not counted (misses %0d, blk1 %0d)", vic_missed - vm0, n_vic_blk1 - t); end
        else $display("   P3: %0d VIC reads of block 1 answered $FF and counted (n_vic_blk1 +%0d); wrong bytes stay %0d", vic_missed - vm0, n_vic_blk1 - t, vic_wrong);
`endif
        no_more_misses(20000, "P3 back in block 0");
        run = 1'b0; repeat (100) @(posedge clk);
        for (t = 0; t < 40; t = t + 1) begin burst_test(16'h7000 + t * 16 + 8 * (t & 1)); repeat (60) @(posedge clk); end
        repeat (200) @(posedge clk);
        if (n_wlost != 0) begin errors = errors + 1; $display("FAIL: %0d writes found the FIFO full", n_wlost); end
        $display("   VIC reads %0d (misses %0d, wrong %0d), CPU reads %0d (held %0d), CPU writes %0d, fetches %0d, writes lost %0d", vic_reads, vic_missed, vic_wrong, cpu_reads, n_cpu_wait, cpu_wrs, n_fetch, n_wlost);
        if (errors == 0) $display("PASS tb_ramd"); else $display("FAIL tb_ramd: %0d errors", errors);
        $finish;
    end
    initial begin #400000000; $display("FAIL tb_ramd: timeout"); $finish; end
endmodule
`else
    initial begin
        repeat (20) @(posedge clk);
        // S1: VIC in bank 0, CPU in bank 3 (cache only) with writes, then in bank 0 (slot + cache)
        $display("== S1 VIC bank 0, CPU in bank 3 (cache only) and in bank 0");
        vic_bank = 3'd0; cpu_base = 16'h6000; cpu_span = 2048; cpu_wr = 40; run = 1'b1;
        settle_full(0, 400000);
        no_more_misses(60000, "S1 VIC bank 0");
        cpu_base = 16'h0100; cpu_span = 4096;          // CPU writes into the VIC's own bank
        no_more_misses(60000, "S1 CPU writes inside the VIC bank");
        // S2: banks alternate, both end up resident
        $display("== S2 two banks alternate");
        for (t = 0; t < 4; t = t + 1) begin vic_bank = 3'd1; repeat (3000) @(posedge clk); vic_bank = 3'd0; repeat (3000) @(posedge clk); end
        vic_bank = 3'd1; settle_full(1, 400000);
        for (t = 0; t < 30; t = t + 1) begin vic_bank = 3'd0; repeat (600) @(posedge clk); vic_bank = 3'd1; repeat (600) @(posedge clk); end
        vm0 = vic_missed;
        for (t = 0; t < 30; t = t + 1) begin vic_bank = 3'd0; repeat (600) @(posedge clk); vic_bank = 3'd1; repeat (600) @(posedge clk); end
        // a switch is stable for 4 clocks before the module notices; the VIC reads one cycle later at the earliest: no miss in resident banks
        if (vic_missed != vm0) begin errors = errors + 1; $display("FAIL S2: %0d VIC misses while two banks alternate", vic_missed - vm0); end
        else $display("   S2: no VIC miss in 60 switches between two resident banks");
        // S3: CPU writes into the bank being swept (a third bank is allocated, evicting the older one)
        $display("== S3 a third bank, CPU writes into it while it is swept");
        cpu_base = 16'h4000; cpu_span = 8192; cpu_wr = 70;
        vic_bank = 3'd2; repeat (200) @(posedge clk);
        settle_full(0 + (slot_tags[2:0] == 3'd2 ? 0 : 1), 600000);
        no_more_misses(40000, "S3 bank 2");
        // S3b: CPU writes aimed at the slot line in flight (a stale line must not become valid)
        $display("== S3b CPU writes into the line that is being fetched");
        cpu_inflight = 1'b1; cpu_wr = 100; cpu_base = 16'h0000; cpu_span = 8192;
        vic_bank = 3'd4; repeat (200) @(posedge clk);
        settle_full(slot_tags[2:0] == 3'd4 ? 0 : 1, 800000);
        cpu_inflight = 1'b0; cpu_wr = 70;
        no_more_misses(40000, "S3b bank 4");
        // S4: the glitch: a VIC command with a bank that is no slot must not allocate
        $display("== S4 CAS glitch: commands with another bank");
        hv = {slot_tags[3 +: 3], slot_tags[0 +: 3]};
        glitch = 1'b1; repeat (30000) @(posedge clk); glitch = 1'b0;
        if ({slot_tags[3 +: 3], slot_tags[0 +: 3]} !== hv) begin errors = errors + 1; $display("FAIL S4: a command allocated a slot (tags %h -> %h)", hv, {slot_tags[3 +: 3], slot_tags[0 +: 3]}); end
        else $display("   S4: slot tags unchanged (%h)", hv);
        // S5: ordering of a write burst and a fetch of the same line (no CPU traffic meanwhile)
        $display("== S5 write burst, then a read of the line");
        run = 1'b0; repeat (100) @(posedge clk);
        for (t = 0; t < 40; t = t + 1) begin burst_test(16'h7000 + t * 16 + 8 * (t & 1)); repeat (60) @(posedge clk); end
        // S7: a port write to the line a CPU read is fetching (the fetched line is dropped, the repeated read sees the new byte)
        $display("== S7 port write while the line is in flight");
        for (t = 0; t < 30; t = t + 1) begin
            pj_addr <= 16'hA000 + t * 64 + 2; pj_wdata <= 8'h5A ^ t; pj_we <= 1'b1;
            issue(1'b0, 1'b0, (16'hA000 + t * 64 + 2) >> 1, 8'd0, 1'b0);       // the CPU read: a miss, the fetch starts
            @(posedge clk);
            refm[17'h0A000 + t * 64 + 2] = 8'h5A ^ t;
            repeat (t % 3) @(posedge clk);
            pj_req <= 1'b1; @(posedge clk); pj_req <= 1'b0;                     // the write, while the line is on its way
            tries = 0; while (!pj_ack && tries < 400) begin @(posedge clk); tries = tries + 1; end
            for (tries = 0; tries < 60; tries = tries + 1) begin
                repeat (14) @(posedge clk);
                issue(1'b0, 1'b0, (16'hA000 + t * 64 + 2) >> 1, 8'd0, 1'b0);
                @(posedge clk);
                if (rd_v) begin
                    if (rd_word[7:0] !== refm[17'h0A000 + t * 64 + 2]) begin errors = errors + 1; $display("FAIL S7: read %h got %h want %h", 16'hA000 + t * 64 + 2, rd_word[7:0], refm[17'h0A000 + t * 64 + 2]); end
                    tries = 99;
                end
            end
            if (tries != 100) begin errors = errors + 1; $display("FAIL S7: read never answered"); end
            repeat (30) @(posedge clk);
        end
        // S6: the port job
        $display("== S6 port job");
        for (t = 0; t < 40; t = t + 1) begin
            pj_addr <= 16'h8000 + t * 37; pj_wdata <= t * 5 + 1; pj_we <= 1'b1; refm[17'h08000 + t * 37] = t * 5 + 1; @(posedge clk);
            pj_req <= 1'b1; @(posedge clk); pj_req <= 1'b0;
            tries = 0; while (!pj_ack && tries < 400) begin @(posedge clk); tries = tries + 1; end
            if (!pj_ack) begin errors = errors + 1; $display("FAIL S6: write job %0d not acked", t); end
            repeat (3) @(posedge clk);
            pj_we <= 1'b0; pj_req <= 1'b1; @(posedge clk); pj_req <= 1'b0;
            tries = 0; while (!pj_ack && tries < 4000) begin @(posedge clk); tries = tries + 1; end
            if (!pj_ack) begin errors = errors + 1; $display("FAIL S6: read job %0d not acked", t); end
            else if (pj_rdata !== refm[{1'b0, pj_addr}]) begin errors = errors + 1; $display("FAIL S6: port read %h got %h want %h", pj_addr, pj_rdata, refm[{1'b0, pj_addr}]); end
            repeat (20) @(posedge clk);
        end
        repeat (200) @(posedge clk);
        if (n_wlost != 0) begin errors = errors + 1; $display("FAIL: %0d writes found the FIFO full", n_wlost); end
        $display("   VIC reads %0d (misses %0d, wrong %0d), CPU reads %0d (held %0d), CPU writes %0d, fetches %0d, hold clocks %0d, writes lost %0d, DDR2 reads %0d writes %0d",
            vic_reads, vic_missed, vic_wrong, cpu_reads, n_cpu_wait, cpu_wrs, n_fetch, hold_clks, n_wlost, ddr.reads, ddr.writes);
        if (errors == 0) $display("PASS tb_ramd"); else $display("FAIL tb_ramd: %0d errors", errors);
        $finish;
    end
    initial begin #400000000; $display("FAIL tb_ramd: timeout"); $finish; end
endmodule
`endif
