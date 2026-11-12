// mode_ctl (T40) with cdce_i2c against a model of the MMCM's DRP and lock, the
// machine halt and the I2C parking:
//   - every switch: the DRP writes only while the MMCM is held in reset, every
//     table register ends with its value and the kept bits as they were, the
//     other registers untouched; sys stopped (sys_en) and the lock loss kept
//     from sys_rst (reconf) for the whole time the MMCM is unlocked; the halt
//     and both parkings around it; vmode at the end only;
//   - the synthesizer bytes cdce_i2c sends after each switch (decoded from the
//     bus): 50 Hz 03 70 33 ED, 60 Hz 3D E0 F3 CD in registers 0x18-0x1B;
//   - the read-back check (mism) stays clear when the model starts with PAL's
//     table values.
//   iverilog -g2012 -o m tb_mode_ctl.v ../../mode_ctl.v ../../cdce_i2c.v && vvp m
`timescale 1ns/1ps
module tb;
    reg clk = 0;
    always #10 clk = ~clk;                      // 50 MHz
    reg por = 1;
    reg [7:0] fmt = 8'h04;                      // PAL | RGB

    // ---- MMCM model ---------------------------------------------------------------
    wire mrst, den, dwe; wire [6:0] daddr; wire [15:0] di;
    reg  [15:0] dout = 0; reg drdy = 0;
    reg  [15:0] regs [0:127];
    reg  [15:0] prev_r [0:127];
    reg  [2:0]  dcnt = 0; reg dpend = 0, dwr = 0; reg [6:0] da; reg [15:0] dd;
    integer errors = 0, i;
    always @(posedge clk) begin
        drdy <= 1'b0;
        if (den) begin
            if (dpend) begin errors = errors + 1; $display("DEN while busy"); end
            if (dwe && !mrst) begin errors = errors + 1; $display("DRP write without MMCM reset"); end
            dpend <= 1'b1; dwr <= dwe; da <= daddr; dd <= di; dcnt <= 3'd4;
        end else if (dpend) begin
            if (dcnt == 0) begin
                dpend <= 1'b0; drdy <= 1'b1;
                if (dwr) regs[da] <= dd; else dout <= regs[da];
            end else dcnt <= dcnt - 1;
        end
    end
    reg sys_locked = 0, ref_locked = 0;
    integer lk = 0, rk = 0;
    always @(posedge clk) begin
        if (mrst || por) begin sys_locked <= 0; lk = 0; end
        else if (lk < 600) lk = lk + 1; else sys_locked <= 1;
        if (!sys_locked) begin ref_locked <= 0; rk = 0; end
        else if (rk < 300) rk = rk + 1; else ref_locked <= 1;
    end
    wire clocks_ok = sys_locked & ref_locked;

    // ---- the rest of the board around it -------------------------------------------
    wire halt_req, i2c_hold, kb_pause, cdce_v60, sys_en, reconf;
    wire [1:0] vmode; wire [31:0] status, status2;
    reg halted = 0, kb_parked = 0;
    always @(posedge clk) begin halted <= halt_req; kb_parked <= kb_pause; end
    wire scl_low, sda_low, i2c_done;
    cdce_i2c cdce (.clk(clk), .rst(por | ~clocks_ok), .scl_low(scl_low), .sda_low(sda_low),
                   .done(i2c_done), .v60(cdce_v60));
    mode_ctl dut (
        .clk(clk), .rst(por), .fmt(fmt), .halt_req(halt_req), .halted(halted), .in_reset(1'b0),
        .i2c_hold(i2c_hold), .i2c_parked(1'b1), .kb_pause(kb_pause), .kb_parked(kb_parked),
        .i2c_done(i2c_done), .cdce_v60(cdce_v60), .sys_en(sys_en), .reconf(reconf),
        .mmcm_rst(mrst), .daddr(daddr), .di(di), .dout(dout), .den(den), .dwe(dwe), .drdy(drdy),
        .sys_locked(sys_locked), .ref_locked(ref_locked), .vmode(vmode), .status(status),
        .status2(status2));

    // sys_rst as top.v makes it: no reset of the sys domain after power-on
    reg booted = 0;
    always @(posedge clk) if (clocks_ok) booted <= 1;
    always @(posedge clk) if (booted && !clocks_ok && !reconf) begin
        errors = errors + 1; if (errors < 4) $display("sys_rst would fire at %t st %0d lk %0d rk %0d", $time, dut.st, lk, rk);
    end
    // sys must be stopped whenever the MMCM is not locked; the halt and the
    // parkings must stand while it is
    always @(posedge clk) if (!por && dut.st != 0 && !clocks_ok) begin
        if (sys_en) begin errors = errors + 1; $display("sys runs unlocked at %t", $time); end
        if (!halt_req || !i2c_hold || !kb_pause) begin errors = errors + 1; $display("not halted/parked at %t", $time); end
    end

    // ---- I2C monitor: the bytes of every transaction ---------------------------------
    wire scl = !scl_low, sda = !sda_low;
    reg scl_d = 1, sda_d = 1; integer nb = 0, nbit = 0; reg [8:0] sh = 0;
    reg [7:0] got [0:15];
    always @(posedge clk) begin
        scl_d <= scl; sda_d <= sda;
        if (scl && scl_d && sda_d && !sda) begin nb = 0; nbit = 0; end          // START
        else if (scl && !scl_d) begin                                          // a bit
            sh = {sh[7:0], sda}; nbit = nbit + 1;
            if (nbit == 9) begin if (nb < 16) got[nb] = sh[8:1]; nb = nb + 1; nbit = 0; end
        end
    end

    // ---- the table, as mode_ctl has it ------------------------------------------------
    task check_regs(input [1:0] m);
        integer k; reg [38:0] w;
        begin
            for (k = 0; k < 14; k = k + 1) begin
                w = dut.drp(m, k[3:0]);
                if ((regs[w[38:32]] & ~w[31:16]) != w[15:0] ||
                    (regs[w[38:32]] & w[31:16]) != (prev_r[w[38:32]] & w[31:16])) begin
                    errors = errors + 1;
                    $display("mode %0d reg %h = %h, want %h (keep %h of %h)", m, w[38:32],
                             regs[w[38:32]], w[15:0], w[31:16], prev_r[w[38:32]]);
                end
            end
        end
    endtask
    task snapshot; integer a; begin for (a = 0; a < 128; a = a + 1) prev_r[a] = regs[a]; end endtask
    task others_same(input [1:0] m);
        integer a, k, hit; reg [38:0] w;
        begin
            for (a = 0; a < 128; a = a + 1) begin
                hit = 0;
                for (k = 0; k < 14; k = k + 1) begin w = dut.drp(m, k[3:0]); if (w[38:32] == a) hit = 1; end
                if (!hit && regs[a] != prev_r[a]) begin errors = errors + 1; $display("reg %h changed", a); end
            end
        end
    endtask

    task switch_to(input [7:0] f, input [1:0] m, input v60);
        begin
            snapshot;
            fmt = f;
            wait (dut.st != 0);
            wait (dut.st == 0 && vmode == m);
            @(posedge clk); #1;
            check_regs(m);
            others_same(m);
            if (got[3] !== 8'h57 || got[7] !== (v60 ? 8'h3D : 8'h03) || got[8] !== (v60 ? 8'hE0 : 8'h70) ||
                got[9] !== (v60 ? 8'hF3 : 8'h33) || got[10] !== (v60 ? 8'hCD : 8'hED) || got[0] !== 8'hC8) begin
                errors = errors + 1;
                $display("CDCE bytes %h %h %h .. %h %h %h %h", got[0], got[1], got[2], got[7], got[8], got[9], got[10]);
            end
            $display("mode %0d: %0d us, vmode %0d, cdce %h %h %h %h, mism %b, timeouts %b", m,
                     dut.last_dur / 50, vmode, got[7], got[8], got[9], got[10], status[11], status[10:8]);
        end
    endtask

    initial begin
        for (i = 0; i < 128; i = i + 1) regs[i] = $random;
        for (i = 0; i < 14; i = i + 1)              // the static PAL setting
            regs[dut.drp(0, i) >> 32] = (regs[dut.drp(0, i) >> 32] & dut.drp(0, i) >> 16) | dut.drp(0, i);
        #100 por = 0;
        wait (i2c_done);
        if (got[7] !== 8'h03 || got[10] !== 8'hED) begin errors = errors + 1; $display("boot CDCE bytes"); end
        switch_to(8'h2F, 2'd1, 1);                  // NTSC
        switch_to(8'h2E, 2'd2, 1);                  // PAL-60
        switch_to(8'h05, 2'd3, 0);                  // NTSC-50
        switch_to(8'h04, 2'd0, 0);                  // PAL
        switch_to(8'h0B | 8'h20, 2'd1, 1);          // NTSC again, from PAL
        if (status[11]) begin errors = errors + 1; $display("read-back check fired: %h at %h", status2[31:16], status[6:0]); end
        if (status[29:22] != 5) begin errors = errors + 1; $display("count %0d %0d %h", status[29:22], dut.count, status); end
        if (errors == 0) $display("PASS"); else $display("FAIL %0d", errors);
        $finish;
    end
    initial begin #2000000000; $display("TIMEOUT st %0d", dut.st); $finish; end
endmodule
