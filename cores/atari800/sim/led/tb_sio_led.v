// T159 D-all: trx_sio_led (layer2_atari800.v, cut out by run.sh) with a command frame as the OS and the application's responder make it:
// the command line falls, the OS sends five bytes, the drive answers ACK, a gap of the application's turn-around, COMPLETE and 128 data bytes.
`timescale 1ns / 1ps
module tb_sio_led;
    localparam HOLD = 3000;            // clocks; the clock is 10 ns here, so one SIO byte (520 us at 19200 baud) is scaled to 20 clocks
    reg clk = 0, rst = 1;
    reg [2:0] sio = 3'b000;            // {drive -> POKEY, POKEY -> drive, command}: the PIA's command output is low after reset
    wire led_n;
    trx_sio_led #(.HOLD(HOLD)) dut (.clk(clk), .rst(rst), .sio(sio), .led_n(led_n));
    always #5 clk = ~clk;
    integer errors = 0, i, low_run, k;
    task check(input ok, input [255*8:0] what);
        if (!ok) begin errors = errors + 1; $display("FAIL %0s", what); end else $display("ok   %0s", what);
    endtask
    task byte_tx(input integer wire_i);   // a start bit (falling edge) and a byte of 20 clocks on wire_i = 1 (POKEY -> drive) or 2 (drive -> POKEY)
        begin sio[wire_i] = 1'b0; repeat (2) @(posedge clk); sio[wire_i] = 1'b1; repeat (18) @(posedge clk); end
    endtask
    initial begin
        repeat (6) @(posedge clk);
        rst = 0;
        repeat (HOLD * 2) @(posedge clk);
        check(led_n === 1'b1, "command line low after reset, no edge: LED stays off");
        sio = 3'b111; repeat (HOLD * 2) @(posedge clk);
        check(led_n === 1'b1, "idle high: LED off");
        // the command frame
        sio[0] = 1'b0; @(posedge clk); @(posedge clk);
        check(led_n === 1'b0, "command line falls: LED on");
        for (i = 0; i < 5; i = i + 1) byte_tx(1);
        sio[0] = 1'b1;
        repeat (HOLD / 2) @(posedge clk);          // the application's turn-around
        check(led_n === 1'b0, "between the command frame and the answer: LED still on");
        byte_tx(2);                                // ACK
        repeat (HOLD / 2) @(posedge clk);
        byte_tx(2);                                // COMPLETE
        low_run = 0;
        for (k = 0; k < 128; k = k + 1) begin byte_tx(2); if (led_n === 1'b0) low_run = low_run + 1; end
        check(low_run == 128, "128 data bytes: LED on for every one");
        repeat (HOLD - 100) @(posedge clk);
        check(led_n === 1'b0, "just inside the hold after the last byte: LED on");
        repeat (300) @(posedge clk);
        check(led_n === 1'b1, "after the hold: LED off");
        // a reset clears the hold
        sio[0] = 1'b0; @(posedge clk); @(posedge clk); sio[0] = 1'b1;
        rst = 1; repeat (3) @(posedge clk); rst = 0; @(posedge clk);
        check(led_n === 1'b1, "reset: LED off");
        if (errors == 0) $display("PASS tb_sio_led errors=0"); else $display("FAIL tb_sio_led errors=%0d", errors);
        $finish;
    end
endmodule
