// T170: CIA 1's serial register on the fast serial wiring (c128_iecfs.v), the reDIP CIA of the core (vendor/icepi/c64_redip_cia.v) behind it.
//   receive: a 1571 stand-in sends 0xA5 MSB first on the bus's DATA with SRQ as the clock (FSDIR = 0, C128 mode); SDR must read 0xA5, ICR bit 3 (SP) and
//            bit 4 (FLAG: SRQ falls) must be set
//   send:    FSDIR = 1, Timer A running, SDR written with 0x5A: CNT drives SRQ (eight pulses), SP drives DATA; a receiver sampling DATA on SRQ's rising edge
//            reads 0x5A MSB first
//   idle:    FSDIR = 1 and nothing started: SRQ and the DATA gate stay released
// The bus polarity is the core's: 1 = high / released. Verilator (--binary), see run_iec_cia.sh.
`timescale 1ns / 1ps
module tb_iec_cia;
    reg clk = 0;
    always #5 clk = ~clk;
    reg [4:0] ph = 5'd0;                       // one CIA phi2 = 32 clocks (31.5 MHz / 32 = 0.985 MHz)
    always @(posedge clk) ph <= ph + 5'd1;
    wire clk_phi = ph < 5'd16;
    wire phi2_p = ph == 5'd0, phi2_n = ph == 5'd16;
    reg reset = 1'b1;
    reg cen = 1'b1, we = 1'b0;
    reg [3:0] addr = 4'd0;
    reg [7:0] din = 8'd0;
    wire [7:0] dout, pa_out, ddra, pb_out, ddrb;
    wire irq, sp_out, cnt_out;
    reg fs_dir = 1'b1, c128_n = 1'b0;
    reg srq_dev = 1'b1, data_dev = 1'b1;       // what the device on the bus pulls (1 = released)
    wire cnt_in, sp_in, flag_n, srq_out, data_gate;
    // the bus: the wired AND of the device and of this machine's own pulls
    wire srq_bus = srq_dev & srq_out;
    wire data_bus = data_dev & data_gate;
    c128_iecfs fs (.fs_dir(fs_dir), .c128_n(c128_n), .srq_bus(srq_bus), .data_bus(data_bus), .cnt_out(cnt_out), .sp_out(sp_out), .tape_read(1'b1),
                   .cnt_in(cnt_in), .sp_in(sp_in), .flag_n(flag_n), .srq_out(srq_out), .data_gate(data_gate));
    c64_redip_cia #(.GENERATE_TOD(1), .CLK_FREQUENCY(31_527_778)) cia (
        .clk(clk), .reset(reset), .clk_phi(clk_phi), .phi2_p(phi2_p), .phi2_n(phi2_n),
        .cen(cen), .we(we), .addr(addr), .din(din), .dout(dout),
        .pa_in(8'hFF), .pa_out(pa_out), .ddra(ddra), .pb_in(8'hFF), .pb_out(pb_out), .ddrb(ddrb),
        .sp_in(sp_in), .sp_out(sp_out), .cnt_in(cnt_in), .cnt_out(cnt_out), .flag_n(flag_n), .pc_n(), .irq(irq),
        .tod_out(), .tod_in(1'b0), .cia_model(1'b0), .clk_freq(25'd31527778), .tod60(1'b0),
        .sb_regs(), .sb_ld(1'b0), .sb_stg(363'd0), .sb_cur());
    integer nbad = 0;
    task chk(input [255:0] what, input [31:0] got, input [31:0] want);
        begin
            if (got !== want) begin $display("FAIL %0s: %0h, want %0h", what, got, want); nbad = nbad + 1; end
            else $display("ok   %0s", what);
        end
    endtask
    // a register access spans one phi2 high phase: set up while phi2 is low, hold through the next high phase, release
    task ciawr(input [3:0] a, input [7:0] d);
        begin
            wait (ph == 5'd20); @(posedge clk); #1 cen = 0; we = 1; addr = a; din = d;
            wait (ph == 5'd18); @(posedge clk); #1 cen = 1; we = 0;      // 18 of the next period: phi2 was high 0..15
        end
    endtask
    reg [7:0] rv;
    task ciard(input [3:0] a);
        begin
            wait (ph == 5'd20); @(posedge clk); #1 cen = 0; we = 0; addr = a;
            wait (ph == 5'd10); @(posedge clk); #1 rv = dout;
            wait (ph == 5'd18); @(posedge clk); #1 cen = 1;
        end
    endtask
    // the receiver of the send test: DATA at each rising edge of SRQ
    reg [7:0] got = 8'd0;
    integer nrise = 0;
    reg srq_q = 1'b1;
    always @(posedge clk) begin
        srq_q <= srq_bus;
        if (srq_bus && !srq_q && test_send) begin got <= {got[6:0], data_bus}; nrise <= nrise + 1; end
    end
    reg test_send = 1'b0;
    integer i;
    initial begin
        repeat (10) @(posedge clk); #1 reset = 0;
        repeat (40) @(posedge clk);
        // idle, FSDIR = 1
        chk("idle: SRQ released", srq_out, 1);
        chk("idle: DATA gate released", data_gate, 1);
        // receive
        fs_dir = 1'b0;
        ciawr(4'hD, 8'h88);                                       // ICR mask: SP interrupt on
        ciawr(4'hE, 8'h00);                                       // CRA: serial port input
        for (i = 7; i >= 0; i = i - 1) begin
            data_dev = (8'hA5 >> i) & 1'b1;
            repeat (100) @(posedge clk); #1 srq_dev = 1'b0;       // SRQ low: FLAG sees a falling edge
            repeat (100) @(posedge clk); #1 srq_dev = 1'b1;       // SRQ rising: the CIA shifts DATA in
            repeat (200) @(posedge clk);
        end
        data_dev = 1'b1;
        repeat (300) @(posedge clk);
        ciard(4'hC); chk("receive: SDR holds the byte the stand-in sent", rv, 8'hA5);
        ciard(4'hD); chk("receive: ICR has IR, FLAG (SRQ fell) and SP (a byte arrived)", rv, 8'h98);
        // FSDIR in C64 mode: the bus does not reach CIA 1
        c128_n = 1'b1; fs_dir = 1'b0;
        srq_dev = 1'b0; data_dev = 1'b0; #20;
        chk("C64 mode, FSDIR in: CNT and SP in stay high whatever the bus does", {cnt_in, sp_in}, 2'b11);
        srq_dev = 1'b1; data_dev = 1'b1; fs_dir = 1'b0; #20;
        c128_n = 1'b0;
        // send
        fs_dir = 1'b1;
        repeat (100) @(posedge clk);
        ciawr(4'h4, 8'h02); ciawr(4'h5, 8'h00);                   // Timer A latch 2
        test_send = 1'b1;
        ciawr(4'hE, 8'h41);                                       // CRA: start, serial port output, continuous
        ciawr(4'hC, 8'h5A);                                       // SDR: the byte
        repeat (3000) @(posedge clk);
        chk("send: eight SRQ rising edges", nrise, 8);
        chk("send: the receiver read 0x5A, MSB first", got, 8'h5A);
        if (nbad == 0) $display("PASS tb_iec_cia"); else $display("FAIL tb_iec_cia: %0d wrong", nbad);
        $finish;
    end
    initial begin #20_000_000; $display("FAIL tb_iec_cia: timeout"); $finish; end
endmodule
