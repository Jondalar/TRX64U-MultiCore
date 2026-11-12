// app_i2c and c64u_keyboard on three open-drain buses, muxed as in top.v,
// against register slaves (pointer + auto-increment). The CPU side follows
// Gideon's Hw_I2C_Driver (write a register, poll status bit 7). Every slave
// flags a START/STOP in the middle of a byte (a corrupted transfer), the CDCE
// at 0x64 flags any write that reaches it. T40: hold parks the master between
// two transactions (a command waits with the lines released until it is gone).
`timescale 1ns/1ps

module i2c_slave #(parameter [6:0] ADDR = 7'h50, parameter STRETCH = 0) (
    input  wire clk, input wire scl, input wire sda,
    output reg  sda_pull = 0, output reg scl_pull = 0
);
    reg [7:0] mem [0:255];
    reg [7:0] ptr = 0, sh = 0, txb = 0;
    reg [3:0] bitn = 0;
    reg       active = 0, addressed = 0, rd = 0, mnack = 0, scl_d = 1, sda_d = 1;
    integer   bytes = 0, errors = 0, writes = 0, txn_bytes = 0, txns = 0, i, hold = 0;
    initial for (i = 0; i < 256; i = i + 1) mem[i] = i + 8'h5A;
    always @(posedge clk) begin
        scl_d <= scl; sda_d <= sda;
        if (hold) begin hold = hold - 1; if (hold == 0) scl_pull <= 0; end
        if (scl && scl_d && sda_d && !sda) begin                 // START
            if (active && bitn > 1) begin errors = errors + 1; $display("%m: START at bit %0d", bitn); end
            if (addressed && !rd && txn_bytes >= 1) txns = txns + 1;
            active = 1; addressed = 0; bitn = 0; bytes = 0; txn_bytes = 0; sda_pull <= 0;
        end else if (scl && scl_d && !sda_d && sda) begin        // STOP
            if (active && bitn > 1) begin errors = errors + 1; $display("%m: STOP at bit %0d", bitn); end
            if (addressed && !rd && txn_bytes >= 1) txns = txns + 1;
            active = 0; addressed = 0; bitn = 0; sda_pull <= 0;
        end else if (active && scl && !scl_d) begin              // SCL rises
            if (bitn < 8) sh = {sh[6:0], sda};
            else if (rd && addressed) mnack = sda;
            bitn = bitn + 1;
        end else if (active && !scl && scl_d) begin              // SCL falls
            if (bitn == 8) begin
                if (bytes == 0) begin
                    addressed = (sh[7:1] == ADDR); rd = sh[0]; mnack = 0;
                    sda_pull <= (sh[7:1] == ADDR);
                end else if (addressed && !rd) begin
                    if (bytes == 1) ptr = sh; else begin mem[ptr] = sh; ptr = ptr + 1; writes = writes + 1; end
                    txn_bytes = txn_bytes + 1;
                    sda_pull <= 1;
                end else sda_pull <= 0;
                bytes = bytes + 1;
            end else if (bitn == 9) begin
                bitn = 0;
                sda_pull <= 0;
                if (addressed && rd && !mnack) begin
                    txb = mem[ptr]; ptr = ptr + 1; sda_pull <= ~txb[7];
                end
                if (addressed && STRETCH) begin scl_pull <= 1; hold = STRETCH; end
            end else if (addressed && rd && !mnack && bitn >= 1 && bitn <= 7 && bytes > 0)
                sda_pull <= ~txb[7 - bitn];
        end
    end
endmodule

module tb;
    reg cclk = 0, kclk = 0;
    always #5  cclk = ~cclk;            // 100 MHz, the RISC-V
    always #10 kclk = ~kclk;            // 50 MHz
    reg rst = 1, i2c_done = 0, hold = 0;
    wire parked;
    reg we = 0, re = 0; reg [3:0] addr = 0; reg [7:0] wdata = 0;
    wire [7:0] rdata; wire scan_en, kb_pause, kb_parked, own;
    wire [2:0] a_scl_lo, a_sda_lo;
    wire kb_scl_low, kb_sda_low;
    wire [63:0] keys; wire [15:0] kb_status;

    // buses {3.3 V, HDMI, 1.8 V}: open drain, top.v's mux
    wire [2:0] scl_lo = own ? a_scl_lo : {i2c_done & kb_scl_low, kb_scl_low & i2c_done, kb_scl_low & i2c_done};
    wire [2:0] sda_lo = own ? a_sda_lo : {i2c_done & kb_sda_low, kb_sda_low & i2c_done, kb_sda_low & i2c_done};
    wire p_kbd, p_codec, p_cdce0, p_cdce1, p_edid, p_hub, s_hub;
    wire [2:0] scl = ~(scl_lo | {s_hub, 2'b00});
    wire [2:0] sda = ~(sda_lo | {p_hub, p_edid | p_cdce1, p_kbd | p_codec | p_cdce0});

    app_i2c #(.DIV(250)) dut (.clk(cclk), .rst(rst), .we(we), .re(re), .addr(addr), .wdata(wdata),
        .rdata(rdata), .scan_en(scan_en), .i2c_done(i2c_done), .kb_pause(kb_pause), .kb_parked(kb_parked),
        .own(own), .scl_lo(a_scl_lo), .sda_lo(a_sda_lo), .scl_in(scl), .sda_in(sda),
        .hold(hold), .parked(parked));
    c64u_keyboard kb (.clk(kclk), .rst(~i2c_done), .sda_in(sda[0] & sda[1] & sda[2]),
        .scl_low(kb_scl_low), .sda_low(kb_sda_low), .keys(keys), .status(kb_status),
        .pause(kb_pause), .parked(kb_parked));

    i2c_slave #(.ADDR(7'h21)) s_kbd   (.clk(cclk), .scl(scl[0]), .sda(sda[0]), .sda_pull(p_kbd), .scl_pull());
    i2c_slave #(.ADDR(7'h1A)) s_codec (.clk(cclk), .scl(scl[0]), .sda(sda[0]), .sda_pull(p_codec), .scl_pull());
    i2c_slave #(.ADDR(7'h64)) s_cdce0 (.clk(cclk), .scl(scl[0]), .sda(sda[0]), .sda_pull(p_cdce0), .scl_pull());
    i2c_slave #(.ADDR(7'h64)) s_cdce1 (.clk(cclk), .scl(scl[1]), .sda(sda[1]), .sda_pull(p_cdce1), .scl_pull());
    i2c_slave #(.ADDR(7'h50)) s_edid  (.clk(cclk), .scl(scl[1]), .sda(sda[1]), .sda_pull(p_edid), .scl_pull());
    i2c_slave #(.ADDR(7'h2C), .STRETCH(3000)) s_hubm (.clk(cclk), .scl(scl[2]), .sda(sda[2]), .sda_pull(p_hub), .scl_pull(s_hub));

    integer errors = 0;
    task wr(input [3:0] a, input [7:0] d); begin
        @(posedge cclk) begin we <= 1; addr <= a; wdata <= d; end
        @(posedge cclk) we <= 0;
    end endtask
    task rd_(input [3:0] a, output [7:0] d); begin
        @(posedge cclk) begin re <= 1; addr <= a; end
        @(posedge cclk) re <= 0;
        #1 d = rdata;
    end endtask
    reg [7:0] st, b;
    task wait_busy; begin st = 8'h80; while (st[7]) rd_(4'd1, st); end endtask
    task send(input [7:0] d, input expect_nack); begin
        wr(4'd0, d); wait_busy;
        if (st[2] != expect_nack) begin errors = errors + 1; $display("ERROR send %02h: status %02h", d, st); end
    end endtask
    task stop; begin wr(4'd3, 0); wait_busy; end endtask
    task restart; begin wr(4'd2, 0); wait_busy; end endtask
    task recv(input ack, output [7:0] d); begin wr(ack ? 4'd5 : 4'd4, 0); wait_busy; rd_(4'd0, d); end endtask
    task check(input [7:0] got, input [7:0] exp, input [8*24-1:0] what); begin
        if (got !== exp) begin errors = errors + 1; $display("ERROR %0s: %02h, expected %02h", what, got, exp); end
    end endtask
    // i2c_read_block as the driver does it: address, register, Sr, address+1, bytes
    task read_block(input [1:0] ch, input [7:0] dev, input [7:0] reg_, input integer n, input [7:0] first);
        integer k; begin
        wr(4'd6, ch); wait_busy;
        send(dev, 0); send(reg_, 0); restart; send(dev | 1, 0);
        for (k = 0; k < n; k = k + 1) begin recv(k != n - 1, b); check(b, first + k, "read byte"); end
        stop;
    end endtask

    integer kb_txns0, t0, cdce_w, cdce_t;
    initial begin
        #100 rst = 0;
        #1000 i2c_done = 1;                         // synthesizers done: the keyboard's turn
        // app constructor: scan_enable = 0 (no claim yet, the keyboard keeps going)
        wr(4'd8, 0);
        #200_000;
        if (own || kb_pause) begin errors = errors + 1; $display("ERROR: claimed without a command"); end
        // EDID-like write and read back on the HDMI bus (channel 0)
        wr(4'd6, 0); wait_busy;
        send(8'hA0, 0); send(8'h10, 0); send(8'h11, 0); send(8'h22, 0); stop;
        check(s_edid.mem[8'h10], 8'h11, "EDID write"); check(s_edid.mem[8'h11], 8'h22, "EDID write");
        read_block(2'd0, 8'hA0, 8'h20, 4, 8'h20 + 8'h5A);
        // CDCE 0x64 on channel 1: write is ACKed but never reaches the bus
        wr(4'd6, 1); wait_busy;
        send(8'hC8, 0); send(8'h14, 0); send(8'h08, 0); send(8'h99, 0); stop;
        // a read from it does go out
        read_block(2'd1, 8'hC8, 8'h00, 1, 8'h00 + 8'h5A);
        if (s_cdce0.writes || s_cdce0.txns) begin errors = errors + 1; $display("ERROR the CDCE was written while closed"); end
        // T115 S9: with CDCE open (+8 b1, scan_enable stays 0) the write reaches the synthesizer (channel 1 = pin index 0, s_cdce0)
        wr(4'd8, 2);
        send(8'hC8, 0); send(8'h14, 0); send(8'h08, 0); send(8'h57, 0); stop;
        check(s_cdce0.mem[8'h15], 8'h57, "CDCE write when open");
        cdce_w = s_cdce0.writes; cdce_t = s_cdce0.txns;
        if (cdce_t == 0) begin errors = errors + 1; $display("ERROR the open CDCE write did not arrive"); end
        // closed again (a scan_enable write with b1 = 0, as Gideon's driver does): blocked as before
        wr(4'd8, 0);
        send(8'hC8, 0); send(8'h14, 0); send(8'h08, 0); send(8'h99, 0); stop;
        check(s_cdce0.mem[8'h15], 8'h57, "CDCE untouched when closed");
        // nobody at 0x77 on channel 2: NACK
        wr(4'd6, 2); wait_busy; send(8'hEE, 1); stop;
        // slave that stretches the clock (30 us after every acknowledge)
        send(8'h58, 0); send(8'h03, 0); send(8'hC3, 0); stop;
        check(s_hubm.mem[3], 8'hC3, "stretched write");
        read_block(2'd2, 8'h58, 8'h03, 1, 8'hC3);
        // the keyboard was parked all along (settle) and has not got far
        // U64Config: enable_scan(true): the keyboard takes over
        wr(4'd8, 1);
        #60_000_000;
        $display("codec writes %0d, keyboard transactions %0d, kb NACKs %0d", s_codec.txns, s_kbd.txns, kb_status[15:8]);
        if (s_codec.txns != 11) begin errors = errors + 1; $display("ERROR codec init incomplete"); end
        // locked accesses in the middle of the scan, several times
        repeat (5) begin
            kb_txns0 = s_kbd.txns;
            #(137_000);
            wr(4'd8, 0);                            // enable_scan(false, true)
            t0 = $time;
            read_block(2'd1, 8'h34, 8'h05, 2, 8'h05 + 8'h5A);
            $display("locked read took %0d us", ($time - t0) / 1000);
            wr(4'd8, 1);
            #(2_000_000);
            if (s_kbd.txns == kb_txns0) begin errors = errors + 1; $display("ERROR keyboard did not resume"); end
        end
        // T40 hold: owned and between transactions -> parked; a command waits
        wr(4'd8, 0);
        read_block(2'd0, 8'hA0, 8'h20, 1, 8'h20 + 8'h5A);
        hold = 1;
        #2000;
        if (!own || !parked) begin errors = errors + 1; $display("ERROR hold: own %b parked %b", own, parked); end
        kb_txns0 = s_edid.bytes;
        fork
            begin wr(4'd0, 8'hA0); wait_busy; end
            begin
                #300_000;
                rd_(4'd1, b);
                if (!b[7] || s_edid.bytes != kb_txns0 || scl_lo != 0 || sda_lo != 0) begin
                    errors = errors + 1; $display("ERROR held command went out: status %02h", b);
                end
                hold = 0;
            end
        join
        if (st[2]) begin errors = errors + 1; $display("ERROR command after hold: status %02h", st); end
        stop;
        wr(4'd8, 1);
        #2_000_000;
        // with scan_enable = 1 a command sees no bus: NACK, nothing on the wire
        kb_txns0 = s_edid.bytes;
        wr(4'd6, 0); send(8'hA0, 1); stop;
        if (own) begin errors = errors + 1; $display("ERROR claimed with scan_enable = 1"); end
        // soft reset clears the channel and scan_enable
        wr(4'd6, 2); wr(4'd7, 1); rd_(4'd6, b); check(b, 0, "channel after reset");
        // soft reset closes the CDCE: a write opened before the reset is blocked after it
        wr(4'd8, 2); wr(4'd7, 1); #200_000;
        wr(4'd6, 1); wait_busy; send(8'hC8, 0); send(8'h14, 0); send(8'h08, 0); send(8'h11, 0); stop;
        check(s_cdce0.mem[8'h15], 8'h57, "CDCE closed by soft reset");
        #1000;
        errors = errors + s_kbd.errors + s_codec.errors + s_cdce0.errors + s_cdce1.errors + s_edid.errors + s_hubm.errors;
        if (s_cdce0.writes != cdce_w || s_cdce1.writes || s_cdce0.txns != cdce_t || s_cdce1.txns) begin
            errors = errors + 1; $display("ERROR the CDCE was written");
        end
        if (kb_status[15:8] != 0) begin errors = errors + 1; $display("ERROR keyboard saw NACKs"); end
        $display("errors %0d: %0s", errors, errors == 0 ? "PASS" : "FAIL");
        $finish;
    end
    initial begin #200_000_000; $display("TIMEOUT FAIL"); $finish; end
endmodule
