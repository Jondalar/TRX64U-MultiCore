// T113 §5.4 bench: the C128 ROM line cache (c128_romc.v + c128_rome.v) against the DDR2 model the NES bench uses (cores/nes/sim/ddr2c_model.v,
// the host's real-time port: 293..693 ns per read, c_rdy low a quarter of the clocks). sys clock 31.5 MHz (a C64 cycle = 32 clocks), ui_clk 75 MHz.
//
//   iverilog -g2012 -o tb_romc cores/c128/sim/tb_romc.v cores/c128/c128_romc.v cores/c128/c128_rome.v cores/nes/sim/ddr2c_model.v && vvp tb_romc
//   negative controls (each must print FAIL): add -DROMC_MUT_NOMISS | -DROMC_MUT_NOTAG | -DROMC_MUT_NOFLUSH
//
// The "machine" is the CPU of the C64 core (T113 5.7): it presents a ROM word address and gets the answer in the next clock - the word (`rvalid`) or
// "miss" (`rmiss`; nothing else waits). On a miss it repeats the read in its next cycle (32 clocks later: a 1 MHz 8502), until the word is there. The
// next read comes `period` clocks after the word (32 = a 1 MHz 8502 reading ROM every cycle, 16 = the same at 2 MHz). The image is generated, never a
// Commodore byte.
`timescale 1ns / 1ps
module tb_romc;
    reg clk = 0, ui_clk = 0;
    always #15.85 clk = ~clk;          // 31.5 MHz
    always #6.667 ui_clk = ~ui_clk;    // 75 MHz

    reg         flush = 0, cmd_valid = 0;
    reg  [15:0] cmd_word = 0;
    wire        cmd_ready, rvalid, rmiss, fill_busy;
    wire [15:0] rdata;
    wire        req_tgl, done_tgl, owns, en, rdy, rd_valid;
    wire [12:0] req_line;
    wire [127:0] fill, rd_data;
    wire [26:0] addr;
    wire [31:0] n_hit, n_miss, n_wait;
    wire [15:0] fill_max;

    c128_romc dut (.clk(clk), .flush(flush), .cmd_valid(cmd_valid), .cmd_ready(cmd_ready), .cmd_word(cmd_word),
        .rvalid(rvalid), .rmiss(rmiss), .rdata(rdata), .req_tgl(req_tgl), .req_line(req_line), .done_tgl(done_tgl), .fill(fill),
        .n_hit(n_hit), .n_miss(n_miss), .n_wait(n_wait), .fill_max(fill_max), .fill_busy(fill_busy), .busy());
    c128_rome #(.ROM_BASE_W(27'h0E00000)) eng (.ui_clk(ui_clk), .ui_rst(1'b0), .req_tgl(req_tgl), .req_line(req_line),
        .done_tgl(done_tgl), .fill(fill), .owns(owns), .en(en), .addr(addr), .rdy(rdy), .rd_valid(rd_valid), .rd_data(rd_data));
    ddr2c_model #(.BASE(32'h01C00000)) m (.ui_clk(ui_clk), .c_addr(addr), .c_cmd(3'b001), .c_en(en), .c_rdy(rdy),
        .c_wdf_data(128'd0), .c_wdf_mask(16'hFFFF), .c_wdf_wren(1'b0), .c_rd_data(rd_data), .c_rd_valid(rd_valid));

    integer gen = 0;                     // image generation (the app rewrites the image: 1)
    function [7:0] img(input [16:0] a);
        img = a[7:0] * 8'd37 + a[15:8] * 8'd11 + a[16] * 8'd101 + 8'd5 + gen[7:0] * 8'd77;
    endfunction
    task load_image;
        integer n, k;
        reg [127:0] w;
        begin
            for (n = 0; n < 4608; n = n + 1) begin
                for (k = 0; k < 16; k = k + 1) w[8 * k +: 8] = img(n * 16 + k);
                m.mem[n] = w;
            end
        end
    endtask

    integer errs = 0, reads = 0, stall_total = 0, maxstall = 0, cyc = 0;
    always @(posedge clk) cyc <= cyc + 1;
    reg probe_missed = 0;                // rmiss was seen (and rvalid was not) for a first read of a line
    integer both = 0, none = 0;          // answers with both flags, answers with neither
    reg [15:0] last_word;

    // one read as the CPU does it: present, look at the answer in the next clock; on a miss repeat 32 clocks after the first try. Returns the clocks from
    // the first try to the word (1 = a hit at once).
    task rd(input [16:0] byte_addr, output integer stall);
        reg [15:0] wd;
        reg [15:0] exp;
        integer tries, tstart;
        reg got;
        begin
            wd = byte_addr[16:1];
            exp = {img({byte_addr[16:1], 1'b1}), img({byte_addr[16:1], 1'b0})};
            tries = 0; got = 0; stall = 0;
            tstart = cyc;
            while (!got) begin
                @(posedge clk); #1;
                cmd_valid = 1; cmd_word = wd;
                @(posedge clk); #1;           // the command was accepted at this edge, the answer is there now
                cmd_valid = 0;
                if (rvalid && rmiss) both = both + 1;
                if (!rvalid && !rmiss) none = none + 1;
                if (rvalid) begin
                    got = 1;
                    stall = (cyc - tstart) - 1;
                    reads = reads + 1;
                    if (rdata !== exp) begin
                        errs = errs + 1;
                        if (errs < 10) $display("FAIL: read %h got %h want %h (after %0d tries)", byte_addr, rdata, exp, tries);
                    end
                end else begin
                    if (tries == 0) probe_missed = 1;
                    tries = tries + 1;
                    if (tries > 40) begin $display("FAIL: no word for %h", byte_addr); errs = errs + 1; got = 1; stall = 0; end
                    repeat (30) @(posedge clk);
                end
            end
        end
    endtask

    integer st, i, j, k, bank, off, nm0, nh0, period, total_clocks, t_start, seed = 7;
    reg [16:0] a;
    // a small program model: loops in several banks + table reads in the character ROM banks
    reg [16:0] loop_base [0:7];
    integer    loop_len  [0:7];
    task run_pattern(input integer n_iter, input integer per);
        integer it, l, p, s, stall_sum, nrd;
        begin
            stall_sum = 0; nrd = 0;
            t_start = cyc;
            for (it = 0; it < n_iter; it = it + 1) begin
                l = {$random(seed)} % 8;
                for (p = 0; p < loop_len[l]; p = p + 2) begin
                    rd(loop_base[l] + p, s); stall_sum = stall_sum + s - 1; nrd = nrd + 1;
                    repeat (per - s - 1 > 0 ? per - s - 1 : 0) @(posedge clk);
                    if (p % 16 == 6) begin   // a data read: character ROM table
                        rd(17'h10000 + ({$random(seed)} % 4096), s); stall_sum = stall_sum + s - 1; nrd = nrd + 1;
                        repeat (per - s - 1 > 0 ? per - s - 1 : 0) @(posedge clk);
                    end
                end
            end
            total_clocks = cyc - t_start;
            $display("   pattern per=%0d: %0d reads, CPU held %0d clocks of %0d (%0d.%0d %%), misses %0d hits %0d", per, nrd, stall_sum, total_clocks,
                     stall_sum * 100 / total_clocks, (stall_sum * 1000 / total_clocks) % 10, n_miss - nm0, n_hit - nh0);
        end
    endtask

    initial begin
        load_image;
        loop_base[0] = {4'd14, 12'h000}  << 0; loop_len[0] = 400;    // kernal bank 14
        loop_base[1] = {4'd15, 12'h100};       loop_len[1] = 300;    // kernal bank 15
        loop_base[2] = {4'd4,  12'h000};       loop_len[2] = 600;    // BASIC banks
        loop_base[3] = {4'd5,  12'h400};       loop_len[3] = 500;
        loop_base[4] = {4'd6,  12'h800};       loop_len[4] = 500;
        loop_base[5] = {4'd9,  12'h200};       loop_len[5] = 800;
        loop_base[6] = {4'd1,  12'h000};       loop_len[6] = 200;    // editor
        loop_base[7] = {4'd12, 12'hC00};       loop_len[7] = 300;
        repeat (4) @(posedge clk);
        flush = 1; @(posedge clk); #1 flush = 0;
        repeat (20) @(posedge clk);
        $display("== flush     the walker runs, lookups wait for it");
        rd(17'h00100, st);
        $display("   first read after flush: stall %0d clocks", st);

        $display("== hit/miss  a miss reports it at once and the CPU repeats it, the same line again hits");
        nm0 = n_miss; nh0 = n_hit; probe_missed = 0;
        rd(17'h01230, st); if (st < 10) begin errs = errs + 1; $display("FAIL: miss answered in %0d clocks", st); end
        $display("   miss: %0d clocks until the word (%0d..%0d ns of DDR2), longest fetch %0d clocks", st, 293, 693, fill_max);
        if (n_miss != nm0 + 1) begin errs = errs + 1; $display("FAIL: miss count"); end
        if (!probe_missed) begin errs = errs + 1; $display("FAIL: the first read of a line did not report a miss"); end
        if (!cmd_ready) begin errs = errs + 1; $display("FAIL: cmd_ready low"); end
        rd(17'h01232, st); if (st != 1) begin errs = errs + 1; $display("FAIL: hit took %0d clocks", st); end
        rd(17'h0123E, st); if (st != 1) begin errs = errs + 1; $display("FAIL: hit (last word of the line) took %0d clocks", st); end
        if (n_miss != nm0 + 1) begin errs = errs + 1; $display("FAIL: a hit asked the DDR2"); end

        $display("== overlap   a lookup of another line while a fetch is open only reports the miss (one fetch at a time)");
        nm0 = n_miss;
        @(posedge clk); #1 cmd_valid = 1; cmd_word = 16'h4000 >> 1;       // a line that is not cached
        @(posedge clk); #1 cmd_valid = 0;
        if (!rmiss) begin errs = errs + 1; $display("FAIL: no miss for A"); end
        @(posedge clk); #1 cmd_valid = 1; cmd_word = 16'h6000 >> 1;       // another one, a clock later
        @(posedge clk); #1 cmd_valid = 0;
        if (!rmiss || rvalid) begin errs = errs + 1; $display("FAIL: B while A is open: rmiss %b rvalid %b", rmiss, rvalid); end
        if (!fill_busy) begin errs = errs + 1; $display("FAIL: fill_busy low while a fetch is open"); end
        if (n_miss != nm0 + 1) begin errs = errs + 1; $display("FAIL: B started a second fetch (%0d)", n_miss - nm0); end
        repeat (200) @(posedge clk);
        if (fill_busy) begin errs = errs + 1; $display("FAIL: the fetch did not end"); end
        rd(17'h4000, st); rd(17'h6000, st);                              // both there after their retries
        if (both != 0 || none != 0) begin errs = errs + 1; $display("FAIL: %0d answers with both flags, %0d with none", both, none); end
        $display("   answers with both flags %0d, with neither %0d; CPU cycles held so far %0d", both, none, n_wait);

        $display("== banks     the same offset in all 18 banks, A/B/A/B across banks, conflicting lines");
        for (bank = 0; bank < 18; bank = bank + 1) begin
            rd({bank[4:0], 12'h2A4}, st);
        end
        for (j = 0; j < 4; j = j + 1)
            for (bank = 0; bank < 18; bank = bank + 1) rd({bank[4:0], 12'h2A4}, st);     // some hit, some conflict-miss
        for (j = 0; j < 20; j = j + 1) begin rd(17'h02000, st); rd(17'h0A000 + 17'h2000 * 0, st); rd(17'h11000, st); end
        // same index, different tag (line numbers differ by 0x200 in the low bits xor'ed with the high bits)
        for (j = 0; j < 20; j = j + 1) begin rd(17'h03010, st); rd(17'h03010 + 17'h2000, st); rd(17'h03010 + 17'h4000, st); end

        $display("== flush2    the app rewrites the image (generation 1); a cached line stays, a flush drops it");
        rd(17'h01230, st);                       // cached
        gen = 1; load_image;
        gen = 0;                                 // the cache still holds generation 0: reads must match 0
        rd(17'h01230, st);
        gen = 1;
        flush = 1; @(posedge clk); #1 flush = 0;
        repeat (3) @(posedge clk);
        rd(17'h01230, st);                       // after the flush: generation 1 expected
        rd(17'h05000, st);

        $display("== pattern   8502-like reads, 1 MHz (every 32 clocks) and 2 MHz (every 16)");
        nm0 = n_miss; nh0 = n_hit; run_pattern(150, 32);
        nm0 = n_miss; nh0 = n_hit; run_pattern(150, 16);
        nm0 = n_miss; nh0 = n_hit; run_pattern(150, 32);

        $display("== result    %0d reads, %0d errors", reads, errs);
        if (errs == 0) $display("PASS tb_romc"); else $display("FAIL tb_romc");
        $finish;
    end
    initial begin #900000000; $display("FAIL tb_romc (timeout)"); $finish; end
endmodule
