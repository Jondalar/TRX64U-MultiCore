// T170: the serial port's fast serial wiring and its trace. Three parts, one bench:
//   1. c128_iecfs against the MEGA65 core's equations (fpga64_sid_iec.vhd, active-high "n" signals as written there), every input combination
//   2. iec_bus_c128: SRQ from the board reaches the core and not drive A; the core's SRQ pull reaches the port; the port off releases it
//   3. c128_iectrace through its register page: events with their deltas, a burst that the ring leaves out (the counters do not), a wrap of the ring,
//      the snapshot, the clear
`timescale 1ns / 1ps
module tb_iec;
    reg sys_clk = 0, cpu_clk = 0;
    always #5 sys_clk = ~sys_clk;
    always #3.5 cpu_clk = ~cpu_clk;
    integer nbad = 0;
    task chk(input [511:0] what, input [63:0] got, input [63:0] want);
        begin
            if (got !== want) begin $display("FAIL %0s: %0h, want %0h", what, got, want); nbad = nbad + 1; end
            else $display("ok   %0s", what);
        end
    endtask

    // ---- 1. c128_iecfs --------------------------------------------------------------------------------------------------
    reg  [6:0] v = 7'd0;
    wire fs_dir = v[0], c128_n = v[1], srq_bus = v[2], data_bus = v[3], cnt_out = v[4], sp_out = v[5], tape_read = v[6];
    wire cnt_in, sp_in, flag_n, srq_out, data_gate;
    c128_iecfs fs (.fs_dir(fs_dir), .c128_n(c128_n), .srq_bus(srq_bus), .data_bus(data_bus), .cnt_out(cnt_out), .sp_out(sp_out), .tape_read(tape_read),
                   .cnt_in(cnt_in), .sp_in(sp_in), .flag_n(flag_n), .srq_out(srq_out), .data_gate(data_gate));
    // the reference, in the names and polarity of fpga64_sid_iec.vhd:1189-1202 (fs_o = fsdir_n; fs_i = not (fsdir_n or mmu_c128_n))
    reg fs_o, fs_i, fclk_o, r_cnt_i, r_sp_i, r_flag, r_data_gate;
    integer k, bad1;
    task part1;
        begin
            bad1 = 0;
            for (k = 0; k < 128; k = k + 1) begin
                v = k[6:0]; #1;
                fs_o = fs_dir; fclk_o = cnt_out | ~fs_o; fs_i = ~(fs_o | c128_n);
                r_cnt_i = (srq_bus | ~fs_i) & 1'b1; r_sp_i = (data_bus | ~fs_i) & 1'b1;
                r_flag = srq_bus & fclk_o & tape_read; r_data_gate = sp_out | ~fs_o;
                if (cnt_in !== r_cnt_i || sp_in !== r_sp_i || flag_n !== r_flag || srq_out !== fclk_o || data_gate !== r_data_gate) bad1 = bad1 + 1;
            end
            chk("iecfs: 128 input combinations against the MEGA65 equations", bad1, 0);
            // the reset state (FSDIR = 1, CIA idle: CNT, SP high): nothing reaches the bus, nothing comes in
            v = 7'b1110001; #1;   // tape 1, sp 1, cnt 1, data 0, srq 0 (a drive pulling both), c128 mode, fsdir 1
            chk("iecfs: sending: CNT/SP in stay high whatever the bus does", {cnt_in, sp_in}, 2'b11);
            v = 7'b1110000; #1;   // FSDIR 0, C128 mode, bus SRQ 0, DATA 0
            chk("iecfs: receiving in C128 mode: the bus is CNT and SP", {cnt_in, sp_in}, 2'b00);
            v = 7'b1110010; #1;   // FSDIR 0 but C64 mode
            chk("iecfs: receiving, C64 mode: CNT/SP stay high", {cnt_in, sp_in}, 2'b11);
            v = 7'b1000001; #1;   // sending, CNT low, SP low
            chk("iecfs: sending: CNT low pulls SRQ, SP low pulls DATA", {srq_out, data_gate}, 2'b00);
        end
    endtask

    // ---- 2. iec_bus_c128 ------------------------------------------------------------------------------------------------
    reg c_atn = 1, c_clk = 1, c_dat = 1, c_srq = 1, d_clk = 1, d_dat = 1, p_off = 0;
    reg p_atn_i = 0, p_clk_i = 0, p_dat_i = 0, p_srq_i = 0;           // 1 = the line is low
    wire ci_atn, ci_clk, ci_dat, ci_srq, di_atn, di_clk, di_dat, po_atn, po_clk, po_dat, po_srq;
    wire [3:0] tap_bus, tap_port;
    iec_bus_c128 bus (.clk(sys_clk), .c64_atn_o(c_atn), .c64_clk_o(c_clk), .c64_data_o(c_dat), .c64_srq_o(c_srq),
                      .c64_atn_i(ci_atn), .c64_clk_i(ci_clk), .c64_data_i(ci_dat), .c64_srq_i(ci_srq),
                      .cpu_clk(cpu_clk), .drv_clk_o(d_clk), .drv_data_o(d_dat), .drv_atn_i(di_atn), .drv_clk_i(di_clk), .drv_data_i(di_dat),
                      .port_off(p_off), .port_atn_i(p_atn_i), .port_clk_i(p_clk_i), .port_data_i(p_dat_i), .port_srq_i(p_srq_i),
                      .port_atn_o(po_atn), .port_clk_o(po_clk), .port_data_o(po_dat), .port_srq_o(po_srq), .tap_bus(tap_bus), .tap_port(tap_port));
    task part2;
        begin
            repeat (8) @(posedge sys_clk);
            chk("bus: idle, every line high to the core", {ci_srq, ci_dat, ci_clk, ci_atn}, 4'hF);
            chk("bus: idle, the port is released", {po_srq, po_dat, po_clk, po_atn}, 4'h0);
            p_srq_i = 1; repeat (8) @(posedge sys_clk);
            chk("bus: a drive pulls SRQ: the core hears it", ci_srq, 1'b0);
            chk("bus: ... and the tap shows the port's SRQ low", tap_port[3], 1'b0);
            chk("bus: ... DATA, CLK and ATN are untouched", {ci_dat, ci_clk, ci_atn}, 3'b111);
            p_srq_i = 0; repeat (8) @(posedge sys_clk);
            chk("bus: released again", ci_srq, 1'b1);
            c_srq = 0; repeat (12) @(posedge cpu_clk);
            chk("bus: the core's CNT pulls SRQ: the port output is 1 (pull)", po_srq, 1'b1);
            chk("bus: ... and none of the other port lines", {po_dat, po_clk, po_atn}, 3'b000);
            chk("bus: ... drive A sees no SRQ (its inputs stay high)", {di_atn, di_clk, di_dat}, 3'b111);
            p_off = 1; repeat (12) @(posedge cpu_clk);
            chk("bus: the port off releases SRQ", po_srq, 1'b0);
            p_off = 0; c_srq = 1; repeat (12) @(posedge cpu_clk);
            chk("bus: the core releases: the port output is 0", po_srq, 1'b0);
            // the existing lines as before: DATA of a drive on the port reaches the core and drive A, the core's ATN reaches the port
            p_dat_i = 1; c_atn = 0; repeat (12) @(posedge sys_clk);
            chk("bus: board DATA low reaches the core", ci_dat, 1'b0);
            chk("bus: the core's ATN reaches the port and drive A", {po_atn, di_atn}, 2'b10);
            p_dat_i = 0; c_atn = 1; repeat (12) @(posedge sys_clk);
        end
    endtask

    // ---- 3. c128_iectrace -----------------------------------------------------------------------------------------------
    reg  [3:0] t_bus = 4'hF, t_port = 4'hF;
    reg        t_fs = 1'b1, t_mode = 1'b1;
    reg        we = 0, re = 0;
    reg [16:0] addr = 17'd0;
    reg  [7:0] wd = 8'd0;
    wire [7:0] rdata;
    wire       ack;
    c128_iectrace tr (.sys_clk(sys_clk), .bus(t_bus), .port(t_port), .fs_dir(t_fs), .c128_mode(t_mode), .tick_hz(32'd31527778),
                      .clk(cpu_clk), .we(we), .re(re), .addr(addr), .wdata(wd), .rdata(rdata), .ack(ack));
    task wr(input [11:0] off, input [7:0] d);
        begin @(posedge cpu_clk); #1 we = 1; addr = {5'b0_1111, off}; wd = d; @(posedge cpu_clk); #1 we = 0; end
    endtask
    reg [7:0] rb;
    task rd8(input [11:0] off);
        begin @(posedge cpu_clk); #1 re = 1; addr = {5'b0_1111, off}; @(posedge cpu_clk); #1 re = 0; @(posedge cpu_clk); #1 rb = rdata; end
    endtask
    reg [31:0] w32;
    task rd32(input [11:0] off);
        begin rd8(off); w32[7:0] = rb; rd8(off + 1); w32[15:8] = rb; rd8(off + 2); w32[23:16] = rb; rd8(off + 3); w32[31:24] = rb; end
    endtask
    task snap;
        begin wr(12'h010, 8'h02); repeat (3) @(posedge cpu_clk); rd8(12'h010); while (rb[0]) rd8(12'h010); end
    endtask
    reg [31:0] ev;
    task slot(input [9:0] n);
        begin wr(12'h030, n[7:0]); wr(12'h031, {6'd0, n[9:8]}); repeat (3) @(posedge cpu_clk); rd32(12'h034); ev = w32; end
    endtask
    integer i, tot0;
    reg [31:0] total;
    task part3;
        begin
            rd32(12'h000); chk("trace: ID is IECT", w32, {"T", "C", "E", "I"});
            rd32(12'h008); chk("trace: TICK_HZ", w32, 32'd31527778);
            wr(12'h010, 8'h01); repeat (6) @(posedge sys_clk); snap; rd32(12'h014); chk("trace: after the clear EV_TOTAL is 0", w32, 0);
            // ATN falls: the first event, the delta saturated
            repeat (20) @(posedge sys_clk); #1 t_bus = 4'hE;
            repeat (100) @(posedge sys_clk); #1 t_bus = 4'hC;     // CLK falls 100 ticks later
            repeat (50) @(posedge sys_clk);
            snap; rd32(12'h014); chk("trace: two events", w32, 2);
            rd32(12'h018); chk("trace: LIVE is the state now", w32[15:0], 16'h03FC);
            slot(0); chk("trace: event 0 state (ATN low)", ev[9:0], 10'h3FE); chk("trace: event 0 delta saturated", ev[31:16], 16'hFFFF);
            slot(1); chk("trace: event 1 state (CLK low too)", ev[9:0], 10'h3FC);
            if (ev[31:16] < 98 || ev[31:16] > 102) begin $display("FAIL trace: event 1 delta %0d, want about 100", ev[31:16]); nbad = nbad + 1; end
            else $display("ok   trace: event 1 delta %0d (about 100 ticks)", ev[31:16]);
            rd32(12'h020); chk("trace: CNT_ATN 1", w32, 1);
            rd32(12'h024); chk("trace: CNT_CLK 1", w32, 1);
            // FSDIR goes to 0 (input), then a burst: SRQ and DATA move every 8 ticks, 200 edges each
            repeat (30) @(posedge sys_clk); #1 t_fs = 1'b0;
            repeat (30) @(posedge sys_clk);
            snap; rd32(12'h014); tot0 = w32;
            for (i = 0; i < 200; i = i + 1) begin
                repeat (8) @(posedge sys_clk); #1 t_bus[3] = ~t_bus[3]; t_bus[2] = ~t_bus[2]; t_port[3] = ~t_port[3]; t_port[2] = ~t_port[2];
            end
            repeat (100) @(posedge sys_clk);
            snap; rd32(12'h014); chk("trace: 100 ticks after the burst only its first edge is an event", w32 - tot0, 1);
            repeat (300) @(posedge sys_clk);
            snap; rd32(12'h014); chk("trace: 200 SRQ/DATA edges made two events (first edge, state after the quiet)", w32 - tot0, 2);
            rd32(12'h02C); chk("trace: CNT_SRQ counts every edge of the burst", w32, 200);
            rd32(12'h028); chk("trace: CNT_DATA counts every edge of the burst", w32, 200);
            // a wrap: 1500 ATN toggles 100 ticks apart; the newest slot holds the last one
            snap; rd32(12'h014); tot0 = w32;
            for (i = 0; i < 1500; i = i + 1) begin repeat (100) @(posedge sys_clk); #1 t_bus[0] = ~t_bus[0]; end
            repeat (50) @(posedge sys_clk);
            snap; rd32(12'h014); total = w32;
            chk("trace: 1500 more events", total - tot0, 1500);
            slot((total - 1) & 10'h3FF);
            chk("trace: the newest slot holds the last state (ATN as set)", ev[0], t_bus[0]);
            if (ev[31:16] < 98 || ev[31:16] > 102) begin $display("FAIL trace: newest delta %0d", ev[31:16]); nbad = nbad + 1; end
            else $display("ok   trace: newest event's delta %0d", ev[31:16]);
            slot((total - 1000) & 10'h3FF);
            if (ev[31:16] < 98 || ev[31:16] > 102) begin $display("FAIL trace: slot 1000 back: %0h", ev); nbad = nbad + 1; end
            else $display("ok   trace: an event 1000 back is still there (delta %0d)", ev[31:16]);
            // the clear
            wr(12'h010, 8'h01); repeat (6) @(posedge sys_clk); snap;
            rd32(12'h014); chk("trace: the clear zeroes EV_TOTAL", w32, 0);
            rd32(12'h02C); chk("trace: the clear zeroes CNT_SRQ", w32, 0);
        end
    endtask

    initial begin
        part1;
        part2;
        part3;
        if (nbad == 0) $display("PASS tb_iec"); else $display("FAIL tb_iec: %0d wrong", nbad);
        $finish;
    end
    initial begin #50_000_000; $display("FAIL tb_iec: timeout"); $finish; end
endmodule
