// T157 S1/S2: nes_pad with joy_swap. The host's joystick 2 (fire = A) must reach controller port 1 with the swap on and port 2 with it off;
// joystick 1 the other way. The serial protocol is the hardware's (strobe latches, a falling edge of rdclk shifts), so the bench reads $4016/$4017
// the way a game does: strobe high, low, then 8 reads of bit 0 first (A, B, Select, Start, Up, Down, Left, Right).
//   iverilog -g2012 -o tb_padswap cores/nes/sim/tb_padswap.v cores/nes/trx/nes_pad.v && vvp tb_padswap
`timescale 1ns / 1ps
module tb_padswap;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst = 1;
    reg [4:0] joy1 = 0, joy2 = 0;
    reg swap = 0, strobe = 0;
    reg [1:0] rdclk = 0;
    wire [4:0] d1, d2;
    nes_pad dut (.clk(clk), .rst(rst), .joy1(joy1), .joy2(joy2), .joy_swap(swap), .keys(64'd0), .app1(8'd0), .app2(8'd0), .strobe(strobe), .rdclk(rdclk), .data1(d1), .data2(d2));
    integer nbad = 0;
    reg [7:0] r1, r2;
    task read_ports;
        integer i;
        begin
            repeat (4) @(posedge clk);                       // the synchronisers
            strobe = 1; repeat (3) @(posedge clk); strobe = 0; repeat (3) @(posedge clk);
            for (i = 0; i < 8; i = i + 1) begin
                r1[i] = d1[0]; r2[i] = d2[0];
                rdclk = 2'b11; repeat (3) @(posedge clk); rdclk = 2'b00; repeat (3) @(posedge clk);
            end
        end
    endtask
    task expect8(input [383:0] name, input [7:0] got, input [7:0] want);
        begin
            if (got !== want) begin $display("FAIL: %0s = %02x, expected %02x", name, got, want); nbad = nbad + 1; end
            else $display("ok   %0s = %02x", name, got);
        end
    endtask
    initial begin
        repeat (4) @(posedge clk); rst = 0;
        // host joystick 2: fire (bit 4) = A; host joystick 1: up (bit 0) = Up (bit 4 of the serial byte)
        joy2 = 5'b10000; joy1 = 5'b00001; swap = 0; read_ports;
        expect8("swap off: port 1 (host joystick 1)", r1, 8'b00010000);
        expect8("swap off: port 2 (host joystick 2)", r2, 8'b00000001);
        swap = 1; read_ports;
        expect8("swap on: port 1 (host joystick 2)", r1, 8'b00000001);
        expect8("swap on: port 2 (host joystick 1)", r2, 8'b00010000);
        swap = 0; read_ports;
        expect8("swap off again: port 1", r1, 8'b00010000);
        if (nbad == 0) $display("PASS tb_padswap"); else $display("FAIL tb_padswap %0d", nbad);
        $finish;
    end
    initial begin #2000000; $display("FAIL timeout"); $finish; end
endmodule
